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
#include "UObject/Package.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

UOneBrahamGameUserSettings::UOneBrahamGameUserSettings()
{
	// Sensible non-zero defaults so a first launch before any save exists still
	// gives the player a keyboard-bindable push-to-talk key.
	PushToTalkKey = EKeys::V;
	PushToTalkGamepadKey = EKeys::Gamepad_LeftThumbstick;
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
		GEngine->GameUserSettings = Settings;
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

bool UOneBrahamGameUserSettings::ConsumeFirstRunPreset(bool bHasSavedUserSettings)
{
	if (bHardwarePresetApplied)
	{
		return false;
	}

	// Marked either way: an install that already has settings (including every
	// install from before this ran at all) keeps what it has, and is not asked
	// again on the next launch.
	bHardwarePresetApplied = true;
	return !bHasSavedUserSettings;
}

bool UOneBrahamGameUserSettings::HasSavedUserSettings()
{
	// The per-user file, not the merged config: Config/DefaultGameUserSettings.ini
	// is always in GConfig. On a Mac it lives in the app's container
	// (~/Library/Containers/<bundle id>/Data/Library/Application Support/Epic/<project>/Saved/Config/Mac/)
	// or ~/Library/Application Support/Epic/<project>/... unsandboxed, and in
	// Saved/Config/MacEditor/ for an uncooked -game run.
	const FString UserIni = FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"), nullptr, *FPaths::GeneratedConfigDir());
	return FPaths::FileExists(UserIni);
}

bool UOneBrahamGameUserSettings::IsPlausibleGpuBenchmark(float GpuIndex)
{
	// 100 is the engine's reference GPU; the fastest desktop cards land in the low
	// thousands. Metal on Apple Silicon times every test identically and reports
	// ~350,000, which would rate a fanless laptop above any GPU that exists.
	return GpuIndex > 0.f && GpuIndex < 5000.f;
}

EOneBrahamQualityPreset UOneBrahamGameUserSettings::ChooseHardwarePreset(const FOneBrahamHardwareProbe& Probe)
{
	const FString& GPUBrand = Probe.GpuBrand;

	// --- GPU family / VRAM ------------------------------------------------
	EOneBrahamQualityPreset Preset = EOneBrahamQualityPreset::Epic;

	const bool bAppleSilicon = GPUBrand.StartsWith(TEXT("Apple M"));
	if (bAppleSilicon)
	{
		// Unified memory: the "VRAM" Metal reports is ~75% of system RAM, so it
		// says nothing about the GPU. The chip suffix does: the base chip has
		// 8-10 GPU cores, Pro 16-20, Max 32-40, Ultra twice a Max. On the base M5
		// at 1280x720 Epic costs ~73 ms and High 30-48 ms; Medium (TSR, 50%
		// internal on a base chip) is the cheapest tier that keeps Lumen GI - Low turns it off and
		// the interiors wash out.
		if (GPUBrand.Contains(TEXT(" Max")) || GPUBrand.Contains(TEXT(" Ultra")))
		{
			Preset = EOneBrahamQualityPreset::High;
		}
		else
		{
			Preset = EOneBrahamQualityPreset::Medium;
		}
	}
	else
	{
		const bool bLooksRayCapable =
			GPUBrand.Contains(TEXT("RTX")) ||
			GPUBrand.Contains(TEXT("Radeon RX 6")) ||
			GPUBrand.Contains(TEXT("Radeon RX 7")) ||
			GPUBrand.Contains(TEXT("Radeon RX 9")) ||
			GPUBrand.Contains(TEXT("Arc A7")) ||
			GPUBrand.Contains(TEXT("Arc B"));

		const bool bIsIntegrated =
			(GPUBrand.Contains(TEXT("Intel")) && !GPUBrand.Contains(TEXT("Arc"))) ||
			GPUBrand.Contains(TEXT("Vega 8")) ||
			GPUBrand.Contains(TEXT("UHD Graphics")) ||
			GPUBrand.Contains(TEXT("Iris")) ||
			GPUBrand.Contains(TEXT("Radeon(TM) Graphics")) ||
			GPUBrand.Contains(TEXT("Radeon Graphics"));

		if (bIsIntegrated)
		{
			Preset = EOneBrahamQualityPreset::Low;
		}
		else if (Probe.VramMb > 0 && Probe.VramMb < 4096)
		{
			Preset = EOneBrahamQualityPreset::Medium;
		}
		else if (Probe.VramMb < 7000 && !bLooksRayCapable)
		{
			Preset = EOneBrahamQualityPreset::High;
		}
		else if (bLooksRayCapable && Probe.VramMb >= 10000)
		{
			Preset = EOneBrahamQualityPreset::Ultra;
		}
		else
		{
			Preset = EOneBrahamQualityPreset::Epic;
		}
	}

	// --- Benchmark cap ----------------------------------------------------
	// Only lowers. The engine's own ladder (BaseScalability.ini: 18 / 42 / 115)
	// puts anything past 115 at Epic; this tower needs roughly four times a
	// typical scene, so the project's steps sit higher.
	if (IsPlausibleGpuBenchmark(Probe.GpuBenchmarkIndex))
	{
		EOneBrahamQualityPreset Cap = EOneBrahamQualityPreset::Ultra;
		if (Probe.GpuBenchmarkIndex < 60.f)
		{
			Cap = EOneBrahamQualityPreset::Low;
		}
		else if (Probe.GpuBenchmarkIndex < 150.f)
		{
			Cap = EOneBrahamQualityPreset::Medium;
		}
		else if (Probe.GpuBenchmarkIndex < 400.f)
		{
			Cap = EOneBrahamQualityPreset::High;
		}
		else if (Probe.GpuBenchmarkIndex < 900.f)
		{
			Cap = EOneBrahamQualityPreset::Epic;
		}

		if (static_cast<uint8>(Cap) < static_cast<uint8>(Preset))
		{
			Preset = Cap;
		}
	}

	return Preset;
}

bool UOneBrahamGameUserSettings::PrefersDynamicResolution(const FOneBrahamHardwareProbe& Probe)
{
	// Measured on the M5 (Metal, UE 5.8), Medium in the same room: fixed 71%
	// 26 ms GPU; dynamic resolution pinned at its 50% floor 48 ms, with
	// multi-frame hitches as it resized. Off on Apple Silicon until that changes.
	return !Probe.GpuBrand.StartsWith(TEXT("Apple"));
}

float UOneBrahamGameUserSettings::FixedResolutionScaleFor(EOneBrahamQualityPreset Preset, const FOneBrahamHardwareProbe& Probe)
{
	// A base Apple M chip (8-10 GPU cores, no Pro / Max / Ultra suffix) is
	// pixel-bound in the tower: per-pixel local lights and Lumen's screen
	// probes scale with the internal resolution. Measured on the M5, Medium in
	// the same view: 71% ~24 ms GPU at 1280x720 (~27 ms at the 1470x956 a
	// game-mode window gets on its Retina panel), 50% ~15 ms. The floor holds
	// 60 fps; TSR hides the difference in a still frame.
	const FString& Brand = Probe.GpuBrand;
	if (Brand.StartsWith(TEXT("Apple M")) &&
		!Brand.Contains(TEXT(" Pro")) && !Brand.Contains(TEXT(" Max")) && !Brand.Contains(TEXT(" Ultra")))
	{
		return 50.f;
	}

	// The engine's own steps for its single quality levels
	// (BaseScalability.ini PerfIndexValues_ResolutionQuality), TSR upscaling.
	switch (Preset)
	{
	case EOneBrahamQualityPreset::Low:		return 50.f;
	case EOneBrahamQualityPreset::Medium:	return 71.f;
	case EOneBrahamQualityPreset::High:		return 87.f;
	default:								return 100.f;
	}
}

void UOneBrahamGameUserSettings::ApplyHardwareDefaultPreset(bool bForce)
{
	if (!bForce && !ConsumeFirstRunPreset(HasSavedUserSettings()))
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

	FOneBrahamHardwareProbe Probe;
	Probe.GpuBrand = GPUBrand;
	Probe.VramMb = DetectedVramMb;

#if !PLATFORM_MAC
	// The engine's synthetic benchmark (~3 s, once). Skipped on Metal, where its
	// GPU timings are meaningless (see IsPlausibleGpuBenchmark); the result is
	// only ever used to lower the family/VRAM pick, never to raise it.
	// RunHardwareBenchmark overwrites ScalabilityQuality; SetQualityPreset below
	// replaces every group of it.
	RunHardwareBenchmark();
	Probe.GpuBenchmarkIndex = LastGPUBenchmarkResult;
#endif

	const EOneBrahamQualityPreset Preset = ChooseHardwarePreset(Probe);

	// Native output resolution with TSR. Where dynamic resolution works it keeps
	// the frame time (the floor, 50%, is what a slower GPU renders in a heavy
	// room; a fast one never leaves 100%); where it does not, a fixed internal
	// resolution does the same job.
	const bool bDynamicResolution = PrefersDynamicResolution(Probe);
	const float Scale = bDynamicResolution ? 100.f : FixedResolutionScaleFor(Preset, Probe);
	ResolutionScale = Scale;
	ScalabilityQuality.ResolutionQuality = Scale;
	SetDynamicResolutionEnabled(bDynamicResolution);

	OB_LOG("[Settings] Detected GPU '%s' (%d MB VRAM, benchmark %.0f%s). First-run preset: %s at %.0f%%, dynamic resolution %s.",
		*GPUBrand, DetectedVramMb, Probe.GpuBenchmarkIndex,
		IsPlausibleGpuBenchmark(Probe.GpuBenchmarkIndex) ? TEXT("") : TEXT(" - not used"),
		*UEnum::GetValueAsString(Preset), Scale, bDynamicResolution ? TEXT("on") : TEXT("off"));

	bHardwarePresetApplied = true;
	SetQualityPreset(Preset, /*bApplyImmediately*/ false);

	// ApplySettings saves.
	ApplySettings(true);
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
	// Global illumination and reflections are the tower's two largest costs
	// (Lumen), so they follow the preset like everything else.
	//
	// Written into ScalabilityQuality as well as the live levels: the base
	// class's ApplyNonResolutionSettings re-applies ScalabilityQuality, and
	// SaveSettings writes the live levels to [ScalabilityGroups] - a preset that
	// only touched the live levels was undone by the next ApplySettings.
	Scalability::FQualityLevels Levels = ScalabilityQuality;
	Levels.SetViewDistanceQuality(FMath::Clamp(Level, 0, 3));
	Levels.SetAntiAliasingQuality(2);					// AA is driven by the upscaler selection instead
	Levels.SetShadowQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetGlobalIlluminationQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetReflectionQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetPostProcessQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetTextureQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetEffectsQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetFoliageQuality(FMath::Clamp(Level, 0, 4));
	Levels.SetShadingQuality(FMath::Clamp(Level, 0, 3));
	ScalabilityQuality = Levels;
	Scalability::SetQualityLevels(Levels);

	// Explicit toggles that the numeric scalability groups cannot express.
	static IConsoleVariable* NaniteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite"));
	static IConsoleVariable* VsmCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
	static IConsoleVariable* LumenCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
	static IConsoleVariable* HwLumenCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));

	if (NaniteCVar) { NaniteCVar->Set(Preset == EOneBrahamQualityPreset::Low ? 0 : (bNanite ? 1 : 0), ECVF_SetByGameSetting); }
	if (VsmCVar) { VsmCVar->Set((Preset == EOneBrahamQualityPreset::Low || Preset == EOneBrahamQualityPreset::Medium) ? 0 : (bVirtualShadowMaps ? 1 : 0), ECVF_SetByGameSetting); }
	if (LumenCVar) { LumenCVar->Set(Preset == EOneBrahamQualityPreset::Low ? 0 : 1, ECVF_SetByGameSetting); }
	if (HwLumenCVar) { HwLumenCVar->Set(Preset == EOneBrahamQualityPreset::Ultra ? 1 : 0, ECVF_SetByGameSetting); }

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

	ApplyDynamicResolutionCVars();
}

void UOneBrahamGameUserSettings::ApplyDynamicResolutionCVars()
{
	auto SetCVar = [](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByGameSetting);
		}
	};

	// Mode 1 = on when the player's bUseDynamicResolution is (the engine's own
	// default is 0, which ignores the setting entirely). Native has no temporal
	// upscaler to hide a moving resolution, so it stays off there.
	SetCVar(TEXT("r.DynamicRes.OperationMode"), UpscalerMode == EOneBrahamUpscaler::Native ? 0.f : 1.f);

	// The budget is the frame cap when there is one, else 60 fps (the engine
	// default of 33.3 ms only ever kicks in below 30).
	const float TargetFps = FrameRateLimit >= 30.f ? FrameRateLimit : 60.f;
	SetCVar(TEXT("r.DynamicRes.FrameTimeBudget"), 1000.f / TargetFps);

	// TSR reconstructs well from half resolution; above 100 is supersampling,
	// which the resolution-scale slider offers deliberately, not the heuristic.
	SetCVar(TEXT("r.DynamicRes.MinScreenPercentage"), 50.f);
	SetCVar(TEXT("r.DynamicRes.MaxScreenPercentage"), 100.f);
}

void UOneBrahamGameUserSettings::ApplyPostProcessCVars()
{
	auto SetFloatCVar = [](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByGameSetting);
		}
	};

	// A player toggle for an effect whose quality belongs to the tier
	// (PostProcessQuality in DefaultScalability.ini). Off is 0 at game-setting
	// priority. On leaves the variable to the tier: this used to write a fixed
	// Epic value (r.Tonemapper.Quality=5, bloom 5, DoF 3, flares 3) at
	// game-setting priority, which outranks SetByScalability, so Low and Medium
	// paid for full-quality post they had turned down. Once a toggle has been
	// off this session the variable is stuck at game-setting priority, so
	// turning it back on writes the current tier's own value at that priority.
	// r.Tonemapper.Quality and r.Tonemapper.GrainQuantization are not player
	// toggles at all and are never touched here.
	const int32 PostLevel = FMath::Clamp(Scalability::GetQualityLevels().PostProcessQuality, 0, 4);
	auto SetTierToggle = [PostLevel](const TCHAR* Name, bool bEnabled, int32 Fallback)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		if (CVar == nullptr)
		{
			return;
		}
		if (!bEnabled)
		{
			CVar->Set(0, ECVF_SetByGameSetting);
			return;
		}
		if ((CVar->GetFlags() & ECVF_SetByMask) != ECVF_SetByGameSetting)
		{
			return;
		}

		// The engine names its top level "Cine", not 4 (see DefaultScalability.ini).
		const FString Section = PostLevel >= 4
			? FString(TEXT("PostProcessQuality@Cine"))
			: FString::Printf(TEXT("PostProcessQuality@%d"), PostLevel);
		int32 Value = Fallback;
		if (GConfig != nullptr)
		{
			GConfig->GetInt(*Section, Name, Value, GScalabilityIni);
		}
		CVar->Set(Value, ECVF_SetByGameSetting);
	};

	SetFloatCVar(TEXT("r.TonemapperFilmGrainIntensity"), bFilmGrain ? FMath::Clamp(FilmGrainIntensity, 0.f, 1.f) : 0.f);
	SetTierToggle(TEXT("r.SceneColorFringeQuality"), bChromaticAberration, 1);
	SetTierToggle(TEXT("r.BloomQuality"), bBloom, 5);
	SetTierToggle(TEXT("r.MotionBlurQuality"), bMotionBlur, 4);
	SetTierToggle(TEXT("r.DepthOfFieldQuality"), bDepthOfField, 2);
	SetTierToggle(TEXT("r.LensFlareQuality"), bLensFlare, 2);
	SetFloatCVar(TEXT("r.DefaultFeature.AutoExposure.Bias"), 0.f);
	SetFloatCVar(TEXT("r.CameraShakeScale"), bCameraShake ? FMath::Clamp(CameraShakeIntensity, 0.f, 2.f) : 0.f);
}

void UOneBrahamGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	// Make sure the CVars match the stored values even if the preset was not
	// re-applied (e.g. user only changed sensitivity).
	ApplyQualityPresetCVars(GraphicsQualityPreset);
	ApplyAudioSettings();
	ApplyVoiceSettings();

	// Blood is a player's choice; the VFX code reads ob.Gore (VFX/OneBrahamVFXStatics.cpp).
	if (IConsoleVariable* Gore = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Gore")))
	{
		Gore->Set(bGoreEnabled ? 1 : 0, ECVF_SetByGameSetting);
	}
	OnSettingsChanged.Broadcast();

	Super::ApplySettings(bCheckForCommandLineOverrides);
	bOBSettingsDirty = false;
}

bool UOneBrahamGameUserSettings::IsDirty() const
{
	return Super::IsDirty() || bOBSettingsDirty;
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

void UOneBrahamGameUserSettings::SetMenuAndGameplayFrameRateLimits(int32 MenuLimit, int32 GameplayLimit)
{
	FPSLimitMenu = FMath::Clamp(MenuLimit, 30, 360);
	FPSLimitGameplay = FMath::Clamp(GameplayLimit, 0, 360);

	// The engine setter is the one that actually caps the frame rate; the two
	// fields above are the menu's remembered preferences.
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

	// One field from here on: the legacy copy is retired the first time the player
	// picks a device, so the two can never disagree again.
	AudioInputDeviceId.Reset();
	ApplyVoiceSettings();
}

FString UOneBrahamGameUserSettings::GetVoiceInputDeviceId() const
{
	return !VoiceInputDeviceId.IsEmpty() ? VoiceInputDeviceId : AudioInputDeviceId;
}

void UOneBrahamGameUserSettings::ApplyVoiceSettings()
{
	// Mirror into the ob.Voice.* CVars first (registered in
	// Voice/OneBrahamVoiceRules.cpp): capture and playback read the gains and the
	// gate from there, so a listener reacting to the broadcast below already sees
	// the new values. QA can inspect or override them from the console.
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

	// The live voice component (on the local PlayerState) subscribes to this and
	// re-reads mode, enable flag and device; keeping the push on that side avoids
	// a dependency from settings onto gameplay code.
	OnVoiceSettingsChanged.Broadcast(bVoiceChatEnabled && VoiceChatMode != EOneBrahamVoiceChatMode::Muted);
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
	ApplyColourBlindModeToWorld(GEngine ? GEngine->GetCurrentPlayWorld() : nullptr);

	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UOneBrahamGameUserSettings::ApplyColourBlindModeToWorld(UWorld* World) const
{
	if (World == nullptr)
	{
		return;
	}

	// CVD is implemented as a post-process material parameter so it works for
	// all three deficiency types without shipping three LUTs. Collection values
	// live in the world's collection instance, so every new world starts at the
	// asset default (Off) until this runs - ApplyWorldBoundSettings does that.
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/Game/OneBraham/Materials/MPC_OneBraham_Global.MPC_OneBraham_Global"));
	if (Collection != nullptr)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("ColourBlindMode"), static_cast<float>(static_cast<int32>(ColourBlindMode)));
	}
}

void UOneBrahamGameUserSettings::ApplyWorldBoundSettings(UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}

	ApplyColourBlindModeToWorld(World);

	// Sound-mix class overrides are pushed onto the world's audio device; before
	// any world exists ApplyAudioSettings has nothing to push them to, so saved
	// volumes only took effect once a slider was touched.
	ApplyAudioSettings();

	// Not world state, but nothing applied the saved toggles at startup either
	// (the engine only calls ApplyNonResolutionSettings, which does not reach
	// these): grain, fringe, bloom, DoF, flares, camera shake, and dynamic
	// resolution. Cheap and idempotent, so every world gets them - except in
	// the editor, where a PIE session would leave them (dynamic resolution,
	// switched-off post) at game-setting priority in the editor's viewports
	// after it ends.
	if (!GIsEditor)
	{
		ApplyPostProcessCVars();
		ApplyDynamicResolutionCVars();
	}

	if (IConsoleVariable* Gore = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Gore")))
	{
		Gore->Set(bGoreEnabled ? 1 : 0, ECVF_SetByGameSetting);
	}

	OB_LOG("[Settings] World-bound settings applied to %s (colour-blind mode %s).",
		*World->GetMapName(), *UEnum::GetValueAsString(ColourBlindMode));
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
