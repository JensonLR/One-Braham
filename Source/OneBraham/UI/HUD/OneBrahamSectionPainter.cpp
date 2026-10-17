// Copyright (c) One Braham. All Rights Reserved.

#include "UI/HUD/OneBrahamSectionPainter.h"

#include "OneBraham.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"
#include "Rendering/DrawElements.h"
#include "UI/HUD/OneBrahamBuildingStatus.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/HUD/OneBrahamTween.h"

using namespace OneBrahamHUDStyle;

namespace OneBrahamSection
{
	// --- Spine geometry (px at 1080p) ------------------------------------------
	constexpr float CellW = 14.f;
	constexpr float CellH = 10.f;
	constexpr float CellGap = 3.f;
	constexpr float HelipadBand = 20.f;
	constexpr float TickLength = 8.f;
	constexpr float DotSize = 4.f;

	/** Reveal for one floor: 0 before its turn, 1 after, a short fade between,
	 *  so the stroke-in reads as floors arriving rather than popping. */
	float FloorReveal(const FOneBrahamSectionParams& Params, int32 Floor)
	{
		const float Count = FMath::Max(1, Params.FloorCount);
		const float Position = Params.Reveal * Count;
		return FMath::Clamp(Position - static_cast<float>(Floor), 0.f, 1.f);
	}

	bool Has(uint32 Mask, int32 Floor)
	{
		return (Mask & FOneBrahamSectionParams::Bit(Floor)) != 0;
	}

	bool IsLit(const FOneBrahamSectionParams& Params, int32 Floor)
	{
		return Params.LitFloors == 0 || Has(Params.LitFloors, Floor);
	}
}

void FOneBrahamSectionParams::FillFromStatus(const FOneBrahamBuildingStatus& Status)
{
	FloorCount = FMath::Max(1, Status.FloorCount());
	SealedFloors = PurgedFloors = NextSealFloors = 0;
	LocalFloor = Status.LocalFloor;
	ExtractionFloor = Status.Mode == EOneBrahamMatchMode::Ascent ? Status.ExtractionFloor : INDEX_NONE;
	PurgeCeiling = Status.PurgeCeiling;

	for (const FOneBrahamFloorStatus& Floor : Status.Floors)
	{
		switch (Floor.Condition)
		{
		case EOneBrahamFloorCondition::Sealed:		SealedFloors |= Bit(Floor.FloorIndex); break;
		case EOneBrahamFloorCondition::Purged:		PurgedFloors |= Bit(Floor.FloorIndex); break;
		case EOneBrahamFloorCondition::NextToSeal:	NextSealFloors |= Bit(Floor.FloorIndex); break;
		default: break;
		}
	}

	Squad.Reset();
	for (const FOneBrahamSquadMarker& Marker : Status.Squad)
	{
		if (Marker.Floor == INDEX_NONE)
		{
			continue;
		}
		FOneBrahamSectionDot& Dot = Squad.AddDefaulted_GetRef();
		Dot.Floor = Marker.Floor;
		Dot.bDowned = Marker.bDowned;
		Dot.bLocal = Marker.bLocal;
	}
}

FVector2D FOneBrahamSectionPainter::SpineSize(int32 FloorCount)
{
	using namespace OneBrahamSection;
	const int32 Count = FMath::Max(1, FloorCount);
	return FVector2D(CellW + 2.f + TickLength + 4.f * (DotSize + 2.f) + 34.f,
		HelipadBand + Count * (CellH + CellGap) - CellGap);
}

FString FOneBrahamSectionPainter::FloorLabel(const FOneBrahamSectionParams& Params, int32 Floor)
{
	FString Name;
	if (Params.FloorLabels.IsValidIndex(Floor))
	{
		Name = Params.FloorLabels[Floor];
	}
	else if (const UOneBrahamBuildingLayoutAsset* Layout = UOneBrahamBuildingLayoutAsset::Get(nullptr))
	{
		Name = Layout->GetFloorShortName(Floor).ToString();
	}
	Name.ReplaceInline(TEXT(" / "), TEXT(" · "));
	return FString::Printf(TEXT("%02d  %s"), Floor, *Name);
}

// ==========================================================================
//  Spine: the lift indicator
// ==========================================================================
static void PaintSpine(FOneBrahamPainter& P, const FOneBrahamSectionParams& Params)
{
	using namespace OneBrahamSection;

	const int32 Count = FMath::Max(1, Params.FloorCount);
	const FLinearColor Line = Params.LineColour;
	const float Blink1Hz = OneBrahamPulse::Blink(Params.Time, OneBrahamPulse::ImminentHz, Params.bReduceMotion);
	const float DownPulse = OneBrahamPulse::Breath(Params.Time, OneBrahamPulse::TeammateHz, 0.35f, 1.f);

	// The helipad above the top cell: a ring with an H, because the roof is
	// where this whole column is pointing.
	{
		const FVector2D Centre(CellW * 0.5f, HelipadBand * 0.5f - 2.f);
		const FLinearColor Pad = Params.ExtractionFloor != INDEX_NONE ? SafetyYellow() : WithAlpha(LightGrey(), 0.5f);
		P.Ring(Centre, 6.5f, WithAlpha(Pad, 0.85f), 1.f);
		P.Text(Centre - FVector2D(0.f, 6.5f), TEXT("H"), Font(EOneBrahamHUDType::Label, 9.f), WithAlpha(Pad, 0.95f), EOneBrahamTextAnchor::Centre);
	}

	for (int32 Floor = 0; Floor < Count; ++Floor)
	{
		const float Reveal = FloorReveal(Params, Floor);
		if (Reveal <= 0.f)
		{
			continue;
		}

		FOneBrahamPainter Row = P.Faded(Reveal);
		const int32 RowIndex = Count - 1 - Floor;
		const FVector2D Cell(0.f, HelipadBand + RowIndex * (CellH + CellGap));
		const FVector2D Size(CellW, CellH);

		const bool bLocal = Floor == Params.LocalFloor;
		const bool bSealed = Has(Params.SealedFloors, Floor);
		const bool bPurged = Has(Params.PurgedFloors, Floor);
		const bool bNext = Has(Params.NextSealFloors, Floor);
		const bool bExit = Floor == Params.ExtractionFloor;

		// --- the cell ---------------------------------------------------------
		if (bPurged)
		{
			Row.Rect(Cell, Size, EmergencyRed());
			Row.Cross(Cell + Size * 0.5f, 6.f, White(), 1.5f);
		}
		else if (bSealed)
		{
			Row.Rect(Cell, Size, WithAlpha(EmergencyRed(), 0.35f));
			Row.Hatch(Cell, Size, WithAlpha(EmergencyRed(), 0.9f), 3.f, 1.f);
		}
		else if (bExit)
		{
			Row.Rect(Cell, Size, SafetyYellow());
		}
		else
		{
			Row.Rect(Cell, Size, WithAlpha(LightGrey(), 0.07f));
			Row.Outline(Cell, Size, WithAlpha(Line, Line.A * 0.9f), 1.f);
		}

		if (bLocal)
		{
			// Your floor is the solid one - the lit button in the lift.
			Row.Rect(Cell + FVector2D(bPurged || bSealed ? 2.f : 0.f, bPurged || bSealed ? 2.f : 0.f),
				bPurged || bSealed ? Size - FVector2D(4.f, 4.f) : Size, White());
			Row.Rect(FVector2D(CellW + 2.f, Cell.Y + CellH * 0.5f - 1.f), FVector2D(TickLength, 2.f), White());
		}

		if (bNext && !bLocal)
		{
			// About to change: a 1 Hz outline in the warning colour. The outline
			// is the shape cue; the blink is the urgency.
			Row.Outline(Cell - FVector2D(1.f, 1.f), Size + FVector2D(2.f, 2.f), WithAlpha(SafetyYellow(), 0.25f + 0.75f * Blink1Hz), 1.f);
		}
		else if (bNext && bLocal)
		{
			Row.Outline(Cell - FVector2D(2.f, 2.f), Size + FVector2D(4.f, 4.f), WithAlpha(SafetyYellow(), 0.25f + 0.75f * Blink1Hz), 1.f);
		}

		// --- squadmates: a dot each, side by side --------------------------------
		float DotX = CellW + 2.f + (bLocal ? TickLength + 2.f : 2.f);
		for (const FOneBrahamSectionDot& Dot : Params.Squad)
		{
			if (Dot.Floor != Floor || Dot.bLocal)
			{
				continue;
			}
			const FLinearColor DotColour = Dot.bDowned ? WithAlpha(EmergencyRed(), DownPulse) : SignalBlue();
			Row.RoundRect(FVector2D(DotX, Cell.Y + (CellH - DotSize) * 0.5f), FVector2D(DotSize, DotSize), DotColour, DotSize * 0.5f);
			DotX += DotSize + 2.f;
		}

		if (bExit)
		{
			Row.Text(FVector2D(DotX + 2.f, Cell.Y - 1.f), TEXT("EXIT"), Font(EOneBrahamHUDType::Telemetry, 9.f, 60),
				SafetyYellow());
		}
	}
}

// ==========================================================================
//  Full: the architect's section
// ==========================================================================
static void PaintFull(FOneBrahamPainter& P, const FOneBrahamSectionParams& Params, const FVector2D& Box)
{
	using namespace OneBrahamSection;

	const int32 Count = FMath::Max(1, Params.FloorCount);
	const FLinearColor Line = Params.LineColour;
	const float LineA = Line.A;
	const FLinearColor Dim = WithAlpha(Line, LineA * 0.22f);
	const float Blink1Hz = OneBrahamPulse::Blink(Params.Time, OneBrahamPulse::ImminentHz, Params.bReduceMotion);

	const float LabelW = Params.bShowLabels ? FMath::Clamp(Box.X * 0.38f, 90.f, 200.f) : 0.f;
	const float X0 = LabelW + (Params.bShowLabels ? 14.f : 4.f);
	const float X1 = Box.X - 18.f;
	const float BW = FMath::Max(10.f, X1 - X0);
	const float TopMargin = 40.f;
	const float BottomMargin = 16.f;
	const float FH = (Box.Y - TopMargin - BottomMargin) / Count;
	const float Ground = Box.Y - BottomMargin;

	auto FloorTop = [&](int32 Floor) { return Ground - (Floor + 1) * FH; };
	auto FloorMid = [&](int32 Floor) { return Ground - (Floor + 0.5f) * FH; };

	// The revealed height: the drawing strokes in bottom-up.
	const float RevealFloors = Params.Reveal * Count;
	const float RevealTop = Ground - FMath::Clamp(RevealFloors, 0.f, static_cast<float>(Count)) * FH;

	// --- the ground and the plaza ----------------------------------------------------
	P.Segment(FVector2D(X0 - 36.f, Ground), FVector2D(Box.X, Ground), WithAlpha(Line, LineA), 1.f);
	for (float X = X0 - 34.f; X < Box.X - 4.f; X += 7.f)
	{
		P.Segment(FVector2D(X, Ground + 1.f), FVector2D(X + 5.f, Ground + 6.f), WithAlpha(Line, LineA * 0.25f), 1.f);
	}

	// --- floors ----------------------------------------------------------------------
	for (int32 Floor = 0; Floor < Count; ++Floor)
	{
		const float Reveal = FloorReveal(Params, Floor);
		if (Reveal <= 0.f)
		{
			continue;
		}

		FOneBrahamPainter Row = P.Faded(Reveal);
		const float Top = FloorTop(Floor);
		const bool bLit = IsLit(Params, Floor);
		const FLinearColor Slab = bLit ? WithAlpha(Line, LineA) : Dim;
		const FVector2D BandPos(X0, Top);
		const FVector2D BandSize(BW, FH);

		// State fills, under the linework.
		if (Has(Params.PurgedFloors, Floor))
		{
			Row.Rect(BandPos, BandSize, WithAlpha(EmergencyRed(), 0.5f));
			Row.Hatch(BandPos, BandSize, WithAlpha(EmergencyRed(), 0.9f), 5.f, 1.f);
		}
		else if (Has(Params.SealedFloors, Floor))
		{
			Row.Rect(BandPos, BandSize, WithAlpha(EmergencyRed(), 0.22f));
			Row.Hatch(BandPos, BandSize, WithAlpha(EmergencyRed(), 0.6f), 6.f, 1.f);
		}
		else if (Floor == Params.TargetFloor || Floor == Params.ExtractionFloor)
		{
			Row.Rect(BandPos, BandSize, WithAlpha(SafetyYellow(), 0.16f));
		}

		if (Floor == Params.LocalFloor)
		{
			Row.Rect(BandPos, BandSize, WithAlpha(White(), 0.14f));
			Row.Triangle(FVector2D(X0 - 10.f, Top + FH * 0.5f - 5.f), FVector2D(X0 - 3.f, Top + FH * 0.5f),
				FVector2D(X0 - 10.f, Top + FH * 0.5f + 5.f), White());
		}

		if (Has(Params.NextSealFloors, Floor))
		{
			// Seals next: a dashed slab line and a tick on the face, blinking at
			// 1 Hz - a warning, not a fill, so a plan full of them stays legible.
			const FLinearColor Warn = WithAlpha(SafetyYellow(), 0.25f + 0.6f * Blink1Hz);
			Row.DashedLine(FVector2D(X0, Top + FH - 1.f), FVector2D(X1, Top + FH - 1.f), Warn, 1.f, 4.f, 4.f);
			Row.Rect(FVector2D(X1 + 2.f, Top + 2.f), FVector2D(3.f, FH - 4.f), Warn);
		}

		// The slab on top of this floor - except between the two floors of the
		// double-height reception, which is one volume under the baffles.
		const bool bReceptionGap = Floor == 0 && Count > 2;
		if (!bReceptionGap)
		{
			const FLinearColor SlabColour = Floor == Params.TargetFloor ? SafetyYellow() : Slab;
			Row.Segment(FVector2D(X0, Top), FVector2D(X1, Top), SlabColour, Floor == Params.TargetFloor ? 2.f : 1.f);
		}
		else
		{
			// The slab stops at the atrium: a stub at each face.
			Row.Segment(FVector2D(X0, Top), FVector2D(X0 + BW * 0.12f, Top), Slab, 1.f);
			Row.Segment(FVector2D(X1 - BW * 0.08f, Top), FVector2D(X1, Top), Slab, 1.f);
		}

		if (Floor == 1 && Count > 2)
		{
			// The black linear-baffle ceiling of reception: thirty fins hanging
			// from the slab above.
			const float BaffleTop = FloorTop(1);
			for (int32 Index = 0; Index < 30; ++Index)
			{
				const float X = X0 + BW * (0.06f + 0.88f * Index / 29.f);
				Row.Segment(FVector2D(X, BaffleTop), FVector2D(X, BaffleTop + FMath::Min(6.f, FH * 0.25f)), WithAlpha(Line, LineA * 0.7f), 1.f);
			}
		}

		// The Winter Garden: a glazed lattice on 17.
		if (Floor == Count - 1 && Count >= 18)
		{
			Row.PushClip(BandPos + FVector2D(1.f, 1.f), BandSize - FVector2D(2.f, 2.f));
			Row.Hatch(BandPos, BandSize, WithAlpha(Line, LineA * 0.45f), 9.f, 1.f, true);
			Row.Hatch(BandPos, BandSize, WithAlpha(Line, LineA * 0.45f), 9.f, 1.f, false);
			Row.PopClip();
		}

		// --- the label ---------------------------------------------------------------
		if (Params.bShowLabels && FH >= 9.f)
		{
			const bool bHere = Floor == Params.LocalFloor;
			const FLinearColor LabelColour = bHere ? White()
				: (Floor == Params.TargetFloor ? SafetyYellow() : WithAlpha(LightGrey(), bLit ? 0.5f : 0.22f));
			const float Px = FMath::Clamp(FH * 0.42f, 9.f, 11.f);
			const FSlateFontInfo LabelFont = Font(bHere ? EOneBrahamHUDType::Telemetry : EOneBrahamHUDType::TelemetryLight, Px, 40);
			const float TextH = Measure(TEXT("0"), LabelFont).Y;
			Row.Text(FVector2D(0.f, Top + (FH - TextH) * 0.5f), FOneBrahamSectionPainter::FloorLabel(Params, Floor), LabelFont, LabelColour);
		}
	}

	// --- the shell: facade lines, roof, crown ----------------------------------------
	const float Roof = FloorTop(Count - 1);
	{
		P.PushClip(FVector2D(0.f, RevealTop - 1.f), FVector2D(Box.X, Box.Y - RevealTop + 2.f));

		P.Segment(FVector2D(X0, Ground), FVector2D(X0, Roof), WithAlpha(Line, LineA), 1.f);
		P.Segment(FVector2D(X1, Ground), FVector2D(X1, Roof), WithAlpha(Line, LineA), 1.f);

		// The aluminium fins on the east face: a tick every 8 px.
		for (float Y = Ground - 4.f; Y > Roof + 2.f; Y -= 8.f)
		{
			P.Segment(FVector2D(X1, Y), FVector2D(X1 + 5.f, Y), WithAlpha(Line, LineA * 0.6f), 1.f);
		}

		// The cores: Stair A and the lift core, hatched, ground to roof.
		const float StairX = X0 + BW * 0.16f;
		const float StairW = BW * 0.09f;
		const float LiftX = X0 + BW * 0.44f;
		const float LiftW = BW * 0.12f;
		for (const TPair<float, float>& Core : { TPair<float, float>(StairX, StairW), TPair<float, float>(LiftX, LiftW) })
		{
			const FVector2D CorePos(Core.Key, Roof);
			const FVector2D CoreSize(Core.Value, Ground - Roof);
			P.Hatch(CorePos, CoreSize, WithAlpha(Line, LineA * 0.3f), 6.f, 1.f);
			P.Outline(CorePos, CoreSize, WithAlpha(Line, LineA * 0.8f), 1.f);
		}

		P.PopClip();
	}

	if (Params.Reveal >= 0.999f)
	{
		// The crown over the Winter Garden: a parapet and the plant screen.
		P.Segment(FVector2D(X0 + BW * 0.05f, Roof - 8.f), FVector2D(X1 - BW * 0.05f, Roof - 8.f), WithAlpha(Line, LineA * 0.6f), 1.f);
		P.Segment(FVector2D(X0 + BW * 0.05f, Roof - 8.f), FVector2D(X0 + BW * 0.05f, Roof), WithAlpha(Line, LineA * 0.6f), 1.f);
		P.Segment(FVector2D(X1 - BW * 0.05f, Roof - 8.f), FVector2D(X1 - BW * 0.05f, Roof), WithAlpha(Line, LineA * 0.6f), 1.f);

		if (Params.bShowHelipad)
		{
			const FVector2D Pad(X1 - BW * 0.22f, Roof - 21.f);
			const FLinearColor PadColour = Params.TargetFloor != INDEX_NONE || Params.ExtractionFloor != INDEX_NONE
				? SafetyYellow() : WithAlpha(Line, LineA);
			P.Ring(Pad, 7.f, PadColour, 1.f);
			P.Text(Pad - FVector2D(0.f, 7.f), TEXT("H"), Font(EOneBrahamHUDType::Label, 10.f), PadColour, EOneBrahamTextAnchor::Centre);
			P.Segment(FVector2D(Pad.X - 12.f, Roof - 12.f), FVector2D(Pad.X + 12.f, Roof - 12.f), WithAlpha(PadColour, 0.6f), 1.f);
		}
	}

	// --- the route to the roof: up Stair A, travelling ---------------------------------
	if (Params.bShowRoute && Params.Reveal > 0.f)
	{
		const float StairMid = X0 + BW * 0.16f + BW * 0.045f;
		const FVector2D Pad(X1 - BW * 0.22f, Roof - 21.f);
		const TArray<FVector2D> Route = {
			FVector2D(X0 - 30.f, Ground - 3.f),
			FVector2D(StairMid, Ground - 3.f),
			FVector2D(StairMid, Roof - 4.f),
			FVector2D(Pad.X - 9.f, Roof - 4.f),
			FVector2D(Pad.X - 9.f, Pad.Y + 4.f)
		};

		const float Travel = Params.bReduceMotion ? 0.f : static_cast<float>(FMath::Fmod(Params.Time * 60.0, 10.0));
		P.PushClip(FVector2D(0.f, RevealTop - 30.f), FVector2D(Box.X, Box.Y - RevealTop + 32.f));
		float Walked = 0.f;
		for (int32 Index = 0; Index + 1 < Route.Num(); ++Index)
		{
			// The dash phase carries across corners so the line reads as one.
			const float Length = (Route[Index + 1] - Route[Index]).Size();
			P.DashedLine(Route[Index], Route[Index + 1], SafetyYellow(), 1.f, 6.f, 4.f, FMath::Fmod(10.f - Travel + Walked, 10.f) + 10.f);
			Walked += Length;
		}
		P.PopClip();
	}

	// --- FIRE DRILL points ---------------------------------------------------------------
	for (const FOneBrahamSectionPoint& Point : Params.Points)
	{
		if (FloorReveal(Params, Point.Floor) < 1.f)
		{
			continue;
		}
		const FVector2D Centre(X0 + BW * 0.74f, FloorMid(Point.Floor));
		const float R = 8.f;
		switch (Point.Owner)
		{
		case 1:
			P.Circle(Centre, R, SignalBlue());
			break;
		case 2:
			P.Circle(Centre, R, WithAlpha(EmergencyRed(), 0.35f));
			P.Ring(Centre, R, EmergencyRed(), 1.5f);
			break;
		case 3:
			P.Circle(Centre, R, WithAlpha(SignalBlue(), 0.6f));
			P.Arc(Centre, R - 1.f, 0.f, 180.f, EmergencyRed(), 2.f);
			break;
		default:
			P.Circle(Centre, R, WithAlpha(Navy(), 0.85f));
			P.Ring(Centre, R, WithAlpha(LightGrey(), 0.4f), 1.f);
			break;
		}
		const FSlateFontInfo PointFont = Font(EOneBrahamHUDType::Label, 12.f);
		P.Text(Centre - FVector2D(0.f, Measure(Point.Letter, PointFont).Y * 0.5f), Point.Letter, PointFont, White(), EOneBrahamTextAnchor::Centre);
	}

	// --- squad ----------------------------------------------------------------------------
	TMap<int32, int32> PerFloor;
	for (const FOneBrahamSectionDot& Dot : Params.Squad)
	{
		if (Dot.bLocal || FloorReveal(Params, Dot.Floor) < 1.f)
		{
			continue;
		}
		int32& Index = PerFloor.FindOrAdd(Dot.Floor);
		const FVector2D Centre(X0 + BW * 0.6f + Index * 9.f, FloorMid(Dot.Floor));
		const float Pulse = OneBrahamPulse::Breath(Params.Time, OneBrahamPulse::TeammateHz, 0.35f, 1.f);
		P.Circle(Centre, 3.5f, Dot.bDowned ? WithAlpha(EmergencyRed(), Pulse) : SignalBlue());
		++Index;
	}
}

// ==========================================================================
//  Mini: the mode card's thumbnail
// ==========================================================================
static void PaintMini(FOneBrahamPainter& P, const FOneBrahamSectionParams& Params, const FVector2D& Box)
{
	using namespace OneBrahamSection;

	const int32 Count = FMath::Max(1, Params.FloorCount);
	const float FH = Box.Y / (Count + 1.f);

	for (int32 Floor = 0; Floor < Count; ++Floor)
	{
		const float Reveal = FloorReveal(Params, Floor);
		if (Reveal <= 0.f)
		{
			continue;
		}

		const float Y = Box.Y - (Floor + 1) * FH;
		FLinearColor Colour = IsLit(Params, Floor) ? WithAlpha(LightGrey(), 0.7f) : WithAlpha(LightGrey(), 0.12f);
		if (Floor == Params.TargetFloor)
		{
			Colour = SafetyYellow();
		}
		P.Faded(Reveal).Rect(FVector2D(2.f, Y + 1.f), FVector2D(Box.X - 4.f, FMath::Max(1.f, FH - 2.f)), Colour);
	}

	// The roof line and the helipad tick.
	if (Params.Reveal >= 0.999f)
	{
		P.Segment(FVector2D(6.f, FH * 0.5f), FVector2D(Box.X - 6.f, FH * 0.5f), WithAlpha(LightGrey(), 0.5f), 1.f);
	}
}

// ==========================================================================
//  Chart: the end-of-day report
// ==========================================================================
static void PaintChart(FOneBrahamPainter& P, const FOneBrahamSectionParams& Params, const FVector2D& Box)
{
	const int32 Count = FMath::Max(1, Params.FloorCount);
	const FLinearColor Line = Params.LineColour;

	const float LabelW = 40.f;
	const float X0 = LabelW;
	const float X1 = Box.X - 10.f;
	const float PW = FMath::Max(10.f, X1 - X0);
	const float Top = 8.f;
	const float Bottom = Box.Y - 22.f;
	const float FH = (Bottom - Top) / Count;

	auto XAt = [&](float T01) { return X0 + PW * FMath::Clamp(T01, 0.f, 1.f); };
	auto YAt = [&](float Floor) { return Bottom - (Floor + 0.5f) * FH; };

	// Grid: a hairline per slab, the floor numbers down the side.
	for (int32 Floor = 0; Floor <= Count; ++Floor)
	{
		const float Y = Bottom - Floor * FH;
		P.Segment(FVector2D(X0, Y), FVector2D(X1, Y), WithAlpha(Line, Line.A * (Floor == 0 ? 1.f : 0.16f)), 1.f);
		if (Floor < Count && (FH >= 11.f || Floor % 2 == 0))
		{
			const FSlateFontInfo LabelFont = Font(EOneBrahamHUDType::TelemetryLight, FMath::Clamp(FH * 0.5f, 9.f, 11.f));
			P.Text(FVector2D(LabelW - 8.f, Y - FH * 0.5f - Measure(TEXT("0"), LabelFont).Y * 0.5f),
				FString::Printf(TEXT("L%02d"), Floor), LabelFont, WithAlpha(LightGrey(), 0.45f), EOneBrahamTextAnchor::Right);
		}
	}

	const float RevealX = XAt(Params.Reveal);
	P.PushClip(FVector2D(X0, 0.f), FVector2D(FMath::Max(0.f, RevealX - X0), Box.Y));

	// The lockdown rising: a stepped fill under the seal ceiling.
	for (int32 Index = 0; Index < Params.SealCeilingOverTime.Num(); ++Index)
	{
		const FVector2D& Sample = Params.SealCeilingOverTime[Index];
		const float NextT = Params.SealCeilingOverTime.IsValidIndex(Index + 1) ? Params.SealCeilingOverTime[Index + 1].X : 1.f;
		if (Sample.Y < 0.f)
		{
			continue;
		}
		const bool bPurge = Params.PurgeAtTime01 >= 0.f && Sample.X >= Params.PurgeAtTime01;
		const float CeilingY = Bottom - (Sample.Y + 1.f) * FH;
		const FVector2D Pos(XAt(Sample.X), CeilingY);
		const FVector2D Size(XAt(NextT) - XAt(Sample.X), Bottom - CeilingY);
		// Held back so the routes read over it: at full strength the purged
		// band was a solid red slab that swallowed the climb it is the context for.
		P.Rect(Pos, Size, WithAlpha(EmergencyRed(), bPurge ? 0.3f : 0.18f));
		P.Hatch(Pos, Size, WithAlpha(EmergencyRed(), bPurge ? 0.6f : 0.4f), bPurge ? 5.f : 7.f, 1.f);
	}

	// Routes: yours in blue, the others grey, each with its letter at the end.
	for (const FOneBrahamSectionRoute& Route : Params.Routes)
	{
		TArray<FVector2D> Points;
		for (const FVector2D& Sample : Route.Samples)
		{
			Points.Add(FVector2D(XAt(Sample.X), YAt(Sample.Y)));
		}
		const FLinearColor Colour = Route.bYours ? SignalBlue() : WithAlpha(LightGrey(), 0.5f);
		if (Points.Num() == 1)
		{
			Points.Add(Points[0] + FVector2D(1.f, 0.f));
		}
		P.Polyline(Points, Colour, Route.bYours ? 2.5f : 2.f);

		for (const FVector2D& Kill : Route.Kills)
		{
			const FVector2D At(XAt(Kill.X), YAt(Kill.Y));
			P.Rect(At - FVector2D(4.f, 4.f), FVector2D(8.f, 8.f), WithAlpha(Navy(), 0.9f));
			P.Outline(At - FVector2D(4.f, 4.f), FVector2D(8.f, 8.f), Colour, 1.f);
			P.Cross(At, 5.f, Colour, 1.f);
		}
	}
	P.PopClip();

	// Letters at the head of each route, outside the clip so they ride the cursor.
	for (const FOneBrahamSectionRoute& Route : Params.Routes)
	{
		FVector2D Head = FVector2D::ZeroVector;
		bool bFound = false;
		for (const FVector2D& Sample : Route.Samples)
		{
			if (Sample.X <= Params.Reveal + KINDA_SMALL_NUMBER)
			{
				Head = FVector2D(XAt(Sample.X), YAt(Sample.Y));
				bFound = true;
			}
		}
		if (bFound && !Route.Label.IsEmpty())
		{
			const FLinearColor Colour = Route.bYours ? SignalBlue() : WithAlpha(LightGrey(), 0.7f);
			const FSlateFontInfo LetterFont = Font(EOneBrahamHUDType::Label, 11.f);
			P.Text(Head + FVector2D(6.f, -Measure(Route.Label, LetterFont).Y - 2.f), Route.Label, LetterFont, Colour);
		}
	}

	// The cursor: now, on the replay.
	if (Params.Reveal < 0.999f)
	{
		P.Segment(FVector2D(RevealX, Top), FVector2D(RevealX, Bottom), WithAlpha(White(), 0.45f), 1.f);
	}
}

// ==========================================================================
//  Entry
// ==========================================================================
int32 FOneBrahamSectionPainter::Paint(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry,
	const FOneBrahamSectionParams& Params)
{
	int32 Layer = LayerId;
	FOneBrahamPainter P(OutDrawElements, Geometry, Layer, FMath::Clamp(Params.Opacity, 0.f, 1.f));
	const FVector2D Box = Geometry.GetLocalSize();

	switch (Params.Mode)
	{
	case EOneBrahamSectionMode::Spine:	PaintSpine(P, Params); break;
	case EOneBrahamSectionMode::Mini:	PaintMini(P, Params, Box); break;
	case EOneBrahamSectionMode::Chart:	PaintChart(P, Params, Box); break;
	case EOneBrahamSectionMode::Full:
	default:							PaintFull(P, Params, Box); break;
	}

	return Layer;
}
