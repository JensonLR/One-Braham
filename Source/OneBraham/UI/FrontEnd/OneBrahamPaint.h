// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPaint.h
// ---------------------------------------------------------------------------
// The front end's drawing kit: text, boxes, rules, gradients and dots laid
// straight into a widget's paint, in its local units (1080p pixels once the
// DPI curve has scaled them). Every painted control on the menu, the cinema
// overlay and the lift loading page draws through these, so a hairline is the
// same hairline everywhere and nobody re-derives a paint geometry by hand.
//
// WHY PAINT AND NOT WIDGETS
//   The design is mostly light, rule and type over a moving picture: a
//   directory board with dotted leaders, a section drawing that strokes in
//   floor by floor, a letterbox that eases. Built out of UImages and text
//   blocks every one of those is a stack of boxes animated from outside;
//   painted, it is a function of time (Docs/08: motion lives in paint, driven
//   by the clock, never by a tick nobody calls).

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "UI/OneBrahamUIStyle.h"

namespace OneBrahamPaint
{
	/** The kinds of element a painter lays down (each its own Slate batch). */
	enum class EPaintKind : uint8 { None, Box, Rounded, Lines, Gradient, Text, Blur };

	/**
	 * Everything a painter needs for one call: where, on which layer, into what.
	 *
	 * DRAW ORDER. Slate batches a layer's elements by kind (plain boxes, rounded
	 * boxes, gradients, text in its atlas) and does not keep the order they
	 * were added in between those batches: a scrim gradient laid first greyed
	 * the eyebrow drawn over it, a card's rounded fill hid the mini section
	 * drawn on it. So a painter moves up a layer whenever the kind changes
	 * (runs of one kind still batch), and the order things are painted in is
	 * the order they appear. A painter made from another shares its layer
	 * count; the widget returns TopLayer() so its siblings land above.
	 */
	struct FPainter
	{
		FSlateWindowElementList& Out;
		const FGeometry& Geometry;
		int32 Layer;
		/** Multiplies every alpha drawn (a page fading out). */
		float Opacity = 1.f;

		/** A root painter: its own layer count, starting at InLayer. */
		FPainter(FSlateWindowElementList& InOut, const FGeometry& InGeometry, int32 InLayer, float InOpacity = 1.f)
			: Out(InOut), Geometry(InGeometry), Layer(InLayer), Opacity(InOpacity), Own{ InLayer, EPaintKind::None }, Shared(&Own) {}

		/** A painter over the same surface at another opacity, sharing the parent's layer count. */
		FPainter(const FPainter& Parent, float InOpacity)
			: Out(Parent.Out), Geometry(Parent.Geometry), Layer(Parent.Layer), Opacity(InOpacity), Own{ Parent.Layer, EPaintKind::None },
			Shared(Parent.Shared) {}

		FPainter(const FPainter& Other)
			: Out(Other.Out), Geometry(Other.Geometry), Layer(Other.Layer), Opacity(Other.Opacity), Own{ Other.Layer, EPaintKind::None },
			Shared(Other.Shared) {}
		FPainter& operator=(const FPainter&) = delete;

		FVector2D Size() const { return FVector2D(Geometry.GetLocalSize()); }

		/** The layer for the next element of this kind. Rounded boxes always
		 *  take their own: each is its own brush, and two need not keep order. */
		int32 LayerFor(EPaintKind Kind) const
		{
			if (Shared->Kind != Kind || Kind == EPaintKind::Rounded)
			{
				if (Shared->Kind != EPaintKind::None)
				{
					++Shared->Layer;
				}
				Shared->Kind = Kind;
			}
			return Shared->Layer;
		}

		/** The highest layer painted on: what the widget's paint returns. */
		int32 TopLayer() const { return Shared->Layer; }

	private:
		struct FCount { int32 Layer; EPaintKind Kind; };
		mutable FCount Own;
		FCount* Shared;
	};

	/** The text's size in local units at this font. */
	ONEBRAHAM_API FVector2D Measure(const FString& Text, const FSlateFontInfo& Font);
	inline FVector2D Measure(const FText& Text, const FSlateFontInfo& Font) { return Measure(Text.ToString(), Font); }

	/** The font's ascent above the baseline, local units (for baseline-aligned type). */
	ONEBRAHAM_API float Baseline(const FSlateFontInfo& Font);

	/** Text with its top-left at Position. Returns its size. */
	ONEBRAHAM_API FVector2D Text(const FPainter& C, const FVector2D& Position, const FString& Text,
		const FSlateFontInfo& Font, const FLinearColor& Colour);
	inline FVector2D Text(const FPainter& C, const FVector2D& Position, const FText& InText,
		const FSlateFontInfo& Font, const FLinearColor& Colour)
	{
		return Text(C, Position, InText.ToString(), Font, Colour);
	}

	/** Text whose baseline sits at BaselineY, left edge at X (Align 0), centred (0.5) or right (1). */
	ONEBRAHAM_API FVector2D TextOnBaseline(const FPainter& C, float X, float BaselineY, const FString& Text,
		const FSlateFontInfo& Font, const FLinearColor& Colour, float Align = 0.f);

	/** A filled rectangle. */
	ONEBRAHAM_API void Box(const FPainter& C, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Colour);

	/** A rounded rectangle, filled and/or outlined (a zero alpha skips either). */
	ONEBRAHAM_API void Rounded(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Radius,
		const FLinearColor& Fill, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 1.f);

	/** A straight line. */
	ONEBRAHAM_API void PenLine(const FPainter& C, const FVector2D& From, const FVector2D& To, const FLinearColor& Colour,
		float Thickness = 1.f);

	/** A polyline. */
	ONEBRAHAM_API void PenPath(const FPainter& C, const TArray<FVector2D>& Points, const FLinearColor& Colour, float Thickness = 1.f);

	/** A horizontal gradient across the rectangle: Stops are (fraction of width, colour). */
	ONEBRAHAM_API void GradientH(const FPainter& C, const FVector2D& Position, const FVector2D& Size,
		const TArray<TPair<float, FLinearColor>>& Stops);

	/** A vertical gradient down the rectangle. */
	ONEBRAHAM_API void GradientV(const FPainter& C, const FVector2D& Position, const FVector2D& Size,
		const TArray<TPair<float, FLinearColor>>& Stops);

	/** The ribbon (#6A1BFF -> #1485FF) as a horizontal bar. Only on things the building lights. */
	ONEBRAHAM_API void Ribbon(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Alpha = 1.f);

	/** A dotted rule: Size px dots every Pitch px from X0 to X1, phase-shifted by Offset (marching). */
	ONEBRAHAM_API void DottedH(const FPainter& C, float X0, float X1, float Y, const FLinearColor& Colour,
		float Pitch = 4.f, float Dot = 1.f, float Offset = 0.f);

	/** A grid of dots over a rectangle: the meeting-room glass manifestation band. */
	ONEBRAHAM_API void DotGrid(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Pitch,
		const FLinearColor& Colour);

	/** 45-degree hatching inside a rectangle (sealed floors, stair cores). */
	ONEBRAHAM_API void Hatch(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Pitch,
		const FLinearColor& Colour, float Thickness = 1.f);

	/** A circle outline (or a filled disc when Thickness <= 0), as a polygon. */
	ONEBRAHAM_API void PenCircle(const FPainter& C, const FVector2D& Centre, float Radius, const FLinearColor& Colour,
		float Thickness = 1.f, float FromFraction = 0.f, float ToFraction = 1.f);

	/** A blur of whatever was drawn under the rectangle (UBackgroundBlur's pass). */
	ONEBRAHAM_API void Blur(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Strength);

	/** Alpha multiplied in, as every painter fades its colours. */
	inline FLinearColor Fade(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * FMath::Clamp(Alpha, 0.f, 1.f));
	}

	/** A token at an alpha: shorthand for UOneBrahamUIStyle::Colour. */
	inline FLinearColor Tok(EOneBrahamColour Token, float Alpha = 1.f)
	{
		return UOneBrahamUIStyle::Colour(Token, Alpha);
	}

	/** Shorthand for UOneBrahamUIStyle::TypeFont. */
	inline FSlateFontInfo Type(EOneBrahamType Token, float Px, int32 Tracking = 0)
	{
		return UOneBrahamUIStyle::TypeFont(Token, Px, Tracking);
	}

	/** Seconds on the real clock: menus animate on it (a paused game still moves its menu). */
	inline double Now() { return FPlatformTime::Seconds(); }

	/** Typed-on text: the first N characters after Seconds at PerChar seconds each. */
	ONEBRAHAM_API FString TypeOn(const FString& Text, double Elapsed, float PerChar);
}
