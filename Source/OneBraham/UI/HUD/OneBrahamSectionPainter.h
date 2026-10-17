// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSectionPainter.h
// ---------------------------------------------------------------------------
// The tower in section: one drawing, five uses.
//
// An architect's section through One Braham - slabs, the double-height
// reception under its baffle ceiling, the stair and lift cores hatched at 45
// degrees, the aluminium fins on the east face, the Winter Garden's lattice on
// 17, the helipad on the roof - drawn in 1 px lines. It is the motif that ties
// the in-match interface to the front end:
//
//   Spine   the HUD's floor stack (the lift indicator): eighteen cells, your
//           floor, your squad, what is sealed, what seals next
//   Full    the PLAY page, the loading screen and the M floor plan: the whole
//           section, with the route to the roof or the FIRE DRILL points
//   Mini    the 40 x 112 px thumbnail on a mode card
//   Chart   the end-of-day report: floors down the side, time across, the
//           lockdown rising under the squads' routes
//
// Because every one of those reads FOneBrahamSectionParams, filled from the
// same FOneBrahamBuildingStatus, the floor stack and the floor plan cannot
// disagree about the building.
//
// Stateless: everything that moves (the reveal, the marching route, the 1 Hz
// next-to-seal blink) is a function of Params.Time, which the caller supplies
// from its own clock.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"

class FSlateWindowElementList;
struct FGeometry;
struct FOneBrahamBuildingStatus;

enum class EOneBrahamSectionMode : uint8
{
	Spine,
	Full,
	Mini,
	Chart
};

/** A colleague on the drawing. */
struct FOneBrahamSectionDot
{
	int32 Floor = 0;
	bool bDowned = false;
	bool bLocal = false;
};

/** A FIRE DRILL point, or anything else that sits on a floor with a letter. */
struct FOneBrahamSectionPoint
{
	int32 Floor = 0;
	FString Letter;
	/** 0 = neutral, 1 = yours, 2 = theirs, 3 = contested. */
	int32 Owner = 0;
};

/** A route through time, for the Chart: (time 0..1, floor) samples. */
struct FOneBrahamSectionRoute
{
	TArray<FVector2D> Samples;
	FLinearColor Colour = FLinearColor::White;
	FString Label;
	bool bYours = false;
	/** Kills along the route, as (time 0..1, floor). */
	TArray<FVector2D> Kills;
};

struct ONEBRAHAM_API FOneBrahamSectionParams
{
	EOneBrahamSectionMode Mode = EOneBrahamSectionMode::Full;

	/** Floors in the building (18 in the shipping tower). */
	int32 FloorCount = 18;

	/** Bit per floor: drawn at full strength (the mode's playable floors). All
	 *  lit when zero. */
	uint32 LitFloors = 0;
	uint32 SealedFloors = 0;
	uint32 PurgedFloors = 0;
	uint32 NextSealFloors = 0;

	/** The player's floor, or INDEX_NONE. */
	int32 LocalFloor = INDEX_NONE;

	/** The floor the mode is heading for (yellow), or INDEX_NONE. */
	int32 TargetFloor = INDEX_NONE;

	/** The extraction floor (yellow fill + EXIT in the Spine), or INDEX_NONE. */
	int32 ExtractionFloor = INDEX_NONE;

	TArray<FOneBrahamSectionDot> Squad;
	TArray<FOneBrahamSectionPoint> Points;

	/** Full: the plaza-to-roof route up Stair A, dashed and travelling. */
	bool bShowRoute = false;
	/** Full: the helipad H on the roof. */
	bool bShowHelipad = true;
	/** Full: floor labels in the left column. */
	bool bShowLabels = true;

	/** Chart only. */
	TArray<FOneBrahamSectionRoute> Routes;
	/** Chart only: the highest sealed floor over time, (time 0..1, floor). */
	TArray<FVector2D> SealCeilingOverTime;
	/** Chart only: the time (0..1) the Purge began, or < 0. */
	float PurgeAtTime01 = -1.f;
	int32 PurgeCeiling = 14;

	/** Bottom-up stroke-in, 0..1 (40 ms a floor at the caller's pace). */
	float Reveal = 1.f;

	/** Clock for blinks and the travelling route, seconds. */
	double Time = 0.0;

	/** Overall opacity. */
	float Opacity = 1.f;

	/** Neutral line colour (StageHairline in the HUD). Alpha is the strength. */
	FLinearColor LineColour = FLinearColor(0.878f, 0.898f, 0.922f, 0.55f);

	/** Reduce Motion: no travelling dashes, no blinks above 1 Hz. */
	bool bReduceMotion = false;

	/** Floor labels, ascending; empty = the building layout's short names. */
	TArray<FString> FloorLabels;

	/** Fills the floor bitmasks, squad and local floor from the HUD's status. */
	void FillFromStatus(const FOneBrahamBuildingStatus& Status);

	static uint32 Bit(int32 Floor) { return Floor >= 0 && Floor < 32 ? (1u << Floor) : 0u; }
};

class ONEBRAHAM_API FOneBrahamSectionPainter
{
public:
	/**
	 * Draws the section into Geometry's local box. Returns the last layer used.
	 * Spine mode ignores the box's width beyond what its cells need and
	 * anchors at the top-left; the other modes fill the box.
	 */
	static int32 Paint(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry,
		const FOneBrahamSectionParams& Params);

	/** The Spine's natural size for a floor count (cells, gaps, helipad). */
	static FVector2D SpineSize(int32 FloorCount);

	/** The label for a floor: "15  CANTEEN / SKYDECK". */
	static FString FloorLabel(const FOneBrahamSectionParams& Params, int32 Floor);
};
