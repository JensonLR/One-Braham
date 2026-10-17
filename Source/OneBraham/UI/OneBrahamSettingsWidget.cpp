// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamSettingsWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UI/FrontEnd/OneBrahamPaintedControl.h"
#include "Voice/OneBrahamMicrophonePermission.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"

using namespace OneBrahamPaint;

namespace OneBrahamSettingsLayout
{
	constexpr float RailWidth	= 300.f;
	constexpr float HeaderHeight = 92.f;
	constexpr float FooterHeight = 64.f;
	constexpr float RowHeight	= 52.f;
	constexpr float TabHeight	= 52.f;
	constexpr float LabelWidth	= 300.f;
	constexpr float ControlWidth = 340.f;
	constexpr int32 MicSegments	= 24;

	FText Percent(float Value)
	{
		return FText::Format(NSLOCTEXT("OneBraham", "Settings_Percent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value)));
	}

	FText Decimal(float Value, int32 Digits)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = Digits;
		Options.MaximumFractionalDigits = Digits;
		return FText::AsNumber(Value, &Options);
	}

	/** Mouse and stick sensitivities are stored as small fractions (0.07); the
	 *  menu shows them x100 because "7.0" is a number people can remember. */
	constexpr float SensitivityDisplayScale = 100.f;

	/** How lit a control is: eased in over 140 ms, out over 120 ms. */
	float LitOf(const FOneBrahamControlState& State)
	{
		return State.bLit ? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
			: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f);
	}

	bool IsLeft(const FKey& Key)
	{
		return Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::A;
	}

	bool IsRight(const FKey& Key)
	{
		return Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right || Key == EKeys::D;
	}
}

UOneBrahamSettingsWidget::UOneBrahamSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamSettingsWidget::BuildLayout()
{
	using namespace OneBrahamSettingsLayout;

	// A minimum size for a host that does not stretch it (the pause menu); the
	// main menu's canvas gives it x 10-90%, y 16-84% of the screen.
	USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SettingsRoot"));
	Root->SetMinDesiredWidth(1040.f);
	Root->SetMinDesiredHeight(640.f);
	WidgetTree->RootWidget = Root;

	UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	Root->AddChild(Stack);

	// The panel itself, the header, the description: painted under the rows.
	UOneBrahamPaintedControl* Panel = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Panel->Setup(FVector2D::ZeroVector, false);
	Panel->PaintFn = [this](const FPainter& C, const FOneBrahamControlState& State)
	{
		const FVector2D S = C.Size();
		Rounded(C, FVector2D::ZeroVector, S, 4.f, Tok(EOneBrahamColour::OLEDBlack, 0.55f), Tok(EOneBrahamColour::LightGrey, 0.10f), 1.f);
		Box(C, FVector2D(RailWidth, 24.f), FVector2D(1.f, S.Y - 48.f), Tok(EOneBrahamColour::LightGrey, 0.10f));

		TextOnBaseline(C, 28.f, 48.f, NSLOCTEXT("OneBraham", "Settings_Kicker", "FACILITIES").ToString(),
			Type(EOneBrahamType::Eyebrow, 12.f, 200), Tok(EOneBrahamColour::LightGrey, 0.6f));
		TextOnBaseline(C, 28.f, 70.f, NSLOCTEXT("OneBraham", "Settings_Directory", "Floor directory").ToString(),
			Type(EOneBrahamType::Aside, 18.f), Tok(EOneBrahamColour::LightGrey, 0.75f));

		// The floor you are on: its name, and the state of your changes.
		static const TCHAR* const Floors[] = { TEXT("G"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5") };
		const FString Floor = ActiveTab >= 0 && ActiveTab < UE_ARRAY_COUNT(Floors) ? Floors[ActiveTab] : TEXT("");
		const float Enter = UOneBrahamUIStyle::Tween(State.Now, TabChangedAt, 0.26f, EOneBrahamEase::Ribbon);
		TextOnBaseline(C, RailWidth + 32.f, 46.f, FString::Printf(TEXT("LEVEL %s"), *Floor),
			Type(EOneBrahamType::TelemetryLight, 11.f, 200), Tok(EOneBrahamColour::SafetyYellow, 0.9f));
		if (TabButtons.IsValidIndex(ActiveTab))
		{
			const FString Name = TabNames.IsValidIndex(ActiveTab) ? TabNames[ActiveTab] : FString();
			TextOnBaseline(C, RailWidth + 32.f + 10.f * (1.f - Enter), 76.f, Name, Type(EOneBrahamType::Verb, 26.f, -10),
				Tok(EOneBrahamColour::White, Enter));
		}
		TextOnBaseline(C, S.X - 32.f, 46.f, StatusText.ToString().ToUpper(), Type(EOneBrahamType::TelemetryLight, 10.f, 160),
			bDirty ? Tok(EOneBrahamColour::SafetyYellow, 0.8f) : Tok(EOneBrahamColour::LightGrey, 0.45f), 1.f);
		Box(C, FVector2D(RailWidth + 32.f, HeaderHeight - 1.f), FVector2D(S.X - RailWidth - 64.f, 1.f), Tok(EOneBrahamColour::LightGrey, 0.10f));

		// The focused setting's description, in the building's voice.
		Box(C, FVector2D(RailWidth + 32.f, S.Y - FooterHeight), FVector2D(S.X - RailWidth - 64.f, 1.f), Tok(EOneBrahamColour::LightGrey, 0.10f));
		if (!FocusedDescription.IsEmpty())
		{
			const float In = UOneBrahamUIStyle::Tween(State.Now, DescriptionAt, 0.2f, EOneBrahamEase::Ribbon);
			TextOnBaseline(C, RailWidth + 32.f, S.Y - FooterHeight * 0.5f + 6.f, FocusedDescription.ToString(), Type(EOneBrahamType::Aside, 16.f),
				Tok(EOneBrahamColour::LightGrey, 0.65f * In));
		}
	};
	if (UOverlaySlot* PanelSlot = Stack->AddChildToOverlay(Panel))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Fill);
		PanelSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UOverlaySlot* ColumnsSlot = Stack->AddChildToOverlay(Columns))
	{
		ColumnsSlot->SetHorizontalAlignment(HAlign_Fill);
		ColumnsSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// --- the floor directory -------------------------------------------------
	TabRail = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TabRail"));
	USizeBox* RailBox = MakeCell(TabRail, RailWidth);
	Columns->AddChildToHorizontalBox(RailBox)->SetPadding(FMargin(0.f, HeaderHeight + 4.f, 0.f, 0.f));

	// --- the pages -------------------------------------------------------------
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Pages"));
	UHorizontalBoxSlot* PagesSlot = Columns->AddChildToHorizontalBox(Pages);
	PagesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PagesSlot->SetPadding(FMargin(16.f, HeaderHeight + 8.f, 24.f, FooterHeight + 4.f));

	BuildGraphicsPage();
	BuildAudioPage();
	BuildVoicePage();
	BuildGameplayPage();
	BuildControlsPage();
	BuildAccessibilityPage();

	StatusText = NSLOCTEXT("OneBraham", "Settings_StatusIdle", "Changes are applied and saved when you leave this page.");
	SelectTab(0);
}

UVerticalBox* UOneBrahamSettingsWidget::AddPage(const FText& Floor, const FText& TabName)
{
	using namespace OneBrahamSettingsLayout;
	const int32 Index = TabButtons.Num();

	UOneBrahamPaintedControl* Tab = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Tab->Setup(FVector2D(RailWidth, TabHeight), true);
	const FString FloorText = Floor.ToString();
	const FString NameText = TabName.ToString();
	TabNames.Add(NameText);
	Tab->PaintFn = [this, Index, FloorText, NameText](const FPainter& C, const FOneBrahamControlState& State)
	{
		const FVector2D S = C.Size();
		const bool bActive = ActiveTab == Index;
		const float Lit = LitOf(State);
		if (Lit > 0.f)
		{
			Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::White, 0.05f * Lit));
		}
		if (bActive)
		{
			const float Grow = UOneBrahamUIStyle::Tween(State.Now, TabChangedAt, 0.14f, EOneBrahamEase::Ribbon);
			Box(C, FVector2D(0.f, S.Y * 0.5f * (1.f - Grow)), FVector2D(6.f, S.Y * Grow), Tok(EOneBrahamColour::SignalBlue));
		}
		const float Alpha = bActive ? 1.f : FMath::Lerp(0.55f, 0.9f, Lit);
		TextOnBaseline(C, 30.f, 33.f, FloorText, Type(EOneBrahamType::Telemetry, 18.f),
			bActive ? Tok(EOneBrahamColour::SafetyYellow) : Tok(EOneBrahamColour::LightGrey, Alpha));
		TextOnBaseline(C, 66.f, 32.f, NameText, Type(EOneBrahamType::Eyebrow, 15.f, 120), Tok(EOneBrahamColour::White, Alpha));
	};
	Tab->OnActivate = [this, Index]() { SelectTab(Index); };
	TabButtons.Add(Tab);
	TabRail->AddChildToVerticalBox(Tab);

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Scroll->SetScrollbarThickness(FVector2D(2.f, 2.f));
	Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	// Gamepad focus walking down a long page brings the focused control into
	// view; without this a controller user tabs into rows they cannot see.
	Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);

	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Scroll->AddChild(Page);
	Pages->AddChild(Scroll);

	return Page;
}

void UOneBrahamSettingsWidget::AddSection(UVerticalBox* Page, const FText& Title)
{
	const bool bFirst = Page->GetChildrenCount() == 0;
	UOneBrahamPaintedControl* Head = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Head->Setup(FVector2D(0.f, bFirst ? 30.f : 46.f), false);
	const FString Text = Title.ToString();
	Head->PaintFn = [Text](const FPainter& C, const FOneBrahamControlState&)
	{
		const FVector2D S = C.Size();
		TextOnBaseline(C, 16.f, S.Y - 10.f, Text, Type(EOneBrahamType::Eyebrow, 11.f, 200), Tok(EOneBrahamColour::LightGrey, 0.5f));
		Box(C, FVector2D(0.f, S.Y - 1.f), FVector2D(S.X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.12f));
	};
	Page->AddChildToVerticalBox(Head)->SetHorizontalAlignment(HAlign_Fill);
}

void UOneBrahamSettingsWidget::AddNote(UVerticalBox* Page, const FText& Text, UTextBlock** OutText)
{
	UTextBlock* Note = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Note->SetFont(UOneBrahamUIStyle::TypeFont(EOneBrahamType::Aside, 15.f));
	Note->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::Colour(EOneBrahamColour::LightGrey, 0.6f)));
	Note->SetText(Text);
	Note->SetAutoWrapText(true);
	Note->SetVisibility(ESlateVisibility::HitTestInvisible);
	Page->AddChildToVerticalBox(Note)->SetPadding(FMargin(16.f, 6.f, 16.f, 8.f));

	if (OutText != nullptr)
	{
		*OutText = Note;
	}
}

UOneBrahamPaintedControl* UOneBrahamSettingsWidget::AddRow(UVerticalBox* Page, const FText& Label, const FText& Description,
	TFunction<void(const FPainter&, const FVector2D&, const FVector2D&, float)> PaintControl)
{
	using namespace OneBrahamSettingsLayout;

	UOneBrahamPaintedControl* Row = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Row->Setup(FVector2D(0.f, RowHeight), true);
	const FString LabelText = Label.ToString();
	Row->PaintFn = [LabelText, PaintControl](const FPainter& C, const FOneBrahamControlState& State)
	{
		const FVector2D S = C.Size();
		const float Lit = LitOf(State);
		if (Lit > 0.f)
		{
			Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::White, 0.05f * Lit));
			Box(C, FVector2D::ZeroVector, FVector2D(2.f, S.Y), Tok(EOneBrahamColour::SignalBlue, Lit));
		}
		TextOnBaseline(C, 16.f, 32.f, LabelText, Type(EOneBrahamType::Body, 16.f), Tok(EOneBrahamColour::White, FMath::Lerp(0.85f, 1.f, Lit)));
		Box(C, FVector2D(0.f, S.Y - 1.f), FVector2D(S.X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.06f));

		// The control in the right half.
		const FVector2D At(S.X * 0.5f, 0.f);
		PaintControl(C, At, FVector2D(S.X * 0.5f - 16.f, S.Y), Lit);
	};

	const FText Said = Description.IsEmpty()
		? FText::Format(NSLOCTEXT("OneBraham", "Settings_DescDefault", "{0}."), Label)
		: Description;
	Row->OnLitChanged = [this, Said](bool bLit)
	{
		if (bLit)
		{
			FocusedDescription = Said;
			DescriptionAt = FPlatformTime::Seconds();
		}
	};

	Page->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Fill);
	return Row;
}

void UOneBrahamSettingsWidget::AddSlider(UVerticalBox* Page, const FText& Label, float Min, float Max, float Step,
	TFunction<float(const UOneBrahamGameUserSettings&)> Get,
	TFunction<void(UOneBrahamGameUserSettings&, float)> Set,
	TFunction<FText(float)> Format, const FText& Description)
{
	const float Range = FMath::Max(Max - Min, KINDA_SMALL_NUMBER);
	auto Current = [Get, Min]()
	{
		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		return Settings != nullptr ? Get(*Settings) : Min;
	};
	auto Write = [this, Set, Min, Max, Step](float Value)
	{
		const float Snapped = Step > 0.f ? FMath::GridSnap(Value - Min, Step) + Min : Value;
		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Set(*Settings, FMath::Clamp(Snapped, Min, Max));
		}
		MarkDirty();
	};

	UOneBrahamPaintedControl* Row = AddRow(Page, Label, Description,
		[Current, Format, Min, Range](const FPainter& C, const FVector2D& At, const FVector2D& Size, float Lit)
		{
			const float Value = Current();
			const float T = FMath::Clamp((Value - Min) / Range, 0.f, 1.f);
			const float TrackW = Size.X - 96.f;
			const float Y = At.Y + Size.Y * 0.5f;
			Box(C, FVector2D(At.X, Y - 2.f), FVector2D(TrackW, 4.f), Tok(EOneBrahamColour::LightGrey, 0.15f));
			Ribbon(C, FVector2D(At.X, Y - 2.f), FVector2D(TrackW * T, 4.f));
			const float Thumb = 16.f;
			Box(C, FVector2D(At.X + TrackW * T - Thumb * 0.5f, Y - Thumb * 0.5f), FVector2D(Thumb, Thumb), Tok(EOneBrahamColour::White));
			if (Lit > 0.f)
			{
				Rounded(C, FVector2D(At.X + TrackW * T - Thumb * 0.5f - 3.f, Y - Thumb * 0.5f - 3.f), FVector2D(Thumb + 6.f, Thumb + 6.f), 1.f,
					FLinearColor::Transparent, Tok(EOneBrahamColour::SignalBlue, Lit), 1.f);
			}
			TextOnBaseline(C, At.X + Size.X, Y + 6.f, Format(Value).ToString(), Type(EOneBrahamType::Telemetry, 15.f), Tok(EOneBrahamColour::White), 1.f);
		});

	// Left / right step it; a click or a drag puts it where the pointer is.
	const float KeyStep = Step > 0.f ? FMath::Max(Step, Range / 40.f) : Range / 40.f;
	Row->OnKey = [Current, Write, KeyStep](const FKey& Key)
	{
		if (OneBrahamSettingsLayout::IsLeft(Key) || OneBrahamSettingsLayout::IsRight(Key))
		{
			Write(Current() + (OneBrahamSettingsLayout::IsRight(Key) ? KeyStep : -KeyStep));
			return true;
		}
		return false;
	};
	Row->OnPointer = [Write, Min, Range](float LocalX, float Width)
	{
		const float From = Width * 0.5f;
		const float TrackW = Width * 0.5f - 16.f - 96.f;
		if (LocalX >= From - 8.f && TrackW > 1.f)
		{
			Write(Min + Range * FMath::Clamp((LocalX - From) / TrackW, 0.f, 1.f));
		}
	};
}

void UOneBrahamSettingsWidget::AddToggle(UVerticalBox* Page, const FText& Label,
	TFunction<bool(const UOneBrahamGameUserSettings&)> Get,
	TFunction<void(UOneBrahamGameUserSettings&, bool)> Set, const FText& Description)
{
	auto Current = [Get]()
	{
		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		return Settings != nullptr && Get(*Settings);
	};
	TSharedRef<double> ChangedAt = MakeShared<double>(0.0);
	auto Write = [this, Set, ChangedAt](bool bValue)
	{
		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Set(*Settings, bValue);
		}
		*ChangedAt = FPlatformTime::Seconds();
		MarkDirty();
	};

	UOneBrahamPaintedControl* Row = AddRow(Page, Label, Description,
		[Current, ChangedAt](const FPainter& C, const FVector2D& At, const FVector2D& Size, float Lit)
		{
			const bool bOn = Current();
			// The knob slides over 140 ms; the word beside it says the state, so
			// the pill's colour is never the only cue.
			const float Slide = UOneBrahamUIStyle::Tween(FPlatformTime::Seconds(), *ChangedAt, 0.14f, EOneBrahamEase::Ribbon);
			const float Pos = bOn ? Slide : 1.f - Slide;
			const FVector2D Pill(44.f, 22.f);
			const FVector2D PillAt(At.X, At.Y + (Size.Y - Pill.Y) * 0.5f);
			if (bOn)
			{
				Rounded(C, PillAt, Pill, 11.f, Tok(EOneBrahamColour::SignalBlue));
			}
			else
			{
				Rounded(C, PillAt, Pill, 11.f, FLinearColor::Transparent, Tok(EOneBrahamColour::LightGrey, 0.4f), 1.f);
			}
			const float KnobX = PillAt.X + 3.f + (Pill.X - 22.f) * Pos;
			PenCircle(C, FVector2D(KnobX + 8.f, PillAt.Y + 11.f), 8.f, bOn ? Tok(EOneBrahamColour::White) : Tok(EOneBrahamColour::LightGrey, 0.6f), 0.f);
			TextOnBaseline(C, PillAt.X + Pill.X + 14.f, PillAt.Y + 15.f, bOn ? TEXT("ON") : TEXT("OFF"), Type(EOneBrahamType::Telemetry, 11.f, 120),
				Tok(EOneBrahamColour::White, bOn ? 1.f : 0.55f));
		});
	Row->OnActivate = [Current, Write]() { Write(!Current()); };
	Row->OnKey = [Current, Write](const FKey& Key)
	{
		if (OneBrahamSettingsLayout::IsLeft(Key) || OneBrahamSettingsLayout::IsRight(Key))
		{
			Write(OneBrahamSettingsLayout::IsRight(Key));
			return true;
		}
		return false;
	};
}

void UOneBrahamSettingsWidget::AddChoice(UVerticalBox* Page, const FText& Label, const TArray<FText>& Options,
	TFunction<int32(const UOneBrahamGameUserSettings&)> Get,
	TFunction<void(UOneBrahamGameUserSettings&, int32)> Set, const FText& Description)
{
	TArray<FString> Words;
	for (const FText& Option : Options)
	{
		Words.Add(Option.ToString().ToUpper());
	}
	auto Current = [Get]()
	{
		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		return Settings != nullptr ? Get(*Settings) : 0;
	};
	const int32 Count = FMath::Max(1, Options.Num());
	auto Write = [this, Set, Current, Count](int32 Delta)
	{
		const int32 Next = (Current() + Delta + Count) % Count;
		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Set(*Settings, Next);
		}
		MarkDirty();
	};

	UOneBrahamPaintedControl* Row = AddRow(Page, Label, Description,
		[Current, Words](const FPainter& C, const FVector2D& At, const FVector2D& Size, float Lit)
		{
			const int32 Index = Current();
			const FString Word = Words.IsValidIndex(Index) ? Words[Index] : FString(TEXT("-"));
			const float Y = At.Y + Size.Y * 0.5f + 5.f;
			const FSlateFontInfo Font = Type(EOneBrahamType::Telemetry, 14.f, 80);
			const FLinearColor Arrow = Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.35f, 1.f, Lit));
			TextOnBaseline(C, At.X, Y, TEXT("\u2039"), Font, Arrow);
			TextOnBaseline(C, At.X + 22.f, Y, Word, Font, Tok(EOneBrahamColour::White));
			const float WordW = Measure(Word, Font).X;
			TextOnBaseline(C, At.X + 22.f + WordW + 14.f, Y, TEXT("\u203A"), Font, Arrow);
			// Position in the list, as dots: how many there are, which one this is.
			for (int32 Dot = 0; Dot < Words.Num(); ++Dot)
			{
				Box(C, FVector2D(At.X + Size.X - (Words.Num() - Dot) * 8.f, Y - 6.f), FVector2D(4.f, 4.f),
					Dot == Index ? Tok(EOneBrahamColour::SignalBlue) : Tok(EOneBrahamColour::LightGrey, 0.25f));
			}
		});
	Row->OnActivate = [Write]() { Write(1); };
	Row->OnKey = [Write](const FKey& Key)
	{
		if (OneBrahamSettingsLayout::IsLeft(Key) || OneBrahamSettingsLayout::IsRight(Key))
		{
			Write(OneBrahamSettingsLayout::IsRight(Key) ? 1 : -1);
			return true;
		}
		return false;
	};
}

// ==========================================================================
//  Pages
// ==========================================================================
void UOneBrahamSettingsWidget::BuildGraphicsPage()
{
	using namespace OneBrahamSettingsLayout;

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_FloorG", "G"), NSLOCTEXT("OneBraham", "Settings_TabGraphics", "GRAPHICS"));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionDisplay", "DISPLAY"));

	AddChoice(Page, NSLOCTEXT("OneBraham", "Settings_Quality", "Quality preset"),
		{
			NSLOCTEXT("OneBraham", "Settings_QualityLow", "Low"),
			NSLOCTEXT("OneBraham", "Settings_QualityMedium", "Medium"),
			NSLOCTEXT("OneBraham", "Settings_QualityHigh", "High"),
			NSLOCTEXT("OneBraham", "Settings_QualityEpic", "Epic"),
			NSLOCTEXT("OneBraham", "Settings_QualityUltra", "Ultra"),
		},
		[](const UOneBrahamGameUserSettings& S) { return static_cast<int32>(S.GetQualityPreset()); },
		// Not applied immediately: a scalability change mid-drag through a list
		// is a hitch per item. ApplySettings on APPLY / BACK does it once.
		[](UOneBrahamGameUserSettings& S, int32 Index)
		{
			S.SetQualityPreset(static_cast<EOneBrahamQualityPreset>(FMath::Clamp(Index, 0, 4)), false);
		},
		NSLOCTEXT("OneBraham", "Settings_QualityDesc", "Quality: how hard the building works your graphics card. Ultra adds ray tracing."));

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_ResolutionScale", "Resolution scale"), 50.f, 100.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.ResolutionScale; },
		[](UOneBrahamGameUserSettings& S, float V) { S.SetResolutionScalePercent(V, false); },
		[](float V) { return Percent(V); },
		NSLOCTEXT("OneBraham", "Settings_ResolutionDesc", "Resolution scale: below 100% the picture is drawn smaller and sharpened back up."));

	// 70-110 is the settings' own clamp (SetFOV): past that the first-person
	// arms leave the frustum. The field is written directly because SetFOV saves
	// the file on every call, and a slider calls it thirty times a second.
	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_FOV", "Field of view"), 70.f, 110.f, 1.f,
		[](const UOneBrahamGameUserSettings& S) { return S.FOV; },
		[](UOneBrahamGameUserSettings& S, float V) { S.FOV = FMath::Clamp(V, 70.f, 110.f); },
		[](float V) { return FText::Format(NSLOCTEXT("OneBraham", "Settings_Degrees", "{0}\u00B0"), FText::AsNumber(FMath::RoundToInt(V))); },
		NSLOCTEXT("OneBraham", "Settings_FOVDesc", "Field of view: how much of the open-plan office you can see at once."));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionPost", "POST-PROCESSING"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_MotionBlur", "Motion blur"),
		[](const UOneBrahamGameUserSettings& S) { return S.bMotionBlur; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bMotionBlur = B; });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_FilmGrain", "Film grain"),
		[](const UOneBrahamGameUserSettings& S) { return S.bFilmGrain; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bFilmGrain = B; },
		NSLOCTEXT("OneBraham", "Settings_FilmGrainDesc", "Film grain: the texture of the picture. Off is fine."));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_Chromatic", "Chromatic aberration"),
		[](const UOneBrahamGameUserSettings& S) { return S.bChromaticAberration; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bChromaticAberration = B; });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_DepthOfField", "Depth of field"),
		[](const UOneBrahamGameUserSettings& S) { return S.bDepthOfField; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bDepthOfField = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionCamera", "CAMERA"));

	// Zero turns shake off altogether rather than scaling it to nothing, so the
	// shake code never runs for a player who asked for none.
	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_CameraShake", "Camera shake intensity"), 0.f, 100.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.bCameraShake ? S.CameraShakeIntensity * 100.f : 0.f; },
		[](UOneBrahamGameUserSettings& S, float V)
		{
			S.CameraShakeIntensity = FMath::Clamp(V / 100.f, 0.f, 1.f);
			S.bCameraShake = V > 0.f;
		},
		[](float V) { return Percent(V); });
}

void UOneBrahamSettingsWidget::BuildAudioPage()
{
	using namespace OneBrahamSettingsLayout;

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_Floor1", "1"), NSLOCTEXT("OneBraham", "Settings_TabAudio", "AUDIO"));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionVolume", "VOLUME"));

	// Bus names are the ones UOneBrahamGameUserSettings::SetVolume accepts. Each
	// is applied to the sound mix as it moves, so the player hears the change.
	struct FBus { const TCHAR* Name; FText Label; };
	const FBus Buses[] =
	{
		{ TEXT("Master"),		NSLOCTEXT("OneBraham", "Settings_VolMaster", "Master") },
		{ TEXT("Music"),		NSLOCTEXT("OneBraham", "Settings_VolMusic", "Music") },
		{ TEXT("Ambience"),		NSLOCTEXT("OneBraham", "Settings_VolAmbience", "Building ambience") },
		{ TEXT("SFX"),			NSLOCTEXT("OneBraham", "Settings_VolSFX", "Effects") },
		{ TEXT("Weapon"),		NSLOCTEXT("OneBraham", "Settings_VolWeapon", "Weapons") },
		{ TEXT("Footstep"),		NSLOCTEXT("OneBraham", "Settings_VolFootstep", "Footsteps") },
		{ TEXT("Dialogue"),		NSLOCTEXT("OneBraham", "Settings_VolDialogue", "Dialogue") },
		{ TEXT("Announcement"),	NSLOCTEXT("OneBraham", "Settings_VolAnnouncement", "Announcements") },
		{ TEXT("UI"),			NSLOCTEXT("OneBraham", "Settings_VolUI", "Interface") },
		{ TEXT("Voice"),		NSLOCTEXT("OneBraham", "Settings_VolVoice", "Voice chat") },
	};

	for (const FBus& Bus : Buses)
	{
		const FName BusName(Bus.Name);

		AddSlider(Page, Bus.Label, 0.f, 100.f, 1.f,
			[BusName](const UOneBrahamGameUserSettings& S) { return S.GetVolume(BusName) * 100.f; },
			[BusName](UOneBrahamGameUserSettings& S, float V) { S.SetVolume(BusName, V / 100.f, false); },
			[](float V) { return Percent(V); });
	}

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionTannoy", "TANNOY"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_Announcements", "Building announcements"),
		[](const UOneBrahamGameUserSettings& S) { return S.bAnnouncementsEnabled; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bAnnouncementsEnabled = B; },
		NSLOCTEXT("OneBraham", "Settings_AnnouncementsDesc", "Announcements: the building's own voice. It has a lot to say."));
}

void UOneBrahamSettingsWidget::BuildVoicePage()
{
	using namespace OneBrahamSettingsLayout;

	VoiceTabIndex = TabButtons.Num();
	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_Floor2", "2"), NSLOCTEXT("OneBraham", "Settings_TabVoice", "VOICE"));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionChat", "VOICE CHAT"));

	// Menu order is not enum order: push to talk is the default and goes first,
	// and "off" goes last, where nobody selects it by accident.
	static const EOneBrahamVoiceChatMode ModeOrder[] =
	{
		EOneBrahamVoiceChatMode::PushToTalk,
		EOneBrahamVoiceChatMode::VoiceActivation,
		EOneBrahamVoiceChatMode::OpenMic,
		EOneBrahamVoiceChatMode::Muted,
	};

	AddChoice(Page, NSLOCTEXT("OneBraham", "Settings_ChatMode", "Transmit"),
		{
			NSLOCTEXT("OneBraham", "Settings_ModePTT", "Push to talk"),
			NSLOCTEXT("OneBraham", "Settings_ModeVAD", "Voice activation"),
			NSLOCTEXT("OneBraham", "Settings_ModeOpen", "Open microphone"),
			NSLOCTEXT("OneBraham", "Settings_ModeOff", "Off"),
		},
		[](const UOneBrahamGameUserSettings& S)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(ModeOrder); ++Index)
			{
				if (ModeOrder[Index] == S.VoiceChatMode)
				{
					return Index;
				}
			}
			return 0;
		},
		[](UOneBrahamGameUserSettings& S, int32 Index)
		{
			S.SetVoiceChatMode(ModeOrder[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(ModeOrder) - 1)]);
		},
		NSLOCTEXT("OneBraham", "Settings_ChatModeDesc", "Transmit: proximity voice. Colleagues nearby hear you, and so does everyone else."));

	// The settings clamp gain to 0-4; the menu offers 0-200%, because past double
	// the microphone is clipping and the colleague on the other end knows it.
	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_InputGain", "Microphone gain"), 0.f, 200.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.VoiceInputGain * 100.f; },
		[](UOneBrahamGameUserSettings& S, float V) { S.SetVoiceInputGain(V / 100.f); },
		[](float V) { return Percent(V); });

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_OutputGain", "Incoming voice gain"), 0.f, 200.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.VoiceOutputGain * 100.f; },
		[](UOneBrahamGameUserSettings& S, float V) { S.SetVoiceOutputGain(V / 100.f); },
		[](float V) { return Percent(V); });

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_NoiseGate", "Noise gate threshold"), 0.f, 0.05f, 0.001f,
		[](const UOneBrahamGameUserSettings& S) { return S.VoiceActivationThreshold; },
		[](UOneBrahamGameUserSettings& S, float V) { S.SetVoiceActivationThreshold(V); },
		[](float V) { return Decimal(V, 3); },
		NSLOCTEXT("OneBraham", "Settings_NoiseGateNote",
			"Voice activation only. Raise it if the air conditioning keeps opening your microphone."));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionMic", "MICROPHONE"));

	// The microphone panel: permission, device, a 24-segment meter with a peak
	// hold and the gate's tick, and the transmit lamp. Segment count carries
	// the level, so the meter reads without its colours.
	UOneBrahamPaintedControl* Mic = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Mic->Setup(FVector2D(0.f, 128.f), false);
	Mic->PaintFn = [this](const FPainter& C, const FOneBrahamControlState& State)
	{
		TextOnBaseline(C, 16.f, 24.f, MicPermission.ToString(), Type(EOneBrahamType::Body, 15.f), Tok(EOneBrahamColour::White, 0.9f));
		TextOnBaseline(C, 16.f, 46.f, MicDevice.ToString(), Type(EOneBrahamType::TelemetryLight, 12.f, 40), Tok(EOneBrahamColour::LightGrey, 0.6f));

		const float SegW = 8.f;
		const float Gap = 2.f;
		const float Y = 62.f;
		const int32 Lit = FMath::RoundToInt(MicLevel * MicSegments);
		const int32 Peak = FMath::RoundToInt(MicPeak * MicSegments);
		for (int32 Seg = 0; Seg < MicSegments; ++Seg)
		{
			const float Db = -60.f + 60.f * (Seg + 1) / MicSegments;
			const FLinearColor On = Db > -3.f ? Tok(EOneBrahamColour::EmergencyRed) : Db > -12.f ? Tok(EOneBrahamColour::SafetyYellow) : Tok(EOneBrahamColour::SignalBlue);
			Box(C, FVector2D(16.f + Seg * (SegW + Gap), Y), FVector2D(SegW, 12.f), Seg < Lit ? On : Tok(EOneBrahamColour::LightGrey, 0.10f));
		}
		if (Peak > 0)
		{
			Box(C, FVector2D(16.f + Peak * (SegW + Gap) - 2.f, Y - 3.f), FVector2D(2.f, 18.f), Tok(EOneBrahamColour::White, 0.9f));
		}
		// The voice-activation gate.
		const float GateX = 16.f + MicGate * MicSegments * (SegW + Gap);
		Box(C, FVector2D(GateX - 1.f, Y + 14.f), FVector2D(2.f, 6.f), Tok(EOneBrahamColour::SafetyYellow));
		TextOnBaseline(C, GateX + 4.f, Y + 24.f, TEXT("GATE"), Type(EOneBrahamType::TelemetryLight, 9.f, 120), Tok(EOneBrahamColour::SafetyYellow, 0.8f));

		PenCircle(C, FVector2D(22.f, 112.f), 5.f, bMicTransmitting ? Tok(EOneBrahamColour::SignalBlue) : Tok(EOneBrahamColour::LightGrey, 0.15f), 0.f);
		TextOnBaseline(C, 36.f, 116.f, bMicTransmitting ? TEXT("TRANSMITTING") : TEXT("NOT TRANSMITTING"),
			Type(EOneBrahamType::Telemetry, 11.f, 160), Tok(EOneBrahamColour::White, bMicTransmitting ? 1.f : 0.5f));
	};
	Page->AddChildToVerticalBox(Mic)->SetHorizontalAlignment(HAlign_Fill);
	AddNote(Page, NSLOCTEXT("OneBraham", "Settings_MicLampNote",
		"The lamp is lit while your voice is being sent. Speak normally to test the gate; hold push to talk to test the key."));
}

void UOneBrahamSettingsWidget::BuildGameplayPage()
{
	using namespace OneBrahamSettingsLayout;

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_Floor3", "3"), NSLOCTEXT("OneBraham", "Settings_TabGameplay", "GAMEPLAY"));

	auto Sensitivity = [](float V) { return Decimal(V, 1); };

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionMouse", "MOUSE"));

	// One slider for both axes: the settings store X and Y separately, and a
	// player who wants them different is a player who edits the ini.
	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_MouseSens", "Sensitivity"), 0.5f, 50.f, 0.5f,
		[](const UOneBrahamGameUserSettings& S) { return S.MouseSensitivityX * SensitivityDisplayScale; },
		[](UOneBrahamGameUserSettings& S, float V)
		{
			S.MouseSensitivityX = FMath::Clamp(V / SensitivityDisplayScale, 0.005f, 1.f);
			S.MouseSensitivityY = S.MouseSensitivityX;
		},
		Sensitivity);

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_ADSSens", "Aiming sensitivity"), 10.f, 100.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.MouseSensitivityADSMultiplier * 100.f; },
		[](UOneBrahamGameUserSettings& S, float V) { S.MouseSensitivityADSMultiplier = FMath::Clamp(V / 100.f, 0.1f, 1.f); },
		[](float V) { return Percent(V); });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_InvertMouse", "Invert vertical"),
		[](const UOneBrahamGameUserSettings& S) { return S.bInvertMouseY; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bInvertMouseY = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionPad", "CONTROLLER"));

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_PadSens", "Look sensitivity"), 0.5f, 50.f, 0.5f,
		[](const UOneBrahamGameUserSettings& S) { return S.GamepadSensitivityX * SensitivityDisplayScale; },
		[](UOneBrahamGameUserSettings& S, float V)
		{
			S.GamepadSensitivityX = FMath::Clamp(V / SensitivityDisplayScale, 0.005f, 1.f);
			S.GamepadSensitivityY = S.GamepadSensitivityX;
		},
		Sensitivity);

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_InvertPad", "Invert vertical"),
		[](const UOneBrahamGameUserSettings& S) { return S.bInvertGamepadY; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bInvertGamepadY = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionMovement", "MOVEMENT"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_HoldSprint", "Hold to sprint"),
		[](const UOneBrahamGameUserSettings& S) { return S.bHoldToSprint; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bHoldToSprint = B; });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_ToggleCrouch", "Toggle crouch"),
		[](const UOneBrahamGameUserSettings& S) { return S.bToggleCrouch; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bToggleCrouch = B; });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_ToggleADS", "Toggle aim"),
		[](const UOneBrahamGameUserSettings& S) { return S.bToggleADS; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bToggleADS = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionComfort", "COMFORT"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_ViewBob", "View bobbing"),
		[](const UOneBrahamGameUserSettings& S) { return S.bViewBobbing; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bViewBobbing = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionContent", "CONTENT"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_Gore", "Blood"),
		[](const UOneBrahamGameUserSettings& S) { return S.IsGoreEnabled(); },
		[](UOneBrahamGameUserSettings& S, bool B) { S.SetGoreEnabled(B); },
		NSLOCTEXT("OneBraham", "Settings_GoreDesc", "Blood: off keeps the carpets clean. HR will be relieved."));
}

void UOneBrahamSettingsWidget::BuildAccessibilityPage()
{
	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_Floor5", "5"), NSLOCTEXT("OneBraham", "Settings_TabAccess", "ACCESSIBILITY"));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionMotion", "MOTION"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_ReduceMotion", "Reduce motion"),
		[](const UOneBrahamGameUserSettings& S) { return S.bReduceMotion; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bReduceMotion = B; },
		NSLOCTEXT("OneBraham", "Settings_ReduceMotionDesc", "Reduce motion: one held shot in the menus, no drift, no shake, nothing that blinks fast."));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionHUD", "INTERFACE"));

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_HUDScale", "HUD scale"), 80.f, 120.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.HUDScale * 100.f; },
		[](UOneBrahamGameUserSettings& S, float V) { S.HUDScale = FMath::Clamp(V / 100.f, 0.8f, 1.2f); },
		[](float V) { return OneBrahamSettingsLayout::Percent(V); },
		NSLOCTEXT("OneBraham", "Settings_HUDScaleDesc", "HUD scale: every sign in the building, larger or smaller."));

	// Menu order is the spec's; the enum keeps its saved order.
	static const EOneBrahamColourBlindMode Modes[] =
	{
		EOneBrahamColourBlindMode::Off,
		EOneBrahamColourBlindMode::Deuteranopia,
		EOneBrahamColourBlindMode::Protanopia,
		EOneBrahamColourBlindMode::Tritanopia,
	};
	AddChoice(Page, NSLOCTEXT("OneBraham", "Settings_ColourBlind", "Colour-blind mode"),
		{
			NSLOCTEXT("OneBraham", "Settings_CBOff", "Off"),
			NSLOCTEXT("OneBraham", "Settings_CBDeuter", "Deuteranopia"),
			NSLOCTEXT("OneBraham", "Settings_CBProtan", "Protanopia"),
			NSLOCTEXT("OneBraham", "Settings_CBTritan", "Tritanopia"),
		},
		[](const UOneBrahamGameUserSettings& S)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Modes); ++Index)
			{
				if (Modes[Index] == S.ColourBlindMode)
				{
					return Index;
				}
			}
			return 0;
		},
		[](UOneBrahamGameUserSettings& S, int32 Index) { S.ColourBlindMode = Modes[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(Modes) - 1)]; },
		NSLOCTEXT("OneBraham", "Settings_ColourBlindDesc", "Colour-blind mode: moves danger off red (or warnings off yellow). Every state keeps its shape."));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_CBMarkers", "Shape-coded markers"),
		[](const UOneBrahamGameUserSettings& S) { return S.bColorBlindFriendlyMarkers; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bColorBlindFriendlyMarkers = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionSubtitles", "SUBTITLES"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_Subtitles", "Subtitles"),
		[](const UOneBrahamGameUserSettings& S) { return S.bSubtitlesEnabled; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bSubtitlesEnabled = B; },
		NSLOCTEXT("OneBraham", "Settings_SubtitlesDesc", "Subtitles: every announcement and colleague, written down."));

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_SubtitleSize", "Subtitle size"), 75.f, 150.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.SubtitleScale * 100.f; },
		[](UOneBrahamGameUserSettings& S, float V) { S.SubtitleScale = FMath::Clamp(V / 100.f, 0.75f, 1.5f); },
		[](float V) { return OneBrahamSettingsLayout::Percent(V); });
}

void UOneBrahamSettingsWidget::BuildControlsPage()
{
	using namespace OneBrahamSettingsLayout;

	ControlsTabIndex = TabButtons.Num();
	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_Floor4", "4"), NSLOCTEXT("OneBraham", "Settings_TabControls", "CONTROLS"));

	AddNote(Page, NSLOCTEXT("OneBraham", "Settings_ControlsNote",
		"Key bindings are listed for reference. Rebinding is not available in this build."));

	UOneBrahamPaintedControl* Headings = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Headings->Setup(FVector2D(0.f, 34.f), false);
	Headings->PaintFn = [](const FPainter& C, const FOneBrahamControlState&)
	{
		const FSlateFontInfo Font = Type(EOneBrahamType::TelemetryLight, 10.f, 160);
		const FLinearColor Ink = Tok(EOneBrahamColour::LightGrey, 0.45f);
		TextOnBaseline(C, 16.f, 24.f, TEXT("ACTION"), Font, Ink);
		TextOnBaseline(C, 16.f + LabelWidth, 24.f, TEXT("KEYBOARD AND MOUSE"), Font, Ink);
		TextOnBaseline(C, 16.f + LabelWidth + ControlWidth, 24.f, TEXT("CONTROLLER"), Font, Ink);
		Box(C, FVector2D(0.f, C.Size().Y - 1.f), FVector2D(C.Size().X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.12f));
	};
	Page->AddChildToVerticalBox(Headings)->SetHorizontalAlignment(HAlign_Fill);

	ControlsRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsRows"));
	Page->AddChildToVerticalBox(ControlsRows);

	RefreshControlsRows();
}

void UOneBrahamSettingsWidget::RefreshControlsRows()
{
	using namespace OneBrahamSettingsLayout;

	if (ControlsRows == nullptr)
	{
		return;
	}

	ControlsRows->ClearChildren();

	const TArray<FOneBrahamBindingLine> Lines = GatherBindings(GetOwningPlayer());
	if (Lines.IsEmpty())
	{
		AddNote(ControlsRows, NSLOCTEXT("OneBraham", "Settings_NoBindings",
			"No input mapping contexts are loaded. Run the input content build."));
		return;
	}

	for (const FOneBrahamBindingLine& Line : Lines)
	{
		UOneBrahamPaintedControl* Row = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
		Row->Setup(FVector2D(0.f, 36.f), false);
		const FString Action = Line.Action.ToString();
		const FString Keys = Line.KeyboardMouse.ToString();
		const FString Pad = Line.Controller.ToString();
		Row->PaintFn = [Action, Keys, Pad](const FPainter& C, const FOneBrahamControlState&)
		{
			TextOnBaseline(C, 16.f, 24.f, Action, Type(EOneBrahamType::Label, 13.f, 80), Tok(EOneBrahamColour::White, 0.9f));
			TextOnBaseline(C, 16.f + LabelWidth, 24.f, Keys, Type(EOneBrahamType::Telemetry, 13.f), Tok(EOneBrahamColour::White));
			TextOnBaseline(C, 16.f + LabelWidth + ControlWidth, 24.f, Pad, Type(EOneBrahamType::TelemetryLight, 13.f), Tok(EOneBrahamColour::LightGrey, 0.7f));
			Box(C, FVector2D(0.f, C.Size().Y - 1.f), FVector2D(C.Size().X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.06f));
		};
		ControlsRows->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Fill);
	}
}

FString UOneBrahamSettingsWidget::KeyLabel(const FKey& Key)
{
	// Engine display names are written for the editor ("Mouse XY 2D-Axis",
	// "Gamepad Face Button Bottom"); the ones a player reads are spelled out
	// here. Both axes of a stick print as the stick, so X and Y mappings of
	// one action collapse into one entry.
	static const TMap<FName, FString> Labels =
	{
		{ EKeys::Mouse2D.GetFName(),					TEXT("MOUSE") },
		{ EKeys::MouseX.GetFName(),						TEXT("MOUSE") },
		{ EKeys::MouseY.GetFName(),						TEXT("MOUSE") },
		{ EKeys::MouseWheelAxis.GetFName(),				TEXT("MOUSE WHEEL") },
		{ EKeys::MouseScrollUp.GetFName(),				TEXT("WHEEL UP") },
		{ EKeys::MouseScrollDown.GetFName(),			TEXT("WHEEL DOWN") },
		{ EKeys::LeftMouseButton.GetFName(),			TEXT("LEFT MOUSE") },
		{ EKeys::RightMouseButton.GetFName(),			TEXT("RIGHT MOUSE") },
		{ EKeys::MiddleMouseButton.GetFName(),			TEXT("MIDDLE MOUSE") },
		{ EKeys::ThumbMouseButton.GetFName(),			TEXT("MOUSE 4") },
		{ EKeys::ThumbMouseButton2.GetFName(),			TEXT("MOUSE 5") },
		{ EKeys::Gamepad_Left2D.GetFName(),				TEXT("LEFT STICK") },
		{ EKeys::Gamepad_LeftX.GetFName(),				TEXT("LEFT STICK") },
		{ EKeys::Gamepad_LeftY.GetFName(),				TEXT("LEFT STICK") },
		{ EKeys::Gamepad_Right2D.GetFName(),			TEXT("RIGHT STICK") },
		{ EKeys::Gamepad_RightX.GetFName(),				TEXT("RIGHT STICK") },
		{ EKeys::Gamepad_RightY.GetFName(),				TEXT("RIGHT STICK") },
		{ EKeys::Gamepad_LeftThumbstick.GetFName(),		TEXT("L3") },
		{ EKeys::Gamepad_RightThumbstick.GetFName(),	TEXT("R3") },
		{ EKeys::Gamepad_FaceButton_Bottom.GetFName(),	TEXT("A / CROSS") },
		{ EKeys::Gamepad_FaceButton_Right.GetFName(),	TEXT("B / CIRCLE") },
		{ EKeys::Gamepad_FaceButton_Left.GetFName(),	TEXT("X / SQUARE") },
		{ EKeys::Gamepad_FaceButton_Top.GetFName(),		TEXT("Y / TRIANGLE") },
		{ EKeys::Gamepad_LeftShoulder.GetFName(),		TEXT("LB / L1") },
		{ EKeys::Gamepad_RightShoulder.GetFName(),		TEXT("RB / R1") },
		{ EKeys::Gamepad_LeftTrigger.GetFName(),		TEXT("LT / L2") },
		{ EKeys::Gamepad_LeftTriggerAxis.GetFName(),	TEXT("LT / L2") },
		{ EKeys::Gamepad_RightTrigger.GetFName(),		TEXT("RT / R2") },
		{ EKeys::Gamepad_RightTriggerAxis.GetFName(),	TEXT("RT / R2") },
		{ EKeys::Gamepad_DPad_Up.GetFName(),			TEXT("D-PAD UP") },
		{ EKeys::Gamepad_DPad_Down.GetFName(),			TEXT("D-PAD DOWN") },
		{ EKeys::Gamepad_DPad_Left.GetFName(),			TEXT("D-PAD LEFT") },
		{ EKeys::Gamepad_DPad_Right.GetFName(),			TEXT("D-PAD RIGHT") },
		{ EKeys::Gamepad_Special_Left.GetFName(),		TEXT("VIEW / SHARE") },
		{ EKeys::Gamepad_Special_Right.GetFName(),		TEXT("MENU / OPTIONS") },
	};

	if (const FString* Label = Labels.Find(Key.GetFName()))
	{
		return *Label;
	}

	return Key.GetDisplayName(/*bLongDisplayName=*/ false).ToString().ToUpper();
}

TArray<FOneBrahamBindingLine> UOneBrahamSettingsWidget::GatherBindings(const APlayerController* Owner)
{
	TArray<FOneBrahamBindingLine> Lines;

	// What the player actually has: the controller's contexts are the ones it
	// installed (an authored asset, or the context it built from the ini with
	// the player's rebinds). Only without a One Braham controller - the main
	// menu's page before any is spawned, a test - fall back to the settings.
	const UOneBrahamGameSettings& GameSettings = UOneBrahamGameSettings::Get();
	const AOneBrahamPlayerController* Controller = Cast<AOneBrahamPlayerController>(Owner);

	const UInputMappingContext* Gameplay = Controller != nullptr ? Controller->GameplayMappingContext.Get() : nullptr;
	const UInputMappingContext* Common = Controller != nullptr ? Controller->CommonMappingContext.Get() : nullptr;
	if (Gameplay == nullptr && !GameSettings.DefaultMappingContext.IsNull())
	{
		Gameplay = GameSettings.DefaultMappingContext.LoadSynchronous();
	}
	if (Common == nullptr && !GameSettings.CommonMappingContext.IsNull())
	{
		Common = GameSettings.CommonMappingContext.LoadSynchronous();
	}

	const UInputMappingContext* Contexts[] = { Gameplay, Common };

	// One key as found: its label, and where it sorts within its action.
	struct FKeyEntry
	{
		FString Label;
		int32 Rank = 0;
		int32 Index = 0;
	};

	// A button on an axis action reads as a direction, set by the modifiers
	// both the authored contexts and BuildMappingContextFromIni use (Move:
	// swizzle = Y, negate = minus). Sorting by direction lists Move as
	// W, A, S, D whatever order the context holds the mappings in; Lean lists
	// its negative (left) key first. Analog keys and plain buttons keep the
	// order the context lists them.
	auto DirectionRank = [](const UInputAction& Action, const FEnhancedActionKeyMapping& Mapping) -> int32
	{
		if (Mapping.Key.IsAnalog() || Action.ValueType == EInputActionValueType::Boolean)
		{
			return 0;
		}

		bool bSwizzle = false;
		bool bNegate = false;
		for (const UInputModifier* Modifier : Mapping.Modifiers)
		{
			bSwizzle |= Modifier != nullptr && Modifier->IsA<UInputModifierSwizzleAxis>();
			bNegate |= Modifier != nullptr && Modifier->IsA<UInputModifierNegate>();
		}

		if (Action.ValueType == EInputActionValueType::Axis1D)
		{
			return bNegate ? 0 : 1;
		}

		// Forward, left, back, right: the WASD reading order.
		return bSwizzle ? (bNegate ? 2 : 0) : (bNegate ? 1 : 3);
	};

	// Action -> (keyboard keys, pad keys), in the order the contexts list them,
	// so the page reads in the order the input designer laid the actions out.
	// Every mapping of an action lands on that action's one line: Move's four
	// keys are one row, not four.
	TArray<const UInputAction*> Order;
	TMap<const UInputAction*, TPair<TArray<FKeyEntry>, TArray<FKeyEntry>>> Keys;

	for (const UInputMappingContext* Context : Contexts)
	{
		if (Context == nullptr)
		{
			continue;
		}

		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == nullptr || !Mapping.Key.IsValid())
			{
				continue;
			}

			if (!Keys.Contains(Mapping.Action))
			{
				Order.Add(Mapping.Action);
			}

			TPair<TArray<FKeyEntry>, TArray<FKeyEntry>>& Entry = Keys.FindOrAdd(Mapping.Action);
			TArray<FKeyEntry>& Column = Mapping.Key.IsGamepadKey() ? Entry.Value : Entry.Key;

			FKeyEntry Key;
			Key.Label = KeyLabel(Mapping.Key);
			Key.Rank = DirectionRank(*Mapping.Action, Mapping);
			Key.Index = Column.Num();
			Column.Add(MoveTemp(Key));
		}
	}

	auto Join = [](TArray<FKeyEntry>& Column) -> FText
	{
		if (Column.IsEmpty())
		{
			return NSLOCTEXT("OneBraham", "Settings_Unbound", "-");
		}

		Column.Sort([](const FKeyEntry& A, const FKeyEntry& B)
		{
			return A.Rank != B.Rank ? A.Rank < B.Rank : A.Index < B.Index;
		});

		// Each key once: both contexts may map it, and both axes of a stick
		// print as the stick. Commas, since a pad label carries its own slash.
		TArray<FString> Labels;
		for (const FKeyEntry& Key : Column)
		{
			Labels.AddUnique(Key.Label);
		}
		return FText::FromString(FString::Join(Labels, TEXT(", ")));
	};

	for (const UInputAction* Action : Order)
	{
		TPair<TArray<FKeyEntry>, TArray<FKeyEntry>>& Entry = Keys[Action];

		FOneBrahamBindingLine Line;

		// An action's own description wins; otherwise its asset name, without the
		// IA_ prefix, set in signage case ("IA_VoiceLineMenu" -> "VOICE LINE MENU").
		if (!Action->ActionDescription.IsEmpty())
		{
			Line.Action = Action->ActionDescription.ToUpper();
		}
		else
		{
			FString Name = Action->GetName();
			Name.RemoveFromStart(TEXT("IA_"));
			Line.Action = SignageCase(Name);
		}

		Line.KeyboardMouse = Join(Entry.Key);
		Line.Controller = Join(Entry.Value);
		Lines.Add(MoveTemp(Line));
	}

	return Lines;
}

// ==========================================================================
//  Behaviour
// ==========================================================================
void UOneBrahamSettingsWidget::SyncFromSettings()
{
	// The painted controls read the settings as they draw; nothing to push.
	for (const TFunction<void()>& Sync : Syncers)
	{
		Sync();
	}

	bDirty = false;
	StatusText = NSLOCTEXT("OneBraham", "Settings_StatusIdle", "Changes are applied and saved when you leave this page.");

	NextVoicePollSeconds = 0.0;
	RefreshVoiceStatus();
}

void UOneBrahamSettingsWidget::FocusDefault()
{
	if (TabButtons.IsValidIndex(ActiveTab) && TabButtons[ActiveTab] != nullptr)
	{
		TabButtons[ActiveTab]->SetKeyboardFocus();
	}
}

UWidget* UOneBrahamSettingsWidget::GetDefaultFocusTarget() const
{
	return TabButtons.IsValidIndex(ActiveTab) ? TabButtons[ActiveTab].Get() : nullptr;
}

void UOneBrahamSettingsWidget::SelectTab(int32 Index)
{
	if (Pages == nullptr || !TabButtons.IsValidIndex(Index))
	{
		return;
	}

	if (Index != ActiveTab || TabChangedAt <= 0.0)
	{
		TabChangedAt = FPlatformTime::Seconds();
	}
	ActiveTab = Index;
	Pages->SetActiveWidgetIndex(Index);
	FocusedDescription = FText::GetEmpty();

	if (Index == VoiceTabIndex)
	{
		NextVoicePollSeconds = 0.0;
		RefreshVoiceStatus();
	}
	else if (Index == ControlsTabIndex)
	{
		RefreshControlsRows();
	}
}

void UOneBrahamSettingsWidget::CycleTab(int32 Direction)
{
	const int32 Count = TabButtons.Num();
	if (Count == 0)
	{
		return;
	}

	SelectTab((ActiveTab + Direction + Count) % Count);
	FocusDefault();
}

void UOneBrahamSettingsWidget::MarkDirty()
{
	if (bDirty)
	{
		return;
	}

	bDirty = true;
	StatusText = NSLOCTEXT("OneBraham", "Settings_StatusDirty", "Unapplied changes. They will be applied when you leave, or press APPLY.");
}

void UOneBrahamSettingsWidget::ApplyAndSave()
{
	UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings == nullptr)
	{
		return;
	}

	// ApplySettings pushes scalability, post-processing, audio and voice, sets
	// the gore CVar and broadcasts OnSettingsChanged - the controller re-reads
	// FOV and sensitivity from that. SaveSettings writes GameUserSettings.ini.
	Settings->ApplySettings(false);
	Settings->SaveSettings();
	bDirty = false;

	StatusText = NSLOCTEXT("OneBraham", "Settings_StatusSaved", "Applied and saved.");

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] Settings applied and saved from the in-match menu."));
}

void UOneBrahamSettingsWidget::ApplyIfDirty()
{
	if (bDirty)
	{
		ApplyAndSave();
	}
}

void UOneBrahamSettingsWidget::RequestBack()
{
	ApplyIfDirty();
	OnBackRequested.ExecuteIfBound();
}

// ==========================================================================
//  Voice status
// ==========================================================================
int32 UOneBrahamSettingsWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Real time, not world time: the pause menu pauses a standalone game, and a
	// lamp that freezes while the player is testing their microphone is useless.
	if (ActiveTab == VoiceTabIndex)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextVoicePollSeconds)
		{
			NextVoicePollSeconds = Now + 0.05;
			RefreshVoiceStatus();
		}
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
}

void UOneBrahamSettingsWidget::RefreshVoiceStatus() const
{
	const AOneBrahamPlayerState* State = GetOwningPlayerState<AOneBrahamPlayerState>();
	const UOneBrahamProximityVoiceComponent* Voice = State != nullptr ? State->GetVoiceComponent() : nullptr;

	// --- permission / availability -------------------------------------------
	// The OS answer is the specific one, so it is checked first: "unavailable"
	// tells a player nothing, "macOS has refused" tells them where to go.
	switch (OneBrahamMicrophonePermission::GetStatus())
	{
	case EOneBrahamMicrophonePermission::Denied:
		MicPermission = NSLOCTEXT("OneBraham", "Settings_MicDenied",
			"Microphone access has been refused. Allow On Break in System Settings > Privacy & Security > Microphone, then restart the game.");
		break;
	case EOneBrahamMicrophonePermission::Restricted:
		MicPermission = NSLOCTEXT("OneBraham", "Settings_MicRestricted",
			"Microphone access is blocked by a device policy. Your administrator will have opinions.");
		break;
	case EOneBrahamMicrophonePermission::MissingUsageDescription:
		MicPermission = NSLOCTEXT("OneBraham", "Settings_MicNoUsage",
			"This build cannot ask for microphone access. Voice chat is listen-only.");
		break;
	case EOneBrahamMicrophonePermission::NotDetermined:
		MicPermission = NSLOCTEXT("OneBraham", "Settings_MicNotAsked",
			"Microphone access has not been requested yet. You will be asked the first time you transmit.");
		break;
	default:
		if (Voice != nullptr && Voice->IsMicrophonePermissionDenied())
		{
			MicPermission = NSLOCTEXT("OneBraham", "Settings_MicDeniedComponent",
				"Microphone access has been refused by the system.");
		}
		else if (Voice != nullptr && !Voice->HasWorkingMicrophone())
		{
			MicPermission = NSLOCTEXT("OneBraham", "Settings_MicUnavailable",
				"No working microphone. Check that one is connected and selected as the system input.");
		}
		else
		{
			MicPermission = NSLOCTEXT("OneBraham", "Settings_MicReady", "Microphone ready.");
		}
		break;
	}

	const FString Device = Voice != nullptr ? Voice->GetCaptureDeviceName() : FString();
	MicDevice = FText::Format(NSLOCTEXT("OneBraham", "Settings_MicDevice", "INPUT \u00B7 {0}"),
		Device.IsEmpty() ? NSLOCTEXT("OneBraham", "Settings_MicDeviceDefault", "SYSTEM DEFAULT") : FText::FromString(Device.ToUpper()));

	// --- level -----------------------------------------------------------------
	// A peak tick that holds 600 ms, then falls to the level.
	const double Now = FPlatformTime::Seconds();
	MicLevel = Voice != nullptr ? Voice->GetInputMeter() : 0.f;
	MicGate = Voice != nullptr ? Voice->GetGateMeter() : 0.f;
	if (MicLevel >= MicPeak || Now - MicPeakAt > 0.6)
	{
		MicPeak = MicLevel;
		MicPeakAt = Now;
	}

	// --- activity --------------------------------------------------------------
	bMicTransmitting = Voice != nullptr && Voice->IsLocalTransmitting();
}
