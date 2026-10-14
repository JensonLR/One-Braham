// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLootSubsystem.h
// ---------------------------------------------------------------------------
// Puts the match's loot in the building.
//
// The match settings say, per floor, how many weapons, ammo boxes, utilities,
// medkits and keycards there are, and which keycard tier the floor issues
// (FOneBrahamFloorLootProfile). The weapon table says which weapons, how
// often, and on which floors. The map has an AOneBrahamLootSpawnPoint on every
// plausible desk. This draws one from the other once, on the server, when the
// match starts. Until it existed nothing read those counts: the building was
// empty, and a squad that arrived with a sidearm left with a sidearm.
//
// The draw is seeded, and the seed is logged, so a match can be replayed with
// the same loot from `ob.loot.seed` to chase a report.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamLootSubsystem.generated.h"

class UOneBrahamMatchSettingsAsset;
class UOneBrahamWeaponDataAsset;

/** What one draw put in the building, for the log and the tests. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamLootReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Seed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 SpawnPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Weapons = 0;
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Ammo = 0;
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Utilities = 0;
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Medkits = 0;
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Keycards = 0;

	/** Items a profile asked for and no free point could take. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Loot") int32 Unplaced = 0;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamLootSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * Server: spawns this match's loot. Once per world; a second call does
	 * nothing and returns the first report. Seed 0 picks one from the clock.
	 */
	FOneBrahamLootReport SpawnMatchLoot(const UOneBrahamMatchSettingsAsset* Settings,
		UOneBrahamWeaponDataAsset* Registry, int32 Seed = 0);

	UFUNCTION(BlueprintPure, Category = "One Braham|Loot")
	bool HasSpawned() const { return bSpawned; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Loot")
	const FOneBrahamLootReport& GetLastReport() const { return LastReport; }

	/**
	 * The weapon a draw picks on a floor: weighted by the mode's spawn table
	 * among the entries whose floor range includes FloorIndex, or the
	 * registry's RollWeaponForFloor when the table is empty. Pure, so it is
	 * testable.
	 */
	static FName ChooseWeapon(const UOneBrahamMatchSettingsAsset* Settings, const UOneBrahamWeaponDataAsset* Registry,
		int32 FloorIndex, struct FRandomStream& Stream, TArray<FName>* OutBundledUtilities = nullptr);

	/** The four utility ids, in the order the HUD strip shows them. */
	static const TArray<FName>& UtilityIds();

private:
	bool bSpawned = false;

	FOneBrahamLootReport LastReport;
};
