// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCharacterDataAsset.h
// ---------------------------------------------------------------------------
// The cast list.
//
// ONE BRAHAM has six playable archetypes, and every one of them is a person who
// would actually be in One Braham on a Tuesday afternoon:
//
//   1. Business Analyst   - the one who knows the deck is booked
//   2. Graduate / Intern  - lanyard on, still enthusiastic, badly equipped
//   3. Finance            - good coat, better watch, terrible hours
//   4. IT / Ops           - knows every service riser by name, has keys
//   5. Facilities/Security- hi-vis, radio, the only person who can open things
//   6. Consultant         - laptop bag, no fixed desk, everyone's problem
//
// THE BALANCE RULE, which is not negotiable: archetype affects *appearance,
// clothing, animation set, voice casting and cosmetics* and NOTHING else. No
// stats, no perks, no loadout differences, no hitbox differences. This is a
// locked design decision (Docs/13) and it is enforced here by there being
// nowhere on this asset to put a gameplay modifier.
//
// ASSET LOCATION: /Game/OneBraham/Characters/DA_CharacterRegistry

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamCharacterDataAsset.generated.h"

class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;
class USoundBase;

/**
 * Animation personality. Drives blend weights in ABP_OneBrahamCharacter so the
 * six archetypes move differently without needing six animation blueprints.
 *
 * These are multipliers on the same skeleton and the same clips: a "Brisk" walk
 * is the same walk cycle at a slightly different cadence and arm swing. It is
 * what stops four players from looking like one player on four screens.
 */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamAnimationPersonality
{
	GENERATED_BODY()

	/** Walk cycle playback rate multiplier (0.9 = ponderous, 1.15 = brisk). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.7", ClampMax = "1.4"))
	float WalkRateScale = 1.f;

	/** How much the arms swing when walking (0.7 = contained, 1.3 = loose). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.4", ClampMax = "1.6"))
	float ArmSwingScale = 1.f;

	/** Weapon-handling speed multiplier: draw, reload, inspect. Never affects
	 *  gameplay timings (those are on the weapon), only the mesh's animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.85", ClampMax = "1.15"))
	float WeaponHandlingScale = 1.f;

	/** Idle lean amount: how much weight shifts off-centre when standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IdleLeanScale = 0.5f;

	/** Extra spine curvature when sprinting - the "carrying a laptop" hunch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SprintHunch = 0.f;

	/** Crouch walk style: 1 = deliberate, 0 = awkward and top-heavy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrouchComposure = 0.5f;
};

/** One archetype: who they are, what they wear, how they move, how they sound. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamArchetypeDefinition
{
	GENERATED_BODY()

	/** OB.Character.Archetype.* - the identity used everywhere else in the game. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (Categories = "OB.Character.Archetype"))
	FGameplayTag ArchetypeTag;

	/** Shown on the deploy screen ("BUSINESS ANALYST"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** One dry line of biography. No jokes: this is their job, not their bit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Biography;

	/** Floor they claim to work on, used by the deploy screen and the voice sets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 HomeFloor = 4;

	// --- Body -------------------------------------------------------------
	/** Third-person body mesh variant. MetaHuman or the studio body set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	/** First-person arms, cuffed and sleeved per archetype. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;

	/** Animation blueprint shared by all archetypes (personality drives the rest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSubclassOf<UAnimInstance> AnimInstanceClass;

	/** Swapped in while a sidearm is drawn: a pistol is held, aimed and run with differently. Empty keeps AnimInstanceClass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSubclassOf<UAnimInstance> SidearmAnimInstanceClass;

	/** Body-shape variant index, fed to the body master material ("BodyVariant"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "0", ClampMax = "7"))
	int32 BodyVariant = 0;

	/** Height variation in cm applied to the skeleton scale (cosmetic only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float HeightOffsetCm = 0.f;

	// --- Face, hair, grooming --------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Face")
	TSoftObjectPtr<UMaterialInterface> FaceMaterial;

	/** Groom/hair assets: the strand set is shared, the preset differs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Face")
	TSoftObjectPtr<UMaterialInterface> HairMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Face")
	FName HairPresetId = NAME_None;

	// --- Clothing ---------------------------------------------------------
	/** Default cosmetic id per slot on first deploy (see the slot table below). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothing")
	TMap<FName, FName> DefaultCosmetics;

	// --- Performance ------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
	FOneBrahamAnimationPersonality Personality;

	/** Voice casting set: which recorded line set this archetype speaks from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
	FName VoiceSetId = NAME_None;

	/** Pitch window applied to that voice set, so two BAs are not one person. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
	FVector2D VoicePitchRange = FVector2D(0.97f, 1.03f);

	/** Non-verbal foley: lanyard swing, keyboard-bag rustle, ID card rattle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
	TSoftObjectPtr<USoundBase> MovementFoleySet;

	// --- Unlock -----------------------------------------------------------
	/**
	 * How the archetype is obtained. All six ship unlocked (cosmetics are the
	 * only monetised content), so this is metadata for the deploy screen and for
	 * future content: "Default", "Level", "Achievement".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName UnlockRuleId = TEXT("Default");

	/** Level requirement when UnlockRuleId is "Level". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 RequiredLevel = 0;

	/** Achievement or challenge id when UnlockRuleId is "Achievement". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName UnlockAchievementId = NAME_None;
};

/** A cosmetic slot: where a piece of clothing attaches and every option in it. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamCosmeticSlot
{
	GENERATED_BODY()

	/** Slot id used in PlayerState cosmetics: Jacket, Fleece, HiVis, Trousers, Shoes,
	 *  Lanyard, Badge, Bag, Headwear, Gloves, Watch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FName SlotName = NAME_None;

	/** Shown in the wardrobe UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FText DisplayName;

	/** Bone the piece parents to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FName ParentBone = TEXT("spine_03");

	/** Socket override when the piece is not bone-parented. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FName AttachSocket = NAME_None;

	/** Offsets in the parent's space, cm / degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FVector RelativeOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	/** True for pieces that must not clip through a bulletproof vest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	bool bHiddenWhenArmed = false;

	/** Cosmetic id -> mesh. The wardrobe lists these keys. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic", meta = (TitleProperty = "Key"))
	TMap<FName, TSoftObjectPtr<USkeletalMesh>> Options;
};

/**
 * The registry. One asset holds the whole cast, the cosmetic slots and the
 * colour rules, so the wardrobe, the deploy screen and the character all read
 * from exactly one place.
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamCharacterDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** All six playable archetypes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cast", meta = (TitleProperty = "DisplayName"))
	TArray<FOneBrahamArchetypeDefinition> Archetypes;

	/** Cosmetic slots in wardrobe order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetics", meta = (TitleProperty = "SlotName"))
	TArray<FOneBrahamCosmeticSlot> CosmeticSlots;

	/** Public playlists/modifiers reject any owner id in this set (see Docs/13). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetics")
	TArray<FName> CompetitiveBannedCosmeticIds;

	/** Finds an archetype by tag. Never returns null for a valid game tag. */
	const FOneBrahamArchetypeDefinition* FindArchetype(const FGameplayTag& ArchetypeTag) const;

	/** Finds an archetype by index (0..5), for the deploy screen carousel. */
	const FOneBrahamArchetypeDefinition* FindArchetypeByIndex(int32 Index) const;

	const FOneBrahamCosmeticSlot* FindCosmeticSlot(FName SlotName) const;

	/** True when a cosmetic id exists in any slot. */
	bool IsValidCosmetic(FName CosmeticId) const;

	/** Which slot a cosmetic id belongs to (NAME_None when unknown). */
	FName FindSlotForCosmetic(FName CosmeticId) const;

	/**
	 * Attaches a cosmetic mesh to a character's body mesh as a leader-pose
	 * component. Creates the component on first use, reuses it afterwards.
	 *
	 * Returns the created component, or null when the slot is misconfigured.
	 */
	class USkeletalMeshComponent* ApplyCosmeticToMesh(class USkeletalMeshComponent* BodyMesh, const FOneBrahamCosmeticSlot* Slot, class USkeletalMesh* CosmeticMesh) const;

	/** Detaches and destroys a cosmetic component for a slot, if present. */
	void ClearCosmeticFromMesh(class USkeletalMeshComponent* BodyMesh, FName SlotName) const;

	/** The archetype every new account starts with. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cast", meta = (Categories = "OB.Character.Archetype"))
	FGameplayTag DefaultArchetypeTag;

	/** Validates the cast: six archetypes, unique tags, every slot reachable. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Cast")
	bool ValidateRegistry(TArray<FString>& OutProblems) const;

#if WITH_EDITOR
	/** Editor: fills any missing slot definition with the shipping default. */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
