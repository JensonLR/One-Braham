// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamWeaponComponent.h
// ---------------------------------------------------------------------------
// The player's gun handling: what is equipped, where it is pointing, how much
// it has kicked, and - the important part - the server-side validation of every
// shot a client claims to have taken.
//
// SERVER-AUTHORITATIVE FIRE PATH
//  1. Client: button pressed -> local prediction (FX, recoil, ammo guess).
//  2. Client -> Server: ServerRequestFire(AimOrigin, AimDirection, ShotIndex)
//     where AimOrigin is the client's eye position and AimDirection its aim.
//  3. Server: validates, in order:
//       a. the client has a weapon and it is in a fire-ready state
//       b. the rate of fire is legal (tolerance from UOneBrahamGameSettings)
//       c. the aim origin is within 90 cm of the pawn's server-side eye position
//          (this is lag compensation: we accept the client's slightly-stale eye
//          but never a teleported one)
//       d. the aim direction is within 12 degrees of a legal spread cone
//       e. the magazine actually has a round
//     Any failure is logged to LogOneBrahamAudit with the player's id.
//  4. Server: traces, applies damage, multicasts FX.
//
// Lag compensation: the server keeps a 1-second ring buffer of every other
// player's hitbox transforms (FActorHistoryBuffer) and re-traces against the
// historical pose at (server time - shooter's ping / 2). Players with more than
// 250 ms of ping are pinned to the 250 ms snapshot rather than being given an
// advantage.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamWeaponComponent.generated.h"

class AOneBrahamWeaponBase;
class AOneBrahamCharacter;
class AOneBrahamPlayerController;
class UOneBrahamWeaponDataAsset;
class UOneBrahamWeaponDefinition;
class UOneBrahamInventoryComponent;
class USkeletalMeshComponent;
class UAnimMontage;

/** Fired on the shooter's machine when the server confirms one of their hits. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamHitConfirmed, bool, bHeadshot, bool, bKill, float, DistanceCm);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamWeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Feedback
	// ======================================================================
	/** Bound by the HUD and by the audio manager's own listener. */
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Weapons|Feedback")
	FOneBrahamHitConfirmed OnHitConfirmed;

	/**
	 * Server-side: one of this shooter's shots just did damage. Sends the
	 * confirmation to the shooter's own machine only - nobody else ever needs to
	 * know whether a shot connected, and replicating it would leak information.
	 */
	void NotifyLocalHitConfirmed(bool bHeadshot, bool bKill, float DistanceCm);

	/** Shooter's machine: drives the hit marker, the audio and the HUD flash. */
	UFUNCTION(Client, Unreliable)
	void ClientHitConfirmed(bool bHeadshot, bool bKill, float DistanceCm);

	// ======================================================================
	//  Config
	// ======================================================================
	/** The whole weapon registry. Assigned on BP_OneBrahamCharacter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Weapons")
	TObjectPtr<UOneBrahamWeaponDataAsset> WeaponRegistry;


	/** How far the client's claimed eye position may differ from the server's. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Weapons")
	float MaxEyePositionDrift = 90.f;

	/** Aim direction tolerance beyond the legal spread cone, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Weapons")
	float MaxAimDeviationDegrees = 12.f;

	// ======================================================================
	//  Equipping
	// ======================================================================
	/** Server: gives the player a weapon (spawn or pickup) and equips it. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	AOneBrahamWeaponBase* ServerGiveWeapon(FName WeaponId, bool bEquip = true, int32 AmmoInMag = -1, int32 ReserveAmmo = -1);

	/** Server: removes and destroys the currently held weapon. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ServerRemoveCurrentWeapon();

	/**
	 * Server: everything a respawn needs.
	 *
	 * The kit the player was carrying is destroyed rather than dropped - a
	 * dropped weapon at a spawn point is loot for whoever is standing next to
	 * them - and they are re-armed from the loadout on their player state. The
	 * death path (DropAllOnDeath) already turned the previous kit into pickups,
	 * so nothing that the player never stood to lose is being deleted here.
	 *
	 * Called by AOneBrahamCharacter::ServerOnRespawned, which is the one place a
	 * life begins, and by AOneBrahamPlayerController when a client's loadout
	 * arrives after their first spawn has already happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ResetOnRespawn();

	/**
	 * Server: grants a primary, a sidearm, utility and a weapon skin.
	 *
	 * Every id is a name a client asked for. The registry refuses a weapon it
	 * does not know, the inventory refuses a utility it does not know, and the
	 * skin falls back to the issue finish if it does not resolve, so the worst a
	 * lying client can do is fail to be given something.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ServerApplyLoadout(FName PrimaryWeaponId, FName SidearmWeaponId,
		const TArray<FName>& UtilityIds, FName WeaponSkinId);

	/** The kit a spawn gets when no loadout has been received: an AI bot, or a
	 *  client whose loadout is still in flight. Never leave a player unarmed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Loadout")
	FName DefaultPrimaryWeaponId = TEXT("W_Carbine556");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Loadout")
	FName DefaultSidearmWeaponId = TEXT("W_Pistol");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Loadout")
	TArray<FName> DefaultUtilityIds;

	/**
	 * Server: takes a weapon actor off the floor into this component, moving its
	 * magazine, reserve pool and attachments with it. Displacing whatever was in
	 * the slot (which is dropped, not deleted).
	 *
	 * Called by AOneBrahamPickupBase when a player interacts with a dropped
	 * weapon. Returns the taken weapon, or null when the pickup is illegal.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	AOneBrahamWeaponBase* ServerPickupFromWorld(AOneBrahamWeaponBase* WorldWeapon);

	/** Server: drops the current weapon on the floor as a pickup. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	AOneBrahamWeaponBase* DropCurrentWeapon();

	/** Server: drops everything (death). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void DropAllOnDeath();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void EquipSlot(EOneBrahamWeaponSlot Slot);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void CycleWeapon(int32 Direction);

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	AOneBrahamWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	/** Current fire mode of the equipped weapon (Single/.../Bolt). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	EOneBrahamFireMode GetCurrentFireMode() const { return CurrentFireMode; }

	/** Rounds left in reserve for the equipped weapon (across all magazines). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	int32 GetReserveAmmoForCurrentWeapon() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	bool IsReloading() const { return bIsReloading; }

	/** 0..1 progress of the current reload, for the HUD ring. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	float GetReloadProgress01() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	AOneBrahamWeaponBase* GetWeaponInSlot(EOneBrahamWeaponSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	bool HasWeapon(FName WeaponId) const;

	/** QA/debug: ob.give. Routes to the server. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "One Braham|Weapons")
	void ServerGiveWeaponDebug(FName WeaponId);

	// ======================================================================
	//  Trigger
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void SetTriggerHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	bool IsTriggerHeld() const { return bTriggerHeld; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void SetAimingDownSights(bool bAiming);

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	bool IsAimingDownSights() const { return bAimingDownSights; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	float GetADSAlpha() const { return ADSAlpha; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void RequestReload();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void CycleFireMode();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void MeleeAttack();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void PlayInspect();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ToggleFlashlight();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ToggleLaser();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	bool CanMelee() const;

	// ======================================================================
	//  Utility / consumables
	// ======================================================================
	/** Throws the currently selected utility (frag/flash/smoke/breach). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void ThrowUtility();

	/** Uses the "plant/use" behaviour of the selected utility (breach charge). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void UseUtilityItem();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void UseMedkit();

	/** Server: switches which utility is selected. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void SelectUtilityIndex(int32 Index);

	// ======================================================================
	//  Spread / recoil (server reads these; client predicts the view kick)
	// ======================================================================
	/** Current bullet cone half-angle in degrees, from stance + movement + bloom. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	float ComputeCurrentSpreadDegrees() const;

	/** Server-side hook called by AOneBrahamWeaponBase after a legal shot. */
	void OnServerShotFired(int32 ShotIndex);

	/** Server-side hook: the weapon's magazine changed (reserve transfer, pickup). */
	void OnWeaponAmmoChanged(int32 AmmoInMagazine);

	/** Server-side hook: finishes the ammo transfer for a completed reload. */
	void ServerCompleteReload();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ResetRecoilAndSpread();

	/** Accumulated recoil in degrees (X = pitch, Y = yaw), for the camera recovery. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	FVector2D GetAccumulatedRecoil() const { return AccumulatedRecoil; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	void ConsumeRecoil(float DeltaTime);

	// ======================================================================
	//  Registry lookups used by the weapon actor / UI
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	UOneBrahamWeaponDefinition* GetWeaponDefinition(FName WeaponId) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	const FOneBrahamAttachmentDefinition* GetAttachmentDefinition(FName AttachmentId) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	const FOneBrahamWeaponSkin* GetSkinDefinition(FName SkinId) const;

	/** Returns the slot an attachment belongs in (used by the weapon actor). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	EOneBrahamAttachmentSlot GetSlotForAttachment(FName AttachmentId) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	bool IsSuppressorAttachment(FName AttachmentId) const;

	/** Server: attaches an attachment to the held weapon (looted from a body). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapons")
	bool ServerAttachToCurrentWeapon(FName AttachmentId);

	// ======================================================================
	//  RPCs
	// ======================================================================
	UFUNCTION(Server, Unreliable, Category = "One Braham|Weapons")
	void ServerRequestFire(FVector_NetQuantize AimOrigin, FVector_NetQuantizeNormal AimDirection, int32 ClientShotIndex);

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestReload();

	/** Stops a shell-by-shell reload so the loaded shells can be fired. */
	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerInterruptReload();

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestEquipSlot(EOneBrahamWeaponSlot Slot);

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestCycleWeapon(int32 Direction);

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestFireMode();

	/** The owning client's ADS state. Spread, recoil and the aim walk speed are all
	 *  computed on the server from bAimingDownSights, so it has to be told. */
	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerSetAimingDownSights(bool bAiming);

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestMelee();

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestThrowUtility(int32 UtilityIndex, FVector_NetQuantize AimOrigin, FVector_NetQuantizeNormal AimDirection);

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestUseUtility(int32 UtilityIndex, FVector_NetQuantize TargetLocation);

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestMedkit();

	UFUNCTION(Server, Reliable, Category = "One Braham|Weapons")
	void ServerRequestToggleAttachment(bool bFlashlight, bool bEnabled);

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Weapons") FOneBrahamWeaponChanged OnWeaponChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Weapons") FOneBrahamAmmoChanged OnAmmoChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Weapons") FOneBrahamReloadStateChanged OnReloadStateChanged;

	// ======================================================================
	//  Replicated state
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, BlueprintReadOnly, Category = "One Braham|Weapons")
	TObjectPtr<AOneBrahamWeaponBase> CurrentWeapon = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapons")
	EOneBrahamWeaponSlot CurrentSlot = EOneBrahamWeaponSlot::Melee;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapons")
	bool bAimingDownSights = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapons")
	bool bIsReloading = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapons")
	EOneBrahamFireMode CurrentFireMode = EOneBrahamFireMode::Auto;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapons")
	int32 SelectedUtilityIndex = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapons")
	bool bMeleeOnCooldown = false;

	/** Reserve ammo per weapon id (the pool the reload pulls from). Local
	 *  lookup: the wire is ReplicatedReserveAmmo. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Weapons")
	TMap<FName, int32> ReserveAmmoByWeapon;

	/** All weapons this player owns, by slot. Local lookup: the wire is
	 *  ReplicatedOwnedWeapons. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Weapons")
	TMap<EOneBrahamWeaponSlot, TObjectPtr<AOneBrahamWeaponBase>> OwnedWeapons;

	/** The two maps above on the wire: a Replicated TMap does not build. */
	UPROPERTY(ReplicatedUsing = OnRep_ReserveAmmo)
	TArray<FOneBrahamReserveAmmoEntry> ReplicatedReserveAmmo;

	UPROPERTY(ReplicatedUsing = OnRep_OwnedWeapons)
	TArray<FOneBrahamOwnedWeaponEntry> ReplicatedOwnedWeapons;

	/** Server: copies both maps to the wire. Called after every change to them. */
	void SyncReplicatedInventory();

	UFUNCTION() void OnRep_ReserveAmmo();
	UFUNCTION() void OnRep_OwnedWeapons();

protected:
	/**
	 * Clients: equip and unequip exactly as the server did. OnEquippedBy used to
	 * run only on the server, so on every client the weapon never knew its owner
	 * (OwningCharacter stayed null), never moved to the owner's first-person
	 * hands, kept its pickup mesh, and drew silently.
	 */
	UFUNCTION() void OnRep_CurrentWeapon(AOneBrahamWeaponBase* PreviousWeapon);
	UFUNCTION() void OnRep_Reloading();

	/** Server: hands this player what they carry this life, from the player state
	 *  when it has a loadout and from the standard-issue kit when it does not. */
	void ApplyLoadoutForCurrentLife();

	/** Actual firing: called on the server (from the RPC) or locally (listen host). */
	bool FireOnce(const FVector& AimOrigin, const FVector& AimDirection);

	/** Client-side prediction of a single shot: FX + kick, no damage. */
	void PredictLocalShot();

	/** Builds the local player's aim ray from the camera. */
	void GetLocalAim(FVector& OutOrigin, FVector& OutDirection) const;

	/** The client half of a shot: the aim from GetLocalAim, sent with this shot's index. */
	void SendFireRequest();

	/** Applies a view kick from the recoil pattern + random yaw jitter. */
	void ApplyRecoilKick(int32 ShotIndex);

	/** Server: ensures the current fire mode is legal for the weapon. */
	void SanitiseFireMode();

	/** Character/controller shortcuts. */
	AOneBrahamCharacter* GetCharacter() const;
	AOneBrahamPlayerController* GetOneBrahamPlayerController() const;

	/** Called by the weapon when it finishes drawing. */
	void OnWeaponReady();

	TWeakObjectPtr<AOneBrahamCharacter> CachedCharacter;

	/** --- Trigger / burst state --- */
	bool bTriggerHeld = false;
	float LastLocalFireTime = -1000.f;
	float LastServerFireTime = -1000.f;
	int32 ServerShotIndex = 0;
	int32 ServerBurstRemaining = 0;
	int32 ClientShotIndex = 0;

	/** --- Recoil / spread state --- */
	FVector2D AccumulatedRecoil = FVector2D::ZeroVector;
	float CurrentBloom = 0.f;
	FVector2D LastRecoilStep = FVector2D::ZeroVector;

	/** --- ADS --- */
	float ADSAlpha = 0.f;

	/** --- Utility --- */
	/** Time of the last throw, for the anti-cheat rate limit. */
	float LastUtilityTime = -1000.f;
	float LastMedkitTime = -1000.f;
	bool bMedkitInProgress = false;
	FTimerHandle MedkitTimerHandle;

	/** Server: a hit during the medkit animation ends it and spends the kit. */
	UFUNCTION()
	void HandleDamageDuringMedkit(float Damage, AActor* DamageInstigator, FVector InstigatorLocation);

	/** Server: unbinds the damage listener and clears the timer. */
	void EndMedkit();

	/** Random stream for spread, seeded per component. */
	FRandomStream SpreadStream;

	/** Timer handle for the melee cooldown. */
	FTimerHandle SourceMeleeCooldownHandle;

	/** Screen-shake-less "kick" applied to the first person weapon mesh. */
	FVector2D WeaponKickOffset = FVector2D::ZeroVector;

public:
	/** The visual kick currently applied to the 1P weapon mesh, in cm. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapons")
	FVector2D GetWeaponKickOffset() const { return WeaponKickOffset; }

	/** Server: number of shots this client has had rejected this match (QA reads it). */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Weapons")
	int32 RejectedShotCount = 0;
};
