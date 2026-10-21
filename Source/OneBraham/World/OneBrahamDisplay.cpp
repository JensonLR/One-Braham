// Copyright (c) One Braham. All Rights Reserved.

#include "World/OneBrahamDisplay.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
	constexpr float OBDisplaySublineSize = 0.09f;      // the italic is light: set it larger than a roman aside
	constexpr float OBDisplayAccentWidth = 0.09f;     // of width
	constexpr float OBDisplayBottomMargin = 0.12f;    // of height
	constexpr float OBDisplayBezelCm = 6.f;

	// Tracking, as a fraction of the type size: the eyebrow spaced like small
	// capitals (the HUD's eyebrows are +180 thousandths); the headline and the
	// aside as the faces come (an offline font has no kerning to tighten).
	constexpr float OBDisplayEyebrowTracking = 0.16f;
	constexpr float OBDisplayHeadlineTracking = 0.f;
	constexpr float OBDisplaySublineTracking = 0.f;

	/** A longer line must be this much bigger to be worth the extra line. */
	constexpr float OBDisplayWrapPreference = 1.12f;

	// build_frontend.import_display_fonts' offline fonts.
	const TCHAR* const OBDisplayEyebrowFontPath = TEXT("/Game/OneBraham/UI/Fonts/F_OLED_Eyebrow.F_OLED_Eyebrow");
	const TCHAR* const OBDisplayHeadlineFontPath = TEXT("/Game/OneBraham/UI/Fonts/F_OLED_Headline.F_OLED_Headline");
	const TCHAR* const OBDisplayAsideFontPath = TEXT("/Game/OneBraham/UI/Fonts/F_OLED_Aside.F_OLED_Aside");

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

	EyebrowFont = TSoftObjectPtr<UFont>(FSoftObjectPath(OBDisplayEyebrowFontPath));
	HeadlineFont = TSoftObjectPtr<UFont>(FSoftObjectPath(OBDisplayHeadlineFontPath));
	SublineFont = TSoftObjectPtr<UFont>(FSoftObjectPath(OBDisplayAsideFontPath));
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

float AOneBrahamDisplay::OverrideFieldWeightAt(double Time, double Start, double End, float FadeOut)
{
	if (Start < 0.0 || Time < Start)
	{
		return 0.f;
	}
	const double Up = FMath::SmoothStep(0.0, static_cast<double>(OverrideFieldSeconds), Time - Start);
	const double Down = Time < End ? 1.0 : 1.0 - FMath::SmoothStep(0.0, FMath::Max(0.01, static_cast<double>(FadeOut)), Time - End);
	return static_cast<float>(FMath::Clamp(Up * Down, 0.0, 1.0));
}

float AOneBrahamDisplay::OverrideCopyAlphaAt(double Time, double CopyStart, double End, float FadeOut)
{
	if (CopyStart < 0.0 || Time < CopyStart || Time >= End)
	{
		return 0.f;
	}
	const double In = (Time - CopyStart) / static_cast<double>(OverrideFadeInSeconds);
	const double Out = (End - Time) / FMath::Max(0.01, static_cast<double>(FadeOut));
	return static_cast<float>(FMath::Clamp(FMath::Min(In, Out), 0.0, 1.0));
}

FString AOneBrahamDisplay::WrapToLines(const FString& Text, int32 Lines)
{
	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);
	if (Lines <= 1 || Words.Num() <= 1)
	{
		return FString::Join(Words, TEXT(" "));
	}
	Lines = FMath::Min(Lines, Words.Num());

	// Greedy against an even target, so the lines come out as level as the
	// words allow (a rag, not one long line and a widow).
	int32 Total = 0;
	for (const FString& Word : Words)
	{
		Total += Word.Len();
	}
	Total += Words.Num() - 1;

	FString Out;
	int32 LineLen = 0;
	int32 LinesLeft = Lines;
	int32 Remaining = Total;
	for (int32 Index = 0; Index < Words.Num(); ++Index)
	{
		const FString& Word = Words[Index];
		const int32 WordsLeft = Words.Num() - Index;
		if (LineLen > 0)
		{
			const float Target = static_cast<float>(Remaining + LineLen) / static_cast<float>(LinesLeft);
			// Break when this word overshoots the target by more than leaving
			// the line short would, or when the remaining lines need a word each.
			const bool bOvershoots = LineLen + 1 + Word.Len() - Target > Target - LineLen;
			if (LinesLeft > 1 && (bOvershoots || WordsLeft < LinesLeft))
			{
				Remaining -= 1;   // the space the break replaces
				Out += TEXT("\n");
				--LinesLeft;
				LineLen = 0;
			}
			else
			{
				Out += TEXT(" ");
				++LineLen;
				Remaining -= 1;
			}
		}
		Out += Word;
		LineLen += Word.Len();
		Remaining -= Word.Len();
	}
	return Out;
}

FString AOneBrahamDisplay::OpenWordSpaces(const FString& Text)
{
	FString Out;
	Out.Reserve(Text.Len() + Text.Len() / 4);
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		const TCHAR Ch = Text[Index];
		Out.AppendChar(Ch);
		const bool bLone = Ch == TEXT(' ')
			&& Index > 0 && Text[Index - 1] != TEXT(' ') && Text[Index - 1] != TEXT('\n')
			&& Index + 1 < Text.Len() && Text[Index + 1] != TEXT(' ') && Text[Index + 1] != TEXT('\n');
		if (bLone)
		{
			Out.AppendChar(TEXT(' '));
		}
	}
	return Out;
}

int32 AOneBrahamDisplay::ChooseFit(const TArray<FVector2D>& MeasuredSizes, float MaxWidth, float MaxHeight, float& OutScale)
{
	OutScale = 1.f;
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < MeasuredSizes.Num(); ++Index)
	{
		const FVector2D Size = MeasuredSizes[Index];
		float Scale = 1.f;
		if (Size.X > MaxWidth && Size.X > KINDA_SMALL_NUMBER && MaxWidth > 0.f)
		{
			Scale = FMath::Min(Scale, MaxWidth / static_cast<float>(Size.X));
		}
		if (Size.Y > MaxHeight && Size.Y > KINDA_SMALL_NUMBER && MaxHeight > 0.f)
		{
			Scale = FMath::Min(Scale, MaxHeight / static_cast<float>(Size.Y));
		}
		if (Best == INDEX_NONE || Scale > OutScale * OBDisplayWrapPreference)
		{
			Best = Index;
			OutScale = Scale;
		}
	}
	return Best;
}

void AOneBrahamDisplay::Layout()
{
	// A dedicated server loads the actor without its render-only parts (text
	// render components are not created there), and a screen nobody can see
	// needs no layout.
	if (Bezel == nullptr || Panel == nullptr || Accent == nullptr
		|| EyebrowText == nullptr || HeadlineText == nullptr || SublineText == nullptr)
	{
		return;
	}

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

	ApplyType();

	for (UTextRenderComponent* Text : { EyebrowText.Get(), HeadlineText.Get(), SublineText.Get() })
	{
		if (TextMaterial != nullptr)
		{
			Text->SetTextMaterial(TextMaterial);
		}
	}
	SetCopyVisible(bShowCopy && (Slides.Num() > 0 || IsOverriding()));

	if (AccentMaterial != nullptr && (AccentMID == nullptr || AccentMID->Parent != AccentMaterial))
	{
		AccentMID = UMaterialInstanceDynamic::Create(AccentMaterial, this);
	}
	if (AccentMID != nullptr)
	{
		Accent->SetMaterial(0, AccentMID);
	}

	// The copy is laid out with its words: the next ApplyAt sets it again.
	ShownSlide = INDEX_NONE;
	bOverrideCopyShown = false;
}

void AOneBrahamDisplay::ApplyType()
{
	struct FLineType
	{
		UTextRenderComponent* Text;
		const TSoftObjectPtr<UFont>* Face;
		float Tracking;
	};
	const FLineType Lines[] =
	{
		{ EyebrowText.Get(), &EyebrowFont, OBDisplayEyebrowTracking },
		{ HeadlineText.Get(), &HeadlineFont, OBDisplayHeadlineTracking },
		{ SublineText.Get(), &SublineFont, OBDisplaySublineTracking },
	};

	for (const FLineType& Line : Lines)
	{
		if (Line.Text == nullptr)
		{
			continue;
		}

		// Quietly: a checkout whose fonts have not been baked yet draws the
		// engine's face rather than logging a failed load per screen.
		if (!Line.Face->IsNull())
		{
			UFont* Face = Line.Face->Get();
			if (Face == nullptr)
			{
				Face = LoadObject<UFont>(nullptr, *Line.Face->ToSoftObjectPath().ToString(), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			if (Face != nullptr && Line.Text->Font != Face)
			{
				Line.Text->SetFont(Face);
			}
		}

		// HorizSpacingAdjust is in the font's pixels, scaled with the size as
		// the glyphs are: a fraction of the cell height is a fraction of the type.
		const float Cell = Line.Text->Font != nullptr ? Line.Text->Font->GetMaxCharHeight() : 0.f;
		Line.Text->SetHorizSpacingAdjust(Cell * Line.Tracking);
	}
}

void AOneBrahamDisplay::SetCopyVisible(bool bVisible)
{
	for (UTextRenderComponent* Text : { EyebrowText.Get(), HeadlineText.Get(), SublineText.Get() })
	{
		if (Text != nullptr && Text->IsVisible() != bVisible)
		{
			Text->SetVisibility(bVisible);
		}
	}
	if (Accent != nullptr && Accent->IsVisible() != bVisible)
	{
		Accent->SetVisibility(bVisible);
	}
}

float AOneBrahamDisplay::FitLine(UTextRenderComponent* Text, const FText& Copy, float BaseSize, float MaxWidth, float MaxHeight, int32 MaxLines,
	bool bOpenSpaces)
{
	if (Text == nullptr)
	{
		return 0.f;
	}
	Text->SetWorldSize(BaseSize);
	if (Copy.IsEmpty())
	{
		Text->SetText(FText::GetEmpty());
		return 0.f;
	}

	// Measure each way of setting it at the base size: one line, then wrapped
	// onto two, three. The text's local box runs Y across, Z down the panel.
	const FString Flat = Copy.ToString();
	TArray<FString> Candidates;
	Candidates.Add(Flat);
	int32 Words = 1;
	for (const TCHAR Ch : Flat)
	{
		Words += FChar::IsWhitespace(Ch) ? 1 : 0;
	}
	for (int32 Lines = 2; Lines <= FMath::Min(MaxLines, Words); ++Lines)
	{
		const FString Wrapped = WrapToLines(Flat, Lines);
		if (!Candidates.Contains(Wrapped))
		{
			Candidates.Add(Wrapped);
		}
	}

	if (bOpenSpaces)
	{
		for (FString& Candidate : Candidates)
		{
			Candidate = OpenWordSpaces(Candidate);
		}
	}

	TArray<FVector2D> Sizes;
	for (const FString& Candidate : Candidates)
	{
		Text->SetText(FText::FromString(Candidate));
		const FVector Local = Text->GetTextLocalSize();
		Sizes.Add(FVector2D(Local.Y, Local.Z));
	}

	float Scale = 1.f;
	const int32 Chosen = FMath::Max(0, ChooseFit(Sizes, MaxWidth, MaxHeight, Scale));
	Text->SetText(Chosen == 0 && !bOpenSpaces ? Copy : FText::FromString(Candidates[Chosen]));
	Text->SetWorldSize(BaseSize * Scale);
	return static_cast<float>(Sizes[Chosen].Y) * Scale;
}

void AOneBrahamDisplay::SetCopy(const FText& Eyebrow, const FText& Headline, const FText& Subline, bool bOverride)
{
	if (EyebrowText == nullptr || HeadlineText == nullptr || SublineText == nullptr || Accent == nullptr)
	{
		return;
	}

	// The copy. Viewed from the front (looking along -X) the panel's left edge
	// is +Y, and left-aligned text runs towards -Y.
	const float W = WidthCm;
	const float H = HeightCm;
	const float Unit = FMath::Min(H, W * 0.42f);
	const float Left = W * 0.5f - W * OBDisplayMargin;
	const float Measure = W * (1.f - 2.f * OBDisplayMargin);
	const float Floor = -H * 0.5f + H * OBDisplayBottomMargin;
	float Line = H * 0.5f - H * OBDisplayTopMargin;

	const float EyebrowSize = Unit * OBDisplayEyebrowSize;
	FitLine(EyebrowText, Eyebrow, EyebrowSize, Measure, EyebrowSize * 1.6f, 1, /*bOpenSpaces*/ false);
	EyebrowText->SetRelativeLocation(FVector(2.4f, Left, Line));
	Line -= EyebrowSize * 1.55f;

	Accent->SetRelativeLocation(FVector(2.4f, Left - W * OBDisplayAccentWidth * 0.5f, Line));
	Accent->SetRelativeScale3D(FVector(0.2f, W * OBDisplayAccentWidth, FMath::Max(0.8f, Unit * 0.008f)) / 100.f);
	Line -= Unit * 0.06f;

	// The headline takes what the subline leaves it: an announcement may run
	// to three lines, an advert to two.
	const float SublineSize = Unit * OBDisplaySublineSize;
	const float SublineReserve = Subline.IsEmpty() ? 0.f : SublineSize * 1.5f;
	const float HeadlineSize = Unit * OBDisplayHeadlineSize;
	const float HeadlineHeight = FitLine(HeadlineText, Headline, HeadlineSize, Measure,
		FMath::Max(HeadlineSize * 0.5f, Line - Floor - SublineReserve), bOverride ? 3 : 2, /*bOpenSpaces*/ true);
	HeadlineText->SetRelativeLocation(FVector(2.4f, Left, Line));
	Line -= FMath::Max(HeadlineHeight, HeadlineText->WorldSize * 0.8f) + Unit * 0.035f;

	FitLine(SublineText, Subline, SublineSize, Measure, FMath::Max(SublineSize * 0.5f, Line - Floor), 2, /*bOpenSpaces*/ true);
	SublineText->SetRelativeLocation(FVector(2.4f, Left, Line));
}

void AOneBrahamDisplay::PushOverride(const FOneBrahamDisplaySlide& Slide, float HoldSeconds)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;

	const bool bWasShowing = IsOverriding();
	if (!bWasShowing)
	{
		// From a lapsing override the field turns back from where it is
		// rather than dropping to the adverts first.
		const float Weight = OverrideFieldWeightAt(Now, OverrideStart, OverrideEnd, FadeSeconds);
		OverrideStart = Now - static_cast<double>(OverrideFieldSeconds) * Weight;
	}
	if (!bWasShowing || !Slide.Headline.EqualTo(OverrideSlide.Headline))
	{
		OverrideCopyStart = Now;
	}

	OverrideSlide = Slide;
	OverrideEnd = Now + FMath::Max(0.5, static_cast<double>(HoldSeconds));
	bOverrideCopyShown = false;

	// On this frame, not the next tick: every screen the director pushes
	// changes together.
	SetActorTickEnabled(true);
	ApplyAt(Now);
}

void AOneBrahamDisplay::ClearOverride()
{
	if (!IsOverriding())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	OverrideEnd = Now;
	ApplyAt(Now);
}

bool AOneBrahamDisplay::IsOverriding() const
{
	if (OverrideEnd < 0.0)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetTimeSeconds() < OverrideEnd;
}

void AOneBrahamDisplay::RecolourOverride(const FLinearColor& ColourA, const FLinearColor& ColourB)
{
	OverrideSlide.ColourA = ColourA;
	OverrideSlide.ColourB = ColourB;
}

void AOneBrahamDisplay::ApplyAt(double Time)
{
	const bool bHasOverride = OverrideEnd >= 0.0;
	const bool bOverrideShowing = bHasOverride && Time < OverrideEnd;
	if (Slides.Num() == 0 && !bHasOverride)
	{
		return;
	}

	// The loop's field: it crossfades from the last advert's colours over the fade.
	const double Local = Time + PhaseSeconds;
	const int32 Index = SlideIndexAt(Local, SlideSeconds, Slides.Num());
	FLinearColor LoopA = OverrideSlide.ColourA;
	FLinearColor LoopB = OverrideSlide.ColourB;
	if (Index != INDEX_NONE)
	{
		const FOneBrahamDisplaySlide& Slide = Slides[Index];
		const FOneBrahamDisplaySlide& Previous = Slides[(Index + Slides.Num() - 1) % Slides.Num()];
		const double Into = FMath::Fmod(FMath::Max(0.0, Local), static_cast<double>(FMath::Max(0.1f, SlideSeconds)));
		const float Blend = static_cast<float>(FMath::SmoothStep(0.0, static_cast<double>(FadeSeconds) * 2.0, Into));
		LoopA = FMath::Lerp(Previous.ColourA, Slide.ColourA, Blend);
		LoopB = FMath::Lerp(Previous.ColourB, Slide.ColourB, Blend);
	}

	// The building's line takes the field over it.
	const float Weight = bHasOverride ? OverrideFieldWeightAt(Time, OverrideStart, OverrideEnd, FadeSeconds) : 0.f;
	if (PanelMID != nullptr)
	{
		PanelMID->SetVectorParameterValue(TEXT("ColourA"), FMath::Lerp(LoopA, OverrideSlide.ColourA, Weight));
		PanelMID->SetVectorParameterValue(TEXT("ColourB"), FMath::Lerp(LoopB, OverrideSlide.ColourB, Weight));

		// The moment it speaks every panel flares and settles: the frame a
		// lobby full of screens changes at once is the thing to notice.
		const double Since = Time - OverrideStart;
		const float Flare = bOverrideShowing && Since >= 0.0 ? 0.9f * static_cast<float>(FMath::Exp(-Since * 2.5)) : 0.f;
		PanelMID->SetScalarParameterValue(TEXT("Brightness"), Brightness * (1.f + Flare));
	}

	// Lapsed and the field back: the override is over.
	if (bHasOverride && !bOverrideShowing && Weight <= 0.f && Time >= OverrideEnd)
	{
		OverrideStart = OverrideCopyStart = OverrideEnd = -1.0;
		if (Slides.Num() == 0)
		{
			SetCopyVisible(false);
			SetActorTickEnabled(false);
			return;
		}
	}

	const bool bCopy = bShowCopy && EyebrowText != nullptr && HeadlineText != nullptr && SublineText != nullptr;
	SetCopyVisible(bCopy && (Index != INDEX_NONE || bOverrideShowing));
	if (!bCopy)
	{
		return;
	}

	const FOneBrahamDisplaySlide* Shown = nullptr;
	float Alpha = 0.f;
	if (bOverrideShowing)
	{
		if (!bOverrideCopyShown)
		{
			bOverrideCopyShown = true;
			ShownSlide = INDEX_NONE;
			SetCopy(OverrideSlide.Eyebrow, OverrideSlide.Headline, OverrideSlide.Subline, /*bOverride*/ true);
		}
		Shown = &OverrideSlide;
		Alpha = OverrideCopyAlphaAt(Time, OverrideCopyStart, OverrideEnd, FadeSeconds * 0.6f);
	}
	else if (Index != INDEX_NONE)
	{
		if (Index != ShownSlide || bOverrideCopyShown)
		{
			ShownSlide = Index;
			bOverrideCopyShown = false;
			SetCopy(Slides[Index].Eyebrow, Slides[Index].Headline, Slides[Index].Subline, /*bOverride*/ false);
		}
		Shown = &Slides[Index];
		Alpha = CopyAlphaAt(Local, SlideSeconds, FadeSeconds);
		if (bHasOverride)
		{
			// The adverts come back in, wherever the loop has got to.
			Alpha *= FMath::Clamp(static_cast<float>((Time - OverrideEnd) / FMath::Max(0.1f, FadeSeconds)), 0.f, 1.f);
		}
	}
	if (Shown == nullptr)
	{
		return;
	}

	// The eyebrow takes the slide's lighter colour so each advert has a colour
	// of its own, and the building's line the colour of its stage.
	const FLinearColor Eyebrow = FMath::Lerp(Shown->ColourB, FLinearColor::White, 0.45f);
	if (AccentMID != nullptr)
	{
		AccentMID->SetVectorParameterValue(TEXT("EmissiveColour"), Eyebrow);
		AccentMID->SetScalarParameterValue(TEXT("EmissiveStrength"), FMath::Min(60.f, Brightness * 0.5f) * Alpha);
	}
	EyebrowText->SetTextRenderColor(WithAlpha(Eyebrow, Alpha));
	HeadlineText->SetTextRenderColor(WithAlpha(FLinearColor::White, Alpha));
	SublineText->SetTextRenderColor(WithAlpha(FLinearColor(0.93f, 0.94f, 0.97f), Alpha));
}

void AOneBrahamDisplay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Layout();
	// In the editor the first slide stands, fully faded in.
	ApplyAt(FMath::Max(0.0, static_cast<double>(FadeSeconds) * 2.0 - PhaseSeconds));
}

void AOneBrahamDisplay::BeginPlay()
{
	Super::BeginPlay();
	Layout();
	// Nothing to animate where nothing renders; a screen with no adverts
	// wakes when the building has something to say (PushOverride).
	SetActorTickEnabled(Slides.Num() > 0 && !IsRunningDedicatedServer());
}

void AOneBrahamDisplay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const UWorld* World = GetWorld())
	{
		ApplyAt(World->GetTimeSeconds());
	}
}
