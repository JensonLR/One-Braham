// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBuildingStatus.h
// ---------------------------------------------------------------------------
// The building, floor by floor, as the interface draws it: which floors are
// sealed, which are being purged, which seal next, where the squad is.
//
// ONE SOURCE
//   The HUD's floor stack, the floor plate's SEALING state, the M floor plan,
//   the lockdown module's timer row and the end-of-day chart all read this
//   struct, gathered the same way, so no two of them can disagree about the
//   building - the failure mode of a HUD with five panels about one tower.
//
// CLIENT-SAFE
//   The lockdown component keeps its sealed set on the server only. A client
//   has the stage, the replicated stage clock and the highest occupied floor,
//   and the seal rule is simple (every floor below the highest occupied one,
//   cumulative through PreLockdown and Lockdown; floors up to the purge
//   ceiling in the Purge), so a client derives the same set. The caller
//   passes the seal ceiling it recorded at the last stage change, because the
//   highest occupied floor keeps moving after the shutters have closed.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"

class APlayerController;
class UWorld;

/** What a floor is doing. Ordered by severity. */
enum class EOneBrahamFloorCondition : uint8
{
	Open,
	/** Will seal (or be purged) when the stage clock runs out. */
	NextToSeal,
	Sealed,
	/** In the purge: lethal. */
	Purged
};

/** One colleague's position on the building. */
struct FOneBrahamSquadMarker
{
	int32 PlayerId = INDEX_NONE;
	FString Name;
	int32 Floor = INDEX_NONE;
	bool bDowned = false;
	bool bLocal = false;
	bool bTransmitting = false;
	FVector Location = FVector::ZeroVector;
	bool bHasLocation = false;
};

struct ONEBRAHAM_API FOneBrahamFloorStatus
{
	int32 FloorIndex = 0;
	EOneBrahamFloorCondition Condition = EOneBrahamFloorCondition::Open;
	bool bLocal = false;
	bool bExtraction = false;
	bool bEndgame = false;
	/** Squadmates here (the local player excluded), and how many of them are down. */
	int32 Mates = 0;
	int32 MatesDowned = 0;
	FText ShortName;
	FText AreaName;
};

struct ONEBRAHAM_API FOneBrahamBuildingStatus
{
	bool bValid = false;
	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;
	EOneBrahamLockdownStage Stage = EOneBrahamLockdownStage::Normal;

	/** The stage clock; 0 when nothing comes next (the Purge, or no director). */
	float SecondsToNextStage = 0.f;
	float StageSeconds = 90.f;
	bool bDirectorRunning = false;
	bool bWarningActive = false;

	int32 HighestOccupiedFloor = 0;
	/** Highest sealed floor, or INDEX_NONE when nothing is sealed. */
	int32 SealCeiling = INDEX_NONE;
	/** Highest floor the next stage will seal or purge, or INDEX_NONE. */
	int32 NextSealCeiling = INDEX_NONE;
	int32 PurgeCeiling = 14;

	int32 LocalFloor = INDEX_NONE;
	int32 ExtractionFloor = 15;

	/** Ascending, one per floor of the building layout. */
	TArray<FOneBrahamFloorStatus> Floors;

	/** The local squad (the team in FIRE DRILL), the local player included. */
	TArray<FOneBrahamSquadMarker> Squad;

	/** Fills Out from the world. SealCeilingHint is the highest sealed floor
	 *  the caller saw at the last stage change (INDEX_NONE if none): used on
	 *  a client, where the sealed set does not replicate. */
	static void Gather(const UWorld* World, const APlayerController* LocalController, int32 SealCeilingHint,
		FOneBrahamBuildingStatus& Out);

	const FOneBrahamFloorStatus* Find(int32 FloorIndex) const;

	EOneBrahamFloorCondition ConditionOf(int32 FloorIndex) const
	{
		const FOneBrahamFloorStatus* Floor = Find(FloorIndex);
		return Floor != nullptr ? Floor->Condition : EOneBrahamFloorCondition::Open;
	}

	/** True while the local player's floor seals within Window seconds. */
	bool IsLocalSealingWithin(float WindowSeconds) const;

	/** True when the local floor is sealed, purged, or one floor above the purge. */
	bool IsLocalInDanger() const;

	/** Number of floors in the building. */
	int32 FloorCount() const { return Floors.Num(); }

	/** The seal rule, pure, for tests and the chart: the floors a stage seals
	 *  given the highest occupied floor when it began. */
	static EOneBrahamFloorCondition ConditionFor(EOneBrahamLockdownStage Stage, int32 FloorIndex, int32 SealCeiling,
		int32 NextSealCeiling, int32 PurgeCeiling);
};

/** The dusk clock: the match's own time of day. */
namespace OneBrahamDuskClock
{
	/** The clock at the start of the match. */
	constexpr int32 StartMinutes = 17 * 60 + 30;
	/** The clock when the Purge begins: sunset over Aldgate. */
	constexpr int32 PurgeMinutes = 18 * 60 + 40;

	/**
	 * Match seconds to a 24-hour clock, minutes resolution. 17:30 at the start,
	 * sunset on the frame the Purge begins (PurgeAtSeconds into the match), and
	 * the same pace after it. Pure, so the kill feed, the accident book and the
	 * end-of-day report all print the same minute for the same moment.
	 */
	ONEBRAHAM_API int32 MatchSecondsToMinutes(float MatchSeconds, float PurgeAtSeconds);

	/** "18:06". */
	ONEBRAHAM_API FText MatchSecondsToClock(float MatchSeconds, float PurgeAtSeconds = 270.f);

	/** The current match clock for a world (the game state's phase clock). */
	ONEBRAHAM_API FText Now(const UWorld* World);

	/** Seconds into the live match, from the game state. */
	ONEBRAHAM_API float MatchSeconds(const UWorld* World);

	/** When the Purge begins, in match seconds, from the match settings. */
	ONEBRAHAM_API float PurgeAtSeconds(const UWorld* World);
}
