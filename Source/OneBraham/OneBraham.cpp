// Copyright (c) One Braham. All Rights Reserved.

#include "OneBraham.h"

#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "Audio/OneBrahamProceduralAudio.h"
#include "VFX/OneBrahamVFXStatics.h"
#include "Engine/World.h"

// ==========================================================================
//  Log category definitions
// ==========================================================================
DEFINE_LOG_CATEGORY(LogOneBraham);
DEFINE_LOG_CATEGORY(LogOneBrahamNet);
DEFINE_LOG_CATEGORY(LogOneBrahamWeapons);
DEFINE_LOG_CATEGORY(LogOneBrahamMatch);
DEFINE_LOG_CATEGORY(LogOneBrahamVoice);
DEFINE_LOG_CATEGORY(LogOneBrahamUI);
DEFINE_LOG_CATEGORY(LogOneBrahamAccess);
DEFINE_LOG_CATEGORY(LogOneBrahamExtraction);
DEFINE_LOG_CATEGORY(LogOneBrahamAudio);
DEFINE_LOG_CATEGORY(LogOneBrahamAudit);
DEFINE_LOG_CATEGORY(LogOneBrahamBots);

// ==========================================================================
//  Console variables
//  Everything gameplay-tunable that QA or design wants to poke at runtime
//  lives here. See Docs/18_Testing_Performance_Optimisation.md for usage.
// ==========================================================================
namespace OneBrahamCVars
{
	/** Global damage scalar - used by QA weapon-damage automation tests. */
	static float DamageScale = 1.f;
	FAutoConsoleVariableRef CVarDamageScale(
		TEXT("ob.DamageScale"),
		DamageScale,
		TEXT("Global outgoing damage multiplier (server-authoritative). 1.0 = shipping values."),
		ECVF_Cheat);

	/** Enables the on-screen replication/net budget overlay for soak tests. */
	static int32 NetDebugOverlay = 0;
	FAutoConsoleVariableRef CVarNetDebug(
		TEXT("ob.NetDebug"),
		NetDebugOverlay,
		TEXT("0: off, 1: draw OB network budget overlay (bandwidth, RPC rate, relevancy)."),
		ECVF_Cheat);

	/** Turns the proximity voice system into a local-only loopback for QA. */
	static int32 VoiceLoopback = 0;
	FAutoConsoleVariableRef CVarVoiceLoopback(
		TEXT("ob.VoiceLoopback"),
		VoiceLoopback,
		TEXT("1: routed voice packets are echoed back to the local client (voice QA without a second machine)."),
		ECVF_Cheat);

	/** Forces the lockdown director to a given stage. -1 = normal flow. */
	static int32 ForcedLockdownStage = -1;
	FAutoConsoleVariableRef CVarForcedLockdown(
		TEXT("ob.Lockdown.ForceStage"),
		ForcedLockdownStage,
		TEXT("-1: normal flow, 0: normal, 1: pre-lockdown, 2: lockdown, 3: purge. QA only."),
		ECVF_Cheat);

	/** Skips the extraction hold timer (QA extraction-flow testing). */
	static int32 FastExtraction = 0;
	FAutoConsoleVariableRef CVarFastExtraction(
		TEXT("ob.Extraction.Fast"),
		FastExtraction,
		TEXT("1: reduce extraction hold + helicopter travel to 3 seconds each."),
		ECVF_Cheat);

	/** Logs every server-side anti-cheat rejection with full context. */
	static int32 VerboseSecurityLog = 1;
	FAutoConsoleVariableRef CVarSecurityLog(
		TEXT("ob.Security.Verbose"),
		VerboseSecurityLog,
		TEXT("1: log every rejected client action to LogOneBrahamAudit (shipping default)."),
		ECVF_Default);
}

// ==========================================================================
//  Module implementation
// ==========================================================================
class FOneBrahamModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogOneBraham, Log,
			TEXT("[OB] ONE BRAHAM runtime module online. Build %d.%d.%d, net protocol %d."),
			ONE_BRAHAM_VERSION_MAJOR, ONE_BRAHAM_VERSION_MINOR, ONE_BRAHAM_VERSION_PATCH, ONE_BRAHAM_NET_PROTOCOL_VERSION);

		// Dedicated server detection - used to disable client-only subsystems
		// (voice capture, UMG overlays, Steam overlay hooks).
		const bool bIsDedicatedServer = IsRunningDedicatedServer();
		const bool bIsPlayInEditor = UE_BUILD_EDITOR && GIsEditor;

		UE_LOG(LogOneBraham, Log, TEXT("[OB] Instance type: %s%s"),
			bIsDedicatedServer ? TEXT("DEDICATED SERVER") : TEXT("CLIENT"),
			bIsPlayInEditor ? TEXT(" (editor)") : TEXT(""));

		// Command-line audit of the two most common support tickets:
		//   -log  (players forget)  and  -SteamServerName (server operators forget)
		if (bIsDedicatedServer)
		{
			FString ServerName;
			if (!FParse::Value(FCommandLine::Get(), TEXT("SteamServerName="), ServerName))
			{
				ServerName = TEXT("One Braham Dedicated");
			}
			UE_LOG(LogOneBraham, Log, TEXT("[OB] Dedicated server name: %s"), *ServerName);
		}

		// The VFX cache remembers which Niagara systems are missing so a code-only
		// checkout does not retry a failed load on every bullet. In the editor that
		// memory outlived the play session, so a system built by build_vfx.py
		// while the editor was open stayed "missing" until a restart. A world's
		// teardown is the point to forget it.
		WorldCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddLambda(
			[](UWorld*, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
			{
				UOneBrahamVFXStatics::ReleaseCachedAssets();
			});
	}

	virtual void ShutdownModule() override
	{
		// Release the procedurally synthesised audio cache before the audio device
		// and object graph go away, otherwise the strong references can outlive the
		// package they were allocated in.
		OneBrahamProceduralAudio::ReleaseSynthesisedSounds();

		FWorldDelegates::OnPostWorldCleanup.Remove(WorldCleanupHandle);
		UOneBrahamVFXStatics::ReleaseCachedAssets();

		UE_LOG(LogOneBraham, Log, TEXT("[OB] ONE BRAHAM runtime module shutting down."));
	}

private:
	FDelegateHandle WorldCleanupHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FOneBrahamModule, OneBraham, "OneBraham");
