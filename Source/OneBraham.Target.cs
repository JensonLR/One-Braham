// Copyright (c) One Braham. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// Standalone game target (client). Shipped to Steam as "OneBraham.exe".
/// Builds against the Launcher engine; see README_BUILD.md.
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
		// Nothing here may change the shared build environment (compiler flags,
		// global definitions, logging or checks in Shipping): the client builds
		// against the Launcher engine's prebuilt modules, and UBT refuses a target
		// that would need them rebuilt. The project's C++ is standards-conforming,
		// so the engine's /permissive- default stands.

		// Steamworks comes in through the OnlineSubsystemSteam plugin, so the
		// target's own bUsesSteam stays at its default (off).
	}
}
