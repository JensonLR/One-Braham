// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamCharacter.h"

#include "OneBraham.h"
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
#include "Interaction/OneBrahamInteractComponent.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimInstance.h"

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
	FirstPersonMesh->SetRelativeLocation(FVector(12.f, 0.f, -8.f));
	FirstPersonMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

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

	if (IsLocallyControlled())
	{
		// The local player never sees their own body: the arms mesh handles that,
		// and hiding the body removes the classic "gun through the chest" artefact.
		GetMesh()->SetVisibility(false, true);
		FirstPersonMesh->SetVisibility(true, true);
	}
	else
	{
		FirstPersonMesh->SetVisibility(false, true);
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

	if (IsLocallyControlled())
	{
		GetMesh()->SetVisibility(false, true);
	}
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

	// Floor detection at 2 Hz - it only changes when someone takes the stairs.
	FloorCheckAccumulator += DeltaSeconds;
	if (FloorCheckAccumulator >= 0.5f)
	{
		FloorCheckAccumulator = 0.f;
		UpdateFloorIndex(DeltaSeconds);
	}

	// Anti-cheat speed reporting at 4 Hz. The client sends its own velocity so a
	// modified client is *forced to lie*, which is detectable; a client that
	// simply does not report is treated as suspicious by the server.
	if (HasAuthority() && !IsLocallyControlled())
	{
		SpeedReportAccumulator += DeltaSeconds;
		if (SpeedReportAccumulator >= 0.25f)
		{
			SpeedReportAccumulator = 0.f;

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

	if (FirstPersonCamera != nullptr)
	{
		// The camera slides sideways rather than rotating: a roll-only lean makes
		// the player feel seasick and gives no actual advantage.
		FVector Offset = bCrouchRequested ? CrouchedCameraOffset : StandingCameraOffset;
		Offset.Y += LeanAmount * LeanCameraSideOffset;
		FirstPersonCamera->SetRelativeLocation(FMath::VInterpTo(FirstPersonCamera->GetRelativeLocation(), Offset, DeltaSeconds, 12.f));

		// A tiny roll sells the shoulder movement without tilting the horizon.
		FRotator CameraRotation = FRotator::ZeroRotator;
		CameraRotation.Roll = LeanAmount * LeanCameraRoll;
		FirstPersonCamera->SetRelativeRotation(CameraRotation);
	}
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

	// Carrying a downed colleague halves your speed - handled in the downed comp.
	const float CarryPenalty = DownedComponent != nullptr && DownedComponent->IsCarryingBody() ? 0.5f : 1.f;

	return HealthPenalty * CarryPenalty;
}

// ==========================================================================
//  Floor tracking
// ==========================================================================
void AOneBrahamCharacter::UpdateFloorIndex(float DeltaSeconds)
{
	// The tower is authored with a fixed 4.0 m floor-to-floor height (see
	// Tools/Python/build_map.py, FLOOR_HEIGHT_M). Ground floor lobby floor is at
	// Z = 0, the Skydeck at Z = 15 * 400 = 6000 cm.
	// One number, defined once: see OneBrahamConstants in OneBraham.h.
	constexpr float FloorHeightCm = OneBrahamConstants::FloorHeightCm;

	const float ActorZ = GetActorLocation().Z;
	const int32 ComputedFloor = FMath::Clamp(FMath::FloorToInt(ActorZ / FloorHeightCm), 0, ONE_BRAHAM_FLOORS);

	if (ComputedFloor != CurrentFloorIndex)
	{
		CurrentFloorIndex = ComputedFloor;

		// Clients tell the server so blind/deaf HUD state stays correct without
		// the server tracing every pawn's feet.
		if (!HasAuthority())
		{
			ServerReportFloor(CurrentFloorIndex);
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
	Appearance.SquadColorIndex = GetPlayerState<AOneBrahamPlayerState>() ? FMath::Abs(GetPlayerState<AOneBrahamPlayerState>()->PlayerId) % 4 : 0;

	if (Definition->BodyMesh.IsValid() && GetMesh() != nullptr)
	{
		if (USkeletalMesh* Mesh = Definition->BodyMesh.LoadSynchronous())
		{
			GetMesh()->SetSkeletalMesh(Mesh);
		}
	}

	if (Definition->AnimInstanceClass != nullptr && GetMesh() != nullptr)
	{
		GetMesh()->SetAnimInstanceClass(Definition->AnimInstanceClass);
	}

	if (Definition->FirstPersonMesh.IsValid() && FirstPersonMesh != nullptr)
	{
		if (USkeletalMesh* ArmsMesh = Definition->FirstPersonMesh.LoadSynchronous())
		{
			FirstPersonMesh->SetSkeletalMesh(ArmsMesh);
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
	Appearance.EquippedCosmetics = InCosmetics;

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
}

void AOneBrahamCharacter::ApplyLifeStateVisuals(EOneBrahamLifeState NewState)
{
	const EOneBrahamLifeState OldState = LifeState;
	LifeState = NewState;

	// Collision follows the life state: a corpse must not block bullets into an
	// ongoing fight, but it should still be shootable for the "shot a body"
	// comedy stat and for hit reactions from stray rounds.
	switch (NewState)
	{
	case EOneBrahamLifeState::Alive:
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("OneBrahamCharacter"));
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		break;

	case EOneBrahamLifeState::Downed:
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("OneBrahamCharacter"));
		break;

	case EOneBrahamLifeState::Dead:
	case EOneBrahamLifeState::Spectating:
	case EOneBrahamLifeState::Extracted:
	default:
		// Ragdolls are handled by UOneBrahamRagdollComponent; the capsule is
		// disabled so it does not catch on geometry while the body flops.
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		break;
	}

	OnLifeStateChanged.Broadcast(OldState, NewState);
}

void AOneBrahamCharacter::OnRep_Appearance()
{
	ApplyArchetype(Appearance.ArchetypeTag);
	ApplyCosmetics(Appearance.EquippedCosmetics);
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

	// The camera dips when hurt: a purely cosmetic "flinch" that also gives the
	// player a readable (and audible) cue that they are losing the fight.
	if (FirstPersonCamera != nullptr && Delta < 0.f && IsLocallyControlled())
	{
		const float FlinchStrength = FMath::Clamp(-Delta / 40.f, 0.05f, 0.35f);
		FirstPersonCamera->SetRelativeRotation(FirstPersonCamera->GetRelativeRotation() + FRotator(-FlinchStrength * 2.f, 0.f, 0.f));
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

	// Notify the controller so the death screen + spectate flow starts.
	if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetController()))
	{
		PC->ShowDeathScreen(true);
		PC->ServerBeginSpectating(INDEX_NONE);
	}

	OnDamageReported.Broadcast(Killer, Report);

	// If the mode allows respawn (FIRE DRILL), queue one now.
	if (AOneBrahamGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AOneBrahamGameMode>() : nullptr)
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
		PlayerState ? *PlayerState->GetPlayerName() : *GetName(),
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

	ApplyLifeStateVisuals(EOneBrahamLifeState::Alive);

	if (RagdollComponent != nullptr)
	{
		RagdollComponent->ExitRagdoll();
	}

	if (HealthComponent != nullptr)
	{
		HealthComponent->ResetHealth();
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
		PC->ShowDeathScreen(false);
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
		PlayerState ? *PlayerState->GetPlayerName() : *GetName(), CurrentFloorIndex, EffectiveFallSpeed, Damage);
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
	// One number, defined once: see OneBrahamConstants in OneBraham.h.
	constexpr float FloorHeightCm = OneBrahamConstants::FloorHeightCm;
	const int32 ServerFloor = FMath::Clamp(FMath::FloorToInt(GetActorLocation().Z / FloorHeightCm), 0, ONE_BRAHAM_FLOORS);

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

	// This is the client's self-report. If a modified client claims a speed above
	// the authoritative cap, it is auto-flagged by the modified-client handshake
	// (see Docs/18) but we also log it here for the audit trail.
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
