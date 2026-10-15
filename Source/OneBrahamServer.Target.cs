// Copyright (c) One Braham. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// Dedicated server target. Packaged by PackageServer.ps1 / PackageServer.bat
/// and by Scripts/PackageServer.sh for Linux hosts.
///
/// Notes:
///  - WITH_SERVER_CODE keeps all Server_* RPC validation paths compiled in.
///  - bUseLoggingInShipping is enabled for servers so that anti-cheat audit
///    logs (Config: [OneBraham.Audit]) are written in production.
///  - Rendering is stripped (target type Server implies this) so Lumen/Nanite
///    costs are not paid on headless hosts.
/// </summary>
public class OneBrahamServerTarget : TargetRules
{
	public OneBrahamServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "OneBraham" });

		// Server logging stays on in shipping for audit trails.
		bUseLoggingInShipping = true;
		bUseChecksInShipping = false;

		// Never cook client-only content into the server pak (halves size).
		GlobalDefinitions.Add("ONE_BRAHAM_SERVER_BUILD=1");

		// Dedicated servers do not need the Steam client overlay, but do need
		// the Steam GameServer API for session registration on Linux hosts.
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			GlobalDefinitions.Add("ONE_BRAHAM_LINUX_SERVER=1");
		}
	}
}
