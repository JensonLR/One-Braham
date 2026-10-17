// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDFrame.h
// ---------------------------------------------------------------------------
// What the redesigned HUD reads on top of FOneBrahamHUDData.
//
// FOneBrahamHUDData (UI/OneBrahamHUDTypes.h) is the long-standing snapshot -
// health, ammo, the squad, the kill feed - and stays the contract a designer's
// Blueprint HUD binds to. The building-as-interface needs a few more facts per
// gather: the building's floor-by-floor state, the dusk clock, the objective
// line in words, the compass's markers, the FIRE DRILL points. They live here
// so the snapshot keeps its shape, and the component fills both on the same
// 15 Hz gather: one moment, two structs, nothing to disagree about.
//
// Plain C++ (no reflection): nothing outside the HUD reads it.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "UI/HUD/OneBrahamBuildingStatus.h"

/** A marker hung off the compass strip. */
enum class EOneBrahamCompassMark : uint8
{
	/** A squadmate: a blue chevron and their initials. */
	Squad,
	/** A ping: a diamond living six seconds. */
	Ping,
	/** The extraction: a yellow square and the distance. */
	Extraction,
	/** The nearest open stair core in the Lockdown. */
	Stair,
	/** A FIRE DRILL point. */
	Point
};

struct FOneBrahamCompassMarkView
{
	EOneBrahamCompassMark Type = EOneBrahamCompassMark::Squad;
	FVector WorldLocation = FVector::ZeroVector;
	/** Initials, a letter, or "STAIR A". */
	FString Label;
	/** Floors above (+) or below (-) the viewer. */
	int32 FloorDelta = 0;
	float DistanceCm = 0.f;
	/** Sealed stair: the tag is struck through. */
	bool bStruck = false;
	/** Downed squadmate: red, pulsing. */
	bool bAlert = false;
	/** Pings: world time placed, for the six-second life. */
	float SpawnTime = 0.f;
};

/** One row of the in/out board. */
struct FOneBrahamSquadRowView
{
	int32 PlayerId = INDEX_NONE;
	FString Name;
	int32 Floor = INDEX_NONE;
	EOneBrahamLifeState LifeState = EOneBrahamLifeState::Alive;
	float HealthFraction = 1.f;
	float BleedOutRemaining = 0.f;
	bool bTransmitting = false;
	bool bLocal = false;
	bool bKeycard = false;
};

/** One line of the sign-out log. */
struct FOneBrahamKillRowView
{
	FString Time;
	FString Killer;
	FString Victim;
	FString Weapon;
	bool bHeadshot = false;
	/** The building did it: a shutter, a lift, the purge. */
	bool bBuilding = false;
	bool bKillerSquad = false;
	bool bVictimSquad = false;
	bool bKillerYou = false;
	bool bVictimYou = false;
	/** Stage when it happened, so the building's name is the colour of the day. */
	EOneBrahamLockdownStage Stage = EOneBrahamLockdownStage::Normal;
	float EventTime = 0.f;
};

/** A FIRE DRILL control point. */
struct FOneBrahamPointView
{
	FString Letter;
	FString Name;
	int32 Floor = 0;
	/** Your team is standing on it. */
	bool bYoursPresent = false;
};

struct FOneBrahamHUDFrame
{
	bool bValid = false;

	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;
	EOneBrahamMatchPhase Phase = EOneBrahamMatchPhase::Warmup;

	/** The building, as every panel draws it. */
	FOneBrahamBuildingStatus Building;

	/** "18:06": the match's own time of day. */
	FString DuskClock;

	// --- objective ----------------------------------------------------------------
	/** "Ascend to the Skydeck · Level 15". */
	FText Objective;
	/** "8 FLOORS". */
	FString ObjectiveDetail;

	// --- extraction ---------------------------------------------------------------
	EOneBrahamExtractionState ExtractionState = EOneBrahamExtractionState::Inactive;
	float ExtractionSeconds = 0.f;
	float ExtractionProgress01 = 0.f;
	bool bExtractionContested = false;
	bool bOwnSquadHolding = false;

	// --- FIRE DRILL ----------------------------------------------------------------
	TArray<FOneBrahamPointView> Points;
	int32 ScoreOurs = 0;
	int32 ScoreTheirs = 0;
	int32 ScoreToWin = 200;
	/** Time left in a timed mode, seconds; 0 when untimed. */
	float TimeRemaining = 0.f;

	// --- people -------------------------------------------------------------------------
	FString PlayerName;
	/** "IT OPERATIONS". */
	FString RoleLabel;
	TArray<FOneBrahamSquadRowView> Squad;
	TArray<FOneBrahamKillRowView> Kills;
	TArray<FOneBrahamCompassMarkView> Compass;

	// --- weapon -------------------------------------------------------------------------
	FString WeaponCategory;
	TArray<EOneBrahamFireMode> FireModes;
	bool bAiming = false;

	// --- voice --------------------------------------------------------------------------
	/** "PROXIMITY", "SQUAD", "TEAM". */
	FString VoiceChannelWord;
	/** Proximity range in metres; 0 for the radio channels. */
	int32 VoiceRangeMetres = 0;

	// --- spectating -------------------------------------------------------------------
	FString SpectatingName;
	int32 SpectatingFloor = INDEX_NONE;

	// --- settings -----------------------------------------------------------------------
	bool bReduceMotion = false;
	/** Off holds every element at 100%. */
	bool bRestDimming = true;
	float HUDScale = 1.f;
	float SubtitleScale = 1.f;
};
