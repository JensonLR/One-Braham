// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamCrosshairWidget.h"

#include "OneBraham.h"
#include "UI/OneBrahamUIStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SWidget.h"

UOneBrahamCrosshairWidget::UOneBrahamCrosshairWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The crosshair is a pure canvas: it draws and nothing else. It must never
	// intercept a click, so it is hit-test invisible.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UOneBrahamCrosshairWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

float UOneBrahamCrosshairWidget::ComputeTickGapPixels(float WidgetScale) const
{
	// The honest version of a "dynamic crosshair": the gap is the real cone,
	// projected onto a reference plane. At 1.4 degrees and 12 m that is a small
	// gap; at 6 degrees it is a warning.
	const float ReferenceDistanceCm = FMath::Max(100.f, ReferenceDistanceCm);
	const float HalfWidthCm = FMath::Tan(FMath::DegreesToRadians(SpreadDegrees * 0.5f)) * ReferenceDistanceCm;

	// A first-person camera in this project has ~90 degrees horizontal FOV, so the
	// screen-half-width maps to tan(45) * distance. Everything cancels except the
	// ratio of the two tangents, which is why this is stable across FOV settings.
	const float ScreenHalfWidthCm = ReferenceDistanceCm; // tan(45) = 1
	const float HalfWidthFraction = HalfWidthCm / ScreenHalfWidthCm;

	// Convert to pixels using the widget's own size (a square viewport is assumed
	// for the horizontal component, which is exactly how FPS crosshairs work).
	const float HalfWidthPixels = (WidgetScale > 0.f ? WidgetScale : 1.f);

	const float Gap = BaseGapPixels + (HalfWidthFraction * SpreadScale * HalfWidthPixels);

	return FMath::Clamp(Gap, BaseGapPixels, MaxGapPixels);
}

float UOneBrahamCrosshairWidget::ComputeDamageRadiusPixels() const
{
	// The arc sits closer in at low resolutions so it is always on screen.
	return DamageArcRadiusPixels;
}

int32 UOneBrahamCrosshairWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 CurrentLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	if (!bCrosshairVisible)
	{
		return CurrentLayer;
	}

	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
	const FVector2D Centre = WidgetSize * 0.5f;

	// The projection needs the half-width in pixels: use the smaller axis so the
	// crosshair never drifts off screen in an ultrawide window.
	const float HalfExtentPixels = FMath::Min(WidgetSize.X, WidgetSize.Y) * 0.5f;

	auto DrawLine = [&](const FVector2D& From, const FVector2D& To, const FLinearColor& Colour, float Thickness)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurrentLayer,
			AllottedGeometry.ToPaintGeometry(),
			TArray<FVector2D>({ From, To }),
			ESlateDrawEffect::None,
			Colour,
			true,           // antialias: a 1.6 px line without AA looks broken at 4K
			Thickness);
	};

	const FLinearColor BaseColour = bBlockedInteraction
		? UOneBrahamUIStyle::EmergencyRed()
		: (bAimingDownSights ? UOneBrahamUIStyle::Crosshair() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::Crosshair(), 0.25f));

	// --- Spread ticks -----------------------------------------------------
	const float Gap = ComputeTickGapPixels(HalfExtentPixels);
	const float SpreadAlphaClamped = FMath::Clamp(SpreadAlpha, 0.f, 1.f);

	// ADS tightens the ticks visually as well as mechanically, which is the tell
	// that the shot will go where the player expects.
	const float TickLength = TickLengthPixels * FMath::Lerp(1.f, 0.75f, bAimingDownSights ? 1.f : 0.f);
	const float Thickness = TickThicknessPixels * FMath::Lerp(1.f, 0.85f, SpreadAlphaClamped);

	// Four ticks: right, left, up, down.
	DrawLine(Centre + FVector2D(Gap, 0.f), Centre + FVector2D(Gap + TickLength, 0.f), BaseColour, Thickness);
	DrawLine(Centre - FVector2D(Gap, 0.f), Centre - FVector2D(Gap + TickLength, 0.f), BaseColour, Thickness);
	DrawLine(Centre + FVector2D(0.f, Gap), Centre + FVector2D(0.f, Gap + TickLength), BaseColour, Thickness);
	DrawLine(Centre - FVector2D(0.f, Gap), Centre - FVector2D(0.f, Gap + TickLength), BaseColour, Thickness);

	// --- Centre dot -------------------------------------------------------
	// One pixel, always present: without it the eyes drift at long range.
	const float DotSize = 1.f + (bAimingDownSights ? 0.5f : 0.f);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		CurrentLayer,
		AllottedGeometry.ToPaintGeometry(FVector2D(DotSize, DotSize),
			FSlateLayoutTransform(Centre - FVector2D(DotSize * 0.5f, DotSize * 0.5f))),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
		ESlateDrawEffect::None,
		BaseColour);

	// --- Reload arc -------------------------------------------------------
	if (ReloadProgress > 0.f && ReloadProgress < 1.f)
	{
		// A single tick sweeping around the crosshair: the player reads the reload
		// without looking away from the target, which is the entire point.
		constexpr int32 ArcSegments = 24;
		const float ArcRadius = Gap + TickLength + 6.f;
		const float StartAngle = -HALF_PI;
		const float Sweep = 2.f * PI * ReloadProgress;

		TArray<FVector2D> ArcPoints;
		ArcPoints.Reserve(ArcSegments + 1);

		for (int32 Index = 0; Index <= ArcSegments; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(ArcSegments);
			const float Angle = StartAngle + Sweep * Alpha;
			ArcPoints.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * ArcRadius);
		}

		FSlateDrawElement::MakeLines(OutDrawElements, CurrentLayer + 1, AllottedGeometry.ToPaintGeometry(),
			ArcPoints, ESlateDrawEffect::None, UOneBrahamUIStyle::SafetyYellow(), true, 2.2f);
	}

	// --- Interaction hold ring -------------------------------------------
	if (InteractionProgress > 0.f && InteractionProgress < 1.f)
	{
		constexpr int32 RingSegments = 28;
		const float RingRadius = Gap + TickLength + 12.f;
		const float Sweep = 2.f * PI * InteractionProgress;

		TArray<FVector2D> RingPoints;
		RingPoints.Reserve(RingSegments + 1);

		for (int32 Index = 0; Index <= RingSegments; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(RingSegments);
			const float Angle = -HALF_PI + Sweep * Alpha;
			RingPoints.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RingRadius);
		}

		FSlateDrawElement::MakeLines(OutDrawElements, CurrentLayer + 1, AllottedGeometry.ToPaintGeometry(),
			RingPoints, ESlateDrawEffect::None, UOneBrahamUIStyle::SignalBlue(), true, 2.6f);
	}

	// --- Hit marker -------------------------------------------------------
	if (HitMarkerAlpha > 0.f)
	{
		// The marker starts wide and snaps inward: motion carries the confirmation
		// far better than a static flash.
		const float Snap = FMath::InterpEaseOut(1.6f, 0.85f, 1.f - HitMarkerAlpha, 2.f);
		const float Inner = HitMarkerRadiusPixels * 0.45f * Snap;
		const float Outer = HitMarkerRadiusPixels * Snap;
		const float Diagonal = Inner * 0.7071f;
		const float DiagonalOuter = Outer * 0.7071f;

		FLinearColor MarkerColour = bHitMarkerHeadshot
			? UOneBrahamUIStyle::SafetyYellow()
			: UOneBrahamUIStyle::Crosshair();
		MarkerColour.A = HitMarkerAlpha;

		const float MarkerThickness = bHitMarkerKill ? 2.6f : 2.0f;

		// Four diagonal ticks, mirrored around the centre.
		const FVector2D Directions[4] = {
			FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f)
		};

		for (const FVector2D& Direction : Directions)
		{
			DrawLine(Centre + Direction * Diagonal, Centre + Direction * DiagonalOuter, MarkerColour, MarkerThickness);
		}

		// A kill adds a horizontal cross-out: instantly legible, and it means the
		// player never has to read the kill feed to know the fight is over.
		if (bHitMarkerKill)
		{
			const float KillWidth = HitMarkerRadiusPixels * 1.15f;
			const float KillY = -HitMarkerRadiusPixels * 1.45f;

			DrawLine(Centre + FVector2D(-KillWidth, KillY), Centre + FVector2D(KillWidth, KillY),
				MarkerColour, 2.4f);
		}
	}

	// --- Damage direction arc --------------------------------------------
	if (DamageDirectionDegrees > -999.f && DamageAlpha > 0.f)
	{
		// A short arc at the bearing of the attacker, fading with the timer. It is
		// drawn outside the crosshair so it never obscures the target.
		const float Radius = ComputeDamageRadiusPixels();
		const float Angle = FMath::DegreesToRadians(DamageDirectionDegrees - 90.f); // -90: screen space is Y-down
		const float ArcHalfWidth = FMath::DegreesToRadians(14.f);

		constexpr int32 ArcSegments = 12;
		TArray<FVector2D> ArcPoints;
		ArcPoints.Reserve(ArcSegments + 1);

		for (int32 Index = 0; Index <= ArcSegments; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(ArcSegments);
			const float SegmentAngle = Angle - ArcHalfWidth + (2.f * ArcHalfWidth * Alpha);
			ArcPoints.Add(Centre + FVector2D(FMath::Cos(SegmentAngle), FMath::Sin(SegmentAngle)) * Radius);
		}

		FLinearColor DamageColour = UOneBrahamUIStyle::EmergencyRed();
		DamageColour.A = DamageAlpha;

		FSlateDrawElement::MakeLines(OutDrawElements, CurrentLayer + 2, AllottedGeometry.ToPaintGeometry(),
			ArcPoints, ESlateDrawEffect::None, DamageColour, true, DamageArcThicknessPixels);
	}

	// --- Low health vignette ---------------------------------------------
	if (LowHealthPulse > 0.f)
	{
		// A thin red line hugging the crosshair rather than a full-screen vignette:
		// the screen edges are already carrying the health bar, and a pulsing screen
		// is the single most complained-about effect in playtests.
		FLinearColor PulseColour = UOneBrahamUIStyle::EmergencyRed();
		PulseColour.A = LowHealthPulse * 0.55f;

		const float PulseRadius = Gap + TickLength + 18.f;
		const FVector2D Corners[4] = {
			FVector2D(-0.6f, -0.6f), FVector2D(0.6f, -0.6f), FVector2D(0.6f, 0.6f), FVector2D(-0.6f, 0.6f)
		};

		TArray<FVector2D> Loop;
		Loop.Reserve(5);
		for (const FVector2D& Corner : Corners)
		{
			Loop.Add(Centre + Corner * PulseRadius);
		}
		Loop.Add(Centre + Corners[0] * PulseRadius);

		FSlateDrawElement::MakeLines(OutDrawElements, CurrentLayer + 3, AllottedGeometry.ToPaintGeometry(),
			Loop, ESlateDrawEffect::None, PulseColour, true, 1.2f);
	}

	return CurrentLayer + 4;
}
