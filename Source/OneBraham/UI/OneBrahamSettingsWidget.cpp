// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamSettingsWidget.h"

#include "Components/HorizontalBox.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamInputBindings.h"
#include "Online/OneBrahamPlatformSubsystem.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	UOneBrahamGameUserSettings* Settings()
	{
		return UOneBrahamGameUserSettings::GetOneBrahamSettings();
	}

	FText Percent(float Value)
	{
		return FText::Format(NSLOCTEXT("OneBraham", "Settings_Percent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value * 100.f)));
	}

	/** A plain field changed: save it and tell the HUD and the controllers.
	 *  ApplySettings would also re-apply the resolution, which flickers. */
	void Commit(UOneBrahamGameUserSettings* S)
	{
		S->SaveSettings();
		S->OnSettingsChanged.Broadcast();
	}

	FText Whole(float Value)
	{
		return FText::AsNumber(FMath::RoundToInt(Value));
	}

	/** The enum's display names, in value order: the cycle buttons' options. */
	template <typename TEnum>
	TArray<FText> EnumOptions()
	{
		TArray<FText> Options;
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
			{
				Options.Add(Enum->GetDisplayNameTextByIndex(Index));
			}
		}
		return Options;
	}
}

FText UOneBrahamSettingsWidget::GetPageTitle() const
{
	return NSLOCTEXT("OneBraham", "Settings_Title", "Settings");
}

FText UOneBrahamSettingsWidget::GetFooterText() const
{
	return bCapturing
		? NSLOCTEXT("OneBraham", "Settings_FooterCapture", "PRESS A KEY  -  ESC CANCELS")
		: NSLOCTEXT("OneBraham", "Settings_Footer", "ESC / B  SAVE AND CLOSE  -  CHANGES APPLY AS YOU MAKE THEM");
}

void UOneBrahamSettingsWidget::GoBack()
{
	if (bCapturing)
	{
		FinishCapture(EKeys::Escape);
		return;
	}

	if (UOneBrahamGameUserSettings* Current = Settings())
	{
		Current->ApplySettings(false);	// applies and saves
	}
	CloseMenu();
}

void UOneBrahamSettingsWidget::BuildPage()
{
	BuildTabs();
	switch (Page)
	{
	case EOneBrahamSettingsPage::Audio: BuildAudio(); break;
	case EOneBrahamSettingsPage::Controls: BuildControls(); break;
	case EOneBrahamSettingsPage::Gameplay: BuildGameplay(); break;
	case EOneBrahamSettingsPage::Accessibility: BuildAccessibility(); break;
	default: BuildVideo(); break;
	}
}

void UOneBrahamSettingsWidget::BuildTabs()
{
	UHorizontalBox* Tabs = AddRow();
	struct FTab { EOneBrahamSettingsPage Target; FText Label; };
	const FTab Entries[] =
	{
		{ EOneBrahamSettingsPage::Video, NSLOCTEXT("OneBraham", "Settings_Video", "Video") },
		{ EOneBrahamSettingsPage::Audio, NSLOCTEXT("OneBraham", "Settings_Audio", "Audio") },
		{ EOneBrahamSettingsPage::Controls, NSLOCTEXT("OneBraham", "Settings_Controls", "Controls") },
		{ EOneBrahamSettingsPage::Gameplay, NSLOCTEXT("OneBraham", "Settings_Gameplay", "Gameplay") },
		{ EOneBrahamSettingsPage::Accessibility, NSLOCTEXT("OneBraham", "Settings_Access", "Access") },
	};
	for (const FTab& Entry : Entries)
	{
		const EOneBrahamSettingsPage Target = Entry.Target;
		AddButton(Entry.Label, [this, Target]() { Page = Target; ShowPage(); }, Tabs, Page != Target);
	}
}

// ==========================================================================
//  Video
// ==========================================================================
void UOneBrahamSettingsWidget::BuildVideo()
{
	UOneBrahamGameUserSettings* S = Settings();
	if (S == nullptr)
	{
		return;
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Display", "Display"));

	// Resolution: the monitor's own list, so nothing offered is unsupported.
	TArray<FIntPoint> Resolutions;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	if (Resolutions.Num() > 0)
	{
		TArray<FText> Labels;
		int32 CurrentIndex = Resolutions.Num() - 1;
		for (int32 Index = 0; Index < Resolutions.Num(); ++Index)
		{
			Labels.Add(FText::FromString(FString::Printf(TEXT("%d x %d"), Resolutions[Index].X, Resolutions[Index].Y)));
			if (Resolutions[Index] == S->GetScreenResolution())
			{
				CurrentIndex = Index;
			}
		}
		AddCycle(NSLOCTEXT("OneBraham", "Settings_Resolution", "Resolution"), Labels, CurrentIndex, [S, Resolutions](int32 Index)
		{
			S->SetScreenResolution(Resolutions[Index]);
			S->ApplyResolutionSettings(false);
		});
	}

	const TArray<FText> Modes = {
		NSLOCTEXT("OneBraham", "Settings_Fullscreen", "Fullscreen"),
		NSLOCTEXT("OneBraham", "Settings_Borderless", "Borderless"),
		NSLOCTEXT("OneBraham", "Settings_Windowed", "Windowed") };
	AddCycle(NSLOCTEXT("OneBraham", "Settings_DisplayMode", "Display mode"), Modes,
		static_cast<int32>(S->GetFullscreenMode()), [S](int32 Index)
		{
			S->SetFullscreenMode(static_cast<EWindowMode::Type>(Index));
			S->ApplyResolutionSettings(false);
		});

	AddToggle(NSLOCTEXT("OneBraham", "Settings_VSync", "V-Sync"), S->IsVSyncEnabled(), [S](bool bOn)
	{
		S->SetVSyncEnabled(bOn);
		S->ApplyNonResolutionSettings();
	});

	const TArray<int32> Caps = { 0, 60, 120, 144, 165, 240 };
	TArray<FText> CapLabels;
	int32 CapIndex = 0;
	for (int32 Index = 0; Index < Caps.Num(); ++Index)
	{
		CapLabels.Add(Caps[Index] == 0 ? NSLOCTEXT("OneBraham", "Settings_Uncapped", "Uncapped") : FText::AsNumber(Caps[Index]));
		if (Caps[Index] == S->FPSLimitGameplay)
		{
			CapIndex = Index;
		}
	}
	AddCycle(NSLOCTEXT("OneBraham", "Settings_FrameCap", "Frame rate limit"), CapLabels, CapIndex, [S, Caps](int32 Index)
	{
		S->SetFrameRateLimit(S->FPSLimitMenu, Caps[Index]);
	});

	AddSlider(NSLOCTEXT("OneBraham", "Settings_FOV", "Field of view"), 70.f, 110.f, 1.f, S->FOV,
		[S](float Value) { S->SetFOV(Value); }, Whole);

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Quality", "Quality"));
	AddCycle(NSLOCTEXT("OneBraham", "Settings_Preset", "Quality preset"), EnumOptions<EOneBrahamQualityPreset>(),
		static_cast<int32>(S->GetQualityPreset()), [S](int32 Index)
		{
			S->SetQualityPreset(static_cast<EOneBrahamQualityPreset>(Index));
		});
	AddCycle(NSLOCTEXT("OneBraham", "Settings_Upscaler", "Upscaler"), EnumOptions<EOneBrahamUpscaler>(),
		static_cast<int32>(S->UpscalerMode), [S](int32 Index)
		{
			S->SetUpscaler(static_cast<EOneBrahamUpscaler>(Index));
		});
	AddSlider(NSLOCTEXT("OneBraham", "Settings_ResScale", "Resolution scale"), 50.f, 100.f, 1.f, S->ResolutionScale,
		[S](float Value) { S->SetResolutionScalePercent(Value); },
		[](float Value) { return FText::Format(NSLOCTEXT("OneBraham", "Settings_PercentWhole", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value))); });

	AddHeading(NSLOCTEXT("OneBraham", "Settings_PostProcess", "Image"));
	struct FNamedOption { FName Name; FText Label; };
	const FNamedOption Effects[] =
	{
		{ TEXT("MotionBlur"), NSLOCTEXT("OneBraham", "Settings_MotionBlur", "Motion blur") },
		{ TEXT("DepthOfField"), NSLOCTEXT("OneBraham", "Settings_DOF", "Depth of field") },
		{ TEXT("FilmGrain"), NSLOCTEXT("OneBraham", "Settings_Grain", "Film grain") },
		{ TEXT("ChromaticAberration"), NSLOCTEXT("OneBraham", "Settings_Chroma", "Chromatic aberration") },
		{ TEXT("Bloom"), NSLOCTEXT("OneBraham", "Settings_Bloom", "Bloom") },
		{ TEXT("LensFlare"), NSLOCTEXT("OneBraham", "Settings_Flare", "Lens flare") },
	};
	const bool States[] = { S->bMotionBlur, S->bDepthOfField, S->bFilmGrain, S->bChromaticAberration, S->bBloom, S->bLensFlare };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Effects); ++Index)
	{
		const FName Option = Effects[Index].Name;
		AddToggle(Effects[Index].Label, States[Index], [S, Option](bool bOn)
		{
			S->SetAdvancedGraphicOption(Option, bOn ? 1 : 0);
		});
	}
}

// ==========================================================================
//  Audio
// ==========================================================================
void UOneBrahamSettingsWidget::BuildAudio()
{
	UOneBrahamGameUserSettings* S = Settings();
	if (S == nullptr)
	{
		return;
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Volume", "Volume"));
	struct FBus { FName Name; FText Label; };
	const FBus Buses[] =
	{
		{ TEXT("Master"), NSLOCTEXT("OneBraham", "Bus_Master", "Master") },
		{ TEXT("Music"), NSLOCTEXT("OneBraham", "Bus_Music", "Music") },
		{ TEXT("Ambience"), NSLOCTEXT("OneBraham", "Bus_Ambience", "The building") },
		{ TEXT("SFX"), NSLOCTEXT("OneBraham", "Bus_SFX", "Effects") },
		{ TEXT("Weapon"), NSLOCTEXT("OneBraham", "Bus_Weapon", "Weapons") },
		{ TEXT("Footstep"), NSLOCTEXT("OneBraham", "Bus_Footstep", "Footsteps") },
		{ TEXT("Dialogue"), NSLOCTEXT("OneBraham", "Bus_Dialogue", "Voice lines") },
		{ TEXT("Announcement"), NSLOCTEXT("OneBraham", "Bus_Announcement", "Tannoy") },
		{ TEXT("UI"), NSLOCTEXT("OneBraham", "Bus_UI", "Interface") },
		{ TEXT("Voice"), NSLOCTEXT("OneBraham", "Bus_Voice", "Voice chat") },
	};
	for (const FBus& Bus : Buses)
	{
		const FName BusName = Bus.Name;
		AddSlider(Bus.Label, 0.f, 1.f, 0.05f, S->GetVolume(BusName), [S, BusName](float Value) { S->SetVolume(BusName, Value); }, Percent);
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_VoiceChat", "Voice chat"));
	AddCycle(NSLOCTEXT("OneBraham", "Settings_VoiceMode", "Microphone"), EnumOptions<EOneBrahamVoiceChatMode>(),
		static_cast<int32>(S->VoiceChatMode), [S](int32 Index)
		{
			S->SetVoiceChatMode(static_cast<EOneBrahamVoiceChatMode>(Index));
		});
	AddSlider(NSLOCTEXT("OneBraham", "Settings_MicGain", "Microphone gain"), 0.f, 2.f, 0.05f, S->VoiceInputGain,
		[S](float Value) { S->SetVoiceInputGain(Value); }, Percent);
	AddToggle(NSLOCTEXT("OneBraham", "Settings_Muffled", "Voices muffled through walls"), S->bVoiceMuffledThroughWalls, [S](bool bOn)
	{
		S->bVoiceMuffledThroughWalls = bOn;
		S->ApplyVoiceSettings();
	});
}

// ==========================================================================
//  Controls
// ==========================================================================
void UOneBrahamSettingsWidget::BuildControls()
{
	UOneBrahamGameUserSettings* S = Settings();
	if (S == nullptr)
	{
		return;
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Aim", "Aim"));
	AddSlider(NSLOCTEXT("OneBraham", "Settings_MouseSens", "Mouse sensitivity"), 0.01f, 0.3f, 0.005f, S->MouseSensitivityX,
		[S](float Value) { S->SetMouseSensitivity(Value, Value, S->MouseSensitivityADSMultiplier); });
	AddSlider(NSLOCTEXT("OneBraham", "Settings_ADSMult", "Aiming sensitivity"), 0.2f, 1.5f, 0.05f, S->MouseSensitivityADSMultiplier,
		[S](float Value) { S->SetMouseSensitivity(S->MouseSensitivityX, S->MouseSensitivityY, Value); }, Percent);
	AddSlider(NSLOCTEXT("OneBraham", "Settings_PadSens", "Stick sensitivity"), 0.01f, 0.3f, 0.005f, S->GamepadSensitivityX,
		[S](float Value) { S->SetGamepadSensitivity(Value, Value); });
	AddToggle(NSLOCTEXT("OneBraham", "Settings_InvertMouse", "Invert mouse Y"), S->bInvertMouseY, [S](bool bOn) { S->bInvertMouseY = bOn; Commit(S); });
	AddToggle(NSLOCTEXT("OneBraham", "Settings_InvertPad", "Invert stick Y"), S->bInvertGamepadY, [S](bool bOn) { S->bInvertGamepadY = bOn; Commit(S); });
	AddToggle(NSLOCTEXT("OneBraham", "Settings_AimAssist", "Aim assist (pad)"), S->bAimAssistEnabled, [S](bool bOn) { S->bAimAssistEnabled = bOn; Commit(S); });
	AddSlider(NSLOCTEXT("OneBraham", "Settings_AimAssistStrength", "Aim assist strength"), 0.f, 1.f, 0.05f, S->AimAssistStrength,
		[S](float Value) { S->AimAssistStrength = Value; Commit(S); }, Percent);

	AddHeading(NSLOCTEXT("OneBraham", "Settings_HoldToggle", "Hold or toggle"));
	AddToggle(NSLOCTEXT("OneBraham", "Settings_HoldSprint", "Hold to sprint"), S->bHoldToSprint, [S](bool bOn) { S->bHoldToSprint = bOn; Commit(S); });
	AddToggle(NSLOCTEXT("OneBraham", "Settings_ToggleCrouch", "Toggle crouch"), S->bToggleCrouch, [S](bool bOn) { S->bToggleCrouch = bOn; Commit(S); });
	AddToggle(NSLOCTEXT("OneBraham", "Settings_ToggleADS", "Toggle aim"), S->bToggleADS, [S](bool bOn) { S->bToggleADS = bOn; Commit(S); });
	AddToggle(NSLOCTEXT("OneBraham", "Settings_ToggleLean", "Toggle lean"), S->bToggleLean, [S](bool bOn) { S->bToggleLean = bOn; Commit(S); });

	AddHeading(bGamepadBindings
		? NSLOCTEXT("OneBraham", "Settings_PadKeys", "Controller buttons")
		: NSLOCTEXT("OneBraham", "Settings_Keys", "Keyboard and mouse"));

	UHorizontalBox* DeviceRow = AddRow();
	AddButton(bGamepadBindings ? NSLOCTEXT("OneBraham", "Settings_ShowKeys", "Show keyboard")
		: NSLOCTEXT("OneBraham", "Settings_ShowPad", "Show controller"),
		[this]() { bGamepadBindings = !bGamepadBindings; RebindStatus = FText::GetEmpty(); ShowPage(); }, DeviceRow);
	AddButton(NSLOCTEXT("OneBraham", "Settings_ResetKeys", "Reset to defaults"), [this]()
	{
		UOneBrahamInputBindings::ResetBindings(bGamepadBindings);
		RebindStatus = NSLOCTEXT("OneBraham", "Settings_KeysReset", "Defaults restored.");
		ShowPage();
	}, DeviceRow);

	if (!RebindStatus.IsEmpty())
	{
		AddText(RebindStatus, EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
	}

	const TArray<FOneBrahamKeyBinding> Bindings = UOneBrahamInputBindings::GetBindings(bGamepadBindings);
	for (const FName& Action : UOneBrahamInputBindings::GetRebindableActions())
	{
		UHorizontalBox* Keys = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		int32 KeyCount = 0;
		for (const FOneBrahamKeyBinding& Binding : Bindings)
		{
			if (Binding.Action != Action)
			{
				continue;
			}
			const FKey Key = Binding.Key;
			const bool bListening = bCapturing && CaptureAction == Action && CaptureOldKey == Key;
			AddButton(bListening ? NSLOCTEXT("OneBraham", "Settings_Listening", "...") : Key.GetDisplayName(false),
				[this, Action, Key]() { BeginCapture(Action, Key); }, Keys);
			++KeyCount;
		}
		if (KeyCount == 0)
		{
			const bool bListening = bCapturing && CaptureAction == Action && !CaptureOldKey.IsValid();
			AddButton(bListening ? NSLOCTEXT("OneBraham", "Settings_Listening", "...") : NSLOCTEXT("OneBraham", "Settings_Unbound", "Unbound"),
				[this, Action]() { BeginCapture(Action, FKey()); }, Keys);
		}
		AddLabelledRow(UOneBrahamInputBindings::GetActionDescription(Action), Keys);
	}
}

void UOneBrahamSettingsWidget::BeginCapture(FName Action, const FKey& OldKey)
{
	bCapturing = true;
	CaptureAction = Action;
	CaptureOldKey = OldKey;
	RebindStatus = FText::Format(NSLOCTEXT("OneBraham", "Settings_PressKey", "Press the new key for: {0}"),
		UOneBrahamInputBindings::GetActionDescription(Action));
	ShowPage();
	SetKeyboardFocus();	// the next key comes here, not to the button
}

void UOneBrahamSettingsWidget::FinishCapture(const FKey& Key)
{
	bCapturing = false;

	if (Key == EKeys::Escape)
	{
		RebindStatus = NSLOCTEXT("OneBraham", "Settings_RebindCancelled", "Unchanged.");
	}
	else if (!UOneBrahamInputBindings::IsKeyBindable(Key, bGamepadBindings))
	{
		RebindStatus = FText::Format(NSLOCTEXT("OneBraham", "Settings_KeyRefused", "{0} cannot be bound here."), Key.GetDisplayName(false));
	}
	else
	{
		FName Swapped;
		if (UOneBrahamInputBindings::Rebind(CaptureAction, CaptureOldKey, Key, bGamepadBindings, Swapped))
		{
			RebindStatus = Swapped.IsNone()
				? FText::Format(NSLOCTEXT("OneBraham", "Settings_Rebound", "{0} is now {1}."),
					UOneBrahamInputBindings::GetActionDescription(CaptureAction), Key.GetDisplayName(false))
				: FText::Format(NSLOCTEXT("OneBraham", "Settings_Swapped", "{0} is now {1}. {2} took {3}."),
					UOneBrahamInputBindings::GetActionDescription(CaptureAction), Key.GetDisplayName(false),
					UOneBrahamInputBindings::GetActionDescription(Swapped),
					CaptureOldKey.IsValid() ? CaptureOldKey.GetDisplayName(false) : NSLOCTEXT("OneBraham", "Settings_NoKey", "nothing"));
		}
		else
		{
			RebindStatus = NSLOCTEXT("OneBraham", "Settings_RebindSame", "Unchanged.");
		}
	}

	CaptureAction = NAME_None;
	CaptureOldKey = FKey();
	ShowPage();
}

FReply UOneBrahamSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bCapturing)
	{
		FinishCapture(InKeyEvent.GetKey());
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UOneBrahamSettingsWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bCapturing && !bGamepadBindings)
	{
		FinishCapture(InMouseEvent.GetEffectingButton());
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UOneBrahamSettingsWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bCapturing && !bGamepadBindings)
	{
		FinishCapture(InMouseEvent.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

// ==========================================================================
//  Gameplay and accessibility
// ==========================================================================
void UOneBrahamSettingsWidget::BuildGameplay()
{
	UOneBrahamGameUserSettings* S = Settings();
	if (S == nullptr)
	{
		return;
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_HUD", "HUD"));
	AddSlider(NSLOCTEXT("OneBraham", "Settings_HUDScale", "HUD scale"), 0.8f, 1.2f, 0.05f, S->HUDScale,
		[S](float Value) { S->SetHUDSettings(Value, S->bShowKillFeed, S->bShowVoiceIndicators); }, Percent);
	AddSlider(NSLOCTEXT("OneBraham", "Settings_HUDOpacity", "HUD opacity"), 0.25f, 1.f, 0.05f, S->HUDOpacity,
		[S](float Value) { S->HUDOpacity = Value; Commit(S); }, Percent);

	struct FHUDToggle { FText Label; bool UOneBrahamGameUserSettings::* Field; };
	const FHUDToggle Toggles[] =
	{
		{ NSLOCTEXT("OneBraham", "Settings_Crosshair", "Crosshair"), &UOneBrahamGameUserSettings::bShowCrosshair },
		{ NSLOCTEXT("OneBraham", "Settings_Compass", "Compass"), &UOneBrahamGameUserSettings::bShowCompass },
		{ NSLOCTEXT("OneBraham", "Settings_Squad", "Squad panel"), &UOneBrahamGameUserSettings::bShowTeamPanel },
		{ NSLOCTEXT("OneBraham", "Settings_Ammo", "Ammo counter"), &UOneBrahamGameUserSettings::bShowAmmoCounter },
		{ NSLOCTEXT("OneBraham", "Settings_HitMarkers", "Hit markers"), &UOneBrahamGameUserSettings::bShowHitMarkers },
		{ NSLOCTEXT("OneBraham", "Settings_DamageDir", "Damage direction"), &UOneBrahamGameUserSettings::bShowDamageDirection },
		{ NSLOCTEXT("OneBraham", "Settings_Objective", "Objective markers"), &UOneBrahamGameUserSettings::bShowObjectiveMarkers },
		{ NSLOCTEXT("OneBraham", "Settings_Floor", "Floor indicator"), &UOneBrahamGameUserSettings::bShowFloorIndicator },
		{ NSLOCTEXT("OneBraham", "Settings_Prompts", "Interaction prompts"), &UOneBrahamGameUserSettings::bShowInteractionPrompts },
		{ NSLOCTEXT("OneBraham", "Settings_KillFeed", "Kill feed"), &UOneBrahamGameUserSettings::bShowKillFeed },
		{ NSLOCTEXT("OneBraham", "Settings_DamageNumbers", "Damage numbers"), &UOneBrahamGameUserSettings::bShowDamageNumbers },
	};
	for (const FHUDToggle& Toggle : Toggles)
	{
		bool UOneBrahamGameUserSettings::* Field = Toggle.Field;
		AddToggle(Toggle.Label, S->*Field, [S, Field](bool bOn) { S->*Field = bOn; Commit(S); });
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Weapons", "Weapons"));
	AddToggle(NSLOCTEXT("OneBraham", "Settings_AutoReload", "Reload automatically when empty"), S->bAutoReloadOnEmpty,
		[S](bool bOn) { S->bAutoReloadOnEmpty = bOn; Commit(S); });

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Online", "Online"));
	AddToggle(NSLOCTEXT("OneBraham", "Settings_Crossplay", "Play with other platforms"), S->bAllowCrossplay,
		[S](bool bOn) { S->bAllowCrossplay = bOn; Commit(S); });
	const UOneBrahamPlatformSubsystem* Platform = UOneBrahamPlatformSubsystem::Get(this);
	if (Platform != nullptr && S->bAllowCrossplay && !Platform->IsCrossplayAllowed())
	{
		AddText(NSLOCTEXT("OneBraham", "Settings_CrossplayBlocked", "Your platform's privacy settings have crossplay switched off."),
			EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
	}
}

void UOneBrahamSettingsWidget::BuildAccessibility()
{
	UOneBrahamGameUserSettings* S = Settings();
	if (S == nullptr)
	{
		return;
	}

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Subtitles", "Subtitles"));
	AddToggle(NSLOCTEXT("OneBraham", "Settings_SubtitlesOn", "Subtitles"), S->bSubtitlesEnabled,
		[S](bool bOn) { S->bSubtitlesEnabled = bOn; Commit(S); });
	AddSlider(NSLOCTEXT("OneBraham", "Settings_SubtitleSize", "Subtitle size"), 0.75f, 1.5f, 0.05f, S->SubtitleScale,
		[S](float Value) { S->SubtitleScale = Value; Commit(S); }, Percent);

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Vision", "Vision"));
	AddCycle(NSLOCTEXT("OneBraham", "Settings_ColourBlind", "Colour-blind mode"), EnumOptions<EOneBrahamColourBlindMode>(),
		static_cast<int32>(S->ColourBlindMode), [S](int32 Index)
		{
			S->SetColourBlindMode(static_cast<EOneBrahamColourBlindMode>(Index));
		});
	AddToggle(NSLOCTEXT("OneBraham", "Settings_Markers", "Colour-blind friendly markers"), S->bColorBlindFriendlyMarkers,
		[S](bool bOn) { S->bColorBlindFriendlyMarkers = bOn; Commit(S); });

	AddHeading(NSLOCTEXT("OneBraham", "Settings_Motion", "Motion"));
	AddToggle(NSLOCTEXT("OneBraham", "Settings_ReduceMotion", "Reduce motion"), S->bReduceMotion,
		[S](bool bOn) { S->SetReducedMotion(bOn, S->bScreenShakeReduction); });
	AddSlider(NSLOCTEXT("OneBraham", "Settings_Shake", "Camera shake"), 0.f, 1.f, 0.05f, S->CameraShakeIntensity,
		[S](float Value) { S->CameraShakeIntensity = Value; S->SetReducedMotion(S->bReduceMotion, Value <= 0.f); }, Percent);
	AddToggle(NSLOCTEXT("OneBraham", "Settings_Bob", "View bobbing"), S->bViewBobbing,
		[S](bool bOn) { S->bViewBobbing = bOn; Commit(S); });
	AddSlider(NSLOCTEXT("OneBraham", "Settings_Vibration", "Controller vibration"), 0.f, 1.f, 0.05f, S->ControllerVibration,
		[S](float Value) { S->ControllerVibration = Value; Commit(S); }, Percent);
}
