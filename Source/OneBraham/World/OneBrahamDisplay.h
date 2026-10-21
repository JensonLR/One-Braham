// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDisplay.h
// ---------------------------------------------------------------------------
// The building's OLED screens: reception, the escalator wall and ceiling, the
// top of the escalators. Each one runs a loop of slides - an eyebrow, a
// headline, a subline, and the two colours of the moving field behind them -
// crossfading between them, with the copy laid out on a margin grid scaled to
// the panel, so a nine-metre escalator wall and a five-metre back wall are
// both composed rather than stretched.
//
// The field itself is M_OB_AdScreen (Tools/Python/onebraham/build_district.py):
// slow aurora bands on true OLED black. This actor drives its colours.
//
// WHEN THE BUILDING SPEAKS
//   PushOverride puts one slide over the loop for a hold - the tannoy's line,
//   in the building's voice and the stage's colours - and the adverts resume
//   when it lapses (or on ClearOverride). Every screen pushed on the same
//   frame changes on the same frame: the override's clock is world time, not
//   the loop's phase. UOneBrahamDisplayDirector is the caller in a match.
//
// THE TYPE
//   The copy is UTextRenderComponent, which draws offline-cached UFonts only:
//   F_OLED_Eyebrow / F_OLED_Headline (Archivo) and F_OLED_Aside (Instrument
//   Serif Italic), baked by build_frontend.import_display_fonts. Every line is
//   fitted to the panel's margins - wrapped onto a second or third line, then
//   shrunk - so a tannoy sentence on a three-metre canteen menu stays inside
//   the glass. Missing fonts fall back to the engine's.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneBrahamDisplay.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UFont;

/** One advert: the copy and the two colours of the field behind it. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamDisplaySlide
{
	GENERATED_BODY()

	/** Small capitals above the headline: the brand, the section. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	FText Eyebrow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	FText Headline;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	FText Subline;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	FLinearColor ColourA = FLinearColor(0.45f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	FLinearColor ColourB = FLinearColor(0.05f, 0.45f, 1.0f);
};

UCLASS()
class ONEBRAHAM_API AOneBrahamDisplay : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamDisplay();

	/** Panel size in cm: width along the actor's Y, height along Z. The face looks along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display", meta = (ClampMin = "20"))
	float WidthCm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display", meta = (ClampMin = "10"))
	float HeightCm = 300.f;

	/** Ceiling panels run the field without copy: text overhead is unreadable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	bool bShowCopy = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	TArray<FOneBrahamDisplaySlide> Slides;

	/** Seconds each slide holds, fades included. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display", meta = (ClampMin = "2"))
	float SlideSeconds = 7.f;

	/** Seconds of the copy's fade out and in, and of the colour crossfade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display", meta = (ClampMin = "0.1"))
	float FadeSeconds = 0.8f;

	/** Offset into the loop, so neighbouring screens do not change in lockstep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display")
	float PhaseSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Look")
	TObjectPtr<UMaterialInterface> ScreenMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Look")
	TObjectPtr<UMaterialInterface> BezelMaterial = nullptr;

	/** A flat emissive (EmissiveColour, EmissiveStrength) for the accent rule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Look")
	TObjectPtr<UMaterialInterface> AccentMaterial = nullptr;

	/** Unlit text: the copy glows with the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Look")
	TObjectPtr<UMaterialInterface> TextMaterial = nullptr;

	/** Panel luminance multiplier (the material's Brightness). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Look")
	float Brightness = 60.f;

	/** The copy's faces: offline-cached fonts (TextRender draws no other kind). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Type")
	TSoftObjectPtr<UFont> EyebrowFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Type")
	TSoftObjectPtr<UFont> HeadlineFont;

	/** The subline: the building's aside, in the serif italic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Display|Type")
	TSoftObjectPtr<UFont> SublineFont;

	// ----------------------------------------------------------------------
	//  The building speaks
	// ----------------------------------------------------------------------

	/**
	 * Holds Slide over the loop for HoldSeconds from now (world time), then the
	 * adverts resume. Pushing again while an override shows replaces it: the
	 * field stays where it is and only the copy changes (a new headline fades
	 * in; the same headline with a new subline just swaps the subline).
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Display")
	void PushOverride(const FOneBrahamDisplaySlide& Slide, float HoldSeconds);

	/** Ends the override now: the copy fades and the adverts come back. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Display")
	void ClearOverride();

	/** True from PushOverride until its hold lapses or ClearOverride. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Display")
	bool IsOverriding() const;

	/** Retints a showing override (the stage replicated after its announcement). */
	void RecolourOverride(const FLinearColor& ColourA, const FLinearColor& ColourB);

	const FOneBrahamDisplaySlide& GetOverrideSlide() const { return OverrideSlide; }
	double GetOverrideEndTime() const { return OverrideEnd; }

	/** Seconds the override's copy takes to come up: a cut, softened. */
	static constexpr float OverrideFadeInSeconds = 0.25f;
	/** Seconds the field takes to turn to the override's colours, and back. */
	static constexpr float OverrideFieldSeconds = 0.6f;

	/** Pure layout and fade maths, exposed for tests. */
	static int32 SlideIndexAt(double Time, float SlideSeconds, int32 NumSlides);
	static float CopyAlphaAt(double Time, float SlideSeconds, float FadeSeconds);

	/**
	 * How much of the field is the override's: 0 before Start, up to 1 over
	 * OverrideFieldSeconds, held, and back to 0 over FadeOut after End.
	 */
	static float OverrideFieldWeightAt(double Time, double Start, double End, float FadeOut);

	/** The override copy's alpha: in over OverrideFadeInSeconds, out over FadeOut before End. */
	static float OverrideCopyAlphaAt(double Time, double CopyStart, double End, float FadeOut);

	/**
	 * Text broken into Lines lines of as even a length as the words allow,
	 * joined by newlines (TextRender's line break). Never more lines than words.
	 */
	static FString WrapToLines(const FString& Text, int32 Lines);

	/**
	 * Of candidate layouts measured at the base size, the scale to set the one
	 * chosen at: each is shrunk to fit MaxWidth x MaxHeight (never grown), and
	 * a layout with more lines must be at least 12% larger to beat one with
	 * fewer. Sizes are (width, height); returns the index chosen, and its scale.
	 */
	/**
	 * Each single space between words doubled. An offline font's space is the
	 * width of its empty glyph cell, which in Archivo SemiCondensed and the
	 * Instrument Serif italic sets words almost touching ("lobbyis"); two
	 * read as one. Runs of spaces (the eyebrow's "  ·  ") are left alone.
	 */
	static FString OpenWordSpaces(const FString& Text);

	static int32 ChooseFit(const TArray<FVector2D>& MeasuredSizes, float MaxWidth, float MaxHeight, float& OutScale);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Bezel = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Panel = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Accent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UTextRenderComponent> EyebrowText = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UTextRenderComponent> HeadlineText = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UTextRenderComponent> SublineText = nullptr;

private:
	void Layout();
	void ApplyAt(double Time);

	/** Sets the three lines and lays them down the panel, each fitted to it. */
	void SetCopy(const FText& Eyebrow, const FText& Headline, const FText& Subline, bool bOverride);

	/** Fits one line: wraps (up to MaxLines) and shrinks it into the box. Returns the height it takes. */
	float FitLine(UTextRenderComponent* Text, const FText& Copy, float BaseSize, float MaxWidth, float MaxHeight, int32 MaxLines,
		bool bOpenSpaces);

	/** Loads the faces and gives each line its font and tracking. */
	void ApplyType();

	void SetCopyVisible(bool bVisible);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PanelMID = nullptr;

	/** The accent rule: the screen material held to one colour, the eyebrow's. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentMID = nullptr;

	int32 ShownSlide = INDEX_NONE;

	/** The override: its slide, when the field turned, when the copy came up, when it lapses. */
	FOneBrahamDisplaySlide OverrideSlide;
	double OverrideStart = -1.0;
	double OverrideCopyStart = -1.0;
	double OverrideEnd = -1.0;
	/** Which copy the text components hold: the override's, or ShownSlide's. */
	bool bOverrideCopyShown = false;
};
