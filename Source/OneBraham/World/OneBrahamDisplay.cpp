// Copyright (c) One Braham. All Rights Reserved.

#include "World/OneBrahamDisplay.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The layout grid, as fractions of the panel. One composition for every
	// screen: a left margin, an eyebrow in small capitals with a short accent
	// rule under it, the headline, the subline. Sizes are fractions of the
	// panel's height, capped against its width so a long, low panel does not
	// set its headline taller than its margins allow.
	constexpr float OBDisplayMargin = 0.07f;          // of width
	constexpr float OBDisplayTopMargin = 0.16f;       // of height
	constexpr float OBDisplayEyebrowSize = 0.065f;    // of height
	constexpr float OBDisplayHeadlineSize = 0.19f;
	constexpr float OBDisplaySublineSize = 0.075f;
	constexpr float OBDisplayAccentWidth = 0.09f;     // of width
	constexpr float OBDisplayBezelCm = 6.f;

	FColor WithAlpha(const FLinearColor& Colour, float Alpha)
	{
		FLinearColor Out = Colour;
		Out.A = FMath::Clamp(Alpha, 0.f, 1.f);
		return Out.ToFColor(true);
	}
}

AOneBrahamDisplay::AOneBrahamDisplay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f / 30.f;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cube = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;

	auto MakePart = [this, Cube](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Root);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetCanEverAffectNavigation(false);
		if (Cube != nullptr)
		{
			Part->SetStaticMesh(Cube);
		}
		return Part;
	};

	Bezel = MakePart(TEXT("Bezel"));
	Bezel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);   // the screen is solid
	Panel = MakePart(TEXT("Panel"));
	Accent = MakePart(TEXT("Accent"));

	auto MakeCopyLine = [this](const TCHAR* Name) -> UTextRenderComponent*
	{
		UTextRenderComponent* Text = CreateDefaultSubobject<UTextRenderComponent>(Name);
		Text->SetupAttachment(Root);
		Text->SetHorizontalAlignment(EHTA_Left);
		Text->SetVerticalAlignment(EVRTA_TextTop);
		Text->SetCastShadow(false);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Text;
	};

	EyebrowText = MakeCopyLine(TEXT("Eyebrow"));
	HeadlineText = MakeCopyLine(TEXT("Headline"));
	SublineText = MakeCopyLine(TEXT("Subline"));
}

int32 AOneBrahamDisplay::SlideIndexAt(double Time, float InSlideSeconds, int32 NumSlides)
{
	if (NumSlides <= 0)
	{
		return INDEX_NONE;
	}
	const double Period = FMath::Max(0.1, static_cast<double>(InSlideSeconds));
	const int64 Step = FMath::FloorToInt64(FMath::Max(0.0, Time) / Period);
	return static_cast<int32>(Step % NumSlides);
}

float AOneBrahamDisplay::CopyAlphaAt(double Time, float InSlideSeconds, float InFadeSeconds)
{
	const double Period = FMath::Max(0.1, static_cast<double>(InSlideSeconds));
	const double Into = FMath::Fmod(FMath::Max(0.0, Time), Period);
	const double Fade = FMath::Clamp(static_cast<double>(InFadeSeconds), 0.01, Period * 0.5);
	const double In = Into / Fade;
	const double Out = (Period - Into) / Fade;
	return static_cast<float>(FMath::Clamp(FMath::Min(In, Out), 0.0, 1.0));
}

void AOneBrahamDisplay::Layout()
{
	const float W = WidthCm;
	const float H = HeightCm;

	// Face along +X. The bezel is the body; the panel sits just proud of it.
	Bezel->SetRelativeLocation(FVector(-2.f, 0.f, 0.f));
	Bezel->SetRelativeScale3D(FVector(6.f, W + 2.f * OBDisplayBezelCm, H + 2.f * OBDisplayBezelCm) / 100.f);
	Panel->SetRelativeLocation(FVector(1.2f, 0.f, 0.f));
	Panel->SetRelativeScale3D(FVector(0.5f, W, H) / 100.f);

	if (BezelMaterial != nullptr)
	{
		Bezel->SetMaterial(0, BezelMaterial);
	}

	if (ScreenMaterial != nullptr && (PanelMID == nullptr || PanelMID->Parent != ScreenMaterial))
	{
		PanelMID = UMaterialInstanceDynamic::Create(ScreenMaterial, this);
	}
	if (PanelMID != nullptr)
	{
		PanelMID->SetScalarParameterValue(TEXT("Brightness"), Brightness);
		Panel->SetMaterial(0, PanelMID);
	}

	// The copy. Viewed from the front (looking along -X) the panel's left edge
	// is +Y, and left-aligned text runs towards -Y.
	const float Unit = FMath::Min(H, W * 0.42f);
	const float Left = W * 0.5f - W * OBDisplayMargin;
	float Line = H * 0.5f - H * OBDisplayTopMargin;

	EyebrowText->SetWorldSize(Unit * OBDisplayEyebrowSize);
	EyebrowText->SetRelativeLocation(FVector(2.4f, Left, Line));
	Line -= Unit * OBDisplayEyebrowSize * 1.55f;

	Accent->SetRelativeLocation(FVector(2.4f, Left - W * OBDisplayAccentWidth * 0.5f, Line));
	Accent->SetRelativeScale3D(FVector(0.2f, W * OBDisplayAccentWidth, FMath::Max(0.8f, Unit * 0.008f)) / 100.f);
	Line -= Unit * 0.06f;

	HeadlineText->SetWorldSize(Unit * OBDisplayHeadlineSize);
	HeadlineText->SetRelativeLocation(FVector(2.4f, Left, Line));
	Line -= Unit * OBDisplayHeadlineSize * 1.08f;

	SublineText->SetWorldSize(Unit * OBDisplaySublineSize);
	SublineText->SetRelativeLocation(FVector(2.4f, Left, Line));

	// Typography: the eyebrow tracked wide like small capitals, the headline
	// a touch tight, the subline set as it comes.
	EyebrowText->SetHorizSpacingAdjust(Unit * OBDisplayEyebrowSize * 0.22f);
	HeadlineText->SetHorizSpacingAdjust(-Unit * OBDisplayHeadlineSize * 0.02f);
	SublineText->SetHorizSpacingAdjust(Unit * OBDisplaySublineSize * 0.04f);

	for (UTextRenderComponent* Text : { EyebrowText.Get(), HeadlineText.Get(), SublineText.Get() })
	{
		Text->SetVisibility(bShowCopy && Slides.Num() > 0);
		if (TextMaterial != nullptr)
		{
			Text->SetTextMaterial(TextMaterial);
		}
	}
	Accent->SetVisibility(bShowCopy && Slides.Num() > 0);
	if (AccentMaterial != nullptr && (AccentMID == nullptr || AccentMID->Parent != AccentMaterial))
	{
		AccentMID = UMaterialInstanceDynamic::Create(AccentMaterial, this);
	}
	if (AccentMID != nullptr)
	{
		Accent->SetMaterial(0, AccentMID);
	}
}

void AOneBrahamDisplay::ApplyAt(double Time)
{
	if (Slides.Num() == 0)
	{
		return;
	}

	const double Local = Time + PhaseSeconds;
	const int32 Index = SlideIndexAt(Local, SlideSeconds, Slides.Num());
	const FOneBrahamDisplaySlide& Slide = Slides[Index];
	const FOneBrahamDisplaySlide& Previous = Slides[(Index + Slides.Num() - 1) % Slides.Num()];

	// The field crossfades from the last advert's colours over the fade.
	const double Into = FMath::Fmod(FMath::Max(0.0, Local), static_cast<double>(FMath::Max(0.1f, SlideSeconds)));
	const float Blend = static_cast<float>(FMath::SmoothStep(0.0, static_cast<double>(FadeSeconds) * 2.0, Into));
	if (PanelMID != nullptr)
	{
		PanelMID->SetVectorParameterValue(TEXT("ColourA"), FMath::Lerp(Previous.ColourA, Slide.ColourA, Blend));
		PanelMID->SetVectorParameterValue(TEXT("ColourB"), FMath::Lerp(Previous.ColourB, Slide.ColourB, Blend));
	}

	if (!bShowCopy)
	{
		return;
	}

	if (Index != ShownSlide)
	{
		ShownSlide = Index;
		EyebrowText->SetText(Slide.Eyebrow);
		HeadlineText->SetText(Slide.Headline);
		SublineText->SetText(Slide.Subline);
	}

	// Copy fades out and in at the change; the eyebrow takes the slide's
	// lighter colour so each advert has a colour of its own.
	const float Alpha = CopyAlphaAt(Local, SlideSeconds, FadeSeconds);
	const FLinearColor Eyebrow = FMath::Lerp(Slide.ColourB, FLinearColor::White, 0.45f);
	if (AccentMID != nullptr)
	{
		AccentMID->SetVectorParameterValue(TEXT("EmissiveColour"), Eyebrow);
		AccentMID->SetScalarParameterValue(TEXT("EmissiveStrength"), FMath::Min(60.f, Brightness * 0.5f) * Alpha);
	}
	EyebrowText->SetTextRenderColor(WithAlpha(Eyebrow, Alpha));
	HeadlineText->SetTextRenderColor(WithAlpha(FLinearColor::White, Alpha));
	SublineText->SetTextRenderColor(WithAlpha(FLinearColor(0.86f, 0.88f, 0.92f), Alpha * 0.9f));
}

void AOneBrahamDisplay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ShownSlide = INDEX_NONE;
	Layout();
	// In the editor the first slide stands, fully faded in.
	ApplyAt(FMath::Max(0.0, static_cast<double>(FadeSeconds) * 2.0 - PhaseSeconds));
}

void AOneBrahamDisplay::BeginPlay()
{
	Super::BeginPlay();
	ShownSlide = INDEX_NONE;
	Layout();
	SetActorTickEnabled(Slides.Num() > 0);
}

void AOneBrahamDisplay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const UWorld* World = GetWorld())
	{
		ApplyAt(World->GetTimeSeconds());
	}
}
