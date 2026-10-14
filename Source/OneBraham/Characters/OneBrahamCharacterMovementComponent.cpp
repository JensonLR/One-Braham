// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamCharacterMovementComponent.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace OneBrahamMovement
{
	/** Custom movement mode: scripted mantle. */
	static constexpr uint8 CMOVE_Mantle = 1;
	/** Custom movement mode: slide. */
	static constexpr uint8 CMOVE_Slide = 2;
}

// ==========================================================================
//  Saved moves: the intent that travels with every move
// ==========================================================================
namespace
{
	class FSavedMove_OneBraham : public FSavedMove_Character
	{
	public:
		typedef FSavedMove_Character Super;

		uint8 bSavedWantsToSprint : 1;
		uint8 bSavedWantsToWalk : 1;
		uint8 bSavedWantsToSlide : 1;
		uint8 bSavedWantsToMantle : 1;

		FSavedMove_OneBraham()
		{
			bSavedWantsToSprint = 0;
			bSavedWantsToWalk = 0;
			bSavedWantsToSlide = 0;
			bSavedWantsToMantle = 0;
		}

		virtual void Clear() override
		{
			Super::Clear();
			bSavedWantsToSprint = 0;
			bSavedWantsToWalk = 0;
			bSavedWantsToSlide = 0;
			bSavedWantsToMantle = 0;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Result = Super::GetCompressedFlags();
			Result |= bSavedWantsToSprint ? FLAG_Custom_0 : 0;
			Result |= bSavedWantsToWalk ? FLAG_Custom_1 : 0;
			Result |= bSavedWantsToSlide ? FLAG_Custom_2 : 0;
			Result |= bSavedWantsToMantle ? FLAG_Custom_3 : 0;
			return Result;
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
		{
			const FSavedMove_OneBraham* Other = static_cast<const FSavedMove_OneBraham*>(NewMove.Get());
			// A move that starts a slide or a mantle is never merged away.
			if (bSavedWantsToSprint != Other->bSavedWantsToSprint || bSavedWantsToWalk != Other->bSavedWantsToWalk
				|| bSavedWantsToSlide || Other->bSavedWantsToSlide || bSavedWantsToMantle || Other->bSavedWantsToMantle)
			{
				return false;
			}
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void SetMoveFor(ACharacter* InCharacter, float InDeltaTime, FVector const& NewAccel,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(InCharacter, InDeltaTime, NewAccel, ClientData);
			if (const UOneBrahamCharacterMovementComponent* Movement = Cast<UOneBrahamCharacterMovementComponent>(InCharacter->GetCharacterMovement()))
			{
				bSavedWantsToSprint = Movement->bWantsToSprint;
				bSavedWantsToWalk = Movement->bWantsToWalk;
				bSavedWantsToSlide = Movement->bWantsToSlide;
				bSavedWantsToMantle = Movement->bWantsToMantle;
			}
		}

		virtual void PrepMoveFor(ACharacter* InCharacter) override
		{
			Super::PrepMoveFor(InCharacter);
			if (UOneBrahamCharacterMovementComponent* Movement = Cast<UOneBrahamCharacterMovementComponent>(InCharacter->GetCharacterMovement()))
			{
				Movement->bWantsToSprint = bSavedWantsToSprint;
				Movement->bWantsToWalk = bSavedWantsToWalk;
				Movement->bWantsToSlide = bSavedWantsToSlide;
				Movement->bWantsToMantle = bSavedWantsToMantle;
			}
		}
	};

	class FNetworkPredictionData_Client_OneBraham : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FNetworkPredictionData_Client_OneBraham(const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return FSavedMovePtr(new FSavedMove_OneBraham());
		}
	};
}

FNetworkPredictionData_Client* UOneBrahamCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UOneBrahamCharacterMovementComponent* MutableThis = const_cast<UOneBrahamCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_OneBraham(*this);
	}
	return ClientPredictionData;
}

void UOneBrahamCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToWalk = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bWantsToSlide = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
	bWantsToMantle = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}

void UOneBrahamCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	// Intent into state, identically on the owning client and on the server.
	if (!bWantsToSprint)
	{
		bSprintSpentBySlide = false;
	}
	SetSprinting(bWantsToSprint && !bWantsToWalk && !bSprintSpentBySlide);

	if (bWantsToMantle)
	{
		bWantsToMantle = false;
		TryStartMantle();
	}
	if (bWantsToSlide)
	{
		bWantsToSlide = false;
		TryStartSlide();
	}
}

void UOneBrahamCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);

	// The move-time clocks, and the two scripted states, advance with the move.
	SlideCooldownRemaining = FMath::Max(0.f, SlideCooldownRemaining - DeltaSeconds);
	SprintElapsed = bSprinting ? SprintElapsed + DeltaSeconds : 0.f;

	if (MovementState == EOneBrahamMovementMode::Mantling)
	{
		TickMantle(DeltaSeconds);
	}
	else if (MovementState == EOneBrahamMovementMode::Sliding)
	{
		TickSlide(DeltaSeconds);
	}
}

UOneBrahamCharacterMovementComponent::UOneBrahamCharacterMovementComponent()
{
	// --- Engine defaults we deliberately change ---------------------------
	MaxWalkSpeed = JogSpeed;
	MaxWalkSpeedCrouched = CrouchSpeed;
	MaxAcceleration = GroundAcceleration;
	BrakingDecelerationWalking = GroundBrakingDeceleration;
	GroundFriction = 8.f;
	// The engine's AirControl, explicitly: see the note on GroundAirControl in the
	// header for the year this project spent writing to a member that shadowed it.
	AirControl = GroundAirControl;

	// Step-up: the tower is full of 18 cm stair risers and 40 cm desk lips.
	MaxStepHeight = 45.f;
	SetWalkableFloorAngle(46.f);
	PerchRadiusThreshold = 12.f;
	PerchAdditionalHeight = 8.f;

	// Jump: an ordinary human jump, not a Moon Boot.
	JumpZVelocity = 380.f;
	AirControlBoostMultiplier = 1.f;
	AirControlBoostVelocityThreshold = 0.f;
	FallingLateralFriction = 0.f;

	// Crouch: the office floors have 2.2 m ceilings and 1.1 m cubicle tops.
	NavAgentProps.bCanCrouch = true;
	CrouchedHalfHeight = 58.f;
	SetCrouchedHalfHeight(58.f);
	bCanWalkOffLedgesWhenCrouching = true;

	// No physics-based sliding on slopes: the marble lobby ramp is cosmetic.
	bUseFlatBaseForFloorChecks = true;
	bEnablePhysicsInteraction = false;

	// Network: high update frequency while alive keeps gunfights fair.
	NetworkSimulatedSmoothLocationTime = 0.075f;
	NetworkSimulatedSmoothRotationTime = 0.045f;
	ListenServerNetworkSimulatedSmoothLocationTime = 0.035f;
	ListenServerNetworkSimulatedSmoothRotationTime = 0.025f;
	bNetworkSmoothingComplete = true;
	NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
}

void UOneBrahamCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentSurface = ResolveSurfaceUnderfoot();
	MovementState = EOneBrahamMovementMode::Normal;
}

void UOneBrahamCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Lean smoothing: 8/s gives a decisive but readable shoulder exposure.
	LeanTarget = FMath::Clamp(LeanTarget, -1.f, 1.f);
	LeanAmount = FMath::FInterpTo(LeanAmount, LeanTarget, DeltaTime, 8.f);

	// Mantle and slide advance inside the move (UpdateCharacterStateAfterMovement),
	// so they are predicted and replayed with it.
	UpdateSurfaceInteraction(DeltaTime);
}

// ==========================================================================
//  Speed
// ==========================================================================
float UOneBrahamCharacterMovementComponent::GetMaxSpeed() const
{
	if (bMovementLocked)
	{
		return 0.f;
	}

	// Stumble overrides everything: the wet marble has decided your fate.
	if (GetWorld() != nullptr && GetWorld()->GetTimeSeconds() < StumbleEndTime)
	{
		return JogSpeed * StumbleSpeedMultiplier;
	}

	if (MovementState == EOneBrahamMovementMode::DownedCrawl)
	{
		return DownedCrawlSpeed;
	}

	switch (MovementState)
	{
	case EOneBrahamMovementMode::Sliding:
		// Sliding decays from the sprint speed to a walk over its duration.
		{
			const float Alpha = FMath::Clamp(SlideElapsed / FMath::Max(0.01f, SlideDuration), 0.f, 1.f);
			return FMath::Lerp(WalkSpeed, SprintSpeed, 1.f - Alpha);
		}

	case EOneBrahamMovementMode::Mantling:
		return 0.f; // fully scripted

	default:
		break;
	}

	// In the air we keep the speed we had; the cap comes from the ground state.
	if (IsFalling())
	{
		return FMath::Max(MaxWalkSpeed, SprintSpeed * 0.98f);
	}

	const AOneBrahamCharacter* OBCharacter = Cast<AOneBrahamCharacter>(CharacterOwner);
	const bool bAiming = OBCharacter != nullptr && OBCharacter->IsAimingDownSights();

	if (IsCrouching())
	{
		return bAiming ? CrouchedAimingSpeed : CrouchSpeed;
	}
	if (bAiming)
	{
		return AimingSpeed;
	}
	if (bSprinting && !bMovementLocked)
	{
		return SprintSpeed * (OBCharacter != nullptr ? OBCharacter->GetSprintSpeedMultiplier() : 1.f);
	}
	if (bWantsToWalk)
	{
		return WalkSpeed;
	}

	return JogSpeed;
}

float UOneBrahamCharacterMovementComponent::GetMaxAcceleration() const
{
	if (MovementState == EOneBrahamMovementMode::Mantling)
	{
		return 0.f;
	}
	return Super::GetMaxAcceleration();
}

bool UOneBrahamCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (bMovementLocked || MovementState == EOneBrahamMovementMode::Mantling ||
		MovementState == EOneBrahamMovementMode::DownedCrawl || IsCrouching())
	{
		// Crouched jumps are disabled: this is an office, not a parkour course,
		// and crouch-jumping is the classic way to get onto geometry the level
		// designer never intended to be reachable.
		return false;
	}

	return Super::DoJump(bReplayingMoves, DeltaTime);
}

void UOneBrahamCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	// Landing: apply a wet-marble stumble if the player was sprinting and lands
	// on a slick surface. This is the ONE BRAHAM "physics comedy" that is not a
	// joke: it is what actually happens when you run on wet marble.
	if (PreviousMovementMode == MOVE_Falling && MovementState == MOVE_Walking && bEnableWetStumble && bSprinting)
	{
		const EOneBrahamFootstepSurface LandingSurface = ResolveSurfaceUnderfoot();
		const bool bSlick = LandingSurface == EOneBrahamFootstepSurface::Marble ||
							LandingSurface == EOneBrahamFootstepSurface::Puddle ||
							LandingSurface == EOneBrahamFootstepSurface::Glass;

		if (bSlick && FMath::FRand() < WetStumbleChance * 1.5f)
		{
			StumbleEndTime = GetWorld() ? GetWorld()->GetTimeSeconds() + StumbleDuration : 0.f;
			OnSurfaceChanged.Broadcast(LandingSurface);
		}
	}
}

void UOneBrahamCharacterMovementComponent::UpdateBasedMovement(float DeltaSeconds)
{
	Super::UpdateBasedMovement(DeltaSeconds);
}

// ==========================================================================
//  Mode control
// ==========================================================================
void UOneBrahamCharacterMovementComponent::SetOBMovementState(EOneBrahamMovementMode NewMode)
{
	if (MovementState == NewMode)
	{
		return;
	}

	const EOneBrahamMovementMode OldMode = MovementState;
	MovementState = NewMode;

	switch (NewMode)
	{
	case EOneBrahamMovementMode::Normal:
		SetMovementMode(MOVE_Walking);
		break;

	case EOneBrahamMovementMode::Crouched:
		SetMovementMode(MOVE_Walking);
		if (CharacterOwner != nullptr && !CharacterOwner->bIsCrouched)
		{
			CharacterOwner->Crouch();
		}
		break;

	case EOneBrahamMovementMode::Mantling:
		SetMovementMode(MOVE_Custom, OneBrahamMovement::CMOVE_Mantle);
		break;

	case EOneBrahamMovementMode::Sliding:
		SetMovementMode(MOVE_Custom, OneBrahamMovement::CMOVE_Slide);
		break;

	case EOneBrahamMovementMode::DownedCrawl:
		SetMovementMode(MOVE_Walking);
		if (CharacterOwner != nullptr)
		{
			CharacterOwner->Crouch();
		}
		break;

	case EOneBrahamMovementMode::Locked:
	default:
		bMovementLocked = true;
		break;
	}

	OnMovementModeChanged.Broadcast(NewMode);
	OB_NET_LOG("Movement mode %s -> %s", *UEnum::GetValueAsString(OldMode), *UEnum::GetValueAsString(NewMode));
}

void UOneBrahamCharacterMovementComponent::SetSprinting(bool bInSprinting)
{
	if (bSprinting == bInSprinting)
	{
		return;
	}

	bSprinting = bInSprinting;
	SprintElapsed = 0.f;
}

void UOneBrahamCharacterMovementComponent::SetCrouchRequested(bool bCrouch)
{
	if (CharacterOwner == nullptr)
	{
		return;
	}

	if (bCrouch)
	{
		SetOBMovementState(EOneBrahamMovementMode::Crouched);
	}
	else
	{
		// Uncrouch only if there is headroom; otherwise stay crouched and let the
		// engine's own CanUnCrouch check resolve it next frame.
		if (CanUnCrouch())
		{
			CharacterOwner->UnCrouch();
			SetOBMovementState(EOneBrahamMovementMode::Normal);
		}
	}
}

void UOneBrahamCharacterMovementComponent::SetMovementLocked(bool bLocked)
{
	bMovementLocked = bLocked;

	if (bLocked)
	{
		Velocity = FVector::ZeroVector;
		if (CharacterOwner != nullptr)
		{
			CharacterOwner->SetActorLocation(CharacterOwner->GetActorLocation(), false);
		}
	}
}

float UOneBrahamCharacterMovementComponent::GetAuthoritativeMaxSpeed() const
{
	// The anti-cheat compares client-reported velocity against this value with
	// a tolerance (see UOneBrahamWeaponComponent / UOneBrahamCharacter's
	// ServerCheckMovement). Sprint is 605, plus slack for slope projection.
	return FMath::Max(SprintSpeed, MaxWalkSpeed) * 1.25f;
}

// ==========================================================================
//  Slide
// ==========================================================================
bool UOneBrahamCharacterMovementComponent::CanStartSlide() const
{
	if (CharacterOwner == nullptr)
	{
		return false;
	}

	if (MovementState == EOneBrahamMovementMode::Mantling ||
		MovementState == EOneBrahamMovementMode::DownedCrawl ||
		bMovementLocked || IsCrouching() || IsFalling())
	{
		return false;
	}

	if (SlideCooldownRemaining > 0.f)
	{
		return false;
	}

	// You must actually have been sprinting: this is what stops slide-spam from
	// becoming a movement mechanic.
	if (!bSprinting || SprintElapsed < SlideMinSprintTime)
	{
		return false;
	}

	// Sliding uphill is not a thing.
	const FVector FloorNormal = CurrentFloor.HitResult.Normal;
	if (!FloorNormal.IsNearlyZero() && FloorNormal.Z > -1.f)
	{
		const float UphillDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FloorNormal.Z, -1.f, 1.f)));
		if (UphillDegrees > SlideMaxUphillDegrees + 30.f) // normal Z ~ cos(angle to up)
		{
			return false;
		}
	}

	return true;
}

bool UOneBrahamCharacterMovementComponent::TryStartSlide()
{
	// Predicted: runs on the owning client and on the server from the same
	// saved move, so both start (or refuse) the same slide.
	if (!CanStartSlide())
	{
		return false;
	}

	SlideElapsed = 0.f;
	SlideCooldownRemaining = SlideCooldown;

	const FVector SlideDirection = Velocity.GetSafeNormal2D();
	Velocity = (SlideDirection.IsNearlyZero() ? CharacterOwner->GetActorForwardVector() : SlideDirection) * SlideInitialSpeed;

	SetOBMovementState(EOneBrahamMovementMode::Sliding);
	return true;
}

void UOneBrahamCharacterMovementComponent::TickSlide(float DeltaTime)
{
	SlideElapsed += DeltaTime;
	const float Elapsed = SlideElapsed;

	// Friction bleed: exponential decay reads more naturally than linear.
	Velocity *= FMath::Pow(FMath::Clamp(1.f - SlideFriction, 0.1f, 1.f), DeltaTime * 10.f);

	// End conditions: time, speed, input, or leaving the floor.
	const bool bTooSlow = Velocity.SizeSquared2D() < FMath::Square(WalkSpeed * 0.75f);
	const bool bTimedOut = Elapsed >= SlideDuration;

	if (bTimedOut || bTooSlow || IsFalling())
	{
		SetOBMovementState(IsCrouching() ? EOneBrahamMovementMode::Crouched : EOneBrahamMovementMode::Normal);
		// Sliding drains the sprint: the player must let go and sprint again.
		bSprintSpentBySlide = true;
		SetSprinting(false);
	}
}

// ==========================================================================
//  Mantle
// ==========================================================================
bool UOneBrahamCharacterMovementComponent::TryStartMantle()
{
	// Predicted like the slide: both machines run the same ledge traces on the
	// same move. The server's world is the one that decides if they disagree.
	if (CharacterOwner == nullptr)
	{
		return false;
	}

	if (MovementState == EOneBrahamMovementMode::Mantling ||
		MovementState == EOneBrahamMovementMode::DownedCrawl ||
		bMovementLocked)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// --- Find a ledge -----------------------------------------------------
	// Two traces: a forward trace at knee height finds the wall, then a
	// downward trace from above the wall finds the top surface.
	const FCollisionShape Capsule = CharacterOwner->GetCapsuleComponent()->GetCollisionShape();
	const FVector Origin = CharacterOwner->GetActorLocation();
	const FVector Forward = CharacterOwner->GetActorForwardVector();
	const float HalfHeight = Capsule.GetCapsuleHalfHeight();

	constexpr float ProbeHeights[] = { 40.f, 80.f, 120.f, 155.f };

	FHitResult WallHit;
	FVector WallLocation = FVector::ZeroVector;
	bool bFoundWall = false;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBMantle), false, CharacterOwner);

	for (const float ProbeHeight : ProbeHeights)
	{
		const FVector Start = Origin + FVector(0.f, 0.f, ProbeHeight - HalfHeight);
		const FVector End = Start + Forward * MantleReach;

		if (World->SweepSingleByChannel(WallHit, Start, End, FQuat::Identity, ECC_Visibility, Capsule, Params))
		{
			WallLocation = WallHit.ImpactPoint;
			bFoundWall = true;
			break; // lowest probe wins: we want the first thing you'd actually grab
		}
	}

	if (!bFoundWall)
	{
		return false;
	}

	// --- Find the top -----------------------------------------------------
	const FVector TopProbeStart = WallLocation + Forward * (Capsule.GetCapsuleRadius() + 12.f) + FVector(0.f, 0.f, MaxMantleHeight);
	const FVector TopProbeEnd = TopProbeStart - FVector(0.f, 0.f, MaxMantleHeight + 40.f);

	FHitResult TopHit;
	if (!World->LineTraceSingleByChannel(TopHit, TopProbeStart, TopProbeEnd, ECC_Visibility, Params))
	{
		return false;
	}

	const float LedgeHeight = TopHit.ImpactPoint.Z - (Origin.Z - HalfHeight);
	if (LedgeHeight < MinMantleHeight || LedgeHeight > MaxMantleHeight)
	{
		return false;
	}

	// Flat enough to stand on?
	if (TopHit.ImpactNormal.Z < 0.75f)
	{
		return false;
	}

	// --- Commit -----------------------------------------------------------
	MantleStartLocation = Origin;
	// Land 6 cm forward of the ledge edge so the capsule is fully on the surface.
	MantleTargetLocation = FVector(TopHit.ImpactPoint.X, TopHit.ImpactPoint.Y, TopHit.ImpactPoint.Z)
		+ Forward * 6.f
		+ FVector(0.f, 0.f, HalfHeight + 2.f);

	MantleElapsed = 0.f;
	MantleAlpha = 0.f;
	Velocity = FVector::ZeroVector;

	SetOBMovementState(EOneBrahamMovementMode::Mantling);

	// The anim BP plays the vault montage; this broadcast is what it listens to.
	OnMovementModeChanged.Broadcast(EOneBrahamMovementMode::Mantling);
	return true;
}

void UOneBrahamCharacterMovementComponent::TickMantle(float DeltaTime)
{
	if (CharacterOwner == nullptr)
	{
		return;
	}

	MantleElapsed += DeltaTime;
	const float Elapsed = MantleElapsed;
	const float LinearAlpha = FMath::Clamp(Elapsed / FMath::Max(0.05f, MantleDuration), 0.f, 1.f);

	// Two-phase curve: pull up (fast) then step forward (slower). Ease-in-out.
	const float Curve = LinearAlpha < 0.6f
		? FMath::InterpEaseInOut(0.f, 1.f, LinearAlpha / 0.6f, 2.f) * 0.85f
		: FMath::Lerp(0.85f, 1.f, FMath::InterpEaseInOut(0.f, 1.f, (LinearAlpha - 0.6f) / 0.4f, 2.f));

	MantleAlpha = Curve;

	const FVector Desired = FMath::Lerp(MantleStartLocation, MantleTargetLocation, Curve);
	FVector Delta = Desired - CharacterOwner->GetActorLocation();
	Delta.Z *= 1.f;
	Delta.XY *= 1.f;

	FHitResult Hit(1.f);
	// Sweep so a miscalculated ledge cannot push the player through a wall.
	SafeMoveUpdatedComponent(Delta, CharacterOwner->GetActorQuat(), /*bSweep*/ true, Hit);

	if (Hit.bBlockingHit && Hit.Time > 0.f)
	{
		// Blocked mid-mantle: abort to a falling state rather than clipping.
		SetOBMovementState(EOneBrahamMovementMode::Normal);
		return;
	}

	Velocity = Delta / FMath::Max(DeltaTime, 0.0001f);
	UpdateComponentVelocity();

	if (LinearAlpha >= 1.f)
	{
		SetOBMovementState(EOneBrahamMovementMode::Normal);
		bJustTeleported = false;
	}
}

void UOneBrahamCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	Super::PhysCustom(deltaTime, Iterations);

	switch (CustomMovementMode)
	{
	case OneBrahamMovement::CMOVE_Mantle:
		// Mantle motion is driven from TickMantle; physics must not fight it.
		break;

	case OneBrahamMovement::CMOVE_Slide:
		// Slide keeps ground friction but no acceleration: the player has no
		// steering authority while sliding, which is what makes it a commitment.
		if (deltaTime >= MIN_TICK_TIME && CharacterOwner != nullptr)
		{
			FHitResult Hit(1.f);
			SafeMoveUpdatedComponent(Velocity * deltaTime, UpdatedComponent->GetComponentQuat(), true, Hit);
			if (Hit.bBlockingHit)
			{
				SlideAlongSurface(Velocity * deltaTime, 1.f - Hit.Time, Hit.Normal, Hit, true);
			}
			UpdateComponentVelocity();
		}
		break;

	default:
		break;
	}
}

// ==========================================================================
//  Surfaces
// ==========================================================================
EOneBrahamFootstepSurface UOneBrahamCharacterMovementComponent::ResolveSurfaceUnderfoot() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (World == nullptr || Owner == nullptr)
	{
		return EOneBrahamFootstepSurface::Concrete;
	}

	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start - FVector(0.f, 0.f, 150.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBSurface), true, Owner);
	Params.bReturnPhysicalMaterial = true;
	Params.bTraceComplex = true;

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return EOneBrahamFootstepSurface::Concrete;
	}

	const UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
	if (PhysMat == nullptr)
	{
		return EOneBrahamFootstepSurface::Concrete;
	}

	// The physical surface enum values are assigned in Config/DefaultEngine.ini:
	// SurfaceType1 = Marble ... SurfaceType15 = BallisticGlass.
	switch (PhysMat->SurfaceType)
	{
	case SurfaceType1:  return EOneBrahamFootstepSurface::Marble;
	case SurfaceType2:  return EOneBrahamFootstepSurface::Carpet;
	case SurfaceType3:  return EOneBrahamFootstepSurface::Concrete;
	case SurfaceType4:  return EOneBrahamFootstepSurface::Metal;
	case SurfaceType5:  return EOneBrahamFootstepSurface::Glass;
	case SurfaceType6:  return EOneBrahamFootstepSurface::Wood;
	case SurfaceType7:  return EOneBrahamFootstepSurface::Puddle;
	case SurfaceType8:  return EOneBrahamFootstepSurface::Grating;
	case SurfaceType9:  return EOneBrahamFootstepSurface::Fabric;
	case SurfaceType10: return EOneBrahamFootstepSurface::ServerRack;
	case SurfaceType11: return EOneBrahamFootstepSurface::Plasterboard;
	case SurfaceType12: return EOneBrahamFootstepSurface::Lino;
	case SurfaceType13: return EOneBrahamFootstepSurface::Decking;
	case SurfaceType14: return EOneBrahamFootstepSurface::Flesh;
	case SurfaceType15: return EOneBrahamFootstepSurface::BallisticGlass;
	default:            return EOneBrahamFootstepSurface::Concrete;
	}
}

void UOneBrahamCharacterMovementComponent::UpdateSurfaceInteraction(float DeltaTime)
{
	if (!IsMovingOnGround())
	{
		return;
	}

	// Only re-trace when the character has actually moved a meaningful distance:
	// a downward trace every frame per player is 960 traces/second for 16 players.
	static constexpr float SurfaceProbeDistanceSq = FMath::Square(45.f);
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	const FVector CurrentLocation = Owner->GetActorLocation();
	static thread_local TWeakObjectPtr<const AActor> LastProbedOwner;
	static thread_local FVector LastProbedLocation = FVector(FLT_MAX);

	if (LastProbedOwner.Get() != Owner || FVector::DistSquared(LastProbedLocation, CurrentLocation) > SurfaceProbeDistanceSq)
	{
		LastProbedOwner = Owner;
		LastProbedLocation = CurrentLocation;

		const EOneBrahamFootstepSurface NewSurface = ResolveSurfaceUnderfoot();
		if (NewSurface != CurrentSurface)
		{
			CurrentSurface = NewSurface;
			OnSurfaceChanged.Broadcast(NewSurface);
		}
	}
}

void UOneBrahamCharacterMovementComponent::SetLeanInput(float InLean)
{
	// Lean is a small shoulder movement, not a peek-around-a-corner exploit:
	// the caps are applied in the camera offset (see AOneBrahamCharacter).
	LeanTarget = FMath::Clamp(InLean, -1.f, 1.f);
}
