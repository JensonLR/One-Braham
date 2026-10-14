// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCharacterMovementComponent.h
// ---------------------------------------------------------------------------
// Custom movement. ONE BRAHAM deliberately restrains movement so that gunfights
// are decided by positioning and trigger discipline rather than by movement
// tech. What we ship:
//   * Walk / sprint (no slide-cancel chains)
//   * Crouch (+ canted lean from cover)
//   * Mantle / vault over office furniture, desk dividers and Skydeck railings
//     (limited to 1.6 m: you may climb onto a desk, not up the outside of the
//     building)
//   * A single, short, momentum-based slide with a 4 s cooldown and a hard cap
//     of one slide per 2 s of sprinting - it is a dodge, not a traversal tool
//   * Wet-floor micro-stumble on marble/puddles (cosmetic + slight accel loss)
//   * Ladder-free stair climbing with step-up (stairs are the main vertical)
//
// Server authority: every one of these is decided on the server. The client
// sends intent (via the character) and reconciles with the server's state.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamCharacterMovementComponent.generated.h"

UENUM(BlueprintType)
enum class EOneBrahamMovementMode : uint8
{
	/** Normal walking/sprinting on the ground. */
	Normal = 0,
	/** Crouched. */
	Crouched = 1,
	/** Mid-mantle: movement is scripted, input is ignored. */
	Mantling = 2,
	/** Sliding. */
	Sliding = 3,
	/** Downed crawl (HP below the downed threshold in ASCENT). */
	DownedCrawl = 4,
	/** Being carried/finished - no movement input accepted. */
	Locked = 5
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamMovementModeChanged, EOneBrahamMovementMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamSurfaceChanged, EOneBrahamFootstepSurface, NewSurface);

UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UOneBrahamCharacterMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void UpdateBasedMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	// ======================================================================
	//  Tuning - the shipping values. All in cm/s unless stated.
	// ======================================================================
	/** Slow tactical walk. Silent-ish on carpet, quiet on marble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float WalkSpeed = 190.f;

	/** Default movement: purposeful, not fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float JogSpeed = 400.f;

	/** Sprint. Faster than a jog, but you cannot shoot and your footsteps are loud. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float SprintSpeed = 605.f;

	/** Crouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float CrouchSpeed = 165.f;

	/** Crouched and aiming. Its own number rather than CrouchSpeed * 0.85, because
	 *  a value written as an expression in a function is a value nobody can find
	 *  from the tuning table (Docs/04 section 2 quotes this property by name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float CrouchedAimingSpeed = 140.f;

	/** Aiming down sights: deliberately slow so ADS is a commitment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float AimingSpeed = 250.f;

	/** Downed crawl. Slow, desperate, audibly pathetic on marble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Speeds")
	float DownedCrawlSpeed = 60.f;

	/** Speed while a live grenade is being held? N/A - we do not do cooked grenades. */

	// --- Accel / braking --------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Accel")
	float GroundAcceleration = 1600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Accel")
	float GroundBrakingDeceleration = 2000.f;

	/**
	 * Air control is intentionally low: no bunny-hopping.
	 *
	 * NOT called `AirControl`. That name belongs to UCharacterMovementComponent,
	 * and this class used to declare its own member with it: the constructor's
	 * `AirControl = 0.14f;` then wrote to the shadowing member and the engine's
	 * property stayed at its default 0.05, so every jump in the game had a fifth
	 * of the control the tuning file and Docs/04 both promised. GetMaxSpeed()
	 * never read the member either. One name, one owner, and this one is read out
	 * loud by the constructor: `AirControl = GroundAirControl;`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Accel")
	float GroundAirControl = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Accel")
	float AirAcceleration = 300.f;

	// --- Slide ------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Slide")
	float SlideInitialSpeed = 720.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Slide")
	float SlideFriction = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Slide")
	float SlideDuration = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Slide")
	float SlideCooldown = 4.f;

	/** Sprint time required before a slide is allowed - stops slide spam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Slide")
	float SlideMinSprintTime = 2.f;

	/** Sliding down a stairwell is a feature; sliding uphill is not allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Slide")
	float SlideMaxUphillDegrees = 12.f;

	// --- Mantle -----------------------------------------------------------
	/** Maximum ledge height. 160 cm = desk, cubicle top, Skydeck glass base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Mantle")
	float MaxMantleHeight = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Mantle")
	float MinMantleHeight = 55.f;

	/** How far forward we probe for a ledge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Mantle")
	float MantleReach = 95.f;

	/** Seconds to complete the mantle; the curve is in the anim BP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Mantle")
	float MantleDuration = 0.62f;

	/** Cannot mantle while carrying an objective or while downed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Mantle")
	float MantleCooldown = 0.35f;

	// --- Stumble (wet surfaces) -------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Stumble")
	bool bEnableWetStumble = true;

	/** Chance per footstep on marble/puddle when sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Stumble")
	float WetStumbleChance = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Stumble")
	float StumbleDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Stumble")
	float StumbleSpeedMultiplier = 0.55f;

	// ======================================================================
	//  API
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	EOneBrahamMovementMode GetOBMovementMode() const { return MovementState; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	EOneBrahamMovementMode GetCachedMovementMode() const { return MovementState; }

	/**
	 * Sets the ONE BRAHAM movement state. Named differently from the engine's
	 * SetMovementMode so that internal engine calls are never ambiguous.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Movement")
	void SetOBMovementState(EOneBrahamMovementMode NewMode);

	/** Current surface under the character, updated each footstep. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	EOneBrahamFootstepSurface GetCurrentSurface() const { return CurrentSurface; }

	/** Resolves the physical material under the capsule into a surface enum. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Movement")
	EOneBrahamFootstepSurface ResolveSurfaceUnderfoot() const;

	/** True when the character may start a slide right now. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	bool CanStartSlide() const;

	/** Server-authoritative slide start. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Movement")
	bool TryStartSlide();

	/** Server-authoritative mantle start. Returns false if no ledge was found. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Movement")
	bool TryStartMantle();

	/** 0..1 progress through the current mantle, for the anim BP. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	float GetMantleAlpha() const { return MantleAlpha; }

	/** +1 = leaning right (E), -1 = leaning left (Q). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Movement")
	void SetLeanInput(float InLean);

	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	float GetLeanAmount() const { return LeanAmount; }

	/** Blocks all movement input (used while interacting with a terminal). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Movement")
	void SetMovementLocked(bool bLocked);

	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	bool IsMovementLocked() const { return bMovementLocked; }

	/** The authoritative speed cap the anti-cheat uses to sanity check client motion. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	float GetAuthoritativeMaxSpeed() const;

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Movement")
	FOneBrahamMovementModeChanged OnMovementModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Movement")
	FOneBrahamSurfaceChanged OnSurfaceChanged;

protected:
	/** Applies the surface-specific friction and stumble roll. */
	void UpdateSurfaceInteraction(float DeltaTime);

	/** Advances an in-progress mantle. */
	void TickMantle(float DeltaTime);

	/** Advances an in-progress slide. */
	void TickSlide(float DeltaTime);

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Movement")
	EOneBrahamMovementMode MovementState = EOneBrahamMovementMode::Normal;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Movement")
	EOneBrahamFootstepSurface CurrentSurface = EOneBrahamFootstepSurface::Concrete;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Movement")
	float LeanAmount = 0.f;

	float LeanTarget = 0.f;
	float MantleAlpha = 0.f;
	float MantleStartTime = 0.f;
	float SlideStartTime = 0.f;
	float LastSlideTime = -100.f;
	float SprintStartTime = -100.f;
	float StumbleEndTime = -100.f;

	FVector MantleStartLocation = FVector::ZeroVector;
	FVector MantleTargetLocation = FVector::ZeroVector;

	bool bMovementLocked = false;

	/** Set true by the character each tick while the player holds sprint. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Movement")
	bool bSprinting = false;

public:
	/** Called by AOneBrahamCharacter when the sprint key state changes. */
	void SetSprinting(bool bInSprinting);

	/** Called by the character when crouch state changes. */
	void SetCrouchRequested(bool bCrouch);

	/** Replicated lean/state payload used by the anim BP on remote clients. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Movement")
	bool IsSprinting() const { return bSprinting; }
};
