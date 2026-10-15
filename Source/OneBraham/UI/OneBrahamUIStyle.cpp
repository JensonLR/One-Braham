// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamUIStyle.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Internationalization/Text.h"

namespace
{
	/** Fallback palette. These exact values are mirrored in DefaultGame.ini. */
	const FLinearColor FallbackNavy(0.016f, 0.063f, 0.145f, 1.f);
	const FLinearColor FallbackLightGrey(0.878f, 0.898f, 0.922f, 1.f);
	const FLinearColor FallbackSignalBlue(0.078f, 0.522f, 1.0f, 1.f);
	const FLinearColor FallbackSafetyYellow(1.0f, 0.804f, 0.0f, 1.f);
	const FLinearColor FallbackEmergencyRed(0.867f, 0.118f, 0.122f, 1.f);

	/** Maps the type scale to point sizes. */
	int32 SizeToPoints(EOneBrahamTextSize Size)
	{
		switch (Size)
		{
		case EOneBrahamTextSize::Micro:	return 10;
		case EOneBrahamTextSize::Small:	return 12;
		case EOneBrahamTextSize::Medium:	return 15;
		case EOneBrahamTextSize::Large:	return 22;
		case EOneBrahamTextSize::Header:	return 30;
		case EOneBrahamTextSize::Display:	return 46;
		default:							return 14;
		}
	}
}

// ==========================================================================
//  Palette
// ==========================================================================
FLinearColor UOneBrahamUIStyle::Navy()			{ return UOneBrahamGameSettings::Get().BrandNavy; }
FLinearColor UOneBrahamUIStyle::LightGrey()		{ return UOneBrahamGameSettings::Get().BrandLightGrey; }
FLinearColor UOneBrahamUIStyle::SignalBlue()	{ return UOneBrahamGameSettings::Get().BrandSignalBlue; }
FLinearColor UOneBrahamUIStyle::SafetyYellow()	{ return UOneBrahamGameSettings::Get().BrandSafetyYellow; }
FLinearColor UOneBrahamUIStyle::EmergencyRed()	{ return UOneBrahamGameSettings::Get().BrandEmergencyRed; }

FLinearColor UOneBrahamUIStyle::SuccessGreen()
{
	// A restrained green, deliberately darker than a "gamer" green so it sits in
	// the same corporate palette as the rest of the interface.
	return FLinearColor(0.196f, 0.612f, 0.376f, 1.f);
}

FLinearColor UOneBrahamUIStyle::Crosshair()
{
	return FLinearColor(0.96f, 0.98f, 1.f, 1.f);
}

FLinearColor UOneBrahamUIStyle::Invert(const FLinearColor& Colour)
{
	return FLinearColor(1.f - Colour.R, 1.f - Colour.G, 1.f - Colour.B, Colour.A);
}

FLinearColor UOneBrahamUIStyle::Muted(const FLinearColor& Colour, float Amount)
{
	const float Clamped = FMath::Clamp(Amount, 0.f, 1.f);
	const float Luma = Colour.GetLuminance();
	const FLinearColor Desaturated(Luma, Luma, Luma, Colour.A);
	const FLinearColor Result = FMath::Lerp(Colour, Desaturated, Clamped);
	return FLinearColor(Result.R * 0.75f, Result.G * 0.75f, Result.B * 0.75f, Result.A);
}

FLinearColor UOneBrahamUIStyle::HealthColour(float Fraction)
{
	const float Clamped = FMath::Clamp(Fraction, 0.f, 1.f);

	if (Clamped > 0.6f)
	{
		// Full health is neutral light grey: the HUD does not need to shout when
		// nothing is wrong, which is why the palette only lights up when it matters.
		return FMath::Lerp(SafetyYellow(), LightGrey(), (Clamped - 0.6f) / 0.4f);
	}

	return FMath::Lerp(EmergencyRed(), SafetyYellow(), Clamped / 0.6f);
}

FLinearColor UOneBrahamUIStyle::TeamColour(int32 Team)
{
	// FIRE DRILL: Internal Staff (0) are signal blue, External Contractors (1) are
	// safety yellow. Deliberately not red/blue: red is reserved for alarms and
	// damage, so no team uses it.
	return Team == 0 ? SignalBlue() : SafetyYellow();
}

// ==========================================================================
//  Brushes
// ==========================================================================
FSlateBrush UOneBrahamUIStyle::PanelBrush(float Alpha, float CornerRadius)
{
	FSlateBrush Brush;
	const FLinearColor Colour(Navy().R, Navy().G, Navy().B, FMath::Clamp(Alpha, 0.f, 1.f));

	Brush.SetResourceObject(nullptr);
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Colour);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius));
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	Brush.OutlineSettings.Width = 0.f;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.ImageSize = FVector2D(32.f, 32.f);

	return Brush;
}

FSlateBrush UOneBrahamUIStyle::OutlineBrush(const FLinearColor& Colour, float Thickness, float CornerRadius)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Colour);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(
		FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius));
	Brush.OutlineSettings.Color = FSlateColor(Colour);
	Brush.OutlineSettings.Width = FMath::Max(0.5f, Thickness);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.ImageSize = FVector2D(32.f, 32.f);

	return Brush;
}

FSlateBrush UOneBrahamUIStyle::SolidBrush(const FLinearColor& Colour)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Box;
	Brush.TintColor = FSlateColor(Colour);
	Brush.ImageSize = FVector2D(4.f, 4.f);

	return Brush;
}

FProgressBarStyle UOneBrahamUIStyle::ProgressBarStyle(const FLinearColor& FillColour, float Thickness, bool bRounded)
{
	FProgressBarStyle Style;

	Style.SetBackgroundImage(PanelBrush(0.55f, bRounded ? Thickness * 0.5f : 0.f));
	Style.SetFillImage(SolidBrush(FillColour));
	Style.SetMarqueeImage(SolidBrush(FillColour));

	Style.EnableFillAnimation = false;

	return Style;
}

// ==========================================================================
//  Type
// ==========================================================================
FSlateFontInfo UOneBrahamUIStyle::Font(EOneBrahamTextSize Size, bool bBold)
{
	// The shipped font is "One Braham Sans" (see Docs/07_UI.md for the licence and
	// where to drop the .ttf). Until it is imported we use the engine's Roboto,
	// which has the correct corporate neutrality to be a believable placeholder -
	// and, critically, ships with the engine so a clean checkout still renders.
	static const FSoftObjectPath BrandFontPath(TEXT("/Game/OneBraham/UI/Fonts/F_OneBrahamSans.F_OneBrahamSans"));

	UObject* BrandFont = BrandFontPath.ResolveObject();
	if (BrandFont == nullptr)
	{
		static bool bLoggedFontFallback = false;
		if (!bLoggedFontFallback)
		{
			bLoggedFontFallback = true;
			UE_LOG(LogOneBrahamUI, Log,
				TEXT("[OB.UI] Brand font not imported; using the engine font. "
					 "See Docs/07_UI.md for the import steps."));
		}
	}

	FSlateFontInfo Info;
	Info.FontObject = BrandFont;
	Info.Size = SizeToPoints(Size);
	Info.TypefaceFontName = bBold ? TEXT("Bold") : TEXT("Regular");
	// 1% tracking on the big type, none below it. Written as a list because a
	// scoped enum has no >=, which is what the previous version of this line
	// asked it for.
	const bool bTracked = Size == EOneBrahamTextSize::Header ||
		Size == EOneBrahamTextSize::Display;
	Info.LetterSpacing = bTracked ? 100 : 0;

	return Info;
}

FSlateFontInfo UOneBrahamUIStyle::FontWithOutline(EOneBrahamTextSize Size, const FLinearColor& OutlineColour,
	int32 OutlineSize)
{
	FSlateFontInfo Info = Font(Size);
	Info.OutlineSettings = FFontOutlineSettings(FMath::Max(0, OutlineSize));
	Info.OutlineSettings.OutlineColor = OutlineColour;
	Info.OutlineSettings.bSeparateFillAlpha = false;

	return Info;
}

FText UOneBrahamUIStyle::HeaderLabel(const FText& InText)
{
	// Building wayfinding is set in caps. Uppercasing in code rather than in the
	// string table means localisers can still control the casing if they need to.
	return InText.ToUpper();
}

FText UOneBrahamUIStyle::DistanceLabel(float DistanceCm, int32 FloorDelta)
{
	if (FloorDelta != 0)
	{
		// Vertical separation is more useful than straight-line distance in a tower.
		return FText::Format(NSLOCTEXT("OneBraham", "HUD_FloorDelta", "{0} FL"),
			FText::AsNumber(FMath::Abs(FloorDelta)));
	}

	const float Metres = DistanceCm / 100.f;

	if (Metres < 100.f)
	{
		return FText::Format(NSLOCTEXT("OneBraham", "HUD_Metres", "{0} m"),
			FText::AsNumber(FMath::RoundToInt(Metres)));
	}

	// Past 100 m the number stops being useful and starts being noise.
	return NSLOCTEXT("OneBraham", "HUD_Far", "FAR");
}

FText UOneBrahamUIStyle::ClockLabel(float SecondsRemaining)
{
	const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt(SecondsRemaining));
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;

	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Minutes, Seconds));
}

FText UOneBrahamUIStyle::FloorLabel(int32 FloorIndex)
{
	// The tower's floor identities are fixed by the map spec: the lobby is the
	// ground floor, the canteen is 15 and the Skydeck is the roof of the canteen.
	switch (FloorIndex)
	{
	case 0:		return NSLOCTEXT("OneBraham", "Floor_Plaza", "G - GROUND PLAZA");
	case 1:		return NSLOCTEXT("OneBraham", "Floor_Lobby", "1 - LOBBY");
	case 2:		return NSLOCTEXT("OneBraham", "Floor_Mezzanine", "2 - LIFT LOBBY");
	case 5:		return NSLOCTEXT("OneBraham", "Floor_Office", "5 - OPEN PLAN OFFICE");
	case 9:		return NSLOCTEXT("OneBraham", "Floor_Server", "9 - SERVER / COMMUNICATIONS");
	case 12:	return NSLOCTEXT("OneBraham", "Floor_Executive", "12 - EXECUTIVE");
	case 13:	return NSLOCTEXT("OneBraham", "Floor_Executive13", "13 - EXECUTIVE");
	case 14:	return NSLOCTEXT("OneBraham", "Floor_Executive14", "14 - EXECUTIVE");
	case 15:	return NSLOCTEXT("OneBraham", "Floor_Skydeck", "15 - CANTEEN / SKYDECK");
	case 16:	return NSLOCTEXT("OneBraham", "Floor_Summit", "16 - MANAGEMENT SUITE");
	case 17:	return NSLOCTEXT("OneBraham", "Floor_WinterGarden", "17 - WINTER GARDEN");
	default:
		return FText::Format(NSLOCTEXT("OneBraham", "Floor_Generic", "FL {0}"), FText::AsNumber(FloorIndex));
	}
}
