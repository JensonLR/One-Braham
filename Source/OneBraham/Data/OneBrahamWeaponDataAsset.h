// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamWeaponDataAsset.h
// ---------------------------------------------------------------------------
// Two assets live here:
//   * UOneBrahamWeaponDefinition - one weapon's full stat block, meshes, sockets,
//     recoil pattern, spread curve, audio and VFX references.
//   * UOneBrahamWeaponDataAsset  - the registry: every weapon in the game, plus
//     the attachment and skin tables and the ground-loot roll weights.
//
// Every number a designer wants to touch is here. No weapon stat is hard-coded
// in C++: if a value is needed at runtime it is read from the definition.
//
// SHIPPING WEAPON LIST (realistic only - no office supplies, no joke weapons).
// These ten ids are the ids build_weapons.py writes onto the definition assets,
// the ids the spawn table in OneBrahamMatchSettingsAsset.cpp rolls, and the ids
// the default loadout carries; Docs/05 lists the same weapons by name. This
// comment used to name a different ten (Pistol_Glock17, Carbine_L85A2, ...) that
// no asset has ever contained, which is how the registry, the loot table and the
// defaults drifted apart unnoticed. check_weapon_ids.py now compares all three:
//
//   W_Pistol             P99 Sidearm. 9x19, fifteen rounds, no surprises.
//   W_CompactPistol      LCP machine pistol. .380, fast, no accuracy claim.
//   W_SuppressedPistol   P99-S. Quiet, lower damage, no tracer.
//   W_SMG                MP5 9mm. The server room's own weapon.
//   W_PDW                MP7 4.6mm. Fast, high capacity, poor past 15 m.
//   W_PumpShotgun        M870. One shot at 3 m, useless past 25 m.
//   W_SemiShotgun        Semi-auto 12 bore. Faster follow-up, more spread.
//   W_Carbine556         M4. The all-rounder; nothing it is bad at.
//   W_CompactAR          MCX Compact 5.56. Better handling, more recoil.
//   W_MarksmanRifle      L129A1 7.62. Executive-floor angles, two shots, slow.
//
// ATTACHMENTS: optic (1x/2x/3x), suppressor, grip (vertical/angled), laser,
//   flashlight, extended magazine, stock. Attachments change handling and
//   concealment, never raw damage (no pay-to-win, no invisible stat cliffs).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamWeaponDataAsset.generated.h"

class AOneBrahamWeaponBase;
class AOneBrahamProjectileBase;
class USkeletalMesh;
class UStaticMesh;
class USoundBase;
class UNiagaraSystem;
class UAnimMontage;
class UTexture2D;

/** Recoil pattern: a fixed sequence of vertical/horizontal impulses in degrees. */
USTRUCT(BlueprintType)
struct FOneBrahamRecoilPattern
{
	GENERATED_BODY()

	/**
	 * Recoil steps in degrees. Applied one per shot, cycling with a wrap-around
	 * blend so a 30-round magazine does not repeat a 10-step pattern awkwardly.
	 * X = pitch (up, negative in UE), Y = yaw (sideways).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	TArray<FVector2D> Steps;

	/** Random yaw jitter added per shot, degrees. Keeps patterns fair but not memorisable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float YawJitter = 0.35f;

	/** Vertical recoil multiplier while aiming down sights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float ADSVRecoilMultiplier = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float ADSHRecoilMultiplier = 0.85f;

	/** Degrees per second the view recovers toward its pre-fire direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float RecoverySpeed = 28.f;

	/** Fraction of the accumulated recoil that is actually recovered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float RecoveryFraction = 0.92f;

	/** First-shot multiplier: the "cold barrel" bonus. 1.0 = no bonus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float FirstShotMultiplier = 0.65f;
};

/** Spread behaviour: hip-fire cone, ADS cone, and how movement/bloom change it. */
USTRUCT(BlueprintType)
struct FOneBrahamSpreadProfile
{
	GENERATED_BODY()

	/** Degrees of cone at hip fire, standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float HipStandingSpread = 3.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float HipCrouchingSpread = 2.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float HipMovingSpread = 4.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float HipSprintingSpread = 7.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float ADSSpread = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float ADSMovingSpread = 0.95f;

	/** Additional spread added per shot, capped by MaxBloom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float BloomPerShot = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float MaxBloom = 3.5f;

	/** Degrees per second the bloom decays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float BloomDecayPerSecond = 4.f;
};

/** Range falloff band: full damage out to FullDamageRange, floored at MaxRange. */
USTRUCT(BlueprintType)
struct FOneBrahamDamageFalloff
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float FullDamageRange = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float MinDamageRange = 8000.f;

	/** Multiplier applied at MinDamageRange. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float MinDamageMultiplier = 0.55f;
};

/** One attachment definition. */
USTRUCT(BlueprintType)
struct FOneBrahamAttachmentDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	FName AttachmentId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	EOneBrahamAttachmentSlot Slot = EOneBrahamAttachmentSlot::Optic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Effective magnification (1.0 = iron sights). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	float Magnification = 1.f;

	/** Multipliers on the base weapon - all <= 1.15 so nothing is a straight upgrade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	float RecoilMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	float SpreadMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	float ADS_SpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	int32 MagazineSizeDelta = 0;

	/** Suppressors reduce the audible radius and remove the tracer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	float AudibleRadiusMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	bool bHidesTracer = false;

	/** Muzzle flash mesh/system override (suppressors have almost none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment")
	TSoftObjectPtr<UNiagaraSystem> MuzzleFlashOverride;
};

/** Cosmetic weapon skin. NEVER affects stats. */
USTRUCT(BlueprintType)
struct FOneBrahamWeaponSkin
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skin")
	FName SkinId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skin")
	FText DisplayName;

	/** Base colour tint + wear scalar fed to M_WeaponMaster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skin")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skin")
	float WearAmount = 0.25f;

	/** Optional pattern texture (corporate inventory-tag decal, security livery). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skin")
	TSoftObjectPtr<UTexture2D> PatternTexture;
};

// ==========================================================================
//  UOneBrahamWeaponDefinition
// ==========================================================================
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Identity ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName WeaponId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** Short, dry description for the loadout screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EOneBrahamWeaponCategory Category = EOneBrahamWeaponCategory::Carbine556;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EOneBrahamWeaponSlot DefaultSlot = EOneBrahamWeaponSlot::Primary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<UTexture2D> LoadoutIcon;

	// --- Ballistics -------------------------------------------------------
	/** Damage per bullet at FullDamageRange. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float BaseDamage = 32.f;

	/** Number of pellets per shot (1 for everything except shotguns). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	int32 PelletCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float MuzzleVelocity = 85000.f;	// cm/s; used for tracer travel time + lead

	/** Rounds per minute. Converted to a minimum seconds-between-shots on the server. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float RoundsPerMinute = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	TArray<EOneBrahamFireMode> AvailableFireModes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	EOneBrahamFireMode DefaultFireMode = EOneBrahamFireMode::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	FOneBrahamDamageFalloff Falloff;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	FOneBrahamRecoilPattern Recoil;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	FOneBrahamSpreadProfile Spread;

	/** Max penetration depth through the Penetrable channel: cubicle walls = 8 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float MaxPenetrationThickness = 0.f;

	/** Damage retained after penetrating (0.65 = loses 35%). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float PenetrationDamageRetention = 0.65f;

	/** Wall penetration: can this round go through a plasterboard partition? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	bool bCanPenetrateCover = false;

	// --- Ammunition -------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	int32 StartingReserve = 120;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	int32 MaxReserve = 240;

	/** Tactical (mag still has a round) reload speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	float ReloadTimeTactical = 2.05f;

	/** Empty reload (bolt release + mag change) is slower. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	float ReloadTimeEmpty = 2.75f;

	/** Shotgun-style per-shell reload. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	bool bPerShellReload = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	float PerShellReloadTime = 0.55f;

	/** Whether the player loses the round in the chamber when reloading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	bool bKeepsChamberedRound = true;

	// --- Handling ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float ADSTime = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float ADSFieldOfView = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float DrawTime = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float HolsterTime = 0.32f;

	/** Degrees per second the weapon sways while moving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float SwayMovementScale = 1.f;

	/** Reduced by the "grip" attachment and by crouching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float SwayLoopScale = 1.f;

	/** Melee: a rifle butt to the face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float MeleeDamage = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float MeleeRange = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling")
	float MeleeCooldown = 0.8f;

	// --- Meshes / sockets -------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UStaticMesh> WorldPickupMesh;

	/** Socket names the animation and VFX teams depend on. See Docs/05. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName MuzzleSocket = TEXT("Muzzle");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName ShellEjectSocket = TEXT("ShellEject");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName OpticSocket = TEXT("OpticMount");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName MuzzleAttachmentSocket = TEXT("MuzzleMount");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName GripSocket = TEXT("GripMount");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName LaserSocket = TEXT("LaserMount");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName MagazineSocket = TEXT("Magazine");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName MagazineWellSocket = TEXT("MagWell");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName ChargingHandleSocket = TEXT("ChargingHandle");

	// --- Audio ------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> FireSoundClose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> FireSoundDistant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> SuppressedFireSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> DryFireSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> ReloadSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> DrawSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> ShellDropSound;

	/** Built quiet (the P99-S): fires as if a suppressor were fitted, with no
	 *  attachment in the muzzle slot. Suppressed audio, no flash, no light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	bool bIntegralSuppressor = false;

	/** Metres at which this weapon's report is still clearly audible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	float AudibleRadius = 9000.f;

	/** Metres for the muffled interior-tail version (heard through the building). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	float InteriorAudibleRadius = 14000.f;

	// --- VFX / animation --------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
	TSoftObjectPtr<UNiagaraSystem> MuzzleFlash;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
	TSoftObjectPtr<UNiagaraSystem> ShellEject;

	/** Tracer system. Suppressed weapons set bHideTracer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
	TSoftObjectPtr<UNiagaraSystem> Tracer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
	bool bHideTracer = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> DrawMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> InspectMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> MeleeMontage;

	// --- Loot -------------------------------------------------------------
	/** Weight when rolling the ground-loot table. 0 = never spawns as loot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	float LootWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 MinFloor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 MaxFloor = 15;

	// --- Helpers ----------------------------------------------------------
	/** Seconds between shots, server-enforced. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetSecondsBetweenShots() const { return RoundsPerMinute > 0.f ? 60.f / RoundsPerMinute : 0.1f; }

	/** Damage at a given distance, with the falloff band applied. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetDamageAtDistance(float Distance) const;

	/** Recoil step for shot index N, with the pattern wrapping smoothly. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector2D GetRecoilStep(int32 ShotIndex) const;
};

// ==========================================================================
//  UOneBrahamWeaponDataAsset - the registry
// ==========================================================================
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons", meta = (TitleProperty = "WeaponId"))
	TArray<TObjectPtr<UOneBrahamWeaponDefinition>> Weapons;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachments", meta = (TitleProperty = "AttachmentId"))
	TArray<FOneBrahamAttachmentDefinition> Attachments;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skins", meta = (TitleProperty = "SkinId"))
	TArray<FOneBrahamWeaponSkin> Skins;

	/** Spawn class for weapon pickups; usually BP_OneBrahamPickup_Weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Classes")
	TSoftClassPtr<AOneBrahamWeaponBase> WeaponActorClass;

	/** Projectile class used by utilities (frag / flash / smoke / breach). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Classes")
	TSoftClassPtr<AOneBrahamProjectileBase> ProjectileClass;

	UFUNCTION(BlueprintPure, Category = "Weapons")
	UOneBrahamWeaponDefinition* FindWeapon(FName WeaponId) const;

	UFUNCTION(BlueprintPure, Category = "Attachments")
	const FOneBrahamAttachmentDefinition* FindAttachment(FName AttachmentId) const;

	UFUNCTION(BlueprintPure, Category = "Skins")
	const FOneBrahamWeaponSkin* FindSkin(FName SkinId) const;

	/**
	 * Rolls a weapon for a floor using the loot weights + floor gates.
	 * @param RandomStream  pass a deterministic stream for reproducible QA runs
	 */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	UOneBrahamWeaponDefinition* RollWeaponForFloor(int32 FloorIndex, const FRandomStream& RandomStream) const;

	/** Fills in any missing definition with sensible values (used by the validator). */
	void ValidateDefinitions(TArray<FString>& OutProblems) const;
};
