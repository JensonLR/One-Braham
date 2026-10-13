// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamFloorPlanWidget.h"

#include "OneBraham.h"
#include "UI/OneBrahamUIStyle.h"
#include "Rendering/DrawElements.h"

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
		// The fade and the pulse are both measured from this instant, in paint.
		const UWorld* World = GetWorld();
		OpenedAtSeconds = World != nullptr ? World->GetTimeSeconds() : 0.f;
		FadeAlpha = 0.f;
		PulseTime = 0.f;
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

void UOneBrahamFloorPlanWidget::PaintFloorRow(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& Geometry, const FOneBrahamFloorPlanFloor& Floor, float RowTop, float RowHeight) const
{
	const float RowLeft = PanelPadding;
	const float RowWidth = LabelColumnWidth;
	const bool bLocal = Floor.bLocalPlayerFloor;

	// --- Row background ----------------------------------------------------
	// The local floor is a filled bar; every other floor is a hairline. That
	// asymmetry is the whole design of the panel: the player's eye lands on
	// their own floor first, every time.
	FLinearColor RowColour = UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.10f);

	if (Floor.bSealed)
	{
		RowColour = UOneBrahamUIStyle::EmergencyRed();
		RowColour.A = bLocal ? 0.42f : 0.22f;
	}
	else if (bLocal)
	{
		RowColour = UOneBrahamUIStyle::LightGrey();
		RowColour.A = 0.30f;
	}

	if (RowColour.A > 0.f)
	{
		const FSlateBrush RowBrush = UOneBrahamUIStyle::PanelBrush(RowColour.A * FadeAlpha, 2.f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(FVector2D(RowWidth, RowHeight - 2.f),
				FSlateLayoutTransform(FVector2D(RowLeft, RowTop))),
			&RowBrush,
			ESlateDrawEffect::None,
			FLinearColor(RowColour.R, RowColour.G, RowColour.B, 1.f));
	}

	// --- Left edge rule: the sealed and local markers ----------------------
	const float RuleWidth = 3.f;
	FLinearColor RuleColour = FLinearColor::Transparent;

	if (Floor.bSealed)
	{
		RuleColour = UOneBrahamUIStyle::EmergencyRed();
	}
	else if (bLocal)
	{
		RuleColour = UOneBrahamUIStyle::SignalBlue();
	}
	else if (Floor.bSquadmateDown)
	{
		// Pulses toward full opacity. One property of one element: a panel that
		// animates as a whole is noise in the corner of the eye of a player who
		// is being shot at.
		const float Pulse = 0.55f + 0.45f * FMath::Sin(PulseTime * 2.f * PI * DownedPulseHz);
		RuleColour = UOneBrahamUIStyle::EmergencyRed();
		RuleColour.A = Pulse;
	}

	if (RuleColour.A > 0.f)
	{
		const FSlateBrush RuleBrush = UOneBrahamUIStyle::PanelBrush(RuleColour.A * FadeAlpha, 0.f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			Geometry.ToPaintGeometry(FVector2D(RuleWidth, RowHeight - 2.f),
				FSlateLayoutTransform(FVector2D(RowLeft, RowTop))),
			&RuleBrush,
			ESlateDrawEffect::None,
			FLinearColor(RuleColour.R, RuleColour.G, RuleColour.B, 1.f));
	}

	// --- Floor number ------------------------------------------------------
	{
		const FSlateFontInfo NumberFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small, bLocal);

		// Ground floor reads "G", the Skydeck reads "15". A tower that calls its
		// ground floor "0" is a tower designed by an engineer.
		const FText NumberText = FText::FromString(Floor.FloorIndex == 0
			? TEXT("G")
			: FString::Printf(TEXT("%d"), Floor.FloorIndex));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			Geometry.ToPaintGeometry(FVector2D(34.f, RowHeight - 2.f),
				FSlateLayoutTransform(FVector2D(RowLeft + 10.f, RowTop + 2.f))),
			NumberText,
			NumberFont,
			ESlateDrawEffect::None,
			(bLocal ? UOneBrahamUIStyle::LightGrey() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.45f)).CopyWithAlpha(
				(bLocal ? 1.f : 0.75f) * FadeAlpha));
	}

	// --- Area name ---------------------------------------------------------
	{
		const FSlateFontInfo NameFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small, bLocal);

		// A sealed floor says so instead of its name: the name is not the news.
		const FText NameText = Floor.bSealed
			? NSLOCTEXT("OneBraham", "PlanSealed", "SEALED")
			: Floor.AreaName;

		const FLinearColor NameColour = Floor.bSealed
			? UOneBrahamUIStyle::EmergencyRed()
			: (bLocal ? UOneBrahamUIStyle::LightGrey() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.5f));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			Geometry.ToPaintGeometry(FVector2D(LabelColumnWidth - 56.f, RowHeight - 2.f),
				FSlateLayoutTransform(FVector2D(RowLeft + 46.f, RowTop + 2.f))),
			NameText,
			NameFont,
			ESlateDrawEffect::None,
			NameColour.CopyWithAlpha((bLocal ? 1.f : 0.8f) * FadeAlpha));
	}

	// --- Squad count and the downed marker ---------------------------------
	if (Floor.SquadCount > 0)
	{
		const FSlateFontInfo CountFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small, true);

		const FText CountText = Floor.bSquadmateDown
			? NSLOCTEXT("OneBraham", "PlanDownedHere", "DOWNED")
			: FText::FromString(FString::Printf(TEXT("%d"), Floor.SquadCount));

		const FLinearColor CountColour = Floor.bSquadmateDown
			? UOneBrahamUIStyle::EmergencyRed()
			: UOneBrahamUIStyle::SignalBlue();

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			Geometry.ToPaintGeometry(FVector2D(70.f, RowHeight - 2.f),
				FSlateLayoutTransform(FVector2D(RowLeft + RowWidth - 74.f, RowTop + 2.f))),
			CountText,
			CountFont,
			ESlateDrawEffect::None,
			CountColour.CopyWithAlpha(FadeAlpha));
	}
}

void UOneBrahamFloorPlanWidget::PaintHeaderAndFooter(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& Geometry, float PanelHeight, float PanelWidth) const
{
	// --- Header ------------------------------------------------------------
	{
		const FSlateFontInfo TitleFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Medium, true);

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(FVector2D(LabelColumnWidth, HeaderHeight),
				FSlateLayoutTransform(FVector2D(PanelPadding, PanelPadding * 0.5f))),
			PlanData.BuildingName,
			TitleFont,
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::LightGrey().CopyWithAlpha(FadeAlpha));

		// Where the player is, in the lift's own words, under the building name:
		// "FLOOR 9 - SERVER AND COMMS". The plan is for orientation, and
		// orientation starts with knowing where you are.
		const FSlateFontInfo PlaceFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro, false);

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(FVector2D(LabelColumnWidth * 1.6f, 16.f),
				FSlateLayoutTransform(FVector2D(PanelPadding, PanelPadding * 0.5f + 24.f))),
			PlanData.LocalFloorLabel,
			PlaceFont,
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::SignalBlue().CopyWithAlpha(FadeAlpha));
	}

	// --- Extraction banner -------------------------------------------------
	if (PlanData.bExtractionInProgress)
	{
		const float BarHeight = 6.f;
		const float BarWidth = LabelColumnWidth * 1.35f;
		const float BarLeft = PanelPadding;
		const float BarTop = PanelPadding * 0.5f + 44.f;

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(FVector2D(BarWidth, BarHeight),
				FSlateLayoutTransform(FVector2D(BarLeft, BarTop))),
			&UOneBrahamUIStyle::PanelBrush(0.25f * FadeAlpha, 1.f),
			ESlateDrawEffect::None,
			FLinearColor::White);

		const float Progress = FMath::Clamp(PlanData.ExtractionProgress01, 0.f, 1.f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			Geometry.ToPaintGeometry(FVector2D(BarWidth * Progress, BarHeight),
				FSlateLayoutTransform(FVector2D(BarLeft, BarTop))),
			&UOneBrahamUIStyle::PanelBrush(FadeAlpha, 1.f),
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::SafetyYellow());

		const FText ExtractionText = PlanData.ExtractingSquadId == INDEX_NONE
			? NSLOCTEXT("OneBraham", "PlanExtraction", "WINCH RUNNING")
			: FText::Format(NSLOCTEXT("OneBraham", "PlanExtractionSquad", "SQUAD {0} ON THE WINCH"),
				FText::AsNumber(PlanData.ExtractingSquadId + 1));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			Geometry.ToPaintGeometry(FVector2D(BarWidth, 16.f),
				FSlateLayoutTransform(FVector2D(BarLeft, BarTop + BarHeight + 3.f))),
			ExtractionText,
			UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro, true),
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::SafetyYellow().CopyWithAlpha(FadeAlpha));
	}

	// --- Footer ------------------------------------------------------------
	{
		const float FooterTop = PanelHeight - FooterHeight + 4.f;
		const FSlateFontInfo FooterFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro, false);

		// Minutes and seconds, because a timer that reads "07:14" is readable at
		// a glance and "434" is not.
		const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt(PlanData.MatchTimeRemaining));
		const FText ClockText = FText::FromString(FString::Printf(TEXT("%02d:%02d"),
			TotalSeconds / 60, TotalSeconds % 60));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(FVector2D(80.f, 16.f),
				FSlateLayoutTransform(FVector2D(PanelPadding, FooterTop))),
			ClockText,
			FooterFont,
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.4f).CopyWithAlpha(FadeAlpha));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(FVector2D(LabelColumnWidth * 1.1f, 16.f),
				FSlateLayoutTransform(FVector2D(PanelPadding + 76.f, FooterTop))),
			PlanData.LockdownStageLabel,
			FooterFont,
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::EmergencyRed().CopyWithAlpha(0.9f * FadeAlpha));
	}
}

int32 UOneBrahamFloorPlanWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 CurrentLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	// --- Animation, taken from the clock rather than from a tick -------------
	// A C++ UUserWidget starts at EWidgetTickFrequency::Auto, and the engine
	// reads Auto as "tick only if this widget has a tick implemented in
	// Blueprint" - an overridden NativeTick does not count as one. So a widget
	// that animates itself in NativeTick is a widget that may simply never
	// animate, and the plan would open at zero alpha and stay there.
	//
	// Nothing here needs a tick. A visible widget is painted every frame, so the
	// fade and the downed pulse are computed from the world clock each time the
	// painter runs. It costs two subtractions, it cannot be switched off by a
	// property nobody remembered, and it is exactly as smooth as the framerate.
	if (bPlanVisible)
	{
		const UWorld* World = GetWorld();
		const float Now = World != nullptr ? World->GetTimeSeconds() : OpenedAtSeconds;

		PulseTime = FMath::Max(0.f, Now - OpenedAtSeconds);
		FadeAlpha = FadeSeconds > KINDA_SMALL_NUMBER
			? FMath::Min(1.f, PulseTime / FadeSeconds)
			: 1.f;
	}

	if (!bPlanVisible || PlanData.Floors.Num() == 0 || FadeAlpha <= KINDA_SMALL_NUMBER)
	{
		return CurrentLayer;
	}

	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();

	if (WidgetSize.X < 40.f || WidgetSize.Y < 40.f)
	{
		return CurrentLayer;
	}

	// --- Panel -------------------------------------------------------------
	const float PanelWidth = FMath::Min(WidgetSize.X - PanelPadding * 2.f, LabelColumnWidth + PanelPadding * 2.f);
	const float PanelHeight = FMath::Min(WidgetSize.Y - PanelPadding * 2.f,
		HeaderHeight + FooterHeight + PlanData.Floors.Num() * RowHeight + PanelPadding * 2.5f);

	// Anchored to the left edge, vertically centred: the plan is read next to
	// the player's field of view, not on top of the crosshair.
	const float PanelLeft = PanelPadding;
	const float PanelTop = (WidgetSize.Y - PanelHeight) * 0.5f;

	const FSlateBrush PanelBrush = UOneBrahamUIStyle::PanelBrush(BackgroundAlpha * FadeAlpha, 6.f);

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		CurrentLayer,
		AllottedGeometry.ToPaintGeometry(FVector2D(PanelWidth, PanelHeight),
			FSlateLayoutTransform(FVector2D(PanelLeft, PanelTop))),
		&PanelBrush,
		ESlateDrawEffect::None,
		FLinearColor::White);

	// A hairline of signal blue down the left edge: the brand mark, at the size
	// a HUD can afford.
	const FSlateBrush AccentBrush = UOneBrahamUIStyle::PanelBrush(FadeAlpha, 0.f);

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		CurrentLayer + 1,
		AllottedGeometry.ToPaintGeometry(FVector2D(3.f, PanelHeight),
			FSlateLayoutTransform(FVector2D(PanelLeft, PanelTop))),
		&AccentBrush,
		ESlateDrawEffect::None,
		UOneBrahamUIStyle::SignalBlue());

	// --- Content, in panel space ------------------------------------------
	// Everything below draws in a space whose origin is the panel's top-left.
	const FGeometry PanelGeometry = AllottedGeometry.MakeChild(
		FVector2D(PanelWidth, PanelHeight),
		FSlateLayoutTransform(FVector2D(PanelLeft, PanelTop)));

	const int32 ContentLayer = CurrentLayer + 2;

	PaintHeaderAndFooter(OutDrawElements, ContentLayer, PanelGeometry, PanelHeight, PanelWidth);

	// Floors are drawn top-down: the Skydeck is at the top, because that is the
	// direction the players are trying to move and a map that counts upwards is
	// a map that matches the building.
	const float ListTop = PanelPadding + (PlanData.bExtractionInProgress ? 70.f : 52.f);

	for (int32 Index = 0; Index < PlanData.Floors.Num(); ++Index)
	{
		const float RowTop = ListTop + Index * RowHeight;

		if (RowTop + RowHeight > PanelHeight - FooterHeight)
		{
			break;
		}

		PaintFloorRow(OutDrawElements, ContentLayer, PanelGeometry, PlanData.Floors[Index], RowTop, RowHeight);
	}

	return CurrentLayer + 3;
}
