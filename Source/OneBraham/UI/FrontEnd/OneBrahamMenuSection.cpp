// Copyright (c) One Braham. All Rights Reserved.

#include "UI/FrontEnd/OneBrahamMenuSection.h"

namespace OneBrahamMenuSectionLayout
{
	// Fractions of the building's width (west on the left).
	constexpr float CrownFrom16 = 0.34f;		// the crown over the canteen half
	constexpr float CrownFrom17 = 0.44f;		// the roof terrace steps back again
	constexpr float CrownTo17 = 0.94f;
	constexpr float StairA0 = 0.50f, StairA1 = 0.57f;
	constexpr float Lift0 = 0.63f, Lift1 = 0.77f;
	constexpr float StairB0 = 0.84f, StairB1 = 0.90f;
	constexpr int32 BaffleTicks = 30;
}

FString FOneBrahamMenuSection::FloorName(int32 Floor)
{
	switch (Floor)
	{
	case 0:		return TEXT("RECEPTION \u00B7 PLAZA");
	case 1:		return TEXT("LOBBY \u00B7 LAB");
	case 2:		return TEXT("MEETING ROOMS");
	case 9:		return TEXT("SERVER \u00B7 COMMS");
	case 12:
	case 13:
	case 14:	return TEXT("EXECUTIVE");
	case 15:	return TEXT("CANTEEN \u00B7 SKYDECK");
	case 16:	return TEXT("MANAGEMENT SUITE");
	case 17:	return TEXT("WINTER GARDEN");
	case 18:	return TEXT("ROOF");
	default:	return TEXT("OPEN PLAN");
	}
}

void FOneBrahamMenuSection::Paint(const OneBrahamPaint::FPainter& C, const FVector2D& Position, const FVector2D& Size,
	const FOneBrahamMenuSectionParams& P, double Now)
{
	using namespace OneBrahamPaint;
	using namespace OneBrahamMenuSectionLayout;

	const bool bFull = P.Style == FOneBrahamMenuSectionParams::EStyle::Full;
	const FLinearColor Ink = Tok(EOneBrahamColour::LightGrey);
	const FLinearColor Yellow = Tok(EOneBrahamColour::SafetyYellow);

	auto FloorAlpha = [&P, Now](int32 Floor) -> float
	{
		if (P.RevealAt <= 0.0)
		{
			return 1.f;
		}
		const double Start = P.RevealAt + P.RevealPerFloor * Floor;
		return UOneBrahamUIStyle::Tween(Now, Start, 0.12f, EOneBrahamEase::Ribbon);
	};
	auto FloorLit = [&P](int32 Floor) { return Floor >= 0 && Floor < 32 && (P.LitFloors & (1u << Floor)) != 0; };

	// --- Mini: a lift indicator of slabs ---------------------------------------
	if (!bFull)
	{
		const float Gap = 2.f;
		const float H = (Size.Y - Gap * (NumFloors - 1)) / NumFloors;
		for (int32 Floor = 0; Floor < NumFloors; ++Floor)
		{
			const float A = FloorAlpha(Floor);
			if (A <= 0.f)
			{
				continue;
			}
			const float Y = Position.Y + Size.Y - (Floor + 1) * H - Floor * Gap;
			const float X0 = Position.X + (Floor >= 17 ? Size.X * CrownFrom17 : Floor >= 16 ? Size.X * CrownFrom16 : 0.f);
			const FLinearColor Colour = Floor == P.TargetFloor ? Yellow : Fade(Ink, FloorLit(Floor) ? 0.7f : 0.12f);
			Box(C, FVector2D(X0, Y), FVector2D(Position.X + Size.X - X0, H), Fade(Colour, A));
		}
		return;
	}

	// --- Full -----------------------------------------------------------------
	const FSlateFontInfo LabelFont = Type(EOneBrahamType::TelemetryLight, 11.f, 60);
	const float LabelW = P.bLabels ? FMath::Min(230.f, Size.X * 0.44f) : 0.f;
	const float X0 = Position.X + LabelW + (P.bLabels ? 18.f : 0.f);
	const float X1 = Position.X + Size.X - 18.f;
	const float W = X1 - X0;
	const float Top = Position.Y + 40.f;
	const float Bottom = Position.Y + Size.Y - 6.f;
	const float FH = (Bottom - Top) / NumFloors;
	auto FloorY = [Bottom, FH](int32 Floor) { return Bottom - Floor * FH; };
	auto SpanOf = [X0, W](int32 Floor, float& OutA, float& OutB)
	{
		OutA = X0 + (Floor >= 17 ? W * CrownFrom17 : Floor >= 16 ? W * CrownFrom16 : 0.f);
		OutB = X0 + (Floor >= 17 ? W * CrownTo17 : W);
	};

	// The plaza: a ground line running out both sides of the tower.
	PenLine(C, FVector2D(Position.X + LabelW - 10.f, Bottom), FVector2D(Position.X + Size.X, Bottom), Fade(Ink, 0.35f * FloorAlpha(0)), 1.f);

	for (int32 Floor = 0; Floor < NumFloors; ++Floor)
	{
		const float A = FloorAlpha(Floor);
		if (A <= 0.f)
		{
			continue;
		}
		const bool bLit = FloorLit(Floor);
		const bool bTarget = Floor == P.TargetFloor;
		const float LineAlpha = (bLit ? 0.55f : 0.16f) * A;
		const FLinearColor Stroke = bTarget ? Fade(Yellow, A) : Fade(Ink, LineAlpha);

		float SA, SB;
		SpanOf(Floor, SA, SB);
		const float YBottom = FloorY(Floor);
		const float YTop = FloorY(Floor + 1);

		// Lit floors carry a faint field, so "in play" reads as filled, not only
		// as brighter lines (no state by colour alone).
		if (bLit || bTarget)
		{
			Box(C, FVector2D(SA, YTop), FVector2D(SB - SA, YBottom - YTop), bTarget ? Fade(Yellow, 0.16f * A) : Fade(Ink, 0.035f * A));
		}

		// Slab (the floor's own) - the reception's double height has no slab at 1,
		// only the gallery's edge stub.
		if (Floor == 1)
		{
			PenLine(C, FVector2D(SA, YBottom), FVector2D(SA + W * 0.32f, YBottom), Stroke, 1.f);
		}
		else
		{
			PenLine(C, FVector2D(SA, YBottom), FVector2D(SB, YBottom), Stroke, 1.f);
		}

		// Facade edges.
		PenLine(C, FVector2D(SA, YBottom), FVector2D(SA, YTop), Stroke, 1.f);
		PenLine(C, FVector2D(SB, YBottom), FVector2D(SB, YTop), Stroke, 1.f);

		// Cores, hatched, where the floor plate has them.
		auto Core = [&](float F0, float F1)
		{
			const float CA = X0 + W * F0;
			const float CB = X0 + W * F1;
			if (CA < SA || CB > SB)
			{
				return;
			}
			Hatch(C, FVector2D(CA, YTop + 1.f), FVector2D(CB - CA, YBottom - YTop - 1.f), 5.f, Fade(Ink, 0.22f * A * (bLit ? 1.f : 0.5f)));
			PenLine(C, FVector2D(CA, YBottom), FVector2D(CA, YTop), Fade(Ink, 0.3f * A), 1.f);
			PenLine(C, FVector2D(CB, YBottom), FVector2D(CB, YTop), Fade(Ink, 0.3f * A), 1.f);
		};
		Core(StairA0, StairA1);
		Core(Lift0, Lift1);
		Core(StairB0, StairB1);

		// The reception's baffle ceiling: thirty ticks hanging under floor 2.
		if (Floor == 1)
		{
			const float Ceiling = FloorY(2);
			for (int32 Tick = 0; Tick < BaffleTicks; ++Tick)
			{
				const float X = SA + 6.f + (W - 12.f) * Tick / (BaffleTicks - 1);
				PenLine(C, FVector2D(X, Ceiling + 1.f), FVector2D(X, Ceiling + 5.f), Fade(Ink, 0.45f * A), 1.f);
			}
		}

		// The fins: one tick every 8 px down the east face.
		for (float Y = YTop + 3.f; Y < YBottom - 1.f; Y += 8.f)
		{
			PenLine(C, FVector2D(SB + 2.f, Y), FVector2D(SB + 7.f, Y), Fade(Ink, 0.3f * A), 1.f);
		}

		// The Winter Garden: a lattice.
		if (Floor == 17)
		{
			const FVector2D Origin(SA, YTop);
			const FVector2D Box2(SB - SA, YBottom - YTop);
			Hatch(C, Origin, Box2, 9.f, Fade(Ink, 0.25f * A));
			// The other diagonal: lines X - Y = K across the box.
			for (float K = -Box2.Y + 4.5f; K < Box2.X; K += 9.f)
			{
				const float Y0 = FMath::Max(0.f, -K);
				const float Y1 = FMath::Min(Box2.Y, Box2.X - K);
				if (Y1 > Y0)
				{
					PenLine(C, Origin + FVector2D(Y0 + K, Y0), Origin + FVector2D(Y1 + K, Y1), Fade(Ink, 0.25f * A), 1.f);
				}
			}
		}

		// The Skydeck balcony on 15: the glass balustrade beyond the crown.
		if (Floor == 15)
		{
			const float Rail = YTop - FH * 0.28f;
			PenLine(C, FVector2D(SA, YTop), FVector2D(SA, Rail), Fade(Ink, 0.3f * A), 1.f);
			DottedH(C, SA, X0 + W * CrownFrom16, Rail, Fade(Ink, 0.4f * A), 3.f, 1.f, 0.f);
		}

		// Labels.
		if (P.bLabels && Floor != 1)
		{
			const FString Label = FString::Printf(TEXT("%02d  %s"), Floor, *FloorName(Floor));
			const float LabelBaseline = Floor == 0 ? (FloorY(0) + FloorY(2)) * 0.5f + 4.f : (YBottom + YTop) * 0.5f + 4.f;
			const FLinearColor LabelColour = bTarget ? Yellow : Fade(Ink, bLit ? 0.5f : 0.22f);
			TextOnBaseline(C, Position.X + LabelW, LabelBaseline, Label, LabelFont, Fade(LabelColour, A), 1.f);
			if (bTarget)
			{
				PenLine(C, FVector2D(Position.X + LabelW + 4.f, LabelBaseline - 4.f), FVector2D(SA - 4.f, LabelBaseline - 4.f), Fade(Yellow, 0.6f * A), 1.f);
			}
		}
	}

	// The roof slab and parapet, with the stroke of the last floor.
	const float RoofA = FloorAlpha(NumFloors);
	{
		float SA, SB;
		SpanOf(17, SA, SB);
		const float Roof = FloorY(NumFloors);
		const bool bRoofTarget = P.TargetFloor == NumFloors;
		PenLine(C, FVector2D(SA, Roof), FVector2D(SB, Roof), bRoofTarget ? Fade(Yellow, RoofA) : Fade(Ink, 0.55f * RoofA), 1.f);
		PenLine(C, FVector2D(SA, Roof), FVector2D(SA, Roof - 5.f), Fade(Ink, 0.4f * RoofA), 1.f);
		PenLine(C, FVector2D(SB, Roof), FVector2D(SB, Roof - 5.f), Fade(Ink, 0.4f * RoofA), 1.f);
	}

	// --- ASCENT: the route up Stair A, and the helipad ------------------------
	if (P.bRoute && RoofA > 0.f)
	{
		const float StairX = X0 + W * (StairA0 + StairA1) * 0.5f;
		const float Roof = FloorY(NumFloors);
		const FVector2D Pad(X0 + W * 0.69f, Roof - 18.f);
		const TArray<FVector2D> Route = {
			FVector2D(Position.X + LabelW, Bottom - 4.f),
			FVector2D(StairX, Bottom - 4.f),
			FVector2D(StairX, Roof - 4.f),
			FVector2D(Pad.X - 9.f, Roof - 4.f),
		};

		// Dashes 6 on, 4 off, marching up the route at 60 px/s.
		const float Dash = 6.f;
		const float Period = 10.f;
		const float March = UOneBrahamUIStyle::ReduceMotion() ? 0.f : FMath::Fmod(static_cast<float>(Now) * 60.f, Period);
		float Along = -March;
		for (int32 Index = 0; Index + 1 < Route.Num(); ++Index)
		{
			const FVector2D A = Route[Index];
			const FVector2D B = Route[Index + 1];
			const float Length = FVector2D::Distance(A, B);
			const FVector2D Dir = (B - A) / FMath::Max(Length, 0.001f);
			float S = FMath::Fmod(Along, Period);
			if (S > 0.f)
			{
				S -= Period;
			}
			for (; S < Length; S += Period)
			{
				const float From = FMath::Max(S, 0.f);
				const float To = FMath::Min(S + Dash, Length);
				if (To > From)
				{
					PenLine(C, A + Dir * From, A + Dir * To, Fade(Yellow, RoofA), 1.f);
				}
			}
			Along += Length;
		}

		PenCircle(C, Pad, 7.f, Fade(Yellow, RoofA), 1.f);
		TextOnBaseline(C, Pad.X, Pad.Y + 4.f, TEXT("H"), Type(EOneBrahamType::Label, 10.f), Fade(Yellow, RoofA), 0.5f);
	}

	// --- FIRE DRILL: the points ------------------------------------------------
	for (int32 Index = 0; Index < P.ControlPoints.Num(); ++Index)
	{
		const int32 Floor = P.ControlPoints[Index];
		const float A = FloorAlpha(Floor);
		if (Floor < 0 || Floor >= NumFloors || A <= 0.f)
		{
			continue;
		}
		const FVector2D Centre(X0 + W * 0.3f, (FloorY(Floor) + FloorY(Floor + 1)) * 0.5f);
		PenCircle(C, Centre, 8.f, Tok(EOneBrahamColour::Navy, 0.9f * A), 0.f);
		PenCircle(C, Centre, 8.f, Fade(Tok(EOneBrahamColour::White), A), 1.f);
		const FString Letter = FString::Chr(TEXT('A') + Index);
		TextOnBaseline(C, Centre.X, Centre.Y + 4.5f, Letter, Type(EOneBrahamType::Label, 12.f), Fade(Tok(EOneBrahamColour::White), A), 0.5f);
	}
}
