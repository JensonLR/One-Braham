// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPaintedControl.h
// ---------------------------------------------------------------------------
// One control of the front end, drawn entirely in paint: a directory row, a
// mode card, a lift button, a ledger row, a departure-board row, a settings
// row - or, unfocusable, a layer of the picture (a scrim, the section
// drawing). The look is a function handed in by the screen that builds it;
// this class owns what every control has in common:
//
//   FOCUS     mouse and pad are the same thing: hovering a control focuses
//             it, so a gamepad user and a mouse user see one state, "lit",
//             and the time it became lit (for the 140 ms focus bar)
//   PRESS     Enter, Space, A or a click; the time of the press (the 90 ms
//             row fill before the page changes)
//   ENTRY     a page's reveal: staggered 25 ms a row, 260 ms in, 140 ms out
//             (the page transition grammar), applied to the whole control as
//             an opacity and a horizontal offset
//   NAVIGATE  an optional explicit next/previous, so a list wraps and a
//             column of cards does not hand focus to the row beside it
//
// Nothing here ticks: the paint function reads the clock (Docs/08).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/FrontEnd/OneBrahamPaint.h"
#include "OneBrahamPaintedControl.generated.h"

class USizeBox;

/** A page's entrance and exit, shared by every control on it. */
struct ONEBRAHAM_API FOneBrahamReveal
{
	/** Real time the page started coming in (or 0: shown, no animation). */
	double InAt = 0.0;
	/** Real time the page started leaving, or 0 while it is in. */
	double OutAt = 0.0;
	/** Reduce Motion: fades only, at half length. */
	bool bFlat = false;

	/** Opacity and x offset for a control at stagger slot Slot. */
	void Evaluate(double Now, int32 Slot, float& OutAlpha, float& OutOffsetX) const;

	void Enter(double Now) { InAt = Now; OutAt = 0.0; }
	void Leave(double Now) { OutAt = Now; }
	bool IsLeaving() const { return OutAt > 0.0; }
};

/** What a paint function gets to know about its control. */
struct FOneBrahamControlState
{
	bool bLit = false;
	/** Seconds since the control became lit (or unlit, when !bLit). */
	float SinceLit = 0.f;
	/** Seconds since the last press, or a large number. */
	float SincePress = 1000.f;
	/** Real time now. */
	double Now = 0.0;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamPaintedControl : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamPaintedControl(const FObjectInitializer& ObjectInitializer);

	using FPaintFn = TFunction<void(const OneBrahamPaint::FPainter&, const FOneBrahamControlState&)>;

	/**
	 * Gives the control its size (0 on an axis: whatever the slot gives it)
	 * and whether it takes focus. Call once, straight after construction.
	 */
	void Setup(const FVector2D& Size, bool bFocusable);

	FPaintFn PaintFn;

	/** Enter / A / a click. Unset for a layer. */
	TFunction<void()> OnActivate;

	/** Keys the control wants before navigation does (left/right on a stepper). */
	TFunction<bool(const FKey&)> OnKey;

	/** Focus arrived (true) or left (false): the screen's chance to react. */
	TFunction<void(bool)> OnLitChanged;

	/** Pointer down or dragged on the control (a slider): local x and the width. */
	TFunction<void(float, float)> OnPointer;

	/** Explicit navigation: the widget to go to, or null for Slate's own choice. */
	TFunction<UWidget*(EUINavigation)> OnNavigate;

	/** The page this control enters and leaves with, and its slot in the stagger. */
	TSharedPtr<FOneBrahamReveal> Reveal;
	int32 RevealSlot = 0;

	/** Lit: focused, which hovering also makes it. */
	bool IsControlLit() const { return bLit; }

	/** Marks a press now, as if activated, without activating. */
	void MarkPressed();

	/** Activates as a press would (a console command, a test). */
	void Activate();

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FNavigationReply NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent,
		const FNavigationReply& InDefaultReply) override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> SizeRoot = nullptr;

	bool bTakesFocus = false;
	bool bLit = false;
	double LitChangedAt = 0.0;
	double PressedAt = -1000.0;
};
