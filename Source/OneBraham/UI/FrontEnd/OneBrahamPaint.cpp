// Copyright (c) One Braham. All Rights Reserved.

#include "UI/FrontEnd/OneBrahamPaint.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace OneBrahamPaint
{
	namespace
	{
		const FSlateBrush* WhiteBrush()
		{
			static const FSlateColorBrush Brush(FLinearColor::White);
			return &Brush;
		}

		FPaintGeometry At(const FPainter& C, const FVector2D& Position, const FVector2D& Size)
		{
			return C.Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position)));
		}

		TSharedPtr<FSlateFontMeasure> MeasureService()
		{
			if (!FSlateApplication::IsInitialized() || FSlateApplication::Get().GetRenderer() == nullptr)
			{
				return nullptr;
			}
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		}
	}

	namespace
	{
		FVector2D MeasurePlain(const FString& InText, const FSlateFontInfo& Font)
		{
			const TSharedPtr<FSlateFontMeasure> Service = MeasureService();
			return Service.IsValid() ? FVector2D(Service->Measure(InText, Font, 1.f)) : FVector2D(InText.Len() * Font.Size * 0.6f, Font.Size * 1.3f);
		}

		/**
		 * Tracking, set by hand. FSlateFontInfo::LetterSpacing is honoured by the
		 * text shaper (STextBlock) but not by MakeText or the font measure, so
		 * the eyebrows' +180 came out set solid. Positive tracking only: it is
		 * for caps, where kerning hardly matters; the headlines' -10 keeps the
		 * engine's kerned run instead.
		 */
		bool IsTracked(const FSlateFontInfo& Font)
		{
			return Font.LetterSpacing > 0;
		}

		/** The space added after each glyph, local units (1/1000 em of the pixel size). */
		float TrackingOf(const FSlateFontInfo& Font)
		{
			return Font.LetterSpacing / 1000.f * (Font.Size / 0.75f);
		}

		FSlateFontInfo Untracked(const FSlateFontInfo& Font)
		{
			FSlateFontInfo Plain = Font;
			Plain.LetterSpacing = 0;
			return Plain;
		}
	}

	FVector2D Measure(const FString& InText, const FSlateFontInfo& Font)
	{
		if (!IsTracked(Font) || InText.Len() < 2)
		{
			return MeasurePlain(InText, Untracked(Font));
		}
		const FSlateFontInfo Plain = Untracked(Font);
		const float Tracking = TrackingOf(Font);
		FVector2D Size(0.f, 0.f);
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			const FVector2D Glyph = MeasurePlain(InText.Mid(Index, 1), Plain);
			Size.X += Glyph.X + (Index + 1 < InText.Len() ? Tracking : 0.f);
			Size.Y = FMath::Max(Size.Y, Glyph.Y);
		}
		return Size;
	}

	float Baseline(const FSlateFontInfo& Font)
	{
		const TSharedPtr<FSlateFontMeasure> Service = MeasureService();
		if (!Service.IsValid())
		{
			return Font.Size;
		}
		// GetBaseline is the descent below the baseline, negative, measured up
		// from the bottom of the line; the ascent is the rest of the line.
		return static_cast<float>(Service->GetMaxCharacterHeight(Font, 1.f)) + static_cast<float>(Service->GetBaseline(Font, 1.f));
	}

	FVector2D Text(const FPainter& C, const FVector2D& Position, const FString& InText, const FSlateFontInfo& Font,
		const FLinearColor& Colour)
	{
		if (InText.IsEmpty())
		{
			return FVector2D::ZeroVector;
		}
		const FVector2D Size = Measure(InText, Font);
		const FLinearColor Tint = Fade(Colour, C.Opacity);
		if (Tint.A <= 0.001f)
		{
			return Size;
		}
		if (IsTracked(Font) && InText.Len() > 1)
		{
			// Glyph by glyph, each advanced by its own width plus the tracking.
			const FSlateFontInfo Plain = Untracked(Font);
			const float Tracking = TrackingOf(Font);
			float X = Position.X;
			for (int32 Index = 0; Index < InText.Len(); ++Index)
			{
				const FString Glyph = InText.Mid(Index, 1);
				const FVector2D GlyphSize = MeasurePlain(Glyph, Plain);
				if (!FChar::IsWhitespace(Glyph[0]))
				{
					FSlateDrawElement::MakeText(C.Out, C.LayerFor(EPaintKind::Text), At(C, FVector2D(X, Position.Y), GlyphSize + FVector2D(4.f, 0.f)), Glyph, Plain,
						ESlateDrawEffect::None, Tint);
				}
				X += GlyphSize.X + Tracking;
			}
			return Size;
		}
		FSlateDrawElement::MakeText(C.Out, C.LayerFor(EPaintKind::Text), At(C, Position, Size + FVector2D(4.f, 0.f)), InText, Untracked(Font),
			ESlateDrawEffect::None, Tint);
		return Size;
	}

	FVector2D TextOnBaseline(const FPainter& C, float X, float BaselineY, const FString& InText, const FSlateFontInfo& Font,
		const FLinearColor& Colour, float Align)
	{
		const FVector2D Size = Measure(InText, Font);
		return Text(C, FVector2D(X - Size.X * Align, BaselineY - Baseline(Font)), InText, Font, Colour);
	}

	void Box(const FPainter& C, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Colour)
	{
		const FLinearColor Tint = Fade(Colour, C.Opacity);
		if (Tint.A <= 0.001f || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(C.Out, C.LayerFor(EPaintKind::Box), At(C, Position, Size), WhiteBrush(), ESlateDrawEffect::None, Tint);
	}

	void Rounded(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Radius,
		const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		const FLinearColor FillTint = Fade(Fill, C.Opacity);
		const FLinearColor OutlineTint = Fade(Outline, C.Opacity);
		if (FillTint.A <= 0.001f && OutlineTint.A <= 0.001f)
		{
			return;
		}
		// A rounded box draws fill and outline in one element: the element's
		// tint is the fill, the brush's outline settings the outline.
		const FSlateRoundedBoxBrush Brush(FillTint, Radius, OutlineTint, OutlineTint.A > 0.001f ? OutlineWidth : 0.f);
		FSlateDrawElement::MakeBox(C.Out, C.LayerFor(EPaintKind::Rounded), At(C, Position, Size), &Brush, ESlateDrawEffect::None, FillTint);
	}

	void PenLine(const FPainter& C, const FVector2D& From, const FVector2D& To, const FLinearColor& Colour, float Thickness)
	{
		PenPath(C, { From, To }, Colour, Thickness);
	}

	void PenPath(const FPainter& C, const TArray<FVector2D>& Points, const FLinearColor& Colour, float Thickness)
	{
		const FLinearColor Tint = Fade(Colour, C.Opacity);
		if (Tint.A <= 0.001f || Points.Num() < 2)
		{
			return;
		}
		TArray<FVector2f> Floats;
		Floats.Reserve(Points.Num());
		for (const FVector2D& Point : Points)
		{
			Floats.Add(FVector2f(Point));
		}
		FSlateDrawElement::MakeLines(C.Out, C.LayerFor(EPaintKind::Lines), C.Geometry.ToPaintGeometry(), MoveTemp(Floats), ESlateDrawEffect::None,
			Tint, true, Thickness);
	}

	static void Gradient(const FPainter& C, const FVector2D& Position, const FVector2D& Size,
		const TArray<TPair<float, FLinearColor>>& Stops, bool bHorizontal)
	{
		if (Stops.Num() < 2 || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		TArray<FSlateGradientStop> Slate;
		for (const TPair<float, FLinearColor>& Stop : Stops)
		{
			const FVector2D At2 = bHorizontal ? FVector2D(Size.X * Stop.Key, 0.f) : FVector2D(0.f, Size.Y * Stop.Key);
			Slate.Add(FSlateGradientStop(At2, Fade(Stop.Value, C.Opacity)));
		}
		// Slate names a gradient by its bands: vertical bands vary across X.
		FSlateDrawElement::MakeGradient(C.Out, C.LayerFor(EPaintKind::Gradient), At(C, Position, Size), MoveTemp(Slate),
			bHorizontal ? Orient_Vertical : Orient_Horizontal);
	}

	void GradientH(const FPainter& C, const FVector2D& Position, const FVector2D& Size, const TArray<TPair<float, FLinearColor>>& Stops)
	{
		Gradient(C, Position, Size, Stops, true);
	}

	void GradientV(const FPainter& C, const FVector2D& Position, const FVector2D& Size, const TArray<TPair<float, FLinearColor>>& Stops)
	{
		Gradient(C, Position, Size, Stops, false);
	}

	void Ribbon(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Alpha)
	{
		GradientH(C, Position, Size, {
			{ 0.f, Tok(EOneBrahamColour::RibbonA, Alpha) },
			{ 1.f, Tok(EOneBrahamColour::RibbonB, Alpha) } });
	}

	void DottedH(const FPainter& C, float X0, float X1, float Y, const FLinearColor& Colour, float Pitch, float Dot, float Offset)
	{
		if (X1 <= X0 || Pitch <= 0.f)
		{
			return;
		}
		const float Phase = FMath::Fmod(Offset, Pitch);
		for (float X = X0 + (Phase < 0.f ? Phase + Pitch : Phase); X + Dot <= X1; X += Pitch)
		{
			Box(C, FVector2D(X, Y), FVector2D(Dot, Dot), Colour);
		}
	}

	void DotGrid(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Pitch, const FLinearColor& Colour)
	{
		if (Pitch <= 1.f)
		{
			return;
		}
		for (float Y = Position.Y; Y < Position.Y + Size.Y; Y += Pitch)
		{
			DottedH(C, Position.X, Position.X + Size.X, Y, Colour, Pitch, 1.f, 0.f);
		}
	}

	void Hatch(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Pitch, const FLinearColor& Colour, float Thickness)
	{
		if (Pitch <= 1.f || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		// Lines of slope -1 (bottom-left to top-right), clipped to the rectangle
		// by hand: X + Y = K for K across the rectangle's diagonal span.
		const float W = Size.X;
		const float H = Size.Y;
		for (float K = Pitch * 0.5f; K < W + H; K += Pitch)
		{
			const FVector2D A(FMath::Max(0.f, K - H), FMath::Min(H, K));
			const FVector2D B(FMath::Min(W, K), FMath::Max(0.f, K - W));
			PenLine(C, Position + A, Position + B, Colour, Thickness);
		}
	}

	void PenCircle(const FPainter& C, const FVector2D& Centre, float Radius, const FLinearColor& Colour, float Thickness,
		float FromFraction, float ToFraction)
	{
		if (Radius <= 0.f)
		{
			return;
		}
		if (Thickness <= 0.f)
		{
			// A disc: a rounded box with the radius of the circle.
			Rounded(C, Centre - FVector2D(Radius), FVector2D(Radius * 2.f), Radius, Colour);
			return;
		}
		const int32 Segments = FMath::Clamp(FMath::RoundToInt(Radius * 1.2f), 16, 96);
		const float From = FMath::Clamp(FromFraction, 0.f, 1.f);
		const float To = FMath::Clamp(ToFraction, From, 1.f);
		const int32 Count = FMath::Max(2, FMath::CeilToInt(Segments * (To - From)) + 1);
		TArray<FVector2D> Points;
		Points.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// Clockwise from twelve o'clock, as a card reader fills.
			const float Angle = 2.f * PI * FMath::Lerp(From, To, static_cast<float>(Index) / (Count - 1)) - PI * 0.5f;
			Points.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		PenPath(C, Points, Colour, Thickness);
	}

	void Blur(const FPainter& C, const FVector2D& Position, const FVector2D& Size, float Strength)
	{
		if (Strength <= 0.05f || !FPlatformMisc::SupportsBackbufferSampling())
		{
			return;
		}
		// The engine's own background-blur pass (SBackgroundBlur), over the
		// rectangle in render space.
		const FGeometry Child = C.Geometry.MakeChild(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position)));
		const FSlateRect Bounds = Child.GetRenderBoundingRect();
		FPaintGeometry Paint(Bounds.GetTopLeft(), Bounds.GetSize(), Child.GetAccumulatedLayoutTransform().GetScale());

		int32 Kernel = FMath::RoundToInt(Strength * 3.f);
		int32 Downsample = 0;
		if (Kernel > 9)
		{
			Downsample = Kernel >= 64 ? (Kernel >= 96 ? 4 : 3) : 2;
			Kernel /= Downsample;
		}
		if (Kernel % 2 == 0)
		{
			++Kernel;
		}
		Kernel = FMath::Clamp(Kernel, 3, 255);

		float Computed = FMath::Max(0.5f, Strength);
		int32 Width = FMath::RoundToInt(Bounds.GetSize().X);
		int32 Height = FMath::RoundToInt(Bounds.GetSize().Y);
		if (Downsample > 0)
		{
			Width = FMath::DivideAndRoundUp(Width, Downsample);
			Height = FMath::DivideAndRoundUp(Height, Downsample);
			Computed /= Downsample;
		}
		if (Width <= 0 || Height <= 0)
		{
			return;
		}
		FSlateDrawElement::MakePostProcessBlur(C.Out, C.LayerFor(EPaintKind::Blur), Paint,
			FVector4f(static_cast<float>(Kernel), Computed, static_cast<float>(Width), static_cast<float>(Height)), Downsample);
	}

	FString TypeOn(const FString& InText, double Elapsed, float PerChar)
	{
		if (PerChar <= 0.f || Elapsed < 0.0)
		{
			return Elapsed < 0.0 ? FString() : InText;
		}
		const int32 Count = FMath::Clamp(FMath::FloorToInt(Elapsed / PerChar), 0, InText.Len());
		return InText.Left(Count);
	}
}
