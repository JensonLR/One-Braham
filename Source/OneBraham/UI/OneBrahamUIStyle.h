// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamUIStyle.h
// ---------------------------------------------------------------------------
// ONE BRAHAM's interface style, expressed in code.
//
// WHY IN CODE
//   Every brush here is generated at runtime from the brand palette (see
//   UOneBrahamGameSettings) using rounded-box brushes, so the entire UI has a
//   consistent, non-default look with ZERO texture assets. A designer can then
//   override any single element in Blueprint without invalidating the rest.
//
// BRAND RULES (do not break these)
//   * Deep navy background, light grey text, signal blue for the player's own
//     information, safety yellow for warnings, emergency red for hostile alarms.
//   * Flat, corporate, faintly cold. No gradients-for-the-sake-of-it, no
//     drop shadows, no comic sans of any description.
//   * The UI reads like building wayfinding: it is a sign, in a lobby, made by
//     somebody in brand compliance.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamUIStyle.generated.h"

/**
 * The type system's faces (Docs/08, "THE BUILDING SPEAKS"). Four open-licence
 * families, each in a fixed role:
 *   Archivo        Title (Expanded Black), Verb (Expanded Bold), Label
 *                  (SemiCondensed Bold), Eyebrow (SemiCondensed SemiBold),
 *                  Hint (SemiCondensed Medium), Caption (SemiCondensed
 *                  Regular), Body (Regular). Never italic.
 *   IBM Plex Mono  every number that changes: Telemetry (Medium),
 *                  TelemetryStrong (SemiBold), TelemetryLight (Regular)
 *   Doto           LED (Bold) and LEDMedium: only what the building shows on
 *                  an LED panel, and never below 28 px
 *   Instrument Serif Italic   Aside: the building's own voice, sentence case
 */
UENUM(BlueprintType)
enum class EOneBrahamType : uint8
{
	Title,
	Verb,
	Label,
	Eyebrow,
	Hint,
	Caption,
	Body,
	Telemetry,
	TelemetryStrong,
	TelemetryLight,
	LED,
	LEDMedium,
	Aside
};

/**
 * Colour tokens, as the eye sees them. The brand values in DefaultEngine.ini
 * are sRGB intent stored in FLinearColor fields; read as linear they wash out
 * (the #22476A slab the first menu was). Colour() converts once, so every
 * token here is the hex in the design spec, on screen.
 */
UENUM(BlueprintType)
enum class EOneBrahamColour : uint8
{
	/** #0070C0: the scoreboard accent bar, the team channel. */
	Primary,
	/** #041025: HUD plates, panels, text on yellow and paper. */
	Navy,
	/** #1485FF: focus, "yours", squad, pings, prompts. */
	SignalBlue,
	/** #FFCD00: Lockdown, sealing, extraction, low ammo, LED counters. */
	SafetyYellow,
	/** #DD1E1F: damage, critical, downed, Purge. Honours the colour-blind mode. */
	EmergencyRed,
	/** #E0E5EB: secondary text; hairlines at 8/12/20/35%. */
	LightGrey,
	/** #000000: letterbox, loading, a screen switched off. */
	OLEDBlack,
	/** #0B0C0F: the scoreboard, the fire panel's face. */
	BaffleBlack,
	/** #F4F6F8: the floor plate, inverted rows, the badge card. */
	SignageWhite,
	/** #E9ECEF: the accident book. */
	Paper,
	/** #6A1BFF -> #1485FF: the ribbon, only on things the building lights. */
	RibbonA,
	RibbonB,
	/** #FFCD00 -> #B37400 and #DD1E1F -> #5A0A0C: the stage fields. */
	LockdownFieldA,
	LockdownFieldB,
	PurgeFieldA,
	PurgeFieldB,
	/** #12160F / #D8F27A: the fire panel's LCD strip only. */
	LCDField,
	LCDInk,
	/** #8F1418: the fire panel's housing at rest. */
	FireHousing,
	/** #FFFFFF: text and one-frame flashes. */
	White
};

/** The motion curves (Docs/08). One library, so every screen moves alike. */
UENUM(BlueprintType)
enum class EOneBrahamEase : uint8
{
	/** Ease-in-out cubic: camera blends, panels opening. */
	Lift,
	/** Ease-out expo, cubic-bezier(0.16,1,0.3,1): entries, underlines, wipes. */
	Ribbon,
	/** Ease-in cubic, cubic-bezier(0.32,0,0.67,0): exits, ~0.6x the entry. */
	Exit,
	/** Ease-out back 1.2: prompt success only. */
	Reader,
	/** Counters, stage cross-fades, data rolls. */
	Linear
};

/** Type scale. Fixed sizes, no auto-fitting, because fitting is what makes UI look cheap. */
UENUM(BlueprintType)
enum class EOneBrahamTextSize : uint8
{
	/** Legal/micro text: build number, ping. */
	Micro		= 0,
	/** Body: subtitles, kill feed, prompts. */
	Small		= 1,
	/** Panel headers, ammo reserve. */
	Medium		= 2,
	/** Ammo count, timer. */
	Large		= 3,
	/** Banners: objective, phase changes. */
	Header		= 4,
	/** Death screen headline. */
	Display		= 5
};

UCLASS()
class ONEBRAHAM_API UOneBrahamUIStyle : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --- palette ---------------------------------------------------------
	/** Deep navy: panels, backgrounds, the "sign" colour. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor Navy();

	/** Light grey: primary text on navy. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor LightGrey();

	/** Signal blue: the player's own information, highlights, selection. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor SignalBlue();

	/** Safety yellow: warnings, low ammo, caustic, extraction countdowns. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor SafetyYellow();

	/** Emergency red: damage, alarms, lockdown, elimination. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor EmergencyRed();

	/** Muted green: extraction, success, friendly. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor SuccessGreen();

	/** Warm white for the crosshair, which must read on every surface. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor Crosshair();

	// --- derived colours -------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor Invert(const FLinearColor& Colour);

	/** Desaturated and darkened - used for out-of-range or unavailable elements. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor Muted(const FLinearColor& Colour, float Amount = 0.65f);

	/** Health colour: green -> yellow -> red as the fraction falls. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor HealthColour(float Fraction);

	/** Team colour for FIRE DRILL and squad tints. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor TeamColour(int32 Team);

	// --- brushes ---------------------------------------------------------
	/** Flat rounded panel, navy at the given opacity. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FSlateBrush PanelBrush(float Alpha = 0.78f, float CornerRadius = 4.f);

	/** Hairline panel: the border-only variant used for secondary panels. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FSlateBrush OutlineBrush(const FLinearColor& Colour, float Thickness = 1.f, float CornerRadius = 4.f);

	/** Solid fill brush. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FSlateBrush SolidBrush(const FLinearColor& Colour);

	/** Horizontal progress bar style used for health, hold prompts and capture. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FProgressBarStyle ProgressBarStyle(const FLinearColor& FillColour, float Thickness = 6.f, bool bRounded = true);

	// --- type ------------------------------------------------------------
	/**
	 * Font for the game's UI. Uses the engine's Roboto by default; the shipped
	 * build substitutes the licensed corporate grotesque described in
	 * Docs/07_UI.md ("One Braham Sans"), which is assigned through this single
	 * function so no widget needs to change.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FSlateFontInfo Font(EOneBrahamTextSize Size, bool bBold = false, int32 TrackingThousandths = 0);

	/** Text style with sensible tracking for the type scale. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FSlateFontInfo FontWithOutline(EOneBrahamTextSize Size, const FLinearColor& OutlineColour,
		int32 OutlineSize = 1);

	/** Uppercase, letter-spaced label used for panel headers ("SQUAD"). */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FText HeaderLabel(const FText& InText);

	/** Localised distance string: "12 m" under 100 m, else "FL 7" or "1.2 km". */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FText DistanceLabel(float DistanceCm, int32 FloorDelta = 0);

	/** Clock string for the match timer: M:SS. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FText ClockLabel(float SecondsRemaining);

	/** Floor label: "15 - SKYDECK", "G - PLAZA", "9 - OPEN PLAN OFFICE". */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FText FloorLabel(int32 FloorIndex);

	// ======================================================================
	//  The design system (spec: palette_and_motion, typography)
	// ======================================================================

	/**
	 * A face of the type system at a size in pixels at 1080p (the DPI curve
	 * scales it from there), tracked in thousandths of an em, optionally with
	 * the HUD's 1 px #000 60% outline. Falls back to the engine's Roboto at the
	 * nearest weight when the faces have not been imported, so a clean
	 * checkout still renders text. Named apart from Font(), the old type
	 * scale, which stays as a wrapper over it while screens migrate.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FSlateFontInfo TypeFont(EOneBrahamType Token, float Px, int32 TrackingThousandths = 0, bool bOutline = false);

	/** True once the imported faces resolve (false on a checkout without them). */
	static bool HasBrandFaces();

	/** A token at an alpha, display-correct (see EOneBrahamColour). */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor Colour(EOneBrahamColour Token, float Alpha = 1.f);

	/** "#RRGGBB" (or "RRGGBB") as a display-correct colour. */
	static FLinearColor Hex(const TCHAR* Hex, float Alpha = 1.f);

	/** An ini brand float (sRGB intent stored linear) as a display colour. */
	static FLinearColor FromBrandFloat(const FLinearColor& IniColour, float Alpha = 1.f);

	/** The stage's accent: blue before the lockdown, yellow in it, red in the Purge. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor StageAccent(EOneBrahamLockdownStage Stage);

	/** Neutral hairlines per stage: #E0E5EB 20%, #FFCD00 25%, #DD1E1F 30%. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static FLinearColor StageHairline(EOneBrahamLockdownStage Stage);

	/** The stage's announcement field, two colours (A at the left). */
	static void StageField(EOneBrahamLockdownStage Stage, FLinearColor& OutA, FLinearColor& OutB);

	/** Evaluates a motion curve at T in [0, 1] (clamped). */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static float Ease(EOneBrahamEase Curve, float T);

	/** Progress of a tween that started at StartSeconds, eased: 0 before, 1 after. */
	static float Tween(double NowSeconds, double StartSeconds, float DurationSeconds, EOneBrahamEase Curve);

	/** The player's Reduce Motion setting. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI|Style")
	static bool ReduceMotion();

	/** Clock text for the real local time: "17:42" or "17:42:09". */
	static FText LocalClock(bool bSeconds);
};
