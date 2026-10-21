// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamUIStyle.h"

#include "Styling/CoreStyle.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Internationalization/Text.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Misc/DateTime.h"

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
	const FLinearColor PanelTint(Navy().R, Navy().G, Navy().B, FMath::Clamp(Alpha, 0.f, 1.f));

	Brush.SetResourceObject(nullptr);
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(PanelTint);
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
FSlateFontInfo UOneBrahamUIStyle::Font(EOneBrahamTextSize Size, bool bBold, int32 TrackingThousandths)
{
	// The old type scale, kept as a wrapper while screens migrate to the
	// tokens: bold is the HUD label face, regular the body face, at the old
	// point sizes (a point is 4/3 of a pixel at Slate's 96 DPI).
	const float Px = static_cast<float>(SizeToPoints(Size)) * (4.f / 3.f);
	const bool bTracked = Size == EOneBrahamTextSize::Header || Size == EOneBrahamTextSize::Display;
	return TypeFont(bBold ? EOneBrahamType::Label : EOneBrahamType::Body, Px,
		TrackingThousandths != 0 ? TrackingThousandths : (bTracked ? 60 : 0));
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


// ==========================================================================
//  The design system
// ==========================================================================
namespace OneBrahamType
{
	/** One face: the imported asset, and the engine Roboto weight standing in for it. */
	struct FFace
	{
		const TCHAR* Asset;
		const TCHAR* Fallback;
	};

	const FFace& FaceFor(EOneBrahamType Token)
	{
		// Asset names are build_frontend.import_fonts' (FF_ + the file name).
		static const FFace Faces[] =
		{
			{ TEXT("FF_ArchivoExpanded_Black"),			TEXT("Black") },		// Title
			{ TEXT("FF_ArchivoExpanded_Bold"),			TEXT("Bold") },			// Verb
			{ TEXT("FF_ArchivoSemiCondensed_Bold"),		TEXT("Bold") },			// Label
			{ TEXT("FF_ArchivoSemiCondensed_SemiBold"),	TEXT("Bold") },			// Eyebrow
			{ TEXT("FF_ArchivoSemiCondensed_Medium"),	TEXT("Regular") },		// Hint
			{ TEXT("FF_ArchivoSemiCondensed_Regular"),	TEXT("Regular") },		// Caption
			{ TEXT("FF_Archivo_Regular"),				TEXT("Regular") },		// Body
			{ TEXT("FF_IBMPlexMono_Medium"),			TEXT("Regular") },		// Telemetry
			{ TEXT("FF_IBMPlexMono_SemiBold"),			TEXT("Bold") },			// TelemetryStrong
			{ TEXT("FF_IBMPlexMono_Regular"),			TEXT("Regular") },		// TelemetryLight
			{ TEXT("FF_Doto_Bold"),						TEXT("Bold") },			// LED
			{ TEXT("FF_Doto_Medium"),					TEXT("Regular") },		// LEDMedium
			{ TEXT("FF_InstrumentSerif_Italic"),		TEXT("Italic") },		// Aside
		};
		static_assert(UE_ARRAY_COUNT(Faces) == static_cast<int32>(EOneBrahamType::Aside) + 1, "one face per token");
		return Faces[FMath::Clamp(static_cast<int32>(Token), 0, UE_ARRAY_COUNT(Faces) - 1)];
	}

	/**
	 * The composite font for a token: one typeface, one face asset. Built once
	 * per token and kept for the life of the process; FStandaloneCompositeFont
	 * reports its face to the garbage collector, so the asset stays loaded
	 * while any FSlateFontInfo still points at it.
	 */
	TSharedPtr<const FCompositeFont> CompositeFor(EOneBrahamType Token)
	{
		static TMap<EOneBrahamType, TSharedPtr<const FCompositeFont>> Cache;
		static TSet<EOneBrahamType> Missing;

		if (const TSharedPtr<const FCompositeFont>* Found = Cache.Find(Token))
		{
			return *Found;
		}
		if (Missing.Contains(Token))
		{
			return nullptr;
		}

		const FFace& Face = FaceFor(Token);
		const FString Path = FString::Printf(TEXT("/Game/OneBraham/UI/Fonts/%s.%s"), Face.Asset, Face.Asset);
		const UFontFace* Asset = LoadObject<UFontFace>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Asset == nullptr)
		{
			Missing.Add(Token);
			static bool bLogged = false;
			if (!bLogged)
			{
				bLogged = true;
				UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] Brand faces not imported (%s missing); using the engine font. "
					"Run Scripts/BuildContent.sh --only frontend."), *Path);
			}
			return nullptr;
		}

		TSharedRef<FStandaloneCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>();
		FTypefaceEntry& Entry = Composite->DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TEXT("Regular");
		Entry.Font = FFontData(Asset);
		// Glyphs a face lacks (an arrow in Archivo, a box-drawing rule) come from
		// the engine's Roboto rather than from the last-resort font's boxes.
		const FSlateFontInfo Engine = FCoreStyle::GetDefaultFontStyle(Face.Fallback, 12);
		if (Engine.CompositeFont.IsValid())
		{
			Composite->FallbackTypeface.Typeface = Engine.CompositeFont->DefaultTypeface;
		}
		Cache.Add(Token, Composite);
		return Composite;
	}
}

FSlateFontInfo UOneBrahamUIStyle::TypeFont(EOneBrahamType Token, float Px, int32 TrackingThousandths, bool bOutline)
{
	// Sizes are pixels at 1080p; Slate sizes are points at 96 DPI.
	const float Points = FMath::Max(1.f, Px * 0.75f);

	FSlateFontInfo Info;
	if (const TSharedPtr<const FCompositeFont> Composite = OneBrahamType::CompositeFor(Token))
	{
		Info = FSlateFontInfo(Composite, Points, TEXT("Regular"));
	}
	else
	{
		Info = FCoreStyle::GetDefaultFontStyle(OneBrahamType::FaceFor(Token).Fallback, FMath::RoundToInt(Points));
		Info.Size = Points;
	}

	Info.LetterSpacing = TrackingThousandths;
	if (bOutline)
	{
		Info.OutlineSettings = FFontOutlineSettings(1);
		Info.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);
		Info.OutlineSettings.bSeparateFillAlpha = true;
	}
	return Info;
}

bool UOneBrahamUIStyle::HasBrandFaces()
{
	return OneBrahamType::CompositeFor(EOneBrahamType::Body).IsValid();
}

FLinearColor UOneBrahamUIStyle::Hex(const TCHAR* InHex, float Alpha)
{
	FLinearColor Result = FLinearColor::FromSRGBColor(FColor::FromHex(InHex));
	Result.A = Alpha;
	return Result;
}

FLinearColor UOneBrahamUIStyle::FromBrandFloat(const FLinearColor& IniColour, float Alpha)
{
	// The ini floats are the hex divided by 255: quantise back to the byte and
	// read it as sRGB, which is what the brand book meant.
	FLinearColor Result = FLinearColor::FromSRGBColor(IniColour.ToFColor(/*bSRGB*/ false));
	Result.A = Alpha;
	return Result;
}

FLinearColor UOneBrahamUIStyle::Colour(EOneBrahamColour Token, float Alpha)
{
	const UOneBrahamGameSettings& Settings = UOneBrahamGameSettings::Get();
	switch (Token)
	{
	case EOneBrahamColour::Primary:			return FromBrandFloat(Settings.BrandPrimary, Alpha);
	case EOneBrahamColour::Navy:			return FromBrandFloat(Settings.BrandNavy, Alpha);
	case EOneBrahamColour::SignalBlue:		return FromBrandFloat(Settings.BrandSignalBlue, Alpha);
	case EOneBrahamColour::LightGrey:		return FromBrandFloat(Settings.BrandLightGrey, Alpha);
	case EOneBrahamColour::SafetyYellow:
	{
		// Tritanopia cannot separate yellow from white-ish greys reliably; the
		// spec swaps it to white, and the shapes (hatching) carry the meaning.
		const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		if (User != nullptr && User->ColourBlindMode == EOneBrahamColourBlindMode::Tritanopia)
		{
			return Hex(TEXT("FFFFFF"), Alpha);
		}
		return FromBrandFloat(Settings.BrandSafetyYellow, Alpha);
	}
	case EOneBrahamColour::EmergencyRed:
	{
		// Deuteranopia and protanopia lose red against dark navy: danger becomes
		// a hot orange there. Only this hue moves; every state keeps its shape.
		const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		if (User != nullptr && (User->ColourBlindMode == EOneBrahamColourBlindMode::Deuteranopia
			|| User->ColourBlindMode == EOneBrahamColourBlindMode::Protanopia))
		{
			return Hex(TEXT("FF6B00"), Alpha);
		}
		return FromBrandFloat(Settings.BrandEmergencyRed, Alpha);
	}
	case EOneBrahamColour::OLEDBlack:		return Hex(TEXT("000000"), Alpha);
	case EOneBrahamColour::BaffleBlack:		return Hex(TEXT("0B0C0F"), Alpha);
	case EOneBrahamColour::SignageWhite:	return Hex(TEXT("F4F6F8"), Alpha);
	case EOneBrahamColour::Paper:			return Hex(TEXT("E9ECEF"), Alpha);
	case EOneBrahamColour::RibbonA:			return Hex(TEXT("6A1BFF"), Alpha);
	case EOneBrahamColour::RibbonB:			return Hex(TEXT("1485FF"), Alpha);
	case EOneBrahamColour::LockdownFieldA:	return Hex(TEXT("FFCD00"), Alpha);
	case EOneBrahamColour::LockdownFieldB:	return Hex(TEXT("B37400"), Alpha);
	case EOneBrahamColour::PurgeFieldA:		return Hex(TEXT("DD1E1F"), Alpha);
	case EOneBrahamColour::PurgeFieldB:		return Hex(TEXT("5A0A0C"), Alpha);
	case EOneBrahamColour::LCDField:		return Hex(TEXT("12160F"), Alpha);
	case EOneBrahamColour::LCDInk:			return Hex(TEXT("D8F27A"), Alpha);
	case EOneBrahamColour::FireHousing:		return Hex(TEXT("8F1418"), Alpha);
	case EOneBrahamColour::WarningAmber:
	case EOneBrahamColour::WarningFieldA:
	{
		// Red-green colour blindness moves the danger hue to #FF6B00, and to
		// those eyes that orange and this amber are one colour: the warning
		// would read as the Purge. There the warning is a pale cream, which
		// they separate from the yellow, the orange and the cyan way out.
		const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		if (User != nullptr && (User->ColourBlindMode == EOneBrahamColourBlindMode::Deuteranopia
			|| User->ColourBlindMode == EOneBrahamColourBlindMode::Protanopia))
		{
			return Hex(TEXT("FFF2B3"), Alpha);
		}
		return Hex(TEXT("FF9F1C"), Alpha);
	}
	case EOneBrahamColour::WarningFieldB:	return Hex(TEXT("A85400"), Alpha);
	case EOneBrahamColour::EvacGreen:
	case EOneBrahamColour::EvacFieldA:
	{
		// Red-green colour blindness takes the exit green into the amber and
		// the yellow: there the way out is a cyan, which every one of those
		// eyes still separates from the warm stage colours.
		const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		if (User != nullptr && (User->ColourBlindMode == EOneBrahamColourBlindMode::Deuteranopia
			|| User->ColourBlindMode == EOneBrahamColourBlindMode::Protanopia))
		{
			return Hex(TEXT("00C8D7"), Alpha);
		}
		// Tritanopia sees the exit green and the calm's signal blue as one
		// teal: there the way out is a brighter spring green, apart from both.
		if (User != nullptr && User->ColourBlindMode == EOneBrahamColourBlindMode::Tritanopia)
		{
			return Hex(TEXT("00FF7F"), Alpha);
		}
		return Hex(TEXT("00A651"), Alpha);
	}
	case EOneBrahamColour::EvacFieldB:		return Hex(TEXT("005C2D"), Alpha);
	default:								return Hex(TEXT("FFFFFF"), Alpha);
	}
}

// ==========================================================================
//  The stage colour rule: one function, every surface
// ==========================================================================
EOneBrahamStageTone UOneBrahamUIStyle::StageTone(EOneBrahamLockdownStage Stage, bool bEvacuating)
{
	// The Purge outranks the way out: while the front is chasing you the
	// screen stays red, extraction or not.
	if (Stage == EOneBrahamLockdownStage::Purge)
	{
		return EOneBrahamStageTone::Purge;
	}
	if (bEvacuating)
	{
		return EOneBrahamStageTone::Evac;
	}
	switch (Stage)
	{
	case EOneBrahamLockdownStage::PreLockdown:	return EOneBrahamStageTone::Warning;
	case EOneBrahamLockdownStage::Lockdown:		return EOneBrahamStageTone::Lockdown;
	case EOneBrahamLockdownStage::Normal:
	default:									return EOneBrahamStageTone::Calm;
	}
}

FLinearColor UOneBrahamUIStyle::ToneColour(EOneBrahamStageTone Tone, float Alpha)
{
	switch (Tone)
	{
	case EOneBrahamStageTone::Warning:	return Colour(EOneBrahamColour::WarningAmber, Alpha);
	case EOneBrahamStageTone::Lockdown:	return Colour(EOneBrahamColour::SafetyYellow, Alpha);
	case EOneBrahamStageTone::Purge:	return Colour(EOneBrahamColour::EmergencyRed, Alpha);
	case EOneBrahamStageTone::Evac:		return Colour(EOneBrahamColour::EvacGreen, Alpha);
	case EOneBrahamStageTone::Calm:
	default:							return Colour(EOneBrahamColour::SignalBlue, Alpha);
	}
}

FLinearColor UOneBrahamUIStyle::ToneHairline(EOneBrahamStageTone Tone)
{
	switch (Tone)
	{
	case EOneBrahamStageTone::Warning:	return ToneColour(Tone, 0.25f);
	case EOneBrahamStageTone::Lockdown:	return ToneColour(Tone, 0.25f);
	case EOneBrahamStageTone::Purge:	return ToneColour(Tone, 0.30f);
	case EOneBrahamStageTone::Evac:		return ToneColour(Tone, 0.30f);
	case EOneBrahamStageTone::Calm:
	default:							return Colour(EOneBrahamColour::LightGrey, 0.20f);
	}
}

FLinearColor UOneBrahamUIStyle::ToneInk(EOneBrahamStageTone Tone)
{
	// Amber and yellow are light: navy type, as on a hazard sign. Red, green
	// and the ribbon are dark enough for white, as on an exit sign. Decided by
	// the tone's own lightness rather than by name, so a colour-blind remap
	// that lightens a tone (the hot-orange danger, the cyan or spring-green
	// way out, the cream warning) takes navy type with it instead of white on
	// a pale field. Exit green (0.28) stays under the line: white, as designed.
	if (Tone == EOneBrahamStageTone::Calm)
	{
		return Colour(EOneBrahamColour::White);	// the field is the dark ribbon
	}
	const FLinearColor Lead = ToneColour(Tone);
	const float Luminance = 0.2126f * Lead.R + 0.7152f * Lead.G + 0.0722f * Lead.B;
	return Luminance > 0.30f ? Colour(EOneBrahamColour::Navy) : Colour(EOneBrahamColour::White);
}

void UOneBrahamUIStyle::ToneField(EOneBrahamStageTone Tone, FLinearColor& OutA, FLinearColor& OutB)
{
	switch (Tone)
	{
	case EOneBrahamStageTone::Warning:
		OutA = Colour(EOneBrahamColour::WarningFieldA);
		OutB = Colour(EOneBrahamColour::WarningFieldB);
		break;
	case EOneBrahamStageTone::Lockdown:
		OutA = Colour(EOneBrahamColour::LockdownFieldA);
		OutB = Colour(EOneBrahamColour::LockdownFieldB);
		break;
	case EOneBrahamStageTone::Purge:
		// The field's lead colour is the tone itself, so the colour-blind
		// remap of the danger hue reaches the cards as well as the type.
		OutA = ToneColour(Tone);
		OutB = Colour(EOneBrahamColour::PurgeFieldB);
		break;
	case EOneBrahamStageTone::Evac:
		OutA = ToneColour(Tone);
		OutB = Colour(EOneBrahamColour::EvacFieldB);
		break;
	case EOneBrahamStageTone::Calm:
	default:
		OutA = Colour(EOneBrahamColour::RibbonA);
		OutB = Colour(EOneBrahamColour::RibbonB);
		break;
	}
}

const TCHAR* UOneBrahamUIStyle::ToneWord(EOneBrahamStageTone Tone)
{
	switch (Tone)
	{
	case EOneBrahamStageTone::Warning:	return TEXT("ALERT");
	case EOneBrahamStageTone::Lockdown:	return TEXT("LOCKDOWN");
	case EOneBrahamStageTone::Purge:	return TEXT("PURGE");
	case EOneBrahamStageTone::Evac:		return TEXT("EVAC");
	case EOneBrahamStageTone::Calm:
	default:							return TEXT("NORMAL");
	}
}

FLinearColor UOneBrahamUIStyle::StageAccent(EOneBrahamLockdownStage Stage)
{
	return ToneColour(StageTone(Stage));
}

FLinearColor UOneBrahamUIStyle::StageHairline(EOneBrahamLockdownStage Stage)
{
	return ToneHairline(StageTone(Stage));
}

void UOneBrahamUIStyle::StageField(EOneBrahamLockdownStage Stage, FLinearColor& OutA, FLinearColor& OutB)
{
	ToneField(StageTone(Stage), OutA, OutB);
}

float UOneBrahamUIStyle::Ease(EOneBrahamEase Curve, float T)
{
	const float X = FMath::Clamp(T, 0.f, 1.f);
	switch (Curve)
	{
	case EOneBrahamEase::Lift:
		return X < 0.5f ? 4.f * X * X * X : 1.f - FMath::Pow(-2.f * X + 2.f, 3.f) * 0.5f;
	case EOneBrahamEase::Ribbon:
		// Expo out: arrives fast and settles, the shape of cubic-bezier(0.16,1,0.3,1).
		return X >= 1.f ? 1.f : 1.f - FMath::Pow(2.f, -10.f * X);
	case EOneBrahamEase::Exit:
		return X * X * X;
	case EOneBrahamEase::Reader:
	{
		const float C1 = 1.2f;
		const float C3 = C1 + 1.f;
		return 1.f + C3 * FMath::Pow(X - 1.f, 3.f) + C1 * FMath::Pow(X - 1.f, 2.f);
	}
	default:
		return X;
	}
}

float UOneBrahamUIStyle::Tween(double NowSeconds, double StartSeconds, float DurationSeconds, EOneBrahamEase Curve)
{
	if (DurationSeconds <= 0.f)
	{
		return NowSeconds >= StartSeconds ? 1.f : 0.f;
	}
	return Ease(Curve, static_cast<float>((NowSeconds - StartSeconds) / DurationSeconds));
}

bool UOneBrahamUIStyle::ReduceMotion()
{
	const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	return User != nullptr && User->bReduceMotion;
}

FText UOneBrahamUIStyle::LocalClock(bool bSeconds)
{
	const FDateTime Now = FDateTime::Now();
	return FText::FromString(bSeconds
		? FString::Printf(TEXT("%02d:%02d:%02d"), Now.GetHour(), Now.GetMinute(), Now.GetSecond())
		: FString::Printf(TEXT("%02d:%02d"), Now.GetHour(), Now.GetMinute()));
}
