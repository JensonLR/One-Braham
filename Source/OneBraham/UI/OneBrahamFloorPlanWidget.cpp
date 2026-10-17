// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamFloorPlanWidget.h"

#include "OneBraham.h"
#include "Rendering/DrawElements.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/HUD/OneBrahamSectionPainter.h"
#include "UI/HUD/OneBrahamTween.h"

using namespace OneBrahamHUDStyle;

UOneBrahamFloorPlanWidget::UOneBrahamFloorPlanWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Self-hit-test rather than the HUD's HitTestInvisible: clicking through the
	// plan while it is open would mean the player cannot tell whether it is
	// open. Looking at the plan costs the player their peripheral vision, which
	// is a real cost, and the plan is small enough that it is not a full
	// screen-cover.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UOneBrahamFloorPlanWidget::SetPlanData(const FOneBrahamFloorPlanData& InData)
{
	PlanData = InData;
}

void UOneBrahamFloorPlanWidget::SetPlanVisible(bool bVisible)
{
	if (bPlanVisible == bVisible)
	{
		return;
	}

	bPlanVisible = bVisible;
	SetVisibility(bPlanVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

	if (bPlanVisible)
	{
		// The fade and the stroke-in are both measured from this instant, in paint.
		const UWorld* World = GetWorld();
		OpenedAtSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;
	}
}

bool UOneBrahamFloorPlanWidget::HasDownedSquadmate() const
{
	for (const FOneBrahamFloorPlanFloor& Floor : PlanData.Floors)
	{
		if (Floor.bSquadmateDown && !Floor.bLocalPlayerFloor)
		{
			return true;
		}
	}

	return false;
}

int32 UOneBrahamFloorPlanWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	// --- Animation, taken from the clock rather than from a tick -------------
	// A C++ UUserWidget starts at EWidgetTickFrequency::Auto, and the engine
	// reads Auto as "tick only if this widget has a tick implemented in
	// Blueprint" - an overridden NativeTick does not count as one. Nothing here
	// needs a tick: a visible widget is painted every frame, so the fade and
	// the stroke-in are computed from the world clock each time.
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : OpenedAtSeconds;
	const float Open = static_cast<float>(Now - OpenedAtSeconds);
	const float Fade = FadeSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(Open / FadeSeconds, 0.f, 1.f) : 1.f;

	const FVector2D Screen = AllottedGeometry.GetLocalSize();
	const FOneBrahamBuildingStatus& Building = Frame.Building;
	if (!bPlanVisible || Fade <= KINDA_SMALL_NUMBER || Screen.X < 200.f || Screen.Y < 200.f || Building.FloorCount() == 0)
	{
		return Layer;
	}

	FOneBrahamPainter Root(OutDrawElements, AllottedGeometry, Layer, Fade);

	// --- the panel: left of the field of view, beside the floor stack ----------------
	// Under the floor plate, above the staff pass: the plan takes the place of
	// the floor stack and the squad board, which it draws bigger.
	const float Left = Screen.X * 0.025f;
	const float Top = Screen.Y * 0.035f + 88.f;
	const float Height = Screen.Y * 0.965f - 84.f - 18.f - Top;
	const float Width = FMath::Min(PanelWidth, Screen.X * 0.4f);
	FOneBrahamPainter P = Root.Child(FVector2D(Left, Top));
	const EOneBrahamLockdownStage Stage = Building.Stage;
	const int32 Band = StageBand(Stage);
	const FLinearColor Accent = Band == 2 ? EmergencyRed() : (Band == 1 ? SafetyYellow() : SignalBlue());

	P.RoundRect(FVector2D::ZeroVector, FVector2D(Width, Height), WithAlpha(Navy(), 0.88f), 4.f);
	P.Outline(FVector2D::ZeroVector, FVector2D(Width, Height), WithAlpha(LightGrey(), 0.1f), 1.f, 4.f);
	P.Rect(FVector2D::ZeroVector, FVector2D(4.f, Height), Accent);

	// --- header: the building, where you are, the stage --------------------------------
	const float Pad = 26.f;
	P.Text(FVector2D(Pad, 20.f), TEXT("ONE BRAHAM  ·  ALDGATE EAST  ·  SECTION A-A"), Font(EOneBrahamHUDType::Eyebrow, 11.f, 200), WithAlpha(LightGrey(), 0.6f));

	FString Here = TEXT("--");
	if (const FOneBrahamFloorStatus* Floor = Building.Find(Building.LocalFloor))
	{
		Here = FString::Printf(TEXT("Level %02d · %s"), Building.LocalFloor, *Floor->AreaName.ToString());
	}
	// The floor name fits beside nothing: the chip sits on the eyebrow's line.
	const FSlateFontInfo HereFont = Font(EOneBrahamHUDType::Verb, 22.f, -10);
	FString Fitted = Here;
	if (Measure(Fitted, HereFont).X > Width - Pad * 2.f)
	{
		while (Fitted.Len() > 4 && Measure(Fitted + TEXT("…"), HereFont).X > Width - Pad * 2.f)
		{
			Fitted.LeftChopInline(1);
		}
		Fitted = Fitted.TrimEnd() + TEXT("…");
	}
	P.Text(FVector2D(Pad, 42.f), Fitted, HereFont, White());

	FString StageWord = TEXT("SYSTEM NORMAL");
	if (Building.Mode != EOneBrahamMatchMode::Ascent)		{ StageWord = Building.Mode == EOneBrahamMatchMode::FireDrill ? TEXT("FIRE DRILL") : TEXT("AFTER HOURS"); }
	else if (Stage == EOneBrahamLockdownStage::Purge)		{ StageWord = TEXT("PURGE"); }
	else if (Band == 1)										{ StageWord = TEXT("LOCKDOWN"); }
	const FSlateFontInfo ChipFont = Font(EOneBrahamHUDType::Telemetry, 11.f, 120);
	const FVector2D Chip = Measure(StageWord, ChipFont) + FVector2D(16.f, 6.f);
	P.RoundRect(FVector2D(Width - Pad - Chip.X, 15.f), Chip, WithAlpha(Accent, Band > 0 ? 1.f : 0.f), Chip.Y * 0.5f);
	P.Outline(FVector2D(Width - Pad - Chip.X, 15.f), Chip, Accent, 1.f, Chip.Y * 0.5f);
	P.Text(FVector2D(Width - Pad - Chip.X * 0.5f, 18.f), StageWord, ChipFont, Band == 1 ? Navy() : White(), EOneBrahamTextAnchor::Centre);
	P.Rect(FVector2D(Pad, 80.f), FVector2D(Width - Pad * 2.f, 1.f), WithAlpha(Accent, 0.35f));

	// --- the section --------------------------------------------------------------------------
	const float LegendH = 96.f;
	const FVector2D DrawPos(Pad, 92.f);
	const FVector2D DrawSize(Width - Pad * 2.f, Height - 92.f - LegendH);

	FOneBrahamSectionParams Params;
	Params.Mode = EOneBrahamSectionMode::Full;
	Params.FillFromStatus(Building);
	Params.Time = Now;
	Params.Reveal = FMath::Clamp(Open / (Params.FloorCount * 0.04f), 0.f, 1.f);
	Params.LineColour = WithAlpha(LightGrey(), 0.55f);
	Params.bShowRoute = Building.Mode == EOneBrahamMatchMode::Ascent && Building.LocalFloor < Building.ExtractionFloor;
	Params.TargetFloor = Building.Mode == EOneBrahamMatchMode::Ascent ? Building.ExtractionFloor : INDEX_NONE;
	Params.Opacity = Fade;
	for (int32 Index = 0; Index < Frame.Points.Num(); ++Index)
	{
		FOneBrahamSectionPoint& Point = Params.Points.AddDefaulted_GetRef();
		Point.Floor = Frame.Points[Index].Floor;
		Point.Letter = Frame.Points[Index].Letter;
		Point.Owner = Frame.Points[Index].bYoursPresent ? 1 : 0;
	}

	const FGeometry SectionGeometry = P.Geometry.MakeChild(DrawSize, FSlateLayoutTransform(DrawPos));
	*P.Layer = FOneBrahamSectionPainter::Paint(OutDrawElements, *P.Layer + 1, SectionGeometry, Params);

	// --- the legend: every state has a shape ----------------------------------------------------
	const float LY = Height - LegendH + 10.f;
	P.Rect(FVector2D(Pad, LY - 8.f), FVector2D(Width - Pad * 2.f, 1.f), WithAlpha(LightGrey(), 0.12f));
	const FSlateFontInfo LegendFont = Font(EOneBrahamHUDType::Hint, 11.f, 80);
	const float Col = (Width - Pad * 2.f) / 3.f;
	auto Key = [&](int32 Column, int32 RowIndex, const TCHAR* Label, TFunctionRef<void(const FVector2D&)> Swatch)
	{
		const FVector2D At(Pad + Column * Col, LY + RowIndex * 24.f);
		Swatch(At);
		P.Text(At + FVector2D(24.f, 0.f), Label, LegendFont, WithAlpha(LightGrey(), 0.75f));
	};
	Key(0, 0, TEXT("YOU"), [&](const FVector2D& At) { P.Triangle(At + FVector2D(2.f, 1.f), At + FVector2D(12.f, 7.f), At + FVector2D(2.f, 13.f), White()); });
	Key(1, 0, TEXT("SQUAD"), [&](const FVector2D& At) { P.Circle(At + FVector2D(7.f, 7.f), 4.f, SignalBlue()); });
	Key(2, 0, TEXT("EXTRACTION"), [&](const FVector2D& At) { P.Rect(At + FVector2D(0.f, 2.f), FVector2D(16.f, 10.f), WithAlpha(SafetyYellow(), 0.6f)); });
	Key(0, 1, TEXT("SEALED"), [&](const FVector2D& At)
	{
		P.Rect(At + FVector2D(0.f, 2.f), FVector2D(16.f, 10.f), WithAlpha(EmergencyRed(), 0.3f));
		P.Hatch(At + FVector2D(0.f, 2.f), FVector2D(16.f, 10.f), EmergencyRed(), 4.f);
	});
	Key(1, 1, TEXT("PURGE"), [&](const FVector2D& At)
	{
		P.Rect(At + FVector2D(0.f, 2.f), FVector2D(16.f, 10.f), EmergencyRed());
		P.Cross(At + FVector2D(8.f, 7.f), 6.f, White(), 1.5f);
	});
	Key(2, 1, TEXT("SEALS NEXT"), [&](const FVector2D& At) { P.Outline(At + FVector2D(0.f, 2.f), FVector2D(16.f, 10.f), SafetyYellow(), 1.f); });

	// --- the footer: the clock, the honest limit of the plan ----------------------------------------
	const FString Footer = Building.Mode == EOneBrahamMatchMode::Ascent && Building.bDirectorRunning && Building.SecondsToNextStage > 0.f
		? FString::Printf(TEXT("%s  ·  NEXT STAGE %02d:%02d"), *Frame.DuskClock,
			FMath::CeilToInt(Building.SecondsToNextStage) / 60, FMath::CeilToInt(Building.SecondsToNextStage) % 60)
		: Frame.DuskClock;
	P.Text(FVector2D(Pad, Height - 26.f), Footer, Font(EOneBrahamHUDType::TelemetryLight, 11.f, 60), WithAlpha(LightGrey(), 0.55f));
	P.Text(FVector2D(Width - Pad, Height - 28.f), TEXT("The plan does not know where anybody else is."), Font(EOneBrahamHUDType::Aside, 14.f),
		WithAlpha(LightGrey(), 0.55f), EOneBrahamTextAnchor::Right);

	return *P.Layer + 1;
}
