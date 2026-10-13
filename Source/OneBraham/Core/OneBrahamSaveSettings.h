// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSaveSettings.h
// ---------------------------------------------------------------------------
// Persistent, non-graphics player data: bindings overrides, loadout cosmetics,
// milestones, and the local profile (display name, clan tag, region).
// Graphics/audio live in UOneBrahamGameUserSettings (engine-owned) so that the
// engine's own reset-to-default path keeps working.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "OneBrahamTypes.h"
#include "OneBrahamSaveSettings.generated.h"

/** One saved weapon loadout slot (ASCENT lets you pick a starting primary). */
USTRUCT(BlueprintType)
struct FOneBrahamSavedLoadout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Loadout")
	FName PrimaryWeaponId = TEXT("Carbine_L85A2");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Loadout")
	FName SidearmWeaponId = TEXT("Pistol_Glock17");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Loadout")
	TArray<FName> UtilityIds;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Loadout")
	FName ArchetypeId = TEXT("BusinessAnalyst");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Loadout")
	FName WeaponSkinId = NAME_None;
};

/** A cosmetic item the player has unlocked + whether it is currently worn. */
USTRUCT(BlueprintType)
struct FOneBrahamCosmeticProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cosmetics")
	FName CosmeticId = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cosmetics")
	bool bUnlocked = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cosmetics")
	int32 TimesEquipped = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cosmetics")
	FDateTime UnlockedUtc = FDateTime(2026, 1, 1);
};

/**
 * Local player save. File: Saved/SaveGames/OneBraham_<SteamId>.sav
 * Everything here is cosmetic or preference-only: nothing in this file can
 * affect damage, speed, recoil or access, by design (no pay-to-win).
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamSaveSettings : public USaveGame
{
	GENERATED_BODY()

public:
	static const TCHAR* SlotNamePrefix() { return TEXT("OneBraham_"); }
	static constexpr int32 SaveVersion = 5;

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	int32 Version = SaveVersion;

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	FString PlayerName;

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	FString ClanTag = TEXT("OB");

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	FString PreferredRegion = TEXT("EU-West");

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	FString LastUsedServerAddress;

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	int32 LastUsedServerPort = 7777;

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	bool bHasSeenTutorial = false;

	UPROPERTY(BlueprintReadWrite, Category = "Profile")
	bool bHasCompletedFirstExtraction = false;

	// --- Loadouts ---------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Loadout")
	TArray<FOneBrahamSavedLoadout> Loadouts;

	UPROPERTY(BlueprintReadWrite, Category = "Loadout")
	int32 SelectedLoadoutIndex = 0;

	// --- Cosmetics --------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	TArray<FOneBrahamCosmeticProgress> CosmeticProgress;

	/** Currently worn cosmetic per slot tag name ("OB.Cosmetic.Slot.Torso" -> "Fleece_Navy_IT"). */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	TMap<FName, FName> EquippedCosmetics;

	/** Per-weapon equipped skin: weapon id -> skin id. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	TMap<FName, FName> EquippedWeaponSkins;

	// --- Milestones (drive cosmetic unlocks + achievements) ---------------
	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 TotalMatchesPlayed = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 TotalExtractions = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 TotalKills = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 TotalDowns = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 TotalRevives = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 TotalFireDrillWins = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	int32 LongestAscentFloorReached = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	float BestAscentTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Progress")
	bool bExtractedWithoutDamagingSquad = false;

	// --- Bindings ---------------------------------------------------------
	/** Key overrides applied on top of the default mapping context, keyed by "IA_Name.Action". */
	UPROPERTY(BlueprintReadWrite, Category = "Controls")
	TMap<FName, FString> KeyOverrides;

	UPROPERTY(BlueprintReadWrite, Category = "Controls")
	TMap<FName, FKey> GamepadOverrides;

	/** Raw mouse/gamepad sensitivity, duplicated here so the custom controls screen owns them. */
	UPROPERTY(BlueprintReadWrite, Category = "Controls")
	bool bHasSeenFirstRunSetup = false;

	/** Called after a schema bump so old saves are migrated rather than wiped. */
	void MigrateIfNeeded();
};
