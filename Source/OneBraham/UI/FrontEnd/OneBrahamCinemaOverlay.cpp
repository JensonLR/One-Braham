// Copyright (c) One Braham. All Rights Reserved.

#include "UI/FrontEnd/OneBrahamCinemaOverlay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Spacer.h"
#include "UI/FrontEnd/OneBrahamPaint.h"

UOneBrahamCinemaOverlay::UOneBrahamCinemaOverlay(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UOneBrahamCinemaOverlay::SetLetterbox(float TargetFrac, float Seconds)
{
	const double Now = FPlatformTime::Seconds();
	BarFrom = CurrentLetterbox(Now);
	BarTo = FMath::Clamp(TargetFrac, 0.f, 0.5f);
	BarAt = Now;
	BarSeconds = FMath::Max(0.f, Seconds);
}

void UOneBrahamCinemaOverlay::Dip(float OutS, float HoldS, float InS)
{
	DipAt = FPlatformTime::Seconds();
	DipOut = FMath::Max(0.f, OutS);
	DipHold = FMath::Max(0.f, HoldS);
	DipIn = FMath::Max(0.01f, InS);
	bHoldBlack = false;
	FadeAt = -1000.0;
}

void UOneBrahamCinemaOverlay::FadeFromBlack(float Seconds)
{
	bHoldBlack = false;
	FadeAt = FPlatformTime::Seconds();
	FadeSeconds = FMath::Max(0.01f, Seconds);
	DipAt = -1000.0;
}

void UOneBrahamCinemaOverlay::HoldBlack()
{
	bHoldBlack = true;
	DipAt = -1000.0;
	FadeAt = -1000.0;
}

void UOneBrahamCinemaOverlay::ShowSuper(FText Eyebrow, FText Line, FText Aside, float HoldS)
{
	SuperEyebrow = MoveTemp(Eyebrow);
	SuperLine = MoveTemp(Line);
	SuperAside = MoveTemp(Aside);
	SuperAt = FPlatformTime::Seconds();
	SuperHold = FMath::Max(0.f, HoldS);
}

void UOneBrahamCinemaOverlay::ClearSuper()
{
	SuperAt = -1000.0;
}

float UOneBrahamCinemaOverlay::CurrentLetterbox(double Now) const
{
	if (BarSeconds <= 0.f)
	{
		return BarTo;
	}
	return FMath::Lerp(BarFrom, BarTo, UOneBrahamUIStyle::Tween(Now, BarAt, BarSeconds, EOneBrahamEase::Ribbon));
}

float UOneBrahamCinemaOverlay::CurrentBlack(double Now) const
{
	if (bHoldBlack)
	{
		return 1.f;
	}

	float Black = 0.f;
	if (FadeAt > 0.0)
	{
		Black = FMath::Max(Black, 1.f - FMath::Clamp(static_cast<float>((Now - FadeAt) / FadeSeconds), 0.f, 1.f));
	}

	const double T = Now - DipAt;
	if (T >= 0.0)
	{
		if (T < DipOut)
		{
			Black = FMath::Max(Black, DipOut > 0.f ? static_cast<float>(T / DipOut) : 1.f);
		}
		else if (T < DipOut + DipHold)
		{
			Black = 1.f;
		}
		else if (T < DipOut + DipHold + DipIn)
		{
			Black = FMath::Max(Black, 1.f - static_cast<float>((T - DipOut - DipHold) / DipIn));
		}
	}
	return FMath::Clamp(Black, 0.f, 1.f);
}

int32 UOneBrahamCinemaOverlay::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace OneBrahamPaint;

	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;
	const double Now = FPlatformTime::Seconds();
	const FVector2D Size(AllottedGeometry.GetLocalSize());
	const FPainter C(OutDrawElements, AllottedGeometry, Layer);

	// --- supers (under the bars, over the picture) -------------------------
	const float SuperT = static_cast<float>(Now - SuperAt);
	const float SuperFadeIn = 0.4f;
	const float SuperFadeOut = 0.3f;
	if (SuperT >= 0.f && SuperT < SuperFadeIn + SuperHold + SuperFadeOut)
	{
		const float Alpha = SuperT < SuperFadeIn ? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, SuperT / SuperFadeIn)
			: SuperT < SuperFadeIn + SuperHold ? 1.f
			: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Exit, (SuperT - SuperFadeIn - SuperHold) / SuperFadeOut);
		const float Rise = (1.f - FMath::Min(1.f, SuperT / SuperFadeIn)) * 6.f;

		if (SuperStyle == EOneBrahamSuperStyle::ColdOpen)
		{
			float Y = Size.Y * 0.76f + Rise;
			const float X = Size.X * 0.07f;
			if (!SuperEyebrow.IsEmpty())
			{
				TextOnBaseline(C, X, Y - 46.f, SuperEyebrow.ToString(), Type(EOneBrahamType::TelemetryLight, 13.f, 180),
					Tok(EOneBrahamColour::LightGrey, 0.7f * Alpha));
			}
			TextOnBaseline(C, X, Y, SuperLine.ToString(), Type(EOneBrahamType::Aside, 34.f), Tok(EOneBrahamColour::SignageWhite, Alpha));
			if (!SuperAside.IsEmpty())
			{
				TextOnBaseline(C, X, Y + 30.f, SuperAside.ToString(), Type(EOneBrahamType::Hint, 13.f, 120),
					Tok(EOneBrahamColour::LightGrey, 0.55f * Alpha));
			}
		}
		else
		{
			// The stage card: never in the 40-60% crosshair band.
			const float CX = Size.X * 0.5f;
			const float Settle = UOneBrahamUIStyle::ReduceMotion() ? 1.f : UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, SuperT / 1.2f);
			const int32 Tracking = FMath::RoundToInt(FMath::Lerp(200.f, 40.f, Settle));
			TextOnBaseline(C, CX, Size.Y * 0.22f, SuperEyebrow.ToString(), Type(EOneBrahamType::TelemetryLight, 12.f, 240),
				Tok(EOneBrahamColour::SafetyYellow, Alpha), 0.5f);
			TextOnBaseline(C, CX, Size.Y * 0.22f + 92.f, SuperLine.ToString(), Type(EOneBrahamType::Title, 88.f, Tracking),
				Tok(EOneBrahamColour::White, Alpha), 0.5f);
			TextOnBaseline(C, CX, Size.Y * 0.22f + 136.f, SuperAside.ToString(), Type(EOneBrahamType::Aside, 24.f),
				Tok(EOneBrahamColour::SignageWhite, 0.9f * Alpha), 0.5f);
		}
	}

	// --- letterbox -----------------------------------------------------------
	const float Frac = IsUltrawide(Size) ? 0.f : CurrentLetterbox(Now);
	if (Frac > 0.0005f)
	{
		const float H = FMath::RoundToFloat(Size.Y * Frac);
		const FLinearColor Bar = Tok(EOneBrahamColour::OLEDBlack, BarAlpha);
		Box(C, FVector2D(0.f, 0.f), FVector2D(Size.X, H), Bar);
		Box(C, FVector2D(0.f, Size.Y - H), FVector2D(Size.X, H), Bar);
	}

	// --- dips ----------------------------------------------------------------
	const float Black = CurrentBlack(Now);
	if (Black > 0.001f)
	{
		Box(C, FVector2D::ZeroVector, Size, Tok(EOneBrahamColour::OLEDBlack, Black));
	}
	return C.TopLayer();
}
