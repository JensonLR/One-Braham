// Copyright (c) One Braham. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// Dedicated server target. Packaged by Scripts/PackageServer.ps1 (and its .bat
/// wrapper) on Windows, and by Scripts/Package.sh Server on a Linux host.
///
/// Notes:
///  - WITH_SERVER_CODE keeps all Server_* RPC validation paths compiled in.
///  - bUseLoggingInShipping is enabled for servers so that the anti-cheat audit
///    log (LogOneBrahamAudit) is written in production.
///  - A Server target needs a source build of the engine: the Launcher
///    install cannot build one (README_BUILD.md).
///  - Rendering is stripped (target type Server implies this) so Lumen/Nanite
///    costs are not paid on headless hosts.
/// </summary>
public class OneBrahamServerTarget : TargetRules
{
	public OneBrahamServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "OneBraham" });

		// Server logging stays on in shipping for the audit trail. That changes
		// how the engine itself is compiled, so this target has its own build
		// environment, which UBT only allows on a source build of the engine;
		// a dedicated server needs one anyway.
		BuildEnvironment = TargetBuildEnvironment.Unique;
		bUseLoggingInShipping = true;
		bUseChecksInShipping = false;

		// Server-only code tests UE_SERVER, or IsRunningDedicatedServer() at
		// runtime; there are no project-wide server defines.
	}
}
