// Copyright (c) One Braham. All Rights Reserved.

#include "UI/HUD/OneBrahamHUDPaint.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

// ==========================================================================
//  Colour
// ==========================================================================
namespace
{
	EOneBrahamColourBlindMode CurrentColourBlindMode()
	{
		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		return Settings != nullptr ? Settings->ColourBlindMode : EOneBrahamColourBlindMode::Off;
	}

	FLinearColor Token(EOneBrahamColour Colour)
	{
		return UOneBrahamUIStyle::Colour(Colour);
	}
}

// The palette is the design system's (UOneBrahamUIStyle::Colour): one place
// converts the brand's sRGB floats and applies the colour-blind remap.
FLinearColor OneBrahamHUDStyle::Hex(const TCHAR* InHex, float Alpha)	{ return UOneBrahamUIStyle::Hex(InHex, Alpha); }
FLinearColor OneBrahamHUDStyle::Primary()			{ return Token(EOneBrahamColour::Primary); }
FLinearColor OneBrahamHUDStyle::Navy()				{ return Token(EOneBrahamColour::Navy); }
FLinearColor OneBrahamHUDStyle::SignalBlue()		{ return Token(EOneBrahamColour::SignalBlue); }
FLinearColor OneBrahamHUDStyle::LightGrey()			{ return Token(EOneBrahamColour::LightGrey); }
FLinearColor OneBrahamHUDStyle::White()				{ return FLinearColor::White; }
FLinearColor OneBrahamHUDStyle::HazardYellow()		{ return Token(EOneBrahamColour::LockdownFieldA); }
FLinearColor OneBrahamHUDStyle::SafetyYellow()		{ return Token(EOneBrahamColour::SafetyYellow); }
FLinearColor OneBrahamHUDStyle::EmergencyRed()		{ return Token(EOneBrahamColour::EmergencyRed); }
FLinearColor OneBrahamHUDStyle::OLEDBlack()			{ return Token(EOneBrahamColour::OLEDBlack); }
FLinearColor OneBrahamHUDStyle::BaffleBlack()		{ return Token(EOneBrahamColour::BaffleBlack); }
FLinearColor OneBrahamHUDStyle::SignageWhite()		{ return Token(EOneBrahamColour::SignageWhite); }
FLinearColor OneBrahamHUDStyle::Paper()				{ return Token(EOneBrahamColour::Paper); }
FLinearColor OneBrahamHUDStyle::RibbonPurple()		{ return Token(EOneBrahamColour::RibbonA); }
FLinearColor OneBrahamHUDStyle::RibbonBlue()		{ return Token(EOneBrahamColour::RibbonB); }
FLinearColor OneBrahamHUDStyle::LockdownFieldDark()	{ return Token(EOneBrahamColour::LockdownFieldB); }
FLinearColor OneBrahamHUDStyle::PurgeFieldDark()	{ return Token(EOneBrahamColour::PurgeFieldB); }
FLinearColor OneBrahamHUDStyle::LCDField()			{ return Token(EOneBrahamColour::LCDField); }
FLinearColor OneBrahamHUDStyle::LCDInk()			{ return Token(EOneBrahamColour::LCDInk); }
FLinearColor OneBrahamHUDStyle::FireHousing()		{ return Token(EOneBrahamColour::FireHousing); }

bool OneBrahamHUDStyle::IsWarningRemapped()
{
	return CurrentColourBlindMode() == EOneBrahamColourBlindMode::Tritanopia;
}

int32 OneBrahamHUDStyle::StageBand(EOneBrahamLockdownStage Stage)
{
	switch (Stage)
	{
	case EOneBrahamLockdownStage::PreLockdown:
	case EOneBrahamLockdownStage::Lockdown:	return 1;
	case EOneBrahamLockdownStage::Purge:	return 2;
	case EOneBrahamLockdownStage::Normal:
	default:								return 0;
	}
}

// The colours are the design system's one rule (UOneBrahamUIStyle::StageTone):
// the HUD never decides a stage colour of its own.
FLinearColor OneBrahamHUDStyle::StageAccent(EOneBrahamLockdownStage Stage)
{
	return UOneBrahamUIStyle::StageAccent(Stage);
}

FLinearColor OneBrahamHUDStyle::StageHairline(EOneBrahamLockdownStage Stage)
{
	return UOneBrahamUIStyle::StageHairline(Stage);
}

FLinearColor OneBrahamHUDStyle::Tone(EOneBrahamStageTone StageTone, float Alpha)
{
	return UOneBrahamUIStyle::ToneColour(StageTone, Alpha);
}

// ==========================================================================
//  Type
// ==========================================================================
bool OneBrahamHUDStyle::HasBrandFonts()
{
	return UOneBrahamUIStyle::HasBrandFaces();
}

FSlateFontInfo OneBrahamHUDStyle::Font(EOneBrahamHUDType Type, float Px, int32 Tracking, float OutlinePx,
	const FLinearColor& OutlineColour)
{
	// The design system's face at the size, with the HUD's own outline: an
	// outline is a legibility decision per element, not a property of a face.
	FSlateFontInfo Info = UOneBrahamUIStyle::TypeFont(Type, Px, Tracking, false);

	if (OutlinePx > 0.f)
	{
		Info.OutlineSettings = FFontOutlineSettings(FMath::Max(1, FMath::RoundToInt(OutlinePx)), OutlineColour);
		Info.OutlineSettings.bSeparateFillAlpha = true;
	}

	return Info;
}

FSlateFontInfo OneBrahamHUDStyle::HUDFont(EOneBrahamHUDType Type, float Px, int32 Tracking, bool bHeavyOutline)
{
	return OutlinedFont(Type, Px, Tracking, bHeavyOutline ? 2.f : 1.f, FLinearColor(0.f, 0.f, 0.f, bHeavyOutline ? 0.7f : 0.6f));
}

FString OneBrahamHUDStyle::Tracked(const FString& Text, const FSlateFontInfo& Font)
{
	// Tracking adds the same gap after every glyph, the space included, so at
	// +150 and up a word space is no wider than the gap between two letters and
	// "FIRE DRILL" reads as one word. Signwriters open the word space as they
	// track: so does this. Not in a monospaced face, whose space is already a
	// full glyph cell: doubled, "ACCIDENT BOOK" fell apart into two words a
	// column apart.
	// The threshold is +300: at the +180 to +220 the eyebrows use, the space
	// glyph's own quarter em already separates words, and doubling it read as
	// "BACK  IN  5  MINUTES" - a signwriter's gap, but a careless one.
	if (Font.LetterSpacing < 300 || !FSlateApplication::IsInitialized())
	{
		return Text;
	}
	const TSharedRef<FSlateFontMeasure> Measurer = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float Narrow = Measurer->Measure(TEXT("i"), Font, 1.f).X;
	const float Wide = Measurer->Measure(TEXT("W"), Font, 1.f).X;
	if (FMath::IsNearlyEqual(Narrow, Wide, 0.5f))
	{
		return Text;
	}
	return Text.Replace(TEXT(" "), TEXT("  "));
}

FVector2D OneBrahamHUDStyle::MeasureTracked(const FString& Text, const FSlateFontInfo& Font)
{
	if (Text.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		return FVector2D::ZeroVector;
	}

	const TSharedRef<FSlateFontMeasure> Measurer = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return Measurer->Measure(Tracked(Text, Font), Font, 1.f);
}

// ==========================================================================
//  Brushes
// ==========================================================================
const FSlateBrush* OneBrahamHUDStyle::RoundedBrush(float Radius)
{
	static TMap<int32, FSlateBrush> Cache;
	const int32 Key = FMath::RoundToInt(Radius * 4.f);

	if (FSlateBrush* Found = Cache.Find(Key))
	{
		return Found;
	}

	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius));
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor::Transparent);
	Brush.OutlineSettings.Width = 0.f;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	return &Cache.Add(Key, Brush);
}

const FSlateBrush* OneBrahamHUDStyle::OutlineBrush(float Radius, float Thickness)
{
	static TMap<int32, FSlateBrush> Cache;
	const int32 Key = FMath::RoundToInt(Radius * 4.f) * 1000 + FMath::RoundToInt(Thickness * 4.f);

	if (FSlateBrush* Found = Cache.Find(Key))
	{
		return Found;
	}

	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius));
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor::White);
	Brush.OutlineSettings.Width = Thickness;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.bUseBrushTransparency = true;
	return &Cache.Add(Key, Brush);
}

const FSlateBrush* OneBrahamHUDStyle::WhiteBrush()
{
	static FSlateBrush Brush = []()
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::Box;
		B.TintColor = FSlateColor(FLinearColor::White);
		return B;
	}();
	return &Brush;
}

// ==========================================================================
//  Painter
// ==========================================================================
FPaintGeometry FOneBrahamPainter::Paint(const FVector2D& Pos, const FVector2D& Size) const
{
	return Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos)));
}

FOneBrahamPainter FOneBrahamPainter::Child(const FVector2D& Offset, float Scale) const
{
	FOneBrahamPainter Copy = *this;
	const FVector2D LocalSize = Geometry.GetLocalSize();
	Copy.Geometry = Geometry.MakeChild(LocalSize / FMath::Max(0.01f, Scale), FSlateLayoutTransform(Scale, Offset));
	return Copy;
}

void FOneBrahamPainter::Rect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour)
{
	if (Size.X <= 0.f || Size.Y <= 0.f || Colour.A * Opacity <= 0.002f)
	{
		return;
	}
	FSlateDrawElement::MakeBox(*Out, Next(), Paint(Pos, Size), OneBrahamHUDStyle::WhiteBrush(), ESlateDrawEffect::None, Tint(Colour));
}

void FOneBrahamPainter::RoundRect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour, float Radius)
{
	if (Size.X <= 0.f || Size.Y <= 0.f || Colour.A * Opacity <= 0.002f)
	{
		return;
	}
	if (Radius <= 0.f)
	{
		Rect(Pos, Size, Colour);
		return;
	}
	FSlateDrawElement::MakeBox(*Out, Next(), Paint(Pos, Size), OneBrahamHUDStyle::RoundedBrush(Radius), ESlateDrawEffect::None, Tint(Colour));
}

void FOneBrahamPainter::Outline(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour, float Thickness, float Radius)
{
	if (Size.X <= 0.f || Size.Y <= 0.f || Colour.A * Opacity <= 0.002f)
	{
		return;
	}

	if (Radius <= 0.f)
	{
		// Four boxes rather than a line loop: crisp corners at any thickness.
		Rect(Pos, FVector2D(Size.X, Thickness), Colour);
		Rect(FVector2D(Pos.X, Pos.Y + Size.Y - Thickness), FVector2D(Size.X, Thickness), Colour);
		Rect(FVector2D(Pos.X, Pos.Y + Thickness), FVector2D(Thickness, Size.Y - 2.f * Thickness), Colour);
		Rect(FVector2D(Pos.X + Size.X - Thickness, Pos.Y + Thickness), FVector2D(Thickness, Size.Y - 2.f * Thickness), Colour);
		return;
	}

	// The rounded-box shader draws the fill in the element's tint and the
	// outline in the brush's own colour. The brush is copied into the element
	// when it is made, so a per-call brush with this outline colour is safe;
	// the fill is all but transparent (not zero, so nothing culls the element).
	FSlateBrush Brush = *OneBrahamHUDStyle::OutlineBrush(Radius, Thickness);
	Brush.OutlineSettings.bUseBrushTransparency = false;
	Brush.OutlineSettings.Color = FSlateColor(Tint(Colour));
	FSlateDrawElement::MakeBox(*Out, Next(), Paint(Pos, Size), &Brush, ESlateDrawEffect::None,
		FLinearColor(Colour.R, Colour.G, Colour.B, 0.0001f));
}

void FOneBrahamPainter::Segment(const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Thickness)
{
	if (Colour.A * Opacity <= 0.002f)
	{
		return;
	}

	// Axis-aligned lines are boxes: Slate's antialiased lines blur a 1 px rule
	// across two pixels, and a hairline that is grey instead of white is the
	// difference between signage and a smudge.
	if (FMath::IsNearlyEqual(A.Y, B.Y))
	{
		const float X0 = FMath::Min(A.X, B.X);
		Rect(FVector2D(X0, A.Y - Thickness * 0.5f), FVector2D(FMath::Abs(B.X - A.X), Thickness), Colour);
		return;
	}
	if (FMath::IsNearlyEqual(A.X, B.X))
	{
		const float Y0 = FMath::Min(A.Y, B.Y);
		Rect(FVector2D(A.X - Thickness * 0.5f, Y0), FVector2D(Thickness, FMath::Abs(B.Y - A.Y)), Colour);
		return;
	}

	TArray<FVector2f> Points = { FVector2f(A), FVector2f(B) };
	FSlateDrawElement::MakeLines(*Out, Next(), Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Tint(Colour), true, Thickness);
}

void FOneBrahamPainter::Polyline(const TArray<FVector2D>& Points, const FLinearColor& Colour, float Thickness)
{
	if (Points.Num() < 2 || Colour.A * Opacity <= 0.002f)
	{
		return;
	}
	TArray<FVector2f> Floats;
	Floats.Reserve(Points.Num());
	for (const FVector2D& P : Points)
	{
		Floats.Add(FVector2f(P));
	}
	FSlateDrawElement::MakeLines(*Out, Next(), Geometry.ToPaintGeometry(), MoveTemp(Floats), ESlateDrawEffect::None, Tint(Colour), true, Thickness);
}

void FOneBrahamPainter::ColouredPolyline(const TArray<FVector2D>& Points, const TArray<FLinearColor>& Colours, float Thickness)
{
	if (Points.Num() < 2 || Colours.Num() != Points.Num())
	{
		return;
	}
	TArray<FVector2f> Floats;
	TArray<FLinearColor> Tints;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Floats.Add(FVector2f(Points[Index]));
		Tints.Add(Tint(Colours[Index]));
	}
	FSlateDrawElement::MakeLines(*Out, Next(), Geometry.ToPaintGeometry(), MoveTemp(Floats), MoveTemp(Tints),
		ESlateDrawEffect::None, FLinearColor::White, true, Thickness);
}

void FOneBrahamPainter::DashedLine(const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Thickness,
	float Dash, float Gap, float Offset)
{
	const FVector2D Delta = B - A;
	const float Length = Delta.Size();
	if (Length <= 0.f || Dash <= 0.f)
	{
		return;
	}

	const FVector2D Dir = Delta / Length;
	const float Period = Dash + FMath::Max(0.f, Gap);
	float Start = -FMath::Fmod(FMath::Max(0.f, Offset), Period);

	for (; Start < Length; Start += Period)
	{
		const float S0 = FMath::Max(0.f, Start);
		const float S1 = FMath::Min(Length, Start + Dash);
		if (S1 > S0)
		{
			Segment(A + Dir * S0, A + Dir * S1, Colour, Thickness);
		}
	}
}

void FOneBrahamPainter::Gradient(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& From, const FLinearColor& To,
	bool bHorizontal, float Radius)
{
	if (Size.X <= 0.f || Size.Y <= 0.f)
	{
		return;
	}

	// Stops are positions in the element's own space; a vertical-orientation
	// gradient in Slate varies along X.
	TArray<FSlateGradientStop> Stops;
	Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, Tint(From)));
	Stops.Add(FSlateGradientStop(bHorizontal ? FVector2D(Size.X, 0.f) : FVector2D(0.f, Size.Y), Tint(To)));

	FSlateDrawElement::MakeGradient(*Out, Next(), Paint(Pos, Size), MoveTemp(Stops),
		bHorizontal ? Orient_Vertical : Orient_Horizontal, ESlateDrawEffect::None, FVector4f(Radius, Radius, Radius, Radius));
}

void FOneBrahamPainter::Hatch(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Colour, float Spacing,
	float Thickness, bool bRising)
{
	if (Size.X <= 0.f || Size.Y <= 0.f || Spacing <= 0.f)
	{
		return;
	}

	// Lines of slope +/-1 clipped to the box analytically, so the hatching never
	// spills out of the cell it describes.
	const float W = Size.X;
	const float H = Size.Y;
	for (float C = -H; C < W; C += Spacing)
	{
		// Rising: from (C, H) going up-right to (C + H, 0).
		FVector2D P0 = bRising ? FVector2D(C, H) : FVector2D(C, 0.f);
		FVector2D P1 = bRising ? FVector2D(C + H, 0.f) : FVector2D(C + H, H);

		// Clip in X.
		if (P0.X < 0.f)
		{
			const float Cut = -P0.X;
			P0.X = 0.f;
			P0.Y += bRising ? -Cut : Cut;
		}
		if (P1.X > W)
		{
			const float Cut = P1.X - W;
			P1.X = W;
			P1.Y += bRising ? Cut : -Cut;
		}
		if (P1.X <= P0.X)
		{
			continue;
		}

		Segment(Pos + P0, Pos + P1, Colour, Thickness);
	}
}

void FOneBrahamPainter::HazardStripes(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& A, const FLinearColor& B,
	float Period, float Phase)
{
	Rect(Pos, Size, B);
	PushClip(Pos, Size);
	const float Half = Period * 0.5f;
	const float Span = Size.X + Size.Y;
	const float Start = -Size.Y - FMath::Fmod(FMath::Max(0.f, Phase), Period);
	for (float C = Start; C < Span; C += Period)
	{
		// One parallelogram per stripe, rising at 45 degrees, clipped by the
		// box rather than by arithmetic.
		const FVector2D BL = Pos + FVector2D(C, Size.Y);
		const FVector2D BR = BL + FVector2D(Half, 0.f);
		const FVector2D TL = BL + FVector2D(Size.Y, -Size.Y);
		const FVector2D TR = TL + FVector2D(Half, 0.f);
		Triangle(BL, BR, TR, A);
		Triangle(BL, TR, TL, A);
	}
	PopClip();
}

void FOneBrahamPainter::Dots(float X0, float X1, float Y, const FLinearColor& Colour, float Pitch, float DotSize, float Phase)
{
	if (X1 <= X0 || Pitch <= 0.f)
	{
		return;
	}
	for (float X = X0 + FMath::Fmod(FMath::Max(0.f, Phase), Pitch); X + DotSize <= X1; X += Pitch)
	{
		Rect(FVector2D(X, Y), FVector2D(DotSize, DotSize), Colour);
	}
}

void FOneBrahamPainter::Circle(const FVector2D& Centre, float Radius, const FLinearColor& Colour)
{
	RoundRect(Centre - FVector2D(Radius, Radius), FVector2D(Radius, Radius) * 2.f, Colour, Radius);
}

void FOneBrahamPainter::Ring(const FVector2D& Centre, float Radius, const FLinearColor& Colour, float Thickness)
{
	Arc(Centre, Radius, 0.f, 360.f, Colour, Thickness);
}

void FOneBrahamPainter::Arc(const FVector2D& Centre, float Radius, float StartDeg, float SweepDeg, const FLinearColor& Colour,
	float Thickness, const FLinearColor* ColourEnd)
{
	if (FMath::Abs(SweepDeg) <= 0.01f || Radius <= 0.f)
	{
		return;
	}

	const int32 Segments = FMath::Clamp(FMath::CeilToInt(FMath::Abs(SweepDeg) / 360.f * FMath::Max(24.f, Radius * 0.8f)), 4, 128);
	TArray<FVector2D> Points;
	TArray<FLinearColor> Colours;
	Points.Reserve(Segments + 1);

	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const float T = static_cast<float>(Index) / Segments;
		const float Rad = FMath::DegreesToRadians(StartDeg + SweepDeg * T - 90.f);
		Points.Add(Centre + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * Radius);
		Colours.Add(ColourEnd != nullptr ? FMath::Lerp(Colour, *ColourEnd, T) : Colour);
	}

	ColouredPolyline(Points, Colours, Thickness);
}

void FOneBrahamPainter::Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Colour)
{
	if (Colour.A * Opacity <= 0.002f || !FSlateApplication::IsInitialized())
	{
		return;
	}

	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	const FColor Packed = Tint(Colour).ToFColorSRGB();

	TArray<FSlateVertex> Verts;
	Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(A), FVector2f(0.f, 0.f), Packed));
	Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(B), FVector2f(0.f, 0.f), Packed));
	Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(C), FVector2f(0.f, 0.f), Packed));
	TArray<SlateIndex> Indices = { 0, 1, 2 };

	static const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
	FSlateDrawElement::MakeCustomVerts(*Out, Next(), Handle, Verts, Indices, nullptr, 0, 0);
}

void FOneBrahamPainter::Chevron(const FVector2D& Centre, const FVector2D& Dir, float Size, const FLinearColor& Colour, float Thickness)
{
	const FVector2D D = Dir.GetSafeNormal();
	const FVector2D N(-D.Y, D.X);
	const float Half = Size * 0.5f;
	const FVector2D Tip = Centre + D * Half * 0.5f;
	const FVector2D Back = Centre - D * Half * 0.5f;
	Polyline({ Back + N * Half, Tip, Back - N * Half }, Colour, Thickness);
}

void FOneBrahamPainter::Diamond(const FVector2D& Centre, float Size, const FLinearColor& Colour, bool bFilled)
{
	const float H = Size * 0.5f;
	const FVector2D Top = Centre + FVector2D(0.f, -H);
	const FVector2D Right = Centre + FVector2D(H, 0.f);
	const FVector2D Bottom = Centre + FVector2D(0.f, H);
	const FVector2D Left = Centre + FVector2D(-H, 0.f);
	if (bFilled)
	{
		Triangle(Top, Right, Bottom, Colour);
		Triangle(Top, Bottom, Left, Colour);
	}
	else
	{
		Polyline({ Top, Right, Bottom, Left, Top }, Colour, 1.5f);
	}
}

void FOneBrahamPainter::Cross(const FVector2D& Centre, float Size, const FLinearColor& Colour, float Thickness)
{
	const float H = Size * 0.5f;
	Segment(Centre + FVector2D(-H, -H), Centre + FVector2D(H, H), Colour, Thickness);
	Segment(Centre + FVector2D(-H, H), Centre + FVector2D(H, -H), Colour, Thickness);
}

FVector2D FOneBrahamPainter::Text(const FVector2D& Pos, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour,
	EOneBrahamTextAnchor Anchor)
{
	if (String.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D TextSize = OneBrahamHUDStyle::Measure(String, Font);
	FVector2D At = Pos;
	if (Anchor == EOneBrahamTextAnchor::Centre)
	{
		At.X -= TextSize.X * 0.5f;
	}
	else if (Anchor == EOneBrahamTextAnchor::Right)
	{
		At.X -= TextSize.X;
	}

	if (Colour.A * Opacity > 0.002f)
	{
		// The outline fades with the text: a ghost outline around nothing is the
		// most common artefact of a hand-painted HUD.
		FSlateFontInfo Faded = Font;
		if (Faded.OutlineSettings.OutlineSize > 0)
		{
			Faded.OutlineSettings.OutlineColor.A *= FMath::Clamp(Colour.A * Opacity, 0.f, 1.f);
		}
		FSlateDrawElement::MakeText(*Out, Next(), Paint(At, TextSize + FVector2D(4.f, 0.f)), OneBrahamHUDStyle::Tracked(String, Font),
			Faded, ESlateDrawEffect::None, Tint(Colour));
	}

	return TextSize;
}

FVector2D FOneBrahamPainter::TextWipe(const FVector2D& Pos, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour,
	float Reveal01, EOneBrahamTextAnchor Anchor)
{
	const FVector2D TextSize = OneBrahamHUDStyle::Measure(String, Font);
	if (Reveal01 <= 0.f)
	{
		return TextSize;
	}
	if (Reveal01 >= 1.f)
	{
		return Text(Pos, String, Font, Colour, Anchor);
	}

	FVector2D At = Pos;
	if (Anchor == EOneBrahamTextAnchor::Centre)		{ At.X -= TextSize.X * 0.5f; }
	else if (Anchor == EOneBrahamTextAnchor::Right)	{ At.X -= TextSize.X; }

	PushClip(At - FVector2D(4.f, 4.f), FVector2D(TextSize.X * Reveal01 + 4.f, TextSize.Y + 8.f));
	Text(At, String, Font, Colour, EOneBrahamTextAnchor::Left);
	PopClip();
	return TextSize;
}

void FOneBrahamPainter::PushClip(const FVector2D& Pos, const FVector2D& Size)
{
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	const FVector2f TL = Transform.TransformPoint(FVector2f(Pos));
	const FVector2f TR = Transform.TransformPoint(FVector2f(Pos.X + Size.X, Pos.Y));
	const FVector2f BL = Transform.TransformPoint(FVector2f(Pos.X, Pos.Y + Size.Y));
	const FVector2f BR = Transform.TransformPoint(FVector2f(Pos + Size));
	Out->PushClip(FSlateClippingZone(TL, TR, BL, BR));
}

void FOneBrahamPainter::PopClip()
{
	Out->PopClip();
}
