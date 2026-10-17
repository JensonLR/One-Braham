// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCompassWidget.h
// ---------------------------------------------------------------------------
// The heading strip: the reception's black baffle ceiling, turned on its side.
//
// This is the only navigational aid in the game: ONE BRAHAM has no minimap by
// design, because a minimap turns an eighteen-floor building into a corridor.
// The strip says which way the player is facing - in London's terms, not just
// the compass's: the City is west, Shoreditch north, Whitechapel east, the
// Tower south, because that is how anybody who works at Aldgate East thinks -
// and where the things that matter are: the squad, the pings, the extraction,
// and in a lockdown the nearest stair core that is still open.
//
// THE LOOK
//   No filled bar. Slats hang from a hairline like baffles: a short one every
//   5 degrees, a longer one every 15, the cardinals in Archivo with their
//   London bearing under them. The centre notch is signal blue with a soft
//   ribbon glow beneath the slats. Markers clamp to the strip's ends as half
//   chevrons when they are behind the player.
//
// Repaints every frame from the player's camera (heading and floor), which is
// why it is its own widget rather than a part of the 15 Hz HUD snapshot. It
// never hides while the compass setting is on (Docs/08).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamHUDTypes.h"
#include "UI/HUD/OneBrahamHUDFrame.h"
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
	/** Overrides the camera's heading (tests, an authored HUD with no camera). */
	void SetHeading(float YawDegrees) { HeadingYaw = FRotator::NormalizeAxis(YawDegrees); bHeadingOverride = true; }

	/** The snapshot's generic markers (an authored HUD's path). */
	void SetMarkers(const TArray<FOneBrahamHUDCompassMarker>& InMarkers) { Markers = InMarkers; }

	/** The painted HUD's markers, the stage (for the stair tags), and the settings. */
	void SetFrame(const TArray<FOneBrahamCompassMarkView>& InMarks, EOneBrahamLockdownStage InStage, bool bInReduceMotion, float InScale)
	{
		Marks = InMarks;
		Stage = InStage;
		bReduceMotion = bInReduceMotion;
		Scale = FMath::Clamp(InScale, 0.8f, 1.2f);
	}

	/** Overrides where the player is (tests); otherwise the camera's location. */
	void SetViewerLocation(const FVector& Location) { ViewerLocation = Location; bViewerOverride = true; }

	void SetCompassVisible(bool bVisible) { bCompassVisible = bVisible; }

	/** Degrees clockwise from geographic north for a world yaw. */
	float BearingForYaw(float Yaw) const { return FRotator::ClampAxis(Yaw - NorthYawDegrees); }

	/** The world yaw that points at geographic north. */
	float GetNorthYaw() const { return NorthYawDegrees; }

protected:
	/** The camera's yaw and location this frame, or the overrides. */
	void ResolveView(float& OutYaw, FVector& OutLocation) const;

	float HeadingYaw = 0.f;
	bool bHeadingOverride = false;
	FVector ViewerLocation = FVector::ZeroVector;
	bool bViewerOverride = false;
	TArray<FOneBrahamHUDCompassMarker> Markers;
	TArray<FOneBrahamCompassMarkView> Marks;
	EOneBrahamLockdownStage Stage = EOneBrahamLockdownStage::Normal;
	bool bReduceMotion = false;
	float Scale = 1.f;

	/**
	 * The world yaw of geographic north. The district is built in One
	 * Braham's own frame (Tools/Python/onebraham/district_data.py): X along the
	 * tower's long axis, turned 30.13 degrees from east, so north is at -59.87.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float NorthYawDegrees = -59.87f;

	/** Degrees of world space the strip covers; 120 is legible without panning. */
	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float VisibleFieldDegrees = 120.f;

	/** The hairline the slats hang from, in the widget's local units. */
	UPROPERTY(EditDefaultsOnly, Category = "Compass")
	float BaselineY = 22.f;

	bool bCompassVisible = true;
};
