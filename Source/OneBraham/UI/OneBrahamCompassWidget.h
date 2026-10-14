// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCompassWidget.h
// ---------------------------------------------------------------------------
// The heading strip and the objective markers that hang off it.
//
// This is the only navigational aid in the game: ONE BRAHAM has no minimap by
// design, because a minimap turns a fifteen-floor building into a corridor. The
// player learns the tower, and the strip tells them which way they are facing
// and how far away the thing that matters is.
//
// Drawn in code for the same reasons as the crosshair: it must be crisp at any
// resolution and it updates every frame.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamHUDTypes.h"
#include "OneBrahamCompassWidget.generated.h"

UCLASS()
class ONEBRAHAM_API UOneBrahamCompassWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamCompassWidget(const FObjectInitializer& ObjectInitializer);

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// -----------------------------------------------------------------------
	//  Data
	// -----------------------------------------------------------------------
	/** Compass the local player is looking along. */
	void SetHeading(float YawDegrees) { HeadingYaw = FRotator::NormalizeAxis(YawDegrees); }

	/** Full marker list; the widget projects and culls it itself. */
	void SetMarkers(const TArray<FOneBrahamHUDCompassMarker>& InMarkers) { Markers = InMarkers; }

	/** Where the player is, so the widget can work out bearings and ranges. */
	void SetViewerLocation(const FVector& Location) { ViewerLocation = Location; }

	void SetCompassVisible(bool bVisible) { bCompassVisible = bVisible; }

protected:
	FLinearColor ColourForMarker(const FOneBrahamHUDCompassMarker& Marker) const;

	/** Projects a world point onto the strip in normalised strip space (-0.5..0.5). */
	float ProjectMarkerToStrip(const FVector& WorldLocation, float& OutAngleDelta, float& OutDistance) const;

	float HeadingYaw = 0.f;
	FVector ViewerLocation = FVector::ZeroVector;
	TArray<FOneBrahamHUDCompassMarker> Markers;

	/** Degrees of world space the strip covers; 120 is legible without panning. */
	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float VisibleFieldDegrees = 120.f;

	/** Markers further than this are clamped to the edge with a distance label. */
	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float EdgeClampDegrees = 56.f;

	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float StripHeightPixels = 26.f;

	/** Cardinal ticks every 15 degrees, with a label every 45. */
	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float CardinalSpacingDegrees = 15.f;

	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float StripBackgroundAlpha = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	bool bShowOffscreenMarkers = true;

	bool bCompassVisible = true;

	/** Cached font-free glyphs: the compass uses text drawn by the parent panel. */
};
