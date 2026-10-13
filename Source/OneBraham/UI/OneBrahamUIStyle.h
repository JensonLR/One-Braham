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
#include "OneBrahamUIStyle.generated.h"

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
	static FSlateFontInfo Font(EOneBrahamTextSize Size, bool bBold = false);

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
};
