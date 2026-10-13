// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamQuickLineWidget.h
// ---------------------------------------------------------------------------
// The comms wheel: eight lines, chosen in a fraction of a second.
//
// A voice system with ninety-five recorded lines and no way to choose one is a
// system that only ever talks to itself. The contextual lines fire on their own -
// downed, reloading, contact - and this is the other half: the things a player
// wants to *say*, to a squadmate, on purpose, now.
//
// WHY EIGHT
//   A wheel with twenty entries is a menu, and a menu in a firefight is a death.
//   Eight segments can be hit without looking, which is the same reason the
//   weapon slots are numbered. The eight are the eight things an extraction
//   squad says: contact, moving, holding, help, on me, going quiet, pushing,
//   and never mind.
//
// WHY IT IS DRAWN IN CODE
//   The wheel has to open in the same frame the key is pressed and close the same
//   frame it is released. A Blueprint-authored radial menu can do that, but it
//   cannot be screenshot-tested without a running game; this reads a struct, so it
//   can. A designer can still subclass it and restyle everything.
//
// HOW IT IS DRIVEN
//   Open:    the controller calls SetWheelVisible(true) on the menu action.
//   Aim:     the mouse position picks the segment (NativeOnMouseMove).
//   Choose:  release the menu key with a segment under the cursor, click it, or
//            press 1-8.
//   Deliver: OnQuickLineChosen fires, and the HUD component forwards the LineId
//            to the controller, which asks the server. Nothing is spoken locally:
//            the server decides, so a modified client cannot spam voice lines
//            faster than its own cooldowns allow.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamHUDTypes.h"
#include "OneBrahamQuickLineWidget.generated.h"

/** Raised when the player picks a segment. The HUD component binds to this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamQuickLineChosen, FName, LineId);

UCLASS()
class ONEBRAHAM_API UOneBrahamQuickLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamQuickLineWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Raised on a selection. The wheel closes itself afterwards. */
	UPROPERTY(BlueprintAssignable, Category = "One Braham|UI|Comms")
	FOneBrahamQuickLineChosen OnQuickLineChosen;

	// -----------------------------------------------------------------------
	//  Data
	// -----------------------------------------------------------------------
	/** Replaces the segments. Called when the wheel opens, so the cooldowns and
	 *  the availability it shows are the ones the server will enforce. */
	void SetWheelData(const FOneBrahamQuickLineData& InData);

	/** Opens or closes the wheel. Opening resets the hover so a fast release
	 *  after a fast press does not speak whatever was last hovered. */
	void SetWheelVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Comms")
	bool IsWheelVisible() const { return bWheelVisible; }

	/** The segment under the cursor, or INDEX_NONE. Exposed so a Blueprint
	 *  subclass can put its own labels around the ring. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Comms")
	int32 GetHoveredSegment() const { return HoveredSegment; }

	/** Commits the hovered segment. Called on the menu key's release, which is
	 *  how a wheel is meant to be used: press, aim, release. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI|Comms")
	bool CommitHoveredSegment();

protected:
	/** Which segment a position in widget space falls in, or INDEX_NONE when it
	 *  is inside the dead zone at the centre or outside the ring. */
	int32 SegmentAtPosition(const FVector2D& Position, const FVector2D& WidgetSize) const;

	/** Raises OnQuickLineChosen for a segment, if it is available. */
	bool ChooseSegment(int32 SegmentIndex);

	FOneBrahamQuickLineData WheelData;

	bool bWheelVisible = false;

	/** -1 for "nothing under the cursor". */
	int32 HoveredSegment = INDEX_NONE;

	// --- Geometry, in pixels at 100% HUD scale -----------------------------
	/** Inner radius: the dead zone. Inside this a fast release chooses nothing,
	 *  which is intentional - an accidental "enemy spotted" is worse than an
	 *  unspoken one. */
	UPROPERTY(EditDefaultsOnly, Category = "Comms") float InnerRadius = 68.f;

	/** Outer radius of the ring. */
	UPROPERTY(EditDefaultsOnly, Category = "Comms") float OuterRadius = 208.f;

	/** Sector gap in degrees, so the segments read as eight separate things. */
	UPROPERTY(EditDefaultsOnly, Category = "Comms") float SectorGapDegrees = 2.5f;

	/** Opacity of the whole wheel. */
	UPROPERTY(EditDefaultsOnly, Category = "Comms") float WheelAlpha = 0.92f;

	/** How long the wheel takes to appear. 90 ms: fast enough to feel instant,
	 *  slow enough that the eye registers it as a thing that opened. */
	UPROPERTY(EditDefaultsOnly, Category = "Comms") float OpenSeconds = 0.09f;

	/** Current open animation 0..1. */
	float OpenAlpha = 0.f;
};
