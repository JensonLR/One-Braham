// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamConsole.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Core/OneBrahamBenchmark.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Characters/OneBrahamCharacter.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UOneBrahamConsole::UOneBrahamConsole()
{
}

void UOneBrahamConsole::PostRender_Console_GameViewport(UCanvas* Canvas)
{
	// Shipping builds keep the console available for the ops team on dedicated
	// servers, but clients only see it when the player asked for it with ~ and
	// the build allows cheats. This is enforced by the engine's own
	// bAllowCheatsInShipping flag; we only add ONE BRAHAM branding.
#if !UE_BUILD_SHIPPING
	if (Canvas != nullptr)
	{
		// Deliberately empty: the engine draws the console body. The override
		// exists so this class owns the hook rather than a hidden engine default
		// appearing to be one, and so a future overlay has an obvious place to
		// go. The QA readouts that exist today are commands, not overlays they
		// hang off: `ob.Audio.Dump` logs the mix, `ob.NetDebug 1` draws the
		// replication overlay, `ob.netbudget` reports the frame budget.
	}
#endif
	Super::PostRender_Console_GameViewport(Canvas);
}

bool UOneBrahamConsole::ConsoleCommand(const FString& Command)
{
	// Route ONE BRAHAM QA commands, then fall through to the engine console.
	const FString Trimmed = Command.TrimStartAndEnd();
	FString Verb;
	FString Args;
	if (Trimmed.Split(TEXT(" "), &Verb, &Args))
	{
		Verb = Verb.ToLower();

		TArray<FString> ArgList;
		Args.ParseIntoArrayWS(ArgList);

		if (Verb == TEXT("ob.give"))
		{
			OBGiveWeapon(ArgList.Num() > 0 ? ArgList[0] : TEXT("W_Carbine556"));
			return true;
		}
		if (Verb == TEXT("ob.lockdown"))
		{
			OBSetLockdownStage(ArgList.Num() > 0 ? FCString::Atoi(*ArgList[0]) : -1);
			return true;
		}
		if (Verb == TEXT("ob.endmatch"))
		{
			OBEndMatch(ArgList.Num() > 0 ? FCString::Atoi(*ArgList[0]) : -1);
			return true;
		}
		if (Verb == TEXT("ob.netbudget"))
		{
			OBReportNetBudget();
			return true;
		}
		if (Verb == TEXT("ob.audio.dump"))
		{
			OBDumpAudio();
			return true;
		}
		if (Verb == TEXT("ob.bench.skydeck"))
		{
			// Seconds per floor, so a short pass can be run while iterating.
			OBBenchSkydeck(ArgList.Num() > 0 ? FCString::Atof(*ArgList[0]) : 6.f);
			return true;
		}
		if (Verb == TEXT("ob.bench.skydeck.stop"))
		{
			OBStopBench();
			return true;
		}
	}

	return Super::ConsoleCommand(Command);
}

void UOneBrahamConsole::OBDumpAudio()
{
	// The mix state, one line per bus: what the submix faders, the voice duck and
	// the occlusion filter are actually doing, rather than what the settings say
	// they should be. The audio manager owns the state; this is the way to see it
	// without a debugger.
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AOneBrahamPlayerController* OBPC = Cast<AOneBrahamPlayerController>(PC);
	UOneBrahamAudioManagerComponent* Audio = OBPC != nullptr ? OBPC->GetAudioManager() : nullptr;

	if (Audio == nullptr)
	{
		OB_WARN("ob.audio.dump: no audio manager on the local player. Audio mixes only "
			"exist where there is a player: run this on a client or a listen server.");
		return;
	}

	// One line per bus, plus the voice duck and the occlusion filter, which are
	// the two values a mix problem usually turns out to be.
	TArray<FString> Lines;
	Lines.Add(TEXT("ob.Audio.Dump - local mix state"));
	Lines.Add(Audio->DumpAudioState());

	for (const FString& Line : Lines)
	{
		OB_LOG("%s", *Line);
	}
}

void UOneBrahamConsole::OBBenchSkydeck(float SecondsPerLeg)
{
	UWorld* World = GetWorld();

	UOneBrahamBenchmarkSubsystem* Benchmark = World != nullptr
		? World->GetSubsystem<UOneBrahamBenchmarkSubsystem>()
		: nullptr;

	if (Benchmark == nullptr)
	{
		OB_WARN("ob.Bench.Skydeck: this world has no benchmark subsystem. It exists in "
			"game worlds on a client or listen server; a dedicated server cannot render.");
		return;
	}

	FText Refusal;

	if (!Benchmark->StartBenchmark(SecondsPerLeg, Refusal))
	{
		OB_WARN("ob.Bench.Skydeck: %s", *Refusal.ToString());
		return;
	}

	OB_LOG("ob.Bench.Skydeck: started, %.1fs per floor, report goes to Saved/Benchmarks.",
		SecondsPerLeg);
}

void UOneBrahamConsole::OBStopBench()
{
	UWorld* World = GetWorld();

	UOneBrahamBenchmarkSubsystem* Benchmark = World != nullptr
		? World->GetSubsystem<UOneBrahamBenchmarkSubsystem>()
		: nullptr;

	if (Benchmark == nullptr || !Benchmark->IsRunning())
	{
		OB_LOG("ob.Bench.Skydeck.Stop: nothing is running.");
		return;
	}

	Benchmark->AbortBenchmark();
}

void UOneBrahamConsole::OBGiveWeapon(const FString& WeaponId)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AOneBrahamCharacter* Character = PC ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
	if (Character == nullptr || Character->GetWeaponComponent() == nullptr)
	{
		OB_WARN("ob.give: no local character with a weapon component.");
		return;
	}

	Character->GetWeaponComponent()->ServerGiveWeaponDebug(FName(*WeaponId));
	OB_LOG("ob.give: requested '%s'.", *WeaponId);
}

void UOneBrahamConsole::OBSetLockdownStage(int32 Stage)
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr || GS->GetLockdownComponent() == nullptr)
	{
		OB_WARN("ob.lockdown: no One Braham game state.");
		return;
	}

	GS->GetLockdownComponent()->ServerForceStage(static_cast<EOneBrahamLockdownStage>(FMath::Clamp(Stage, 0, 3)));
	OB_LOG("ob.lockdown: forced stage %d.", Stage);
}

void UOneBrahamConsole::OBEndMatch(int32 WinningTeamIndex)
{
	UWorld* World = GetWorld();
	AOneBrahamGameMode* GM = World ? World->GetAuthGameMode<AOneBrahamGameMode>() : nullptr;
	if (GM == nullptr)
	{
		OB_WARN("ob.endmatch: only the server can end a match.");
		return;
	}

	GM->DebugForceMatchResult(WinningTeamIndex);
	OB_LOG("ob.endmatch: forced result for team index %d.", WinningTeamIndex);
}

void UOneBrahamConsole::OBReportNetBudget()
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	OB_LOG("=== ONE BRAHAM NET BUDGET ===");
	GS->LogNetBudget();
}
