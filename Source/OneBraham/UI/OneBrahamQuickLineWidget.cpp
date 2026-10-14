// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamQuickLineWidget.h"

#include "OneBraham.h"
#include "UI/OneBrahamUIStyle.h"
#include "Rendering/DrawElements.h"

namespace
{
	/** Segments are laid out clockwise from the top, which is where the eye
	 *  starts and where the most-used line goes. */
	constexpr float SegmentStartOffsetDegrees = -90.f;
}

UOneBrahamQuickLineWidget::UOneBrahamQuickLineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The wheel takes focus while it is open so it can read number keys. It is
	// gameplay input, not UI input: the player's mouse still aims the character
	// if the game mode leaves the cursor alone, and a wheel that stole the
	// camera would be worse than no wheel.
	SetVisibility(ESlateVisibility::Collapsed);
	SetIsFocusable(true);
}

void UOneBrahamQuickLineWidget::SetWheelData(const FOneBrahamQuickLineData& InData)
{
	WheelData = InData;
}

void UOneBrahamQuickLineWidget::SetWheelVisible(bool bVisible)
{
	if (bWheelVisible == bVisible)
	{
		return;
	}

	bWheelVisible = bVisible;

	if (bWheelVisible)
	{
		// A fast press-release must not speak whatever was hovered last time:
		// the segment list is newest-first in the player's mind, and a stale
		// hover is how "never mind" gets said by mistake.
		HoveredSegment = INDEX_NONE;
		OpenAlpha = 0.f;

		const UWorld* World = GetWorld();
		OpenedAtSeconds = World != nullptr ? World->GetTimeSeconds() : 0.f;

		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	else
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

int32 UOneBrahamQuickLineWidget::SegmentAtPosition(const FVector2D& Position, const FVector2D& WidgetSize) const
{
	const int32 Count = WheelData.Entries.Num();

	if (Count <= 0)
	{
		return INDEX_NONE;
	}

	const FVector2D Centre = WidgetSize * 0.5f;
	const FVector2D Offset = Position - Centre;
	const float Distance = Offset.Size();

	// The dead zone is not a bug: releasing inside it chooses nothing, so a
	// panicked press-release on the way to cover does not broadcast.
	if (Distance < InnerRadius || Distance > OuterRadius)
	{
		return INDEX_NONE;
	}

	float Angle = FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X));
	Angle = FRotator::NormalizeAxis(Angle - SegmentStartOffsetDegrees);

	if (Angle < 0.f)
	{
		Angle += 360.f;
	}

	const float SegmentDegrees = 360.f / static_cast<float>(Count);
	const int32 Index = FMath::Clamp(FMath::FloorToInt(Angle / SegmentDegrees), 0, Count - 1);

	// The gap is a visual separator, not a hole in the logic: aiming into the
	// gap still picks the segment it belongs to, because a player aiming just
	// off a label should still be understood.
	return Index;
}

bool UOneBrahamQuickLineWidget::ChooseSegment(int32 SegmentIndex)
{
	if (!WheelData.Entries.IsValidIndex(SegmentIndex))
	{
		return false;
	}

	const FOneBrahamQuickLineEntry& Entry = WheelData.Entries[SegmentIndex];

	if (!Entry.bAvailable)
	{
		// The cooldown is enforced on the server regardless. This is so the
		// player is told, rather than pressing a greyed button and hearing
		// nothing.
		return false;
	}

	OnQuickLineChosen.Broadcast(Entry.LineId);
	SetWheelVisible(false);

	return true;
}

bool UOneBrahamQuickLineWidget::CommitHoveredSegment()
{
	return ChooseSegment(HoveredSegment);
}

FReply UOneBrahamQuickLineWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bWheelVisible)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}

	// ScreenToLocal, not the raw cursor: the widget is not full-screen and the
	// ring is centred on the widget, so the centre has to move with it.
	const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	HoveredSegment = SegmentAtPosition(LocalPosition, InGeometry.GetLocalSize());

	return FReply::Handled();
}

FReply UOneBrahamQuickLineWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (!bWheelVisible || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const int32 Segment = SegmentAtPosition(LocalPosition, InGeometry.GetLocalSize());

	// A click inside the dead zone closes the wheel without speaking, which is
	// how a player cancels: same motion as opening it, opposite intent.
	if (Segment == INDEX_NONE)
	{
		SetWheelVisible(false);
		return FReply::Handled();
	}

	ChooseSegment(Segment);

	return FReply::Handled();
}

FReply UOneBrahamQuickLineWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bWheelVisible)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	// 1-8 pick a segment directly. The numbering matches the visual order
	// clockwise from the top, so the key a player learns is the key that stays
	// right if the wheel is ever re-ordered by the squad's own preferences.
	const FKey Key = InKeyEvent.GetKey();

	static const FKey NumberKeys[] =
	{
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(NumberKeys); ++Index)
	{
		if (Key == NumberKeys[Index])
		{
			ChooseSegment(Index);
			return FReply::Handled();
		}
	}

	// Escape closes without speaking. The menu key's release also closes it; this
	// is for players who press Escape out of habit, and it costs four lines.
	if (Key == EKeys::Escape)
	{
		SetWheelVisible(false);
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

int32 UOneBrahamQuickLineWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 CurrentLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	const int32 Count = WheelData.Entries.Num();

	if (!bWheelVisible || Count == 0)
	{
		return CurrentLayer;
	}

	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();

	if (WidgetSize.X < InnerRadius * 2.f || WidgetSize.Y < InnerRadius * 2.f)
	{
		return CurrentLayer;
	}

	const FVector2D Centre = WidgetSize * 0.5f;
	const float Alpha = WheelAlpha * OpenAlpha;
	const float SegmentDegrees = 360.f / static_cast<float>(Count);

	// --- Outer ring --------------------------------------------------------
	// A thin circle so the wheel has an edge to aim against even when every
	// segment is dark.
	{
		TArray<FVector2D> RingPoints;
		RingPoints.Reserve(49);

		for (int32 Index = 0; Index <= 48; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(Index * (360.f / 48.f));
			RingPoints.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * OuterRadius);
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurrentLayer,
			AllottedGeometry.ToPaintGeometry(),
			RingPoints,
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.5f).CopyWithNewOpacity(0.55f * Alpha),
			true,
			1.5f);
	}

	// --- Segments ----------------------------------------------------------
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FOneBrahamQuickLineEntry& Entry = WheelData.Entries[Index];
		const bool bHovered = Index == HoveredSegment;

		const float StartDegrees = SegmentStartOffsetDegrees + Index * SegmentDegrees + SectorGapDegrees * 0.5f;
		const float EndDegrees = StartDegrees + SegmentDegrees - SectorGapDegrees;

		// Twelve steps is enough that a 43-degree arc has no visible corners.
		constexpr int32 Steps = 12;
		const float StepDegrees = (EndDegrees - StartDegrees) / static_cast<float>(Steps);

		// Unavailable segments are drawn in the muted emergency colour rather
		// than greyed to invisibility: a player who cannot say something needs
		// to know that the button works and the line is cooling down.
		FLinearColor SegmentColour = Entry.bAvailable
			? UOneBrahamUIStyle::Navy()
			: UOneBrahamUIStyle::EmergencyRed();

		if (bHovered)
		{
			SegmentColour = Entry.bAvailable
				? UOneBrahamUIStyle::SignalBlue()
				: UOneBrahamUIStyle::EmergencyRed();
		}
		else
		{
			SegmentColour = UOneBrahamUIStyle::Muted(SegmentColour, 0.55f);
		}

		// The segment itself: a thick arc along the ring's mid-radius. Slate has
		// no filled-sector primitive, and the three ways to fake one are custom
		// vertices (whose FSlateVertex signature moves between engine versions),
		// a rotated box (Slate layout transforms carry scale and translation,
		// not rotation), or this - a polyline whose thickness is the ring's
		// width. This costs one draw call per segment, uses the same call the
		// compass already makes, and reads as a radar ring at any HUD scale.
		const float MidRadius = (InnerRadius + OuterRadius) * 0.5f;
		const float BandThickness = (OuterRadius - InnerRadius) * 0.86f;

		TArray<FVector2D> ArcPoints;
		ArcPoints.Reserve(Steps + 1);

		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(StartDegrees + StepDegrees * Step);
			ArcPoints.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * MidRadius);
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurrentLayer + 1,
			AllottedGeometry.ToPaintGeometry(),
			ArcPoints,
			ESlateDrawEffect::None,
			SegmentColour.CopyWithNewOpacity(Alpha * (bHovered ? 0.95f : 0.62f)),
			true,
			BandThickness);

		// --- Label -------------------------------------------------------
		const float LabelAngle = FMath::DegreesToRadians(StartDegrees + (EndDegrees - StartDegrees) * 0.5f);
		const FVector2D LabelCentre = Centre + FVector2D(FMath::Cos(LabelAngle), FMath::Sin(LabelAngle))
			* (InnerRadius + (OuterRadius - InnerRadius) * 0.5f);

		const FText LabelText = Entry.bAvailable
			? Entry.Label
			: FText::Format(NSLOCTEXT("OneBraham", "CommsCooldown", "{0} ({1}s)"),
				Entry.Label, FText::AsNumber(FMath::CeilToInt(Entry.CooldownRemaining)));

		const FSlateFontInfo LabelFont = UOneBrahamUIStyle::Font(
			EOneBrahamTextSize::Small, bHovered);

		FSlateDrawElement::MakeText(
			OutDrawElements,
			CurrentLayer + 2,
			AllottedGeometry.ToPaintGeometry(FVector2D(120.f, 18.f),
				FSlateLayoutTransform(LabelCentre - FVector2D(60.f, 9.f))),
			LabelText,
			LabelFont,
			ESlateDrawEffect::None,
			(bHovered ? UOneBrahamUIStyle::LightGrey() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.35f))
				.CopyWithNewOpacity(Alpha));
	}

	// --- Centre ------------------------------------------------------------
	{
		// The hub says what is about to be said. It is the only place a player
		// can confirm a choice before committing, and it is why the wheel is
		// usable without reading the ring.
		const FText HubText = WheelData.Entries.IsValidIndex(HoveredSegment)
			? WheelData.Entries[HoveredSegment].Label
			: NSLOCTEXT("OneBraham", "CommsNothingSelected", "RELEASE TO CANCEL");

		FSlateDrawElement::MakeText(
			OutDrawElements,
			CurrentLayer + 2,
			AllottedGeometry.ToPaintGeometry(FVector2D(InnerRadius * 2.f, 20.f),
				FSlateLayoutTransform(Centre - FVector2D(InnerRadius, 10.f))),
			HubText,
			UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small, true),
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::LightGrey().CopyWithNewOpacity(0.85f * Alpha));

		// Push-to-talk state, at the bottom of the hub: holding the radio key
		// and choosing a quick line are two different things, and a player who
		// is transmitting should be able to see that they are.
		if (WheelData.bTransmitting)
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				CurrentLayer + 2,
				AllottedGeometry.ToPaintGeometry(FVector2D(InnerRadius * 2.f, 16.f),
					FSlateLayoutTransform(Centre - FVector2D(InnerRadius, -14.f))),
				NSLOCTEXT("OneBraham", "CommsTransmitting", "LIVE"),
				UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro, true),
				ESlateDrawEffect::None,
				UOneBrahamUIStyle::EmergencyRed().CopyWithNewOpacity(0.9f * Alpha));
		}
	}

	return CurrentLayer + 3;
}
