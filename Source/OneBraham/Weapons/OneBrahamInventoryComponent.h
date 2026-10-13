// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInventoryComponent.h
// ---------------------------------------------------------------------------
// Everything the player is carrying that is NOT a gun: utility (frag, flash,
// smoke, breach charge), medkits (meal deals), keycards and the security
// override tool.
//
// Keycards are deliberately stored here rather than on the weapon component:
// a keycard is not a weapon, it never occupies a hand slot, and it must survive
// death (a downed player's card is lootable off their body by design - that is
// the entire access-control drama of ASCENT).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamInventoryComponent.generated.h"

class AOneBrahamPlayerController;
class AOneBrahamCharacter;
class UOneBrahamWeaponComponent;
class AOneBrahamProjectileBase;

/**
 * Counts of each utility type carried.
 *
 * The members carry no `Replicated` specifier, and should not: a struct does not
 * replicate itself. The whole struct travels as one property - see
 * UOneBrahamInventoryComponent::Utility, which is `ReplicatedUsing =
 * OnRep_Loadout` and named in GetLifetimeReplicatedProps. Marking the members
 * individually suggests a replication path that does not exist.
 */
USTRUCT(BlueprintType)
struct FOneBrahamUtilityLoadout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Utility")
	int32 FragCount = 1;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Utility")
	int32 FlashbangCount = 1;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Utility")
	int32 SmokeCount = 1;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Utility")
	int32 BreachChargeCount = 1;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Utility")
	int32 MedkitCount = 1;

	/** Total items held, for the HUD's utility strip. */
	int32 Total() const { return FragCount + FlashbangCount + SmokeCount + BreachChargeCount; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOneBrahamInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamItemPickedUp, FName, ItemId, int32, NewCount);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamInventoryComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Utility
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_Loadout, BlueprintReadOnly, Category = "One Braham|Inventory")
	FOneBrahamUtilityLoadout Utility;

	/** Max of each utility a player can hold. Deliberately small: this is not an RPG. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Inventory")
	int32 MaxPerUtilityType = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Inventory")
	int32 MaxMedkits = 2;

	/** Server: consumes one utility of the selected type and spawns its projectile. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Inventory")
	AOneBrahamProjectileBase* ServerConsumeAndSpawnUtility(UOneBrahamWeaponComponent* WeaponComponent, int32 UtilityIndex, const FVector& SpawnLocation, const FVector& ThrowDirection);

	/** Server: places the selected utility on a surface (breach charge). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Inventory")
	bool ServerPlaceUtility(UOneBrahamWeaponComponent* WeaponComponent, int32 UtilityIndex);

	UFUNCTION(BlueprintPure, Category = "One Braham|Inventory")
	bool ServerHasMedkit() const { return Utility.MedkitCount > 0; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Inventory")
	bool ServerConsumeMedkit();

	/** Server: adds utility (from a pickup). Returns the amount actually added. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Inventory")
	int32 ServerAddUtility(FName UtilityId, int32 Amount);

	UFUNCTION(BlueprintPure, Category = "One Braham|Inventory")
	int32 GetUtilityCount(FName UtilityId) const;

	/** Maps the HUD's utility strip index (0-3) to an item id. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Inventory")
	static FName GetUtilityIdForIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "One Braham|Inventory")
	static FGameplayTag GetUtilityTagForIndex(int32 Index);

	// ======================================================================
	//  Keycards / access
	// ======================================================================
	/** The best (highest tier) keycard this player holds. */
	UPROPERTY(ReplicatedUsing = OnRep_Keycards, BlueprintReadOnly, Category = "One Braham|Access")
	EOneBrahamAccessTier HighestKeycardTier = EOneBrahamAccessTier::Public;

	/** All cards held, for the HUD's access strip. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	TArray<EOneBrahamAccessTier> HeldKeycards;

	/** True when the player carries the security override tool (breach + terminal boost). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	bool bHasOverrideTool = false;

	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	void ServerGrantKeycard(EOneBrahamAccessTier Tier);

	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	bool HasKeycardForTier(EOneBrahamAccessTier RequiredTier) const;

	/** Server: drops one keycard on death so an enemy can loot the escalation. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	void ServerDropKeycardOnDeath();

	/** Server: clears everything (used on respawn in FIRE DRILL). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	void ServerResetInventory(bool bKeepKeycards);

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Inventory") FOneBrahamInventoryChanged OnInventoryChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Inventory") FOneBrahamItemPickedUp OnItemPickedUp;

protected:
	UFUNCTION() void OnRep_Loadout();
	UFUNCTION() void OnRep_Keycards();

	TWeakObjectPtr<AOneBrahamCharacter> CachedCharacter;

	/** Recomputes HighestKeycardTier from HeldKeycards. */
	void RecalculateKeycardTier();

	/** Server: spawns the projectile actor for a thrown utility. */
	AOneBrahamProjectileBase* SpawnUtilityProjectile(FName UtilityId, const FVector& Location, const FVector& Direction);
};
