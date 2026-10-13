// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamCompassWidget.h"

#include "OneBraham.h"
#include "UI/OneBrahamUIStyle.h"
#include "Rendering/DrawElements.h"

UOneBrahamCompassWidget::UOneBrahamCompassWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

FLinearColor UOneBrahamCompassWidget::ColourForMarker(const FOneBrahamHUDCompassMarker& Marker) const
{
	if (Marker.MarkerType == TEXT("Extraction"))
	{
		return UOneBrahamUIStyle::SuccessGreen();
	}
	if (Marker.MarkerType == TEXT("Enemy"))
	{
		return UOneBrahamUIStyle::EmergencyRed();
	}
	if (Marker.MarkerType == TEXT("Objective"))
	{
		return UOneBrahamUIStyle::SafetyYellow();
	}
	if (Marker.MarkerType == TEXT("Keycard") || Marker.MarkerType == TEXT("Item"))
	{
		return UOneBrahamUIStyle::SignalBlue();
	}

	return Marker.Colour.A > 0.f ? Marker.Colour : UOneBrahamUIStyle::LightGrey();
}

float UOneBrahamCompassWidget::ProjectMarkerToStrip(const FVector& WorldLocation, float& OutAngleDelta,
	float& OutDistance) const
{
	const FVector ToMarker = WorldLocation - ViewerLocation;

	// Horizontal bearing only: the strip answers "which way", the panel answers
	// "how far up the building".
	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(ToMarker.Y, ToMarker.X));
	const float DeltaAngle = FRotator::NormalizeAxis(TargetYaw - HeadingYaw);

	OutAngleDelta = DeltaAngle;
	OutDistance = ToMarker.Size();

	// Returns -1..1 across the strip, clamped for off-screen markers.
	const float HalfField = FMath::Max(1.f, VisibleFieldDegrees * 0.5f);
	return FMath::Clamp(DeltaAngle / HalfField, -1.f, 1.f);
}

int32 UOneBrahamCompassWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 CurrentLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	if (!bCompassVisible)
	{
		return CurrentLayer;
	}

	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
	if (WidgetSize.X < 4.f || WidgetSize.Y < 4.f)
	{
		return CurrentLayer;
	}

	const float CentreX = WidgetSize.X * 0.5f;
	const float HalfField = FMath::Max(1.f, VisibleFieldDegrees * 0.5f);
	const float DeguPerPixel = VisibleFieldDegrees / WidgetSize.X;

	// --- Background strip -------------------------------------------------
	const FSlateBrush StripBrush = UOneBrahamUIStyle::PanelBrush(StripBackgroundAlpha, 3.f);
	const float StripTop = WidgetSize.Y - StripHeightPixels;

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		CurrentLayer,
		AllottedGeometry.ToPaintGeometry(FVector2D(WidgetSize.X, StripHeightPixels),
			FSlateLayoutTransform(FVector2D(0.f, StripTop))),
		&StripBrush,
		ESlateDrawEffect::None,
		FLinearColor::White);

	// --- Cardinal ticks ---------------------------------------------------
	const FSlateFontInfo LabelFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro, true);
	const FLinearColor TickColour = UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.35f);

	// Snap to the nearest tick so the strip slides smoothly instead of stepping.
	const float FirstTick = FMath::FloorToFloat((HeadingYaw - HalfField) / CardinalSpacingDegrees)
		* CardinalSpacingDegrees;

	for (float TickAngle = FirstTick; TickAngle <= HeadingYaw + HalfField; TickAngle += CardinalSpacingDegrees)
	{
		const float Delta = FRotator::NormalizeAxis(TickAngle - HeadingYaw);
		if (FMath::Abs(Delta) > HalfField)
		{
			continue;
		}

		const float X = CentreX + (Delta / DeguPerPixel);
		const bool bMajor = FMath::IsNearlyEqual(FMath::Fmod(FMath::Abs(TickAngle), 45.f), 0.f, 0.5f);
		const float TickHeight = bMajor ? StripHeightPixels * 0.55f : StripHeightPixels * 0.28f;

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurrentLayer + 1,
			AllottedGeometry.ToPaintGeometry(),
			TArray<FVector2D>({ FVector2D(X, StripTop + 2.f), FVector2D(X, StripTop + 2.f + TickHeight) }),
			ESlateDrawEffect::None,
			TickColour,
			true,
			1.f);

		if (!bMajor)
		{
			continue;
		}

		// Cardinal letters: N at 0, then every 45 degrees. 0 degrees is +X in world
		// space, so the labels are offset accordingly.
		static const TPair<float, const TCHAR*> Cardinals[] = {
			{ 0.f,   TEXT("E") },
			{ 45.f,  TEXT("NE") },
			{ 90.f,  TEXT("N") },
			{ 135.f, TEXT("NW") },
			{ 180.f, TEXT("W") },
			{ 225.f, TEXT("SW") },
			{ 270.f, TEXT("S") },
			{ 315.f, TEXT("SE") }
		};

		FText Label;
		const float Normalised = FMath::Fmod(FMath::Abs(TickAngle) + 360.f, 360.f);

		for (const TPair<float, const TCHAR*>& Cardinal : Cardinals)
		{
			if (FMath::IsNearlyEqual(Normalised, Cardinal.Key, 0.5f))
			{
				Label = FText::FromString(Cardinal.Value);
				break;
			}
		}

		if (!Label.IsEmpty())
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				CurrentLayer + 2,
				AllottedGeometry.ToPaintGeometry(FVector2D(40.f, 14.f),
					FSlateLayoutTransform(FVector2D(X - 20.f, StripTop + TickHeight + 3.f))),
				Label,
				LabelFont,
				ESlateDrawEffect::None,
				UOneBrahamUIStyle::LightGrey());
		}
	}

	// --- Objective / team markers ----------------------------------------
	const FSlateFontInfo GlyphFont = UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro, true);

	for (const FOneBrahamHUDCompassMarker& Marker : Markers)
	{
		float AngleDelta = 0.f;
		float Distance = 0.f;
		const float Normalised = ProjectMarkerToStrip(Marker.WorldLocation, AngleDelta, Distance);

		const bool bOffscreen = FMath::Abs(AngleDelta) > EdgeClampDegrees;
		if (bOffscreen && !bShowOffscreenMarkers)
		{
			continue;
		}

		// Off-screen markers are clamped to the edge so the player knows which way
		// to turn rather than losing the objective entirely.
		const float StripX = bOffscreen
			? CentreX + FMath::Sign(Normalised) * (WidgetSize.X * 0.5f - 12.f)
			: CentreX + Normalised * (WidgetSize.X * 0.5f);

		const FLinearColor MarkerColour = ColourForMarker(Marker);
		const float MarkerTop = StripTop - 9.f;

		// A small downward triangle: readable at a glance, and it does not need a
		// texture or a font glyph to render identically on every machine.
		const FVector2D TrianglePoints[3] = {
			FVector2D(StripX, MarkerTop + 8.f),
			FVector2D(StripX - 5.f, MarkerTop),
			FVector2D(StripX + 5.f, MarkerTop)
		};

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurrentLayer + 3,
			AllottedGeometry.ToPaintGeometry(),
			TArray<FVector2D>({ TrianglePoints[1], TrianglePoints[2], TrianglePoints[0], TrianglePoints[1] }),
			ESlateDrawEffect::None,
			MarkerColour,
			true,
			2.f);

		if (!Marker.Glyph.IsEmpty())
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				CurrentLayer + 4,
				AllottedGeometry.ToPaintGeometry(FVector2D(48.f, 14.f),
					FSlateLayoutTransform(FVector2D(StripX - 24.f, MarkerTop - 14.f))),
				FText::FromString(Marker.Glyph),
				GlyphFont,
				ESlateDrawEffect::None,
				MarkerColour);
		}

		// Distance under the marker, in metres, or "FL n" when the target is on
		// another floor - which is the more useful number in a tower.
		const FText DistanceText = UOneBrahamUIStyle::DistanceLabel(Distance, Marker.MarkerType == TEXT("OtherFloor") ? 1 : 0);

		FSlateDrawElement::MakeText(
			OutDrawElements,
			CurrentLayer + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(48.f, 12.f),
				FSlateLayoutTransform(FVector2D(StripX - 24.f, StripTop + StripHeightPixels - 13.f))),
			DistanceText,
			UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro),
			ESlateDrawEffect::None,
			UOneBrahamUIStyle::Muted(MarkerColour, 0.3f));
	}

	// --- Centre line ------------------------------------------------------
	// The thin marker over the player's own heading: without it the strip is
	// ambiguous about which direction "straight ahead" is.
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		CurrentLayer + 5,
		AllottedGeometry.ToPaintGeometry(),
		TArray<FVector2D>({
			FVector2D(CentreX, StripTop - 6.f),
			FVector2D(CentreX, StripTop + StripHeightPixels + 4.f)
		}),
		ESlateDrawEffect::None,
		UOneBrahamUIStyle::SignalBlue(),
		true,
		1.4f);

	return CurrentLayer + 6;
}
