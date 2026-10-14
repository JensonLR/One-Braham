// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMatchSettingsAsset.h
// ---------------------------------------------------------------------------
// Every tunable that distinguishes ASCENT, FIRE DRILL and NIGHT SHIFT lives
// here. The GameMode reads this asset on PostLogin and replicates the parts
// clients need through AOneBrahamGameState, so a designer can retune a mode
// without a recompile and without touching gameplay code.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamMatchSettingsAsset.generated.h"

class USoundBase;
class UOneBrahamWeaponDataAsset;
class UOneBrahamCharacterDataAsset;

/** One entry in a mode's weapon spawn table. */
USTRUCT(BlueprintType)
struct FOneBrahamSpawnTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FName WeaponId = NAME_None;

	/** Relative weight when rolling this entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/** Optional restriction: only spawn at or above this floor. 0 = anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 MinFloor = 0;

	/** Optional restriction: only spawn at or below this floor. 15 = anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 MaxFloor = 15;

	/** Utility slots that come with this weapon when it spawns as a pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	TArray<FName> BundledUtilityIds;
};

/** Per-floor loot density. Index 0 = plaza, 15 = Skydeck. */
USTRUCT(BlueprintType)
struct FOneBrahamFloorLootProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 FloorIndex = 1;

	/** Weapons spawned on this floor at match start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 WeaponSpawns = 6;

	/** Ammo boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 AmmoSpawns = 8;

	/** Utility spawns (frag/flash/smoke/breach). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 UtilitySpawns = 5;

	/** Medkits / meal deals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 MedkitSpawns = 3;

	/** Keycards of the tier this floor serves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 KeycardSpawns = 3;

	/** Keycard tier that spawns here. Skydeck is reachable with Executive cards + override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	EOneBrahamAccessTier KeycardTier = EOneBrahamAccessTier::Staff;
};

/** One FIRE DRILL control point definition. */
USTRUCT(BlueprintType)
struct FOneBrahamControlPointDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	FName PointId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	int32 FloorIndex = 1;

	/** Seconds of uncontested presence needed to capture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	float CaptureSeconds = 4.f;

	/** Score per second while held. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	float ScorePerSecond = 1.f;

	/** Only the triggering actor picks this up; the volume itself lives in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	FName VolumeActorTag = NAME_None;
};

/**
 * Match settings for one mode. Create three assets:
 *   DA_MatchSettings_Ascent, DA_MatchSettings_FireDrill, DA_MatchSettings_NightShift
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamMatchSettingsAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shown in the mode select screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText ModeDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText ModeDescription;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<UTexture2D> ModeThumbnail;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName MapName = TEXT("L_Persistent_OneBraham");

	// --- Rosters ----------------------------------------------------------
	/** ASCENT: squads (3 with 12 players). FIRE DRILL: 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster", meta = (ClampMin = "1", ClampMax = "4"))
	int32 TeamCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PlayersPerTeam = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	int32 MaxSpectators = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	bool bAllowJoinInProgress = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	int32 WarmupSeconds = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	int32 PostMatchSeconds = 45;

	/** Chance a match starts in the rain: the plaza and the Skydeck get it, nowhere else. London. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float RainChance = 0.6f;

	// --- Damage model -----------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bFriendlyFire = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float FriendlyFireScale = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float GlobalDamageScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float HeadshotMultiplier = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float LimbDamageMultiplier = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float StartingHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float HealthRegenPerSecond = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float HealthRegenDelaySeconds = 6.f;

	/** Max HP a medkit can restore. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float MedkitHealAmount = 60.f;

	/** Seconds to use a medkit / eat the meal deal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float MedkitUseSeconds = 3.5f;

	// --- Downed state -----------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	bool bEnableDownedState = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	float BleedOutSeconds = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	float ReviveSeconds = 4.5f;

	/** HP restored on revive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	float ReviveHealth = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	int32 MaxRevivesPerPlayerPerRound = 2;

	/** Downed players can be finished with an execution (2 s interaction). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	bool bAllowExecutions = true;

	// --- Respawn ----------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	bool bRespawnEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	float RespawnDelaySeconds = 5.f;

	/** 0 = random spawn, 1 = furthest from enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	int32 SpawnSelectionMode = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	float SpawnGraceInvulnerabilitySeconds = 0.f;

	// --- Access / lockdown (ASCENT) ---------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float TerminalOverrideSeconds = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float TerminalOverrideWithKeycardSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float BreachFuseSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float BreachChargeDamage = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	bool bLockdownEnabled = true;

	/** Seconds between escalation stages. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float LockdownStageSeconds = 90.f;

	/** Seconds of warning before a stage change so players can move. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float LockdownWarningSeconds = 15.f;

	/** Damage per second applied by the security purge on staged floors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	float PurgeDamagePerSecond = 18.f;

	/** Never purge above this floor: the Skydeck fight must always be possible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ascent")
	int32 PurgeFloorCeiling = 13;

	// --- Extraction -------------------------------------------------------
	/** Seconds a squad has to reach the Skydeck after calling the winch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	float ExtractionBroadcastSeconds = 10.f;

	/** Radius of the winch hold zone around the pad, centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	float ExtractionZoneRadiusCm = 900.f;

	/** Set false to disable the winch altogether (used by the FIRE DRILL arena). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	bool bExtractionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	float ExtractionHoldSeconds = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	float HelicopterArrivalSeconds = 45.f;

	/** Seconds the helicopter loiters once landed; the hold timer must fit inside it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	float HelicopterLoiterSeconds = 75.f;

	/** How long the extraction stays broadcast (and contestable) after a squad wipes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	float ExtractionContestWindowSeconds = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	bool bOnlyOneSquadCanExtract = true;

	/** Sends the "extraction called" broadcast to every player's HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction")
	bool bBroadcastExtractionGlobally = true;

	// --- FIRE DRILL -------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	TArray<FOneBrahamControlPointDef> ControlPoints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	int32 ScoreToWin = 200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	int32 ScorePerKill = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	int32 ScorePerCapture = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	bool bScoreByTimeLimit = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Drill")
	int32 TimeLimitSeconds = 900;

	// --- NIGHT SHIFT ------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Shift")
	int32 NightShiftWaveCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Shift")
	float NightShiftWaveIntervalSeconds = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Shift")
	float NightShiftEnemyHealthScale = 1.f;

	// --- Loot tables ------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FOneBrahamSpawnTableEntry> WeaponSpawnTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FOneBrahamFloorLootProfile> FloorLootProfiles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	/** Seconds before a dropped weapon despawns. 0 = never. */
	float DroppedWeaponLifetimeSeconds = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	/** Seconds before a dead body's lootable bag despawns. */
	float DeathBagLifetimeSeconds = 300.f;

	// --- Voice ------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	bool bProximityVoiceEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	bool bTeamVoiceEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	bool bVoiceOcclusionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float ProximityVoiceRangeCm = 3500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	EOneBrahamVoiceChannel DefaultVoiceChannel = EOneBrahamVoiceChannel::Proximity;

	// --- Audio / presentation --------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> MatchStartMusic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> ExtractionMusic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText ModeBriefing;

	/** Fills any unassigned loot profile with ASCENT-sensible defaults. */
	void EnsureDefaults();

	/** Total players this mode expects (TeamCount * PlayersPerTeam). */
	int32 GetExpectedPlayerCount() const { return TeamCount * PlayersPerTeam; }
};
