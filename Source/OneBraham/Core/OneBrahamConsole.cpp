// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamConsole.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Core/OneBrahamBenchmark.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Characters/OneBrahamCharacter.h"
#include "Bots/OneBrahamBotController.h"
#include "EngineUtils.h"
#include "Extraction/OneBrahamExtractionComponent.h"
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
	//
	// The verb is the first token whether or not there are arguments. An earlier
	// version split on a space and only routed when the split succeeded, which
	// meant every one-word command - ob.lockdown, ob.netbudget, ob.audio.dump,
	// ob.extraction.fast, ob.bench.skydeck.stop - fell straight through to the
	// engine console, where nothing answers them and nothing says so.
	const FString Trimmed = Command.TrimStartAndEnd();

	TArray<FString> Tokens;
	Trimmed.ParseIntoArrayWS(Tokens);

	if (Tokens.Num() > 0)
	{
		const FString Verb = Tokens[0].ToLower();

		TArray<FString> ArgList = Tokens;
		ArgList.RemoveAt(0);

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
		if (Verb == TEXT("ob.extraction.fast"))
		{
			OBExtractionFast();
			return true;
		}
		if (Verb == TEXT("ob.extraction.abort"))
		{
			OBExtractionAbort();
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
		if (Verb == TEXT("ob.bots.dump"))
		{
			OBDumpBots();
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
		if (Verb == TEXT("ob.weapon.remove"))
		{
			OBRemoveCurrentWeapon();
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

void UOneBrahamConsole::OBDumpBots()
{
	// Bots exist only where the game mode spawned them - the server - so on a
	// client this lists nothing, and says why rather than printing an empty list.
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		OB_WARN("ob.bots.dump: bots live on the server. Run this on a listen host or a dedicated server.");
		return;
	}

	int32 Count = 0;
	for (TActorIterator<AOneBrahamBotController> It(World); It; ++It)
	{
		OB_LOG("%s", *It->GetBotDebugString());
		++Count;
	}

	OB_LOG("ob.bots.dump: %d bot(s).", Count);
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

void UOneBrahamConsole::OBRemoveCurrentWeapon()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AOneBrahamCharacter* Character = PC ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
	UOneBrahamWeaponComponent* Weapons = Character ? Character->GetWeaponComponent() : nullptr;

	if (Weapons == nullptr)
	{
		OB_WARN("ob.weapon.remove: no local character with a weapon component.");
		return;
	}

	// Destruction rather than a drop, which is the point: this is the verb for
	// the unarmed cases - the player who has just been relieved of their rifle,
	// the lobby wanderer, the bug where the first-person arms animate with
	// nothing in them. A drop would put a perfectly good carbine on the floor and
	// change the thing being tested.
	const AOneBrahamWeaponBase* Held = Weapons->GetCurrentWeapon();
	const FString HeldId = Held != nullptr && Held->Definition != nullptr
		? Held->Definition->WeaponId.ToString()
		: FString(TEXT("nothing"));

	Weapons->ServerRemoveCurrentWeapon();

	OB_LOG("ob.weapon.remove: destroyed '%s'.", *HeldId);
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

void UOneBrahamConsole::OBExtractionFast()
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	UOneBrahamExtractionComponent* Extraction = GS ? GS->GetExtractionComponent() : nullptr;

	if (Extraction == nullptr)
	{
		OB_WARN("ob.extraction.fast: no One Braham game state.");
		return;
	}

	if (GS->MatchMode != EOneBrahamMatchMode::Ascent)
	{
		OB_WARN("ob.extraction.fast: this mode has no winch. ASCENT only.");
		return;
	}

	// One command, not three. A QA pass that has to walk to the Skydeck, hold a
	// console for 1.4 seconds and then wait 55 seconds of helicopter is a QA pass
	// nobody runs, so this calls the winch for whoever typed the command and then
	// jumps the state machine straight to the hold.
	if (Extraction->GetExtractionState() == EOneBrahamExtractionState::Inactive)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(
			World->GetFirstPlayerController());
		AOneBrahamPlayerState* PS = PC ? PC->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
		const int32 SquadId = PS ? GS->GetSquadIdForPlayer(PS->GetPlayerId()) : INDEX_NONE;

		FText Refusal;
		if (!Extraction->ServerCallExtraction(SquadId, PS, Refusal))
		{
			OB_WARN("ob.extraction.fast: %s", *Refusal.ToString());
			return;
		}

		OB_LOG("ob.extraction.fast: called the winch for squad %d.", SquadId);
	}

	Extraction->ServerSkipToHolding();
	OB_LOG("ob.extraction.fast: squad %d is on the wire, holding.",
		Extraction->GetClaimingSquadId());
}

void UOneBrahamConsole::OBExtractionAbort()
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	UOneBrahamExtractionComponent* Extraction = GS ? GS->GetExtractionComponent() : nullptr;

	if (Extraction == nullptr)
	{
		OB_WARN("ob.extraction.abort: no One Braham game state.");
		return;
	}

	// The failure path is the one that never gets tested by playing the game
	// honestly, and it is the path with the end-of-match copy on it.
	Extraction->ServerAbortExtraction(EOneBrahamExtractionFailReason::Contested);
	OB_LOG("ob.extraction.abort: attempt aborted as CONTESTED.");
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
