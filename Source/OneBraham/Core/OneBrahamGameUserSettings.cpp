// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameUserSettings.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Scalability.h"
#include "HAL/PlatformMemory.h"
#include "RHI.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"

UOneBrahamGameUserSettings::UOneBrahamGameUserSettings()
{
	// Push to talk has no key setting here. It is IA_PushToTalk, bound in
	// [OneBraham.Input] like every other action: a second key setting was a
	// second source of truth that nothing read (V, which is melee).
}

UOneBrahamGameUserSettings* UOneBrahamGameUserSettings::GetOneBrahamSettings()
{
	UOneBrahamGameUserSettings* Settings = Cast<UOneBrahamGameUserSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	if (Settings == nullptr && GEngine != nullptr)
	{
		// The engine fell back to UGameUserSettings (e.g. Config/DefaultEngine.ini
		// was edited and the class override lost). Recreate ours and keep going:
		// shipping builds must never hard-fail on a settings class mismatch.
		Settings = NewObject<UOneBrahamGameUserSettings>(GetTransientPackage(), UOneBrahamGameUserSettings::StaticClass());
		Settings->AddToRoot();
		GEngine->SetGameUserSettings(Settings);
		OB_WARN("Engine GameUserSettings was not UOneBrahamGameUserSettings - replaced at runtime.");
	}
	return Settings;
}

void UOneBrahamGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	GraphicsQualityPreset = EOneBrahamQualityPreset::Epic;
	UpscalerMode = EOneBrahamUpscaler::TSR;
	ResolutionScale = 100.f;
	bMotionBlur = false;
	MotionBlurAmount = 0.25f;
	bFilmGrain = true;
	FilmGrainIntensity = 0.02f;
	bChromaticAberration = true;
	ChromaticAberrationIntensity = 0.05f;
	bBloom = true;
	BloomIntensity = 1.f;
	bDepthOfField = true;
	DepthOfFieldMenusOnly = true;
	bLensFlare = true;
	bNanite = true;
	bVirtualShadowMaps = true;
	bHardwareLumen = false;
	bRayTracedReflections = false;

	MasterVolume = 1.f;
	MusicVolume = 0.65f;
	SFXVolume = 1.f;
	WeaponVolume = 1.f;
	FootstepVolume = 1.f;
	DialogueVolume = 1.f;
	AnnouncementVolume = 0.8f;
	UIVolume = 0.8f;
	VoiceChatVolume = 1.f;

	VoiceChatMode = EOneBrahamVoiceChatMode::PushToTalk;
	VoiceInputGain = 1.f;
	VoiceOutputGain = 1.f;
	VoiceActivationThreshold = 0.009f;
	bVoiceNoiseSuppression = true;
	bVoiceMuffledThroughWalls = true;
	VoiceSubmixPreset = EOneBrahamVoiceSubmixPreset::Raw;

	MouseSensitivityX = 0.07f;
	MouseSensitivityY = 0.07f;
	MouseSensitivityADSMultiplier = 0.65f;
	FOV = 90.f;
	HUDScale = 1.f;
	bReduceMotion = false;
	bScreenShakeReduction = false;
	bInvertMouseY = false;
	bInvertGamepadY = false;

	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::ApplyHardwareDefaultPreset(bool bForce)
{
	if (bHardwarePresetApplied && !bForce)
	{
		return;
	}

	// --- Detect the GPU class ---------------------------------------------
	const FString GPUBrand = FPlatformMisc::GetPrimaryGPUBrand();
	DetectedGpuName = GPUBrand;

	int64 VramBytes = 0;
	if (GDynamicRHI != nullptr)
	{
		FTextureMemoryStats MemStats;
		RHIGetTextureMemoryStats(MemStats);
		VramBytes = MemStats.DedicatedVideoMemory > 0 ? static_cast<int64>(MemStats.DedicatedVideoMemory) : static_cast<int64>(MemStats.TotalGraphicsMemory);
		if (VramBytes == 0 && MemStats.DedicatedVideoMemory == 0)
		{
			// Integrated / driver-reporting gap: fall back to the 25% of system RAM
			// heuristic that the Windows RHI assumes for shared memory GPUs.
			const FPlatformMemoryStats PlatformStats = FPlatformMemory::GetStats();
			VramBytes = static_cast<int64>(PlatformStats.TotalPhysical) / 4;
		}
	}
	DetectedVramMb = static_cast<int32>(VramBytes / (1024 * 1024));

	const bool bLooksRayCapable =
		GPUBrand.Contains(TEXT("RTX")) ||
		GPUBrand.Contains(TEXT("Radeon RX 6")) ||
		GPUBrand.Contains(TEXT("Radeon RX 7")) ||
		GPUBrand.Contains(TEXT("Arc A7"));

	const bool bIsIntegrated =
		GPUBrand.Contains(TEXT("Intel")) ||
		GPUBrand.Contains(TEXT("Vega 8")) ||
		GPUBrand.Contains(TEXT("UHD Graphics")) ||
		GPUBrand.Contains(TEXT("Iris"));

	EOneBrahamQualityPreset Preset = EOneBrahamQualityPreset::Epic;
	if (bIsIntegrated)
	{
		Preset = EOneBrahamQualityPreset::Low;
	}
	else if (DetectedVramMb > 0 && DetectedVramMb < 4096)
	{
		Preset = EOneBrahamQualityPreset::Medium;
	}
	else if (DetectedVramMb < 7000 && !bLooksRayCapable)
	{
		Preset = EOneBrahamQualityPreset::High;
	}
	else if (bLooksRayCapable && DetectedVramMb >= 10000)
	{
		Preset = EOneBrahamQualityPreset::Ultra;
	}
	else
	{
		Preset = EOneBrahamQualityPreset::Epic;
	}

	OB_LOG("[Settings] Detected GPU '%s' (%d MB VRAM). First-run preset: %s", *GPUBrand, DetectedVramMb,
		*UEnum::GetValueAsString(Preset));

	SetQualityPreset(Preset, /*bApplyImmediately*/ false);
	ApplySettings(true);

	bHardwarePresetApplied = true;
	SaveSettings();
}

void UOneBrahamGameUserSettings::SetQualityPreset(EOneBrahamQualityPreset NewPreset, bool bApplyImmediately)
{
	GraphicsQualityPreset = NewPreset;
	ApplyQualityPresetCVars(NewPreset);

	if (bApplyImmediately)
	{
		ApplySettings(true);
		OnSettingsChanged.Broadcast();
	}
	else
	{
		MarkSettingsDirty();
	}
}

void UOneBrahamGameUserSettings::ApplyQualityPresetCVars(EOneBrahamQualityPreset Preset)
{
	const int32 Level = static_cast<int32>(Preset);

	// Field-by-field so the code stays correct if Epic adds a quality group.
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	Levels.SetViewDistanceQuality(FMath::Clamp(Level, 0, 3));
	Levels.SetAntiAliasingQuality(2);					// AA is driven by the upscaler selection instead
	Levels.SetShadowQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetPostProcessQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetTextureQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetEffectsQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetFoliageQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetShadingQuality(FMath::Clamp(Level, 0, 3));
	Scalability::SetQualityLevels(Levels);

	// Explicit toggles that the numeric scalability groups cannot express.
	static IConsoleVariable* NaniteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite"));
	static IConsoleVariable* VsmCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
	static IConsoleVariable* LumenCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
	static IConsoleVariable* HwLumenCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
	static IConsoleVariable* MblurCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"));

	if (NaniteCVar) { NaniteCVar->Set(Preset == EOneBrahamQualityPreset::Low ? 0 : (bNanite ? 1 : 0), ECVF_SetByGameSetting); }
	if (VsmCVar) { VsmCVar->Set((Preset == EOneBrahamQualityPreset::Low || Preset == EOneBrahamQualityPreset::Medium) ? 0 : (bVirtualShadowMaps ? 1 : 0), ECVF_SetByGameSetting); }
	if (LumenCVar) { LumenCVar->Set(Preset == EOneBrahamQualityPreset::Low ? 0 : 1, ECVF_SetByGameSetting); }
	if (HwLumenCVar) { HwLumenCVar->Set(Preset == EOneBrahamQualityPreset::Ultra ? 1 : 0, ECVF_SetByGameSetting); }
	if (MblurCVar) { MblurCVar->Set(bMotionBlur ? 4 : 0, ECVF_SetByGameSetting); }

	ApplyUpscalerCVars();
	ApplyPostProcessCVars();
}

void UOneBrahamGameUserSettings::ApplyUpscalerCVars()
{
	static IConsoleVariable* ScreenPctCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	static IConsoleVariable* AAMethodCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
	static IConsoleVariable* TSRAACVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TSR.Enable"));
	static IConsoleVariable* FSRCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR3.Enabled"));
	static IConsoleVariable* DLSSCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable"));

	switch (UpscalerMode)
	{
	case EOneBrahamUpscaler::TSR:
		if (AAMethodCVar) { AAMethodCVar->Set(4, ECVF_SetByGameSetting); }	// TSR
		if (TSRAACVar) { TSRAACVar->Set(1, ECVF_SetByGameSetting); }
		if (FSRCVar) { FSRCVar->Set(0, ECVF_SetByGameSetting); }
		if (DLSSCVar) { DLSSCVar->Set(0, ECVF_SetByGameSetting); }
		if (ScreenPctCVar) { ScreenPctCVar->Set(ResolutionScale, ECVF_SetByGameSetting); }
		break;

	case EOneBrahamUpscaler::FSR3:
		if (FSRCVar) { FSRCVar->Set(1, ECVF_SetByGameSetting); }
		if (DLSSCVar) { DLSSCVar->Set(0, ECVF_SetByGameSetting); }
		if (AAMethodCVar) { AAMethodCVar->Set(2, ECVF_SetByGameSetting); }	// TAA fallback path
		if (ScreenPctCVar) { ScreenPctCVar->Set(ResolutionScale, ECVF_SetByGameSetting); }
		break;

	case EOneBrahamUpscaler::DLSS:
		if (DLSSCVar) { DLSSCVar->Set(1, ECVF_SetByGameSetting); }
		if (FSRCVar) { FSRCVar->Set(0, ECVF_SetByGameSetting); }
		if (AAMethodCVar) { AAMethodCVar->Set(4, ECVF_SetByGameSetting); }
		if (ScreenPctCVar) { ScreenPctCVar->Set(ResolutionScale, ECVF_SetByGameSetting); }
		break;

	case EOneBrahamUpscaler::Native:
	default:
		if (TSRAACVar) { TSRAACVar->Set(0, ECVF_SetByGameSetting); }
		if (FSRCVar) { FSRCVar->Set(0, ECVF_SetByGameSetting); }
		if (DLSSCVar) { DLSSCVar->Set(0, ECVF_SetByGameSetting); }
		if (AAMethodCVar) { AAMethodCVar->Set(1, ECVF_SetByGameSetting); }	// FXAA
		if (ScreenPctCVar) { ScreenPctCVar->Set(100.f, ECVF_SetByGameSetting); }
		break;
	}
}

void UOneBrahamGameUserSettings::ApplyPostProcessCVars()
{
	auto SetBoolCVar = [](const TCHAR* Name, bool bValue)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(bValue ? 1 : 0, ECVF_SetByGameSetting);
		}
	};
	auto SetFloatCVar = [](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByGameSetting);
		}
	};

	SetFloatCVar(TEXT("r.TonemapperFilmGrainIntensity"), bFilmGrain ? FMath::Clamp(FilmGrainIntensity, 0.f, 1.f) : 0.f);
	SetFloatCVar(TEXT("r.Tonemapper.Quality"), 5.f);
	SetFloatCVar(TEXT("r.SceneColorFringeQuality"), bChromaticAberration ? FMath::Clamp(ChromaticAberrationIntensity, 0.f, 1.f) : 0.f);
	SetFloatCVar(TEXT("r.BloomQuality"), bBloom ? 5.f : 0.f);
	SetFloatCVar(TEXT("r.MotionBlurQuality"), bMotionBlur ? 4.f : 0.f);
	SetFloatCVar(TEXT("r.DepthOfFieldQuality"), bDepthOfField ? 3.f : 0.f);
	SetFloatCVar(TEXT("r.DefaultFeature.AutoExposure.Bias"), 0.f);
	SetFloatCVar(TEXT("r.LensFlareQuality"), bLensFlare ? 3.f : 0.f);
	SetFloatCVar(TEXT("r.CameraShakeScale"), bCameraShake ? FMath::Clamp(CameraShakeIntensity, 0.f, 2.f) : 0.f);
	SetBoolCVar(TEXT("r.Tonemapper.GrainQuantization"), true);
}

void UOneBrahamGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	// Make sure the CVars match the stored values even if the preset was not
	// re-applied (e.g. user only changed sensitivity).
	ApplyQualityPresetCVars(GraphicsQualityPreset);
	ApplyAudioSettings();
	ApplyVoiceSettings();
	OnSettingsChanged.Broadcast();

	Super::ApplySettings(bCheckForCommandLineOverrides);
}

void UOneBrahamGameUserSettings::SetUpscaler(EOneBrahamUpscaler NewUpscaler, bool bApplyImmediately)
{
	// Guard against selecting DLSS on a non-NVIDIA GPU; the menu greys the
	// option out, but a console command could still sneak through.
	if (NewUpscaler == EOneBrahamUpscaler::DLSS && !DetectedGpuName.Contains(TEXT("RTX")) && !DetectedGpuName.Contains(TEXT("NVIDIA")))
	{
		OB_WARN("DLSS requested but the detected GPU is '%s'. Falling back to TSR.", *DetectedGpuName);
		NewUpscaler = EOneBrahamUpscaler::TSR;
	}

	UpscalerMode = NewUpscaler;
	ApplyUpscalerCVars();
	if (bApplyImmediately)
	{
		ApplySettings(false);
		OnSettingsChanged.Broadcast();
	}
	else
	{
		MarkSettingsDirty();
	}
}

void UOneBrahamGameUserSettings::SetResolutionScalePercent(float Percent, bool bApplyImmediately)
{
	ResolutionScale = FMath::Clamp(Percent, 50.f, 200.f);
	ApplyUpscalerCVars();
	if (bApplyImmediately)
	{
		OnSettingsChanged.Broadcast();
	}
	else
	{
		MarkSettingsDirty();
	}
}

void UOneBrahamGameUserSettings::SetFrameRateLimit(int32 MenuLimit, int32 GameplayLimit)
{
	FPSLimitMenu = FMath::Clamp(MenuLimit, 30, 360);
	FPSLimitGameplay = FMath::Clamp(GameplayLimit, 0, 360);

	// Qualified on purpose. This class declares its own two-argument
	// SetFrameRateLimit, which hides the engine's one-argument
	// UGameUserSettings::SetFrameRateLimit(float) - a member of the same name in
	// a derived class hides every base overload of that name. Unqualified, this
	// line resolves against the two-argument version, finds no viable candidate,
	// and stops the build. The engine setter is the one that actually caps the
	// frame rate; the two fields above are the menu's remembered preferences.
	UGameUserSettings::SetFrameRateLimit(static_cast<float>(FPSLimitGameplay));
	ApplySettings(false);
}

void UOneBrahamGameUserSettings::SetAdvancedGraphicOption(FName OptionName, int32 Value, bool bApplyImmediately)
{
	const FName Name = OptionName;
	auto SetCVarInt = [](const TCHAR* CVarName, int32 InValue)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName))
		{
			CVar->Set(InValue, ECVF_SetByGameSetting);
		}
	};

	if (Name == TEXT("Nanite")) { bNanite = Value != 0; SetCVarInt(TEXT("r.Nanite"), Value); }
	else if (Name == TEXT("VirtualShadowMaps")) { bVirtualShadowMaps = Value != 0; SetCVarInt(TEXT("r.Shadow.Virtual.Enable"), Value); }
	else if (Name == TEXT("HardwareLumen")) { bHardwareLumen = Value != 0; SetCVarInt(TEXT("r.Lumen.HardwareRayTracing"), Value); }
	else if (Name == TEXT("RayTracedReflections")) { bRayTracedReflections = Value != 0; SetCVarInt(TEXT("r.Lumen.Reflections.HardwareRayTracing"), Value); }
	else if (Name == TEXT("RayTracingShadows")) { bRayTracingShadows = Value != 0; SetCVarInt(TEXT("r.RayTracing.Shadows"), Value); }
	else if (Name == TEXT("RayTracingAO")) { bRayTracingAmbientOcclusion = Value != 0; SetCVarInt(TEXT("r.RayTracing.AmbientOcclusion"), Value); }
	else if (Name == TEXT("MotionBlur")) { bMotionBlur = Value != 0; ApplyPostProcessCVars(); }
	else if (Name == TEXT("Bloom")) { bBloom = Value != 0; ApplyPostProcessCVars(); }
	else if (Name == TEXT("FilmGrain")) { bFilmGrain = Value != 0; ApplyPostProcessCVars(); }
	else if (Name == TEXT("ChromaticAberration")) { bChromaticAberration = Value != 0; ApplyPostProcessCVars(); }
	else if (Name == TEXT("DepthOfField")) { bDepthOfField = Value != 0; ApplyPostProcessCVars(); }
	else if (Name == TEXT("LensFlare")) { bLensFlare = Value != 0; ApplyPostProcessCVars(); }
	else
	{
		OB_WARN("SetAdvancedGraphicOption called with unknown option '%s'.", *Name.ToString());
		return;
	}

	if (bApplyImmediately)
	{
		OnSettingsChanged.Broadcast();
	}
	else
	{
		MarkSettingsDirty();
	}
}

// ==========================================================================
//  Audio
// ==========================================================================
void UOneBrahamGameUserSettings::SetVolume(FName BusName, float Value, bool bApplyImmediately)
{
	const float Clamped = FMath::Clamp(Value, 0.f, 1.f);

	if (BusName == TEXT("Master"))			{ MasterVolume = Clamped; }
	else if (BusName == TEXT("Music"))		{ MusicVolume = Clamped; }
	else if (BusName == TEXT("Ambience"))	{ AmbienceVolume = Clamped; }
	else if (BusName == TEXT("SFX"))		{ SFXVolume = Clamped; }
	else if (BusName == TEXT("Weapon"))		{ WeaponVolume = Clamped; }
	else if (BusName == TEXT("Footstep"))	{ FootstepVolume = Clamped; }
	else if (BusName == TEXT("Dialogue"))	{ DialogueVolume = Clamped; }
	else if (BusName == TEXT("Announcement")) { AnnouncementVolume = Clamped; }
	else if (BusName == TEXT("UI"))			{ UIVolume = Clamped; }
	else if (BusName == TEXT("Voice"))		{ VoiceChatVolume = Clamped; }
	else
	{
		OB_WARN("SetVolume called with unknown bus '%s'.", *BusName.ToString());
		return;
	}

	ApplyAudioSettings();

	if (bApplyImmediately)
	{
		OnSettingsChanged.Broadcast();
	}
	else
	{
		MarkSettingsDirty();
	}
}

float UOneBrahamGameUserSettings::GetVolume(FName BusName) const
{
	if (BusName == TEXT("Master"))			return MasterVolume;
	if (BusName == TEXT("Music"))			return MusicVolume;
	if (BusName == TEXT("Ambience"))		return AmbienceVolume;
	if (BusName == TEXT("SFX"))				return SFXVolume;
	if (BusName == TEXT("Weapon"))			return WeaponVolume;
	if (BusName == TEXT("Footstep"))		return FootstepVolume;
	if (BusName == TEXT("Dialogue"))		return DialogueVolume;
	if (BusName == TEXT("Announcement"))	return AnnouncementVolume;
	if (BusName == TEXT("UI"))				return UIVolume;
	if (BusName == TEXT("Voice"))			return VoiceChatVolume;
	return 1.f;
}

void UOneBrahamGameUserSettings::ApplyAudioSettings()
{
	// The sound mix SM_OneBraham is loaded from the project's audio settings so
	// that the mixer can be re-authored without a code change.
	UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
	if (World == nullptr)
	{
		// No world yet (main menu before a map loads): values are read directly
		// from this settings object by the UI mixer widget, so nothing is lost.
		return;
	}

	// The class and mix names below are the ones Tools/Python/onebraham/build_audio.py
	// authors, and therefore the ones that actually exist in a built project. They
	// were once short ("SC_Master") and every slider in the game was silently
	// doing nothing because the load returned null and nothing said so. That is
	// the failure mode this project's documentation calls out by name, so the
	// resolution path below reports loudly in development and still degrades
	// gracefully in a shipping build.
	static const TCHAR* MixPath = TEXT("/Game/OneBraham/Audio/Mix/SM_OneBraham.SM_OneBraham");

	USoundMix* SoundMix = LoadObject<USoundMix>(nullptr, MixPath);
	if (SoundMix == nullptr)
	{
		UE_LOG(LogOneBrahamAudio, Verbose,
			TEXT("Base sound mix not found at %s: volume sliders are stored but not applied. ")
			TEXT("Run Scripts/BuildContent.bat --only audio."), MixPath);
		return;
	}

	struct FBusMapping
	{
		const TCHAR* ClassPath;
		float Value;
	};

	const FBusMapping Buses[] =
	{
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Master.SC_OneBraham_Master"),           MasterVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Music.SC_OneBraham_Music"),             MusicVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Ambience.SC_OneBraham_Ambience"),       AmbienceVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_SFX.SC_OneBraham_SFX"),                 SFXVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Weapon.SC_OneBraham_Weapon"),           WeaponVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Footstep.SC_OneBraham_Footstep"),       FootstepVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Dialogue.SC_OneBraham_Dialogue"),       DialogueVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Announcement.SC_OneBraham_Announcement"), AnnouncementVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_UI.SC_OneBraham_UI"),                   UIVolume },
		{ TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Voice.SC_OneBraham_Voice"),             VoiceChatVolume }
	};

	int32 Applied = 0;

	for (const FBusMapping& Bus : Buses)
	{
		if (USoundClass* SoundClass = LoadObject<USoundClass>(nullptr, Bus.ClassPath))
		{
			// 0 volume would mute the bus entirely and stop streaming; 0.0001 keeps
			// the graph alive so re-raising the slider is instant.
			UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, SoundClass, FMath::Max(Bus.Value, 0.0001f), 1.f, 0.10f, true);
			++Applied;
		}
		else
		{
			UE_LOG(LogOneBrahamAudio, Warning,
				TEXT("Sound class missing: %s - the corresponding volume slider does nothing."),
				Bus.ClassPath);
		}
	}

	UGameplayStatics::PushSoundMixModifier(World, SoundMix);

	UE_LOG(LogOneBrahamAudio, Verbose, TEXT("Audio settings applied: %d of %d buses."),
		Applied, UE_ARRAY_COUNT(Buses));
}

// ==========================================================================
//  Voice
// ==========================================================================
void UOneBrahamGameUserSettings::SetVoiceChatMode(EOneBrahamVoiceChatMode NewMode)
{
	VoiceChatMode = NewMode;
	bVoiceChatEnabled = (NewMode != EOneBrahamVoiceChatMode::Muted);
	bUsePushToTalk = (NewMode == EOneBrahamVoiceChatMode::PushToTalk);
	bVoiceActivationEnabled = (NewMode == EOneBrahamVoiceChatMode::VoiceActivation);
	ApplyVoiceSettings();
}

void UOneBrahamGameUserSettings::SetVoiceActivationThreshold(float Threshold)
{
	VoiceActivationThreshold = FMath::Clamp(Threshold, 0.f, 0.2f);
	ApplyVoiceSettings();
}

void UOneBrahamGameUserSettings::SetVoiceInputGain(float Gain)
{
	VoiceInputGain = FMath::Clamp(Gain, 0.f, 4.f);
	ApplyVoiceSettings();
}

void UOneBrahamGameUserSettings::SetVoiceOutputGain(float Gain)
{
	VoiceOutputGain = FMath::Clamp(Gain, 0.f, 4.f);
	ApplyVoiceSettings();
}

void UOneBrahamGameUserSettings::SetVoiceInputDevice(const FString& DeviceId)
{
	VoiceInputDeviceId = DeviceId;
	ApplyVoiceSettings();
}

void UOneBrahamGameUserSettings::ApplyVoiceSettings()
{
	// The voice subsystem picks these up through the settings delegate; keeping
	// the push inside the subsystem avoids a circular dependency here.
	OnVoiceSettingsChanged.Broadcast(bVoiceChatEnabled && VoiceChatMode != EOneBrahamVoiceChatMode::Muted);

	// Mirror into CVars so QA can inspect the effective values from the console.
	if (IConsoleVariable* GainCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Voice.InputGain")))
	{
		GainCVar->Set(VoiceInputGain, ECVF_SetByGameSetting);
	}
	if (IConsoleVariable* OutCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Voice.OutputGain")))
	{
		OutCVar->Set(VoiceOutputGain, ECVF_SetByGameSetting);
	}
	if (IConsoleVariable* GateCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Voice.NoiseGate")))
	{
		GateCVar->Set(VoiceActivationThreshold, ECVF_SetByGameSetting);
	}
}

// ==========================================================================
//  Controls / accessibility
// ==========================================================================
void UOneBrahamGameUserSettings::SetMouseSensitivity(float SensitivityX, float SensitivityY, float ADSMultiplier)
{
	MouseSensitivityX = FMath::Clamp(SensitivityX, 0.005f, 1.f);
	MouseSensitivityY = FMath::Clamp(SensitivityY, 0.005f, 1.f);
	MouseSensitivityADSMultiplier = FMath::Clamp(ADSMultiplier, 0.1f, 1.f);
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::SetGamepadSensitivity(float SensitivityX, float SensitivityY)
{
	GamepadSensitivityX = FMath::Clamp(SensitivityX, 0.005f, 1.f);
	GamepadSensitivityY = FMath::Clamp(SensitivityY, 0.005f, 1.f);
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::SetFOV(float NewFOV)
{
	// 70..110 keeps the first-person weapon sockets inside the frustum: the
	// arms are authored for a 90 degree default and the vertical FOV clamps
	// prevent the classic "gun floating in space" artefact at 110.
	FOV = FMath::Clamp(NewFOV, 70.f, 110.f);
	if (APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(GEngine->GetCurrentPlayWorld()) : nullptr)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>())
			{
				Camera->SetFieldOfView(FOV);
			}
		}
	}
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::SetColourBlindMode(EOneBrahamColourBlindMode NewMode)
{
	ColourBlindMode = NewMode;

	// CVD is implemented as a post-process material parameter so it works for
	// all three deficiency types without shipping three LUTs.
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/Game/OneBraham/Materials/MPC_OneBraham_Global.MPC_OneBraham_Global"));

	UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
	if (Collection != nullptr && World != nullptr)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("ColourBlindMode"), static_cast<float>(static_cast<int32>(NewMode)));
	}

	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::SetHUDSettings(float NewHUDScale, bool bNewShowKillFeed, bool bNewShowVoiceIndicators)
{
	HUDScale = FMath::Clamp(NewHUDScale, 0.75f, 1.5f);
	bShowKillFeed = bNewShowKillFeed;
	bShowVoiceIndicators = bNewShowVoiceIndicators;
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::SetReducedMotion(bool bReduceMotionEnabled, bool bReduceCameraShake)
{
	bReduceMotion = bReduceMotionEnabled;
	bScreenShakeReduction = bReduceCameraShake;
	bCameraShake = !bReduceCameraShake;
	ApplyPostProcessCVars();
	SaveSettings();
	OnSettingsChanged.Broadcast();
}
