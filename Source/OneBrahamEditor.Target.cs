// Copyright (c) One Braham. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// Editor target. Used by BuildEditor.ps1 / BuildEditor.bat and by the
/// Tools/Python automation scripts (asset generation, map assembly, validation).
/// </summary>
public class OneBrahamEditorTarget : TargetRules
{
	public OneBrahamEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "OneBraham" });

		// Nothing here may change the shared build environment (compiler flags,
		// global definitions, logging or checks in Shipping): this target builds
		// against the Launcher engine's prebuilt modules, and UBT refuses a target
		// that would need them rebuilt. The project's C++ is standards-conforming,
		// so the engine's /permissive- default stands.
	}
}
