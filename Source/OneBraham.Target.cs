// Copyright (c) One Braham. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// Standalone game target (client). Shipped to Steam as "OneBraham.exe".
/// Ships with the shooter handshake so clients cannot be spoofed by plain
/// editor builds, and includes the cooked asset registry for streaming levels.
/// </summary>
public class OneBrahamTarget : TargetRules
{
	public OneBrahamTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "OneBraham" });

		// --- ONE BRAHAM shipping configuration -------------------------------
		// Server-authoritative shooter: we never trust the client, so we keep the
		// shooter handshake on for all non-shipping builds and the packaged client.
		bUseLoggingInShipping = false;
		bUseChecksInShipping = false;

		// Steam build: build with -cryptokeys & pak signing in PackageClient.ps1.
		bUsesSteam = false; // Steamworks is loaded through OnlineSubsystemSteam plugin.

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows client requirements: D3D12 SM6 + Lumen/Nanite.
			WindowsPlatform.bStrictConformanceMode = false;
		}
	}
}
