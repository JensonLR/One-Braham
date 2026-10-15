// Copyright (c) One Braham. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// ONE BRAHAM runtime module.
///
/// Design notes:
///  - We deliberately avoid GameplayAbilitySystem to keep the server tick budget
///    tight for 12-16 player matches; damage, health, downed and inventory are
///    implemented as explicit, replicated components with server authority.
///  - Voice uses the engine "Voice" module (IVoiceCapture / IVoiceEncoder) and
///    pushes opus-encoded frames over unreliable RPC into a per-speaker
///    USoundWaveProcedural, which gives us real 3D attenuation and submix FX.
/// </summary>
public class OneBraham : ModuleRules
{
	public OneBraham(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = false;
		bUseUnity = true;
		DefaultBuildSettings = BuildSettingsVersion.Latest;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			"GameplayTasks",
			"UMG",
			"Slate",
			"SlateCore",
			"CommonUI",
			"AIModule",
			"NavigationSystem",
			"Niagara",
			"PhysicsCore",
			"Chaos",
			"DeveloperSettings",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"Voice",
			"AudioMixer",
			"AudioExtensions",
			"Json",
			"JsonUtilities",
			"ApplicationCore",
			"RenderCore",
			"RHI",
			"NetCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"SlateCore",
			"Projects",
			"MoviePlayer",
			"AudioMixer",
			"PreLoadScreen",
			"PakFile",
			"EngineCameras" // UPerlinNoiseCameraShakePattern, used by the camera shakes
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"AssetRegistry",
				"AssetTools",
				"EditorScriptingUtilities",
				"EditorSubsystem",
				"DataValidation",
				"BlueprintGraph",
				"KismetCompiler",
				"DerivedDataCache"
			});
		}

		// Public include root so headers can be included module-relative, e.g.
		//   #include "Core/OneBrahamGameMode.h"
		PublicIncludePaths.Add(ModuleDirectory);

		// One Braham defines -----------------------------------------------------
		PublicDefinitions.Add("ONE_BRAHAM_GAME_NAME=\"ONE BRAHAM\"");
		PublicDefinitions.Add("ONE_BRAHAM_NET_PROTOCOL=1");
		PublicDefinitions.Add("ONE_BRAHAM_MAX_SQUADS=4");
		PublicDefinitions.Add("ONE_BRAHAM_SQUAD_SIZE=4");
		PublicDefinitions.Add("ONE_BRAHAM_MAX_PLAYERS=16");
		PublicDefinitions.Add("ONE_BRAHAM_FLOORS=17");
		PublicDefinitions.Add("ONE_BRAHAM_MAX_VOICE_FRAME_BYTES=1024");
		PublicDefinitions.Add("ONE_BRAHAM_VOICE_SAMPLE_RATE=16000");

		// Voice capture requires an input device; dedicated servers get a stub.
		if (Target.Type == TargetType.Server)
		{
			PublicDefinitions.Add("ONE_BRAHAM_HEADLESS_VOICE=1");
		}
	}
}
