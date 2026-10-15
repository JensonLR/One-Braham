// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamWeaponBase.h
// ---------------------------------------------------------------------------
// The weapon actor: a mesh with sockets, a state machine, and - crucially -
// the *server-side* firing path. Every shot in ONE BRAHAM is traced by the
// server; the client only plays the local FX and sends a request.
//
// Why an actor and not just data on the component? Because weapons are dropped,
// picked up, dropped again when the owner dies, and looted off bodies. Keeping
// the mesh, the attachment state and the ammo on an actor makes all of that
// normal actor replication rather than a bespoke inventory serialisation.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamWeaponBase.generated.h"

class UOneBrahamWeaponDefinition;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UAudioComponent;
class USpotLightComponent;

UENUM(BlueprintType)
enum class EOneBrahamWeaponState : uint8
{
	/** On the ground / in a bag, nobody holding it. */
	Idle = 0,
	/** Being drawn from the holster. */
	Drawing = 1,
	/** Ready to fire. */
	Ready = 2,
	/** Mid reload. */
	Reloading = 3,
	/** Being holstered to switch. */
	Holstering = 4,
	/** Melee swing. */
	Meleeing = 5,
	/** Inspect animation (cosmetic). */
	Inspecting = 6,
	/** Out of ammo entirely (no reserve). */
	Empty = 7
};

/**
 * One character hit, as every machine needs to see it: who, where, which way,
 * how hard. Carried by MulticastPlayCharacterHitFX so the victim's flinch, the
 * blood on the wall behind them and the wound shading play on every client,
 * not just on the server that traced the shot.
 */
USTRUCT()
struct FOneBrahamCharacterHitFX
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<class AOneBrahamCharacter> Victim = nullptr;

	UPROPERTY()
	FName Bone = NAME_None;

	UPROPERTY()
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;

	/** Damage actually dealt, rounded and clamped to a byte: it only scales visuals. */
	UPROPERTY()
	uint8 Damage = 0;

	UPROPERTY()
	bool bHeadshot = false;

	UPROPERTY()
	bool bKilled = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamWeaponFired, AOneBrahamWeaponBase*, Weapon, bool, bWasSuppressed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamWeaponStateChanged, EOneBrahamWeaponState, NewState);

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamWeaponBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

	// ======================================================================
	//  Setup
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Weapon")
	TObjectPtr<UOneBrahamWeaponDefinition> Definition;

	/** Initialises this weapon actor from a definition. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void InitialiseFromDefinition(UOneBrahamWeaponDefinition* InDefinition);

	/** Called by the weapon component when this weapon is equipped by a character. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void OnEquippedBy(class AOneBrahamCharacter* NewOwnerCharacter);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void OnUnequipped();

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	class AOneBrahamCharacter* GetOwningOneBrahamCharacter() const { return OwningCharacter.Get(); }

	// ======================================================================
	//  Sockets
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	FTransform GetMuzzleTransform() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	FVector GetMuzzleLocation() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	FTransform GetShellEjectTransform() const;

	/** Aim point used by the third-person body's aim offset and the laser. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	FVector GetAimPoint() const { return CachedAimPoint; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void SetAimPoint(const FVector& NewAimPoint) { CachedAimPoint = NewAimPoint; }

	// ======================================================================
	//  Firing (SERVER ONLY - the component calls these)
	// ======================================================================
	/**
	 * Fires one shot (or one shell of pellets).
	 * @param StartLocation  server-authoritative trace origin (the shooter's eye)
	 * @param AimDirection   normalised direction the server believes the player aimed
	 * @return damage dealt, and fills OutImpacts for the FX multicast
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	float ServerFireShot(const FVector& StartLocation, const FVector& AimDirection, TArray<FOneBrahamImpactInfo>& OutImpacts, AActor*& OutFirstHitActor);

	/** Server: consumes one round. Returns false if the magazine is empty. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	bool ConsumeRound();

	/** Server: starts a reload. Returns the total seconds the reload will take. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	float ServerStartReload();

	/** Server: finishes the reload (called by a timer set in ServerStartReload). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void ServerFinishReload();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void ServerCancelReload();

	/** Server: melee attack. Returns the damage dealt (0 if it missed). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	float ServerMelee();

	// ======================================================================
	//  Ammo
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	int32 GetMagazineSize() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	bool IsMagazineEmpty() const { return AmmoInMagazine <= 0; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void SetAmmoInMagazine(int32 NewAmmo);

	// ======================================================================
	//  Attachments / cosmetics
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	bool AttachAttachment(FName AttachmentId);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	bool DetachAttachment(EOneBrahamAttachmentSlot Slot);

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	bool HasAttachmentInSlot(EOneBrahamAttachmentSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	FName GetAttachmentInSlot(EOneBrahamAttachmentSlot Slot) const;

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void ApplySkin(FName SkinId);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void SetFlashlightEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void SetLaserEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	bool IsFlashlightEnabled() const { return bFlashlightEnabled; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	bool IsLaserEnabled() const { return bLaserEnabled; }

	// ======================================================================
	//  Effective stats (base x attachments)
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float GetEffectiveRecoilMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float GetEffectiveSpreadMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float GetEffectiveADSTime() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	bool HasSuppressor() const { return bSuppressorFitted; }

	// ======================================================================
	//  FX (client-side, called on every machine that can see the shot)
	// ======================================================================
	UFUNCTION(NetMulticast, Unreliable, Category = "One Braham|Weapon")
	void MulticastPlayFireFX(const FVector_NetQuantize& MuzzleLocation, const FVector_NetQuantizeNormal& Direction, bool bSuppressed);

	UFUNCTION(NetMulticast, Unreliable, Category = "One Braham|Weapon")
	void MulticastPlayImpactFX(const TArray<FOneBrahamImpactInfo>& Impacts);

	UFUNCTION(NetMulticast, Reliable, Category = "One Braham|Weapon")
	void MulticastPlayReloadFX(bool bStarted);

	/**
	 * Character hits from one trigger pull (one entry per pellet that landed).
	 * Unreliable: it is purely cosmetic - flinch, stagger, blood decals, wound
	 * shading - and a lost packet costs a splash of blood, not a hit. Damage,
	 * the hit marker and death all travel on their own reliable paths.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayCharacterHitFX(const TArray<FOneBrahamCharacterHitFX>& Hits);

public:
	/** Total length of the current reload, seconds. 0 when not reloading. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float GetReloadDuration() const;

	/** Seconds left on the current reload, 0 when not reloading. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float GetReloadTimeRemaining() const;

	/** The weapon state machine's current state. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	EOneBrahamWeaponState GetWeaponState() const { return WeaponState; }

	/** True while this weapon is on the ground and can be picked up. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	bool IsGroundPickup() const { return bIsGroundPickup; }

	/** Server: flags the weapon as on the ground (pickable) or held. */
	void SetIsGroundPickup(bool bOnGround) { bIsGroundPickup = bOnGround; }

	UFUNCTION(NetMulticast, Unreliable, Category = "One Braham|Weapon")
	void MulticastPlayDryFire();

protected:

	/** Plays the tracer as a travelling projectile-like effect the client sees. */
	void PlayTracerLocal(const FVector& Start, const FVector& End, bool bSuppressed);

	// ======================================================================
	//  State
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "One Braham|Weapon")
	EOneBrahamWeaponState WeaponState = EOneBrahamWeaponState::Idle;

	UPROPERTY(ReplicatedUsing = OnRep_Ammo, BlueprintReadOnly, Category = "One Braham|Weapon")
	int32 AmmoInMagazine = 0;

	/** Attachment ids by slot. Authoritative on the server; rebuilt from
	 *  ReplicatedAttachments on clients so they see the right model. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Weapon")
	TMap<EOneBrahamAttachmentSlot, FName> Attachments;

	/** Wire form of Attachments (see FOneBrahamAttachmentEntry). */
	UPROPERTY(ReplicatedUsing = OnRep_Attachments)
	TArray<FOneBrahamAttachmentEntry> ReplicatedAttachments;

	/** The cosmetic skin. Replicated with a rep notify so every machine paints
	 *  the weapon it can see: a material instance is local to the machine that
	 *  made it, so the value travelling only gets a skin as far as the server. */
	UPROPERTY(ReplicatedUsing = OnRep_SkinId, BlueprintReadOnly, Category = "One Braham|Weapon")
	FName SkinId = NAME_None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapon")
	bool bFlashlightEnabled = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapon")
	bool bLaserEnabled = false;

	/** True while this weapon is on the ground and can be picked up. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapon")
	bool bIsGroundPickup = false;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Weapon") FOneBrahamWeaponFired OnWeaponFired;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Weapon") FOneBrahamWeaponStateChanged OnWeaponStateChanged;

	// ======================================================================
	//  Components
	// ======================================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> PickupMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UNiagaraComponent> MuzzleFlashComponent = nullptr;

	/**
	 * Conventional generated FX, used when the weapon definition does not name its
	 * own. Resolved once, at InitialiseFromDefinition.
	 *
	 * WHY THESE LIVE ON THE ACTOR
	 *     The definition is a data asset, and the content build creates weapon
	 *     definitions before it creates Niagara systems - so a fresh checkout has
	 *     ten weapons with no muzzle flash, no shell ejection and no tracer, and
	 *     the only symptom is "the guns do not look like they are firing". The
	 *     actor caches the generated system instead of writing to the asset, so a
	 *     designer's authored system always wins and the fallback never mutates
	 *     content.
	 */
	void ResolveFxFallbacks();

	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> CachedMuzzleFlash = nullptr;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> CachedShellEject = nullptr;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> CachedTracer = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USpotLightComponent> MuzzleLight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USpotLightComponent> WeaponLight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<class UAudioComponent> FireAudioComponent = nullptr;

protected:
	UFUNCTION() void OnRep_State();
	UFUNCTION() void OnRep_SkinId();
	UFUNCTION() void OnRep_Ammo();
	UFUNCTION() void OnRep_Attachments();

	/** Places attachment meshes on their sockets. */
	void RebuildAttachmentMeshes();

	/** Socket name for an attachment slot, from the weapon definition. */
	FName GetAttachmentSocketName(EOneBrahamAttachmentSlot Slot) const;

	/** Applies the skin to the weapon master material. */
	void RefreshMaterialParameters();

	/**
	 * Server: one bullet. Traces, penetrates a shoot-through surface if the
	 * weapon can, applies damage, and appends every impact the round made
	 * (entry, exit and far side) to OutImpacts and every character it hurt to
	 * OutCharacterHits. Returns the damage dealt.
	 */
	float TraceBullet(const FVector& Start, const FVector& Direction, TArray<FOneBrahamImpactInfo>& OutImpacts, AActor*& OutHitActor,
		TArray<FOneBrahamCharacterHitFX>& OutCharacterHits);

	/** Server: damage, hit confirmation and the hit-FX entry for one character hit. */
	float ApplyBulletHitToCharacter(class AOneBrahamCharacter* HitCharacter, const FHitResult& Hit, const FVector& Direction,
		float TotalDistance, float DamageScale, bool bPenetrated, TArray<FOneBrahamCharacterHitFX>& OutCharacterHits);

	/**
	 * Server: tells every living enemy player whose head the round passed within
	 * NearMissRadius of (and did not hit) that they are being shot at, through
	 * their own UOneBrahamRagdollComponent::ClientNearMiss.
	 */
	void NotifyNearMisses(const FVector& Start, const FVector& End, const AActor* HitActor);

	/** How far a round is traced. Not the falloff band: rounds do not stop where damage bottoms out. */
	float GetMaxTraceDistance() const;

	/** Head-to-path distance under which a passing round counts as a near miss, cm. */
	static constexpr float NearMissRadius = 150.f;

	/** Server: last near-miss time per victim, so automatic fire sends four a second, not fifteen. */
	TMap<TWeakObjectPtr<AActor>, float> LastNearMissTime;

	/** Client: counts shots locally so the fallback tracer is one round in three. */
	int32 LocalTracerCounter = 0;

	/** Server: converts a bone name into a hit zone. */
	static EOneBrahamHitZone ResolveHitZone(FName BoneName);

	/** Server: resolves a physical material surface into our surface enum. */
	static EOneBrahamFootstepSurface ResolveSurface(const UPhysicalMaterial* PhysicalMaterial);

	/** Sets state + fires the delegate + forces a net update. */
	void SetWeaponState(EOneBrahamWeaponState NewState);

	TWeakObjectPtr<class AOneBrahamCharacter> OwningCharacter;

	/** Cached component references on the owning character. */
	UPROPERTY()
	TWeakObjectPtr<class UOneBrahamWeaponComponent> OwningWeaponComponent;

	/**
	 * Seconds remaining in a transitional state (Drawing, Holstering, Meleeing).
	 *
	 * WHY THIS EXISTS AT ALL
	 *     A weapon that reaches for its state to decide whether it can fire, and
	 *     whose only transitions are "hit something" and "be reloaded by
	 *     something", never becomes Ready after it is drawn - and a weapon that is
	 *     never Ready can never fire. That is a whole-class-of-nothing-works bug,
	 *     and it is why every transitional state in this project is on a timer
	 *     owned by the thing that entered it.
	 */
	float StateSecondsRemaining = 0.f;

	/** Finishes a timed state and moves to the state that follows it. */
	void AdvanceStateTimer(float DeltaSeconds);

	/** Ready, or Empty when the magazine is dry. The state a settled weapon is in. */
	EOneBrahamWeaponState SettledState() const;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Weapon")
	bool bSuppressorFitted = false;

	FVector CachedAimPoint = FVector::ZeroVector;

	/** Timer handle for the reload completion. */
	FTimerHandle ReloadTimerHandle;
	FTimerHandle MeleeTimerHandle;

	/** Server-side shot counter, used to index the recoil pattern. */
	int32 ShotCounter = 0;

	/** Time of the last shot, for the server's fire-rate validation. */
	float LastShotTime = -1000.f;

	/** Random stream for spread: deterministic per weapon so QA can reproduce. */
	FRandomStream SpreadStream;

public:
public:
	/** Called by the weapon component so the weapon can read attachments/skins. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void SetOwningWeaponComponent(class UOneBrahamWeaponComponent* InComponent);

	/** Server diagnostics: how many shots this weapon has fired. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float GetLastShotTime() const { return LastShotTime; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	int32 GetShotCounter() const { return ShotCounter; }

	/** Resets recoil/shot tracking (on reload, on respawn, on pickup). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Weapon")
	void ResetShotTracking();

	/** The reload duration the server will use, considering ammo state. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Weapon")
	float ComputeReloadDuration() const;
};
