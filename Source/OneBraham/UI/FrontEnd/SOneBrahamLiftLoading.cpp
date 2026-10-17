// Copyright (c) One Braham. All Rights Reserved.

#include "UI/FrontEnd/SOneBrahamLiftLoading.h"

#include "UI/FrontEnd/OneBrahamMenuSection.h"
#include "UI/FrontEnd/OneBrahamPaint.h"
#include "UObject/UObjectGlobals.h"

using namespace OneBrahamPaint;

namespace OneBrahamLiftLoading
{
	/** The lift adverts (DT_HUDStrings carries the same lines for the HUD lane). */
	const TCHAR* const Adverts[] = {
		TEXT("Taking the stairs burns seven calories per floor. Today it may also save your life."),
		TEXT("Please do not prop fire doors open."),
		TEXT("Hot desks are cleaned at 18:00. Do not be on one."),
		TEXT("The lifts are thinking about what they've done."),
	};
	constexpr float AdvertSeconds = 6.f;
	constexpr float FloorSeconds = 0.8f;
}

void SOneBrahamLiftLoading::Construct(const FArguments& InArgs)
{
	ModeName = InArgs._ModeName;
	Status = InArgs._Status;
	Squad = InArgs._Squad;
	TargetFloor = InArgs._TargetFloor;
	StartedAt = FPlatformTime::Seconds();

	// Resolve every face this page and the section drawing use, here on the
	// game thread: the movie player paints on its own.
	for (int32 Token = 0; Token <= static_cast<int32>(EOneBrahamType::Aside); ++Token)
	{
		UOneBrahamUIStyle::TypeFont(static_cast<EOneBrahamType>(Token), 16.f);
	}
}

int32 SOneBrahamLiftLoading::CounterFloorAt(double Seconds)
{
	const int32 Steps = FMath::Max(0, FMath::FloorToInt(Seconds / OneBrahamLiftLoading::FloorSeconds));
	if (Steps <= 3)
	{
		return Steps;
	}
	// 03 to 08 and round again, until the map is in.
	return 3 + (Steps - 3) % 6;
}

int32 SOneBrahamLiftLoading::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const double Now = FPlatformTime::Seconds();
	const double T = Now - StartedAt;
	const FPainter C(OutDrawElements, AllottedGeometry, LayerId);
	const FVector2D S = C.Size();

	Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::OLEDBlack));

	// Real progress: how much of the loader's queue has drained since its peak.
	const int32 Queued = GetNumAsyncPackages();
	PeakPackages = FMath::Max(PeakPackages, Queued);
	if (PeakPackages > 0)
	{
		Progress = FMath::Max(Progress, 1.f - static_cast<float>(Queued) / static_cast<float>(PeakPackages));
	}

	const float X = S.X * 0.07f;
	const float In = FMath::Clamp(static_cast<float>(T / 0.4), 0.f, 1.f);

	TextOnBaseline(C, X, S.Y * 0.27f, TEXT("LIFT 3  \u00B7  GOING UP"), Type(EOneBrahamType::TelemetryLight, 13.f, 220),
		Tok(EOneBrahamColour::LightGrey, 0.6f * In));

	// The floor counter, in the lift's own LED face.
	const int32 Floor = CounterFloorAt(T);
	TextOnBaseline(C, X - 8.f, S.Y * 0.47f, FString::Printf(TEXT("%02d"), Floor), Type(EOneBrahamType::LED, 160.f),
		Tok(EOneBrahamColour::SafetyYellow, In));
	// An up arrow beside it, lit on the half-second.
	const bool bArrow = FMath::Fmod(T, 0.8) < 0.5;
	const FVector2D Arrow(X + 210.f, S.Y * 0.47f - 96.f);
	PenPath(C, { Arrow + FVector2D(-14.f, 12.f), Arrow + FVector2D(0.f, -8.f), Arrow + FVector2D(14.f, 12.f), Arrow + FVector2D(-14.f, 12.f) },
		Tok(EOneBrahamColour::SafetyYellow, (bArrow ? 1.f : 0.25f) * In), 3.f);

	TextOnBaseline(C, X, S.Y * 0.58f, ModeName.ToString(), Type(EOneBrahamType::Verb, 48.f, -10), Tok(EOneBrahamColour::White, In));
	TextOnBaseline(C, X, S.Y * 0.62f, Status.ToString().ToUpper(), Type(EOneBrahamType::TelemetryLight, 13.f, 160),
		Tok(EOneBrahamColour::LightGrey, 0.6f * In));
	Ribbon(C, FVector2D(X, S.Y * 0.635f), FVector2D(220.f * UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, static_cast<float>(T / 0.6)), 2.f));

	// The squad, each with a ready lamp.
	for (int32 Index = 0; Index < Squad.Num() && Index < 4; ++Index)
	{
		const float Y = S.Y * 0.69f + Index * 28.f;
		PenCircle(C, FVector2D(X + 5.f, Y - 5.f), 4.f, Tok(EOneBrahamColour::SignalBlue, In), 0.f);
		TextOnBaseline(C, X + 20.f, Y, Squad[Index].ToUpper(), Type(EOneBrahamType::Label, 14.f, 80), Tok(EOneBrahamColour::White, 0.9f * In));
		TextOnBaseline(C, X + 220.f, Y, Index == 0 ? TEXT("READY") : TEXT("IN THE LIFT"), Type(EOneBrahamType::TelemetryLight, 11.f, 160),
			Tok(EOneBrahamColour::LightGrey, 0.5f * In));
	}

	// The shaft: the section, lit bottom-up as the map loads.
	FOneBrahamMenuSectionParams Params;
	const int32 Lit = FMath::Clamp(1 + FMath::FloorToInt(Progress * (FOneBrahamMenuSection::NumFloors - 1)), 1, FOneBrahamMenuSection::NumFloors);
	Params.LitFloors = (1u << Lit) - 1u;
	Params.TargetFloor = TargetFloor;
	Params.bLabels = false;
	Params.RevealAt = StartedAt;
	FOneBrahamMenuSection::Paint(C, FVector2D(S.X * 0.62f, S.Y * 0.16f), FVector2D(S.X * 0.18f, S.Y * 0.66f), Params, Now);
	TextOnBaseline(C, S.X * 0.80f, S.Y * 0.86f, FString::Printf(TEXT("%3d%%"), FMath::RoundToInt(Progress * 100.f)),
		Type(EOneBrahamType::Telemetry, 12.f, 120), Tok(EOneBrahamColour::LightGrey, 0.5f), 1.f);

	// One lift advert at a time, six seconds each, crossfading.
	const int32 Count = UE_ARRAY_COUNT(OneBrahamLiftLoading::Adverts);
	const int32 Advert = FMath::FloorToInt(T / OneBrahamLiftLoading::AdvertSeconds) % Count;
	const float Into = static_cast<float>(FMath::Fmod(T, static_cast<double>(OneBrahamLiftLoading::AdvertSeconds)));
	const float Fade = FMath::Min(FMath::Clamp(Into / 0.6f, 0.f, 1.f), FMath::Clamp((OneBrahamLiftLoading::AdvertSeconds - Into) / 0.6f, 0.f, 1.f));
	TextOnBaseline(C, X, S.Y * 0.90f, OneBrahamLiftLoading::Adverts[Advert], Type(EOneBrahamType::Aside, 20.f),
		Tok(EOneBrahamColour::LightGrey, 0.7f * Fade));

	return C.TopLayer();
}
