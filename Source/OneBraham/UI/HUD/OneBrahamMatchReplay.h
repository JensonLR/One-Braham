// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMatchReplay.h
// ---------------------------------------------------------------------------
// The end-of-day chart's memory: where everybody this machine could see was,
// floor by floor, every two seconds, and how far the lockdown had risen.
//
// WHY THE CLIENT RECORDS IT
//   The ideal source is a server-recorded (time, floor) per player, replicated
//   on the game state - and that is a gameplay change for the game state's
//   owner. Until it exists, each machine keeps what it already knows: its own
//   squad's pawns (always relevant to it), anybody else in view, and the
//   lockdown stage and seal line, which every machine has. The chart is drawn
//   from this record, so it never shows a route this machine did not see.
//
// Plain data, appended by UOneBrahamHUDComponent and handed to
// UOneBrahamMatchEndWidget when the match ends.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"

struct FOneBrahamReplaySample
{
	float MatchSeconds = 0.f;
	/** Highest sealed floor, INDEX_NONE for none. */
	int32 SealCeiling = INDEX_NONE;
	bool bPurge = false;
	/** (player id, floor) for every pawn seen. */
	TArray<TPair<int32, int32>> Floors;
};

struct FOneBrahamReplayPerson
{
	int32 PlayerId = INDEX_NONE;
	FString Name;
	int32 SquadId = INDEX_NONE;
	bool bLocalSquad = false;
	bool bLocal = false;
};

struct FOneBrahamReplayKill
{
	float MatchSeconds = 0.f;
	int32 KillerId = INDEX_NONE;
	int32 Floor = 0;
};

struct FOneBrahamMatchReplay
{
	TArray<FOneBrahamReplaySample> Samples;
	TArray<FOneBrahamReplayPerson> People;
	TArray<FOneBrahamReplayKill> Kills;
	float PurgeAtSeconds = -1.f;
	int32 PurgeCeiling = 14;
	float DurationSeconds = 0.f;

	/** Two seconds a sample: a 20-minute match is 600 of them. */
	static constexpr float SampleSeconds = 2.f;

	void Reset()
	{
		Samples.Reset();
		People.Reset();
		Kills.Reset();
		PurgeAtSeconds = -1.f;
		DurationSeconds = 0.f;
	}

	const FOneBrahamReplayPerson* FindPerson(int32 PlayerId) const
	{
		return People.FindByPredicate([PlayerId](const FOneBrahamReplayPerson& P) { return P.PlayerId == PlayerId; });
	}

	/** The floor a player was on at a time, from the nearest sample at or before it. */
	int32 FloorAt(int32 PlayerId, float MatchSeconds) const
	{
		int32 Floor = INDEX_NONE;
		for (const FOneBrahamReplaySample& Sample : Samples)
		{
			if (Sample.MatchSeconds > MatchSeconds)
			{
				break;
			}
			for (const TPair<int32, int32>& Entry : Sample.Floors)
			{
				if (Entry.Key == PlayerId)
				{
					Floor = Entry.Value;
				}
			}
		}
		return Floor;
	}
};
