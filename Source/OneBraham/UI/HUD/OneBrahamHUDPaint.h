// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDPaint.h
// ---------------------------------------------------------------------------
// The HUD's drawing kit: colour tokens, type tokens, and a small painter that
// turns "a 2 px rule from here to there" into Slate draw elements.
//
// WHY A PAINTER
//   Every in-match element (the floor plate, the fire panel, the staff pass,
//   the sign-out log) is drawn in NativePaint rather than assembled from UMG
//   widgets. Paint is the one callback a visible widget is guaranteed every
//   frame (Docs/08), it is crisp at any resolution, and it lets one element
//   animate a strikethrough or a split-flap without a widget per glyph. The
//   cost is verbosity, which this file absorbs.
//
// COLOUR
//   The brand floats in DefaultEngine.ini are sRGB values stored in
//   FLinearColor fields; read as linear they wash out (Navy turns into the
//   #22476A slab of the first menu). Every token here is authored as the hex
//   the brand book gives and converted once with FromSRGBColor.
//
//   Colour-blind modes remap only the danger hue (Deuteranopia/Protanopia red
//   -> #FF6B00) and, for Tritanopia, the warning hue (yellow -> white with
//   hazard stripes, which the elements draw themselves). Shapes never change:
//   every state also has one (hatching, a cross, a strikethrough, a chevron).
//
// TYPE
//   Four open families in fixed roles - Archivo for words, IBM Plex Mono for
//   every number that changes, Doto for things the building shows on LED
//   panels (28 px and up only), Instrument Serif Italic for the building's
//   own voice. Sizes are given in px at 1080p, as the spec gives them.
//   Until the brand composites are imported the engine's Roboto and Droid
//   Sans Mono stand in, by role, so a clean checkout still draws every glyph.
//
// LAYERS
//   Slate may batch elements on one layer out of submission order, so the
//   painter advances the layer on every call. A HUD is a few hundred
//   elements; correctness is worth more than the batches.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "Styling/SlateBrush.h"
#include "Core/OneBrahamTypes.h"
#include "UI/OneBrahamUIStyle.h"

class FSlateWindowElementList;

/** The type roles: the design system's own tokens (UOneBrahamUIStyle), so a
 *  face named on the HUD is the face named in the menus. */
using EOneBrahamHUDType = EOneBrahamType;

/** Horizontal anchor for text drawn at a point. */
enum class EOneBrahamTextAnchor : uint8
{
	Left,
	Centre,
	Right
};

namespace OneBrahamHUDStyle
{
	// --- colour ---------------------------------------------------------------
	/** "#RRGGBB" (sRGB) to a linear colour. */
	ONEBRAHAM_API FLinearColor Hex(const TCHAR* InHex, float Alpha = 1.f);

	inline FLinearColor WithAlpha(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Alpha);
	}

	/** Multiplies the alpha. */
	inline FLinearColor Fade(const FLinearColor& Colour, float Factor)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * Factor);
	}

	// Brand.
	ONEBRAHAM_API FLinearColor Primary();		// #0070C0
	ONEBRAHAM_API FLinearColor Navy();			// #041025
	ONEBRAHAM_API FLinearColor SignalBlue();	// #1485FF  "yours", focus
	ONEBRAHAM_API FLinearColor SafetyYellow();	// #FFCD00  (Tritanopia: #FFFFFF)
	ONEBRAHAM_API FLinearColor EmergencyRed();	// #DD1E1F  (Deut./Prot.: #FF6B00)
	ONEBRAHAM_API FLinearColor LightGrey();		// #E0E5EB
	ONEBRAHAM_API FLinearColor White();

	// Building.
	ONEBRAHAM_API FLinearColor OLEDBlack();		// #000000
	ONEBRAHAM_API FLinearColor BaffleBlack();	// #0B0C0F
	ONEBRAHAM_API FLinearColor SignageWhite();	// #F4F6F8
	ONEBRAHAM_API FLinearColor Paper();			// #E9ECEF
	ONEBRAHAM_API FLinearColor RibbonPurple();	// #6A1BFF
	ONEBRAHAM_API FLinearColor RibbonBlue();	// #1485FF
	ONEBRAHAM_API FLinearColor LockdownFieldDark();	// #B37400
	ONEBRAHAM_API FLinearColor PurgeFieldDark();	// #5A0A0C
	ONEBRAHAM_API FLinearColor LCDField();		// #12160F
	ONEBRAHAM_API FLinearColor LCDInk();		// #D8F27A
	ONEBRAHAM_API FLinearColor FireHousing();	// #8F1418

	/** The un-remapped safety yellow, for hazard stripes (which are the cue). */
	ONEBRAHAM_API FLinearColor HazardYellow();

	/** True when the warning hue is remapped to white (Tritanopia). */
	ONEBRAHAM_API bool IsWarningRemapped();

	/** The stage's accent: blue in the calm, yellow in Lockdown, red in Purge. */
	ONEBRAHAM_API FLinearColor StageAccent(EOneBrahamLockdownStage Stage);

	/** The neutral hairline for a stage: grey 20%, yellow 25%, red 30%. */
	ONEBRAHAM_API FLinearColor StageHairline(EOneBrahamLockdownStage Stage);

	/** Spec stage for an engine stage: Normal is the calm ("PreLockdown" in the
	 *  design), PreLockdown and Lockdown are both the yellow Lockdown, Purge is
	 *  Purge. Returned as 0, 1, 2. */
	ONEBRAHAM_API int32 StageBand(EOneBrahamLockdownStage Stage);

	/** Colour mix in linear space. */
	inline FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FMath::Lerp(A, B, FMath::Clamp(T, 0.f, 1.f));
	}

	// --- type -----------------------------------------------------------------
	/**
	 * A font for a role at a pixel size (at 1080p; the geometry scales it).
	 * Tracking is in thousandths of an em, as FSlateFontInfo::LetterSpacing
	 * takes it. OutlinePx 0 means no outline.
	 */
	ONEBRAHAM_API FSlateFontInfo Font(EOneBrahamHUDType Type, float Px, int32 Tracking = 0,
		float OutlinePx = 0.f, const FLinearColor& OutlineColour = FLinearColor(0.f, 0.f, 0.f, 0.6f));

	/** Font() with an explicit outline. A name of its own so that a 5-argument
	 *  call is never mistaken for UOneBrahamUIStyle::Font by the arity check. */
	inline FSlateFontInfo OutlinedFont(EOneBrahamHUDType Type, float Px, int32 Tracking, float OutlinePx, const FLinearColor& OutlineColour)
	{
		return Font(Type, Px, Tracking, OutlinePx, OutlineColour);
	}

	/** The standard HUD text treatment: 1 px outline, black at 60% (1.5 px in the Purge). */
	ONEBRAHAM_API FSlateFontInfo HUDFont(EOneBrahamHUDType Type, float Px, int32 Tracking = 0, bool bHeavyOutline = false);

	/** True once the brand faces are loaded (the type looks right). */
	ONEBRAHAM_API bool HasBrandFonts();

	/** The string as it is drawn in the font: word spaces opened up when the
	 *  tracking is wide enough to swallow them. */
	ONEBRAHAM_API FString Tracked(const FString& Text, const FSlateFontInfo& Font);

	/** Measures a string in the font, in local units. */
	ONEBRAHAM_API FVector2D Measure(const FString& Text, const FSlateFontInfo& Font);

	// --- brushes ---------------------------------------------------------------
	/** A rounded rectangle brush; the tint is applied at draw time. */
	ONEBRAHAM_API const FSlateBrush* RoundedBrush(float Radius);

	/** A rounded outline brush (no fill). */
	ONEBRAHAM_API const FSlateBrush* OutlineBrush(float Radius, float Thickness);

	/** A plain white box. */
	ONEBRAHAM_API const FSlateBrush* WhiteBrush();
}

/**
 * Draws into one geometry. Every coordinate is local to that geometry, so an
 * element drawn into a scaled child geometry (HUD Scale) scales as a whole -
 * text, rules and all - about the anchor it was placed from.
 */
struct ONEBRAHAM_API FOneBrahamPainter
{
	/** InLayer is the caller's counter: every painter made from this one
	 *  advances the same number, so the caller knows the top layer used. */
	FOneBrahamPainter(FSlateWindowElementList& InOut, const FGeometry& InGeometry, int32& InLayer, float InOpacity = 1.f)
		: Out(&InOut), Geometry(InGeometry), Layer(&InLayer), Opacity(InOpacity)
	{
	}

	FSlateWindowElementList* Out = nullptr;
	FGeometry Geometry;
	int32* Layer = nullptr;
	/** Multiplies every alpha this painter draws. */
	float Opacity = 1.f;

	/** The same painter at another anchor and scale (shares the layer counter). */
	FOneBrahamPainter Child(const FVector2D& Offset, float Scale = 1.f) const;

	/** The same painter with its opacity multiplied. */
	FOneBrahamPainter Faded(float Factor) const
	{
		FOneBrahamPainter Copy = *this;
		Copy.Opacity *= Factor;
		return Copy;
	}

	/** Advances and returns the layer for the next element. */
	int32 Next() { return ++(*Layer); }

	/** The geometry's local size. */
	FVector2D Size() const { return Geometry.GetLocalSize(); }

	// --- shapes ------------------------------------------------------------------
	void Rect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour);
	void RoundRect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour, float Radius);
	void Outline(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour, float Thickness = 1.f, float Radius = 0.f);
	void Segment(const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Thickness = 1.f);
	void Polyline(const TArray<FVector2D>& Points, const FLinearColor& Colour, float Thickness = 1.f);
	void ColouredPolyline(const TArray<FVector2D>& Points, const TArray<FLinearColor>& Colours, float Thickness = 1.f);
	void DashedLine(const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Thickness, float Dash, float Gap, float Offset = 0.f);

	/** A horizontal (or vertical) two-stop gradient. */
	void Gradient(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& From, const FLinearColor& To,
		bool bHorizontal = true, float Radius = 0.f);

	/** 45-degree hatching clipped to a rectangle. */
	void Hatch(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour, float Spacing = 4.f,
		float Thickness = 1.f, bool bRising = true);

	/** Diagonal hazard stripes in two colours (the sealing border). */
	void HazardStripes(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& A, const FLinearColor& B,
		float Period = 8.f, float Phase = 0.f);

	/** A horizontal run of dots (the leader between a label and its value). */
	void Dots(float X0, float X1, float Y, const FLinearColor& Colour, float Pitch = 4.f, float DotSize = 1.f, float Phase = 0.f);

	void Circle(const FVector2D& Centre, float Radius, const FLinearColor& Colour);
	void Ring(const FVector2D& Centre, float Radius, const FLinearColor& Colour, float Thickness = 1.f);

	/** An arc of a circle, degrees clockwise from 12 o'clock. ColourEnd lets
	 *  the arc fade along its length (the damage arc's tail). */
	void Arc(const FVector2D& Centre, float Radius, float StartDeg, float SweepDeg, const FLinearColor& Colour,
		float Thickness, const FLinearColor* ColourEnd = nullptr);

	/** A filled triangle (drawn as a thick closed polyline: no texture needed). */
	void Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Colour);

	/** A chevron pointing in Dir (unit), Size px across. */
	void Chevron(const FVector2D& Centre, const FVector2D& Dir, float Size, const FLinearColor& Colour, float Thickness = 2.f);

	/** A diamond (rotated square). */
	void Diamond(const FVector2D& Centre, float Size, const FLinearColor& Colour, bool bFilled = true);

	/** A cross (x) glyph. */
	void Cross(const FVector2D& Centre, float Size, const FLinearColor& Colour, float Thickness = 1.5f);

	// --- text ----------------------------------------------------------------------
	/** Draws a string with its top at Pos.Y, anchored horizontally. Returns its size. */
	FVector2D Text(const FVector2D& Pos, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour,
		EOneBrahamTextAnchor Anchor = EOneBrahamTextAnchor::Left);

	FVector2D Text(const FVector2D& Pos, const FText& String, const FSlateFontInfo& Font, const FLinearColor& Colour,
		EOneBrahamTextAnchor Anchor = EOneBrahamTextAnchor::Left)
	{
		return Text(Pos, String.ToString(), Font, Colour, Anchor);
	}

	/** Text revealed left-to-right up to Reveal01 of its width (a wipe). */
	FVector2D TextWipe(const FVector2D& Pos, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour,
		float Reveal01, EOneBrahamTextAnchor Anchor = EOneBrahamTextAnchor::Left);

	/** Pushes a clip rectangle in local coordinates. Pair with PopClip. */
	void PushClip(const FVector2D& Pos, const FVector2D& Size);
	void PopClip();

private:
	FLinearColor Tint(const FLinearColor& Colour) const
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * Opacity);
	}

	FPaintGeometry Paint(const FVector2D& Pos, const FVector2D& Size) const;
};
