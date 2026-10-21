// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCinemaOverlay.h
// ---------------------------------------------------------------------------
// The camera grammar's screen half: letterbox bars, dips to black, and the
// title supers ("Eighteen floors."). The front end uses it for the cold open
// and every page change; the HUD reuses it for the in-match stage cards
// (LOCKDOWN / PURGE / THE ROOF), which is why the API is three verbs and no
// knowledge of menus:
//
//   SetLetterbox(Frac, Seconds)   bars to Frac of the height each (0.128 is
//                                 2.39:1 on 16:9), eased ribbon-out
//   Dip(Out, Hold, In)            to black and back, seconds each
//   ShowSuper(Eyebrow, Line, Aside, Hold)
//                                 a title card: mono eyebrow, Archivo line,
//                                 serif aside, held then faded
//
// Everything is drawn in NativePaint from the real clock, hit-test invisible.
// On a display 21:9 or wider the bars stay at zero: the picture is already
// that shape, and bars on an ultrawide are a frame inside a frame.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "OneBrahamCinemaOverlay.generated.h"

UENUM(BlueprintType)
enum class EOneBrahamSuperStyle : uint8
{
	/** The front end's cold open: serif line, lower left, white. */
	ColdOpen,
	/** The in-match stage card: eyebrow, 88 px title, serif line, y 22-32%. */
	StageCard
};

UCLASS()
class ONEBRAHAM_API UOneBrahamCinemaOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamCinemaOverlay(const FObjectInitializer& ObjectInitializer);

	/** Bars to TargetFrac of the screen height each, over Seconds (ribbon ease). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI|Cinema")
	void SetLetterbox(float TargetFrac, float Seconds);

	/** To black over OutS, held HoldS, back over InS. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI|Cinema")
	void Dip(float OutS, float HoldS, float InS);

	/** Cross-fades from black to the picture (a held black frame coming up). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI|Cinema")
	void FadeFromBlack(float Seconds);

	/** Holds the screen black until FadeFromBlack or Dip. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI|Cinema")
	void HoldBlack();

	/** A title card, held HoldS seconds, then faded. Empty parts are skipped. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI|Cinema")
	void ShowSuper(FText Eyebrow, FText Line, FText Aside, float HoldS);

	/** Which layout ShowSuper uses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|UI|Cinema")
	EOneBrahamSuperStyle SuperStyle = EOneBrahamSuperStyle::ColdOpen;

	/** The stage card's tone: its eyebrow takes the design system's stage
	 *  colour (UOneBrahamUIStyle::ToneColour), as the HUD's card does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|UI|Cinema")
	EOneBrahamStageTone SuperTone = EOneBrahamStageTone::Lockdown;

	/** Bar colour's alpha (1 in the menu, 0.85 in a match). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|UI|Cinema")
	float BarAlpha = 1.f;

	/** Clears the super immediately (a skip). */
	void ClearSuper();

	/** The bars' current height as a fraction of the screen, each. */
	float CurrentLetterbox(double Now) const;

	/** How black the screen is now from dips and holds, 0-1. */
	float CurrentBlack(double Now) const;

	/** True while a dip is past its out-fade and before its in-fade (the cut point). */
	bool IsBlackNow(double Now) const { return CurrentBlack(Now) >= 0.999f; }

	/** Whether the viewport is 21:9 or wider, where the bars stay at zero. */
	static bool IsUltrawide(const FVector2D& Size) { return Size.Y > 0.f && Size.X / Size.Y >= 2.3f; }

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	// Letterbox tween.
	float BarFrom = 0.f;
	float BarTo = 0.f;
	double BarAt = 0.0;
	float BarSeconds = 0.f;

	// Dip timeline (real seconds).
	double DipAt = -1000.0;
	float DipOut = 0.f;
	float DipHold = 0.f;
	float DipIn = 0.f;
	bool bHoldBlack = false;
	double FadeAt = -1000.0;
	float FadeSeconds = 0.f;

	// Super.
	FText SuperEyebrow;
	FText SuperLine;
	FText SuperAside;
	double SuperAt = -1000.0;
	float SuperHold = 0.f;
};
