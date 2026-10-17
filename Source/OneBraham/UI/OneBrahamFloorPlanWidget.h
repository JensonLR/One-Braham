// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFloorPlanWidget.h
// ---------------------------------------------------------------------------
// The floor plan: the tower in section, what is sealed, where the squad is.
//
// This is the game's only map, and it is deliberately not a minimap. A minimap
// would show the building from above and turn a vertical extraction shooter into
// a flat one; a section shows *which floor*, which is the question the player
// actually has. It does not pause the game, it does not block movement, and it
// does not know where the enemy is.
//
// THE DRAWING
//   The same architect's section the front end draws and the HUD's floor stack
//   is cut from (FOneBrahamSectionPainter, Full mode): slabs, the double-height
//   reception, the stair and lift cores, the fins, the Winter Garden, the
//   helipad - with the building's state laid over it from the same
//   FOneBrahamBuildingStatus the stack reads, so the plan and the stack can
//   never disagree. A legend says what each shape means, so no state is
//   carried by colour alone.
//
// WHAT IT SHOWS, IN ORDER OF WHAT THE PLAYER NEEDS
//   1. The floor they are on: a white band and a marker.
//   2. A downed squadmate's floor: a red dot, pulsing at 0.5 Hz - the one thing
//      on the panel that should change what somebody does.
//   3. Sealed floors (hatched), purged floors (red), the next to seal (a 1 Hz
//      outline).
//   4. Squadmates' floors: blue dots.
//   5. The extraction: the yellow floor and the helipad, with the hold.
//
// NOT SHOWN, ON PURPOSE
//   Enemy positions, and anything about a floor beyond its name.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamHUDTypes.h"
#include "UI/HUD/OneBrahamHUDFrame.h"
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
	/** Replaces the plan's summary (the names, the extraction, the clock). */
	void SetPlanData(const FOneBrahamFloorPlanData& InData);

	/** The building as the HUD sees it this gather: the section's state. */
	void SetBuildingFrame(const FOneBrahamHUDFrame& InFrame) { Frame = InFrame; }

	/** Shows or hides the plan. */
	void SetPlanVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Plan")
	bool IsPlanVisible() const { return bPlanVisible; }

	/** True while a squadmate on another floor is down: the one state that
	 *  justifies a ping on the HUD itself, out of sight of this panel. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Plan")
	bool HasDownedSquadmate() const;

protected:
	FOneBrahamFloorPlanData PlanData;
	FOneBrahamHUDFrame Frame;

	bool bPlanVisible = false;

	/** World time the plan was last opened: the fade and the stroke-in are a
	 *  function of this and the clock (see NativePaint for why there is no tick). */
	double OpenedAtSeconds = 0.0;

	/** Panel width, px at 1080p. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float PanelWidth = 560.f;

	/** Fade-in time, seconds. A map that appears instantly reads as a glitch. */
	UPROPERTY(EditDefaultsOnly, Category = "Plan") float FadeSeconds = 0.12f;
};
