// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameUserSettings.h
// ---------------------------------------------------------------------------
// UOneBrahamGameUserSettings extends UGameUserSettings with everything the
// ONE BRAHAM settings menu needs: graphics tiers, audio bus levels, voice chat
// preferences, accessibility options, and hardware-preset detection.
//
// Registered in Config/DefaultEngine.ini:
//   GameUserSettingsClassName=/Script/OneBraham.OneBrahamGameUserSettings

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamGameUserSettings.generated.h"

/** Quality tiers exposed by the settings menu. Order matches DefaultScalability.ini. */
UENUM(BlueprintType)
enum class EOneBrahamQualityPreset : uint8
{
	Low = 0		UMETA(DisplayName = "Low"),
	Medium = 1	UMETA(DisplayName = "Medium"),
	High = 2	UMETA(DisplayName = "High"),
	Epic = 3	UMETA(DisplayName = "Epic"),
	Ultra = 4	UMETA(DisplayName = "Ultra (Ray Traced)")
};

/** Colour-blind modes implemented by the HUD/material parameter collection. */
UENUM(BlueprintType)
enum class EOneBrahamColourBlindMode : uint8
{
	Off = 0		UMETA(DisplayName = "Off"),
	Protanopia = 1 UMETA(DisplayName = "Protanopia"),
	Deuteranopia = 2 UMETA(DisplayName = "Deuteranopia"),
	Tritanopia = 3 UMETA(DisplayName = "Tritanopia")
};

/** Upscaler selection. 0 = engine default (TSR). */
UENUM(BlueprintType)
enum class EOneBrahamUpscaler : uint8
{
	TSR = 0			UMETA(DisplayName = "TSR (Temporal Super Resolution)"),
	FSR3 = 1		UMETA(DisplayName = "AMD FSR 3"),
	DLSS = 2		UMETA(DisplayName = "NVIDIA DLSS (RTX only)"),
	Native = 3		UMETA(DisplayName = "Native (no upscaling)")
};

/** How chatty the building is. */
UENUM(BlueprintType)
enum class EOneBrahamVoiceSubmixPreset : uint8
{
	/** Hardware-direct, no radio filtering: the person is next to you. */
	Raw = 0			UMETA(DisplayName = "Clear"),
	/** Slight compression + high-pass, like a radio in the next room. */
	Radio = 1		UMETA(DisplayName = "Radio"),
	/** Heavy band-pass + noise floor, like a radio in a lift shaft. */
	Comms = 2		UMETA(DisplayName = "Comms (muffled)")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOneBrahamSettingsApplied);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamVoiceSettingsChanged, bool, bEnabled);

/**
 * Player settings. Every setter marks the settings dirty and raises
 * OnSettingsChanged so live UI (volume sliders, sensitivity) applies instantly
 * without a level reload.
 */
UCLASS(config = GameUserSettings, configdonotcheckdefaults, BlueprintType)
class ONEBRAHAM_API UOneBrahamGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UOneBrahamGameUserSettings();

	/** Typed accessor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Settings")
	static UOneBrahamGameUserSettings* GetOneBrahamSettings();

	// --- Lifecycle --------------------------------------------------------
	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	virtual void SetToDefaults() override;

	/** Picks a sane first-run preset from GPU class + VRAM. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Settings")
	void ApplyHardwareDefaultPreset(bool bForce = false);

	// --- Graphics ---------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "One Braham|Graphics")
	void SetQualityPreset(EOneBrahamQualityPreset NewPreset, bool bApplyImmediately = true);

	UFUNCTION(BlueprintPure, Category = "One Braham|Graphics")
	EOneBrahamQualityPreset GetQualityPreset() const { return GraphicsQualityPreset; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Graphics")
	void SetUpscaler(EOneBrahamUpscaler NewUpscaler, bool bApplyImmediately = true);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Graphics")
	void SetResolutionScalePercent(float Percent, bool bApplyImmediately = true);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Graphics")
	void SetFrameRateLimit(int32 MenuLimit, int32 GameplayLimit);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Graphics")
	void SetAdvancedGraphicOption(FName OptionName, int32 Value, bool bApplyImmediately = true);

	// --- Audio ------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	void SetVolume(FName BusName, float Value, bool bApplyImmediately = true);

	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	float GetVolume(FName BusName) const;

	/** Pushes every volume to the SM_OneBraham sound mix. Safe to call every frame. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	void ApplyAudioSettings();

	// --- Voice ------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceChatMode(EOneBrahamVoiceChatMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceActivationThreshold(float Threshold);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceInputGain(float Gain);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceOutputGain(float Gain);

	/** Device id from the audio device enumeration; empty = system default. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceInputDevice(const FString& DeviceId);

	// --- Controls / accessibility -----------------------------------------
	UFUNCTION(BlueprintCallable, Category = "One Braham|Controls")
	void SetMouseSensitivity(float SensitivityX, float SensitivityY, float ADSMultiplier);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Controls")
	void SetGamepadSensitivity(float SensitivityX, float SensitivityY);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Controls")
	void SetFOV(float NewFOV);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Accessibility")
	void SetColourBlindMode(EOneBrahamColourBlindMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Accessibility")
	void SetHUDSettings(float NewHUDScale, bool bNewShowKillFeed, bool bNewShowVoiceIndicators);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Accessibility")
	void SetReducedMotion(bool bReduceMotionEnabled, bool bReduceCameraShake);

	// --- Delegates --------------------------------------------------------
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Settings")
	FOneBrahamSettingsApplied OnSettingsChanged;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Settings")
	FOneBrahamVoiceSettingsChanged OnVoiceSettingsChanged;

	// ---------------------------------------------------------------------
	//  Stored values (config = GameUserSettings -> written per user)
	// ---------------------------------------------------------------------
	UPROPERTY(config) EOneBrahamQualityPreset GraphicsQualityPreset = EOneBrahamQualityPreset::Epic;
	UPROPERTY(config) EOneBrahamUpscaler UpscalerMode = EOneBrahamUpscaler::TSR;
	UPROPERTY(config) float ResolutionScale = 100.f;
	UPROPERTY(config) int32 AntiAliasingMethod = 4;
	UPROPERTY(config) bool bMotionBlur = false;
	UPROPERTY(config) float MotionBlurAmount = 0.25f;
	UPROPERTY(config) bool bFilmGrain = true;
	UPROPERTY(config) float FilmGrainIntensity = 0.02f;
	UPROPERTY(config) bool bChromaticAberration = true;
	UPROPERTY(config) float ChromaticAberrationIntensity = 0.05f;
	UPROPERTY(config) bool bBloom = true;
	UPROPERTY(config) float BloomIntensity = 1.f;
	UPROPERTY(config) bool bDepthOfField = true;
	UPROPERTY(config) bool DepthOfFieldMenusOnly = true;
	UPROPERTY(config) bool bLensFlare = true;
	UPROPERTY(config) bool bRayTracedReflections = false;
	UPROPERTY(config) bool bHardwareLumen = false;
	UPROPERTY(config) int32 LumenQuality = 1;
	UPROPERTY(config) bool bNanite = true;
	UPROPERTY(config) bool bVirtualShadowMaps = true;
	UPROPERTY(config) bool bRayTracingShadows = false;
	UPROPERTY(config) bool bRayTracingAmbientOcclusion = false;
	UPROPERTY(config) int32 FPSLimitMenu = 120;
	UPROPERTY(config) int32 FPSLimitGameplay = 0;
	UPROPERTY(config) bool bBorderlessOnAltEnter = true;
	UPROPERTY(config) float DisplayGamma = 2.2f;
	UPROPERTY(config) EOneBrahamColourBlindMode ColourBlindMode = EOneBrahamColourBlindMode::Off;
	UPROPERTY(config) bool bShowFPS = false;
	UPROPERTY(config) bool bShowNetStats = false;

	// Audio
	UPROPERTY(config) float MasterVolume = 1.f;
	UPROPERTY(config) float MusicVolume = 0.65f;
	/** Building beds: HVAC, racks, wind. Audible information, so it sits high. */
	UPROPERTY(config) float AmbienceVolume = 0.9f;
	UPROPERTY(config) float SFXVolume = 1.f;
	UPROPERTY(config) float WeaponVolume = 1.f;
	UPROPERTY(config) float FootstepVolume = 1.f;
	UPROPERTY(config) float DialogueVolume = 1.f;
	UPROPERTY(config) float AnnouncementVolume = 0.8f;
	UPROPERTY(config) float UIVolume = 0.8f;
	UPROPERTY(config) float VoiceChatVolume = 1.f;
	UPROPERTY(config) bool bAnnouncementsEnabled = true;
	UPROPERTY(config) bool bSubtitlesEnabled = true;
	UPROPERTY(config) float SubtitleScale = 1.f;
	UPROPERTY(config) bool bDirectionalHitIndicators = true;

	// Voice
	UPROPERTY(config) bool bVoiceChatEnabled = true;
	UPROPERTY(config) EOneBrahamVoiceChatMode VoiceChatMode = EOneBrahamVoiceChatMode::PushToTalk;
	UPROPERTY(config) float VoiceInputGain = 1.f;
	UPROPERTY(config) float VoiceOutputGain = 1.f;
	UPROPERTY(config) bool bVoiceActivationEnabled = false;
	UPROPERTY(config) float VoiceActivationThreshold = 0.009f;
	UPROPERTY(config) bool bVoiceNoiseSuppression = true;
	UPROPERTY(config) bool bVoiceMuffledThroughWalls = true;
	UPROPERTY(config) EOneBrahamVoiceSubmixPreset VoiceSubmixPreset = EOneBrahamVoiceSubmixPreset::Raw;
	UPROPERTY(config) FString AudioOutputDeviceId;

	/** Preferred microphone. Empty = the system default communications device. */
	UPROPERTY(config) FString AudioInputDeviceId;
	UPROPERTY(config) FString VoiceInputDeviceId;
	UPROPERTY(config) bool bUsePushToTalk = true;
	UPROPERTY(config) FKey PushToTalkKey;
	UPROPERTY(config) FKey PushToTalkGamepadKey;
	UPROPERTY(config) bool bVoiceChannelCycleEnabled = true;

	// Gameplay / accessibility
	UPROPERTY(config) bool bHoldToSprint = false;
	UPROPERTY(config) bool bToggleCrouch = true;
	UPROPERTY(config) bool bToggleLean = false;
	UPROPERTY(config) bool bToggleADS = true;
	UPROPERTY(config) bool bAutoReloadOnEmpty = true;
	UPROPERTY(config) bool bSprintRequiresForwardInput = true;
	UPROPERTY(config) bool bInvertMouseY = false;
	UPROPERTY(config) bool bInvertGamepadY = false;
	UPROPERTY(config) float MouseSensitivityX = 0.07f;
	UPROPERTY(config) float MouseSensitivityY = 0.07f;
	UPROPERTY(config) float MouseSensitivityADSMultiplier = 0.65f;
	UPROPERTY(config) float GamepadSensitivityX = 0.07f;
	UPROPERTY(config) float GamepadSensitivityY = 0.07f;
	UPROPERTY(config) bool bAimAssistEnabled = true;
	UPROPERTY(config) float AimAssistStrength = 0.6f;
	UPROPERTY(config) float FOV = 90.f;
	UPROPERTY(config) float ADSFieldOfViewTransitionSpeed = 1.f;
	UPROPERTY(config) float ControllerVibration = 1.f;
	UPROPERTY(config) bool bViewBobbing = true;
	UPROPERTY(config) float ViewBobbingAmount = 1.f;
	UPROPERTY(config) bool bCameraShake = true;
	UPROPERTY(config) float CameraShakeIntensity = 1.f;
	UPROPERTY(config) bool bShowDamageNumbers = false;
	UPROPERTY(config) float HUDScale = 1.f;
	UPROPERTY(config) bool bColorBlindFriendlyMarkers = false;
	UPROPERTY(config) int32 EnemyOutlineMode = 0;
	UPROPERTY(config) bool bShowKillFeed = true;
	UPROPERTY(config) bool bShowVoiceIndicators = true;

	// --- HUD elements (Settings -> Interface) ------------------------------
	// Every element of the combat HUD can be switched off independently: this is
	// the accessibility baseline for a shooter, and streamers use it constantly.
	UPROPERTY(config) bool bShowCrosshair = true;
	UPROPERTY(config) bool bShowCompass = true;
	UPROPERTY(config) bool bShowTeamPanel = true;
	UPROPERTY(config) bool bShowAmmoCounter = true;
	UPROPERTY(config) bool bShowDamageDirection = true;
	UPROPERTY(config) bool bShowHitMarkers = true;
	UPROPERTY(config) bool bShowObjectiveMarkers = true;
	UPROPERTY(config) bool bShowFloorIndicator = true;
	UPROPERTY(config) bool bShowInteractionPrompts = true;

	/** Overall HUD opacity, 0.25 - 1.0. */
	UPROPERTY(config) float HUDOpacity = 1.f;

	/** Combat text (damage numbers). Off by default: it is not that kind of game. */
	UPROPERTY(config) bool bShowDamageNumbersEnabled = true;
	UPROPERTY(config) bool bShowProximityVoiceDebug = false;
	UPROPERTY(config) bool bAutoSkipIntro = false;
	UPROPERTY(config) bool bReduceMotion = false;
	UPROPERTY(config) bool bScreenShakeReduction = false;

	// Privacy / misc
	UPROPERTY(config) bool bShareAnonymousStats = true;
	UPROPERTY(config) bool bAllowRichPresence = true;
	UPROPERTY(config) bool bAllowSteamOverlay = true;
	UPROPERTY(config) FString PreferredRegion = TEXT("EU-West");
	UPROPERTY(config) FString LastUsedServerAddress;
	UPROPERTY(config) int32 LastUsedServerPort = 7777;

	/** Broadcasts voice changes and mirrors gain/gate into ob.Voice.* CVars. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void ApplyVoiceSettings();

private:
	/** Applies the CVars for a preset. Called by SetQualityPreset and ApplySettings. */
	void ApplyQualityPresetCVars(EOneBrahamQualityPreset Preset);

	/** Applies motion blur / bloom / grain / chroma / DoF CVars. */
	void ApplyPostProcessCVars();

	/** Applies r.ScreenPercentage + upscaler CVars. */
	void ApplyUpscalerCVars();

	/** Cached GPU name captured at first run (used by the settings menu footer). */
	UPROPERTY(config) FString DetectedGpuName;

	UPROPERTY(config) int32 DetectedVramMb = 0;

	UPROPERTY(config) bool bHardwarePresetApplied = false;
};
