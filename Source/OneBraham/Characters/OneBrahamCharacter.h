// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCharacter.h
// ---------------------------------------------------------------------------
// The player pawn. A tired London office worker with a first-person camera, a
// 3P body that everyone else sees, and a set of components that own the parts
// of the game we care about:
//
//   Health      - damage, regen, invulnerability windows, death
//   Downed      - ASCENT downed state, crawl, bleed-out, revive, execution
//   Weapons     - holstered weapons, fire control, attachments, pickups
//   Inventory   - utility, keycards, ammo pool
//   Interact    - doors, terminals, lifts, revives, looting
//   Ragdoll     - death physics, hit reactions, corpse cleanup
//
// Anti-cheat: every input that changes state is sent as a *request* to the
// server (Server_* RPCs, rate limited by AOneBrahamPlayerController) and the
// server is the only place that mutates replicated state. The client only ever
// predicts visuals, never damage or access.

#pragma once

#include "CoreMinimal.h"
#include "OneBraham.h"
#include "GameFramework/Character.h"
#include "Core/OneBrahamTypes.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "OneBrahamCharacter.generated.h"

class UCameraComponent;
class USkeletalMeshComponent;
class UOneBrahamCharacterMovementComponent;
class UOneBrahamHealthComponent;
class UOneBrahamDownedComponent;
class UOneBrahamWeaponComponent;
class AOneBrahamWeaponBase;
class UAnimInstance;
class UAnimationAsset;
class UOneBrahamInventoryComponent;
class UOneBrahamInteractComponent;
class UOneBrahamRagdollComponent;
class UOneBrahamFirstPersonFeelComponent;
class UOneBrahamCharacterDataAsset;
class UOneBrahamMatchSettingsAsset;
class USpotLightComponent;

/** Cosmetic-only description of how this character looks. Replicated once. */
USTRUCT(BlueprintType)
struct FOneBrahamCharacterAppearance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Appearance")
	FGameplayTag ArchetypeTag;

	/** Slot -> cosmetic asset id, as entries: this struct replicates, and TMap
	 *  does not (see FOneBrahamNamePairEntry). */
	UPROPERTY(BlueprintReadOnly, Category = "Appearance")
	TArray<FOneBrahamNamePairEntry> EquippedCosmetics;

	/** Which MetaHuman/body mesh variant (0-3) this archetype uses. */
	UPROPERTY(BlueprintReadOnly, Category = "Appearance")
	int32 BodyVariant = 0;

	/** Tick this in the lobby to synchronise "same outfit" squads. */
	UPROPERTY(BlueprintReadOnly, Category = "Appearance")
	int32 SquadColorIndex = 0;
};

UCLASS(BlueprintType)
class ONEBRAHAM_API AOneBrahamCharacter : public ACharacter, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	virtual void OnRep_PlayerState() override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// ======================================================================
	//  Components
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamCharacterMovementComponent* GetOBCharacterMovement() const { return OBCharacterMovement; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamDownedComponent* GetDownedComponent() const { return DownedComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamInteractComponent* GetInteractComponent() const { return InteractComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamRagdollComponent* GetRagdollComponent() const { return RagdollComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	/** Camera punch, sway, bob, landing, screen grade and the death camera (local only). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	UOneBrahamFirstPersonFeelComponent* GetFeelComponent() const { return FeelComponent; }

	/** Degrees of camera roll at full lean; the feel layer applies it. */
	float GetLeanCameraRoll() const { return LeanCameraRoll; }

	/**
	 * Puts the first-person camera back on the body at eye height, under pawn
	 * control rotation, with the arms visible again. The death camera detaches
	 * the camera; a respawn reuses this pawn, so something has to re-attach it.
	 */
	void RestoreFirstPersonView();

	/** The MetaHuman body everyone else sees. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	USkeletalMeshComponent* GetThirdPersonMesh() const { return GetMesh(); }

	/** The arms + weapon mesh only the owner sees. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	// ======================================================================
	//  Interaction: a colleague on the carpet is an interactable
	// ======================================================================
	//
	// WHY THE PAWN IMPLEMENTS THIS
	//   The interact ray finds actors, not components, and a downed colleague is
	//   an actor: the same trace that finds a fire door finds the body lying in
	//   front of it. The verbs belong to the downed component (it owns the rules
	//   and the state), so everything below is an adapter that asks it one
	//   question - GetInteractionFor() - and turns the answer into a prompt, a
	//   tag and a hold time. There is no second copy of the friendly/hostile rule
	//   anywhere in this file.
	//
	//   Before this existed the game shipped a sixty-second bleed-out, a revive
	//   and an execution that no player could ever reach: ServerRevive and
	//   ServerExecute had no caller, and the trace could not see a body anyway
	//   (the capsule's Interactable response was Overlap, the hitboxes' default
	//   was Block). See Docs/13 section 9, which is the interaction surface - this
	//   citation used to point at section 5, which is Cloth.

	/** The colleague's name for the prompt: the PlayerState's, so it matches the kill feed. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Interaction")
	FText GetColleagueName() const;

	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

	// ======================================================================
	//  Camera / eye helpers used by the controller and weapons
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	FVector GetEyeLocation() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	FRotator GetEyeRotation() const;

	/** Interaction reach: 220 cm (arms-length) as standard. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	float GetInteractRange() const { return OneBrahamConstants::InteractRangeCm; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	bool IsAimingDownSights() const;

	/** 1.0 normally; lower when injured or carrying, which is the whole point. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	float GetSprintSpeedMultiplier() const;

	// ======================================================================
	//  Movement intent (called by AOneBrahamPlayerController)
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void HandleMoveInput(const FVector2D& Input);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void HandleJumpPressed();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void HandleJumpReleased();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void SetSprintRequested(bool bSprint);

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	bool IsSprintRequested() const { return bSprintRequested; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ToggleWalkState();

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	bool IsWalking() const { return bWalking; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ToggleCrouchState();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void SetCrouchRequested(bool bCrouch);

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	bool IsCrouchRequested() const { return bCrouchRequested; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void SetLeanInput(float Lean);

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	float GetLeanInput() const { return LeanInput; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void TryStartMantle();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void TryStartSlide();

	// ======================================================================
	//  Floor tracking (HUD objective + audio mixing + spawn rules)
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	int32 GetCurrentFloorIndex() const { return CurrentFloorIndex; }

	/** Server-side move request; the client never sets this directly. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void SetCurrentFloorIndex(int32 NewFloor);

	// ======================================================================
	//  Cosmetic application (server -> all clients)
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ApplyArchetype(FGameplayTag ArchetypeTag);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ApplyCosmetics(const TMap<FName, FName>& InCosmetics);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ApplyLifeStateVisuals(EOneBrahamLifeState NewState);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ApplyMatchSettings(UOneBrahamMatchSettingsAsset* Settings);

	/** Called by the GameMode after a respawn: resets state, holsters weapons, re-enables input. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ServerOnRespawned();

	/** Called by the GameMode on death: drops weapons, enables ragdoll, hands over to the corpse. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void ServerOnDied(AController* Killer, const FOneBrahamDamageReport& Report);

	/**
	 * The line under the death screen title, built from the kill-feed entry.
	 * Static and pure so a test can assert the wording without a world.
	 */
	static FText BuildDeathCauseText(const FOneBrahamKillFeedEntry& Entry);

	// ======================================================================
	//  Weapon trace origin (server-side hit validation uses this exact value)
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	FVector GetWeaponTraceOrigin() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Character")
	FVector GetWeaponTraceDirection() const;

	// ======================================================================
	//  Replicated state
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "One Braham|Character")
	FOneBrahamCharacterAppearance Appearance;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Character")
	EOneBrahamLifeState LifeState = EOneBrahamLifeState::Alive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Character")
	float LeanAmount = 0.f;

	/** Broadcast so the anim BP can react on remote clients. */
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Character")
	FOneBrahamLifeStateChanged OnLifeStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Character")
	FOneBrahamDamaged OnDamageReported;

	// ======================================================================
	//  Server RPCs (all rate limited through AOneBrahamPlayerController)
	// ======================================================================
	UFUNCTION(Server, Reliable, Category = "One Braham|Character")
	void ServerSetSprint(bool bSprint);

	UFUNCTION(Server, Reliable, Category = "One Braham|Character")
	void ServerSetWalk(bool bWalk);

	UFUNCTION(Server, Reliable, Category = "One Braham|Character")
	void ServerSetCrouch(bool bCrouch);

	UFUNCTION(Server, Unreliable, Category = "One Braham|Character")
	void ServerSetLean(float Lean);

	UFUNCTION(Server, Reliable, Category = "One Braham|Character")
	void ServerRequestMantle(FVector_NetQuantize LedgeLocation);

	UFUNCTION(Server, Reliable, Category = "One Braham|Character")
	void ServerRequestSlide();

	/** Client tells the server its floor so the HUD/audio can update without a full trace. */
	UFUNCTION(Server, Unreliable, Category = "One Braham|Character")
	void ServerReportFloor(int32 FloorIndex);

	/** Anti-cheat: the client reports its own speed; the server compares with its own view. */
	UFUNCTION(Server, Unreliable, Category = "One Braham|Character")
	void ServerReportSpeed(float ClientSpeed);

	/**
	 * True when an AOneBrahamBotController drives this character.
	 *
	 * Bots are a practice-mode feature (ASCENT ships with no AI opponents). The
	 * flag exists so the parts of the game that assume a human on the other end -
	 * voice, the anti-cheat reporters, the scoreboard's "player" language - can
	 * behave sensibly instead of logging nonsense about a person who is not there.
	 */
	virtual bool IsBotControlled() const override { return bBotControlled; }

	/**
	 * The animation set for a weapon slot: the sidearm set for a sidearm when
	 * there is one, otherwise the primary set. Pure, for tests.
	 */
	static TSubclassOf<UAnimInstance> ChooseCarryAnimClass(EOneBrahamWeaponSlot Slot,
		TSubclassOf<UAnimInstance> Primary, TSubclassOf<UAnimInstance> Sidearm);

	/** Server: set by AOneBrahamBotController when it possesses/unpossesses. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Character")
	void SetBotControlled(bool bInBotControlled);

protected:
	UFUNCTION()
	void OnRep_Appearance();

	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth, float Delta);

	UFUNCTION()
	void HandleDeath(AActor* Killer, const FOneBrahamDamageReport& Report);

	/** Swaps the body's animation set to suit the weapon in hand. */
	UFUNCTION()
	void HandleWeaponChanged(AOneBrahamWeaponBase* NewWeapon, AOneBrahamWeaponBase* OldWeapon);

	/** The archetype's two animation sets, kept from ApplyArchetype for HandleWeaponChanged. */
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> PrimaryAnimClass;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> SidearmAnimClass;

	/** Updates the lean camera offset + shoulder exposure. */
	void UpdateLean(float DeltaSeconds);

	/**
	 * Footsteps, on every machine with a player at it: each pawn steps through the
	 * local player's audio manager, which owns the stride length and the per-
	 * surface cue. The surface comes from the movement component's cached trace.
	 */
	void TickFootsteps();

	/** Cheap 2 Hz floor detection: floor index from world Z, cached. */
	void UpdateFloorIndex(float DeltaSeconds);

	/** Handles the animation-relevant state (sprint/ADS/crouch) for the ABP. */
	void UpdateAnimationState();

	// --- Components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamCharacterMovementComponent> OBCharacterMovement = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamHealthComponent> HealthComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamDownedComponent> DownedComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamWeaponComponent> WeaponComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamInventoryComponent> InventoryComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamInteractComponent> InteractComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamRagdollComponent> RagdollComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamFirstPersonFeelComponent> FeelComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh = nullptr;

	/** Flashlight on the weapon rail; off by default, blinding in the server room. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USpotLightComponent> WeaponFlashlight = nullptr;

	/** Character registry entry (meshes, archetype metadata, voice set). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Character")
	TObjectPtr<UOneBrahamCharacterDataAsset> CharacterData;

	/** True once the "no registry found" warning has been logged, so it logs once. */
	bool bLoggedMissingCharacterData = false;

	/**
	 * Returns the character registry, loading it on first use.
	 *
	 * ORDER OF RESOLUTION
	 *     1. The asset assigned on this Blueprint (the normal shipping path).
	 *     2. Project Settings -> One Braham -> Content -> CharacterRegistry.
	 *     3. The well-known path /Game/OneBraham/Characters/DA_CharacterRegistry.
	 *
	 * WHY IT IS A FUNCTION AND NOT A CONSTRUCTOR ASSIGNMENT
	 *     A character spawned from C++ (a test, a bot, a command-line smoke test)
	 *     has no Blueprint to assign the asset on. Without a fallback the
	 *     archetype lookup returns early, cosmetics return early, and the whole
	 *     character system silently does nothing while looking correct. The
	 *     weapon component already resolves its registry this way; this is the
	 *     same pattern in the other half of the game.
	 */
	UOneBrahamCharacterDataAsset* ResolveCharacterData();

	// --- Input state ------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Character")
	bool bSprintRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Character")
	bool bWalking = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Character")
	bool bCrouchRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Character")
	float LeanInput = 0.f;

	/** Server floor + client-side cached floor both land here. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Character")
	int32 CurrentFloorIndex = 1;

	/** Anti-cheat accumulators. */
	float LastSpeedReportTime = 0.f;
	int32 ConsecutiveSpeedViolations = 0;

	/** True when an AOneBrahamBotController drives this character (practice only). */
	bool bBotControlled = false;

	/** Camera offsets. */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Camera")
	FVector StandingCameraOffset = FVector(0.f, 0.f, 68.f);

	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Camera")
	FVector CrouchedCameraOffset = FVector(0.f, 0.f, 44.f);

	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Camera")
	float LeanCameraRoll = 3.2f;

	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Camera")
	float LeanCameraSideOffset = 22.f;

	/**
	 * The first-person hold, one looping pose per carry set, played on the arms
	 * mesh as a single node (the arms hang off the camera, so they take no aim
	 * offset: the camera's pitch is already theirs). Epic's mannequin idles,
	 * which the third-person carry blueprints also use, until the cast has its
	 * own first-person set.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|First Person")
	TSoftObjectPtr<UAnimationAsset> FirstPersonPrimaryPose;

	UPROPERTY(EditDefaultsOnly, Category = "One Braham|First Person")
	TSoftObjectPtr<UAnimationAsset> FirstPersonSidearmPose;

	/**
	 * Bones hidden on the arms mesh. Until the cast has arms-only meshes the arms
	 * are the archetype's whole body, so the head (which would fill the camera)
	 * and the legs (which would swing up into view) are switched off.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|First Person")
	TArray<FName> FirstPersonHiddenBones;

	/** Remote footsteps further than this from the listener are not scheduled at all. */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Audio")
	float FootstepCullDistanceCm = 5000.f;

private:
	/**
	 * Arms for the human driving this pawn on this machine, body for everyone
	 * else. Re-run whenever control can have changed (spawn, possess, client
	 * restart), because BeginPlay alone runs before possession on the server and
	 * before the controller replicates on a remote client.
	 */
	void UpdateFirstPersonVisibility();

	/** SetVisibility on Root and this pawn's own mesh children (the cosmetics), never attached actors or lights. */
	void SetOwnMeshVisibility(USceneComponent* Root, bool bVisible);

	/**
	 * Plays the first-person hold for a weapon slot on the arms mesh. Only the
	 * machine whose player is behind this camera draws the arms, so nothing
	 * else pays for the pose.
	 */
	void UpdateFirstPersonPose(EOneBrahamWeaponSlot Slot);

	/** The pose UpdateFirstPersonPose last started, so a re-run does not restart the loop. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> ActiveFirstPersonPose = nullptr;

	/** Accumulators for the 2 Hz/10 Hz subsystems. */
	float FloorCheckAccumulator = 0.f;

	/** Server: the accumulator for the 4 Hz authoritative movement check. */
	float SpeedReportAccumulator = 0.f;

	/** Client: the accumulator for the 4 Hz self-report. */
	float ClientSpeedReportAccumulator = 0.f;

	/**
	 * Server: when this pawn last heard a speed report from the machine driving
	 * it, on the world clock. Set when the pawn begins play, refreshed by
	 * `ServerReportSpeed`, and the silence itself is the violation when it goes
	 * stale.
	 */
	float LastClientSpeedReportTime = -1.f;
};
