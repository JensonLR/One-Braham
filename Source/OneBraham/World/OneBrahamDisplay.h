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

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneBrahamDisplay.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

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

	/** Pure layout and fade maths, exposed for tests. */
	static int32 SlideIndexAt(double Time, float SlideSeconds, int32 NumSlides);
	static float CopyAlphaAt(double Time, float SlideSeconds, float FadeSeconds);

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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PanelMID = nullptr;

	/** The accent rule: the screen material held to one colour, the eyebrow's. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentMID = nullptr;

	int32 ShownSlide = INDEX_NONE;
};
