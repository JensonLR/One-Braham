// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFloorPlanWidget.h
// ---------------------------------------------------------------------------
// The floor plan: fifteen floors, what is sealed, where the squad is.
//
// This is the game's only map, and it is deliberately not a minimap. A minimap
// would show the building from above and turn a vertical extraction shooter into
// a flat one; a floor plan shows *which floor*, which is the question the player
// actually has. It does not pause the game, it does not block movement, and it
// does not know where the enemy is.
//
// Drawn in code for the same reasons as the crosshair and the compass: it must
// be crisp at 1440p and 4K alike, and it reads a plain struct, so it can be
// screenshot-tested without a running game. A designer can subclass it in
// Blueprint and put anything around it; the plan itself is generated.
//
// WHAT IT SHOWS, IN ORDER OF WHAT THE PLAYER NEEDS
//   1. The floor they are on. Filled bar, brand white.
//   2. A downed squadmate's floor. Alarm red, and the only thing on the panel
//      that should make somebody change what they are doing.
//   3. Sealed floors (the lockdown director). Muted red.
//   4. Squadmates' floors. Signal blue count.
//   5. The extraction, if a squad is holding the winch. Safety yellow, with the
//      hold progress, because thirty seconds is exactly long enough to run for.
//
// NOT SHOWN, ON PURPOSE
//   Enemy positions, objectives the player has not found, and any floor label
//   beyond the area name: the building's own signage is part of the game.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamHUDTypes.h"
#include "OneBrahamFloorPlanWidget.generated.h"

UCLASS()
class ONEBRAHAM_API UOneBrahamFloorPlanWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamFloorPlanWidget(const FObjectInitializer& ObjectInitializer);

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// -----------------------------------------------------------------------
	//  Data
	// -----------------------------------------------------------------------
	/** Replaces the plan. Cheaper than it looks: no layout, no widgets, just a
	 *  struct the painter walks. Called when the plan opens and when the
	 *  lockdown stage or the extraction state changes while it is open. */
	void SetPlanData(const FOneBrahamFloorPlanData& InData);

	/** Shows or hides the plan. Hiding it stops the tick. */
	void SetPlanVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Plan")
	bool IsPlanVisible() const { return bPlanVisible; }

	/** True while a squadmate on another floor is down: the one state that
	 *  justifies a ping on the HUD itself, out of sight of this panel. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Plan")
	bool HasDownedSquadmate() const;

protected:
	/** One floor row, top-down: the Skydeck row is drawn first. */
	void PaintFloorRow(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& Geometry, const FOneBrahamFloorPlanFloor& Floor, float RowTop, float RowHeight) const;

	/** The building's name, the local floor label, the extraction banner and the
	 *  footer. Drawn in panel space: the caller passes the panel's own geometry,
	 *  so the footer can be placed from PanelHeight rather than from a second
	 *  translated child geometry. */
	void PaintHeaderAndFooter(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& PanelGeometry, float PanelHeight, float PanelWidth) const;

	FOneBrahamFloorPlanData PlanData;

	bool bPlanVisible = false;

	// --- Layout, in pixels at 100% HUD scale -------------------------------
	/** Panel padding. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float PanelPadding = 18.f;

	/** Height of one floor row. Fifteen rows plus padding must fit the panel. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float RowHeight = 22.f;

	/** Width of the area-name column. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float LabelColumnWidth = 190.f;

	/** Height of the header, where the building name sits. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float HeaderHeight = 44.f;

	/** Height of the footer, where the clock and the lockdown stage sit. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float FooterHeight = 34.f;

	/** Panel background opacity. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float BackgroundAlpha = 0.86f;

	/** Fade-in time, seconds. A map that appears instantly reads as a glitch. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float FadeSeconds = 0.12f;

	/** Current fade 0..1. Derived from the clock at paint, hence mutable. */
	mutable float FadeAlpha = 0.f;

	/** Pulses the downed markers so they catch the eye without animating a
	 *  whole panel: 0.5 Hz, subtle, off by default in the accessibility
	 *  settings because a pulsing UI is a migraine for some players. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float DownedPulseHz = 0.5f;

	/** Seconds since the plan opened. Derived at paint, like the fade. */
	mutable float PulseTime = 0.f;

	/** World time the plan was last opened. The fade and the downed pulse are a
	 *  function of this and the clock - see NativePaint for why there is no
	 *  tick. */
	float OpenedAtSeconds = 0.f;
};
