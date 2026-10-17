// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamDeathRecap.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamCharacterDataAsset.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Characters/OneBrahamCharacterMovementComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamRagdollComponent.h"
#include "Characters/OneBrahamFirstPersonFeelComponent.h"
#include "Interaction/OneBrahamInteractComponent.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"

AOneBrahamCharacter::AOneBrahamCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UOneBrahamCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// --- Capsule ----------------------------------------------------------
	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("OneBrahamCharacter"));
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// --- Movement ---------------------------------------------------------
	OBCharacterMovement = Cast<UOneBrahamCharacterMovementComponent>(GetCharacterMovement());
	check(OBCharacterMovement != nullptr);

	// --- Third person body (MetaHuman) -----------------------------------
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetupAttachment(GetCapsuleComponent());
		BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -89.f));
		BodyMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		BodyMesh->SetCollisionProfileName(TEXT("OneBrahamHitbox"));
		BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BodyMesh->bCastDynamicShadow = true;
		BodyMesh->bCastHiddenShadow = true;
		BodyMesh->SetCastShadow(true);

		// Shadows from the first-person mesh are what stop the player's own arms
		// from glowing in the server room. The 1P mesh casts shadows only.
		BodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}

	// --- First person arms ------------------------------------------------
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(StandingCameraOffset);
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(90.f);
	FirstPersonCamera->PostProcessBlendWeight = 1.f;

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonCamera);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->bCastDynamicShadow = true;
	FirstPersonMesh->CastShadow = true;             // arms cast a shadow on the world
	FirstPersonMesh->bSelfShadowOnly = true;        // ...but not on themselves
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Mannequin space (+Y forward, +Z up, feet at the origin) into camera space:
	// -90 yaw turns the mesh to face down the camera, and the offset puts the
	// camera 9 cm above and 5 cm left of the mannequin's eye (head bone + Epic's
	// template camera offset, about (-4.6, 0.3, 153.5) in the ADS idles), so the
	// gun the idle holds up to the eye sits low and right of the crosshair
	// instead of over it. Not further forward: the rifle idle leans
	// into the stock, and its left shoulder would fill the bottom-left corner.
	FirstPersonMesh->SetRelativeLocation(FVector(-0.3f, 0.4f, -162.5f));
	FirstPersonMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	// Only the owner draws it, so only the owner should pay for its pose.
	FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	FirstPersonPrimaryPose = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS")));
	FirstPersonSidearmPose = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS")));
	FirstPersonHiddenBones = { TEXT("neck_01"), TEXT("thigh_l"), TEXT("thigh_r") };

	// --- Weapon-mounted flashlight (the server room's only defence) -------
	WeaponFlashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("WeaponFlashlight"));
	WeaponFlashlight->SetupAttachment(FirstPersonMesh);
	WeaponFlashlight->SetRelativeLocation(FVector(10.f, 12.f, -6.f));
	WeaponFlashlight->Intensity = 4200.f;
	WeaponFlashlight->AttenuationRadius = 2600.f;
	WeaponFlashlight->InnerConeAngle = 12.f;
	WeaponFlashlight->OuterConeAngle = 26.f;
	WeaponFlashlight->LightColor = FColor(255, 244, 226);
	WeaponFlashlight->SetVisibility(false);

	// --- Gameplay components ----------------------------------------------
	HealthComponent = CreateDefaultSubobject<UOneBrahamHealthComponent>(TEXT("HealthComponent"));
	DownedComponent = CreateDefaultSubobject<UOneBrahamDownedComponent>(TEXT("DownedComponent"));
	WeaponComponent = CreateDefaultSubobject<UOneBrahamWeaponComponent>(TEXT("WeaponComponent"));
	InventoryComponent = CreateDefaultSubobject<UOneBrahamInventoryComponent>(TEXT("InventoryComponent"));
	InteractComponent = CreateDefaultSubobject<UOneBrahamInteractComponent>(TEXT("InteractComponent"));
	RagdollComponent = CreateDefaultSubobject<UOneBrahamRagdollComponent>(TEXT("RagdollComponent"));
	FeelComponent = CreateDefaultSubobject<UOneBrahamFirstPersonFeelComponent>(TEXT("FirstPersonFeelComponent"));
	CreateDefaultSubobject<UOneBrahamProximityVoiceComponent>(TEXT("ProximityVoiceComponent"));

	// --- Networking --------------------------------------------------------
	// 30 Hz for the pawn: fast enough that movement is smooth, slow enough that
	// 16 players do not saturate the server's out bandwidth.
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
	bReplicates = true;
	bAlwaysRelevant = false;

	// The default FPS mesh is the owner's arms; the body mesh is the world one.
	GetMesh()->SetOwnerNoSee(false);
	GetMesh()->SetOnlyOwnerSee(false);
}

void AOneBrahamCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamCharacter, Appearance);
	DOREPLIFETIME(AOneBrahamCharacter, LifeState);
	// Lean is cosmetic but needs to be seen by everyone: a leaning enemy must be
	// visible around a corner, or leaning becomes a wallhack.
	DOREPLIFETIME(AOneBrahamCharacter, LeanAmount);
}

void AOneBrahamCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent != nullptr)
	{
		HealthComponent->OnHealthChanged.AddDynamic(this, &AOneBrahamCharacter::HandleHealthChanged);
		HealthComponent->OnDeath.AddDynamic(this, &AOneBrahamCharacter::HandleDeath);
	}

	if (WeaponComponent != nullptr)
	{
		WeaponComponent->OnWeaponChanged.AddDynamic(this, &AOneBrahamCharacter::HandleWeaponChanged);
	}

	// Resolve the registry on the server once, at spawn, so the archetype is
	// applied on the same frame the player state arrives rather than on whichever
	// call happens to touch appearance first.
	if (HasAuthority())
	{
		ResolveCharacterData();
	}

	// Seed the floor from the spawn Z so the HUD is correct on frame one.
	CurrentFloorIndex = 1;
	UpdateFloorIndex(0.f);

	UpdateFirstPersonVisibility();
}

void AOneBrahamCharacter::UpdateFirstPersonVisibility()
{
	// A bot is "locally controlled" on the server, so a listen host would hide
	// every bot's body without the player-controller check.
	const bool bLocalPlayerView = IsLocallyControlled() && Controller != nullptr && Controller->IsPlayerController();

	// The local player never sees their own body: the arms mesh handles that,
	// and hiding the body removes the classic "gun through the chest" artefact.
	// Everyone else - other players, spectators, the listen host looking at a
	// client - sees the body and never the arms.
	SetOwnMeshVisibility(GetMesh(), !bLocalPlayerView);

	// The death camera owns the arms while it runs (it hides them, and
	// RestoreFirstPersonView brings them back): a late client restart must
	// not put arms in front of a corpse's camera.
	const bool bDeathCam = FeelComponent != nullptr && FeelComponent->IsDeathCamActive();
	SetOwnMeshVisibility(FirstPersonMesh, bLocalPlayerView && !bDeathCam);
}

void AOneBrahamCharacter::SetOwnMeshVisibility(USceneComponent* Root, bool bVisible)
{
	if (Root == nullptr)
	{
		return;
	}

	// Not SetVisibility(x, true): that walks into the attached weapon actor and
	// the lights. Re-run on every cosmetic change, it would switch on every
	// flashlight and the held gun's pickup mesh, or leave a gun hidden on the
	// body when it moves to the arms. Only this pawn's own meshes (the cosmetic
	// layers) follow the body; the weapon manages its own visibility.
	Root->SetVisibility(bVisible, false);
	for (USceneComponent* Child : Root->GetAttachChildren())
	{
		if (Child != nullptr && Child->GetOwner() == this && Child->IsA<UMeshComponent>())
		{
			SetOwnMeshVisibility(Child, bVisible);
		}
	}
}

void AOneBrahamCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		ApplyArchetype(State->ArchetypeTag);
		ApplyCosmetics(State->EquippedCosmetics);
	}

	// Server side of possession: on a listen host this is the host's own pawn
	// becoming theirs (BeginPlay ran before the possess) or a remote player's
	// pawn that the host must see as a body.
	UpdateFirstPersonVisibility();
}

void AOneBrahamCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// Owning-machine side of possession: a remote client's BeginPlay usually runs
	// before Controller has replicated, so this is where its arms first appear.
	UpdateFirstPersonVisibility();
}

void AOneBrahamCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		ApplyArchetype(State->ArchetypeTag);
		ApplyCosmetics(State->EquippedCosmetics);
	}
}

void AOneBrahamCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateLean(DeltaSeconds);

	// The feel layer reads the camera the lean has just placed, so it runs
	// second: punch, bob, sway, the screen grade, the death camera.
	if (FeelComponent != nullptr)
	{
		FeelComponent->TickFeel(DeltaSeconds);
	}

	TickFootsteps();

	// Floor detection at 2 Hz - it only changes when someone takes the stairs.
	FloorCheckAccumulator += DeltaSeconds;
	if (FloorCheckAccumulator >= 0.5f)
	{
		FloorCheckAccumulator = 0.f;
		UpdateFloorIndex(DeltaSeconds);

		// Possession applies the archetype, but a player joining a match in
		// progress is possessed inside the game mode's Super::PostLogin, before
		// it picks their archetype; clients catch the tag in OnRep_Archetype, the
		// server never would, and a listen host would see that player as an empty
		// mesh with a gun hanging in the air. Catch the late tag here.
		if (HasAuthority())
		{
			const AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>();
			if (State != nullptr && State->ArchetypeTag.IsValid() && State->ArchetypeTag != Appearance.ArchetypeTag)
			{
				ApplyArchetype(State->ArchetypeTag);
			}
		}
	}

	// Anti-cheat, both halves of it, at 4 Hz.
	//
	// The client's half: the machine that is actually simulating this pawn sends
	// its own velocity. A client whose movement has been modified therefore has to
	// choose between lying to the server - which is compared against the
	// authoritative cap below - and going quiet, which is a violation in itself on
	// the server's own clock. That is the whole reason the report exists: it is not
	// that a cheat cannot lie, it is that it cannot cheaply do neither.
	if (!HasAuthority() && IsLocallyControlled() && !bBotControlled)
	{
		ClientSpeedReportAccumulator += DeltaSeconds;
		if (ClientSpeedReportAccumulator >= OneBrahamConstants::SpeedReportIntervalSeconds)
		{
			ClientSpeedReportAccumulator = 0.f;
			ServerReportSpeed(GetVelocity().Size2D());
		}
	}

	// The server's half: its own simulation against the cap, and the last report
	// it heard against the clock.
	if (HasAuthority() && !IsLocallyControlled() && !bBotControlled)
	{
		SpeedReportAccumulator += DeltaSeconds;
		if (SpeedReportAccumulator >= OneBrahamConstants::SpeedReportIntervalSeconds)
		{
			SpeedReportAccumulator = 0.f;

			const UWorld* World = GetWorld();
			const float Now = World != nullptr ? World->GetTimeSeconds() : 0.f;

			// Silence first: a pawn on the other end of a connection that has sent
			// nothing for eight report intervals is not a pawn with a bad
			// connection, it is a client that has stopped reporting.
			if (LastClientSpeedReportTime > 0.f &&
				(Now - LastClientSpeedReportTime) > OneBrahamConstants::MaxSpeedReportGapSeconds)
			{
				if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
				{
					PC->ReportViolation(FString::Printf(
						TEXT("No movement report for %.1f s (expected every %.2f s)"),
						Now - LastClientSpeedReportTime, OneBrahamConstants::SpeedReportIntervalSeconds), 3.f);
				}

				// Rate limit: the next flag is another full gap away, so a client
				// with a genuinely broken report channel is not flagged 4x a second.
				LastClientSpeedReportTime = Now;
			}

			const float ServerSpeed = GetVelocity().Size2D();
			const float Allowed = OBCharacterMovement != nullptr ? OBCharacterMovement->GetAuthoritativeMaxSpeed() : 800.f;

			// The server compares its own simulated speed against the pawn's cap.
			// A sustained 35% overspeed means the client is manipulating movement.
			if (ServerSpeed > Allowed * UOneBrahamGameSettings::Get().MaxMovementSpeedTolerance)
			{
				++ConsecutiveSpeedViolations;
				if (ConsecutiveSpeedViolations >= 4)
				{
					if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
					{
						PC->ReportViolation(FString::Printf(TEXT("Movement speed %.0f exceeds cap %.0f"), ServerSpeed, Allowed), 3.f);
					}
					ConsecutiveSpeedViolations = 0;

					// Snap the cheater back to legal velocity.
					GetCharacterMovement()->Velocity = GetVelocity().GetClampedToMaxSize2D(Allowed);
				}
			}
			else
			{
				ConsecutiveSpeedViolations = FMath::Max(0, ConsecutiveSpeedViolations - 1);
			}
		}
	}

	UpdateAnimationState();
}

void AOneBrahamCharacter::SetBotControlled(bool bInBotControlled)
{
	bBotControlled = bInBotControlled;

	// A bot has no microphone and no client to report to: the voice and anti-cheat
	// systems both check this flag rather than producing warnings about a player
	// who does not exist.
	UE_LOG(LogOneBraham, Verbose, TEXT("[OB] %s is now %s-controlled."),
		*GetName(), bBotControlled ? TEXT("bot") : TEXT("player"));
}

void AOneBrahamCharacter::UpdateAnimationState()
{
	// The anim BP reads these; keeping the names stable means animators never
	// need to ask for a code change when they add a blend.
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		if (UAnimInstance* AnimInstance = BodyMesh->GetAnimInstance())
		{
			// CharacterMovement already provides Speed/IsInAir/bIsCrouched; these
			// two are ONE BRAHAM specific.
			AnimInstance->SetRootMotionMode(ERootMotionMode::Type::IgnoreRootMotion);
		}
	}
}

// ==========================================================================
//  Movement input
// ==========================================================================
void AOneBrahamCharacter::HandleMoveInput(const FVector2D& Input)
{
	if (!Controller || Input.IsNearlyZero(0.01f))
	{
		if (Controller)
		{
			AddMovementInput(FVector::ZeroVector, 0.f);
		}
		return;
	}

	// Input arrives in the controller's yaw frame: W is "away from my eyes".
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(Forward, Input.Y);
	AddMovementInput(Right, Input.X);
}

void AOneBrahamCharacter::HandleJumpPressed()
{
	Jump();
}

void AOneBrahamCharacter::HandleJumpReleased()
{
	StopJumping();
}

void AOneBrahamCharacter::SetSprintRequested(bool bSprint)
{
	if (bSprintRequested == bSprint)
	{
		return;
	}

	bSprintRequested = bSprint;

	// Movement caps are server-authoritative; the client forwards the intent.
	if (!HasAuthority())
	{
		ServerSetSprint(bSprint);
	}

	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->SetSprinting(bSprint && !bWalking);
	}

	// Sprinting while aiming is not allowed: ADS forces the aim speed.
	if (bSprint && IsAimingDownSights() && WeaponComponent != nullptr)
	{
		WeaponComponent->SetAimingDownSights(false);
	}
}

void AOneBrahamCharacter::ToggleWalkState()
{
	bWalking = !bWalking;

	if (!HasAuthority())
	{
		ServerSetWalk(bWalking);
	}

	if (OBCharacterMovement != nullptr && bWalking)
	{
		OBCharacterMovement->SetSprinting(false);
	}
}

void AOneBrahamCharacter::ToggleCrouchState()
{
	SetCrouchRequested(!bCrouchRequested);
}

void AOneBrahamCharacter::SetCrouchRequested(bool bCrouch)
{
	if (bCrouchRequested == bCrouch)
	{
		return;
	}

	bCrouchRequested = bCrouch;

	if (!HasAuthority())
	{
		ServerSetCrouch(bCrouch);
	}

	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->SetCrouchRequested(bCrouch);
	}
}

void AOneBrahamCharacter::SetLeanInput(float Lean)
{
	// Lean is disabled while sprinting or airborne: no lean-spamming round corners.
	const float Clamped = (bSprintRequested || GetCharacterMovement()->IsFalling()) ? 0.f : FMath::Clamp(Lean, -1.f, 1.f);

	LeanInput = Clamped;

	if (!HasAuthority())
	{
		ServerSetLean(Clamped);
	}

	LeanAmount = Clamped;
}

void AOneBrahamCharacter::UpdateLean(float DeltaSeconds)
{
	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->SetLeanInput(LeanInput);
		LeanAmount = OBCharacterMovement->GetLeanAmount();
	}

	// While the death camera has the lens, the body does not get to move it.
	const bool bDeathCam = FeelComponent != nullptr && FeelComponent->IsDeathCamActive();

	if (FirstPersonCamera != nullptr && !bDeathCam)
	{
		// The camera slides sideways rather than rotating: a roll-only lean makes
		// the player feel seasick and gives no actual advantage. The slide stays
		// on the component because the aim ray starts from it.
		FVector Offset = bCrouchRequested ? CrouchedCameraOffset : StandingCameraOffset;
		Offset.Y += LeanAmount * LeanCameraSideOffset;
		FirstPersonCamera->SetRelativeLocation(FMath::VInterpTo(FirstPersonCamera->GetRelativeLocation(), Offset, DeltaSeconds, 12.f));

		// The small roll that sells the shoulder movement is NOT written here. The
		// camera uses pawn control rotation, so UCameraComponent::GetCameraView
		// sets its world rotation every frame and a relative roll never reached
		// the screen. The feel component adds it to the camera's additive offset,
		// together with every other cosmetic rotation (see GetLeanCameraRoll).
	}
}

void AOneBrahamCharacter::RestoreFirstPersonView()
{
	if (FirstPersonCamera == nullptr)
	{
		return;
	}

	FirstPersonCamera->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	FirstPersonCamera->SetRelativeLocationAndRotation(bCrouchRequested ? CrouchedCameraOffset : StandingCameraOffset, FRotator::ZeroRotator);
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->ClearAdditiveOffset();

	// EndDeathCam runs on the old body after the respawn has moved the controller
	// on; that corpse stays as the ragdoll left it rather than reappearing.
	if (IsLocallyControlled())
	{
		UpdateFirstPersonVisibility();
	}
}

void AOneBrahamCharacter::TickFootsteps()
{
	// The server has no ears; a client hears every pawn near it, its own included.
	UWorld* World = GetWorld();
	if (World == nullptr || GetNetMode() == NM_DedicatedServer || OBCharacterMovement == nullptr)
	{
		return;
	}

	// The dead do not walk. The downed crawl, and a crawl is audible on marble.
	if (LifeState != EOneBrahamLifeState::Alive && LifeState != EOneBrahamLifeState::Downed)
	{
		return;
	}

	// The audio manager lives on the local player's controller - the only one a
	// client has - so every pawn on this machine steps through it.
	AOneBrahamPlayerController* LocalPC = Cast<AOneBrahamPlayerController>(World->GetFirstPlayerController());
	UOneBrahamAudioManagerComponent* Audio = LocalPC != nullptr && LocalPC->IsLocalController() ? LocalPC->GetAudioManager() : nullptr;
	if (Audio == nullptr)
	{
		return;
	}

	const bool bLocalPlayer = IsLocallyControlled() && IsPlayerControlled();

	// Distance cull before any work: sixteen pawns stepping through a map lookup
	// every frame is cheap, but fifteen of them on other floors is pointless.
	if (!bLocalPlayer && LocalPC->PlayerCameraManager != nullptr &&
		FVector::DistSquared(LocalPC->PlayerCameraManager->GetCameraLocation(), GetActorLocation()) > FMath::Square(FootstepCullDistanceCm))
	{
		return;
	}

	const float Speed2D = GetVelocity().Size2D();
	const bool bInAir = !OBCharacterMovement->IsMovingOnGround();

	// Sprint is a local flag on the movement component; a remote pawn only has
	// its speed, which is the thing a sprint actually changes.
	const bool bSprinting = OBCharacterMovement->IsSprinting() || Speed2D > OBCharacterMovement->JogSpeed * 1.1f;

	Audio->TickFootstepAudio(this, OBCharacterMovement->GetCurrentSurface(), Speed2D, bIsCrouched, bSprinting, bInAir, bLocalPlayer);
}

void AOneBrahamCharacter::TryStartMantle()
{
	if (OBCharacterMovement == nullptr || HealthComponent == nullptr || !HealthComponent->IsAlive())
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerRequestMantle(GetActorLocation());
		return;
	}

	OBCharacterMovement->TryStartMantle();
}

void AOneBrahamCharacter::TryStartSlide()
{
	if (OBCharacterMovement == nullptr || HealthComponent == nullptr || !HealthComponent->IsAlive())
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerRequestSlide();
		return;
	}

	OBCharacterMovement->TryStartSlide();
}

// ==========================================================================
//  Eye / weapon trace origins
// ==========================================================================
FVector AOneBrahamCharacter::GetEyeLocation() const
{
	// Use the camera component directly so the value is identical on the server
	// and the client (the server has no camera rotation, so it uses the control
	// rotation which is replicated as the pawn's rotation).
	if (FirstPersonCamera != nullptr && IsLocallyControlled())
	{
		return FirstPersonCamera->GetComponentLocation();
	}

	const FVector CapsuleLocation = GetActorLocation();
	const float EyeHeight = bCrouchRequested ? CrouchedCameraOffset.Z : StandingCameraOffset.Z;

	// Lean shifts the eye sideways, which is exactly why leaning exposes you.
	const FVector Right = GetActorRightVector();
	return CapsuleLocation + FVector(0.f, 0.f, EyeHeight - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + Right * (LeanAmount * LeanCameraSideOffset * 0.6f);
}

FRotator AOneBrahamCharacter::GetEyeRotation() const
{
	if (Controller != nullptr)
	{
		return Controller->GetControlRotation();
	}
	return GetActorRotation();
}

FVector AOneBrahamCharacter::GetWeaponTraceOrigin() const
{
	// Traces start at the eye, not the muzzle: the muzzle offset causes the
	// classic "shot the doorframe I was standing behind" complaint, and the
	// muzzle-vs-eye difference is only visible at extreme close range.
	return GetEyeLocation();
}

FVector AOneBrahamCharacter::GetWeaponTraceDirection() const
{
	return GetEyeRotation().Vector();
}

bool AOneBrahamCharacter::IsAimingDownSights() const
{
	return WeaponComponent != nullptr && WeaponComponent->IsAimingDownSights();
}

float AOneBrahamCharacter::GetSprintSpeedMultiplier() const
{
	// Tired office workers do not sprint at full speed when they are hurt.
	// 84 HP (below the "you have been shot" line) costs 6% top speed.
	const float HealthAlpha = HealthComponent != nullptr && HealthComponent->GetMaxHealth() > 0.f
		? FMath::Clamp(HealthComponent->GetHealth() / HealthComponent->GetMaxHealth(), 0.f, 1.f)
		: 1.f;

	const float HealthPenalty = HealthAlpha < 0.85f ? FMath::Lerp(1.f, 0.88f, (0.85f - HealthAlpha) / 0.85f) : 1.f;

	// No carry penalty, because there is no carrying: ASCENT has no fireman's lift
	// (Docs/04 section 4). There used to be a penalty here multiplied by a
	// bCarryingBody flag that nothing in the project could set true - a speed
	// modifier that could never apply, wired to a replicated bool that was sent to
	// every client every time it did not change.

	return HealthPenalty;
}

// ==========================================================================
//  Floor tracking
// ==========================================================================
void AOneBrahamCharacter::UpdateFloorIndex(float DeltaSeconds)
{
	// The tower is authored on a fixed 4.0 m floor-to-floor pitch with the
	// ground floor at Z = -400 (Tools/Python/onebraham/build_environment.py,
	// _floor_z). OneBrahamConstants::FloorIndexForZ is the one conversion.

	const float ActorZ = GetActorLocation().Z;
	const int32 ComputedFloor = FMath::Clamp(OneBrahamConstants::FloorIndexForZ(ActorZ), 0, ONE_BRAHAM_FLOORS);

	if (ComputedFloor != CurrentFloorIndex)
	{
		CurrentFloorIndex = ComputedFloor;

		// Clients tell the server so blind/deaf HUD state stays correct without
		// the server tracing every pawn's feet.
		// Only the owning client can send a server RPC; every client also sees
		// other players and bots change floor, and the server tracks those itself.
		if (!HasAuthority())
		{
			if (IsLocallyControlled())
			{
				ServerReportFloor(CurrentFloorIndex);
			}
		}
		else if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
		{
			State->CurrentFloor = CurrentFloorIndex;
		}
	}
}

void AOneBrahamCharacter::SetCurrentFloorIndex(int32 NewFloor)
{
	CurrentFloorIndex = FMath::Clamp(NewFloor, 0, ONE_BRAHAM_FLOORS);

	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		State->CurrentFloor = CurrentFloorIndex;
	}
}

// ==========================================================================
//  Appearance
// ==========================================================================
UOneBrahamCharacterDataAsset* AOneBrahamCharacter::ResolveCharacterData()
{
	if (CharacterData != nullptr)
	{
		return CharacterData;
	}

	// The config-declared registry, so a project can move its content without a
	// code change.
	CharacterData = UOneBrahamGameSettings::Get().CharacterRegistry.LoadSynchronous();

	if (CharacterData == nullptr)
	{
		// The last resort: the path the content build always writes.
		CharacterData = LoadObject<UOneBrahamCharacterDataAsset>(
			nullptr, TEXT("/Game/OneBraham/Characters/DA_CharacterRegistry.DA_CharacterRegistry"));
	}

	if (CharacterData == nullptr && !bLoggedMissingCharacterData)
	{
		bLoggedMissingCharacterData = true;
		OB_WARN("No character registry: '%s' will use its Blueprint's meshes only. "
			"Run Scripts/BuildContent.bat to create DA_CharacterRegistry.", *GetName());
	}

	return CharacterData;
}

TSubclassOf<UAnimInstance> AOneBrahamCharacter::ChooseCarryAnimClass(EOneBrahamWeaponSlot Slot,
	TSubclassOf<UAnimInstance> Primary, TSubclassOf<UAnimInstance> Sidearm)
{
	return (Slot == EOneBrahamWeaponSlot::Sidearm && Sidearm != nullptr) ? Sidearm : Primary;
}

void AOneBrahamCharacter::HandleWeaponChanged(AOneBrahamWeaponBase* NewWeapon, AOneBrahamWeaponBase* OldWeapon)
{
	const EOneBrahamWeaponSlot Slot = (NewWeapon != nullptr && NewWeapon->Definition != nullptr)
		? NewWeapon->Definition->DefaultSlot : EOneBrahamWeaponSlot::Primary;
	UpdateFirstPersonPose(Slot);

	USkeletalMeshComponent* Body = GetMesh();
	if (Body == nullptr || PrimaryAnimClass == nullptr)
	{
		return;
	}
	const TSubclassOf<UAnimInstance> Wanted = ChooseCarryAnimClass(Slot, PrimaryAnimClass, SidearmAnimClass);
	// Re-setting the class rebuilds the anim instance; only do it on a real change.
	if (Body->GetAnimClass() != Wanted)
	{
		Body->SetAnimInstanceClass(Wanted);
	}
}

void AOneBrahamCharacter::UpdateFirstPersonPose(EOneBrahamWeaponSlot Slot)
{
	if (FirstPersonMesh == nullptr || FirstPersonMesh->GetSkeletalMeshAsset() == nullptr || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const TSoftObjectPtr<UAnimationAsset>& PosePtr = (Slot == EOneBrahamWeaponSlot::Sidearm && !FirstPersonSidearmPose.IsNull())
		? FirstPersonSidearmPose : FirstPersonPrimaryPose;
	UAnimationAsset* Pose = PosePtr.IsNull() ? nullptr : PosePtr.LoadSynchronous();
	if (Pose == nullptr || Pose == ActiveFirstPersonPose)
	{
		return;
	}

	ActiveFirstPersonPose = Pose;
	FirstPersonMesh->PlayAnimation(Pose, true);
}

void AOneBrahamCharacter::ApplyArchetype(FGameplayTag ArchetypeTag)
{
	if (!ArchetypeTag.IsValid())
	{
		return;
	}

	Appearance.ArchetypeTag = ArchetypeTag;

	UOneBrahamCharacterDataAsset* Registry = ResolveCharacterData();
	if (Registry == nullptr)
	{
		return;
	}

	// The data asset maps an archetype to: body mesh variant, face, hair, voice
	// set, and animation personality blend weights. See Docs/13.
	const FOneBrahamArchetypeDefinition* Definition = Registry->FindArchetype(ArchetypeTag);
	if (Definition == nullptr)
	{
		OB_WARN("No archetype definition for '%s' in '%s'.", *ArchetypeTag.ToString(), *Registry->GetName());
		return;
	}

	Appearance.BodyVariant = Definition->BodyVariant;
	Appearance.SquadColorIndex = GetPlayerState<AOneBrahamPlayerState>() ? FMath::Abs(GetPlayerState<AOneBrahamPlayerState>()->GetPlayerId()) % 4 : 0;

	// IsNull, not IsValid: IsValid is "already loaded", and a body that had not
	// been loaded by anything else yet was never applied.
	if (!Definition->BodyMesh.IsNull() && GetMesh() != nullptr)
	{
		if (USkeletalMesh* Mesh = Definition->BodyMesh.LoadSynchronous())
		{
			// Overrides are kept by index across a mesh swap, so a Manny outfit
			// would land on the wrong Quinn slots: start the new body clean.
			if (GetMesh()->GetSkeletalMeshAsset() != Mesh)
			{
				GetMesh()->EmptyOverrideMaterials();
			}
			GetMesh()->SetSkeletalMesh(Mesh);
		}
	}

	// Archetype clothing, keyed by material slot name because Manny and Quinn do
	// not share slot order. Applied before any material parameter below, which
	// would otherwise make MIDs of the bare body materials and lose the outfit.
	// A dedicated server draws nothing, so it does not load the outfits.
	USkeletalMeshComponent* Body = GetMesh();
	if (Body != nullptr && GetNetMode() != NM_DedicatedServer)
	{
		for (const TPair<FName, TSoftObjectPtr<UMaterialInterface>>& Pair : Definition->BodyMaterials)
		{
			const int32 MaterialIndex = Body->GetMaterialIndex(Pair.Key);
			if (MaterialIndex == INDEX_NONE || Pair.Value.IsNull())
			{
				continue;
			}

			UMaterialInterface* Material = Pair.Value.LoadSynchronous();
			if (Material == nullptr)
			{
				continue;
			}

			// ApplyArchetype re-runs on every appearance rep; a slot already
			// wearing this outfit (or the parameter MID made from it below) is
			// left alone rather than swapped for a fresh MID each time.
			UMaterialInterface* Current = Body->GetMaterial(MaterialIndex);
			const UMaterialInstanceDynamic* CurrentMID = Cast<UMaterialInstanceDynamic>(Current);
			if (Current == Material || (CurrentMID != nullptr && CurrentMID->Parent == Material))
			{
				continue;
			}

			Body->SetMaterial(MaterialIndex, Material);
		}
	}

	PrimaryAnimClass = Definition->AnimInstanceClass;
	SidearmAnimClass = Definition->SidearmAnimInstanceClass;
	if (Definition->AnimInstanceClass != nullptr && GetMesh() != nullptr)
	{
		const AOneBrahamWeaponBase* Held = WeaponComponent != nullptr ? WeaponComponent->GetCurrentWeapon() : nullptr;
		const EOneBrahamWeaponSlot Slot = (Held != nullptr && Held->Definition != nullptr) ? Held->Definition->DefaultSlot : EOneBrahamWeaponSlot::Primary;
		GetMesh()->SetAnimInstanceClass(ChooseCarryAnimClass(Slot, PrimaryAnimClass, SidearmAnimClass));
	}

	// The arms. No archetype has an arms-only mesh yet, so the body stands in,
	// headless and legless (FirstPersonHiddenBones), as Epic's
	// first-person template draws its arms with the whole mannequin. Without a
	// mesh here the component is empty, the weapon's hand socket does not exist
	// and the gun hangs from the camera itself.
	// A dedicated server draws nothing, so it does not load the arms.
	if (FirstPersonMesh != nullptr && GetNetMode() != NM_DedicatedServer)
	{
		const TSoftObjectPtr<USkeletalMesh>& ArmsPtr = !Definition->FirstPersonMesh.IsNull()
			? Definition->FirstPersonMesh : Definition->BodyMesh;
		USkeletalMesh* ArmsMesh = ArmsPtr.IsNull() ? nullptr : ArmsPtr.LoadSynchronous();
		if (ArmsMesh != nullptr && FirstPersonMesh->GetSkeletalMeshAsset() != ArmsMesh)
		{
			FirstPersonMesh->SetSkeletalMesh(ArmsMesh);
			for (const FName& Bone : FirstPersonHiddenBones)
			{
				if (FirstPersonMesh->GetBoneIndex(Bone) != INDEX_NONE)
				{
					FirstPersonMesh->HideBoneByName(Bone, EPhysBodyOp::PBO_None);
				}
			}

			// A new mesh starts with no pose: pick the hold up again.
			ActiveFirstPersonPose = nullptr;
			const AOneBrahamWeaponBase* HeldWeapon = WeaponComponent != nullptr ? WeaponComponent->GetCurrentWeapon() : nullptr;
			UpdateFirstPersonPose((HeldWeapon != nullptr && HeldWeapon->Definition != nullptr)
				? HeldWeapon->Definition->DefaultSlot : EOneBrahamWeaponSlot::Primary);
		}

		// The arms mesh draws the archetype's clothing too: same slots, same
		// materials, so a sleeve in first person matches the body others see.
		if (ArmsMesh != nullptr && GetMesh() != nullptr && ArmsMesh == GetMesh()->GetSkeletalMeshAsset())
		{
			for (int32 Index = 0; Index < GetMesh()->GetNumMaterials(); ++Index)
			{
				FirstPersonMesh->SetMaterial(Index, GetMesh()->GetMaterial(Index));
			}
		}
	}

	// Material parameter that drives the subtle body-shape variation (shoulders,
	// posture) so two Business Analysts are not clones of each other.
	if (GetMesh() != nullptr)
	{
		GetMesh()->SetScalarParameterValueOnMaterials(TEXT("BodyVariant"), static_cast<float>(Appearance.BodyVariant));
		GetMesh()->SetScalarParameterValueOnMaterials(TEXT("SquadMarker"), static_cast<float>(Appearance.SquadColorIndex));
	}
}

void AOneBrahamCharacter::ApplyCosmetics(const TMap<FName, FName>& InCosmetics)
{
	Appearance.EquippedCosmetics.Reset(InCosmetics.Num());
	for (const TPair<FName, FName>& Pair : InCosmetics)
	{
		Appearance.EquippedCosmetics.Add({ Pair.Key, Pair.Value });
	}

	// Cosmetic meshes are modular skeletal/static meshes parented to the body
	// (jacket, fleece, hi-vis, lanyard, badge, backpack). They are looked up from
	// the character data asset and applied as leader-pose components; see
	// Docs/13_Characters_Clothing.md for the full slot -> asset table.
	UOneBrahamCharacterDataAsset* Registry = ResolveCharacterData();

	if (Registry == nullptr || GetMesh() == nullptr)
	{
		return;
	}

	for (const TPair<FName, FName>& Pair : InCosmetics)
	{
		const FOneBrahamCosmeticSlot* Slot = Registry->FindCosmeticSlot(Pair.Key);
		if (Slot == nullptr)
		{
			continue;
		}

		if (const TSoftObjectPtr<USkeletalMesh>* MeshPtr = Slot->Options.Find(Pair.Value))
		{
			if (USkeletalMesh* CosmeticMesh = MeshPtr->LoadSynchronous())
			{
				Registry->ApplyCosmeticToMesh(GetMesh(), Slot, CosmeticMesh);
			}
		}
	}

	// A freshly created cosmetic component starts visible; the local player's
	// hidden body must take it with it, or a jacket floats round the camera.
	UpdateFirstPersonVisibility();
}

// ==========================================================================
//  Interaction: revive and execute (IOneBrahamInteractionInterface)
// ==========================================================================
FText AOneBrahamCharacter::GetColleagueName() const
{
	if (const AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		const FString Name = State->GetPlayerName();
		if (!Name.IsEmpty())
		{
			// Runtime data, so a literal-by-construction FText: the name is the
			// player's, and it is the same string the kill feed prints.
			return FText::FromString(Name);
		}
	}

	return NSLOCTEXT("OneBraham", "Colleague_Unknown", "a colleague");
}

FText AOneBrahamCharacter::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const UOneBrahamDownedComponent* Downed = DownedComponent;
	if (Downed == nullptr)
	{
		return FText::GetEmpty();
	}

	const FText Name = GetColleagueName();

	switch (Downed->GetInteractionFor(Interactor))
	{
	case EOneBrahamDownedInteraction::Revive:
		// The prompt names them because you are deciding whose sixty seconds to
		// spend four and a half of your own on, and because a squad that talks
		// says "get Okonkwo up", not "revive the body".
		return FText::Format(NSLOCTEXT("OneBraham", "Downed_Prompt_Revive", "Hold E - revive {0}"), Name);

	case EOneBrahamDownedInteraction::Execute:
		return FText::Format(NSLOCTEXT("OneBraham", "Downed_Prompt_Execute", "Hold E - finish {0}"), Name);

	default:
		// An empty prompt draws nothing at all: a standing colleague, a body seen
		// by a spectator, and a body whose revive budget is spent are all "nothing
		// in reach", and the HUD already knows how to draw that.
		return FText::GetEmpty();
	}
}

FGameplayTag AOneBrahamCharacter::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const UOneBrahamDownedComponent* Downed = DownedComponent;
	const EOneBrahamDownedInteraction Verb = Downed != nullptr
		? Downed->GetInteractionFor(Interactor)
		: EOneBrahamDownedInteraction::None;

	// The tag picks the input verb and the HUD icon: OB.Interaction.Revive is the
	// friendly hold, OB.Interaction.Execute the hostile one, and an invalid tag is
	// the generic interact glyph.
	return Verb == EOneBrahamDownedInteraction::Execute
		? OneBrahamTags::Interaction_Execute
		: (Verb == EOneBrahamDownedInteraction::Revive ? OneBrahamTags::Interaction_Revive : FGameplayTag());
}

bool AOneBrahamCharacter::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const UOneBrahamDownedComponent* Downed = DownedComponent;
	return Downed != nullptr && Downed->GetInteractionFor(Interactor) != EOneBrahamDownedInteraction::None;
}

bool AOneBrahamCharacter::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	// Both verbs are holds. There is no tap version of picking somebody up, and
	// there should not be: a revive you can do instantly is a revive that happens
	// mid-firefight with no risk, and ASCENT's whole tension is that helping costs
	// you the fight you were in.
	const UOneBrahamDownedComponent* Downed = DownedComponent;
	return Downed != nullptr && Downed->GetInteractionFor(Interactor) != EOneBrahamDownedInteraction::None;
}

float AOneBrahamCharacter::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const UOneBrahamDownedComponent* Downed = DownedComponent;
	if (Downed == nullptr)
	{
		return 0.f;
	}

	return Downed->GetInteractionHoldSeconds(Downed->GetInteractionFor(Interactor));
}

bool AOneBrahamCharacter::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	UOneBrahamDownedComponent* Downed = DownedComponent;
	if (Downed == nullptr || Interactor == nullptr)
	{
		return false;
	}

	const EOneBrahamDownedInteraction Verb = Downed->GetInteractionFor(Interactor);
	if (Verb == EOneBrahamDownedInteraction::None)
	{
		return false;
	}

	// A good revive is visible from the floor: the bar starts empty the moment a
	// colleague commits, so the player who is bleeding knows somebody came.
	if (Verb == EOneBrahamDownedInteraction::Revive)
	{
		Downed->SetReviveProgress(0.f, Interactor->GetPawn());
	}

	return true;
}

void AOneBrahamCharacter::OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01)
{
	UOneBrahamDownedComponent* Downed = DownedComponent;
	if (Downed == nullptr || Interactor == nullptr)
	{
		return;
	}

	// The execution has no progress to publish: two seconds of a hold whose only
	// feedback is the animation of the person doing it.
	if (Downed->GetInteractionFor(Interactor) == EOneBrahamDownedInteraction::Revive)
	{
		Downed->SetReviveProgress(Progress01, Interactor->GetPawn());
	}
}

void AOneBrahamCharacter::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	UOneBrahamDownedComponent* Downed = DownedComponent;
	if (Downed == nullptr || Interactor == nullptr || !HasAuthority())
	{
		return;
	}

	// Re-resolve: the hold took four and a half seconds, and in that time the body
	// can have bled out, been executed by somebody else, or been picked up by a
	// different squadmate. Whatever the answer is now is the answer.
	const EOneBrahamDownedInteraction Verb = Downed->GetInteractionFor(Interactor);

	if (!bCompleted)
	{
		// Let go, walked away, or shot: the bar empties and nothing happened. The
		// next hand to reach them starts from zero.
		if (Verb == EOneBrahamDownedInteraction::Revive)
		{
			Downed->SetReviveProgress(0.f, Interactor->GetPawn());
		}
		return;
	}

	switch (Verb)
	{
	case EOneBrahamDownedInteraction::Revive:
		Downed->ServerRevive(Interactor);
		break;

	case EOneBrahamDownedInteraction::Execute:
		Downed->ServerExecute(Interactor);
		break;

	default:
		// The body stood up, died or was extracted during the hold. Nothing to do,
		// and nothing logged: this is the normal race, not an error.
		break;
	}
}

void AOneBrahamCharacter::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
	OnInteractionEnd_Implementation(Interactor, /*bCompleted*/ false);
}

void AOneBrahamCharacter::SetInteractionHighlight_Implementation(bool bHighlighted)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (Body == nullptr)
	{
		return;
	}

	// Stencil 2 is the interact-outline pass (r.CustomDepth=3 in DefaultEngine.ini;
	// M_OB_Highlight reads it, built by build_materials.py and documented in
	// Docs/10 section 2.7). Only the body is outlined, never the first-person
	// arms: you are not an interactable to yourself.
	Body->SetRenderCustomDepth(bHighlighted);
	Body->SetCustomDepthStencilValue(bHighlighted ? 2 : 0);
}

namespace OneBrahamBodyCollision
{
	/** The client's word on a body, so a LAN smoke can show there is no invisible wall where somebody lies. */
	static void LogClientBodyCollision(const AOneBrahamCharacter* Character, EOneBrahamLifeState OldState, EOneBrahamLifeState NewState)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const bool bOn = Capsule != nullptr && Capsule->IsCollisionEnabled();
		UE_LOG(LogOneBrahamNet, Log, TEXT("[OB.NET] Body: '%s' %s -> %s here; capsule %s, pawns %s"),
			*Character->GetName(), *UEnum::GetValueAsString(OldState), *UEnum::GetValueAsString(NewState),
			bOn ? TEXT("on") : TEXT("off"),
			bOn && Capsule->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block ? TEXT("blocked") : TEXT("pass"));
	}
}
using OneBrahamBodyCollision::LogClientBodyCollision;

AOneBrahamCharacter::FCapsuleCollisionRule AOneBrahamCharacter::GetCapsuleCollisionRule(EOneBrahamLifeState State)
{
	FCapsuleCollisionRule Rule;

	switch (State)
	{
	case EOneBrahamLifeState::Alive:
		// The profile as authored: a person on their feet is a solid thing, and
		// the Interactable channel stays on Overlap so a colleague standing in
		// front of a door never hides it.
		break;

	case EOneBrahamLifeState::Downed:
		// A body on the floor blocks the interaction trace: that is what makes it
		// aimable for a revive or an execution (invariant: a person is something
		// you interact with when they are on the carpet, and nothing but scenery
		// when they are on their feet). It still takes rounds, through the
		// hitboxes first and this capsule as the fallback.
		//
		// It does not block other people. The crawl capsule is already down at
		// crouch height, but in a 150 cm stair door or a 120 cm spiral landing a
		// crawling body that is a wall is a plug: the squad behind it cannot reach
		// the stair, and neither can its own medic. Pawns step over and past it.
		Rule.InteractableResponse = ECR_Block;
		Rule.PawnResponse = ECR_Ignore;
		break;

	case EOneBrahamLifeState::Dead:
	case EOneBrahamLifeState::Spectating:
	case EOneBrahamLifeState::Extracted:
	default:
		// The ragdoll (UOneBrahamRagdollComponent) is the body now, on the
		// OneBrahamRagdoll profile that takes rounds and ignores pawns. The capsule
		// goes entirely, so it neither catches on geometry while the body flops nor
		// stands in a doorway as an invisible wall where the body used to be.
		Rule.Enabled = ECollisionEnabled::NoCollision;
		Rule.PawnResponse = ECR_Ignore;
		Rule.InteractableResponse = ECR_Ignore;
		Rule.WeaponResponse = ECR_Ignore;
		break;
	}

	return Rule;
}

void AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(UCapsuleComponent* Capsule, EOneBrahamLifeState State)
{
	if (Capsule == nullptr)
	{
		return;
	}

	const FCapsuleCollisionRule Rule = GetCapsuleCollisionRule(State);

	// The profile first, every time: it resets any per-channel override the last
	// state left behind (Downed's pawn and interact responses), so a revive is a
	// return to the authored capsule rather than to whatever the carpet set.
	Capsule->SetCollisionProfileName(TEXT("OneBrahamCharacter"));
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	if (Rule.Enabled == ECollisionEnabled::NoCollision)
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	Capsule->SetCollisionResponseToChannel(ECC_Pawn, Rule.PawnResponse);
	Capsule->SetCollisionResponseToChannel((ECollisionChannel)EOneBrahamTraceChannel::Interactable, Rule.InteractableResponse);
	Capsule->SetCollisionResponseToChannel((ECollisionChannel)EOneBrahamTraceChannel::Weapon, Rule.WeaponResponse);
	Capsule->SetCollisionEnabled(Rule.Enabled);
}

bool AOneBrahamCharacter::CanClientApplyLifeState(EOneBrahamLifeState NewState, bool bBodyIsCorpse)
{
	// A ragdolling body is a corpse until the server says otherwise, and the
	// server says so on the body itself (bRagdolling and LifeState, one bunch,
	// OnRep_LifeState). A PlayerState that has moved on to a respawned pawn can
	// still point at the corpse on a client for a few packets (the two actors
	// replicate in no fixed order): its Alive must not stand the corpse back up
	// as a solid capsule where it fell - an invisible wall nothing ever clears,
	// because the corpse's own LifeState never changes again to correct it.
	return !bBodyIsCorpse || (NewState != EOneBrahamLifeState::Alive && NewState != EOneBrahamLifeState::Downed);
}

void AOneBrahamCharacter::ApplyLifeStateVisuals(EOneBrahamLifeState NewState)
{
	const EOneBrahamLifeState OldState = LifeState;

	if (!HasAuthority()
		&& !CanClientApplyLifeState(NewState, RagdollComponent != nullptr && RagdollComponent->IsRagdolling()))
	{
		UE_LOG(LogOneBrahamNet, Log, TEXT("[OB.NET] Body: '%s' is a corpse here; %s from a stale PlayerState ignored"),
			*GetName(), *UEnum::GetValueAsString(NewState));
		return;
	}

	LifeState = NewState;

	// Collision follows the life state, on the server here and on every client
	// through OnRep_LifeState and the downed/ragdoll notifies.
	ApplyCapsuleCollisionForLifeState(GetCapsuleComponent(), NewState);

	if (!HasAuthority() && OldState != NewState)
	{
		// Clients get here from the PlayerState's and the downed component's
		// notifies, usually before OnRep_LifeState (which then has nothing left
		// to change).
		LogClientBodyCollision(this, OldState, NewState);
	}

	OnLifeStateChanged.Broadcast(OldState, NewState);
}

void AOneBrahamCharacter::OnRep_LifeState(EOneBrahamLifeState OldState)
{
	// The replicated value has already landed in LifeState; the capsule and the
	// listeners catch up with it. Downed and Alive also arrive via the downed
	// component's notify, which may have applied them first: applying twice is
	// idempotent, and Dead has no other way onto a client's capsule.
	ApplyCapsuleCollisionForLifeState(GetCapsuleComponent(), LifeState);

	if (OldState != LifeState)
	{
		LogClientBodyCollision(this, OldState, LifeState);
		OnLifeStateChanged.Broadcast(OldState, LifeState);
	}
}

void AOneBrahamCharacter::OnRep_Appearance()
{
	ApplyArchetype(Appearance.ArchetypeTag);
	TMap<FName, FName> Cosmetics;
	for (const FOneBrahamNamePairEntry& Entry : Appearance.EquippedCosmetics)
	{
		Cosmetics.Add(Entry.Key, Entry.Value);
	}
	ApplyCosmetics(Cosmetics);
}

void AOneBrahamCharacter::ApplyMatchSettings(UOneBrahamMatchSettingsAsset* Settings)
{
	if (Settings == nullptr || HealthComponent == nullptr)
	{
		return;
	}

	HealthComponent->InitializeHealth(Settings->StartingHealth);
	HealthComponent->HealthRegenPerSecond = Settings->HealthRegenPerSecond;
	HealthComponent->HealthRegenDelay = Settings->HealthRegenDelaySeconds;
}

// ==========================================================================
//  Death / respawn
// ==========================================================================
void AOneBrahamCharacter::HandleHealthChanged(float NewHealth, float MaxHealth, float Delta)
{
	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		State->Health = NewHealth;
		State->MaxHealth = MaxHealth;
	}

	// The hit flinch used to live here, and never worked: on a client this fires
	// from the health OnRep with Delta = 0, and the rotation it wrote was erased
	// by UpdateLean and by pawn control rotation within the frame. It is now
	// driven from AOneBrahamPlayerController::ClientDamageTaken, which carries the
	// amount and the attacker's position, into the feel component's punch spring.
}

FText AOneBrahamCharacter::BuildDeathCauseText(const FOneBrahamKillFeedEntry& Entry)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Killer"), FText::FromString(Entry.KillerName));
	Args.Add(TEXT("Victim"), FText::FromString(Entry.VictimName));
	Args.Add(TEXT("Weapon"), FText::FromName(Entry.WeaponId));

	// These two never have a killer, and each has its own line: checked before
	// the empty-killer fallback below, or they would read as the building's.
	if (Entry.Verb == EOneBrahamKillFeedVerb::OwnUtility)
	{
		return NSLOCTEXT("OneBraham", "DeathCause_OwnUtility",
			"Your own utility. Commendable.");
	}

	if (Entry.Verb == EOneBrahamKillFeedVerb::Left)
	{
		return NSLOCTEXT("OneBraham", "DeathCause_Left",
			"You left the building before the job was done.");
	}

	if (Entry.KillerName.IsEmpty())
	{
		// The building does not gloat. It states the position.
		return NSLOCTEXT("OneBraham", "DeathCause_Building",
			"ONE BRAHAM SECURITY - no hostile contact recorded.");
	}

	switch (Entry.Verb)
	{
	case EOneBrahamKillFeedVerb::Headshot:
		return FText::Format(NSLOCTEXT("OneBraham", "DeathCause_Headshot",
			"{Killer} - head shot"), Args);

	case EOneBrahamKillFeedVerb::Executed:
		return FText::Format(NSLOCTEXT("OneBraham", "DeathCause_Executed",
			"{Killer} - finished you off"), Args);

	case EOneBrahamKillFeedVerb::Building:
		return NSLOCTEXT("OneBraham", "DeathCause_Building",
			"ONE BRAHAM SECURITY - no hostile contact recorded.");

	default:
		return FText::Format(NSLOCTEXT("OneBraham", "DeathCause_Killed",
			"{Killer} - {Weapon}"), Args);
	}
}

void AOneBrahamCharacter::HandleDeath(AActor* Killer, const FOneBrahamDamageReport& Report)
{
	ServerOnDied(Killer ? Killer->GetInstigatorController() : nullptr, Report);
}

void AOneBrahamCharacter::ServerOnDied(AController* Killer, const FOneBrahamDamageReport& Report)
{
	if (!HasAuthority())
	{
		return;
	}

	// Drop everything: a death bag is what the next squad fights over.
	if (WeaponComponent != nullptr)
	{
		WeaponComponent->DropAllOnDeath();
	}

	// The card that got this player up eleven floors drops with the rest. ASCENT
	// is an escalation, and an escalation that survives its owner's death is not
	// one: a squad that dies at the top must not keep the building.
	if (InventoryComponent != nullptr)
	{
		InventoryComponent->ServerDropKeycardOnDeath();
	}

	if (InteractComponent != nullptr)
	{
		InteractComponent->EndInteraction();
	}

	ApplyLifeStateVisuals(EOneBrahamLifeState::Dead);

	if (RagdollComponent != nullptr)
	{
		RagdollComponent->EnterRagdoll(Report.HitBoneName, Report.ShotDirection, Report.FinalDamage);
	}

	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		State->LifeState = EOneBrahamLifeState::Dead;
		State->ServerRegisterDeath();
	}

	// The kill feed and the credit: the one place a kill is recorded, so the feed,
	// the scoreboard and the death screen cannot disagree about who killed whom.
	AOneBrahamGameMode* GameMode = GetWorld() != nullptr
		? GetWorld()->GetAuthGameMode<AOneBrahamGameMode>()
		: nullptr;

	FOneBrahamKillFeedEntry KillEntry;

	if (GameMode != nullptr)
	{
		KillEntry = GameMode->RegisterKill(this, Killer, Report);
	}

	// Notify the controller so the death camera, the death screen and then the
	// spectate flow start. Client RPCs: the death was detected here, on the
	// server, and the player who died is usually on another machine.
	//
	// The death camera comes first and spectating waits for it: a few seconds of
	// looking at the person who did it, from where you fell, is the beat that
	// makes a death land. The killer's pawn is sent so the camera can follow it
	// if it moves; the position is the fallback for a killer the client cannot
	// resolve, and for a death with no killer at all the camera simply sinks.
	if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
	{
		APawn* KillerPawn = Killer != nullptr ? Killer->GetPawn() : nullptr;
		if (KillerPawn == this)
		{
			KillerPawn = nullptr;
		}

		bool bHasFocus = KillerPawn != nullptr;
		FVector Focus = KillerPawn != nullptr ? KillerPawn->GetActorLocation() + FVector(0.f, 0.f, 55.f) : FVector::ZeroVector;

		if (!bHasFocus && Killer != nullptr && Report.Distance > 100.f)
		{
			// A killer whose pawn has gone (died in the same exchange, left):
			// walk back along the shot to where it came from.
			Focus = FVector(Report.HitLocation) - FVector(Report.ShotDirection) * Report.Distance;
			bHasFocus = true;
		}

		PC->ClientBeginDeathCam(this, KillerPawn, Focus, bHasFocus);

		// The recap goes first, so the death screen can set it under the cause.
		if (HealthComponent != nullptr)
		{
			float KillerHealth = -1.f;
			float KillerArmour = 0.f;
			if (const AOneBrahamCharacter* KillerCharacter = Cast<AOneBrahamCharacter>(KillerPawn))
			{
				if (const UOneBrahamHealthComponent* KillerHealthComp = KillerCharacter->GetHealthComponent())
				{
					KillerHealth = KillerHealthComp->GetHealth();
					KillerArmour = KillerHealthComp->GetArmour();
				}
			}
			PC->ClientSetDeathRecap(OneBrahamDeathRecap::Summarise(HealthComponent->GetRecentHits(),
				GetWorld()->GetTimeSeconds(), KillEntry.KillerName, KillerHealth, KillerArmour));
		}
		PC->ClientShowDeathScreen(true, BuildDeathCauseText(KillEntry));
		PC->ScheduleSpectatingAfterDeathCam();
	}

	OnDamageReported.Broadcast(Killer, Report);

	// If the mode allows respawn (FIRE DRILL), queue one now.
	if (GameMode != nullptr)
	{
		if (const UOneBrahamMatchSettingsAsset* Settings = GameMode->GetMatchSettings())
		{
			if (Settings->bRespawnEnabled && GetController() != nullptr)
			{
				GameMode->QueueRespawn(GetController(), Settings->RespawnDelaySeconds);
			}
		}

		GameMode->EvaluateMatchEndConditions();
	}

	OB_MATCH_LOG("'%s' was killed by '%s' on floor %d (%.1f damage, %s).",
		GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName(),
		Killer && Killer->PlayerState ? *Killer->PlayerState->GetPlayerName() : TEXT("the building"),
		CurrentFloorIndex,
		Report.FinalDamage,
		*UEnum::GetValueAsString(Report.HitZone));
}

void AOneBrahamCharacter::ServerOnRespawned()
{
	if (!HasAuthority())
	{
		return;
	}

	// A respawn is a fresh 4 Hz clock for the movement report as well: the client
	// that was mid-hitch when it died must not be flagged for the second it spent
	// on the death screen.
	if (const UWorld* World = GetWorld())
	{
		LastClientSpeedReportTime = World->GetTimeSeconds();
	}

	ApplyLifeStateVisuals(EOneBrahamLifeState::Alive);

	if (RagdollComponent != nullptr)
	{
		RagdollComponent->ExitRagdoll();
	}

	if (HealthComponent != nullptr)
	{
		HealthComponent->ResetHealth();
	}

	// Order matters: the inventory goes back to the standard-issue kit first, so
	// the loadout's utility is added on top of one of each rather than on top of
	// what the last life left behind. Utility does not accumulate across lives.
	if (InventoryComponent != nullptr)
	{
		InventoryComponent->ServerResetInventory(/*bKeepKeycards*/ false);
	}

	if (WeaponComponent != nullptr)
	{
		WeaponComponent->ResetOnRespawn();
	}

	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		State->LifeState = EOneBrahamLifeState::Alive;
	}

	if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
	{
		// A respawn inside the death camera's window must not be followed by a
		// spectate that was queued for the life that just ended.
		PC->CancelScheduledSpectating();
		PC->ClientShowDeathScreen(false, FText::GetEmpty());
		PC->bIsSpectating = false;
	}

	bSprintRequested = false;
	bCrouchRequested = false;
	bWalking = false;
	LeanInput = 0.f;

	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->SetMovementLocked(false);
		OBCharacterMovement->SetOBMovementState(EOneBrahamMovementMode::Normal);
	}

	SetCurrentFloorIndex(CurrentFloorIndex);
}

// ==========================================================================
//  Physics events
// ==========================================================================
void AOneBrahamCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Fall damage: the Skydeck is 78 m up, and this is the single most important
	// environmental story in the game - you *can* fall off the building, and it
	// is *not* survivable, but it is survivable from the mezzanine.
	const float FallSpeed = FMath::Abs(GetCharacterMovement()->Velocity.Z);

	// Velocity is already zeroed by the time Landed fires in some cases, so use
	// the last recorded falling speed from the movement component.
	const float EffectiveFallSpeed = FallSpeed > 0.f ? FallSpeed : FMath::Abs(GetCharacterMovement()->GetLastUpdateVelocity().Z);

	static constexpr float SafeFallSpeed = 1150.f;      // ~4 m fall: fine
	static constexpr float LethalFallSpeed = 2100.f;    // ~12 m fall: fatal

	// The landing is felt before it is scored: a dip, a nod and a shake on the
	// machine of the player who landed, scaled by how fast they came down. This
	// runs on the owning client's own movement simulation, so it is immediate.
	if (FeelComponent != nullptr && IsLocallyControlled() && IsPlayerControlled())
	{
		FeelComponent->NotifyLanded(EffectiveFallSpeed);
	}

	if (EffectiveFallSpeed <= SafeFallSpeed || HealthComponent == nullptr || !HealthComponent->IsAlive())
	{
		return;
	}

	const float Alpha = FMath::Clamp((EffectiveFallSpeed - SafeFallSpeed) / (LethalFallSpeed - SafeFallSpeed), 0.f, 1.f);
	const float Damage = FMath::Lerp(15.f, 100.f, Alpha * Alpha);

	FOneBrahamDamageReport Report;
	Report.BaseDamage = Damage;
	Report.FinalDamage = Damage;
	Report.HitZone = EOneBrahamHitZone::LowerLeg;
	Report.HitLocation = GetActorLocation();
	Report.ShotDirection = FVector(0.f, 0.f, -1.f);
	Report.bLethal = Damage >= HealthComponent->GetHealth();

	// Environmental damage is attributed to "the building" so the killfeed reads
	// "FELL 22 FLOORS" rather than crediting a player.
	HealthComponent->ApplyDamage(Damage, nullptr, Report, nullptr);

	OB_MATCH_LOG("'%s' landed hard on floor %d at %.0f cm/s (%.1f damage).",
		GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName(), CurrentFloorIndex, EffectiveFallSpeed, Damage);
}

void AOneBrahamCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	// Fell off the Skydeck. This must be an immediate, unambiguous death, and the
	// body must be cleaned up instantly so it does not fall forever on the server.
	if (HealthComponent != nullptr && HealthComponent->IsAlive())
	{
		FOneBrahamDamageReport Report;
		Report.BaseDamage = 1000.f;
		Report.FinalDamage = 1000.f;
		Report.HitZone = EOneBrahamHitZone::Torso;
		Report.bLethal = true;
		HealthComponent->ApplyDamage(1000.f, nullptr, Report, nullptr);
	}

	if (RagdollComponent != nullptr)
	{
		RagdollComponent->DestroyCorpseNow();
	}

	// Do NOT call Super: the engine would teleport the pawn back to the origin,
	// which for this building is inside the lobby and visible to everyone.
}

// ==========================================================================
//  Server RPCs
// ==========================================================================
void AOneBrahamCharacter::ServerSetSprint_Implementation(bool bSprint)
{
	if (!HasAuthority())
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController());
	if (PC != nullptr && !PC->CheckRPCBudget(TEXT("ServerSetSprint")))
	{
		return;
	}

	bSprintRequested = bSprint;
	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->SetSprinting(bSprint && !bWalking);
	}
}

void AOneBrahamCharacter::ServerSetWalk_Implementation(bool bWalk)
{
	if (!HasAuthority())
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController());
	if (PC != nullptr && !PC->CheckRPCBudget(TEXT("ServerSetWalk")))
	{
		return;
	}

	bWalking = bWalk;
	if (OBCharacterMovement != nullptr && bWalk)
	{
		OBCharacterMovement->SetSprinting(false);
	}
}

void AOneBrahamCharacter::ServerSetCrouch_Implementation(bool bCrouch)
{
	if (!HasAuthority())
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController());
	if (PC != nullptr && !PC->CheckRPCBudget(TEXT("ServerSetCrouch")))
	{
		return;
	}

	bCrouchRequested = bCrouch;
	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->SetCrouchRequested(bCrouch);
	}
}

void AOneBrahamCharacter::ServerSetLean_Implementation(float Lean)
{
	if (!HasAuthority())
	{
		return;
	}

	// Lean is Unreliable and cosmetic: no RPC budget cost, just a clamp. A client
	// spamming it cannot gain anything because the server re-clamps every frame
	// and the value only moves a camera.
	LeanInput = FMath::Clamp(Lean, -1.f, 1.f);
	LeanAmount = LeanInput;
}

void AOneBrahamCharacter::ServerRequestMantle_Implementation(FVector_NetQuantize LedgeLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController());
	if (PC != nullptr && !PC->CheckRPCBudget(TEXT("ServerRequestMantle")))
	{
		return;
	}

	// Anti-cheat: trust the server's own ledge detection, never the client's
	// reported ledge location. The client value is only used for logging.
	const FVector ClientClaim = LedgeLocation;
	if (FVector::DistSquared(ClientClaim, GetActorLocation()) > FMath::Square(400.f))
	{
		if (PC != nullptr)
		{
			PC->ReportViolation(TEXT("Mantle claim far from pawn"), 2.f);
		}
		return;
	}

	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->TryStartMantle();
	}
}

void AOneBrahamCharacter::ServerRequestSlide_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController());
	if (PC != nullptr && !PC->CheckRPCBudget(TEXT("ServerRequestSlide")))
	{
		return;
	}

	if (OBCharacterMovement != nullptr)
	{
		OBCharacterMovement->TryStartSlide();
	}
}

void AOneBrahamCharacter::ServerReportFloor_Implementation(int32 FloorIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	// Sanity check against the server's own view of the pawn's Z.
	const int32 ServerFloor = FMath::Clamp(OneBrahamConstants::FloorIndexForZ(GetActorLocation().Z), 0, ONE_BRAHAM_FLOORS);

	if (FMath::Abs(ServerFloor - FloorIndex) > 1)
	{
		// More than one floor of disagreement means the client is lying about
		// where it is (used by teleport/hitbox manipulation cheats).
		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
		{
			PC->ReportViolation(FString::Printf(TEXT("Floor desync: client=%d server=%d"), FloorIndex, ServerFloor), 4.f);
		}
		SetCurrentFloorIndex(ServerFloor);
		return;
	}

	SetCurrentFloorIndex(FloorIndex);
}

void AOneBrahamCharacter::ServerReportSpeed_Implementation(float ClientSpeed)
{
	if (!HasAuthority())
	{
		return;
	}

	// This is the client's self-report, and its arrival is also the heartbeat the
	// silence check on the server's tick watches: a pawn whose owner stopped
	// sending these is flagged on the server's own clock rather than on anything
	// the client says.
	if (const UWorld* World = GetWorld())
	{
		LastClientSpeedReportTime = World->GetTimeSeconds();
	}

	if (OBCharacterMovement == nullptr)
	{
		return;
	}

	const float Allowed = OBCharacterMovement->GetAuthoritativeMaxSpeed() * UOneBrahamGameSettings::Get().MaxMovementSpeedTolerance;
	if (ClientSpeed > Allowed)
	{
		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
		{
			PC->ReportViolation(FString::Printf(TEXT("Client self-reported speed %.0f > cap %.0f"), ClientSpeed, Allowed), 2.f);
		}
	}
}

void AOneBrahamCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// All input is bound by AOneBrahamPlayerController with Enhanced Input. This
	// override exists only so a designer can add a legacy debug binding on a BP
	// subclass without fighting the controller.
}
