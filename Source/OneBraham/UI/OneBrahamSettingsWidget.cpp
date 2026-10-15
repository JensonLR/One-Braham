// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamSettingsWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerState.h"

namespace
{
	/** The Settings microphone meter's width, px. */
	constexpr float MicMeterWidth = 320.f;
}
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Voice/OneBrahamMicrophonePermission.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"

namespace OneBrahamSettingsLayout
{
	constexpr float LabelWidth	= 300.f;
	constexpr float ControlWidth	= 340.f;
	constexpr float ValueWidth	= 96.f;
	constexpr float RowHeight	= 38.f;
	constexpr float PanelWidth	= 980.f;
	constexpr float PageHeight	= 560.f;

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

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsRoot"));

	// --- title -------------------------------------------------------------
	Column->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Settings_Kicker", "FACILITIES / PREFERENCES"),
		EOneBrahamTextSize::Micro, InkMuted()));
	Column->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Settings_Title", "SETTINGS"),
		EOneBrahamTextSize::Header, Ink(), true))->SetPadding(FMargin(0.f, 4.f, 0.f, 16.f));

	// --- tab strip ---------------------------------------------------------
	UHorizontalBox* TabStrip = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabStrip"));
	Column->AddChildToVerticalBox(TabStrip);
	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Pages"));
	USizeBox* PageFrame = MakeCell(Pages, PanelWidth - 64.f, PageHeight);
	Column->AddChildToVerticalBox(PageFrame);

	BuildGraphicsPage();
	BuildAudioPage();
	BuildVoicePage();
	BuildGameplayPage();
	BuildControlsPage();

	// --- footer ------------------------------------------------------------
	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 12.f, 0.f, 12.f));

	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Text_Status = MakeLabel(NSLOCTEXT("OneBraham", "Settings_StatusIdle",
		"Changes are applied and saved when you leave this page."), EOneBrahamTextSize::Micro, InkMuted());
	UHorizontalBoxSlot* StatusSlot = Footer->AddChildToHorizontalBox(Text_Status);
	StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	StatusSlot->SetVerticalAlignment(VAlign_Center);

	Footer->AddChildToHorizontalBox(MakeButton(NSLOCTEXT("OneBraham", "Settings_Apply", "APPLY"),
		[this]() { ApplyAndSave(); }, EOneBrahamTextSize::Small))->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	Footer->AddChildToHorizontalBox(MakeButton(NSLOCTEXT("OneBraham", "Settings_Back", "BACK"),
		[this]() { RequestBack(); }, EOneBrahamTextSize::Small));
	Column->AddChildToVerticalBox(Footer);

	WidgetTree->RootWidget = Column;

	// Tab buttons go into the strip built above. They were created by AddPage
	// in page order; hook them up now that every page exists.
	for (int32 Index = 0; Index < TabButtons.Num(); ++Index)
	{
		UVerticalBox* Tab = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Tab->AddChildToVerticalBox(TabButtons[Index]);
		Tab->AddChildToVerticalBox(TabUnderlines[Index])->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		TabStrip->AddChildToHorizontalBox(Tab)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	}

	SelectTab(0);
}

UVerticalBox* UOneBrahamSettingsWidget::AddPage(const FText& TabName)
{
	const int32 Index = TabButtons.Num();

	UTextBlock* TabLabel = nullptr;
	UButton* Tab = MakeButton(TabName, [this, Index]() { SelectTab(Index); }, EOneBrahamTextSize::Small,
		true, &TabLabel, FMargin(14.f, 7.f));
	TabButtons.Add(Tab);

	// The active tab is marked by an accent underline rather than by restyling
	// the button, so it never fights the focus styling a gamepad user relies on.
	TabUnderlines.Add(MakeSwatch(FLinearColor(0.f, 0.f, 0.f, 0.f), FVector2D(4.f, 2.f)));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Scroll->SetScrollbarThickness(FVector2D(2.f, 2.f));
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
	Page->AddChildToVerticalBox(MakeLabel(Title, EOneBrahamTextSize::Micro, InkMuted(), true))
		->SetPadding(FMargin(0.f, bFirst ? 0.f : 18.f, 0.f, 4.f));
	Page->AddChildToVerticalBox(MakeRule(FLinearColor(Ink().R, Ink().G, Ink().B, 0.1f)))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
}

void UOneBrahamSettingsWidget::AddNote(UVerticalBox* Page, const FText& Text, UTextBlock** OutText)
{
	UTextBlock* Note = MakeLabel(Text, EOneBrahamTextSize::Micro, InkMuted());
	Note->SetAutoWrapText(true);
	Page->AddChildToVerticalBox(Note)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));

	if (OutText != nullptr)
	{
		*OutText = Note;
	}
}

void UOneBrahamSettingsWidget::AddSettingRow(UVerticalBox* Page, const FText& Label, UWidget* Control, UTextBlock* Value)
{
	using namespace OneBrahamSettingsLayout;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	Row->AddChildToHorizontalBox(MakeCell(MakeLabel(Label, EOneBrahamTextSize::Small, Ink()), LabelWidth, RowHeight))
		->SetVerticalAlignment(VAlign_Center);

	// The control sits in a fixed column, left-aligned within it: a checkbox is
	// not stretched to the width of a slider.
	USizeBox* ControlCell = MakeCell(nullptr, ControlWidth, RowHeight);
	UHorizontalBox* ControlAlign = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UHorizontalBoxSlot* ControlSlot = ControlAlign->AddChildToHorizontalBox(Control);
	ControlSlot->SetVerticalAlignment(VAlign_Center);
	if (Control->IsA<USlider>() || Control->IsA<UComboBoxString>())
	{
		ControlSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	ControlCell->AddChild(ControlAlign);
	Row->AddChildToHorizontalBox(ControlCell)->SetVerticalAlignment(VAlign_Center);

	if (Value != nullptr)
	{
		Row->AddChildToHorizontalBox(MakeCell(Value, ValueWidth, RowHeight))->SetVerticalAlignment(VAlign_Center);
	}

	Page->AddChildToVerticalBox(Row);
}

void UOneBrahamSettingsWidget::AddSlider(UVerticalBox* Page, const FText& Label, float Min, float Max, float Step,
	TFunction<float(const UOneBrahamGameUserSettings&)> Get,
	TFunction<void(UOneBrahamGameUserSettings&, float)> Set,
	TFunction<FText(float)> Format)
{
	UTextBlock* Value = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted(), false, ETextJustify::Right);

	USlider* Slider = MakeSlider(Min, Max, Step, [this, Set, Format, Value](float NewValue)
	{
		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Set(*Settings, NewValue);
		}

		Value->SetText(Format(NewValue));
		MarkDirty();
	});

	Syncers.Add([Slider, Value, Get, Format]()
	{
		if (const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			const float Current = Get(*Settings);
			Slider->SetValue(Current);
			Value->SetText(Format(Current));
		}
	});

	AddSettingRow(Page, Label, Slider, Value);
}

void UOneBrahamSettingsWidget::AddToggle(UVerticalBox* Page, const FText& Label,
	TFunction<bool(const UOneBrahamGameUserSettings&)> Get,
	TFunction<void(UOneBrahamGameUserSettings&, bool)> Set)
{
	UTextBlock* Value = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted(), false, ETextJustify::Right);

	auto Word = [](bool bOn)
	{
		return bOn ? NSLOCTEXT("OneBraham", "Settings_On", "ON") : NSLOCTEXT("OneBraham", "Settings_Off", "OFF");
	};

	UCheckBox* Box = MakeCheckBox([this, Set, Value, Word](bool bChecked)
	{
		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Set(*Settings, bChecked);
		}

		Value->SetText(Word(bChecked));
		MarkDirty();
	});

	Syncers.Add([Box, Value, Get, Word]()
	{
		if (const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			const bool bCurrent = Get(*Settings);
			Box->SetIsChecked(bCurrent);
			Value->SetText(Word(bCurrent));
		}
	});

	AddSettingRow(Page, Label, Box, Value);
}

void UOneBrahamSettingsWidget::AddChoice(UVerticalBox* Page, const FText& Label, const TArray<FText>& Options,
	TFunction<int32(const UOneBrahamGameUserSettings&)> Get,
	TFunction<void(UOneBrahamGameUserSettings&, int32)> Set)
{
	UOneBrahamComboBox* Combo = MakeCombo(Options, [this, Set](int32 Index)
	{
		if (Index == INDEX_NONE)
		{
			return;
		}

		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Set(*Settings, Index);
		}

		MarkDirty();
	});

	Syncers.Add([Combo, Get]()
	{
		if (const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			// Direct selection: the binder ignores it, so syncing is not a change.
			Combo->SetSelectedIndex(Get(*Settings));
		}
	});

	AddSettingRow(Page, Label, Combo, nullptr);
}

// ==========================================================================
//  Pages
// ==========================================================================
void UOneBrahamSettingsWidget::BuildGraphicsPage()
{
	using namespace OneBrahamSettingsLayout;

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_TabGraphics", "GRAPHICS"));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionDisplay", "DISPLAY"));

	AddChoice(Page, NSLOCTEXT("OneBraham", "Settings_Quality", "Quality preset"),
		{
			NSLOCTEXT("OneBraham", "Settings_QualityLow", "Low"),
			NSLOCTEXT("OneBraham", "Settings_QualityMedium", "Medium"),
			NSLOCTEXT("OneBraham", "Settings_QualityHigh", "High"),
			NSLOCTEXT("OneBraham", "Settings_QualityEpic", "Epic"),
			NSLOCTEXT("OneBraham", "Settings_QualityUltra", "Ultra (ray traced)"),
		},
		[](const UOneBrahamGameUserSettings& S) { return static_cast<int32>(S.GetQualityPreset()); },
		// Not applied immediately: a scalability change mid-drag through a list
		// is a hitch per item. ApplySettings on APPLY / BACK does it once.
		[](UOneBrahamGameUserSettings& S, int32 Index)
		{
			S.SetQualityPreset(static_cast<EOneBrahamQualityPreset>(FMath::Clamp(Index, 0, 4)), false);
		});

	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_ResolutionScale", "Resolution scale"), 50.f, 100.f, 5.f,
		[](const UOneBrahamGameUserSettings& S) { return S.ResolutionScale; },
		[](UOneBrahamGameUserSettings& S, float V) { S.SetResolutionScalePercent(V, false); },
		[](float V) { return Percent(V); });

	// 70-110 is the settings' own clamp (SetFOV): past that the first-person
	// arms leave the frustum. The field is written directly because SetFOV saves
	// the file on every call, and a slider calls it thirty times a second.
	AddSlider(Page, NSLOCTEXT("OneBraham", "Settings_FOV", "Field of view"), 70.f, 110.f, 1.f,
		[](const UOneBrahamGameUserSettings& S) { return S.FOV; },
		[](UOneBrahamGameUserSettings& S, float V) { S.FOV = FMath::Clamp(V, 70.f, 110.f); },
		[](float V) { return FText::Format(NSLOCTEXT("OneBraham", "Settings_Degrees", "{0}°"), FText::AsNumber(FMath::RoundToInt(V))); });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionPost", "POST-PROCESSING"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_MotionBlur", "Motion blur"),
		[](const UOneBrahamGameUserSettings& S) { return S.bMotionBlur; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bMotionBlur = B; });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_FilmGrain", "Film grain"),
		[](const UOneBrahamGameUserSettings& S) { return S.bFilmGrain; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bFilmGrain = B; });

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

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_TabAudio", "AUDIO"));

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
		[](UOneBrahamGameUserSettings& S, bool B) { S.bAnnouncementsEnabled = B; });
}

void UOneBrahamSettingsWidget::BuildVoicePage()
{
	using namespace OneBrahamSettingsLayout;

	VoiceTabIndex = TabButtons.Num();
	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_TabVoice", "VOICE"));

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
		});

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
		[](float V) { return Decimal(V, 3); });

	AddNote(Page, NSLOCTEXT("OneBraham", "Settings_NoiseGateNote",
		"Voice activation only. Raise the threshold if the air conditioning keeps opening your microphone."));

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionMic", "MICROPHONE"));

	Text_MicPermission = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), true);
	Text_MicPermission->SetAutoWrapText(true);
	Page->AddChildToVerticalBox(Text_MicPermission)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));

	Text_MicDevice = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Page->AddChildToVerticalBox(Text_MicDevice)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// The input meter: the microphone's level on a -60..0 dB scale, a short peak
	// hold, and a tick where the voice-activation gate opens - so tuning the
	// threshold above is a matter of watching the bar cross the tick.
	{
		UOverlay* Meter = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		UImage* Track = MakeSwatch(FLinearColor(Ink().R, Ink().G, Ink().B, 0.10f), FVector2D(MicMeterWidth, 8.f));
		Meter->AddChildToOverlay(MakeCell(Track, MicMeterWidth, 8.f));
		MicMeterFill = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		MicMeterFill->SetHeightOverride(8.f);
		MicMeterFill->SetWidthOverride(0.f);
		MicMeterBar = MakeSwatch(Accent(), FVector2D(MicMeterWidth, 8.f));
		MicMeterFill->AddChild(MicMeterBar);
		if (UOverlaySlot* FillSlot = Meter->AddChildToOverlay(MicMeterFill))
		{
			FillSlot->SetHorizontalAlignment(HAlign_Left);
		}
		UHorizontalBox* TickRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		MicGateOffset = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		MicGateOffset->SetSize(FVector2D(0.f, 14.f));
		TickRow->AddChildToHorizontalBox(MicGateOffset);
		TickRow->AddChildToHorizontalBox(MakeCell(MakeSwatch(Ink(), FVector2D(2.f, 14.f)), 2.f, 14.f));
		if (UOverlaySlot* TickSlot = Meter->AddChildToOverlay(TickRow))
		{
			TickSlot->SetVerticalAlignment(VAlign_Center);
		}
		Page->AddChildToVerticalBox(Meter)->SetPadding(FMargin(0.f, 4.f, 0.f, 10.f));
	}

	// The activity lamp: whether voice is leaving this machine right now.
	UHorizontalBox* Activity = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	MicLamp = MakeSwatch(FLinearColor(Ink().R, Ink().G, Ink().B, 0.12f), FVector2D(12.f, 12.f));
	Activity->AddChildToHorizontalBox(MakeCell(MicLamp, 12.f, 12.f))->SetVerticalAlignment(VAlign_Center);
	Text_MicActivity = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Activity->AddChildToHorizontalBox(Text_MicActivity)->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
	Page->AddChildToVerticalBox(Activity);

	AddNote(Page, NSLOCTEXT("OneBraham", "Settings_MicLampNote",
		"The lamp is lit while your voice is being sent. Speak normally to test the gate; hold push to talk to test the key."));
}

void UOneBrahamSettingsWidget::BuildGameplayPage()
{
	using namespace OneBrahamSettingsLayout;

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_TabGameplay", "GAMEPLAY"));

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

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_ReduceMotion", "Reduce motion"),
		[](const UOneBrahamGameUserSettings& S) { return S.bReduceMotion; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bReduceMotion = B; });

	AddSection(Page, NSLOCTEXT("OneBraham", "Settings_SectionContent", "CONTENT"));

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_Subtitles", "Subtitles"),
		[](const UOneBrahamGameUserSettings& S) { return S.bSubtitlesEnabled; },
		[](UOneBrahamGameUserSettings& S, bool B) { S.bSubtitlesEnabled = B; });

	AddToggle(Page, NSLOCTEXT("OneBraham", "Settings_Gore", "Blood"),
		[](const UOneBrahamGameUserSettings& S) { return S.IsGoreEnabled(); },
		[](UOneBrahamGameUserSettings& S, bool B) { S.SetGoreEnabled(B); });
}

void UOneBrahamSettingsWidget::BuildControlsPage()
{
	using namespace OneBrahamSettingsLayout;

	UVerticalBox* Page = AddPage(NSLOCTEXT("OneBraham", "Settings_TabControls", "CONTROLS"));

	AddNote(Page, NSLOCTEXT("OneBraham", "Settings_ControlsNote",
		"Key bindings are listed for reference. Rebinding is not available in this build."));

	UHorizontalBox* Headings = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Headings->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Settings_ColAction", "ACTION"),
		EOneBrahamTextSize::Micro, InkMuted(), true), LabelWidth));
	Headings->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Settings_ColKeyboard", "KEYBOARD AND MOUSE"),
		EOneBrahamTextSize::Micro, InkMuted(), true), ControlWidth));
	Headings->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Settings_ColPad", "CONTROLLER"),
		EOneBrahamTextSize::Micro, InkMuted(), true), 260.f));
	Page->AddChildToVerticalBox(Headings)->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));
	Page->AddChildToVerticalBox(MakeRule(FLinearColor(Ink().R, Ink().G, Ink().B, 0.1f)));

	const TArray<FOneBrahamBindingLine> Lines = GatherBindings();

	if (Lines.IsEmpty())
	{
		AddNote(Page, NSLOCTEXT("OneBraham", "Settings_NoBindings",
			"No input mapping contexts are loaded. Run the input content build."));
		return;
	}

	for (const FOneBrahamBindingLine& Line : Lines)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Row->AddChildToHorizontalBox(MakeCell(MakeLabel(Line.Action, EOneBrahamTextSize::Small, Ink()), LabelWidth, 30.f))
			->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(MakeCell(MakeLabel(Line.KeyboardMouse, EOneBrahamTextSize::Small, Ink(), true), ControlWidth, 30.f))
			->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(MakeCell(MakeLabel(Line.Controller, EOneBrahamTextSize::Small, InkMuted()), 260.f, 30.f))
			->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Row);
	}
}

TArray<FOneBrahamBindingLine> UOneBrahamSettingsWidget::GatherBindings()
{
	TArray<FOneBrahamBindingLine> Lines;

	const UOneBrahamGameSettings& GameSettings = UOneBrahamGameSettings::Get();
	const UInputMappingContext* Contexts[] =
	{
		GameSettings.DefaultMappingContext.LoadSynchronous(),
		GameSettings.CommonMappingContext.LoadSynchronous(),
	};

	// Action -> (keyboard keys, pad keys), in the order the contexts list them,
	// so the page reads in the order the input designer laid the actions out.
	TArray<const UInputAction*> Order;
	TMap<const UInputAction*, TPair<TArray<FString>, TArray<FString>>> Keys;

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

			TPair<TArray<FString>, TArray<FString>>& Entry = Keys.FindOrAdd(Mapping.Action);
			TArray<FString>& Column = Mapping.Key.IsGamepadKey() ? Entry.Value : Entry.Key;
			Column.AddUnique(Mapping.Key.GetDisplayName().ToString().ToUpper());
		}
	}

	for (const UInputAction* Action : Order)
	{
		const TPair<TArray<FString>, TArray<FString>>& Entry = Keys[Action];

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

		const FText Dash = NSLOCTEXT("OneBraham", "Settings_Unbound", "-");
		Line.KeyboardMouse = Entry.Key.IsEmpty() ? Dash : FText::FromString(FString::Join(Entry.Key, TEXT("  /  ")));
		Line.Controller = Entry.Value.IsEmpty() ? Dash : FText::FromString(FString::Join(Entry.Value, TEXT("  /  ")));
		Lines.Add(MoveTemp(Line));
	}

	return Lines;
}

// ==========================================================================
//  Behaviour
// ==========================================================================
void UOneBrahamSettingsWidget::SyncFromSettings()
{
	for (const TFunction<void()>& Sync : Syncers)
	{
		Sync();
	}

	bDirty = false;

	if (Text_Status != nullptr)
	{
		Text_Status->SetText(NSLOCTEXT("OneBraham", "Settings_StatusIdle",
			"Changes are applied and saved when you leave this page."));
	}

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

	ActiveTab = Index;
	Pages->SetActiveWidgetIndex(Index);

	for (int32 Tab = 0; Tab < TabUnderlines.Num(); ++Tab)
	{
		TabUnderlines[Tab]->SetColorAndOpacity(Tab == Index ? Accent() : FLinearColor(0.f, 0.f, 0.f, 0.f));
	}

	if (Index == VoiceTabIndex)
	{
		NextVoicePollSeconds = 0.0;
		RefreshVoiceStatus();
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

	if (Text_Status != nullptr)
	{
		Text_Status->SetText(NSLOCTEXT("OneBraham", "Settings_StatusDirty",
			"Unapplied changes. They will be applied when you leave, or press APPLY."));
	}
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

	if (Text_Status != nullptr)
	{
		Text_Status->SetText(NSLOCTEXT("OneBraham", "Settings_StatusSaved", "Applied and saved."));
	}

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
			NextVoicePollSeconds = Now + 0.1;
			RefreshVoiceStatus();
		}
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
}

void UOneBrahamSettingsWidget::RefreshVoiceStatus() const
{
	if (Text_MicPermission == nullptr || Text_MicDevice == nullptr || Text_MicActivity == nullptr || MicLamp == nullptr)
	{
		return;
	}

	const AOneBrahamPlayerState* State = GetOwningPlayerState<AOneBrahamPlayerState>();
	const UOneBrahamProximityVoiceComponent* Voice = State != nullptr ? State->GetVoiceComponent() : nullptr;

	// --- permission / availability -------------------------------------------
	// The OS answer is the specific one, so it is checked first: "unavailable"
	// tells a player nothing, "macOS has refused" tells them where to go.
	FText Permission;
	switch (OneBrahamMicrophonePermission::GetStatus())
	{
	case EOneBrahamMicrophonePermission::Denied:
		Permission = NSLOCTEXT("OneBraham", "Settings_MicDenied",
			"Microphone access has been refused. Allow On Break in System Settings > Privacy & Security > Microphone, then restart the game.");
		break;
	case EOneBrahamMicrophonePermission::Restricted:
		Permission = NSLOCTEXT("OneBraham", "Settings_MicRestricted",
			"Microphone access is blocked by a device policy. Your administrator will have opinions.");
		break;
	case EOneBrahamMicrophonePermission::MissingUsageDescription:
		Permission = NSLOCTEXT("OneBraham", "Settings_MicNoUsage",
			"This build cannot ask for microphone access. Voice chat is listen-only.");
		break;
	case EOneBrahamMicrophonePermission::NotDetermined:
		Permission = NSLOCTEXT("OneBraham", "Settings_MicNotAsked",
			"Microphone access has not been requested yet. You will be asked the first time you transmit.");
		break;
	default:
		if (Voice != nullptr && Voice->IsMicrophonePermissionDenied())
		{
			Permission = NSLOCTEXT("OneBraham", "Settings_MicDeniedComponent",
				"Microphone access has been refused by the system.");
		}
		else if (Voice != nullptr && !Voice->HasWorkingMicrophone())
		{
			Permission = NSLOCTEXT("OneBraham", "Settings_MicUnavailable",
				"No working microphone. Check that one is connected and selected as the system input.");
		}
		else
		{
			Permission = NSLOCTEXT("OneBraham", "Settings_MicReady", "Microphone ready.");
		}
		break;
	}

	Text_MicPermission->SetText(Permission);

	const FString Device = Voice != nullptr ? Voice->GetCaptureDeviceName() : FString();
	Text_MicDevice->SetText(FText::Format(NSLOCTEXT("OneBraham", "Settings_MicDevice", "Input device: {0}"),
		Device.IsEmpty() ? NSLOCTEXT("OneBraham", "Settings_MicDeviceDefault", "system default") : FText::FromString(Device)));

	// --- level -----------------------------------------------------------------
	if (MicMeterFill != nullptr && MicGateOffset != nullptr && MicMeterBar != nullptr)
	{
		const float Level = Voice != nullptr ? Voice->GetInputMeter() : 0.f;
		const float Gate = Voice != nullptr ? Voice->GetGateMeter() : 0.f;
		MicMeterFill->SetWidthOverride(MicMeterWidth * Level);
		MicGateOffset->SetSize(FVector2D(FMath::Max(0.f, MicMeterWidth * Gate - 1.f), 14.f));
		// Below the gate the bar is muted; across it, it takes the accent.
		MicMeterBar->SetColorAndOpacity(Level >= Gate ? Accent() : FLinearColor(Ink().R, Ink().G, Ink().B, 0.45f));
	}

	// --- activity --------------------------------------------------------------
	const bool bTransmitting = Voice != nullptr && Voice->IsLocalTransmitting();
	MicLamp->SetColorAndOpacity(bTransmitting ? Accent() : FLinearColor(Ink().R, Ink().G, Ink().B, 0.12f));
	Text_MicActivity->SetText(bTransmitting
		? NSLOCTEXT("OneBraham", "Settings_MicTransmitting", "TRANSMITTING")
		: NSLOCTEXT("OneBraham", "Settings_MicQuiet", "NOT TRANSMITTING"));
	Text_MicActivity->SetColorAndOpacity(FSlateColor(bTransmitting ? Ink() : InkMuted()));
}
