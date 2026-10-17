// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDElements.cpp
// ---------------------------------------------------------------------------
// The painted elements of UOneBrahamHUDWidget, one function each, in the
// order they are drawn. Every size is in px at 1080p and scales with the HUD
// Scale setting about the corner the element is anchored to; every motion is
// a tween against HUDNow(). See OneBrahamHUDWidget.h for the idea, and the
// design spec ("THE BUILDING SPEAKS") for the numbers.

#include "UI/OneBrahamHUDWidget.h"

#include "OneBraham.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/HUD/OneBrahamSectionPainter.h"
#include "UI/HUD/OneBrahamTween.h"

using namespace OneBrahamHUDStyle;

namespace OneBrahamHUDDraw
{
	float Since(double Now, double At)
	{
		return static_cast<float>(Now - At);
	}

	/** Eased progress of a window that opened at At and lasts Duration. */
	float Ramp(double Now, double At, float Duration, EOneBrahamEase Curve)
	{
		return OneBrahamEase::Evaluate(Curve, Duration > 0.f ? Since(Now, At) / Duration : 1.f);
	}

	FString TwoDigit(int32 Value)
	{
		return FString::Printf(TEXT("%02d"), FMath::Max(0, Value));
	}

	FString Clock(float Seconds, bool bLeadingZero = true)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return bLeadingZero ? FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60)
			: FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	FString Initials(const FString& Name)
	{
		// "BOT Brennan" -> "BR", "Priya Shah" -> "PS", "jenson" -> "JE".
		TArray<FString> Parts;
		Name.Replace(TEXT("BOT "), TEXT("")).ParseIntoArrayWS(Parts);
		if (Parts.Num() >= 2)
		{
			return (Parts[0].Left(1) + Parts.Last().Left(1)).ToUpper();
		}
		return Parts.Num() == 1 ? Parts[0].Left(2).ToUpper() : FString(TEXT("?"));
	}

	FString FireModeWord(EOneBrahamFireMode Mode)
	{
		switch (Mode)
		{
		case EOneBrahamFireMode::Single:	return TEXT("SEMI");
		case EOneBrahamFireMode::Burst:		return TEXT("BURST");
		case EOneBrahamFireMode::Auto:		return TEXT("AUTO");
		case EOneBrahamFireMode::Pump:		return TEXT("PUMP");
		case EOneBrahamFireMode::Bolt:		return TEXT("BOLT");
		default:							return FString();
		}
	}

	/** A plate behind grouped HUD text: navy at 72%, 4 px corners. */
	void Plate(FOneBrahamPainter& P, const FVector2D& Pos, const FVector2D& Size, float Alpha = 0.72f, float Radius = 4.f)
	{
		P.RoundRect(Pos, Size, WithAlpha(Navy(), Alpha), Radius);
	}

	/** A key cap: an outlined box with the key's name in mono. Returns its width. */
	float KeyCap(FOneBrahamPainter& P, const FVector2D& Pos, const FString& Key, const FLinearColor& Colour, float Height = 18.f)
	{
		const FSlateFontInfo Font = OneBrahamHUDStyle::Font(EOneBrahamHUDType::Telemetry, 12.f);
		const FVector2D Size = Measure(Key, Font);
		const float W = FMath::Max(Height, Size.X + 10.f);
		P.RoundRect(Pos, FVector2D(W, Height), WithAlpha(Navy(), 0.6f), 2.f);
		P.Outline(Pos, FVector2D(W, Height), WithAlpha(Colour, 0.7f), 1.f, 2.f);
		P.Text(FVector2D(Pos.X + W * 0.5f, Pos.Y + (Height - Size.Y) * 0.5f), Key, Font, Colour, EOneBrahamTextAnchor::Centre);
		return W;
	}

	/**
	 * Splits a prompt sentence into its verb, key and action. The game's prompts
	 * are written "Hold E - Executive Suite" or "Press E to badge in"; the key
	 * becomes a cap, the rest stays words.
	 */
	void SplitPrompt(const FString& Prompt, FString& OutVerb, FString& OutKey, FString& OutAction)
	{
		OutVerb.Reset();
		OutKey.Reset();
		OutAction = Prompt;

		TArray<FString> Words;
		Prompt.ParseIntoArrayWS(Words);
		if (Words.Num() >= 2 && (Words[0].Equals(TEXT("Hold"), ESearchCase::IgnoreCase) || Words[0].Equals(TEXT("Press"), ESearchCase::IgnoreCase)))
		{
			OutVerb = Words[0];
			OutKey = Words[1].ToUpper();
			FString Rest = Prompt.Mid(Prompt.Find(Words[1]) + Words[1].Len()).TrimStartAndEnd();
			if (Rest.StartsWith(TEXT("-")) || Rest.StartsWith(TEXT("·")))
			{
				Rest = Rest.Mid(1).TrimStart();
			}
			OutAction = Rest;
		}
	}

	/** Wraps a sentence into at most MaxLines lines of Width. */
	TArray<FString> Wrap(const FString& Text, const FSlateFontInfo& Font, float Width, int32 MaxLines)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		TArray<FString> Lines;
		FString Current;
		for (const FString& Word : Words)
		{
			const FString Trial = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
			if (!Current.IsEmpty() && Measure(Trial, Font).X > Width)
			{
				Lines.Add(Current);
				Current = Word;
			}
			else
			{
				Current = Trial;
			}
		}
		if (!Current.IsEmpty())
		{
			Lines.Add(Current);
		}
		if (Lines.Num() > MaxLines)
		{
			// Keep the end of a long line: subtitles are read as they finish.
			Lines.RemoveAt(0, Lines.Num() - MaxLines);
		}
		return Lines;
	}

	/** A string cut to a width with an ellipsis, so a long name ends rather than vanishes. */
	FString Fit(const FString& Text, const FSlateFontInfo& Font, float Width)
	{
		if (Measure(Text, Font).X <= Width)
		{
			return Text;
		}
		FString Cut = Text;
		while (Cut.Len() > 1 && Measure(Cut + TEXT("…"), Font).X > Width)
		{
			Cut.LeftChopInline(1);
		}
		return Cut.TrimEnd() + TEXT("…");
	}

	/** The running-man pictogram of an exit sign, 16 px tall, white. */
	void RunningMan(FOneBrahamPainter& P, const FVector2D& At, const FLinearColor& Colour, float Size = 16.f)
	{
		const float S = Size / 16.f;
		auto Pt = [&](float X, float Y) { return At + FVector2D(X, Y) * S; };
		P.Circle(Pt(9.f, 2.5f), 2.f * S, Colour);
		P.Polyline({ Pt(8.f, 5.f), Pt(6.f, 10.f) }, Colour, 2.2f * S);					// torso
		P.Polyline({ Pt(8.f, 6.f), Pt(11.f, 8.f), Pt(13.f, 6.f) }, Colour, 1.6f * S);	// front arm
		P.Polyline({ Pt(7.5f, 6.f), Pt(4.5f, 7.f), Pt(3.f, 9.5f) }, Colour, 1.6f * S);	// back arm
		P.Polyline({ Pt(6.f, 10.f), Pt(9.f, 12.f), Pt(9.f, 16.f) }, Colour, 1.8f * S);	// front leg
		P.Polyline({ Pt(6.f, 10.f), Pt(4.f, 13.f), Pt(1.f, 14.f) }, Colour, 1.8f * S);	// back leg
	}
}

using namespace OneBrahamHUDDraw;

// ==========================================================================
//  The order of things
// ==========================================================================
void UOneBrahamHUDWidget::PaintHUD(FOneBrahamPainter& P, const FPaintContext& InContext) const
{
	FPaintContext C = InContext;

	// A stage card takes the secondary panels down to a quarter while it
	// speaks; the things a player fights with are never touched.
	if (StageCard != EOneBrahamStageCard::None)
	{
		const float T = Since(C.Now, StageCardAt);
		const float End = 0.35f + StageCardHold + 0.3f;
		if (T >= 0.f && T < End + 0.3f)
		{
			const float In = FMath::Clamp(T / 0.35f, 0.f, 1.f);
			const float Out = FMath::Clamp((T - (End - 0.3f)) / 0.3f, 0.f, 1.f);
			C.SecondaryFade = FMath::Lerp(1.f, 0.25f, In * (1.f - Out));
		}
	}

	// --- dead: the HUD drops away; the accident book is the only thing up ---------------
	if (bDeathScreenVisible || !C.bAlive)
	{
		if (!bDeathScreenVisible && !Frame.SpectatingName.IsEmpty())
		{
			PaintSpectating(P, C);
		}
		if (bDeathScreenVisible)
		{
			PaintAccidentBook(P, C);
		}
		PaintSubtitles(P, C);
		// Over the book the PA would cover the form; the subtitle still carries
		// what the building said. A spectator sees the card as usual.
		if (!bDeathScreenVisible)
		{
			PaintAnnouncement(P, C, C.Screen.Y * 0.13f);
		}
		return;
	}

	PaintStageVignette(P, C);
	PaintDamageArcs(P, C);

	// The stage card's letterbox is painted in NativePaint, under the child
	// widgets and so under everything here: the HUD stays readable on top of
	// the bars, so the moment never costs the player their health or bearing.

	// The secondary panels: the ones that rest-dim and give way to a stage card.
	// The floor plan, when it is open, is the bigger drawing of both.
	if (!bPlanOpen)
	{
		FOneBrahamPainter Secondary = P.Faded(C.SecondaryFade);
		PaintFloorStack(Secondary, C);
		PaintSquadBoard(Secondary, C);
	}

	PaintFloorPlate(P, C);

	float ObjectiveBottom = C.Screen.Y * 0.11f;
	PaintObjective(P, C, ObjectiveBottom);
	PaintBanner(P, C);

	float ModuleBottom = C.SafeT;
	PaintLockdownModule(P, C, ModuleBottom);
	{
		FOneBrahamPainter Secondary = P.Faded(C.SecondaryFade);
		PaintKillFeed(Secondary, C, ModuleBottom + 8.f * C.Scale);
	}

	if (!bBoardOpen)
	{
		PaintAnnouncement(P, C, FMath::Max(C.Screen.Y * 0.13f, ObjectiveBottom + 14.f));
	}

	PaintHealthCard(P, C);
	PaintAmmo(P, C);
	PaintMicPill(P, C);

	// Down, the only thing to do is wait or be helped: the reader and the
	// induction's next step would be instructions the player cannot follow.
	if (Data.LifeState != EOneBrahamLifeState::Downed && !bBoardOpen)
	{
		PaintPrompt(P, C);
	}
	PaintSubtitles(P, C);
	PaintCrosshair(P, C);
	PaintDowned(P, C);
	PaintStageCard(P, C);
}

// ==========================================================================
//  1. The floor plate: the stair core's level sign
// ==========================================================================
void UOneBrahamHUDWidget::PaintFloorPlate(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	if (!Data.bShowFloorIndicator || ShownFloor == INDEX_NONE)
	{
		return;
	}

	const FOneBrahamBuildingStatus& Building = Frame.Building;
	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeL, C.SafeT), C.Scale);
	const float PlateSize = 72.f;

	const bool bDanger = Building.IsLocalInDanger();
	const bool bSealing = !bDanger && Building.IsLocalSealingWithin(30.f);
	const float Blink = OneBrahamPulse::Blink(C.Now, OneBrahamPulse::ImminentHz, C.bReduceMotion);

	// --- the hazard border: this floor is about to close ----------------------------
	if (bSealing)
	{
		const FVector2D Pos(-3.f, -3.f);
		const FVector2D Size(PlateSize + 6.f, PlateSize + 6.f);
		FOneBrahamPainter Border = P.Faded(0.35f + 0.65f * Blink);
		Border.PushClip(Pos, Size);
		Border.HazardStripes(Pos, Size, HazardYellow(), Navy(), 8.f);
		Border.PopClip();
	}

	// --- the plate ----------------------------------------------------------------------
	const FLinearColor Face = bDanger ? EmergencyRed() : WithAlpha(SignageWhite(), 0.92f);
	const FLinearColor Ink = bDanger ? White() : Navy();
	P.RoundRect(FVector2D::ZeroVector, FVector2D(PlateSize, PlateSize), Face, 2.f);
	// A navy hairline holds the white plate's edge against a white wall.
	P.Outline(FVector2D::ZeroVector, FVector2D(PlateSize, PlateSize), WithAlpha(Navy(), 0.35f), 1.f, 2.f);

	// The split-flap: the old number folds away in 90 ms, the new one unfolds in
	// the next 90. With Reduce Motion the number simply changes.
	const float Flip = C.bReduceMotion ? 1.f : FMath::Clamp(Since(C.Now, FloorChangedAt) / 0.18f, 0.f, 1.f);
	const bool bShowOld = Flip < 0.5f && PreviousFloor != INDEX_NONE;
	const int32 Number = bShowOld ? PreviousFloor : ShownFloor;
	const float Squash = bShowOld ? 1.f - Flip * 2.f : (Flip - 0.5f) * 2.f;
	const float YScale = Flip >= 1.f ? 1.f : FMath::Clamp(Squash, 0.02f, 1.f);

	const int32 TopFloor = Building.FloorCount() > 0 ? Building.Floors.Last().FloorIndex : 17;
	const FString Numerals = Number > TopFloor ? FString(TEXT("RF")) : TwoDigit(Number);
	// Archivo Expanded Black is wide: "08" at 50 px ran edge to edge of the
	// plate. A stair-core sign keeps a margin round its numerals, so the size
	// steps down until the pair sits inside 9 px either side.
	FSlateFontInfo NumberFont = Font(EOneBrahamHUDType::Title, 46.f, -30);
	FVector2D NumberSize = Measure(Numerals, NumberFont);
	for (int32 Step = 0; Step < 6 && NumberSize.X > PlateSize - 18.f; ++Step)
	{
		NumberFont = Font(EOneBrahamHUDType::Title, 44.f - Step * 3.f, -30);
		NumberSize = Measure(Numerals, NumberFont);
	}
	{
		const FGeometry Squashed = P.Geometry.MakeChild(FVector2D(PlateSize, PlateSize),
			FSlateLayoutTransform(), FSlateRenderTransform(FScale2D(1.f, YScale)), FVector2D(0.5f, 0.5f));
		FOneBrahamPainter Flap = P;
		Flap.Geometry = Squashed;
		Flap.Text(FVector2D(PlateSize * 0.5f, (PlateSize - NumberSize.Y) * 0.5f + 1.f), Numerals, NumberFont, Ink, EOneBrahamTextAnchor::Centre);
	}

	// The flap's hinge: a hairline across the middle, as on a real split-flap.
	P.Rect(FVector2D(4.f, PlateSize * 0.5f), FVector2D(PlateSize - 8.f, 1.f), WithAlpha(Ink, 0.12f));

	// --- the lockdown beacon: a slow yellow lamp beside the sign -----------------------
	if (C.Band == 1)
	{
		const float Beacon = OneBrahamPulse::Breath(C.Now, OneBrahamPulse::BeaconHz, 0.25f, 1.f);
		P.Circle(FVector2D(PlateSize + 8.f, 6.f), 3.f, WithAlpha(SafetyYellow(), Beacon));
	}

	// --- the words beside it --------------------------------------------------------------
	const float TextX = PlateSize + 12.f + (C.Band == 1 ? 10.f : 0.f);
	if (bDanger)
	{
		const FSlateFontInfo Big = HUDFont(EOneBrahamHUDType::Label, 18.f, 60, C.bHeavyOutline);
		const FVector2D Size = P.Text(FVector2D(TextX, 6.f), TEXT("EVACUATE ↑"), Big, White());
		RunningMan(P, FVector2D(TextX + Size.X + 8.f, 8.f), White(), 16.f);
		const FString Why = Building.Stage == EOneBrahamLockdownStage::Purge
			? FString::Printf(TEXT("PURGE FRONT L%02d ↑"), Building.PurgeCeiling)
			: TEXT("THIS FLOOR IS SEALED");
		// White on a red tag: red type straight onto a lit wall was the one line
		// on the HUD that vanished against concrete.
		const FSlateFontInfo WhyFont = Font(EOneBrahamHUDType::Telemetry, 13.f, 40);
		const FVector2D WhySize = Measure(Why, WhyFont);
		P.RoundRect(FVector2D(TextX, 34.f), FVector2D(WhySize.X + 12.f, WhySize.Y + 4.f), EmergencyRed(), 2.f);
		P.Outline(FVector2D(TextX, 34.f), FVector2D(WhySize.X + 12.f, WhySize.Y + 4.f), WithAlpha(OLEDBlack(), 0.35f), 1.f, 2.f);
		P.Text(FVector2D(TextX + 6.f, 36.f), Why, WhyFont, White());
		return;
	}

	// The area name retypes, 18 ms a character, when the floor changes.
	const int32 Typed = C.bReduceMotion ? PlateArea.Len()
		: FMath::Clamp(FMath::FloorToInt(Since(C.Now, PlateAreaAt) / 0.018f), 0, PlateArea.Len());
	P.Text(FVector2D(TextX, 8.f), PlateArea.Left(Typed), HUDFont(EOneBrahamHUDType::Label, 18.f, 60, C.bHeavyOutline), White());

	if (bSealing)
	{
		// Yellow on navy, as the fire panel's own lettering: yellow on a white
		// wall is a colour the eye cannot find.
		const FString Sealing = FString::Printf(TEXT("SEALING IN %s"), *Clock(Building.SecondsToNextStage, false));
		const FSlateFontInfo SealFont = Font(EOneBrahamHUDType::Telemetry, 14.f, 20);
		const FVector2D SealSize = Measure(Sealing, SealFont);
		Plate(P, FVector2D(TextX - 6.f, 32.f), FVector2D(SealSize.X + 12.f, SealSize.Y + 4.f), 0.86f, 2.f);
		P.Text(FVector2D(TextX, 34.f), Sealing, SealFont, SafetyYellow());
	}
	else
	{
		// Which side of the tower: the sign by each stair core says it.
		FString Wing;
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				const FVector L = Pawn->GetActorLocation();
				// The tower's frame (district_data.py): north is at yaw -59.87.
				const float Bearing = FRotator::ClampAxis(FMath::RadiansToDegrees(FMath::Atan2(L.Y, L.X)) + 59.87f);
				static const TCHAR* Sides[4] = { TEXT("NORTH"), TEXT("EAST"), TEXT("SOUTH"), TEXT("WEST") };
				Wing = Sides[FMath::RoundToInt(Bearing / 90.f) % 4];
			}
		}
		P.Text(FVector2D(TextX, 34.f), FString::Printf(TEXT("LEVEL %s  ·  %s"), *TwoDigit(ShownFloor), *Wing),
			HUDFont(EOneBrahamHUDType::TelemetryLight, 12.f, 60, C.bHeavyOutline), WithAlpha(LightGrey(), 0.7f));
	}
}

// ==========================================================================
//  2. The floor stack: the lift indicator
// ==========================================================================
void UOneBrahamHUDWidget::PaintFloorStack(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	if (!Data.bShowFloorIndicator || !bStackRevealed || Frame.Building.FloorCount() == 0)
	{
		return;
	}

	const int32 Count = Frame.Building.FloorCount();
	const FVector2D Natural = FOneBrahamSectionPainter::SpineSize(Count);
	const float RegionTop = C.Screen.Y * 0.14f;
	const float RegionH = C.Screen.Y * 0.30f;
	const float Top = RegionTop + FMath::Max(0.f, (RegionH - Natural.Y * C.Scale) * 0.5f);

	// At rest it sits back at 55%; for three seconds after anything on it
	// changes it is at full strength, because that is when it is news.
	const float SinceChange = Since(C.Now, StackChangedAt);
	float Alpha = 1.f;
	if (C.bRestDimming && SinceChange > 3.f)
	{
		Alpha = FMath::Lerp(1.f, 0.55f, FMath::Clamp((SinceChange - 3.f) / 0.6f, 0.f, 1.f));
	}

	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeL + 26.f * C.Scale, Top), C.Scale);

	FOneBrahamSectionParams Params;
	Params.Mode = EOneBrahamSectionMode::Spine;
	Params.FillFromStatus(Frame.Building);
	Params.Time = C.Now;
	Params.bReduceMotion = C.bReduceMotion;
	Params.LineColour = WithAlpha(LightGrey(), 0.4f);
	Params.Opacity = Alpha * P.Opacity;
	// The stack strokes in bottom-up when it first appears: 40 ms a floor.
	Params.Reveal = C.bReduceMotion ? 1.f : FMath::Clamp(Since(C.Now, StackChangedAt) / (Count * 0.04f), 0.f, 1.f);
	if (SinceChange > Count * 0.04f)
	{
		Params.Reveal = 1.f;
	}

	*P.Layer = FOneBrahamSectionPainter::Paint(*P.Out, *P.Layer + 1, P.Geometry, Params);
}

// ==========================================================================
//  4. The objective line, and the building's OVERRIDE of it
// ==========================================================================
void UOneBrahamHUDWidget::PaintObjective(FOneBrahamPainter& P, const FPaintContext& C, float& OutBottom) const
{
	const float CX = C.Screen.X * 0.5f;
	float Y = C.Screen.Y * 0.082f;
	OutBottom = Y;

	if (Frame.Mode == EOneBrahamMatchMode::FireDrill)
	{
		PaintFireDrillPoints(P, C, Y, OutBottom);
		return;
	}

	if (ObjectiveShown.IsEmpty())
	{
		return;
	}

	FOneBrahamPainter Q = P.Child(FVector2D(CX, Y), C.Scale);

	const FSlateFontInfo Eyebrow = HUDFont(EOneBrahamHUDType::Eyebrow, 11.f, 200, C.bHeavyOutline);
	Q.Text(FVector2D(0.f, 0.f), TEXT("OBJECTIVE"), Eyebrow, WithAlpha(LightGrey(), 0.6f), EOneBrahamTextAnchor::Centre);

	const FSlateFontInfo LineFont = HUDFont(EOneBrahamHUDType::Label, 20.f, 0, C.bHeavyOutline);
	const FSlateFontInfo DetailFont = HUDFont(EOneBrahamHUDType::Telemetry, 16.f, 0, C.bHeavyOutline);
	const float LineY = 15.f;

	// --- the override: struck line, then the building's instruction --------------------
	const float OverrideT = Since(C.Now, OverrideAt);
	const bool bOverriding = OverrideT >= 0.f && OverrideT < 6.4f && !OverriddenLine.IsEmpty();

	auto MeasureLine = [&](const FString& Line, const FString& Detail)
	{
		const float W = Measure(Line, LineFont).X;
		const float D = Detail.IsEmpty() ? 0.f : Measure(TEXT("  ") + Detail, DetailFont).X;
		return FVector2D(W, D);
	};

	// On the override's plate the type is set without its outline: a black
	// halo round navy letters on yellow is what made the line look smudged.
	const FSlateFontInfo PlateLineFont = Font(EOneBrahamHUDType::Label, 20.f, 0);
	const FSlateFontInfo PlateDetailFont = Font(EOneBrahamHUDType::Telemetry, 16.f, 0);

	auto DrawLine = [&](float AtY, const FString& Line, const FString& Detail, const FLinearColor& Ink, const FLinearColor& DetailInk, float Reveal,
		bool bOnPlate = false)
	{
		const FSlateFontInfo& LFont = bOnPlate ? PlateLineFont : LineFont;
		const FSlateFontInfo& DFont = bOnPlate ? PlateDetailFont : DetailFont;
		const FVector2D Widths = MeasureLine(Line, Detail);
		const float Left = -(Widths.X + Widths.Y) * 0.5f;
		Q.TextWipe(FVector2D(Left, AtY), Line, LFont, Ink, Reveal);
		if (!Detail.IsEmpty())
		{
			const float DetailH = Measure(Detail, DFont).Y;
			const float LineH = Measure(Line, LFont).Y;
			Q.TextWipe(FVector2D(Left + Widths.X, AtY + (LineH - DetailH) * 0.75f), TEXT("  ") + Detail, DFont, DetailInk, Reveal);
		}
		return FVector2D(Left, Widths.X + Widths.Y);
	};

	if (bOverriding)
	{
		const FLinearColor Stamp = OverrideBand == 2 ? EmergencyRed() : SafetyYellow();
		const FVector2D Old = MeasureLine(OverriddenLine, OverriddenDetail);
		const float OldW = Old.X + Old.Y;
		const float OldLeft = -OldW * 0.5f;
		const float LineH = Measure(OverriddenLine, LineFont).Y;

		// The struck line collapses after six seconds.
		const float Collapse = FMath::Clamp((OverrideT - 6.f) / 0.2f, 0.f, 1.f);
		const float OldAlpha = (OverrideT < 0.26f ? 1.f : 0.4f) * (1.f - Collapse);
		DrawLine(LineY, OverriddenLine, OverriddenDetail, WithAlpha(White(), OldAlpha), WithAlpha(SafetyYellow(), OldAlpha), 1.f);

		// 1. A bar wipes across it, hard stop, 260 ms.
		// 2. The bar retracts to a 2 px strike through the middle.
		if (OverrideT < 0.26f)
		{
			const float Wipe = FMath::Clamp(OverrideT / 0.26f, 0.f, 1.f);
			Q.Rect(FVector2D(OldLeft - 6.f, LineY - 2.f), FVector2D((OldW + 12.f) * Wipe, LineH + 4.f), Stamp);
		}
		else
		{
			const float Retract = Ramp(C.Now, OverrideAt + 0.26, 0.14f, EOneBrahamEase::Ribbon);
			const float BarH = FMath::Lerp(LineH + 4.f, 2.f, Retract) * (1.f - Collapse);
			Q.Rect(FVector2D(OldLeft - 6.f, LineY + LineH * 0.55f - BarH * 0.5f), FVector2D(OldW + 12.f, BarH), WithAlpha(Stamp, 1.f - Collapse));
		}

		// 3. The new line, directly under it, on a plate in the stage colour;
		// 4. once the struck line has gone, it rises into place, plate removed.
		if (OverrideT >= 0.26f)
		{
			const float Rise = FMath::Lerp(LineH + 12.f, 0.f, OneBrahamEase::Evaluate(EOneBrahamEase::Lift, Collapse));
			const float NewY = LineY + Rise;
			const float Reveal = Ramp(C.Now, OverrideAt + 0.26, 0.26f, EOneBrahamEase::Ribbon);
			const FVector2D New = MeasureLine(ObjectiveShown, ObjectiveDetailShown);
			const float NewW = New.X + New.Y;
			const float PlateFade = 1.f - Collapse;
			Q.RoundRect(FVector2D(-NewW * 0.5f - 10.f, NewY - 3.f), FVector2D((NewW + 20.f) * Reveal, LineH + 6.f), WithAlpha(Stamp, PlateFade), 2.f);
			const FLinearColor Ink = FMath::Lerp(OverrideBand == 2 ? White() : Navy(), White(), Collapse);
			const FLinearColor DetailInk = FMath::Lerp(OverrideBand == 2 ? White() : Navy(), SafetyYellow(), Collapse);
			DrawLine(NewY, ObjectiveShown, ObjectiveDetailShown, Ink, DetailInk, Reveal, PlateFade > 0.5f);
			OutBottom = Y + (NewY + LineH + 8.f) * C.Scale;
		}
		else
		{
			OutBottom = Y + (LineY + LineH + 8.f) * C.Scale;
		}
	}
	else
	{
		// --- the standard line: an 80 ms wipe and a ribbon underline on change -----------
		const float Reveal = C.bReduceMotion ? 1.f : FMath::Clamp(Since(C.Now, ObjectiveChangedAt) / 0.08f, 0.f, 1.f);
		const FVector2D Span = DrawLine(LineY, ObjectiveShown, ObjectiveDetailShown, White(), SafetyYellow(), Reveal);
		const float LineH = Measure(ObjectiveShown, LineFont).Y;

		const float Ribbon = Ramp(C.Now, ObjectiveChangedAt, OneBrahamEase::RibbonDraw, EOneBrahamEase::Ribbon);
		const float RibbonFade = 1.f - FMath::Clamp((Since(C.Now, ObjectiveChangedAt) - 3.f) / 0.8f, 0.f, 1.f);
		if (RibbonFade > 0.f)
		{
			Q.Gradient(FVector2D(Span.X, LineY + LineH + 3.f), FVector2D(Span.Y * Ribbon, 2.f),
				WithAlpha(RibbonPurple(), RibbonFade), WithAlpha(RibbonBlue(), RibbonFade));
		}

		OutBottom = Y + (LineY + LineH + 8.f) * C.Scale;
	}

	if (Frame.ExtractionState == EOneBrahamExtractionState::Arriving || Frame.ExtractionState == EOneBrahamExtractionState::Holding
		|| Frame.ExtractionState == EOneBrahamExtractionState::Broadcast)
	{
		PaintExtraction(P, C, OutBottom + 6.f * C.Scale, OutBottom);
	}
}

// ==========================================================================
//  4a. FIRE DRILL: the points and the score
// ==========================================================================
void UOneBrahamHUDWidget::PaintFireDrillPoints(FOneBrahamPainter& P, const FPaintContext& C, float Top, float& OutBottom) const
{
	const float CX = C.Screen.X * 0.5f;
	FOneBrahamPainter Q = P.Child(FVector2D(CX, Top), C.Scale);

	const FSlateFontInfo ScoreFont = HUDFont(EOneBrahamHUDType::TelemetryStrong, 18.f, 0, C.bHeavyOutline);
	const FSlateFontInfo LetterFont = HUDFont(EOneBrahamHUDType::Verb, 26.f, 0, C.bHeavyOutline);
	const FSlateFontInfo Eyebrow = HUDFont(EOneBrahamHUDType::Eyebrow, 11.f, 200, C.bHeavyOutline);

	Q.Text(FVector2D(0.f, 0.f), TEXT("ASSEMBLY POINTS"), Eyebrow, WithAlpha(LightGrey(), 0.6f), EOneBrahamTextAnchor::Centre);

	const int32 Count = FMath::Max(1, Frame.Points.Num());
	const float Span = C.Screen.X * 0.18f / C.Scale;
	const float Step = Count > 1 ? Span / (Count - 1) : 0.f;
	const float Y = 16.f;

	// The score either side: yours in blue on the left, theirs on the right,
	// "212 - 187" read as a ledger.
	Q.Text(FVector2D(-Span * 0.5f - 34.f, Y + 6.f), FString::FromInt(Frame.ScoreOurs), ScoreFont, SignalBlue(), EOneBrahamTextAnchor::Right);
	Q.Text(FVector2D(Span * 0.5f + 34.f, Y + 6.f), FString::FromInt(Frame.ScoreTheirs), ScoreFont, White(), EOneBrahamTextAnchor::Left);

	for (int32 Index = 0; Index < Frame.Points.Num(); ++Index)
	{
		const FOneBrahamPointView& Point = Frame.Points[Index];
		const float X = -Span * 0.5f + Index * Step;
		const bool bHere = Point.Floor == Frame.Building.LocalFloor;

		// Ownership is the server's and does not reach this machine yet: the
		// points are drawn neutral (outline only), and lit in your colour while
		// you stand on one, because that much this machine does know.
		const FLinearColor Ink = Point.bYoursPresent ? SignalBlue() : WithAlpha(LightGrey(), 0.9f);
		const FVector2D LetterSize = Measure(Point.Letter, LetterFont);
		Q.Text(FVector2D(X, Y), Point.Letter, LetterFont, Ink, EOneBrahamTextAnchor::Centre);

		const FVector2D BarPos(X - 18.f, Y + LetterSize.Y + 1.f);
		if (Point.bYoursPresent)
		{
			Q.Rect(BarPos, FVector2D(36.f, 3.f), SignalBlue());
		}
		else
		{
			Q.Outline(BarPos - FVector2D(0.f, 1.f), FVector2D(36.f, 4.f), WithAlpha(LightGrey(), 0.4f), 1.f);
		}

		Q.Text(FVector2D(X, BarPos.Y + 7.f), FString::Printf(TEXT("L%02d%s"), Point.Floor, bHere ? TEXT(" · HERE") : TEXT("")),
			HUDFont(EOneBrahamHUDType::Telemetry, 10.f, 40, C.bHeavyOutline), WithAlpha(LightGrey(), bHere ? 0.9f : 0.55f), EOneBrahamTextAnchor::Centre);
	}

	OutBottom = Top + 76.f * C.Scale;
}

// ==========================================================================
//  4b. The extraction countdown
// ==========================================================================
void UOneBrahamHUDWidget::PaintExtraction(FOneBrahamPainter& P, const FPaintContext& C, float Top, float& OutBottom) const
{
	const float CX = C.Screen.X * 0.5f;
	FOneBrahamPainter Q = P.Child(FVector2D(CX, Top), C.Scale);

	const bool bHolding = Frame.ExtractionState == EOneBrahamExtractionState::Holding;
	const bool bContested = Frame.bExtractionContested;
	const FString Eyebrow = bHolding ? TEXT("EXTRACTION  ·  HOLD THE ROOF")
		: (Frame.ExtractionState == EOneBrahamExtractionState::Broadcast ? TEXT("EXTRACTION  ·  CALLED") : TEXT("EXTRACTION  ·  ROOF"));

	const FSlateFontInfo LED = HUDFont(EOneBrahamHUDType::LED, 48.f, 0, false);
	const FString Digits = Clock(Frame.ExtractionSeconds, false);
	const FLinearColor DigitInk = bContested ? EmergencyRed() : SafetyYellow();

	// The LED face is dots: it needs a dark panel behind it, as a real dot-matrix
	// sign has, or its digits break up against a lit ceiling.
	const FVector2D DigitMeasure = Measure(Digits, LED);
	const float PanelW = FMath::Max(260.f, DigitMeasure.X + (bContested ? 260.f : 60.f));
	const float PanelH = 12.f + DigitMeasure.Y + 4.f + 6.f + 12.f;
	Plate(Q, FVector2D(-PanelW * 0.5f, -6.f), FVector2D(PanelW, PanelH), 0.86f, 4.f);
	Q.Rect(FVector2D(-PanelW * 0.5f, -6.f), FVector2D(PanelW, 2.f), WithAlpha(DigitInk, 0.9f));

	Q.Text(FVector2D(0.f, 0.f), Eyebrow, HUDFont(EOneBrahamHUDType::Eyebrow, 11.f, 200, false), SafetyYellow(), EOneBrahamTextAnchor::Centre);
	const FVector2D DigitSize = Q.Text(FVector2D(0.f, 12.f), Digits, LED, DigitInk, EOneBrahamTextAnchor::Centre);

	if (bContested)
	{
		// The word is the cue, not the colour; the blink says "now".
		const float Blink = OneBrahamPulse::Blink(C.Now, OneBrahamPulse::ImminentHz, C.bReduceMotion);
		Q.Text(FVector2D(DigitSize.X * 0.5f + 12.f, 12.f + DigitSize.Y * 0.5f - 10.f), TEXT("CONTESTED"),
			HUDFont(EOneBrahamHUDType::Label, 14.f, 80, C.bHeavyOutline), WithAlpha(EmergencyRed(), 0.3f + 0.7f * Blink));
	}

	// Thirty cells, one a second of the hold.
	const float Progress = bHolding ? Frame.ExtractionProgress01 : FMath::Clamp(Frame.ExtractionProgress01, 0.f, 1.f);
	const float BarW = 220.f;
	const float CellW = (BarW - 29.f) / 30.f;
	const float BarY = 12.f + DigitSize.Y + 4.f;
	const int32 Lit = FMath::FloorToInt(Progress * 30.f + 0.001f);
	for (int32 Cell = 0; Cell < 30; ++Cell)
	{
		Q.Rect(FVector2D(-BarW * 0.5f + Cell * (CellW + 1.f), BarY), FVector2D(CellW, 6.f),
			Cell < Lit ? DigitInk : WithAlpha(LightGrey(), 0.18f));
	}

	OutBottom = Top + (PanelH + 4.f) * C.Scale;
}

// ==========================================================================
//  5. The lockdown module: the fire-alarm panel
// ==========================================================================
void UOneBrahamHUDWidget::PaintLockdownModule(FOneBrahamPainter& Root, const FPaintContext& C, float& OutBottom) const
{
	const float Width = 280.f;
	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeR - Width * C.Scale, C.SafeT), C.Scale);
	const FOneBrahamBuildingStatus& Building = Frame.Building;
	const bool bAscent = Frame.Mode == EOneBrahamMatchMode::Ascent;

	// Open in the Lockdown and the Purge: 28 to 96 px over 280 ms, the Lift curve.
	const bool bOpen = bAscent && C.Band > 0;
	const float OpenT = bOpen ? Ramp(C.Now, StageStartedAt, OneBrahamEase::Panel, EOneBrahamEase::Lift) : 0.f;
	const float Height = FMath::Lerp(28.f, 96.f, OpenT);
	OutBottom = C.SafeT + Height * C.Scale;

	const FLinearColor Housing = C.Band == 2 ? EmergencyRed() : (C.Band == 1 ? SafetyYellow() : FireHousing());

	if (OpenT < 0.02f)
	{
		// --- at rest: the panel's status pill ---------------------------------------------
		P.RoundRect(FVector2D::ZeroVector, FVector2D(Width, 28.f), WithAlpha(BaffleBlack(), 0.9f), 14.f);
		P.Outline(FVector2D::ZeroVector, FVector2D(Width, 28.f), FireHousing(), 1.f, 14.f);
		P.Circle(FVector2D(15.f, 14.f), 4.f, LightGrey());
		P.Circle(FVector2D(15.f, 14.f), 7.f, WithAlpha(LightGrey(), 0.12f));

		FString Status = TEXT("SYSTEM NORMAL");
		FString Right;
		if (bAscent)
		{
			Right = Building.bDirectorRunning && Building.SecondsToNextStage > 0.f
				? FString::Printf(TEXT("LOCKDOWN %s"), *Clock(Building.SecondsToNextStage)) : TEXT("LOCKDOWN --:--");
		}
		else if (Frame.Mode == EOneBrahamMatchMode::FireDrill)
		{
			Status = TEXT("FIRE DRILL");
			Right = Frame.TimeRemaining > 0.f ? FString::Printf(TEXT("ENDS %s"), *Clock(Frame.TimeRemaining)) : FString(Frame.DuskClock);
		}
		else
		{
			Status = TEXT("AFTER HOURS");
			Right = Frame.DuskClock;
		}

		const FSlateFontInfo StatusFont = HUDFont(EOneBrahamHUDType::Label, 10.f, 140, C.bHeavyOutline);
		P.Text(FVector2D(28.f, 14.f - Measure(Status, StatusFont).Y * 0.5f), Status, StatusFont, LightGrey());

		const FSlateFontInfo ClockFont = HUDFont(EOneBrahamHUDType::Telemetry, 13.f, 20, C.bHeavyOutline);
		// The warning: in the last fifteen seconds the clock takes the warning colour.
		const bool bWarning = bAscent && Building.bWarningActive;
		P.Text(FVector2D(Width - 14.f, 14.f - Measure(Right, ClockFont).Y * 0.5f), Right, ClockFont,
			bWarning ? SafetyYellow() : WithAlpha(LightGrey(), 0.7f), EOneBrahamTextAnchor::Right);
		return;
	}

	// --- open: the panel ---------------------------------------------------------------
	P.RoundRect(FVector2D::ZeroVector, FVector2D(Width, Height), WithAlpha(BaffleBlack(), 0.92f), 3.f);
	P.Outline(FVector2D::ZeroVector, FVector2D(Width, Height), Housing, 2.f, 3.f);
	P.PushClip(FVector2D(2.f, 2.f), FVector2D(Width - 4.f, Height - 4.f));

	// Row 1: four LEDs. The lit one's position is the stage - readable with no
	// colour vision at all.
	static const TCHAR* LEDs[4] = { TEXT("NORMAL"), TEXT("LOCKDOWN"), TEXT("PURGE"), TEXT("EVAC") };
	const int32 Active = C.Band == 2 ? 2 : (C.Band == 1 ? 1 : 0);
	const float StageAge = Since(C.Now, StageStartedAt);
	const float LEDBlink = StageAge < 6.f ? OneBrahamPulse::Blink(C.Now, OneBrahamPulse::ImminentHz, C.bReduceMotion) : 1.f;
	const FSlateFontInfo LEDFont = HUDFont(EOneBrahamHUDType::Label, 10.f, 100, false);
	float LX = 12.f;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bLit = Index == Active;
		const FLinearColor Lamp = bLit ? WithAlpha(Housing, LEDBlink) : WithAlpha(LightGrey(), 0.14f);
		P.Circle(FVector2D(LX + 4.f, 15.f), 4.f, Lamp);
		if (bLit)
		{
			P.Circle(FVector2D(LX + 4.f, 15.f), 8.f, WithAlpha(Housing, 0.18f * LEDBlink));
		}
		const FVector2D LabelSize = P.Text(FVector2D(LX + 12.f, 15.f - Measure(LEDs[Index], LEDFont).Y * 0.5f), LEDs[Index], LEDFont,
			WithAlpha(LightGrey(), bLit ? 1.f : 0.4f));
		LX += 12.f + LabelSize.X + 12.f;
	}

	// Row 2: THE TIMER. The split-second read: what goes next, and when.
	FString What;
	FString When;
	if (Building.Stage == EOneBrahamLockdownStage::Purge)
	{
		What = FString::Printf(TEXT("PURGE FRONT L%02d ↑"), Building.PurgeCeiling);
		When = TEXT("ACTIVE");
	}
	else if (Building.Stage == EOneBrahamLockdownStage::Lockdown)
	{
		What = FString::Printf(TEXT("PURGE L00-L%02d IN"), Building.PurgeCeiling);
		When = Building.SecondsToNextStage > 0.f ? Clock(Building.SecondsToNextStage) : TEXT("--:--");
	}
	else
	{
		What = Building.NextSealCeiling != INDEX_NONE
			? FString::Printf(TEXT("L%02d SEALS IN"), Building.NextSealCeiling) : TEXT("SHUTTERS IN");
		When = Building.SecondsToNextStage > 0.f ? Clock(Building.SecondsToNextStage) : TEXT("--:--");
	}

	const FSlateFontInfo WhatFont = HUDFont(EOneBrahamHUDType::Label, 13.f, 60, false);
	const FSlateFontInfo TimerFont = HUDFont(EOneBrahamHUDType::TelemetryStrong, 30.f, 0, false);
	const bool bFinal = Building.SecondsToNextStage > 0.f && Building.SecondsToNextStage < 10.f;
	const FVector2D TimerSize = Measure(When, TimerFont);
	P.Text(FVector2D(12.f, 46.f - Measure(What, WhatFont).Y * 0.5f), What, WhatFont, White());
	P.Text(FVector2D(Width - 12.f, 46.f - TimerSize.Y * 0.5f), When, TimerFont, bFinal ? Housing : White(), EOneBrahamTextAnchor::Right);

	// Row 3: the LCD strip. A new line scrolls the last one up in 120 ms.
	const FVector2D LCDPos(8.f, 66.f);
	const FVector2D LCDSize(Width - 16.f, 22.f);
	P.RoundRect(LCDPos, LCDSize, LCDField(), 2.f);
	P.PushClip(LCDPos, LCDSize);
	const FSlateFontInfo LCDFont = Font(EOneBrahamHUDType::TelemetryLight, 12.f, 40);
	const float Scroll = C.bReduceMotion ? 1.f : FMath::Clamp(Since(C.Now, LCDScrolledAt) / 0.12f, 0.f, 1.f);
	const float LineY = LCDPos.Y + (LCDSize.Y - Measure(TEXT("0"), LCDFont).Y) * 0.5f;
	if (LCDLines.Num() > 0)
	{
		P.Text(FVector2D(LCDPos.X + 8.f, LineY + (1.f - Scroll) * LCDSize.Y), LCDLines.Last(), LCDFont, LCDInk());
		if (LCDLines.Num() > 1 && Scroll < 1.f)
		{
			P.Text(FVector2D(LCDPos.X + 8.f, LineY - Scroll * LCDSize.Y), LCDLines[LCDLines.Num() - 2], LCDFont, LCDInk());
		}
	}
	else
	{
		P.Text(FVector2D(LCDPos.X + 8.f, LineY), TEXT("ALL ZONES MONITORED"), LCDFont, LCDInk());
	}
	// The LCD's own clock, right-aligned, because a fire panel always shows one.
	P.Text(FVector2D(LCDPos.X + LCDSize.X - 8.f, LineY), Frame.DuskClock, LCDFont, WithAlpha(LCDInk(), 0.6f), EOneBrahamTextAnchor::Right);
	P.PopClip();

	P.PopClip();
}

// ==========================================================================
//  6. The kill feed: the sign-out log
// ==========================================================================
void UOneBrahamHUDWidget::PaintKillFeed(FOneBrahamPainter& Root, const FPaintContext& C, float Top) const
{
	if (!Data.bShowKillFeed || Frame.Kills.Num() == 0)
	{
		return;
	}

	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeR, Top), C.Scale);
	const FSlateFontInfo TimeFont = HUDFont(EOneBrahamHUDType::TelemetryLight, 12.f, 0, C.bHeavyOutline);
	const FSlateFontInfo NameFont = HUDFont(EOneBrahamHUDType::Label, 14.f, 20, C.bHeavyOutline);
	const FSlateFontInfo WeaponFont = HUDFont(EOneBrahamHUDType::Telemetry, 11.f, 20, C.bHeavyOutline);
	const FSlateFontInfo SignedOut = HUDFont(EOneBrahamHUDType::Aside, 15.f, 0, C.bHeavyOutline);

	float Y = 0.f;
	int32 Shown = 0;

	// Newest first, never more than five: a feed longer than that is a log.
	for (int32 Index = Frame.Kills.Num() - 1; Index >= 0 && Shown < 5; --Index)
	{
		const FOneBrahamKillRowView& Row = Frame.Kills[Index];
		const float Age = Since(C.Now, Row.EventTime);
		if (Age > 7.4f)
		{
			continue;
		}

		const float In = C.bReduceMotion ? 1.f : FMath::Clamp(Age / 0.16f, 0.f, 1.f);
		const float Fade = 1.f - FMath::Clamp((Age - 7.f) / 0.4f, 0.f, 1.f);
		const float Slide = (1.f - OneBrahamEase::Evaluate(EOneBrahamEase::Ribbon, In)) * 12.f;
		FOneBrahamPainter R = P.Faded(Fade * In);

		// Laid out right to left: the victim is nearest the edge of the screen.
		struct FRun { FString Text; FSlateFontInfo Font; FLinearColor Ink; bool bUnderline = false; float Gap = 0.f; bool bHeadshotMark = false; };
		TArray<FRun> Runs;
		auto NameInk = [](bool bSquad) { return bSquad ? SignalBlue() : White(); };

		Runs.Add({ Row.Time, TimeFont, WithAlpha(LightGrey(), 0.5f), false, 10.f });
		if (Row.bBuilding)
		{
			const FLinearColor BuildingInk = Row.Stage == EOneBrahamLockdownStage::Purge ? EmergencyRed()
				: (OneBrahamHUDStyle::StageBand(Row.Stage) == 1 ? SafetyYellow() : White());
			Runs.Add({ TEXT("THE BUILDING"), NameFont, BuildingInk, false, 6.f });
			Runs.Add({ TEXT("signed out"), SignedOut, WithAlpha(LightGrey(), 0.85f), false, 6.f });
		}
		else
		{
			Runs.Add({ Row.Killer.ToUpper(), NameFont, NameInk(Row.bKillerSquad), Row.bKillerYou, 8.f });
			Runs.Add({ FString::Printf(TEXT("— %s"), *Row.Weapon.ToUpper()), WeaponFont, WithAlpha(LightGrey(), 0.6f), false, 6.f });
			if (Row.bHeadshot)
			{
				// The headshot mark: a ring with a dot, drawn, so no font has to own it.
				Runs.Add({ TEXT("  "), WeaponFont, SafetyYellow(), false, 6.f, true });
			}
			Runs.Add({ TEXT("→"), WeaponFont, WithAlpha(LightGrey(), 0.6f), false, 8.f });
		}
		Runs.Add({ Row.Victim.ToUpper(), NameFont, NameInk(Row.bVictimSquad), Row.bVictimYou, 0.f });

		float Total = 0.f;
		float RowH = 0.f;
		for (const FRun& Run : Runs)
		{
			const FVector2D Size = Measure(Run.Text, Run.Font);
			Total += Size.X + Run.Gap;
			RowH = FMath::Max(RowH, Size.Y);
		}

		const float PlateH = 26.f;
		const float PlateW = Total + 20.f;
		const float X0 = -PlateW + Slide;
		Plate(R, FVector2D(X0, Y), FVector2D(PlateW, PlateH), 0.72f, 3.f);

		float X = X0 + 10.f;
		for (const FRun& Run : Runs)
		{
			const FVector2D Size = Measure(Run.Text, Run.Font);
			const float TY = Y + (PlateH - Size.Y) * 0.5f;
			if (Run.bHeadshotMark)
			{
				const FVector2D Mark(X + Size.X * 0.5f, Y + PlateH * 0.5f);
				R.Ring(Mark, 5.f, Run.Ink, 1.2f);
				R.Circle(Mark, 1.6f, Run.Ink);
			}
			else
			{
				R.Text(FVector2D(X, TY), Run.Text, Run.Font, Run.Ink);
			}
			if (Run.bUnderline)
			{
				R.Rect(FVector2D(X, TY + Size.Y - 3.f), FVector2D(Size.X, 1.f), Run.Ink);
			}
			X += Size.X + Run.Gap;
		}

		Y += PlateH + 4.f;
		++Shown;
	}
}

// ==========================================================================
//  7. The squad board: the in/out board
// ==========================================================================
void UOneBrahamHUDWidget::PaintSquadBoard(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	if (!Data.bShowSquadPanel)
	{
		return;
	}

	TArray<const FOneBrahamSquadRowView*> Rows;
	for (const FOneBrahamSquadRowView& Row : Frame.Squad)
	{
		if (!Row.bLocal)
		{
			Rows.Add(&Row);
		}
	}
	if (Rows.Num() == 0)
	{
		return;
	}

	const float SinceChange = Since(C.Now, SquadChangedAt);
	float Alpha = 1.f;
	if (C.bRestDimming && SinceChange > 8.f)
	{
		Alpha = FMath::Lerp(1.f, 0.7f, FMath::Clamp((SinceChange - 8.f) / 0.6f, 0.f, 1.f));
	}

	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeL, C.Screen.Y * 0.47f), C.Scale).Faded(Alpha);
	const float RowH = 30.f;
	const float BoardW = 292.f;
	const int32 Count = FMath::Min(Rows.Num(), 6);

	// Header: what the board is, in the building's words.
	const FString Header = Frame.Mode == EOneBrahamMatchMode::FireDrill ? TEXT("YOUR TEAM  ·  IN / OUT") : TEXT("YOUR SQUAD  ·  IN / OUT");
	P.Text(FVector2D(0.f, -24.f), Header, HUDFont(EOneBrahamHUDType::Eyebrow, 11.f, 200, C.bHeavyOutline), WithAlpha(LightGrey(), 0.6f));
	Plate(P, FVector2D(-8.f, -4.f), FVector2D(BoardW + 16.f, Count * RowH + 8.f), 0.72f, 4.f);

	const FSlateFontInfo NameFont = HUDFont(EOneBrahamHUDType::Label, 14.f, 20, false);
	const FSlateFontInfo FloorFont = HUDFont(EOneBrahamHUDType::Telemetry, 12.f, 0, false);
	const FSlateFontInfo TagFont = HUDFont(EOneBrahamHUDType::Label, 10.f, 100, false);
	const float Teammate = OneBrahamPulse::Breath(C.Now, OneBrahamPulse::TeammateHz, 0.45f, 1.f);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FOneBrahamSquadRowView& Row = *Rows[Index];
		const float Y = Index * RowH;
		const bool bOut = Row.LifeState == EOneBrahamLifeState::Dead || Row.LifeState == EOneBrahamLifeState::Spectating;
		const bool bDown = Row.LifeState == EOneBrahamLifeState::Downed;
		const bool bExtracted = Row.LifeState == EOneBrahamLifeState::Extracted;
		FOneBrahamPainter R = P.Faded(bOut ? 0.35f : 1.f);

		if (Index > 0)
		{
			R.Rect(FVector2D(0.f, Y), FVector2D(BoardW, 1.f), WithAlpha(LightGrey(), 0.08f));
		}

		// The voice lamp.
		const FVector2D Lamp(5.f, Y + RowH * 0.5f - 2.f);
		if (Row.bTransmitting && !bOut)
		{
			R.Circle(Lamp, 4.f, SignalBlue());
			R.Circle(Lamp, 7.f, WithAlpha(SignalBlue(), 0.2f));
		}
		else
		{
			R.Ring(Lamp, 3.5f, WithAlpha(LightGrey(), 0.3f), 1.f);
		}

		// The name, and under it the 2 px health hairline in four segments.
		const FString Name = Row.Name.Len() > 18 ? Row.Name.Left(17) + TEXT("…") : Row.Name;
		const FVector2D NameSize = R.Text(FVector2D(18.f, Y + 5.f), Name, NameFont, White());
		if (bOut)
		{
			R.Rect(FVector2D(18.f, Y + 5.f + NameSize.Y * 0.55f), FVector2D(NameSize.X, 1.5f), White());
		}

		float NameEnd = 18.f + NameSize.X;
		if (Row.bTransmitting && !bOut)
		{
			// A three-bar level meter beside the name while they talk.
			for (int32 Bar = 0; Bar < 3; ++Bar)
			{
				const float Level = 0.35f + 0.65f * FMath::Abs(FMath::Sin(static_cast<float>(C.Now) * (11.f + Bar * 4.3f) + Bar * 1.7f));
				const float H = 9.f * Level;
				R.Rect(FVector2D(NameEnd + 6.f + Bar * 4.f, Y + 5.f + NameSize.Y * 0.5f + 4.5f - H), FVector2D(2.f, H), SignalBlue());
			}
			NameEnd += 20.f;
		}
		if (Row.bKeycard)
		{
			R.RoundRect(FVector2D(NameEnd + 6.f, Y + 9.f), FVector2D(10.f, 7.f), WithAlpha(LightGrey(), 0.85f), 1.f);
			R.Rect(FVector2D(NameEnd + 6.f, Y + 11.f), FVector2D(10.f, 1.f), WithAlpha(Navy(), 0.9f));
		}

		const float SegW = 28.f;
		for (int32 Seg = 0; Seg < 4; ++Seg)
		{
			const float Fill = FMath::Clamp(Row.HealthFraction * 4.f - Seg, 0.f, 1.f);
			const FVector2D SegPos(18.f + Seg * (SegW + 2.f), Y + RowH - 6.f);
			R.Rect(SegPos, FVector2D(SegW, 2.f), WithAlpha(LightGrey(), 0.15f));
			R.Rect(SegPos, FVector2D(SegW * Fill, 2.f), bDown ? EmergencyRed() : WithAlpha(LightGrey(), 0.85f));
		}

		// The floor, right-aligned, in the lift's own format.
		const FString Floor = Row.Floor != INDEX_NONE && !bOut && !bExtracted ? FString::Printf(TEXT("L%02d"), Row.Floor) : TEXT("--");
		R.Text(FVector2D(BoardW - 74.f, Y + 7.f), Floor, FloorFont, WithAlpha(LightGrey(), 0.85f), EOneBrahamTextAnchor::Right);

		// The status tag.
		FString Tag = TEXT("IN");
		if (bDown)			{ Tag = FString::Printf(TEXT("DOWN %s"), *Clock(Row.BleedOutRemaining, false)); }
		else if (bOut)		{ Tag = TEXT("OUT"); }
		else if (bExtracted){ Tag = TEXT("EXTRACTED"); }

		const FSlateFontInfo& TFont = bDown ? FloorFont : TagFont;
		const FVector2D TagSize = Measure(Tag, TFont) + FVector2D(10.f, 2.f);
		const FVector2D TagPos(BoardW - TagSize.X, Y + (RowH - TagSize.Y) * 0.5f - 1.f);
		if (bDown)
		{
			R.RoundRect(TagPos, TagSize, WithAlpha(EmergencyRed(), Teammate), 2.f);
			R.Text(TagPos + FVector2D(5.f, 1.f), Tag, TFont, White());
		}
		else
		{
			const FLinearColor TagInk = bExtracted ? SafetyYellow() : WithAlpha(LightGrey(), 0.75f);
			R.Outline(TagPos, TagSize, TagInk, 1.f, 2.f);
			R.Text(TagPos + FVector2D(5.f, 1.f), Tag, TFont, TagInk);
		}
	}
}

// ==========================================================================
//  7a. MIC LIVE
// ==========================================================================
void UOneBrahamHUDWidget::PaintMicPill(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	if (!Data.bShowVoiceIndicator || !Data.bLocalTransmitting)
	{
		return;
	}

	FOneBrahamPainter P = Root.Child(FVector2D(C.Screen.X * 0.5f - 220.f * C.Scale, C.Screen.Y * 0.92f), C.Scale);
	const FSlateFontInfo Word = HUDFont(EOneBrahamHUDType::Label, 11.f, 120, false);
	const FSlateFontInfo Channel = HUDFont(EOneBrahamHUDType::Telemetry, 11.f, 40, false);
	const FString Range = Frame.VoiceRangeMetres > 0 ? FString::Printf(TEXT("  ·  %d m"), Frame.VoiceRangeMetres) : FString();
	const FString ChannelText = (Frame.VoiceChannelWord.IsEmpty() ? FString(TEXT("PROXIMITY")) : Frame.VoiceChannelWord) + Range;

	const float W = 34.f + Measure(TEXT("MIC LIVE"), Word).X + 12.f + Measure(ChannelText, Channel).X + 14.f;
	P.RoundRect(FVector2D::ZeroVector, FVector2D(W, 24.f), WithAlpha(Navy(), 0.86f), 12.f);
	P.Outline(FVector2D::ZeroVector, FVector2D(W, 24.f), WithAlpha(SignalBlue(), 0.6f), 1.f, 12.f);

	// The ring breathes with the voice; there is no level getter on the voice
	// component yet, so it breathes at speaking pace instead.
	const float Level = 0.5f + 0.5f * FMath::Abs(FMath::Sin(static_cast<float>(C.Now) * 9.f));
	P.Circle(FVector2D(16.f, 12.f), 4.f, SignalBlue());
	P.Ring(FVector2D(16.f, 12.f), 6.f + 2.f * Level, WithAlpha(SignalBlue(), 0.6f * Level), 1.f);
	const FVector2D Mic = P.Text(FVector2D(30.f, 12.f - Measure(TEXT("M"), Word).Y * 0.5f), TEXT("MIC LIVE"), Word, White());
	P.Text(FVector2D(30.f + Mic.X + 12.f, 12.f - Measure(TEXT("M"), Channel).Y * 0.5f), ChannelText, Channel, WithAlpha(LightGrey(), 0.8f));
}

// ==========================================================================
//  8. The staff pass: health and armour
// ==========================================================================
void UOneBrahamHUDWidget::PaintHealthCard(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	if (Data.LifeState == EOneBrahamLifeState::Downed)
	{
		return;
	}

	const FVector2D Card(340.f, 84.f);
	// A heavy hit nudges the card 3 px left and back in 90 ms (not with Reduce Motion).
	const float NudgeT = Since(C.Now, NudgeAt);
	const float Nudge = !C.bReduceMotion && NudgeT >= 0.f && NudgeT < 0.09f ? -3.f * FMath::Sin(NudgeT / 0.09f * PI) : 0.f;
	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeL + Nudge, C.SafeB - Card.Y * C.Scale), C.Scale);

	const float MaxHealth = FMath::Max(1.f, Data.MaxHealth);
	const float Health = FMath::Clamp(Data.Health, 0.f, MaxHealth);
	const bool bCritical = Health < 30.f;
	const float Breath = OneBrahamPulse::Breath(C.Now, OneBrahamPulse::BreathHz, 0.4f, 1.f);

	// The chromatic shove of a heavy hit: 70 ms, two pixels, red and blue.
	const float HeavyT = Since(C.Now, HeavyHitAt);
	if (!C.bReduceMotion && HeavyT >= 0.f && HeavyT < 0.07f)
	{
		P.RoundRect(FVector2D(2.f, 0.f), Card, WithAlpha(EmergencyRed(), 0.35f), 4.f);
		P.RoundRect(FVector2D(-2.f, 0.f), Card, WithAlpha(SignalBlue(), 0.35f), 4.f);
	}

	Plate(P, FVector2D::ZeroVector, Card, 0.78f, 4.f);
	if (bCritical)
	{
		P.Outline(FVector2D(-1.f, -1.f), Card + FVector2D(2.f, 2.f), WithAlpha(EmergencyRed(), Breath), 2.f, 5.f);
	}

	// The photo slot: the archetype's initials, as a pass's photo would be on a
	// building where nobody has updated the photos.
	const FLinearColor Photo = bCritical ? EmergencyRed() : WithAlpha(SignalBlue(), 0.85f);
	P.RoundRect(FVector2D::ZeroVector, FVector2D(60.f, Card.Y), Photo, 4.f);
	P.Rect(FVector2D(30.f, 0.f), FVector2D(30.f, Card.Y), Photo);
	const FString Initial = Initials(Frame.PlayerName.IsEmpty() ? FString(TEXT("You")) : Frame.PlayerName);
	const FSlateFontInfo InitialFont = Font(EOneBrahamHUDType::Verb, 22.f);
	P.Text(FVector2D(30.f, Card.Y * 0.5f - Measure(Initial, InitialFont).Y * 0.5f - 6.f), Initial, InitialFont, Navy(), EOneBrahamTextAnchor::Centre);
	// The lanyard slot, cut through the top of the pass.
	P.RoundRect(FVector2D(20.f, 6.f), FVector2D(20.f, 4.f), WithAlpha(Navy(), 0.55f), 2.f);

	// The number: counts to the new value rather than jumping.
	const float CountT = FMath::Clamp(Since(C.Now, CountAt) / 0.22f, 0.f, 1.f);
	const float Shown = FMath::Lerp(CountFrom, Health, CountT);
	const FString Number = FString::FromInt(FMath::CeilToInt(Shown));
	const FSlateFontInfo NumberFont = HUDFont(EOneBrahamHUDType::TelemetryStrong, 44.f, -20, false);
	const FVector2D NumberSize = Measure(Number, NumberFont);
	P.Text(FVector2D(Card.X - 14.f, 0.f), Number, NumberFont, bCritical ? EmergencyRed() : White(), EOneBrahamTextAnchor::Right);

	// The top line: who, and what they do, in whatever the number leaves. The
	// space is measured against "100" so the line does not reflow as health
	// changes; when name and role do not both fit, the role goes first, and a
	// long name ends in an ellipsis rather than in the numerals.
	const FSlateFontInfo WhoFont = HUDFont(EOneBrahamHUDType::Label, 12.f, 60, false);
	const float WhoW = Card.X - 14.f - FMath::Max(NumberSize.X, Measure(TEXT("100"), NumberFont).X) - 16.f - 72.f - (bCritical ? 18.f : 0.f);
	const FString Name = Frame.PlayerName.ToUpper();
	const FString WithRole = Frame.RoleLabel.IsEmpty() ? Name : FString::Printf(TEXT("%s  ·  %s"), *Name, *Frame.RoleLabel.ToUpper());
	const FString Who = Measure(WithRole, WhoFont).X <= WhoW ? WithRole : Fit(Name, WhoFont, WhoW);
	P.Text(FVector2D(72.f, 9.f), Who, WhoFont, WithAlpha(White(), 0.8f));
	if (Who != WithRole && !Frame.RoleLabel.IsEmpty())
	{
		// The role on its own line under the name, quieter: a pass prints both.
		const FSlateFontInfo RoleFont = HUDFont(EOneBrahamHUDType::TelemetryLight, 10.f, 80, false);
		P.Text(FVector2D(72.f, 27.f), Fit(Frame.RoleLabel.ToUpper(), RoleFont, WhoW), RoleFont, WithAlpha(LightGrey(), 0.55f));
	}
	if (bCritical)
	{
		// The critical glyph: a triangle with a bang.
		const FVector2D G(Card.X - 22.f - NumberSize.X, 18.f);
		P.Triangle(G + FVector2D(-7.f, 6.f), G + FVector2D(7.f, 6.f), G + FVector2D(0.f, -7.f), EmergencyRed());
		P.Rect(G + FVector2D(-0.75f, -3.f), FVector2D(1.5f, 5.f), White());
		P.Rect(G + FVector2D(-0.75f, 3.5f), FVector2D(1.5f, 1.5f), White());
	}

	// --- the cells: ten of ten HP, emptying from the right --------------------------------
	const float BarX = 72.f;
	const float BarY = 62.f;
	const float CellW = 22.f;
	const float CellH = 10.f;
	const float Gap = 2.f;
	const float Per = MaxHealth / 10.f;

	// The ghost: what was just lost, held 300 ms in red then drained in 350.
	const float GhostT = Since(C.Now, GhostAt);
	const float Drain = GhostT < 0.3f ? 0.f : FMath::Clamp((GhostT - 0.3f) / 0.35f, 0.f, 1.f);
	const float GhostTop = GhostT >= 0.f && GhostT < 0.65f ? FMath::Lerp(GhostFrom, GhostTo, OneBrahamEase::Evaluate(EOneBrahamEase::Exit, Drain)) : 0.f;

	const float HealT = Since(C.Now, HealAt);
	for (int32 Cell = 0; Cell < 10; ++Cell)
	{
		const FVector2D Pos(BarX + Cell * (CellW + Gap), BarY);
		const float Fill = FMath::Clamp((Health - Cell * Per) / Per, 0.f, 1.f);
		P.Rect(Pos, FVector2D(CellW, CellH), WithAlpha(LightGrey(), 0.12f));

		const float GhostFill = FMath::Clamp((GhostTop - Cell * Per) / Per, 0.f, 1.f);
		if (GhostFill > Fill)
		{
			P.Rect(Pos, FVector2D(CellW * GhostFill, CellH), EmergencyRed());
		}

		if (Fill > 0.f)
		{
			// A heal refills left to right, 30 ms a cell, each with one white frame.
			const float CellTime = HealT - Cell * 0.03f;
			const bool bRefilling = HealT >= 0.f && HealT < 0.4f && Cell * Per >= HealFrom * MaxHealth - Per;
			if (bRefilling && CellTime < 0.f)
			{
				continue;
			}
			const FLinearColor Ink = bRefilling && CellTime < 0.034f ? White() : (bCritical ? EmergencyRed() : White());
			P.Rect(Pos, FVector2D(CellW * Fill, CellH), Ink);
		}
	}

	// --- armour: the magnetic stripe above the cells ------------------------------------
	const float MaxArmour = FMath::Max(1.f, Data.MaxArmour);
	const float Armour = FMath::Clamp(Data.Armour, 0.f, MaxArmour);
	const float StripeW = (CellW * 10.f + Gap * 9.f - 4.f * 2.f) / 5.f;
	const float BreakT = Since(C.Now, ArmourBreakAt);
	for (int32 Cell = 0; Cell < 5; ++Cell)
	{
		const FVector2D Pos(BarX + Cell * (StripeW + 2.f), BarY - 8.f);
		if (Armour > 0.f)
		{
			const float Fill = FMath::Clamp((Armour - Cell * MaxArmour / 5.f) / (MaxArmour / 5.f), 0.f, 1.f);
			P.Rect(Pos, FVector2D(StripeW, 4.f), WithAlpha(LightGrey(), 0.14f));
			P.Rect(Pos, FVector2D(StripeW * Fill, 4.f), LightGrey());
		}
		else if (!C.bReduceMotion && BreakT >= 0.f && BreakT < 0.2f)
		{
			// The break: the five cells scatter 6 px and fade.
			const float T = BreakT / 0.2f;
			const FVector2D Scatter(((Cell % 2) ? 1.f : -1.f) * 6.f * T, (Cell - 2) * 1.5f * T - 3.f * T);
			P.Rect(Pos + Scatter, FVector2D(StripeW, 4.f), WithAlpha(LightGrey(), 1.f - T));
		}
	}
}

// ==========================================================================
//  9. Ammo and fire mode
// ==========================================================================
void UOneBrahamHUDWidget::PaintAmmo(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	if (!Data.bShowAmmoCount || Data.WeaponId.IsNone() || Data.LifeState == EOneBrahamLifeState::Downed)
	{
		return;
	}

	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeR, C.SafeB), C.Scale);
	const int32 Mag = FMath::Max(0, Data.AmmoInMagazine);
	const int32 MagSize = FMath::Max(1, Data.MagazineSize);
	const bool bEmpty = Mag == 0 && !Data.bReloading;
	const bool bLow = !bEmpty && Mag <= FMath::CeilToInt(MagSize * 0.3f);

	// --- the fire-mode row, at the bottom -------------------------------------------------
	const FSlateFontInfo ModeFont = HUDFont(EOneBrahamHUDType::Label, 11.f, 120, false);
	TArray<EOneBrahamFireMode> Modes = Frame.FireModes;
	if (Modes.Num() == 0)
	{
		Modes.Add(Data.FireMode);
	}
	float X = 0.f;
	const float RowY = -16.f;
	float ActiveX = 0.f, ActiveW = 0.f, PrevX = 0.f, PrevW = 0.f;
	for (int32 Index = Modes.Num() - 1; Index >= 0; --Index)
	{
		const FString Word = FireModeWord(Modes[Index]);
		const FVector2D Size = Measure(Word, ModeFont);
		X -= Size.X;
		const bool bActive = Modes[Index] == Data.FireMode;
		P.Text(FVector2D(X, RowY), Word, ModeFont, WithAlpha(White(), bActive ? 1.f : 0.35f));
		if (bActive) { ActiveX = X; ActiveW = Size.X; }
		if (Modes[Index] == PrevFireMode) { PrevX = X; PrevW = Size.X; }
		X -= 10.f;
		if (Index > 0)
		{
			P.Text(FVector2D(X + 1.f, RowY), TEXT("·"), ModeFont, WithAlpha(White(), 0.35f));
			X -= 10.f;
		}
	}
	// The underline slides to the new mode in 120 ms.
	const float Slide = Ramp(C.Now, FireModeAt, 0.12f, EOneBrahamEase::Ribbon);
	const float UX = PrevW > 0.f ? FMath::Lerp(PrevX, ActiveX, Slide) : ActiveX;
	const float UW = PrevW > 0.f ? FMath::Lerp(PrevW, ActiveW, Slide) : ActiveW;
	P.Rect(FVector2D(UX, RowY + Measure(TEXT("A"), ModeFont).Y + 1.f), FVector2D(UW, 2.f), White());

	// --- the number and the reserve ---------------------------------------------------------
	const FSlateFontInfo MagFont = HUDFont(EOneBrahamHUDType::TelemetryStrong, 56.f, -30, false);
	const FSlateFontInfo ReserveFont = HUDFont(EOneBrahamHUDType::TelemetryLight, 20.f, 0, false);
	const FString Reserve = FString::Printf(TEXT("/ %d"), FMath::Max(0, Data.AmmoReserve));
	const FVector2D ReserveSize = Measure(Reserve, ReserveFont);
	const FString MagText = FString::FromInt(Mag);
	const FVector2D MagSizePx = Measure(MagText, MagFont);
	const float NumberTop = RowY - 4.f - MagSizePx.Y + 8.f;
	const float ReserveX = -ReserveSize.X;
	P.Text(FVector2D(ReserveX, NumberTop + MagSizePx.Y - ReserveSize.Y - 10.f), Reserve, ReserveFont, WithAlpha(White(), 0.6f));
	const FLinearColor MagInk = bEmpty ? EmergencyRed() : (bLow ? SafetyYellow() : White());
	const float MagRight = ReserveX - 8.f;
	P.Text(FVector2D(MagRight, NumberTop), MagText, MagFont, MagInk, EOneBrahamTextAnchor::Right);

	if (bLow)
	{
		// The low glyph: a filled corner wedge, the shape that says "running out".
		const FVector2D G(MagRight - MagSizePx.X - 12.f, NumberTop + MagSizePx.Y - 22.f);
		P.Triangle(G + FVector2D(0.f, 10.f), G + FVector2D(10.f, 10.f), G + FVector2D(10.f, 0.f), SafetyYellow());
	}

	if (bEmpty)
	{
		// RELOAD blinks at 2 Hz until the reload starts: act now.
		const float Blink = OneBrahamPulse::Blink(C.Now, OneBrahamPulse::ActNowHz, C.bReduceMotion);
		P.Text(FVector2D(0.f, NumberTop - 18.f), TEXT("RELOAD"), HUDFont(EOneBrahamHUDType::Verb, 14.f, 100, false),
			WithAlpha(EmergencyRed(), 0.25f + 0.75f * Blink), EOneBrahamTextAnchor::Right);
	}
	else if (Data.bReloading)
	{
		P.Text(FVector2D(0.f, NumberTop - 16.f), TEXT("RELOADING"), HUDFont(EOneBrahamHUDType::Label, 11.f, 160, false),
			WithAlpha(LightGrey(), 0.75f), EOneBrahamTextAnchor::Right);
	}

	// --- the weapon's name ----------------------------------------------------------------------
	const FString Display = Data.WeaponDisplayName.ToString().ToUpper();
	const FString Name = Frame.WeaponCategory.IsEmpty() || Display.Contains(Frame.WeaponCategory) ? Display
		: FString::Printf(TEXT("%s  ·  %s"), *Display, *Frame.WeaponCategory);
	if (!bEmpty && !Data.bReloading)
	{
		P.Text(FVector2D(0.f, NumberTop - 14.f), Name, HUDFont(EOneBrahamHUDType::Label, 13.f, 80, false), WithAlpha(White(), 0.85f), EOneBrahamTextAnchor::Right);
	}

	// --- the magazine gauge: a tick a round, rows of thirty ---------------------------------------
	const float GaugeRight = MagRight - MagSizePx.X - (bLow ? 28.f : 14.f);
	const int32 PerRow = 30;
	const int32 RowsNeeded = FMath::DivideAndRoundUp(MagSize, PerRow);
	const float TickW = 2.f;
	const float TickH = RowsNeeded > 2 ? 6.f : 10.f;
	const float GaugeBottom = NumberTop + MagSizePx.Y - 12.f;
	const float ShotT = Since(C.Now, ShotAt);
	const float ReloadFill = Data.bReloading ? FMath::Clamp(Data.ReloadProgress01, 0.f, 1.f) : 1.f;
	for (int32 Round = 0; Round < MagSize; ++Round)
	{
		const int32 RowIndex = Round / PerRow;
		const int32 Col = Round % PerRow;
		const FVector2D Pos(GaugeRight - (PerRow - Col) * (TickW + 1.f), GaugeBottom - (RowIndex + 1) * (TickH + 2.f));
		const bool bLoaded = Round < Mag;
		float Alpha = bLoaded ? 0.9f : 0.12f;
		if (!bLoaded && Round == Mag && ShotT >= 0.f && ShotT < 0.06f)
		{
			Alpha = FMath::Lerp(0.9f, 0.12f, ShotT / 0.06f);
		}
		if (Data.bReloading)
		{
			Alpha = static_cast<float>(Round) / MagSize < ReloadFill ? 0.6f : 0.12f;
		}
		P.Rect(Pos, FVector2D(TickW, TickH), WithAlpha(bEmpty ? EmergencyRed() : (bLow && bLoaded ? SafetyYellow() : White()), Alpha));
	}

	// --- utilities: up to three, left of the block ------------------------------------------------
	struct FUtility { const TCHAR* Glyph; int32 Count; };
	TArray<FUtility> Utilities;
	if (Data.FragCount > 0)		{ Utilities.Add({ TEXT("FRAG"), Data.FragCount }); }
	if (Data.FlashCount > 0)	{ Utilities.Add({ TEXT("FLASH"), Data.FlashCount }); }
	if (Data.SmokeCount > 0)	{ Utilities.Add({ TEXT("SMOKE"), Data.SmokeCount }); }
	if (Data.BreachCount > 0)	{ Utilities.Add({ TEXT("BREACH"), Data.BreachCount }); }
	if (Data.MedkitCount > 0)	{ Utilities.Add({ TEXT("MED"), Data.MedkitCount }); }

	float UX2 = GaugeRight - PerRow * (TickW + 1.f) - 18.f;
	const FSlateFontInfo CountFont = HUDFont(EOneBrahamHUDType::Telemetry, 12.f, 0, false);
	for (int32 Index = 0; Index < FMath::Min(3, Utilities.Num()); ++Index)
	{
		const FUtility& Utility = Utilities[Index];
		const FVector2D Box(UX2 - 28.f, GaugeBottom - 28.f);
		P.RoundRect(Box, FVector2D(28.f, 28.f), WithAlpha(Navy(), 0.72f), 3.f);
		P.Outline(Box, FVector2D(28.f, 28.f), WithAlpha(LightGrey(), 0.25f), 1.f, 3.f);
		const FVector2D Centre = Box + FVector2D(14.f, 14.f);
		const FString Glyph(Utility.Glyph);
		if (Glyph == TEXT("FRAG"))
		{
			P.Circle(Centre + FVector2D(0.f, 2.f), 6.f, WithAlpha(LightGrey(), 0.9f));
			P.Rect(Centre + FVector2D(-2.f, -8.f), FVector2D(4.f, 4.f), WithAlpha(LightGrey(), 0.9f));
		}
		else if (Glyph == TEXT("FLASH"))
		{
			P.Triangle(Centre + FVector2D(2.f, -9.f), Centre + FVector2D(-5.f, 2.f), Centre + FVector2D(1.f, 2.f), WithAlpha(LightGrey(), 0.9f));
			P.Triangle(Centre + FVector2D(-1.f, -2.f), Centre + FVector2D(5.f, -2.f), Centre + FVector2D(-2.f, 9.f), WithAlpha(LightGrey(), 0.9f));
		}
		else if (Glyph == TEXT("SMOKE"))
		{
			P.Circle(Centre + FVector2D(-3.f, 1.f), 4.f, WithAlpha(LightGrey(), 0.8f));
			P.Circle(Centre + FVector2D(3.f, -2.f), 5.f, WithAlpha(LightGrey(), 0.8f));
			P.Circle(Centre + FVector2D(2.f, 4.f), 3.5f, WithAlpha(LightGrey(), 0.8f));
		}
		else if (Glyph == TEXT("MED"))
		{
			P.Rect(Centre + FVector2D(-2.f, -7.f), FVector2D(4.f, 14.f), WithAlpha(LightGrey(), 0.9f));
			P.Rect(Centre + FVector2D(-7.f, -2.f), FVector2D(14.f, 4.f), WithAlpha(LightGrey(), 0.9f));
		}
		else
		{
			P.Rect(Centre + FVector2D(-6.f, -4.f), FVector2D(12.f, 8.f), WithAlpha(LightGrey(), 0.9f));
		}
		P.Text(FVector2D(Box.X + 14.f, Box.Y + 30.f), FString::FromInt(Utility.Count), CountFont, WithAlpha(LightGrey(), 0.85f), EOneBrahamTextAnchor::Centre);
		UX2 -= 36.f;
	}
}

// ==========================================================================
//  10. Crosshair and hit markers
// ==========================================================================
void UOneBrahamHUDWidget::PaintCrosshair(FOneBrahamPainter& P, const FPaintContext& C) const
{
	const FVector2D Centre = C.Screen * 0.5f;

	if (Data.bShowCrosshair && Data.LifeState == EOneBrahamLifeState::Alive)
	{
		// The honest dynamic crosshair (UOneBrahamCrosshairWidget's rule): the
		// gap is the weapon's real cone projected onto a reference plane.
		const float HalfWidthFraction = FMath::Tan(FMath::DegreesToRadians(FMath::Max(0.f, Data.SpreadDegrees) * 0.5f));
		const float Gap = FMath::Clamp(3.f + HalfWidthFraction * 1.35f * C.Screen.Y * 0.5f, 3.f, 64.f);
		const float Len = 7.f;
		const FLinearColor Outline(0.f, 0.f, 0.f, 0.55f);
		const FLinearColor Ink = Data.bInteractionBlocked ? EmergencyRed() : White();

		auto Tick = [&](const FVector2D& Dir)
		{
			const FVector2D A = Centre + Dir * Gap;
			const FVector2D B = Centre + Dir * (Gap + Len);
			const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
			const FVector2D Size = FVector2D(FMath::Abs(B.X - A.X), FMath::Abs(B.Y - A.Y)) + FVector2D(Dir.X == 0.f ? 1.f : 0.f, Dir.Y == 0.f ? 1.f : 0.f);
			const FVector2D Snap(FMath::RoundToFloat(Min.X - (Dir.X == 0.f ? 0.5f : 0.f)), FMath::RoundToFloat(Min.Y - (Dir.Y == 0.f ? 0.5f : 0.f)));
			P.Rect(Snap - FVector2D(1.f, 1.f), Size + FVector2D(2.f, 2.f), Outline);
			P.Rect(Snap, Size, Ink);
		};
		Tick(FVector2D(1.f, 0.f));
		Tick(FVector2D(-1.f, 0.f));
		Tick(FVector2D(0.f, 1.f));
		Tick(FVector2D(0.f, -1.f));
	}

	// --- hit markers: four 45-degree ticks at 14 px -----------------------------------------------
	const float HitT = Since(C.Now, HitAt);
	const float Life = bHitKill ? 0.16f : 0.11f;
	if (Data.bShowHitMarkers && HitT >= 0.f && HitT < Life + 0.08f)
	{
		const float Grow = FMath::Lerp(1.f, 1.25f, FMath::Clamp(HitT / Life, 0.f, 1.f));
		const float Fade = 1.f - FMath::Clamp((HitT - Life) / 0.08f, 0.f, 1.f);
		const FLinearColor Ink = bHitKill ? EmergencyRed() : (bHitHeadshot ? SafetyYellow() : White());
		const float Thick = bHitHeadshot ? 3.f : 2.f;
		const float R0 = 14.f * Grow;
		for (int32 Q = 0; Q < 4; ++Q)
		{
			const float Angle = FMath::DegreesToRadians(45.f + 90.f * Q);
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			P.Segment(Centre + Dir * (R0 - 5.f), Centre + Dir * (R0 + 5.f), FLinearColor(0.f, 0.f, 0.f, 0.5f * Fade), Thick + 2.f);
			P.Segment(Centre + Dir * (R0 - 5.f), Centre + Dir * (R0 + 5.f), WithAlpha(Ink, Fade), Thick);
		}
		if (bHitKill)
		{
			P.Cross(Centre, 8.f, WithAlpha(EmergencyRed(), Fade), 2.f);
		}
	}
}

// ==========================================================================
//  10a. Damage arcs, with the floor they came from
// ==========================================================================
void UOneBrahamHUDWidget::PaintDamageArcs(FOneBrahamPainter& P, const FPaintContext& C) const
{
	if (!Data.bShowDamageDirection || DamageArcs.Num() == 0)
	{
		return;
	}

	const APlayerController* PC = GetOwningPlayer();
	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
	{
		return;
	}

	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
	const FVector2D Centre = C.Screen * 0.5f;
	const float Radius = C.Screen.Y * 0.22f;
	const FSlateFontInfo DeltaFont = HUDFont(EOneBrahamHUDType::Telemetry, 11.f, 0, true);

	for (const FDamageArc& Arc : DamageArcs)
	{
		const float Age = Since(C.Now, Arc.At);
		if (Age < 0.f || Age > 0.9f)
		{
			continue;
		}

		// Recomputed against the view every frame, so the arc keeps pointing at
		// the shooter while the player turns to face them.
		const FVector To = Arc.From - Eye;
		const float Bearing = FMath::FindDeltaAngleDegrees(Yaw, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
		const float Fade = 1.f - Age / 0.9f;
		const float Thick = FMath::Lerp(6.f, 14.f, FMath::Clamp(Arc.Damage / 60.f, 0.f, 1.f));
		const FLinearColor Head = WithAlpha(EmergencyRed(), 0.9f * Fade);
		const FLinearColor Tail = WithAlpha(EmergencyRed(), 0.f);

		// Two halves from the middle out, so the arc is brightest where the
		// shooter is and its tails fade either side.
		P.Arc(Centre, Radius, Bearing, -20.f, Head, Thick, &Tail);
		P.Arc(Centre, Radius, Bearing, 20.f, Head, Thick, &Tail);

		if (Arc.FloorDelta != 0)
		{
			// Verticality: in a tower the floor is the useful half of "where".
			const float Rad = FMath::DegreesToRadians(Bearing - 90.f);
			const FVector2D Dir(FMath::Cos(Rad), FMath::Sin(Rad));
			const FVector2D At = Centre + Dir * (Radius + Thick + 14.f);
			const float Up = Arc.FloorDelta > 0 ? -1.f : 1.f;
			P.Triangle(At + FVector2D(-5.f, -Up * 3.f), At + FVector2D(5.f, -Up * 3.f), At + FVector2D(0.f, Up * 6.f), Head);
			P.Text(At + FVector2D(8.f, -8.f), FString::Printf(TEXT("%+d"), Arc.FloorDelta), DeltaFont, Head);
		}
	}
}

// ==========================================================================
//  11. The PA line card
// ==========================================================================
void UOneBrahamHUDWidget::PaintAnnouncement(FOneBrahamPainter& Root, const FPaintContext& C, float Top) const
{
	const FAnnouncement& A = CurrentAnnouncement;
	if (A.At < 0.0)
	{
		return;
	}

	const float T = Since(C.Now, A.At);
	if (T < 0.f || T > A.Hold + 0.3f)
	{
		return;
	}

	const float Width = C.Screen.X * 0.46f / C.Scale;
	const float Height = 78.f;
	FOneBrahamPainter P = Root.Child(FVector2D(C.Screen.X * 0.5f - Width * 0.5f * C.Scale, Top), C.Scale);

	// In: a left-to-right wipe over 280 ms. Out: 300 ms, fading and rising.
	const float Wipe = C.bReduceMotion ? 1.f : Ramp(C.Now, A.At, 0.28f, EOneBrahamEase::Ribbon);
	const float Leave = FMath::Clamp((T - A.Hold) / 0.3f, 0.f, 1.f);
	FOneBrahamPainter Card = P.Faded(1.f - OneBrahamEase::Evaluate(EOneBrahamEase::Exit, Leave));

	// The field: the stage's two colours, with a slow light passing across it -
	// the same aurora the OLED screens in the building run.
	FLinearColor FieldA = RibbonPurple();
	FLinearColor FieldB = RibbonBlue();
	const int32 Band = OneBrahamHUDStyle::StageBand(A.Stage);
	if (Band == 1)		{ FieldA = HazardYellow(); FieldB = LockdownFieldDark(); }
	else if (Band == 2)	{ FieldA = EmergencyRed(); FieldB = PurgeFieldDark(); }

	Card.PushClip(FVector2D::ZeroVector, FVector2D(Width * Wipe, Height));
	Card.Gradient(FVector2D::ZeroVector, FVector2D(Width, Height), FieldA, FieldB, true, 4.f);
	const float Pan = static_cast<float>(FMath::Fmod(C.Now / 6.0, 1.0));
	const float LightX = (Pan * 1.6f - 0.3f) * Width;
	Card.Gradient(FVector2D(LightX - 160.f, 0.f), FVector2D(160.f, Height), WithAlpha(White(), 0.f), WithAlpha(White(), 0.10f));
	Card.Gradient(FVector2D(LightX, 0.f), FVector2D(160.f, Height), WithAlpha(White(), 0.10f), WithAlpha(White(), 0.f));
	Card.Gradient(FVector2D(0.f, Height * 0.55f), FVector2D(Width, Height * 0.45f), WithAlpha(Navy(), 0.f), WithAlpha(Navy(), 0.25f), false);
	Card.PopClip();

	// The copy rises 6 px and fades in between 120 and 400 ms.
	const float CopyT = C.bReduceMotion ? 1.f : FMath::Clamp((T - 0.12f) / 0.28f, 0.f, 1.f);
	const float Rise = (1.f - OneBrahamEase::Evaluate(EOneBrahamEase::Ribbon, CopyT)) * 6.f;
	FOneBrahamPainter Copy = Card.Faded(CopyT);
	const FLinearColor Ink = Band == 1 ? Navy() : White();

	Copy.Text(FVector2D(18.f, 10.f + Rise), A.Eyebrow, Font(EOneBrahamHUDType::Eyebrow, 11.f, 220), WithAlpha(Ink, 0.8f));
	const FSlateFontInfo HeadlineFont = Font(EOneBrahamHUDType::Verb, A.Headline.Len() > 44 ? 22.f : 28.f, -10);
	Copy.PushClip(FVector2D(12.f, 0.f), FVector2D(Width - 24.f, Height));
	Copy.Text(FVector2D(18.f, 24.f + Rise), A.Headline, HeadlineFont, Ink);
	if (!A.Subline.IsEmpty())
	{
		Copy.Text(FVector2D(18.f, 54.f + Rise), A.Subline, Font(EOneBrahamHUDType::Aside, 18.f), WithAlpha(Ink, 0.9f));
	}
	Copy.PopClip();
}

// ==========================================================================
//  11a. The stage card: LOCKDOWN, PURGE, THE ROOF
// ==========================================================================
void UOneBrahamHUDWidget::PaintStageBars(FOneBrahamPainter& P, const FPaintContext& C) const
{
	if (StageCard == EOneBrahamStageCard::None)
	{
		return;
	}

	const float T = Since(C.Now, StageCardAt);
	const float HoldEnd = 0.35f + StageCardHold;
	if (T < 0.f || T > HoldEnd + 0.3f)
	{
		return;
	}

	// Bars: 7% each, black 85%, in over 350 ms (Ribbon), out over 300. The
	// Purge arrives as a hard cut on the bars - never the whole screen.
	const bool bPurge = StageCard == EOneBrahamStageCard::Purge;
	const float In = bPurge || C.bReduceMotion ? 1.f : Ramp(C.Now, StageCardAt, 0.35f, EOneBrahamEase::Ribbon);
	const float Out = FMath::Clamp((T - HoldEnd) / 0.3f, 0.f, 1.f);
	const float Bars = In * (1.f - OneBrahamEase::Evaluate(EOneBrahamEase::Exit, Out));
	const float BarH = C.Screen.Y * 0.07f * Bars;
	const float BarAlpha = bPurge && T < 2.f / 60.f ? 1.f : 0.85f;
	P.Rect(FVector2D::ZeroVector, FVector2D(C.Screen.X, BarH), FLinearColor(0.f, 0.f, 0.f, BarAlpha));
	P.Rect(FVector2D(0.f, C.Screen.Y - BarH), FVector2D(C.Screen.X, BarH), FLinearColor(0.f, 0.f, 0.f, BarAlpha));
}

void UOneBrahamHUDWidget::PaintStageCard(FOneBrahamPainter& P, const FPaintContext& C) const
{
	if (StageCard == EOneBrahamStageCard::None)
	{
		return;
	}

	const float T = Since(C.Now, StageCardAt);
	const float HoldEnd = 0.35f + StageCardHold;
	const float End = HoldEnd + 0.3f;
	if (T < 0.f || T > End)
	{
		return;
	}
	const float Out = FMath::Clamp((T - HoldEnd) / 0.3f, 0.f, 1.f);

	FString Title;
	FString Line;
	FLinearColor StageInk = SafetyYellow();
	switch (StageCard)
	{
	case EOneBrahamStageCard::Lockdown:
		Title = TEXT("LOCKDOWN");
		Line = TEXT("The lift lobby is closed. Please use the stairs.");
		break;
	case EOneBrahamStageCard::Purge:
		Title = TEXT("PURGE");
		Line = TEXT("Floors are being cleared. Thank you for your patience.");
		StageInk = EmergencyRed();
		break;
	case EOneBrahamStageCard::Roof:
	default:
		Title = TEXT("THE ROOF");
		Line = TEXT("Your lift is here. Please mind the gap.");
		break;
	}

	// The stack sits between 22% and 32% of the height: never in the band the
	// crosshair works in.
	const float Copy = FMath::Clamp((T - 0.08f) / 0.25f, 0.f, 1.f) * (1.f - Out);
	FOneBrahamPainter Q = P.Child(FVector2D(C.Screen.X * 0.5f, C.Screen.Y * 0.22f), C.Scale).Faded(Copy);

	Q.Text(FVector2D(0.f, 0.f), StageCardEyebrow, Font(EOneBrahamHUDType::TelemetryLight, 12.f, 240), StageInk, EOneBrahamTextAnchor::Centre);

	// The tracking settles from +200 to +40 over 1.2 s (held at +40 with Reduce Motion).
	const float Settle = C.bReduceMotion ? 1.f : Ramp(C.Now, StageCardAt, 1.2f, EOneBrahamEase::Ribbon);
	const int32 Tracking = FMath::RoundToInt(FMath::Lerp(200.f, 40.f, Settle));
	const FSlateFontInfo TitleFont = OutlinedFont(EOneBrahamHUDType::Title, 88.f, Tracking, 2.f, FLinearColor(0.f, 0.f, 0.f, 0.35f));
	const FVector2D TitleSize = Q.Text(FVector2D(0.f, 16.f), Title, TitleFont, White(), EOneBrahamTextAnchor::Centre);

	Q.Text(FVector2D(0.f, 16.f + TitleSize.Y - 4.f), Line, OutlinedFont(EOneBrahamHUDType::Aside, 24.f, 0, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f)),
		WithAlpha(White(), 0.92f), EOneBrahamTextAnchor::Centre);
}

// ==========================================================================
//  12. The card reader: interaction prompts
// ==========================================================================
void UOneBrahamHUDWidget::PaintPrompt(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	const float AcceptT = Since(C.Now, ReaderAcceptedAt);
	const bool bAccepting = AcceptT >= 0.f && AcceptT < 0.25f;
	const bool bTarget = Data.bShowInteractionPrompt && Data.bHasInteractionTarget;

	FOneBrahamPainter P = Root.Child(FVector2D(C.Screen.X * 0.5f, C.Screen.Y * 0.58f), C.Scale);

	if (bTarget || bAccepting)
	{
		const bool bLocked = bTarget && Data.bInteractionBlocked;
		const FString Prompt = bTarget ? LastPromptText : FString();
		FString Verb, Key, Action;
		SplitPrompt(Prompt, Verb, Key, Action);

		const FSlateFontInfo ActionFont = HUDFont(EOneBrahamHUDType::Label, 16.f, 20, C.bHeavyOutline);
		// "Hold E - Hold the winch" would read "Hold · Hold the winch": when the
		// action already starts with the verb, the action is the whole line.
		const bool bVerbRepeated = !Verb.IsEmpty() && Action.StartsWith(Verb + TEXT(" "), ESearchCase::IgnoreCase);
		const FString Shown = bLocked ? Prompt.ToUpper()
			: (Verb.IsEmpty() ? Prompt : (bVerbRepeated ? Action : FString::Printf(TEXT("%s  ·  %s"), *Verb, *Action)));
		const FVector2D TextSize = Measure(Shown, ActionFont);
		const float CapW = Key.IsEmpty() || bLocked ? 0.f : FMath::Max(18.f, Measure(Key, Font(EOneBrahamHUDType::Telemetry, 12.f)).X + 10.f) + 8.f;
		const float Ring = 36.f;
		const float Total = Ring + 12.f + CapW + TextSize.X;
		const float X0 = -Total * 0.5f;
		const FVector2D RingCentre(X0 + Ring * 0.5f, 0.f);

		// The plate behind the whole prompt.
		Plate(P, FVector2D(X0 - 10.f, -Ring * 0.5f - 6.f), FVector2D(Total + 22.f, Ring + 12.f), 0.72f, Ring * 0.5f + 6.f);

		// The reader: a ring that fills clockwise while held. Locked: red with a cross.
		const float Hold = FMath::Clamp(Data.InteractionHoldProgress01, 0.f, 1.f);
		if (bAccepting && !bTarget)
		{
			// Accepted: the ring flashes white and closes in 120 ms (Reader ease).
			const float Close = OneBrahamEase::Evaluate(EOneBrahamEase::Reader, AcceptT / 0.12f);
			P.Ring(RingCentre, Ring * 0.5f * FMath::Lerp(1.f, 0.6f, Close), WithAlpha(White(), 1.f - AcceptT / 0.25f), 3.f);
		}
		else if (bLocked)
		{
			P.Ring(RingCentre, Ring * 0.5f - 1.5f, EmergencyRed(), 3.f);
			P.Cross(RingCentre, 12.f, EmergencyRed(), 2.5f);
		}
		else
		{
			P.Ring(RingCentre, Ring * 0.5f - 1.5f, WithAlpha(LightGrey(), 0.3f), 3.f);
			if (Hold > 0.f)
			{
				P.Arc(RingCentre, Ring * 0.5f - 1.5f, 0.f, 360.f * Hold, SignalBlue(), 3.f);
			}
			// The reader's own lamp in the middle.
			P.Circle(RingCentre, 3.f, Hold > 0.f ? SignalBlue() : WithAlpha(LightGrey(), 0.6f));
		}

		float X = X0 + Ring + 12.f;
		if (CapW > 0.f)
		{
			KeyCap(P, FVector2D(X, -9.f), Key, White());
			X += CapW;
		}
		if (bTarget)
		{
			// Locked reads white: the red ring and its cross are the cue, and red
			// type on the plate over a lit wall was the hardest line to read.
			P.Text(FVector2D(X, -TextSize.Y * 0.5f), Shown, ActionFont, White());
		}
	}

	// --- the sub-banner: the induction's steps, and the building's asides ------------------
	const float SubT = Since(C.Now, SubBannerAt);
	if (!SubBannerShown.IsEmpty() && SubT >= 0.f && SubT < SubBannerSeconds)
	{
		const float Fade = FMath::Clamp(SubT / 0.15f, 0.f, 1.f) * (1.f - FMath::Clamp((SubT - SubBannerSeconds + 0.3f) / 0.3f, 0.f, 1.f));
		FOneBrahamPainter S = Root.Child(FVector2D(C.Screen.X * 0.5f, C.Screen.Y * 0.665f), C.Scale).Faded(Fade);

		// "INDUCTION  ·  Walk in from the street  -  W A S D": an eyebrow, the
		// instruction, and the keys as caps.
		FString Body = SubBannerShown;
		FString Eyebrow;
		if (Body.StartsWith(TEXT("INDUCTION")))
		{
			Eyebrow = TEXT("INDUCTION");
			Body = Body.Mid(9).TrimStart();
			if (Body.StartsWith(TEXT("·"))) { Body = Body.Mid(1).TrimStart(); }
		}
		else if (Frame.Phase == EOneBrahamMatchPhase::Warmup)
		{
			Eyebrow = TEXT("INDUCTION");
		}

		// "Sprint  -  hold SHIFT while moving": the instruction, then how - where
		// a word in capitals is a key and gets a cap, and the rest stays words.
		FString How;
		int32 Split = INDEX_NONE;
		if (Body.FindLastChar(TEXT('-'), Split) && Split > 0 && Body.Mid(Split - 1, 1) == TEXT(" "))
		{
			How = Body.Mid(Split + 1).TrimStartAndEnd();
			Body = Body.Left(Split).TrimEnd();
		}

		const FSlateFontInfo BodyFont = HUDFont(EOneBrahamHUDType::Label, 16.f, 20, C.bHeavyOutline);
		const FSlateFontInfo HowFont = HUDFont(EOneBrahamHUDType::Hint, 14.f, 0, C.bHeavyOutline);
		const FSlateFontInfo CapFont = Font(EOneBrahamHUDType::Telemetry, 12.f);
		const FVector2D BodySize = Measure(Body, BodyFont);

		struct FToken { FString Text; bool bKey = false; };
		TArray<FToken> Tokens;
		TArray<FString> Words;
		How.ParseIntoArray(Words, TEXT(" "), true);
		for (const FString& Word : Words)
		{
			// FString's == ignores case: a key is a word that is already in capitals.
			const bool bKey = Word.Equals(Word.ToUpper(), ESearchCase::CaseSensitive) && Word.Len() <= 12 && !Word.Equals(TEXT("·")) && !Word.Equals(TEXT("-"));
			if (!bKey && Tokens.Num() > 0 && !Tokens.Last().bKey)
			{
				Tokens.Last().Text += TEXT(" ") + Word;
			}
			else
			{
				Tokens.Add({ Word, bKey });
			}
		}

		float HowW = 0.f;
		for (const FToken& Token : Tokens)
		{
			HowW += (Token.bKey ? FMath::Max(18.f, Measure(Token.Text, CapFont).X + 10.f) : Measure(Token.Text, HowFont).X) + 6.f;
		}
		// 16 px between the instruction and its keys: at 720p the drawn words
		// run a pixel or two past their measure, and 10 put the last letter
		// against the first cap.
		const float Gutter = 16.f;
		const float Total = BodySize.X + (HowW > 0.f ? HowW + Gutter : 0.f);
		const float X0 = -Total * 0.5f;

		Plate(S, FVector2D(X0 - 14.f, Eyebrow.IsEmpty() ? -6.f : -22.f), FVector2D(Total + 28.f, BodySize.Y + (Eyebrow.IsEmpty() ? 12.f : 28.f)), 0.72f, 4.f);
		if (!Eyebrow.IsEmpty())
		{
			S.Text(FVector2D(X0, -17.f), Eyebrow, Font(EOneBrahamHUDType::Eyebrow, 11.f, 200), SignalBlue());
		}
		S.Text(FVector2D(X0, 0.f), Body, BodyFont, White());
		float KX = X0 + BodySize.X + Gutter;
		for (const FToken& Token : Tokens)
		{
			if (Token.bKey)
			{
				KX += KeyCap(S, FVector2D(KX, (BodySize.Y - 18.f) * 0.5f), Token.Text, White()) + 6.f;
			}
			else
			{
				const FVector2D Size = Measure(Token.Text, HowFont);
				S.Text(FVector2D(KX, (BodySize.Y - Size.Y) * 0.5f), Token.Text, HowFont, WithAlpha(LightGrey(), 0.8f));
				KX += Size.X + 6.f;
			}
		}
	}
}

// ==========================================================================
//  13. Downed
// ==========================================================================
void UOneBrahamHUDWidget::PaintDowned(FOneBrahamPainter& P, const FPaintContext& C) const
{
	if (Data.LifeState != EOneBrahamLifeState::Downed)
	{
		return;
	}

	const float In = FMath::Clamp(Since(C.Now, DownedAt) / 0.3f, 0.f, 1.f);
	FOneBrahamPainter D = P.Faded(In);

	// A red edge, 10%: pain, never a wash over the room.
	const float Edge = C.Screen.Y * 0.22f;
	const FLinearColor Red = WithAlpha(EmergencyRed(), 0.10f);
	const FLinearColor None = WithAlpha(EmergencyRed(), 0.f);
	D.Gradient(FVector2D::ZeroVector, FVector2D(C.Screen.X, Edge), Red, None, false);
	D.Gradient(FVector2D(0.f, C.Screen.Y - Edge), FVector2D(C.Screen.X, Edge), None, Red, false);
	D.Gradient(FVector2D::ZeroVector, FVector2D(Edge, C.Screen.Y), Red, None, true);
	D.Gradient(FVector2D(C.Screen.X - Edge, 0.f), FVector2D(Edge, C.Screen.Y), None, Red, true);

	// A medallion above the crosshair band: the bleed-out in the LED face
	// inside a ring, YOU'RE DOWN over it, the call for help under it. The ring
	// fills blue while a colleague's hands are on you.
	FOneBrahamPainter Q = D.Child(FVector2D(C.Screen.X * 0.5f, C.Screen.Y * 0.33f), C.Scale);
	const float R = 92.f;

	// Dense enough to read on sunlit concrete as well as in the dark stairs.
	// An LED panel is black, not navy: red dots on navy were 3:1, on black they
	// are lit.
	Q.Circle(FVector2D::ZeroVector, R, WithAlpha(OLEDBlack(), 0.86f));
	Q.Ring(FVector2D::ZeroVector, R, WithAlpha(EmergencyRed(), 0.35f), 2.f);
	if (Data.ReviveProgress01 > 0.f)
	{
		Q.Arc(FVector2D::ZeroVector, R, 0.f, 360.f * FMath::Clamp(Data.ReviveProgress01, 0.f, 1.f), SignalBlue(), 5.f);
	}
	else
	{
		// The bleed-out drains the ring the other way, in red.
		const float Left = FMath::Clamp(Data.BleedOutRemaining / FMath::Max(1.f, OneBrahamConstants::BleedOutSeconds), 0.f, 1.f);
		Q.Arc(FVector2D::ZeroVector, R, 0.f, 360.f * Left, EmergencyRed(), 3.f);
	}

	const FString Bleed = Clock(Data.BleedOutRemaining, false);
	// No outline on the dot face: a black ring round every dot turns the digits to gravel.
	const FSlateFontInfo LED = Font(EOneBrahamHUDType::LED, 60.f, 0);
	const FVector2D BleedSize = Measure(Bleed, LED);
	Q.Text(FVector2D(0.f, -BleedSize.Y * 0.5f - 4.f), Bleed, LED, EmergencyRed(), EOneBrahamTextAnchor::Centre);
	Q.Text(FVector2D(0.f, BleedSize.Y * 0.5f - 2.f), Data.ReviveProgress01 > 0.f ? TEXT("BEING REVIVED") : TEXT("BLEEDING OUT"),
		Font(EOneBrahamHUDType::Telemetry, 11.f, 200), Data.ReviveProgress01 > 0.f ? White() : WithAlpha(LightGrey(), 0.8f), EOneBrahamTextAnchor::Centre);

	const FSlateFontInfo TitleFont = OutlinedFont(EOneBrahamHUDType::Title, 44.f, 40, 2.f, FLinearColor(0.f, 0.f, 0.f, 0.5f));
	const FVector2D Title = Measure(TEXT("YOU'RE DOWN"), TitleFont);
	Q.Text(FVector2D(0.f, -R - 18.f - Title.Y), TEXT("YOU'RE DOWN"), TitleFont, White(), EOneBrahamTextAnchor::Centre);

	if (Data.ReviveProgress01 <= 0.f)
	{
		const float PromptY = R + 22.f;
		const FSlateFontInfo PromptFont = HUDFont(EOneBrahamHUDType::Label, 16.f, 20, false);
		const FString Words = TEXT("Hold  ·  Call for help");
		const float W = 26.f + Measure(Words, PromptFont).X;
		Plate(Q, FVector2D(-W * 0.5f - 12.f, PromptY - 6.f), FVector2D(W + 24.f, 30.f), 0.6f, 4.f);
		KeyCap(Q, FVector2D(-W * 0.5f, PromptY), TEXT("F"), White());
		Q.Text(FVector2D(-W * 0.5f + 26.f, PromptY), Words, PromptFont, White());
	}
}

// ==========================================================================
//  14. Subtitles
// ==========================================================================
void UOneBrahamHUDWidget::PaintSubtitles(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	const float T = Since(C.Now, SubtitleAt);
	if (!Data.bShowSubtitles || SubtitleShown.IsEmpty() || T < 0.f || T > SubtitleSeconds)
	{
		return;
	}

	const float Scale = FMath::Clamp(Frame.SubtitleScale, 0.75f, 1.75f) * C.Scale;
	FOneBrahamPainter P = Root.Child(FVector2D(C.Screen.X * 0.5f, C.Screen.Y * 0.84f), Scale)
		.Faded(FMath::Clamp(T / 0.12f, 0.f, 1.f) * (1.f - FMath::Clamp((T - SubtitleSeconds + 0.25f) / 0.25f, 0.f, 1.f)));

	const bool bBuilding = SubtitleSpeaker.IsEmpty();
	const FString Speaker = bBuilding ? TEXT("THE BUILDING") : SubtitleSpeaker.ToUpper();
	bool bSquad = false;
	for (const FOneBrahamSquadRowView& Row : Frame.Squad)
	{
		bSquad |= Row.Name.Equals(SubtitleSpeaker, ESearchCase::IgnoreCase);
	}
	const FLinearColor SpeakerInk = bBuilding ? SafetyYellow() : (bSquad ? SignalBlue() : LightGrey());

	const FSlateFontInfo SpeakerFont = Font(EOneBrahamHUDType::Label, 15.f, 120);
	const FSlateFontInfo BodyFont = Font(EOneBrahamHUDType::Body, 20.f);
	const float MaxW = C.Screen.X * 0.6f / Scale;
	const float SpeakerW = Measure(Speaker, SpeakerFont).X + 12.f;
	const TArray<FString> Lines = Wrap(SubtitleShown, BodyFont, MaxW - SpeakerW, 2);
	const float LineH = Measure(TEXT("Ag"), BodyFont).Y;

	float Widest = 0.f;
	for (const FString& Line : Lines)
	{
		Widest = FMath::Max(Widest, Measure(Line, BodyFont).X);
	}
	const float Total = SpeakerW + Widest;
	const float X0 = -Total * 0.5f;
	P.RoundRect(FVector2D(X0 - 14.f, -8.f), FVector2D(Total + 28.f, Lines.Num() * LineH + 16.f), FLinearColor(0.f, 0.f, 0.f, 0.55f), 4.f);
	P.Text(FVector2D(X0, (LineH - Measure(Speaker, SpeakerFont).Y) * 0.5f), Speaker, SpeakerFont, SpeakerInk);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		P.Text(FVector2D(X0 + SpeakerW, Index * LineH), Lines[Index], BodyFont, White());
	}
}

// ==========================================================================
//  The banner: short notices from the server and the game
// ==========================================================================
void UOneBrahamHUDWidget::PaintBanner(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	const float T = Since(C.Now, BannerAt);
	if (BannerShown.IsEmpty() || T < 0.f || T > BannerSeconds)
	{
		return;
	}

	// A notice, not a headline: the stage cards and the PA line do the shouting.
	// It sits under the crosshair band, centred, as a labelled tag.
	const float Fade = FMath::Clamp(T / 0.14f, 0.f, 1.f) * (1.f - FMath::Clamp((T - BannerSeconds + 0.3f) / 0.3f, 0.f, 1.f));
	FOneBrahamPainter P = Root.Child(FVector2D(C.Screen.X * 0.5f, C.Screen.Y * 0.36f), C.Scale).Faded(Fade);
	const FSlateFontInfo BannerFont = HUDFont(EOneBrahamHUDType::Label, 18.f, 120, C.bHeavyOutline);
	const FString Text = BannerShown.ToUpper();
	const FVector2D Size = Measure(Text, BannerFont);
	const FLinearColor Ink = BannerColour.A > 0.f ? FLinearColor(BannerColour.R, BannerColour.G, BannerColour.B, 1.f) : White();

	Plate(P, FVector2D(-Size.X * 0.5f - 16.f, -6.f), FVector2D(Size.X + 32.f, Size.Y + 12.f), 0.72f, 3.f);
	P.Rect(FVector2D(-Size.X * 0.5f - 16.f, -6.f), FVector2D(4.f, Size.Y + 12.f), Ink);
	P.Text(FVector2D(0.f, 0.f), Text, BannerFont, Ink, EOneBrahamTextAnchor::Centre);
}

// ==========================================================================
//  15. The stage-wide edge: the Purge front
// ==========================================================================
void UOneBrahamHUDWidget::PaintStageVignette(FOneBrahamPainter& P, const FPaintContext& C) const
{
	if (C.Band != 2 || !Frame.Building.IsLocalInDanger())
	{
		return;
	}

	const float Edge = C.Screen.Y * 0.18f;
	const FLinearColor Red = WithAlpha(EmergencyRed(), 0.06f);
	const FLinearColor None = WithAlpha(EmergencyRed(), 0.f);
	P.Gradient(FVector2D::ZeroVector, FVector2D(C.Screen.X, Edge), Red, None, false);
	P.Gradient(FVector2D(0.f, C.Screen.Y - Edge), FVector2D(C.Screen.X, Edge), None, Red, false);
	P.Gradient(FVector2D::ZeroVector, FVector2D(Edge, C.Screen.Y), Red, None, true);
	P.Gradient(FVector2D(C.Screen.X - Edge, 0.f), FVector2D(Edge, C.Screen.Y), None, Red, true);
}

// ==========================================================================
//  The accident book: the death screen
// ==========================================================================
void UOneBrahamHUDWidget::PaintAccidentBook(FOneBrahamPainter& P, const FPaintContext& C) const
{
	const float T = Since(C.Now, DeathAt);

	// 0 - 0.4 s: the world holds still under a navy dim; then the book slides in.
	const float Dim = FMath::Clamp(T / 0.4f, 0.f, 1.f);
	P.Rect(FVector2D::ZeroVector, C.Screen, WithAlpha(Navy(), 0.6f * Dim));

	const float Slide = C.bReduceMotion ? (T > 0.4f ? 1.f : 0.f) : Ramp(C.Now, DeathAt + 0.4, 0.26f, EOneBrahamEase::Ribbon);
	if (Slide <= 0.f)
	{
		return;
	}

	const float PanelW = C.Screen.X * 0.38f;
	const float PanelX = C.Screen.X - PanelW * Slide;
	P.Rect(FVector2D(PanelX, 0.f), FVector2D(PanelW, C.Screen.Y), Paper());
	// The binding: a navy strip down the book's spine.
	P.Rect(FVector2D(PanelX, 0.f), FVector2D(6.f, C.Screen.Y), Navy());

	const float Pad = 56.f;
	FOneBrahamPainter B = P.Child(FVector2D(PanelX + Pad, C.Screen.Y * 0.09f), C.Scale);
	const float W = (PanelW - Pad * 2.f) / C.Scale;
	const FLinearColor Ink = Navy();
	const FLinearColor Rule = WithAlpha(Navy(), 0.12f);
	const FLinearColor Label = WithAlpha(Navy(), 0.55f);

	FString Cause;
	TArray<FRecapRow> Rows;
	FString Remaining;
	ParseDeathText(DeathText, Cause, Rows, Remaining);

	float Y = 0.f;
	B.Text(FVector2D(0.f, Y), FString::Printf(TEXT("ACCIDENT BOOK  ·  ENTRY %04d"), DeathEntryNumber), Font(EOneBrahamHUDType::TelemetryLight, 12.f, 200), Ink);
	B.Text(FVector2D(W, Y), TEXT("ONE BRAHAM  ·  FACILITIES"), Font(EOneBrahamHUDType::TelemetryLight, 12.f, 200), Label, EOneBrahamTextAnchor::Right);
	Y += 26.f;
	B.Rect(FVector2D(0.f, Y), FVector2D(W, 2.f), Ink);
	Y += 22.f;

	const FSlateFontInfo FieldLabel = Font(EOneBrahamHUDType::Eyebrow, 11.f, 180);
	const FSlateFontInfo FieldValue = Font(EOneBrahamHUDType::Label, 18.f, 20);
	auto Field = [&](const TCHAR* Name, const FString& Value)
	{
		B.Text(FVector2D(0.f, Y), Name, FieldLabel, Label);
		const TArray<FString> Lines = Wrap(Value.IsEmpty() ? FString(TEXT("—")) : Value, FieldValue, W, 2);
		float LY = Y + 16.f;
		for (const FString& Line : Lines)
		{
			B.Text(FVector2D(0.f, LY), Line, FieldValue, Ink);
			LY += 24.f;
		}
		Y = LY + 8.f;
		B.Rect(FVector2D(0.f, Y), FVector2D(W, 1.f), Rule);
		Y += 14.f;
	};

	Field(TEXT("NAME"), Frame.PlayerName);
	Field(TEXT("TIME"), DeathTime.IsEmpty() ? Frame.DuskClock : DeathTime);
	Field(TEXT("LOCATION"), DeathLocation);
	Field(TEXT("NATURE OF INCIDENT"), Cause);

	// Witnesses: anyone else in the recap who hit you.
	TArray<FString> Witnesses;
	for (int32 Index = 1; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].bParsed)
		{
			Witnesses.Add(Rows[Index].Attacker);
		}
	}
	Field(TEXT("WITNESSES"), Witnesses.Num() > 0 ? FString::Join(Witnesses, TEXT(", ")) : FString(TEXT("None recorded")));

	// --- the recap table -------------------------------------------------------------------------
	Y += 6.f;
	const FSlateFontInfo Table = Font(EOneBrahamHUDType::TelemetryLight, 13.f);
	const FSlateFontInfo TableHead = Font(EOneBrahamHUDType::Telemetry, 10.f, 160);
	const float Cols[5] = { 0.f, W * 0.36f, W * 0.48f, W * 0.62f, W };
	B.Text(FVector2D(Cols[0], Y), TEXT("WHO"), TableHead, Label);
	B.Text(FVector2D(Cols[1], Y), TEXT("HITS"), TableHead, Label);
	B.Text(FVector2D(Cols[2], Y), TEXT("DMG"), TableHead, Label);
	B.Text(FVector2D(Cols[3], Y), TEXT("WHERE"), TableHead, Label);
	B.Text(FVector2D(Cols[4], Y), TEXT("RANGE"), TableHead, Label, EOneBrahamTextAnchor::Right);
	Y += 18.f;
	for (const FRecapRow& Row : Rows)
	{
		if (Row.bParsed)
		{
			B.PushClip(FVector2D(0.f, Y - 2.f), FVector2D(Cols[1] - 8.f, 22.f));
			B.Text(FVector2D(Cols[0], Y), Row.Attacker, Table, Ink);
			B.PopClip();
			B.Text(FVector2D(Cols[1], Y), Row.Hits, Table, Ink);
			B.Text(FVector2D(Cols[2], Y), Row.Damage, Table, Ink);
			B.PushClip(FVector2D(Cols[3], Y - 2.f), FVector2D(Cols[4] - Cols[3] - 52.f, 22.f));
			B.Text(FVector2D(Cols[3], Y), Row.Locations, Table, Ink);
			B.PopClip();
			B.Text(FVector2D(Cols[4], Y), Row.Distance, Table, Ink, EOneBrahamTextAnchor::Right);
		}
		else
		{
			B.Text(FVector2D(0.f, Y), Row.Raw, Table, Ink);
		}
		Y += 20.f;
		B.Rect(FVector2D(0.f, Y - 3.f), FVector2D(W, 1.f), WithAlpha(Navy(), 0.06f));
	}
	if (!Remaining.IsEmpty())
	{
		Y += 6.f;
		B.Text(FVector2D(0.f, Y), FString::Printf(TEXT("Remaining: %s"), *Remaining), Font(EOneBrahamHUDType::Telemetry, 13.f), Ink);
		Y += 22.f;
	}

	// --- the stamp, at 1.0 s: SIGNED OUT --------------------------------------------------------
	const float StampT = T - 1.f;
	if (StampT >= 0.f)
	{
		const float Land = FMath::Clamp(StampT / 0.09f, 0.f, 1.f);
		const float StampScale = FMath::Lerp(1.4f, 1.f, Land);
		const float Shake = !C.bReduceMotion && StampT < 0.15f && Land >= 1.f ? FMath::Sin(StampT * 120.f) * 2.f : 0.f;
		const FSlateFontInfo StampFont = Font(EOneBrahamHUDType::Title, 40.f, 60);
		const FVector2D TextSize = Measure(TEXT("SIGNED OUT"), StampFont);
		const FVector2D Box = TextSize + FVector2D(28.f, 10.f);
		const FVector2D StampCentre(W * 0.62f + Shake, Y + 70.f);

		const FGeometry Stamped = B.Geometry.MakeChild(Box, FSlateLayoutTransform(StampCentre - Box * 0.5f),
			FSlateRenderTransform(FScale2D(StampScale)).Concatenate(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(-6.f)))),
			FVector2D(0.5f, 0.5f));
		FOneBrahamPainter S = B.Faded(FMath::Clamp(Land * 1.5f, 0.f, 1.f) * 0.92f);
		S.Geometry = Stamped;
		S.Outline(FVector2D::ZeroVector, Box, EmergencyRed(), 3.f, 3.f);
		S.Text(FVector2D(Box.X * 0.5f, (Box.Y - TextSize.Y) * 0.5f), TEXT("SIGNED OUT"), StampFont, EmergencyRed(), EOneBrahamTextAnchor::Centre);
	}

	// --- the rest of the form: what facilities did about it ----------------------------------------
	// A real accident book page does not end at the stamp; it has the boxes
	// nobody fills in properly. They also stop the lower half being blank paper.
	{
		float LY = Y + 150.f;
		const float PageBottom = (C.Screen.Y * 0.82f) / C.Scale;
		if (LY + 120.f < PageBottom)
		{
			FOneBrahamPainter Form = B.Faded(FMath::Clamp((T - 1.3f) / 0.3f, 0.f, 1.f));
			Form.Text(FVector2D(0.f, LY), TEXT("ACTION TAKEN"), FieldLabel, Label);
			LY += 18.f;
			Form.Text(FVector2D(0.f, LY), TEXT("None required. The floor has been cleaned."), Font(EOneBrahamHUDType::Aside, 22.f), WithAlpha(Ink, 0.85f));
			LY += 40.f;
			Form.Rect(FVector2D(0.f, LY), FVector2D(W, 1.f), Rule);
			LY += 18.f;
			Form.Text(FVector2D(0.f, LY), TEXT("SIGNED  (FACILITIES)"), FieldLabel, Label);
			Form.Text(FVector2D(W * 0.62f, LY), TEXT("COUNTERSIGNED"), FieldLabel, Label);
			LY += 46.f;
			// Two signature lines; the first is signed, after a fashion.
			Form.Rect(FVector2D(0.f, LY), FVector2D(W * 0.5f, 1.f), WithAlpha(Ink, 0.45f));
			Form.Rect(FVector2D(W * 0.62f, LY), FVector2D(W * 0.38f, 1.f), WithAlpha(Ink, 0.45f));
			Form.Text(FVector2D(4.f, LY - 30.f), TEXT("The Building"), Font(EOneBrahamHUDType::Aside, 26.f), WithAlpha(SignalBlue(), 0.9f));
		}
	}

	// --- the footer --------------------------------------------------------------------------------
	FOneBrahamPainter F = P.Child(FVector2D(PanelX + Pad, C.Screen.Y * 0.91f), C.Scale);
	float FX = 0.f;
	FX += KeyCap(F, FVector2D(FX, 0.f), TEXT("SPACE"), Navy()) + 8.f;
	FX += F.Text(FVector2D(FX, 1.f), TEXT("Spectate"), Font(EOneBrahamHUDType::Hint, 13.f), Ink).X + 24.f;
	for (const float Dir : { -1.f, 1.f })
	{
		const float CapW = KeyCap(F, FVector2D(FX, 0.f), TEXT(" "), Navy());
		const FVector2D Mid(FX + CapW * 0.5f, 9.f);
		F.Triangle(Mid + FVector2D(-3.f * Dir, -4.f), Mid + FVector2D(-3.f * Dir, 4.f), Mid + FVector2D(3.f * Dir, 0.f), White());
		FX += CapW + 4.f;
	}
	FX += 4.f;
	F.Text(FVector2D(FX, 1.f), TEXT("Next colleague"), Font(EOneBrahamHUDType::Hint, 13.f), Ink);
}

// ==========================================================================
//  Spectating: the lower third
// ==========================================================================
void UOneBrahamHUDWidget::PaintSpectating(FOneBrahamPainter& Root, const FPaintContext& C) const
{
	FOneBrahamPainter P = Root.Child(FVector2D(C.SafeL, C.SafeB - 30.f * C.Scale), C.Scale);
	const FString Line = Frame.SpectatingFloor != INDEX_NONE
		? FString::Printf(TEXT("WATCHING  —  %s  ·  L%02d"), *Frame.SpectatingName.ToUpper(), Frame.SpectatingFloor)
		: FString::Printf(TEXT("WATCHING  —  %s"), *Frame.SpectatingName.ToUpper());
	const FSlateFontInfo LineFont = HUDFont(EOneBrahamHUDType::Telemetry, 12.f, 120, false);
	const FVector2D Size = Measure(Line, LineFont);
	Plate(P, FVector2D(-12.f, -6.f), FVector2D(Size.X + 24.f, Size.Y + 12.f), 0.72f, 3.f);
	P.Rect(FVector2D(-12.f, -6.f), FVector2D(3.f, Size.Y + 12.f), SignalBlue());
	P.Text(FVector2D::ZeroVector, Line, LineFont, White());
}
