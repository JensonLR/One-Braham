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
		// Editor targets share UnrealEditor's build environment, so their settings
		// must match the installed engine's; Latest tracks it (5.5 and 5.8 alike).
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "OneBraham" });

		// Editor builds keep full logging + checks so QA can read logs directly.
		bUseLoggingInShipping = false;
		bUseChecksInShipping = false;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			WindowsPlatform.bStrictConformanceMode = false;
		}
	}
}
