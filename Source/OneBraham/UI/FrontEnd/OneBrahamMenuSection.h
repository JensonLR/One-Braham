// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMenuSection.h
// ---------------------------------------------------------------------------
// The tower in section, as an architect draws it: eighteen slabs, the double-
// height reception with its baffle ceiling, the stair and lift cores hatched,
// the fins ticked down the east face, the crown stepping back over the
// canteen half, the Winter Garden's lattice on 17. The front end draws it
// three ways:
//
//   Full   PLAY's right-hand drawing, the loading page's lift shaft: floor
//          labels, the ASCENT route up Stair A, the FIRE DRILL points
//   Mini   the 40 x 112 px drawing on each mode card
//
// It strokes in bottom-up, 40 ms a floor (signature move 6), from a reveal
// time the caller owns. Floor names are the map spec's (UOneBrahamUIStyle::
// FloorLabel), so the drawing and the HUD's floor plate agree.

#pragma once

#include "CoreMinimal.h"
#include "UI/FrontEnd/OneBrahamPaint.h"

/** What to draw. Floors are 0 (reception / plaza) to 17 (Winter Garden). */
struct ONEBRAHAM_API FOneBrahamMenuSectionParams
{
	enum class EStyle : uint8 { Full, Mini };
	EStyle Style = EStyle::Full;

	/** Floors in play: lit. The rest are drawn faint. Bit N = floor N. */
	uint32 LitFloors = 0x3FFFF;

	/** The destination floor, in safety yellow, or INDEX_NONE. 18 = the roof. */
	int32 TargetFloor = INDEX_NONE;

	/** ASCENT: the dashed route up Stair A, plaza to roof, and the helipad. */
	bool bRoute = false;

	/** FIRE DRILL: lettered control points, one per floor listed (A, B, C...). */
	TArray<int32> ControlPoints;

	/** Real time the stroke-in started; 0 draws everything at once. */
	double RevealAt = 0.0;

	/** Seconds per floor of the stroke-in. */
	float RevealPerFloor = 0.04f;

	/** Floor labels down the left (Full only). */
	bool bLabels = true;

	/** 0-1 overall: the loading page lights floors from real load progress. */
	float Progress = 1.f;
};

struct ONEBRAHAM_API FOneBrahamMenuSection
{
	static constexpr int32 NumFloors = 18;

	/** Draws the section into Rect (local units). */
	static void Paint(const OneBrahamPaint::FPainter& Canvas, const FVector2D& Position, const FVector2D& Size,
		const FOneBrahamMenuSectionParams& Params, double Now);

	/** The short name of a floor as the drawing labels it ("CANTEEN \u00B7 SKYDECK"). */
	static FString FloorName(int32 Floor);
};
