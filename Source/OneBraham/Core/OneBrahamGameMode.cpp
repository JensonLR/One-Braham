// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameMode.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Core/OneBrahamSpawnPoint.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Bots/OneBrahamBotController.h"
#include "AI/OneBrahamBotRules.h"
#include "Extraction/OneBrahamExtractionPad.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Data/OneBrahamWeaponDataAsset.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Core/OneBrahamSpawnRules.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "GameFramework/CharacterMovementComponent.h"

// ==========================================================================
//  Dev acceleration (Docs/18): compress a match into minutes for the
//  -OBSmokeFullMatch run. Read every tick, so -ExecCmds after the map loads
//  still applies. None of it exists in Shipping.
// ==========================================================================
#if !UE_BUILD_SHIPPING
namespace OneBrahamMatchDev
{
	TAutoConsoleVariable<float> CVarWarmupSeconds(TEXT("ob.Match.WarmupSeconds"), -1.f,
		TEXT("Dev: the warm-up ends this many seconds after it began (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarTimeLimit(TEXT("ob.Match.TimeLimit"), -1.f,
		TEXT("Dev: the match time limit in seconds, 0 for none (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarPostMatchSeconds(TEXT("ob.Match.PostMatchSeconds"), -1.f,
		TEXT("Dev: seconds of end screen before everybody leaves (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarScoreToWin(TEXT("ob.Match.ScoreToWin"), -1,
		TEXT("Dev: FIRE DRILL points to win (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarFollowBots(TEXT("ob.Match.FollowBots"), 0,
		TEXT("Dev: 1 = a local player with no pawn watches the highest living bot."), ECVF_Cheat);
}
#endif

namespace
{
	/** FIRE DRILL's points to win: the settings, or the dev override. */
	int32 OBResolveScoreToWin(const UOneBrahamMatchSettingsAsset* Settings)
	{
		int32 Score = Settings != nullptr ? Settings->ScoreToWin : 200;
#if !UE_BUILD_SHIPPING
		if (OneBrahamMatchDev::CVarScoreToWin.GetValueOnGameThread() > 0)
		{
			Score = OneBrahamMatchDev::CVarScoreToWin.GetValueOnGameThread();
		}
#endif
		return FMath::Max(1, Score);
	}

	/** Seconds of end screen: the settings, or the dev override. */
	float OBResolvePostMatchSeconds(const UOneBrahamMatchSettingsAsset* Settings)
	{
		float Seconds = Settings != nullptr ? static_cast<float>(Settings->PostMatchSeconds) : 45.f;
#if !UE_BUILD_SHIPPING
		if (OneBrahamMatchDev::CVarPostMatchSeconds.GetValueOnGameThread() >= 0.f)
		{
			Seconds = OneBrahamMatchDev::CVarPostMatchSeconds.GetValueOnGameThread();
		}
#endif
		return FMath::Max(1.f, Seconds);
	}

	/** Seamless travel needs the transition map; without it a travel is a hard one. */
	bool OBTransitionMapExists()
	{
		FString TransitionMap;
		if (GConfig == nullptr || !GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("TransitionMap"), TransitionMap, GEngineIni)
			|| TransitionMap.IsEmpty())
		{
			return false;
		}

		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(TransitionMap));
	}

	/** How often the squad list is rebuilt and the endings re-checked while a match runs. */
	constexpr float OBSquadRefreshSeconds = 0.5f;

	/** Seconds the host waits after telling its clients to leave, so the order reaches them. */
	constexpr float OBHostLeaveDelaySeconds = 1.5f;
}

AOneBrahamGameMode::AOneBrahamGameMode()
{
	// Classes: BP_OneBrahamGameMode may override these in the editor, but the
	// C++ defaults make a code-only checkout fully playable.
	DefaultPawnClass = AOneBrahamCharacter::StaticClass();
	PlayerControllerClass = AOneBrahamPlayerController::StaticClass();
	PlayerStateClass = AOneBrahamPlayerState::StaticClass();
	GameStateClass = AOneBrahamGameState::StaticClass();

	bUseSeamlessTravel = true;
	bPauseable = false;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // match bookkeeping does not need 60 Hz
}

// ==========================================================================
//  Init
// ==========================================================================
void AOneBrahamGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	ParseTravelOptions(Options);

	// Resolve the match settings asset: GameMode default override (set on
	// BP_OneBrahamGameMode) beats the GameInstance's per-mode asset.
	if (MatchSettings == nullptr)
	{
		const EOneBrahamMatchMode Mode = StaticEnum<EOneBrahamMatchMode>()->GetValueByNameString(PendingModeString) != INDEX_NONE
			? static_cast<EOneBrahamMatchMode>(StaticEnum<EOneBrahamMatchMode>()->GetValueByNameString(PendingModeString))
			: UOneBrahamGameSettings::Get().DefaultMode;

		if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
		{
			MatchSettings = GameInstance->GetMatchSettings(Mode);
		}
		else
		{
			MatchSettings = NewObject<UOneBrahamMatchSettingsAsset>(this, TEXT("DA_MatchSettings_Runtime"));
			MatchSettings->Mode = Mode;
			MatchSettings->EnsureDefaults();
		}
	}

	check(MatchSettings != nullptr);
	MatchSettings->EnsureDefaults();
	ConfigureClassesFromSettings();

	// Squad -> team map: ASCENT uses Alpha/Bravo/Charlie/Delta, FIRE DRILL uses
	// the two factions.
	SquadToTeamMap.Reset();
	if (MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
	{
		SquadToTeamMap.Add(EOneBrahamTeam::InternalStaff);
		SquadToTeamMap.Add(EOneBrahamTeam::ExternalContractors);
	}
	else
	{
		for (int32 Index = 0; Index < MatchSettings->TeamCount && Index < ONE_BRAHAM_MAX_SQUADS; ++Index)
		{
			const EOneBrahamTeam Team = static_cast<EOneBrahamTeam>(static_cast<uint8>(EOneBrahamTeam::Alpha) + Index);
			SquadToTeamMap.Add(Team);
		}
	}

	// Bots fill the slots no human holds (OneBrahamBotRules). The mode decides
	// whether that happens by default - ASCENT and FIRE DRILL do, so a solo or
	// half-full lobby still has three squads - and the match settings asset can
	// say otherwise (bFillWithBots / MaxBots, read by name so an asset built
	// before those fields existed still loads). `?Bots=N` on the travel URL, or
	// `-Bots=N` on a server's command line, caps the total; `?Bots=0` is none.
	bool bSettingsFill = OneBrahamBotRules::ModeFillsWithBotsByDefault(MatchSettings->Mode);
	int32 SettingsMaxBots = OneBrahamBotRules::FillAllSlots;
	OneBrahamBotRules::ReadSettingsBotFill(MatchSettings, bSettingsFill, SettingsMaxBots);

	int32 BotOption = 0;
	bool bBotOptionGiven = UGameplayStatics::HasOption(Options, TEXT("Bots"));
	if (bBotOptionGiven)
	{
		BotOption = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), 0);
	}
	else
	{
		// "-Bots=" with its dash: a bare "Bots=" also matched inside other
		// switches (anything ending in "...Bots=").
		bBotOptionGiven = FParse::Value(FCommandLine::Get(), TEXT("-Bots="), BotOption);
	}

	BotLimit = OneBrahamBotRules::ResolveBotLimit(bBotOptionGiven, BotOption, bSettingsFill, SettingsMaxBots);

	OB_MATCH_LOG("InitGame map='%s' mode=%s teams=%d teamSize=%d bots=%s dedicated=%s",
		*MapName,
		*UEnum::GetValueAsString(MatchSettings->Mode),
		SquadToTeamMap.Num(),
		MatchSettings->PlayersPerTeam,
		BotLimit < 0 ? TEXT("fill") : *FString::FromInt(BotLimit),
		IsRunningDedicatedServer() ? TEXT("yes") : TEXT("no"));
}

void AOneBrahamGameMode::ParseTravelOptions(const FString& Options)
{
	PendingModeString = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
	PendingPlayerName = UGameplayStatics::ParseOption(Options, TEXT("Name"));
	PendingArchetype = UGameplayStatics::ParseOption(Options, TEXT("Archetype"));

	if (PendingModeString.IsEmpty())
	{
		// Dedicated servers launched with `-Mode=FireDrill` on the command line.
		FParse::Value(FCommandLine::Get(), TEXT("Mode="), PendingModeString);
	}

	OB_MATCH_LOG("Travel options: Mode='%s' Name='%s' Archetype='%s'",
		*PendingModeString, *PendingPlayerName, *PendingArchetype);
}

void AOneBrahamGameMode::ConfigureClassesFromSettings()
{
	// A ServerGameModeClass override on the settings asset lets a designer swap
	// the pawn class per mode (NIGHT SHIFT uses an AI-focused variant).
	if (MatchSettings->Mode == EOneBrahamMatchMode::NightShift)
	{
		// NIGHT SHIFT runs the same pawn; the difference is the bot director.
		OB_MATCH_LOG("NIGHT SHIFT configuration: %d waves, %.0fs interval.",
			MatchSettings->NightShiftWaveCount, MatchSettings->NightShiftWaveIntervalSeconds);
	}
}

void AOneBrahamGameMode::InitGameState()
{
	Super::InitGameState();

	if (AOneBrahamGameState* GS = GetOBGameState())
	{
		GS->SetMatchSettings(MatchSettings);
		GS->ServerName = UGameplayStatics::ParseOption(OptionsString, TEXT("ServerName"));
		if (GS->ServerName.IsEmpty())
		{
			GS->ServerName = IsRunningDedicatedServer() ? TEXT("One Braham Dedicated") : TEXT("One Braham Listen Server");
		}
		GS->bIsDedicatedServerMatch = IsRunningDedicatedServer();
		GS->SurvivorsRemaining = 0;
	}
}

void AOneBrahamGameMode::StartPlay()
{
	// Spawn points must exist before the first player logs in.
	RebuildSpawnPointRegistry();

	Super::StartPlay();

	StartWarmup();

	// Bots take the empty squad slots now, so the roster (and the scoreboard)
	// is whole from the first frame; their pawns follow when the mode lets
	// anyone spawn. Local players have already logged in by this point, so
	// they keep their slots; anyone who joins later takes a bot's.
	if (BotLimit != 0)
	{
		PopulateWithBots(BotLimit);
	}
}

void AOneBrahamGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Respawn queue (FIRE DRILL). Handled here rather than with N individual
	// timers so a full 8v8 wipe is one tick of work.
	if (PendingRespawns.Num() > 0)
	{
		TArray<AController*> Ready;
		for (TPair<TObjectPtr<AController>, float>& Pair : PendingRespawns)
		{
			Pair.Value -= DeltaSeconds;
			if (Pair.Value <= 0.f && Pair.Key != nullptr)
			{
				Ready.Add(Pair.Key);
			}
		}

		for (AController* Controller : Ready)
		{
			// The death path leaves the corpse possessed (ragdoll, death cam,
			// looting), so a dead pawn is the normal case here, not null.
			APawn* OldPawn = Controller->GetPawn();
			if (OldPawn != nullptr && !IsPawnDeadForRespawn(OldPawn))
			{
				// Back on their feet some other way; nothing to restart.
				PendingRespawns.Remove(Controller);
				continue;
			}

			if (OldPawn != nullptr)
			{
				// RestartPlayer would otherwise reuse the corpse it is still holding.
				Controller->UnPossess();
			}

			RestartPlayer(Controller);

			if (Controller->GetPawn() != nullptr)
			{
				PendingRespawns.Remove(Controller);
			}
			else
			{
				// No spawn point free yet: keep the entry and try again shortly.
				PendingRespawns.Add(Controller, RespawnRetrySeconds);
			}
		}
	}

#if !UE_BUILD_SHIPPING
	// ob.Bots.Watch N: the first local player watches through bot N's eyes, so a
	// screenshot (the -OBSmoke run's, or F9) shows what the bot is doing.
	if (const IConsoleVariable* WatchVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Bots.Watch")))
	{
		const int32 WatchIndex = WatchVar->GetInt();
		APlayerController* Local = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (WatchIndex >= 0 && Local != nullptr && BotControllers.IsValidIndex(WatchIndex)
			&& IsValid(BotControllers[WatchIndex]) && BotControllers[WatchIndex]->GetPawn() != nullptr
			&& Local->GetViewTarget() != BotControllers[WatchIndex]->GetPawn())
		{
			Local->SetViewTarget(BotControllers[WatchIndex]->GetPawn());
		}
	}

	// ob.Match.FollowBots: a player without a pawn (withdrawn by the full-match
	// smoke run, or between lives) watches the living bot highest in the tower,
	// moving on when that one goes down.
	if (OneBrahamMatchDev::CVarFollowBots.GetValueOnGameThread() != 0 && !bMatchFinished)
	{
		APlayerController* Local = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Watched = Local != nullptr ? Cast<APawn>(Local->GetViewTarget()) : nullptr;
		if (Local != nullptr && Local->GetPawn() == nullptr && (Watched == nullptr || IsPawnDeadForRespawn(Watched)))
		{
			APawn* Best = nullptr;
			for (AOneBrahamBotController* Bot : BotControllers)
			{
				APawn* BotPawn = IsValid(Bot) ? Bot->GetPawn() : nullptr;
				if (BotPawn != nullptr && !IsPawnDeadForRespawn(BotPawn)
					&& (Best == nullptr || BotPawn->GetActorLocation().Z > Best->GetActorLocation().Z))
				{
					Best = BotPawn;
				}
			}
			if (Best != nullptr)
			{
				Local->SetViewTarget(Best);
			}
		}
	}

	ApplyDevOverrides();
#endif

	if (bBotFloorGraphPending)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (Nav == nullptr || !Nav->IsNavigationBuildInProgress())
		{
			BuildBotFloorGraph();
		}
	}

	// Bots without a pawn try again once a second: a start that was crowded
	// (every plaza point resting after the opening wave) frees up shortly.
	BotSpawnRetryAccumulator += DeltaSeconds;
	if (BotSpawnRetryAccumulator >= RespawnRetrySeconds && BotControllers.Num() > 0 && CanBotsSpawnNow())
	{
		BotSpawnRetryAccumulator = 0.f;
		for (AOneBrahamBotController* Bot : BotControllers)
		{
			// One life in a mode without respawns; a respawning mode's dead go
			// through the respawn queue, which keeps their corpse possessed.
			const bool bLifeLeft = Bot != nullptr && (Bot->LivesStarted == 0 || (MatchSettings != nullptr && MatchSettings->bRespawnEnabled));
			if (bLifeLeft && Bot->GetPawn() == nullptr && !PendingRespawns.Contains(Bot))
			{
				SpawnBotPawn(Bot);
			}
		}
	}

	// Time limit, for every mode that has one. The game state's
	// MatchTimeLimitSeconds is the number the HUD counts down from, so the
	// match ends when the clock players can see says it does - ASCENT's 35
	// minutes included, which used to be a countdown to nothing.
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS != nullptr && !bMatchFinished
		&& HasMatchTimeLimitExpired(GS->MatchTimeLimitSeconds, GS->PhaseElapsedSeconds, GS->MatchPhase))
	{
		HandleMatchTimeLimitReached();
	}

	const bool bRunning = GS != nullptr && !bMatchFinished
		&& (GS->MatchPhase == EOneBrahamMatchPhase::Active || GS->MatchPhase == EOneBrahamMatchPhase::Extraction);

	if (bRunning)
	{
		// The phase follows the winch: Extraction while an attempt is live, so
		// the music, the scoreboard and the bots' spawn rules all agree.
		if (const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent())
		{
			const bool bLive = Extraction->IsExtractionActive();
			if (bLive && GS->MatchPhase == EOneBrahamMatchPhase::Active)
			{
				SetMatchPhase(EOneBrahamMatchPhase::Extraction);
			}
			else if (!bLive && GS->MatchPhase == EOneBrahamMatchPhase::Extraction)
			{
				SetMatchPhase(EOneBrahamMatchPhase::Active);
			}
		}

		// The squad list is the replicated picture of who is still in it, and it
		// was only rebuilt on joins and spawns: a death changed the player state
		// and nothing else, so the HUD showed the dead as on site and the end
		// checks (which read the list) never saw a squad wiped. Rebuilt twice a
		// second, and the endings re-checked against it, whatever killed whom.
		SquadRefreshAccumulator += DeltaSeconds;
		if (SquadRefreshAccumulator >= OBSquadRefreshSeconds)
		{
			SquadRefreshAccumulator = 0.f;
			EvaluateMatchEndConditions();
		}

		if (!bMatchFinished)
		{
			TickControlPoints(DeltaSeconds);
		}
	}

	// Keep the survivor count fresh for the HUD.
	if (GS != nullptr && (GS->MatchPhase == EOneBrahamMatchPhase::Active || GS->MatchPhase == EOneBrahamMatchPhase::Extraction))
	{
		GS->SurvivorsRemaining = GS->GetAlivePlayerCount();
	}
}

void AOneBrahamGameMode::ApplyDevOverrides()
{
#if !UE_BUILD_SHIPPING
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || MatchSettings == nullptr)
	{
		return;
	}

	// The time limit lives on the game state, where the HUD counts it down from,
	// so the override moves the clock everybody reads.
	const float TimeLimit = OneBrahamMatchDev::CVarTimeLimit.GetValueOnGameThread();
	if (TimeLimit >= 0.f && !FMath::IsNearlyEqual(GS->MatchTimeLimitSeconds, TimeLimit))
	{
		GS->MatchTimeLimitSeconds = TimeLimit;
		GS->ForceNetUpdate();
		OB_MATCH_LOG("Dev: time limit set to %.0f s (ob.Match.TimeLimit).", TimeLimit);
	}

	const float Warmup = OneBrahamMatchDev::CVarWarmupSeconds.GetValueOnGameThread();
	if (Warmup >= 0.f && GS->MatchPhase == EOneBrahamMatchPhase::Warmup && GetWorld() != nullptr)
	{
		const float WarmupStart = WarmupEndTime - static_cast<float>(MatchSettings->WarmupSeconds);
		if (GetWorld()->GetTimeSeconds() - WarmupStart >= Warmup)
		{
			OB_MATCH_LOG("Dev: warm-up cut to %.0f s (ob.Match.WarmupSeconds).", Warmup);
			GetWorldTimerManager().ClearTimer(MatchPhaseTimerHandle);
			StartActiveMatch();
		}
	}
#endif
}

// ==========================================================================
//  Match flow
// ==========================================================================
void AOneBrahamGameMode::StartWarmup()
{
	SetMatchPhase(EOneBrahamMatchPhase::Warmup);
	WarmupEndTime = GetWorld()->GetTimeSeconds() + static_cast<float>(MatchSettings->WarmupSeconds);

	Announce(FText::FromString(TEXT("Welcome to One Braham. Please sign in at reception.")), true);

	if (MatchSettings->WarmupSeconds > 0)
	{
		GetWorldTimerManager().SetTimer(MatchPhaseTimerHandle, this, &AOneBrahamGameMode::HandleWarmupComplete,
			static_cast<float>(MatchSettings->WarmupSeconds), false);
	}
	else
	{
		StartActiveMatch();
	}
}

void AOneBrahamGameMode::HandleWarmupComplete()
{
	StartActiveMatch();
}

void AOneBrahamGameMode::StartActiveMatch()
{
	SetMatchPhase(EOneBrahamMatchPhase::Active);
	MatchStartTime = GetWorld()->GetTimeSeconds();

	Announce(FText::FromString(TEXT("The lift lobby is open. Ascend.")), true);

	// Bring in everyone held back during warmup. HandleStartingNewPlayer parks
	// ASCENT players as spectators of the plaza until now, and nothing else
	// spawned them: every match began with an empty building and ended "no
	// survivors". Players on the spectator team (squads full) stay spectating.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController == nullptr || PlayerController->GetPawn() != nullptr)
		{
			continue;
		}

		AOneBrahamPlayerState* State = PlayerController->GetPlayerState<AOneBrahamPlayerState>();
		if (State == nullptr || State->Team == EOneBrahamTeam::Spectator)
		{
			continue;
		}

		if (State->LifeState == EOneBrahamLifeState::Spectating)
		{
			State->LifeState = EOneBrahamLifeState::Alive;
		}
		RestartPlayer(PlayerController);
	}

	// The bots walk in off the plaza with everybody else.
	for (AOneBrahamBotController* Bot : BotControllers)
	{
		SpawnBotPawn(Bot);
	}

	BindGameStateComponents();

	if (AOneBrahamGameState* GS = GetOBGameState())
	{
		if (UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
		{
			Lockdown->StartDirector();
		}
	}

	OB_MATCH_LOG("Match ACTIVE at t=%.2f on '%s' (mode %s)",
		MatchStartTime, *GetName(), *UEnum::GetValueAsString(MatchSettings->Mode));
}

void AOneBrahamGameMode::SetMatchPhase(EOneBrahamMatchPhase NewPhase)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || GS->MatchPhase == NewPhase)
	{
		return;
	}

	GS->MatchPhase = NewPhase;

	// Phase transitions are the natural sync point for gameplay tags; the
	// GameState broadcasts to UI on the OnRep so clients stay consistent.
	OB_MATCH_LOG("Phase: %s", *UEnum::GetValueAsString(NewPhase));
}

EOneBrahamMatchPhase AOneBrahamGameMode::GetMatchPhase() const
{
	const AOneBrahamGameState* GS = GetOBGameState();
	return GS != nullptr ? GS->MatchPhase : EOneBrahamMatchPhase::Warmup;
}

AOneBrahamGameState* AOneBrahamGameMode::GetOBGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
}

EOneBrahamMatchMode AOneBrahamGameMode::GetMatchMode() const
{
	return MatchSettings != nullptr ? MatchSettings->Mode : EOneBrahamMatchMode::Ascent;
}

void AOneBrahamGameMode::BindGameStateComponents()
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	// Extraction is the ASCENT win condition: the component owns the sequence,
	// the game mode owns the consequence. One bind, both directions handled.
	if (UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent())
	{
		Extraction->OnExtractionComplete.AddUniqueDynamic(this, &AOneBrahamGameMode::HandleExtractionComplete);
		Extraction->OnExtractionFailed.AddUniqueDynamic(this, &AOneBrahamGameMode::HandleExtractionFailed);
	}

	if (UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
	{
		Lockdown->OnStageChanged.AddUniqueDynamic(this, &AOneBrahamGameMode::HandleLockdownStageChanged);
	}

	UpdateObjectiveState();
}

void AOneBrahamGameMode::HandleExtractionComplete(int32 WinningSquadId, EOneBrahamTeam WinningTeam)
{
	if (GetOBGameState() == nullptr || bMatchFinished)
	{
		return;
	}

	// FinishMatch puts the squad's players who are on their feet on the wire and
	// counts them: a squad that extracts with two of its four has still won, and
	// the result screen says so.
	OB_MATCH_LOG("Extraction complete: squad %d (%s) is on the wire.", WinningSquadId, *GetWinnerName(WinningSquadId));

	FinishMatch(EOneBrahamMatchEnding::Extracted, WinningSquadId, 0);
}

void AOneBrahamGameMode::HandleExtractionFailed(int32 SquadId, EOneBrahamExtractionFailReason Reason)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || bMatchFinished)
	{
		return;
	}

	SetSquadExtractionClaim(SquadId, false);

	// Dry, procedural, and in the building's voice: the humour is that the tower
	// keeps filing a report while people are shooting at each other.
	FText Announcement;
	switch (Reason)
	{
	case EOneBrahamExtractionFailReason::CallersEliminated:
		Announcement = FText::FromString(TEXT("Winch request withdrawn. The requesting party is no longer on site."));
		break;
	case EOneBrahamExtractionFailReason::LoiterExpired:
		Announcement = FText::FromString(TEXT("The helicopter has departed. It will not be returning."));
		break;
	case EOneBrahamExtractionFailReason::Purge:
		Announcement = FText::FromString(TEXT("Skydeck access suspended during security purge. Please move away from the edge."));
		break;
	case EOneBrahamExtractionFailReason::Contested:
		Announcement = FText::FromString(TEXT("Winch zone contested. The winch is not available."));
		break;
	default:
		Announcement = FText::FromString(TEXT("Winch unavailable. Please try again later."));
		break;
	}

	Announce(Announcement, true);

	OB_MATCH_LOG("Extraction failed for squad %d (%s)", SquadId, *UEnum::GetValueAsString(Reason));

	// The attempt was the only thing holding the match open in two endings: the
	// last squad standing on the wire, and a wipe during the arrival. Neither
	// waits for somebody else to die before it is noticed.
	EvaluateMatchEndConditions();
}

void AOneBrahamGameMode::HandleLockdownStageChanged(EOneBrahamLockdownStage NewStage)
{
	UpdateObjectiveState();

	// A stage change is a match event, not just a building event: the objective
	// line, the music and the HUD banner all move together.
	OB_MATCH_LOG("Objective updated for lockdown stage %s", *UEnum::GetValueAsString(NewStage));
}

void AOneBrahamGameMode::UpdateObjectiveState()
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	// The objective line is derived, never stored: the HUD polls the same two
	// components, and this function exists so server-side logs and the Tannoy
	// agree with what the player is being told.
	FString ObjectiveText = TEXT("Reach the Skydeck");

	switch (GS->MatchPhase)
	{
	case EOneBrahamMatchPhase::Warmup:
		ObjectiveText = TEXT("Sign in at reception");
		break;

	case EOneBrahamMatchPhase::Active:
	{
		if (const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent())
		{
			if (Extraction->IsExtractionActive())
			{
				ObjectiveText = Extraction->GetHoldingSquadId() == INDEX_NONE
					? TEXT("Winch zone is open - take the Skydeck")
					: TEXT("Winch is holding - get to the pad");
				break;
			}
		}

		if (const UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
		{
			if (Lockdown->GetStage() == EOneBrahamLockdownStage::Purge)
			{
				ObjectiveText = TEXT("Security purge on lower floors - get to floor 15");
			}
			else if (Lockdown->GetStage() == EOneBrahamLockdownStage::Lockdown)
			{
				ObjectiveText = TEXT("Shutters down - the Skydeck is still open");
			}
		}
		break;
	}

	case EOneBrahamMatchPhase::Extraction:
		ObjectiveText = TEXT("Extraction in progress");
		break;

	case EOneBrahamMatchPhase::Complete:
	case EOneBrahamMatchPhase::PostMatch:
		ObjectiveText = TEXT("Match concluded");
		break;

	default:
		break;
	}

	UE_LOG(LogOneBrahamMatch, Verbose, TEXT("[OB.MATCH] Objective: %s"), *ObjectiveText);
}

void AOneBrahamGameMode::CompleteMatchWithExtraction(EOneBrahamTeam WinningTeam, const FString& WinningSquadName, int32 ExtractedCount)
{
	if (bMatchFinished)
	{
		return;
	}

	const int32 SquadId = SquadToTeamMap.IndexOfByKey(WinningTeam);
	OB_MATCH_LOG("Extraction completed for '%s' (squad %d).", *WinningSquadName, SquadId);
	FinishMatch(EOneBrahamMatchEnding::Extracted, SquadId, ExtractedCount);
}

void AOneBrahamGameMode::CompleteMatchNoSurvivors()
{
	if (bMatchFinished)
	{
		return;
	}

	FinishMatch(EOneBrahamMatchEnding::NoSurvivors, INDEX_NONE, 0);
}

FString AOneBrahamGameMode::GetWinnerName(int32 WinningSquadId) const
{
	if (WinningSquadId == INDEX_NONE)
	{
		return FString();
	}

	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS != nullptr && GS->Squads.IsValidIndex(WinningSquadId) && !GS->Squads[WinningSquadId].SquadName.IsEmpty())
	{
		return GS->Squads[WinningSquadId].SquadName.ToString();
	}

	return GetSquadDisplayName(WinningSquadId);
}

void AOneBrahamGameMode::GatherSquadStandings(TArray<FOneBrahamSquadStanding>& OutSquads) const
{
	OutSquads.Reset();
	for (int32 SquadId = 0; SquadId < SquadToTeamMap.Num(); ++SquadId)
	{
		FOneBrahamSquadStanding& Standing = OutSquads.AddDefaulted_GetRef();
		Standing.SquadId = SquadId;
	}

	// From the player states, which are the truth: the replicated squad list is
	// a copy of them, and a copy can be a frame behind.
	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	const UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent();

	for (const APlayerState* BaseState : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(BaseState);
		if (State == nullptr || !OutSquads.IsValidIndex(State->SquadId))
		{
			continue;
		}

		FOneBrahamSquadStanding& Standing = OutSquads[State->SquadId];
		++Standing.Members;
		Standing.Extracted += State->LifeState == EOneBrahamLifeState::Extracted ? 1 : 0;

		if (State->LifeState == EOneBrahamLifeState::Alive || State->LifeState == EOneBrahamLifeState::Downed)
		{
			++Standing.InFight;

			const APawn* Pawn = State->GetPawn();
			if (Lockdown != nullptr && Pawn != nullptr
				&& Lockdown->IsFloorBeingPurged(OneBrahamConstants::FloorIndexForZ(Pawn->GetActorLocation().Z)))
			{
				++Standing.OnPurgedFloor;
			}
		}
	}
}

int32 AOneBrahamGameMode::MarkSquadExtracted(int32 SquadId)
{
	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || SquadId == INDEX_NONE)
	{
		return 0;
	}

	// Who is standing goes up the wire. A downed squadmate is carried - the
	// winch takes a stretcher - because leaving them on the deck after thirty
	// seconds of holding it for them is not the ending anybody played for.
	int32 Extracted = 0;
	for (APlayerState* BaseState : GS->PlayerArray)
	{
		AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(BaseState);
		if (State == nullptr || State->SquadId != SquadId)
		{
			continue;
		}

		if (State->LifeState == EOneBrahamLifeState::Alive || State->LifeState == EOneBrahamLifeState::Downed
			|| State->LifeState == EOneBrahamLifeState::Extracted)
		{
			State->LifeState = EOneBrahamLifeState::Extracted;
			State->ForceNetUpdate();
			++Extracted;
		}
	}

	return Extracted;
}

void AOneBrahamGameMode::FreezeMatchActors()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The fight is over under the end screen. Bots stop thinking (their
	// controller's tick is their whole brain) and walking; every pawn is made
	// untouchable for longer than the end screen lasts, so a last burst cannot
	// kill somebody who has already been counted as a survivor. Players' own
	// input is the end screen's: it takes the keyboard on their machine.
	for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (Controller == nullptr)
		{
			continue;
		}

		const bool bBot = Cast<APlayerController>(Controller) == nullptr;
		if (bBot)
		{
			Controller->StopMovement();
			Controller->SetActorTickEnabled(false);
		}

		AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Controller->GetPawn());
		if (Character == nullptr)
		{
			continue;
		}

		if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			if (Health->IsAlive())
			{
				Health->GrantInvulnerability(OBResolvePostMatchSeconds(MatchSettings) + 60.f);
			}
		}

		if (bBot)
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
				Movement->DisableMovement();
			}
		}
	}
}

void AOneBrahamGameMode::FinishMatch(EOneBrahamMatchEnding Ending, int32 WinningSquadId, int32 ExtractedCount)
{
	// Once. A FIRE DRILL kill landing under the end screen used to call this
	// again on every point over the limit, and each call restarted the timer
	// back to the lobby: a busy end screen never ended.
	if (bMatchFinished)
	{
		OB_MATCH_LOG("FinishMatch(%s) ignored: the match already ended as %s.",
			*OneBrahamMatchEnd::LogNameFor(Ending), *OneBrahamMatchEnd::LogNameFor(MatchEnding));
		return;
	}

	bMatchFinished = true;
	MatchEnding = Ending;
	SetMatchPhase(EOneBrahamMatchPhase::Complete);

	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	const EOneBrahamMatchMode Mode = GetMatchMode();
	const bool bHasWinner = OneBrahamMatchEnd::HasWinner(Ending)
		|| (Ending == EOneBrahamMatchEnding::TimeOut && Mode == EOneBrahamMatchMode::FireDrill);
	const int32 Winner = bHasWinner && SquadToTeamMap.IsValidIndex(WinningSquadId) ? WinningSquadId : INDEX_NONE;
	const EOneBrahamTeam WinningTeam = Winner != INDEX_NONE ? GetTeamForSquad(Winner) : EOneBrahamTeam::None;
	const FString WinnerName = GetWinnerName(Winner);

	if (Ending == EOneBrahamMatchEnding::Extracted)
	{
		ExtractedCount = FMath::Max(ExtractedCount, MarkSquadExtracted(Winner));
	}

	// Pending respawns and the building's escalation stop with the match.
	PendingRespawns.Reset();
	if (UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
	{
		Lockdown->StopDirector();
	}
	FreezeMatchActors();
	RebuildSquadInfo();

	// Everybody lost in ASCENT unless somebody won it: the results say so
	// rather than ranking the squad that happened to be highest in the tower.
	const bool bNobodyWon = Winner == INDEX_NONE && Mode != EOneBrahamMatchMode::FireDrill;
	GS->MatchResult = BuildMatchResult(WinningTeam, WinnerName, bNobodyWon, ExtractedCount);
	GS->ForceNetUpdate();
	GS->OnMatchResultReady.Broadcast(GS->MatchResult);

	const float PostMatchSeconds = OBResolvePostMatchSeconds(MatchSettings);
	PostMatchAction = OneBrahamMatchEnd::DecidePostMatchAction(GetNetMode() == NM_DedicatedServer,
		FPackageName::DoesPackageExist(OneBrahamMatchEnd::MainMenuMapPath()));
	if (UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent())
	{
		Extraction->ServerRecordMatchEnding(Ending, Winner, GS->GetServerWorldTimeSeconds() + PostMatchSeconds, PostMatchAction);
	}

	// The tannoy gets the last word, as it should.
	Announce(OneBrahamMatchEnd::AnnouncementFor(Ending, WinnerName), true);

	TArray<FOneBrahamSquadStanding> Standings;
	GatherSquadStandings(Standings);
	FString PerSquad;
	for (const FOneBrahamSquadStanding& Standing : Standings)
	{
		PerSquad += FString::Printf(TEXT("%s%d:%d/%d in, %d out"), PerSquad.IsEmpty() ? TEXT("") : TEXT("  "),
			Standing.SquadId, Standing.InFight, Standing.Members, Standing.Extracted);
	}

	OB_MATCH_LOG("MATCH COMPLETE: ending=%s winner=%s squad=%d '%s' extracted=%d duration=%.1fs scores=[%s] squads [%s]; leaving in %.0f s",
		*OneBrahamMatchEnd::LogNameFor(Ending), *UEnum::GetValueAsString(WinningTeam), Winner, *WinnerName, ExtractedCount,
		GS->MatchResult.MatchDurationSeconds,
		*FString::JoinBy(GS->TeamScores, TEXT(","), [](int32 Score) { return FString::FromInt(Score); }),
		*PerSquad, PostMatchSeconds);

	GetWorldTimerManager().SetTimer(MatchPhaseTimerHandle, this, &AOneBrahamGameMode::HandlePostMatchTimeUp,
		PostMatchSeconds, false);
}

void AOneBrahamGameMode::HandlePostMatchTimeUp()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	SetMatchPhase(EOneBrahamMatchPhase::PostMatch);

	// What FinishMatch decided and the end screen has been counting down to.
	const EOneBrahamPostMatchAction Action = PostMatchAction;

	switch (Action)
	{
	case EOneBrahamPostMatchAction::ReturnToMenu:
	{
		// The clients first, while their connections are open to carry it: a
		// host that leaves first takes the server with it, and its clients would
		// read the match's end as a lost connection. The return-to-menu path (not
		// a bare ClientTravel to the menu map) is the one that destroys the
		// client's online session, so a Steam guest can host or join again
		// without "a session already exists"; it lands on GameDefaultMap, the menu.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PlayerController = It->Get();
			if (PlayerController != nullptr && !PlayerController->IsLocalController() && PlayerController->GetNetConnection() != nullptr)
			{
				PlayerController->ClientReturnToMainMenuWithTextReason(
					NSLOCTEXT("OneBraham", "Notice_MatchConcluded", "The match is over."));
			}
		}

		OB_MATCH_LOG("Post-match: everybody to the main menu (%s).", OneBrahamMatchEnd::MainMenuMapPath());

		FTimerHandle LeaveHandle;
		GetWorldTimerManager().SetTimer(LeaveHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
			{
				GameInstance->TravelToMainMenu();
			}
			else if (GetWorld() != nullptr)
			{
				UGameplayStatics::OpenLevel(GetWorld(), FName(OneBrahamMatchEnd::MainMenuMapPath()));
			}
		}), GetNetMode() == NM_Standalone ? 0.01f : OBHostLeaveDelaySeconds, false);
		break;
	}

	case EOneBrahamPostMatchAction::ReloadMap:
	case EOneBrahamPostMatchAction::RestartMatch:
	default:
	{
		// The same map with the same options, and everybody connected comes with
		// it. Seamless travel needs the transition map; until it is in the build
		// the travel is a hard one (the clients reconnect), which works without it.
		FString URL = TEXT("?Restart");
		if (!OBTransitionMapExists())
		{
			URL += TEXT("?NoSeamlessTravel");
		}

		OB_MATCH_LOG("Post-match: %s (%s).", Action == EOneBrahamPostMatchAction::RestartMatch
			? TEXT("the dedicated server restarts the match")
			: TEXT("no main menu map in this build - reloading the map"), *URL);

		// The bots go first, properly: a bot's player state is not carried
		// through seamless travel (GetSeamlessTravelActorList), and left in the
		// game state's player list it was a null entry by the transition map,
		// which the engine's checkpoint dereferenced - the dedicated server
		// crashed on its first restart. Destroying the controller logs it out
		// and takes its player state out of the list.
		for (AOneBrahamBotController* Bot : BotControllers)
		{
			if (!IsValid(Bot))
			{
				continue;
			}
			if (APawn* Pawn = Bot->GetPawn())
			{
				Bot->UnPossess();
				Pawn->Destroy();
			}
			Bot->Destroy();
		}
		BotControllers.Reset();
		PendingRespawns.Reset();

		World->ServerTravel(URL, /*bAbsolute*/ false);
		break;
	}
	}
}

FOneBrahamMatchResult AOneBrahamGameMode::BuildMatchResult(EOneBrahamTeam WinningTeam, const FString& WinningSquadName, bool bNoSurvivors, int32 ExtractedCount) const
{
	FOneBrahamMatchResult Result;
	Result.Mode = MatchSettings ? MatchSettings->Mode : EOneBrahamMatchMode::Ascent;
	Result.WinningTeam = WinningTeam;
	Result.WinningSquadName = FText::FromString(WinningSquadName);
	Result.bNoSurvivors = bNoSurvivors;
	Result.ExtractedCount = ExtractedCount;
	Result.MatchDurationSeconds = GetWorld() && MatchStartTime > 0.f ? GetWorld()->GetTimeSeconds() - MatchStartTime : 0.f;

	// From the player states: they carry assists, revives and damage, which the
	// squad list does not. Squad by squad, so the end screen reads in order.
	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return Result;
	}

	TArray<const AOneBrahamPlayerState*> Players;
	for (const APlayerState* BaseState : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(BaseState);
		if (State != nullptr && State->SquadId != INDEX_NONE)
		{
			Players.Add(State);
		}
	}

	Players.Sort([](const AOneBrahamPlayerState& A, const AOneBrahamPlayerState& B)
	{
		if (A.SquadId != B.SquadId) { return A.SquadId < B.SquadId; }
		if (A.Kills != B.Kills) { return A.Kills > B.Kills; }
		return A.GetPlayerName() < B.GetPlayerName();
	});

	for (const AOneBrahamPlayerState* State : Players)
	{
		Result.PlayerSummaries.Add(State->MakeSummary());
	}

	return Result;
}

EOneBrahamKillFeedVerb AOneBrahamGameMode::ChooseKillVerb(bool bKilledByBuilding, bool bSelfKill,
	bool bVictimWasDowned, EOneBrahamHitZone HitZone)
{
	if (bKilledByBuilding)
	{
		return EOneBrahamKillFeedVerb::Building;
	}

	if (bSelfKill)
	{
		return EOneBrahamKillFeedVerb::OwnUtility;
	}

	if (bVictimWasDowned)
	{
		return EOneBrahamKillFeedVerb::Executed;
	}

	if (HitZone == EOneBrahamHitZone::Head || HitZone == EOneBrahamHitZone::Neck)
	{
		return EOneBrahamKillFeedVerb::Headshot;
	}

	return EOneBrahamKillFeedVerb::Killed;
}

FName AOneBrahamGameMode::GetKillWeaponId(const AController* KillerController) const
{
	const APawn* KillerPawn = KillerController != nullptr ? KillerController->GetPawn() : nullptr;

	const UOneBrahamWeaponComponent* Weapons = KillerPawn != nullptr
		? KillerPawn->FindComponentByClass<UOneBrahamWeaponComponent>()
		: nullptr;

	const AOneBrahamWeaponBase* Weapon = Weapons != nullptr ? Weapons->GetCurrentWeapon() : nullptr;

	return (Weapon != nullptr && Weapon->Definition != nullptr) ? Weapon->Definition->WeaponId : NAME_None;
}

FOneBrahamKillFeedEntry AOneBrahamGameMode::RegisterKill(AActor* Victim, AController* KillerController,
	const FOneBrahamDamageReport& Report)
{
	FOneBrahamKillFeedEntry Entry;

	AOneBrahamGameState* GS = GetOBGameState();

	if (GS == nullptr)
	{
		return Entry;
	}

	const APawn* VictimPawn = Cast<APawn>(Victim);
	AOneBrahamPlayerState* VictimState = VictimPawn != nullptr
		? VictimPawn->GetPlayerState<AOneBrahamPlayerState>()
		: nullptr;

	// Remembered for spawn selection: where the death was, and who did it.
	if (Victim != nullptr && GetWorld() != nullptr)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		RecentDeaths.RemoveAll([Now](const FRecentDeath& Death)
		{
			return Now - Death.Time > OneBrahamSpawnRules::RecentDeathSeconds;
		});
		RecentDeaths.Add({ Victim->GetActorLocation(), Now });
	}
	if (VictimPawn != nullptr && VictimPawn->GetController() != nullptr && KillerController != nullptr
		&& KillerController != VictimPawn->GetController())
	{
		LastKillerOf.Add(VictimPawn->GetController(), KillerController);
	}
	AOneBrahamPlayerState* KillerState = KillerController != nullptr
		? Cast<AOneBrahamPlayerState>(KillerController->PlayerState)
		: nullptr;

	Entry.VictimName = VictimState != nullptr
		? VictimState->GetPlayerName()
		: (Victim != nullptr ? Victim->GetName() : FString(TEXT("Someone")));
	Entry.VictimTeam = VictimState != nullptr ? VictimState->Team : EOneBrahamTeam::None;
	Entry.KillerName = KillerState != nullptr ? KillerState->GetPlayerName() : FString();
	Entry.KillerTeam = KillerState != nullptr ? KillerState->Team : EOneBrahamTeam::None;

	// The building cannot be self-killed and a self-kill is not a kill by anyone
	// else, so both facts are decided here and handed to the verb selector, whose
	// branch order is the order of the claims.
	const bool bKilledByBuilding = KillerController == nullptr || KillerState == nullptr;
	const bool bSelfKill = !bKilledByBuilding
		&& VictimPawn != nullptr && VictimPawn->GetController() == KillerController;

	const AOneBrahamCharacter* VictimCharacter = Cast<AOneBrahamCharacter>(VictimPawn);
	const UOneBrahamDownedComponent* Downed = VictimCharacter != nullptr
		? VictimCharacter->GetDownedComponent()
		: nullptr;

	Entry.bWasDowned = Downed != nullptr && Downed->IsDowned();
	Entry.Verb = ChooseKillVerb(bKilledByBuilding, bSelfKill, Entry.bWasDowned, Report.HitZone);

	Entry.WeaponId = GetKillWeaponId(KillerController);

	// Assists. The health component has been watching who took what off this
	// player since it was written - everyone who has accounted for at least 20%
	// of their maximum health, inside a window that closes five seconds after the
	// last hit they took - and this is the half that pays them. Without it a
	// squad's kill is one player's kill and two players' empty magazines, which
	// is how a squad stops shooting together.
	TArray<AActor*> Attackers;
	if (const UOneBrahamHealthComponent* VictimHealth = VictimCharacter != nullptr
		? VictimCharacter->GetHealthComponent()
		: nullptr)
	{
		VictimHealth->GetRecentAttackers(Attackers);
	}

	int32 AssistCount = 0;
	for (AActor* Attacker : Attackers)
	{
		// The tracker holds whoever the damage was attributed to. That is the
		// character in every normal case; a projectile whose thrower has since
		// died arrives here as the projectile, which is why the instigator is the
		// fallback rather than the only path.
		APawn* AttackerPawn = Cast<APawn>(Attacker);
		if (AttackerPawn == nullptr && Attacker != nullptr)
		{
			AttackerPawn = Attacker->GetInstigator();
		}

		AController* AttackerController = AttackerPawn != nullptr ? AttackerPawn->GetController() : nullptr;
		if (AttackerController == nullptr || AttackerController == KillerController)
		{
			// The killer is not their own assist, and the building has no
			// player state to credit.
			continue;
		}

		AOneBrahamPlayerState* AttackerState = AttackerController->GetPlayerState<AOneBrahamPlayerState>();
		if (AttackerState == nullptr || AttackerState == VictimState)
		{
			continue;
		}

		AttackerState->ServerRegisterAssist();
		++AssistCount;
	}

	// The feed says so when it was a squad's kill: the HUD draws "assisted"
	// against the killer's name, and in FIRE DRILL it is why two players who both
	// emptied a magazine get something for it.
	Entry.bAssisted = AssistCount > 0;
	Entry.Timestamp = GS->GetServerWorldTimeSeconds();

	GS->AddKillFeedEntry(Entry);

	// Credit. Personal score always - the scoreboard is the player's own record
	// of the match - and team score only in the mode that wins on points, because
	// in ASCENT the win condition is the winch and a kill count is not a score.
	// Nothing counts once the match is over: a burst under the end screen is not
	// a kill on anybody's record, and in FIRE DRILL it would be a point.
	if (KillerState != nullptr && !bSelfKill && !bKilledByBuilding && !bMatchFinished)
	{
		const int32 ScorePerKill = MatchSettings != nullptr ? MatchSettings->ScorePerKill : 1;

		KillerState->ServerRegisterKill(ScorePerKill);

		if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
		{
			AddTeamScoreByIndex(KillerState->Team == EOneBrahamTeam::InternalStaff ? 0 : 1, ScorePerKill);
		}
	}

	OB_MATCH_LOG("Kill feed: '%s' %s '%s' (%s, floor %d).",
		Entry.KillerName.IsEmpty() ? TEXT("the building") : *Entry.KillerName,
		*UEnum::GetValueAsString(Entry.Verb),
		*Entry.VictimName,
		Entry.WeaponId.IsNone() ? TEXT("no weapon") : *Entry.WeaponId.ToString(),
		VictimCharacter != nullptr ? VictimCharacter->GetCurrentFloorIndex() : -1);

	return Entry;
}

void AOneBrahamGameMode::AddTeamScoreByIndex(int32 TeamIndex, int32 Amount)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || !GS->TeamScores.IsValidIndex(TeamIndex) || bMatchFinished)
	{
		return;
	}

	GS->TeamScores[TeamIndex] += Amount;

	const int32 ScoreToWin = OBResolveScoreToWin(MatchSettings);
	if (GS->TeamScores[TeamIndex] >= ScoreToWin)
	{
		OB_MATCH_LOG("Team %d reached %d of %d points.", TeamIndex, GS->TeamScores[TeamIndex], ScoreToWin);
		FinishMatch(EOneBrahamMatchEnding::ScoreLimit, TeamIndex, 0);
	}
}

bool AOneBrahamGameMode::HasMatchTimeLimitExpired(float TimeLimitSeconds, float ElapsedSeconds, EOneBrahamMatchPhase Phase)
{
	if (TimeLimitSeconds <= 0.f)
	{
		return false;
	}

	if (Phase != EOneBrahamMatchPhase::Active && Phase != EOneBrahamMatchPhase::Extraction)
	{
		return false;
	}

	return ElapsedSeconds >= TimeLimitSeconds;
}

FOneBrahamTimeOutOutcome AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode Mode, const TArray<int32>& TeamScores)
{
	FOneBrahamTimeOutOutcome Outcome;

	// ASCENT (and anything else without points): the hard limit is the "Time
	// out" ending. Nobody extracts and the results screen says everybody lost
	// rather than ranking the squad that happened to be highest in the tower.
	if (Mode != EOneBrahamMatchMode::FireDrill)
	{
		Outcome.bNoSurvivors = true;
		return Outcome;
	}

	// FIRE DRILL: most points when the drill ends. Level at the top is a draw,
	// whatever the number of teams.
	int32 BestIndex = INDEX_NONE;
	bool bLevel = false;
	for (int32 Index = 0; Index < TeamScores.Num(); ++Index)
	{
		if (BestIndex == INDEX_NONE || TeamScores[Index] > TeamScores[BestIndex])
		{
			BestIndex = Index;
			bLevel = false;
		}
		else if (TeamScores[Index] == TeamScores[BestIndex])
		{
			bLevel = true;
		}
	}

	if (BestIndex == INDEX_NONE || bLevel)
	{
		Outcome.bDraw = true;
		Outcome.WinningSquadName = TEXT("Draw");
		return Outcome;
	}

	Outcome.WinningTeam = BestIndex == 0 ? EOneBrahamTeam::InternalStaff : EOneBrahamTeam::ExternalContractors;
	Outcome.WinningSquadName = BestIndex == 0 ? TEXT("Internal Staff") : TEXT("External Contractors");
	return Outcome;
}

void AOneBrahamGameMode::HandleMatchTimeLimitReached()
{
	if (bMatchFinished)
	{
		return;
	}

	const AOneBrahamGameState* GS = GetOBGameState();
	const EOneBrahamMatchMode Mode = GetMatchMode();
	const FOneBrahamTimeOutOutcome Outcome = DecideTimeOutOutcome(Mode, GS != nullptr ? GS->TeamScores : TArray<int32>());

	OB_MATCH_LOG("Time limit reached (%.0fs) in %s: winner=%s draw=%d nobodyOut=%d",
		GS != nullptr ? GS->MatchTimeLimitSeconds : 0.f, *UEnum::GetValueAsString(Mode),
		*UEnum::GetValueAsString(Outcome.WinningTeam), Outcome.bDraw ? 1 : 0, Outcome.bNoSurvivors ? 1 : 0);

	// FIRE DRILL's teams are its squads 0 and 1 (AddTeamScoreByIndex).
	const int32 WinningSquad = Outcome.WinningTeam == EOneBrahamTeam::None ? INDEX_NONE : SquadToTeamMap.IndexOfByKey(Outcome.WinningTeam);
	FinishMatch(OneBrahamMatchEnd::EndingForTimeOut(Mode, Outcome.bDraw), WinningSquad, 0);
}

void AOneBrahamGameMode::EvaluateMatchEndConditions()
{
	if (bMatchFinished || MatchSettings == nullptr)
	{
		return;
	}

	// FIRE DRILL respawns and ends on points or the clock; only ASCENT ends by
	// elimination. Only a match in progress ends: in the warmup everyone is
	// parked as a spectator, so a player leaving the lobby read as a wipe.
	const EOneBrahamMatchPhase Phase = GetMatchPhase();
	if (MatchSettings->Mode != EOneBrahamMatchMode::Ascent
		|| (Phase != EOneBrahamMatchPhase::Active && Phase != EOneBrahamMatchPhase::Extraction))
	{
		return;
	}

	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	// The squad list first: the HUD reads it, and so does the winch.
	RebuildSquadInfo();

	TArray<FOneBrahamSquadStanding> Standings;
	GatherSquadStandings(Standings);

	// The match is for its humans. With bots filling the squads, a solo player
	// who died used to watch the bots until the purge or the clock. (A player
	// who leaves is still in the array while Logout evaluates, so leaving does
	// not end the match for the others.)
	int32 HumansInSquads = 0;
	int32 HumansInFight = 0;
	int32 BotsInFight = 0;
	CountInFight(HumansInSquads, HumansInFight, BotsInFight);

	const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent();
	const int32 ExtractionSquad = Extraction != nullptr && Extraction->IsExtractionActive()
		? Extraction->GetClaimingSquadId() : INDEX_NONE;

	const FOneBrahamEndVerdict Verdict = OneBrahamMatchEnd::DecideEliminationEnding(MatchSettings->Mode, Standings,
		ExtractionSquad, HumansInSquads, HumansInFight, BotsInFight);

	if (!Verdict.bEnds)
	{
		return;
	}

	OB_MATCH_LOG("Elimination: %s (squad %d; %d human(s) in squads, %d in the fight, %d bot(s) in the fight, winch squad %d).",
		*OneBrahamMatchEnd::LogNameFor(Verdict.Ending), Verdict.WinningSquadId, HumansInSquads, HumansInFight,
		BotsInFight, ExtractionSquad);

	FinishMatch(Verdict.Ending, Verdict.WinningSquadId, 0);
}

void AOneBrahamGameMode::DebugForceMatchResult(int32 WinningTeamIndex)
{
	if (WinningTeamIndex < 0)
	{
		CompleteMatchNoSurvivors();
		return;
	}

	// QA: FIRE DRILL's win is points, ASCENT's the winch.
	FinishMatch(GetMatchMode() == EOneBrahamMatchMode::FireDrill ? EOneBrahamMatchEnding::ScoreLimit : EOneBrahamMatchEnding::Extracted,
		WinningTeamIndex, 0);
}

// ==========================================================================
//  Squads
// ==========================================================================
int32 AOneBrahamGameMode::GetSquadCount() const
{
	return SquadToTeamMap.Num();
}

EOneBrahamTeam AOneBrahamGameMode::GetTeamForSquad(int32 SquadId) const
{
	return SquadToTeamMap.IsValidIndex(SquadId) ? SquadToTeamMap[SquadId] : EOneBrahamTeam::None;
}

void AOneBrahamGameMode::AssignPlayerToSquad(AOneBrahamPlayerState* PlayerState)
{
	if (PlayerState == nullptr || MatchSettings == nullptr)
	{
		return;
	}

	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	// The replicated squad list is built from the squad plan InitGame made, but
	// only ever rebuilt after an assignment - so the first player searched an
	// empty list, found "all 0 squads full" and joined as a spectator, and so
	// did everyone after them. Build it before searching it.
	if (GS->Squads.Num() != SquadToTeamMap.Num())
	{
		RebuildSquadInfo();
	}

	// Find the squad with the fewest members that is not full. Squads fill in
	// order, which keeps premade parties together when they join together.
	int32 BestSquad = INDEX_NONE;
	int32 FewestMembers = MAX_int32;
	for (const FOneBrahamSquadInfo& Squad : GS->Squads)
	{
		if (Squad.Members.Num() < MatchSettings->PlayersPerTeam && Squad.Members.Num() < FewestMembers)
		{
			FewestMembers = Squad.Members.Num();
			BestSquad = Squad.SquadId;
		}
	}

	if (BestSquad == INDEX_NONE)
	{
		// Everyone is full. In FIRE DRILL the player still gets a team (the
		// roster rotates); in ASCENT they become a spectator.
		if (MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
		{
			BestSquad = NextSquadId % FMath::Max(1, SquadToTeamMap.Num());
		}
		else
		{
			PlayerState->Team = EOneBrahamTeam::Spectator;
			PlayerState->SquadId = INDEX_NONE;
			OB_MATCH_LOG("'%s' joined as a spectator: all %d squads are full.",
				*PlayerState->GetPlayerName(), GS->Squads.Num());
			return;
		}
	}

	PlayerState->SquadId = BestSquad;
	PlayerState->Team = GetTeamForSquad(BestSquad);
	PlayerState->bIsSquadLeader = (FewestMembers == 0);

	// The first member of a squad is the leader; if the leader leaves, promote
	// the next surviving member (handled in Logout).
	if (PlayerState->bIsSquadLeader)
	{
		NextSquadId = (BestSquad + 1) % FMath::Max(1, SquadToTeamMap.Num());
	}

	RebuildSquadInfo();

	OB_MATCH_LOG("'%s' assigned to squad %d (%s), leader=%s",
		*PlayerState->GetPlayerName(), BestSquad, *UEnum::GetValueAsString(PlayerState->Team),
		PlayerState->bIsSquadLeader ? TEXT("yes") : TEXT("no"));
}

void AOneBrahamGameMode::RebuildSquadInfo()
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || MatchSettings == nullptr)
	{
		return;
	}

	TArray<FOneBrahamSquadInfo> NewSquads;

	for (int32 SquadId = 0; SquadId < SquadToTeamMap.Num(); ++SquadId)
	{
		FOneBrahamSquadInfo Squad;
		Squad.SquadId = SquadId;
		Squad.Team = SquadToTeamMap[SquadId];
		Squad.SquadName = FText::FromString(GetSquadDisplayName(SquadId));
		NewSquads.Add(Squad);
	}

	for (APlayerState* BaseState : GS->PlayerArray)
	{
		AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(BaseState);
		if (State == nullptr || State->SquadId == INDEX_NONE || !NewSquads.IsValidIndex(State->SquadId))
		{
			continue;
		}

		FOneBrahamSquadMemberInfo Member;
		Member.PlayerId = State->GetPlayerId();
		Member.PlayerName = State->GetPlayerName();
		Member.ArchetypeTag = State->ArchetypeTag;
		Member.LifeState = State->LifeState;
		Member.Health = State->Health;
		Member.MaxHealth = State->MaxHealth;
		Member.Kills = State->Kills;
		Member.Downs = State->Downs;
		Member.Deaths = State->Deaths;
		Member.bIsSquadLeader = State->bIsSquadLeader;
		NewSquads[State->SquadId].Members.Add(Member);
	}

	// Elimination is recomputed every rebuild so the HUD and end conditions agree.
	for (FOneBrahamSquadInfo& Squad : NewSquads)
	{
		Squad.bEliminated = (Squad.Members.Num() > 0) && !Squad.IsAlive();

		// Preserve the active extraction claim across rebuilds.
		if (GS->GetExtractionComponent() != nullptr)
		{
			Squad.bHasExtractionClaim = GS->GetExtractionComponent()->IsSquadHoldingExtraction(Squad.SquadId);
		}
	}

	GS->Squads = NewSquads;
}

FString AOneBrahamGameMode::GetSquadDisplayName(int32 SquadId) const
{
	// Dry, corporate squad names. These are what the killfeed and scoreboard show.
	static const TCHAR* AscentSquadNames[] =
	{
		TEXT("Braham Compliance"),
		TEXT("Floor 9 Ops"),
		TEXT("External Contractors")
	};
	static const TCHAR* FireDrillNames[] =
	{
		TEXT("Internal Staff"),
		TEXT("External Contractors")
	};

	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
	{
		return FireDrillNames[FMath::Clamp(SquadId, 0, 1)];
	}

	if (SquadId >= 0 && SquadId < UE_ARRAY_COUNT(AscentSquadNames))
	{
		return AscentSquadNames[SquadId];
	}

	return FString::Printf(TEXT("Squad %d"), SquadId + 1);
}

void AOneBrahamGameMode::SetSquadExtractionClaim(int32 SquadId, bool bClaimed)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || !GS->Squads.IsValidIndex(SquadId))
	{
		return;
	}

	GS->Squads[SquadId].bHasExtractionClaim = bClaimed;
}

// ==========================================================================
//  Spawning
// ==========================================================================
void AOneBrahamGameMode::RebuildSpawnPointRegistry()
{
	SpawnPoints.Reset();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<AOneBrahamSpawnPoint> It(World); It; ++It)
	{
		if (AOneBrahamSpawnPoint* Spawn = *It)
		{
			SpawnPoints.Add(Spawn);
		}
	}

	// Stable order: floor, then cluster, then name. Deterministic spawn selection
	// makes multiplayer reproduction of bugs possible, which matters more than
	// the minor variety loss.
	SpawnPoints.Sort([](const AOneBrahamSpawnPoint& A, const AOneBrahamSpawnPoint& B)
	{
		if (A.FloorIndex != B.FloorIndex) { return A.FloorIndex < B.FloorIndex; }
		if (A.SquadClusterId != B.SquadClusterId) { return A.SquadClusterId < B.SquadClusterId; }
		return A.GetName() < B.GetName();
	});

	OB_MATCH_LOG("Spawn point registry rebuilt: %d points across %s floors.",
		SpawnPoints.Num(),
		SpawnPoints.Num() > 0 ? *FString::Printf(TEXT("%d"), SpawnPoints.Last()->FloorIndex + 1) : TEXT("0"));

	if (SpawnPoints.Num() == 0)
	{
		OB_ERROR("No AOneBrahamSpawnPoint actors in the level. Players will fall back to the "
			"default PlayerStart; place spawn points using Tools/Python/build_map.py.");
	}
}

AOneBrahamSpawnPoint* AOneBrahamGameMode::SelectSpawnPointFor(AController* Controller, int32 FloorIndex) const
{
	TArray<AOneBrahamSpawnPoint*> Ranked;
	RankSpawnPointsFor(Controller, FloorIndex, Ranked);
	return Ranked.Num() > 0 ? Ranked[0] : nullptr;
}

void AOneBrahamGameMode::RankSpawnPointsFor(AController* Controller, int32 FloorIndex, TArray<AOneBrahamSpawnPoint*>& OutRanked) const
{
	OutRanked.Reset();

	const UWorld* World = GetWorld();
	if (World == nullptr || SpawnPoints.Num() == 0)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const AOneBrahamPlayerState* RequestingState = Controller ? Controller->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	const EOneBrahamTeam RequestingTeam = RequestingState ? RequestingState->Team : EOneBrahamTeam::None;
	const int32 RequestingSquad = RequestingState ? RequestingState->SquadId : INDEX_NONE;

	// --- Gather candidates ------------------------------------------------
	// Each passing point gets a key FRand^(1/weight); sorting by it is a
	// weighted random order (Efraimidis-Spirakis), so the first point is the
	// same weighted pick this always made and the rest are its runners-up.
	struct FKeyed
	{
		AOneBrahamSpawnPoint* Spawn = nullptr;
		float Key = 0.f;
	};
	TArray<FKeyed> Candidates;
	TArray<FKeyed> Vetoed;
	TArray<FKeyed> Resting;

	for (AOneBrahamSpawnPoint* Spawn : SpawnPoints)
	{
		if (Spawn == nullptr || !Spawn->bEnabled)
		{
			continue;
		}

		// Explicit team affinity wins over everything else.
		if (Spawn->TeamAffinity != EOneBrahamTeam::None && Spawn->TeamAffinity != RequestingTeam)
		{
			continue;
		}

		// Floor lock: once the extraction is live we want late spawns on the
		// upper floors rather than a 15-floor climb in the last 40 seconds.
		if (FloorIndex != INDEX_NONE && Spawn->FloorIndex != FloorIndex)
		{
			continue;
		}

		// Resting after use: a last resort, the longest-rested first.
		if (!Spawn->IsAvailable(Now))
		{
			Resting.Add({ Spawn, -Spawn->LastUsedTime });
			continue;
		}

		float Weight = FMath::Max(0.f, Spawn->Weight);

		// --- Threat ------------------------------------------------------
		// See OneBrahamSpawnRules.h. Only enemies within a floor of the spawn
		// count (a slab stops rounds); any of them that can see the spawn makes
		// it a free kill; a fresh death next to it means someone is camping it;
		// and a respawn never goes where the player's own killer can reach them.
		OneBrahamSpawnRules::FThreat Threat;
		const FVector SpawnHead = Spawn->GetActorLocation() + FVector(0.f, 0.f, 90.f);
		const AController* LastKiller = nullptr;
		if (const TWeakObjectPtr<AController>* Found = LastKillerOf.Find(const_cast<AController*>(Controller)))
		{
			LastKiller = Found->Get();
		}

		// Every controller, bots included: a bot squad on the plaza is as much a
		// spawn threat as a human one.
		for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
		{
			const AController* OtherPC = It->Get();
			if (OtherPC == nullptr || OtherPC == Controller)
			{
				continue;
			}

			const AOneBrahamPlayerState* OtherState = OtherPC->GetPlayerState<AOneBrahamPlayerState>();
			if (OtherState == nullptr || OtherState->IsEliminated())
			{
				continue;
			}

			const AOneBrahamCharacter* OtherCharacter = Cast<AOneBrahamCharacter>(OtherPC->GetPawn());
			if (OtherCharacter == nullptr)
			{
				continue;
			}

			const bool bEnemy = !OneBrahamTags::AreTeamsFriendly(RequestingTeam, OtherState->Team,
				MatchSettings ? MatchSettings->Mode : EOneBrahamMatchMode::Ascent);
			const float DistCm = FVector::Dist(Spawn->GetActorLocation(), OtherCharacter->GetActorLocation());

			if (!bEnemy)
			{
				// Teammates nearby are a *bonus*: spawning next to your squad is
				// exactly what ASCENT wants.
				if (DistCm < 1500.f)
				{
					Weight *= (OtherState->SquadId == RequestingSquad) ? 2.2f : 1.4f;
				}
				continue;
			}

			if (!OneBrahamSpawnRules::IsInFloorBand(Spawn->FloorIndex, OtherCharacter->GetCurrentFloorIndex()))
			{
				continue;
			}

			Threat.NearestEnemyCm = FMath::Min(Threat.NearestEnemyCm, DistCm);

			bool bHasSight = false;
			if (DistCm < OneBrahamSpawnRules::SightCheckCm)
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(OneBrahamSpawnSight), false, OtherCharacter);
				Params.AddIgnoredActor(Spawn);
				bHasSight = !World->LineTraceTestByChannel(OtherCharacter->GetEyeLocation(), SpawnHead,
					ECC_Visibility, Params);
				if (bHasSight)
				{
					Threat.NearestSightingEnemyCm = FMath::Min(Threat.NearestSightingEnemyCm, DistCm);
				}
			}

			if (LastKiller != nullptr && OtherPC == LastKiller)
			{
				Threat.KillerCm = DistCm;
				Threat.bKillerHasSight = bHasSight;
			}
		}

		for (const FRecentDeath& Death : RecentDeaths)
		{
			if (Now - Death.Time <= OneBrahamSpawnRules::RecentDeathSeconds)
			{
				Threat.NearestRecentDeathCm = FMath::Min(Threat.NearestRecentDeathCm,
					static_cast<float>(FVector::Dist(Spawn->GetActorLocation(), Death.Location)));
			}
		}

		const float ThreatMultiplier = OneBrahamSpawnRules::ThreatMultiplier(Threat);
		if (ThreatMultiplier <= 0.f)
		{
			// Vetoed, but remembered: if every spawn is vetoed, the safest of
			// them is still better than none.
			Vetoed.Add({ Spawn, OneBrahamSpawnRules::FallbackSafety(Threat) });
			continue;
		}
		Weight *= ThreatMultiplier;

		// --- Same-cluster preference -------------------------------------
		if (RequestingSquad != INDEX_NONE && Spawn->SquadClusterId == RequestingSquad)
		{
			Weight *= 1.8f;
		}

		if (Weight <= 0.f)
		{
			continue;
		}

		Candidates.Add({ Spawn, FMath::Pow(FMath::Max(FMath::FRand(), UE_KINDA_SMALL_NUMBER), 1.f / Weight) });
	}

	// --- Order -----------------------------------------------------------
	// Passing points first. If every spawn is threatened, the safest of the
	// vetoed ones rather than a random one - a random pick is how a camped
	// spawn gets chosen. A point still resting beats no spawn at all.
	auto ByKey = [](const FKeyed& A, const FKeyed& B) { return A.Key > B.Key; };
	Candidates.StableSort(ByKey);
	Vetoed.StableSort(ByKey);
	Resting.StableSort(ByKey);

	OutRanked.Reserve(Candidates.Num() + Vetoed.Num() + Resting.Num());
	for (const TArray<FKeyed>* Tier : { &Candidates, &Vetoed, &Resting })
	{
		for (const FKeyed& Keyed : *Tier)
		{
			OutRanked.Add(Keyed.Spawn);
		}
	}
}

bool AOneBrahamGameMode::IsPawnDeadForRespawn(const APawn* Pawn)
{
	if (Pawn == nullptr)
	{
		return true;
	}

	if (const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Pawn))
	{
		if (const UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			if (!Health->IsAlive())
			{
				return true;
			}
		}
	}

	const AOneBrahamPlayerState* State = Pawn->GetPlayerState<AOneBrahamPlayerState>();
	return State != nullptr && State->IsEliminated();
}

void AOneBrahamGameMode::QueueRespawn(AController* Controller, float DelaySeconds)
{
	if (Controller == nullptr || bMatchFinished)
	{
		return;
	}

	PendingRespawns.Add(Controller, FMath::Max(0.1f, DelaySeconds));
}

bool AOneBrahamGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	if (Player == nullptr || bMatchFinished)
	{
		return false;
	}

	// Withdrawn, or a full lobby's spectator: they watch.
	if (const AOneBrahamPlayerState* Watching = Player->GetPlayerState<AOneBrahamPlayerState>())
	{
		if (Watching->Team == EOneBrahamTeam::Spectator)
		{
			return false;
		}
	}

	// ASCENT is one life: a dead player stays dead. FIRE DRILL respawns.
	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::Ascent && !MatchSettings->bRespawnEnabled)
	{
		const AOneBrahamPlayerState* State = Player->GetPlayerState<AOneBrahamPlayerState>();
		return State == nullptr || State->LifeState == EOneBrahamLifeState::Alive;
	}

	return Super::PlayerCanRestart_Implementation(Player);
}

AActor* AOneBrahamGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// The same list RestartPlayer walks; its head is the one choice the engine
	// asks for. Prefer our own spawn points; only fall back to a legacy
	// APlayerStart if a level has not been authored yet (early greyblock,
	// tutorial map).
	TArray<AOneBrahamSpawnPoint*> Points;
	TArray<bool> Clear;
	GatherSpawnAttemptsFor(Player, Points, Clear);
	if (Points.Num() > 0)
	{
		return Points[0];
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

int32 AOneBrahamGameMode::GetAscentEntryFloor() const
{
	TArray<int32> Floors;
	for (const AOneBrahamSpawnPoint* Spawn : SpawnPoints)
	{
		if (Spawn != nullptr && Spawn->bEnabled)
		{
			Floors.AddUnique(Spawn->FloorIndex);
		}
	}
	Floors.Sort();

	const AOneBrahamGameState* GS = GetOBGameState();
	const UOneBrahamLockdownComponent* Lockdown = GS != nullptr ? GS->GetLockdownComponent() : nullptr;
	return OneBrahamSpawnRules::LateJoinFloor(Floors, [Lockdown](int32 Floor)
	{
		return Lockdown != nullptr && (Lockdown->IsFloorBeingPurged(Floor) || Lockdown->IsFloorSealed(Floor));
	});
}

void AOneBrahamGameMode::GatherSpawnAttemptsFor(AController* Controller, TArray<AOneBrahamSpawnPoint*>& OutPoints, TArray<bool>& OutClear) const
{
	OutPoints.Reset();
	OutClear.Reset();

	// Preferred[0, PrimaryCount) are on the mode's own floors; the rest are
	// fallbacks (OneBrahamSpawnRules::FSpawnAttempt::bFallback).
	TArray<AOneBrahamSpawnPoint*> Preferred;
	int32 PrimaryCount = INDEX_NONE;
	TSet<AOneBrahamSpawnPoint*> SquadPoints;
	TArray<AOneBrahamSpawnPoint*> Ranked;
	auto AppendRanked = [this, Controller, &Preferred, &Ranked](int32 Floor)
	{
		RankSpawnPointsFor(Controller, Floor, Ranked);
		for (AOneBrahamSpawnPoint* Spawn : Ranked)
		{
			Preferred.AddUnique(Spawn);
		}
	};

	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::Ascent)
	{
		// ASCENT is the climb: everyone walks in off the plaza (Docs/09, floor 0
		// is the spawn; Docs/06, "the lobby start"), and so does a late joiner
		// while the plaza is open; after that, the lowest floor the lockdown has
		// not closed (GetAscentEntryFloor, OneBrahamSpawnRules::LateJoinFloor).
		const int32 EntryFloor = GetAscentEntryFloor();

		// A squad walks in together from its own corner of the plaza: the
		// cluster is the squad (Docs/06, "round-robins players onto clusters").
		// The opening wave puts four people on one point at the same instant,
		// so the point's rest timer is not a reason to send three of them to
		// another squad's corner - which is what the weighted pick did, and the
		// first seconds of every full match were a fight on one spawn point.
		// The later arrivals stand beside the point (SpawnPawnAtOrBeside).
		const AOneBrahamPlayerState* State = Controller != nullptr ? Controller->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
		if (EntryFloor == OneBrahamConstants::FloorPlaza && State != nullptr && State->SquadId != INDEX_NONE)
		{
			TArray<AOneBrahamSpawnPoint*> Plaza;
			for (AOneBrahamSpawnPoint* Spawn : SpawnPoints)
			{
				if (Spawn != nullptr && Spawn->bEnabled && Spawn->FloorIndex == OneBrahamConstants::FloorPlaza)
				{
					Plaza.Add(Spawn);
				}
			}

			for (AOneBrahamSpawnPoint* Spawn : Plaza)
			{
				if (Spawn->SquadClusterId == State->SquadId % FMath::Max(1, Plaza.Num()))
				{
					Preferred.Add(Spawn);
					SquadPoints.Add(Spawn);
				}
			}
		}

		if (EntryFloor != INDEX_NONE)
		{
			AppendRanked(EntryFloor);
		}
		PrimaryCount = Preferred.Num();

		// Blocked on the way in: the next floor up that is still open, never
		// one behind a shutter or in the gas.
		const AOneBrahamGameState* GS = GetOBGameState();
		const UOneBrahamLockdownComponent* Lockdown = GS != nullptr ? GS->GetLockdownComponent() : nullptr;
		TArray<int32> Above;
		for (const AOneBrahamSpawnPoint* Spawn : SpawnPoints)
		{
			if (Spawn != nullptr && EntryFloor != INDEX_NONE && Spawn->FloorIndex > EntryFloor
				&& (Lockdown == nullptr || (!Lockdown->IsFloorBeingPurged(Spawn->FloorIndex) && !Lockdown->IsFloorSealed(Spawn->FloorIndex))))
			{
				Above.AddUnique(Spawn->FloorIndex);
			}
		}
		Above.Sort();
		for (const int32 Floor : Above)
		{
			if (Preferred.Num() >= MaxSpawnAttemptsPerRestart)
			{
				break;
			}
			AppendRanked(Floor);
		}
	}
	else if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
	{
		// FIRE DRILL is a fight over three control points, so it is fought on
		// their floors: a spawn anywhere in the tower put the twelve players on
		// twelve floors the navmesh does not join, and a match could run its ten
		// minutes without two enemies meeting (bots or people). Each spawn goes
		// to one of the point floors under the usual safety rules, ranked across
		// all three at once: ranking floor by floor in a random order put a
		// point in an enemy's sight (vetoed) or one used a second ago (resting)
		// on the first floor ahead of a safe, rested one on the next. The rest
		// of the tower is the last resort.
		TArray<int32> PointFloors;
		for (const FOneBrahamControlPointDef& Point : MatchSettings->ControlPoints)
		{
			if (Point.FloorIndex >= 0)
			{
				PointFloors.AddUnique(Point.FloorIndex);
			}
		}

		RankSpawnPointsFor(Controller, INDEX_NONE, Ranked);
		for (AOneBrahamSpawnPoint* Spawn : Ranked)
		{
			if (PointFloors.Num() == 0 || PointFloors.Contains(Spawn->FloorIndex))
			{
				Preferred.Add(Spawn);
			}
		}
		PrimaryCount = Preferred.Num();
		for (AOneBrahamSpawnPoint* Spawn : Ranked)
		{
			Preferred.AddUnique(Spawn);
		}
	}
	else
	{
		AppendRanked(INDEX_NONE);
	}

	if (Preferred.Num() == 0)
	{
		return;
	}

	// Who is standing where, and what has already failed this controller.
	float Radius = 0.f;
	float HalfHeight = 0.f;
	GetPawnCapsuleFor(Controller, Radius, HalfHeight);
	const APawn* OwnPawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	const TArray<TWeakObjectPtr<AOneBrahamSpawnPoint>>* Failed = FailedSpawnsOf.Find(Controller);

	TArray<OneBrahamSpawnRules::FSpawnAttempt> Attempts;
	Attempts.Reserve(Preferred.Num());
	for (AOneBrahamSpawnPoint* Spawn : Preferred)
	{
		OneBrahamSpawnRules::FSpawnAttempt& Attempt = Attempts.AddDefaulted_GetRef();
		Attempt.bClearNow = Spawn->IsClearForPawn(Radius, HalfHeight, OwnPawn);
		Attempt.bSquadPoint = SquadPoints.Contains(Spawn);
		Attempt.FailedSerial = Failed != nullptr ? Failed->IndexOfByKey(Spawn) : INDEX_NONE;
		Attempt.bFallback = PrimaryCount != INDEX_NONE && Attempts.Num() - 1 >= PrimaryCount;
	}

	TArray<int32> Order;
	OneBrahamSpawnRules::OrderSpawnAttempts(Attempts, Order);
	for (const int32 Index : Order)
	{
		OutPoints.Add(Preferred[Index]);
		OutClear.Add(Attempts[Index].bClearNow);
	}
}

void AOneBrahamGameMode::GetPawnCapsuleFor(AController* Controller, float& OutRadius, float& OutHalfHeight) const
{
	// The engine's character capsule, unless the pawn class says otherwise.
	OutRadius = 34.f;
	OutHalfHeight = 88.f;

	const UClass* PawnClass = const_cast<AOneBrahamGameMode*>(this)->GetDefaultPawnClassForController(Controller);
	const ACharacter* Character = PawnClass != nullptr ? Cast<ACharacter>(PawnClass->GetDefaultObject()) : nullptr;
	if (Character != nullptr && Character->GetCapsuleComponent() != nullptr)
	{
		OutRadius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		OutHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
}

APawn* AOneBrahamGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	// The engine spawns with the pawn class's own collision handling. Nudging a
	// capsule out of a doorframe is fine; spawning one inside another player
	// (AlwaysSpawn) or a wall is not, so a blocked spot fails and the caller
	// moves on to the next point.
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	return PawnClass != nullptr ? GetWorld()->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo) : nullptr;
}

APawn* AOneBrahamGameMode::SpawnPawnAtOrBeside(AController* Controller, AActor* Spot, bool bTrySpot)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Controller == nullptr || Spot == nullptr || GetDefaultPawnClassForController(Controller) == nullptr)
	{
		return nullptr;
	}

	// Never any pitch or roll, as the engine's SpawnDefaultPawnFor.
	const FRotator Facing(0.f, Spot->GetActorRotation().Yaw, 0.f);
	const FVector Spot3D = Spot->GetActorLocation();
	if (bTrySpot)
	{
		if (APawn* Pawn = SpawnDefaultPawnAtTransform(Controller, FTransform(Facing, Spot3D)))
		{
			return Pawn;
		}
	}

	// The spot is occupied. ASCENT puts all twelve players on the plaza at the
	// same moment and the plaza has fewer points than that, so the second
	// player sent to a point found the first one standing on it and the spawn
	// failed outright (most of a full lobby - or of the bots filling it - never
	// entered the building). Same spawn point, a free patch of navmesh beside it.
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (Nav == nullptr)
	{
		return nullptr;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	GetPawnCapsuleFor(Controller, Radius, HalfHeight);

	FNavLocation SpotOnNav;
	const float FeetToSpawnZ = Nav->ProjectPointToNavigation(Spot3D, SpotOnNav, FVector(100.f, 100.f, 300.f))
		? Spot3D.Z - SpotOnNav.Location.Z : HalfHeight + 2.f;

	// Beside means in the open with the spot: a point that is merely
	// reachable can be through a wall (the long way round, by the navmesh),
	// and a player stood outside the building is not "beside" the spawn the
	// squad was sent to - so a wall (world static) between the two rules a
	// candidate out; the squadmate standing on the spot does not. Each
	// candidate is checked for a free capsule first, so a crowded spot costs
	// queries, not failed spawns.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OneBrahamSpawnBeside), false);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		FNavLocation Beside;
		if (!Nav->GetRandomReachablePointInRadius(Spot3D, 150.f + 30.f * Attempt, Beside))
		{
			continue;
		}

		const FVector Candidate = Beside.Location + FVector(0.f, 0.f, FeetToSpawnZ + 5.f);
		if (FMath::Abs(Candidate.Z - Spot3D.Z) > 60.f
			|| World->LineTraceTestByObjectType(Spot3D, Candidate, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			|| World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn, Capsule, Params))
		{
			continue;
		}

		if (APawn* Pawn = SpawnDefaultPawnAtTransform(Controller, FTransform(Facing, Candidate)))
		{
			return Pawn;
		}
	}

	return nullptr;
}

void AOneBrahamGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending() || bMatchFinished)
	{
		return;
	}

	// A listen host logs in while the map loads, before StartPlay has built
	// the registry: an empty list there was "No spawn point available".
	if (SpawnPoints.Num() == 0)
	{
		RebuildSpawnPointRegistry();
	}

	APlayerController* PlayerController = Cast<APlayerController>(NewPlayer);
	if (PlayerController != nullptr && MustSpectate(PlayerController))
	{
		return;
	}

	// ASCENT with every floor closed (the Purge seals the whole tower up to the
	// Skydeck): there is no legal way in for a late joiner, and a fallback
	// PlayerStart is on the plaza, in the gas. They watch - Spectating, so they
	// are not counted as a survivor who has no pawn - and are not retried.
	if (NewPlayer->GetPawn() == nullptr && SpawnPoints.Num() > 0 && MatchSettings != nullptr
		&& MatchSettings->Mode == EOneBrahamMatchMode::Ascent && GetAscentEntryFloor() == INDEX_NONE)
	{
		if (AOneBrahamPlayerState* State = NewPlayer->GetPlayerState<AOneBrahamPlayerState>())
		{
			if (State->LifeState != EOneBrahamLifeState::Spectating)
			{
				State->LifeState = EOneBrahamLifeState::Spectating;
				RebuildSquadInfo();
				OB_MATCH_LOG("'%s' joined with every floor sealed or purged: spectating.", *State->GetPlayerName());
			}
		}
		PendingRespawns.Remove(NewPlayer);
		return;
	}

	TArray<AOneBrahamSpawnPoint*> Points;
	TArray<bool> Clear;
	GatherSpawnAttemptsFor(NewPlayer, Points, Clear);

	// A living pawn is re-possessed where it stands; an unauthored level uses
	// the engine's PlayerStart.
	if (NewPlayer->GetPawn() != nullptr || Points.Num() == 0)
	{
		AActor* StartSpot = Points.Num() > 0 ? Points[0] : ChoosePlayerStart(NewPlayer);
		if (StartSpot == nullptr)
		{
			OB_ERROR("No spawn point available for '%s' - the match cannot continue for that player.",
				*NewPlayer->GetName());
			return;
		}
		RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
		return;
	}

	// Walk the list. The pawn is spawned here and handed to the engine's
	// restart already set, so a blocked point costs nothing: the engine's own
	// path would have called FailedToSpawnPawn (the controller goes Inactive,
	// and the client is told so) on every point that did not fit.
	const int32 Tries = FMath::Min(Points.Num(), MaxSpawnAttemptsPerRestart);
	for (int32 Index = 0; Index < Tries; ++Index)
	{
		AOneBrahamSpawnPoint* Spawn = Points[Index];
		if (APawn* Pawn = SpawnPawnAtOrBeside(NewPlayer, Spawn, Clear[Index]))
		{
			FailedSpawnsOf.Remove(NewPlayer);
			NewPlayer->SetPawn(Pawn);
			RestartPlayerAtPlayerStart(NewPlayer, Spawn);
			return;
		}

		// To the back of this controller's list (OrderSpawnAttempts).
		TArray<TWeakObjectPtr<AOneBrahamSpawnPoint>>& Failed = FailedSpawnsOf.FindOrAdd(NewPlayer);
		Failed.Remove(Spawn);
		Failed.Add(Spawn);
	}

	OB_WARN("'%s' found no free spawn point in %d tries; trying again in %.0f s.",
		NewPlayer->PlayerState != nullptr ? *NewPlayer->PlayerState->GetPlayerName() : *NewPlayer->GetName(),
		Tries, RespawnRetrySeconds);

	// A human never waits on a spawn that nothing will retry: the respawn
	// queue tries again (and the next point) in a second. Bots have their own
	// once-a-second retry in Tick.
	if (PlayerController != nullptr && !bMatchFinished && !PendingRespawns.Contains(NewPlayer)
		&& PlayerCanRestart(PlayerController))
	{
		PendingRespawns.Add(NewPlayer, RespawnRetrySeconds);
	}
}

void AOneBrahamGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	// Called directly (not through RestartPlayer) with no pawn yet: the same
	// at-or-beside spawn, so the engine's path never stacks two capsules.
	if (NewPlayer != nullptr && NewPlayer->GetPawn() == nullptr && StartSpot != nullptr)
	{
		if (APawn* Pawn = SpawnPawnAtOrBeside(NewPlayer, StartSpot, true))
		{
			NewPlayer->SetPawn(Pawn);
		}
	}

	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);

	if (StartSpot == nullptr || NewPlayer == nullptr || NewPlayer->GetPawn() == nullptr)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	AOneBrahamCharacter* Character = NewPlayer ? Cast<AOneBrahamCharacter>(NewPlayer->GetPawn()) : nullptr;
	AOneBrahamPlayerState* State = NewPlayer ? NewPlayer->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

	if (AOneBrahamSpawnPoint* OBSpawn = Cast<AOneBrahamSpawnPoint>(StartSpot))
	{
		OBSpawn->MarkUsed(Now);

		if (Character != nullptr)
		{
			Character->SetCurrentFloorIndex(OBSpawn->FloorIndex);
		}
	}

	if (State != nullptr)
	{
		State->LifeState = EOneBrahamLifeState::Alive;
		State->Health = MatchSettings ? MatchSettings->StartingHealth : 100.f;
		State->MaxHealth = State->Health;
		State->LastSpawnServerTime = Now;
	}

	if (Character != nullptr && MatchSettings != nullptr)
	{
		Character->ApplyMatchSettings(MatchSettings);
		Character->ServerOnRespawned();
	}

	// Spawn grace (invulnerability) if the mode asks for it.
	if (Character != nullptr && MatchSettings != nullptr && MatchSettings->SpawnGraceInvulnerabilitySeconds > 0.f)
	{
		if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			Health->GrantInvulnerability(MatchSettings->SpawnGraceInvulnerabilitySeconds);
		}
	}

	RebuildSquadInfo();
}

// ==========================================================================
//  Login / logout
// ==========================================================================
void AOneBrahamGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	AOneBrahamPlayerState* State = NewPlayer ? NewPlayer->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	if (State == nullptr)
	{
		return;
	}

	// Apply the archetype + display name chosen in the lobby. The GameInstance
	// profile on this machine is the host's own, so it only ever describes the
	// local controller; remote players send theirs (cosmetics, clan tag,
	// loadout) from their own machine, and on dedicated the travel option rules.
	UOneBrahamGameInstance* GameInstance = NewPlayer->IsLocalController() ? GetGameInstance<UOneBrahamGameInstance>() : nullptr;
	if (GameInstance != nullptr)
	{
		if (const UOneBrahamSaveSettings* Profile = GameInstance->GetProfile())
		{
			if (State->GetPlayerName().IsEmpty() && !Profile->PlayerName.IsEmpty())
			{
				State->SetPlayerName(Profile->PlayerName);
			}
			State->EquippedCosmetics = Profile->EquippedCosmetics;
			State->ClanTag = Profile->ClanTag;
		}
	}

	State->SetPlayerName(State->GetPlayerName().IsEmpty() && !PendingPlayerName.IsEmpty() ? PendingPlayerName : State->GetPlayerName());
	State->SessionMode = MatchSettings ? MatchSettings->Mode : EOneBrahamMatchMode::Ascent;

	// Archetype: prefer the travel option, then the GameInstance profile, then a
	// stable per-player default so two players never look identical by accident.
	if (!PendingArchetype.IsEmpty())
	{
		State->ArchetypeTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("OB.Character.Archetype.%s"), *PendingArchetype)), false);
	}
	if (!State->ArchetypeTag.IsValid())
	{
		static const TCHAR* DefaultArchetypes[] =
		{
			TEXT("BusinessAnalyst"), TEXT("Graduate"), TEXT("FinanceBroker"),
			TEXT("ITOperations"), TEXT("FacilitiesSecurity"), TEXT("Consultant")
		};
		const int32 Index = FMath::Abs(State->GetPlayerId()) % UE_ARRAY_COUNT(DefaultArchetypes);
		State->ArchetypeTag = FGameplayTag::RequestGameplayTag(
			FName(*FString::Printf(TEXT("OB.Character.Archetype.%s"), DefaultArchetypes[Index])), false);
	}

	State->RemainingRevives = MatchSettings ? MatchSettings->MaxRevivesPerPlayerPerRound : 2;

	// A human takes a bot's slot rather than spectating a squad of bots. This
	// player's own state is already in the array but has no squad yet, so it
	// is not counted.
	MakeRoomForHuman();

	AssignPlayerToSquad(State);

	// Now that the player has a squad, they get a pawn if the mode allows one
	// now - a late joiner included (HandleStartingNewPlayer waited for this).
	StartNewPlayerIfAllowed(NewPlayer);

	Announce(FText::Format(
		FText::FromString(TEXT("{0} has entered the building.")),
		FText::FromString(State->GetPlayerName())), false);

	OB_MATCH_LOG("PostLogin: '%s' (id=%d, team=%s, squad=%d). Players=%d",
		*State->GetPlayerName(), State->GetPlayerId(), *UEnum::GetValueAsString(State->Team), State->SquadId,
		GetNumPlayers());
}

void AOneBrahamGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// In ASCENT, the player does not spawn until the active phase begins; they
	// stand in the lobby "holding area" (a locked camera above the plaza).
	AOneBrahamPlayerState* State = NewPlayer ? NewPlayer->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	const bool bWarmup = GetMatchPhase() == EOneBrahamMatchPhase::Warmup;

	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::Ascent && bWarmup && State != nullptr)
	{
		// Spectate the plaza during warmup rather than spawning into a fight that
		// has not started yet.
		State->LifeState = EOneBrahamLifeState::Spectating;
		return;
	}

	// The engine calls this from inside Super::PostLogin, before PostLogin has
	// given the player a squad: a pawn spawned now was teamless (the spawn
	// rules took every bot for an enemy, the squad corner was nobody's) and,
	// on a failed spawn, nothing tried again. PostLogin and seamless travel
	// start the player once the squad is settled (StartNewPlayerIfAllowed).
	if (State != nullptr && State->SquadId == INDEX_NONE && State->Team != EOneBrahamTeam::Spectator)
	{
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AOneBrahamGameMode::StartNewPlayerIfAllowed(APlayerController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->GetPawn() != nullptr || bMatchFinished)
	{
		return;
	}

	AOneBrahamPlayerState* State = NewPlayer->GetPlayerState<AOneBrahamPlayerState>();
	if (State == nullptr || State->Team == EOneBrahamTeam::Spectator)
	{
		// A full ASCENT lobby: they watch.
		return;
	}

	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::Ascent
		&& GetMatchPhase() == EOneBrahamMatchPhase::Warmup)
	{
		// Parked over the plaza until StartActiveMatch brings everyone in.
		State->LifeState = EOneBrahamLifeState::Spectating;
		return;
	}

	if (!bStartPlayersAsSpectators && !MustSpectate(NewPlayer) && PlayerCanRestart(NewPlayer))
	{
		RestartPlayer(NewPlayer);
	}
}

void AOneBrahamGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	Super::HandleSeamlessTravelPlayer(C);

	// A player carried over by seamless travel (a dedicated server moving to
	// the match map) never goes through PostLogin, and most of them arrive
	// after StartPlay has already filled the empty slots with bots. Without
	// this they kept the squad their PlayerState copied from the last map on
	// top of a full squad of bots (five in a squad of four), or, with no squad
	// to copy, had none at all.
	APlayerController* PlayerController = Cast<APlayerController>(C);
	AOneBrahamPlayerState* State = PlayerController != nullptr ? PlayerController->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	if (State == nullptr || MatchSettings == nullptr || State->Team == EOneBrahamTeam::Spectator)
	{
		return;
	}

	if (SquadToTeamMap.IsValidIndex(State->SquadId))
	{
		// Keep the squad (premade parties stay together across maps); a bot in
		// it makes way if it is now over-full.
		TArray<int32> Humans;
		TArray<int32> Bots;
		CountSquadMembers(Humans, Bots);
		if (Humans[State->SquadId] + Bots[State->SquadId] > MatchSettings->PlayersPerTeam && Bots[State->SquadId] > 0)
		{
			RemoveBotToMakeRoom(State->SquadId);
		}
		State->Team = GetTeamForSquad(State->SquadId);
		RebuildSquadInfo();
		return;
	}

	State->SquadId = INDEX_NONE;
	MakeRoomForHuman();
	AssignPlayerToSquad(State);

	// HandleStartingNewPlayer waited for the squad; start them now.
	StartNewPlayerIfAllowed(PlayerController);
}

void AOneBrahamGameMode::GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList)
{
	Super::GetSeamlessTravelActorList(bToTransition, ActorList);

	// The engine keeps every PlayerState through seamless travel, bots' too,
	// but not the bot controllers (they are transient). A bot's PlayerState
	// would arrive in the next match owned by nobody, the new GameState would
	// pick it up from the world, and CountSquadMembers would count a squad
	// slot held by a bot that no longer exists. The next match makes its own.
	ActorList.RemoveAll([](const AActor* Actor)
	{
		const APlayerState* State = Cast<APlayerState>(Actor);
		return State != nullptr && (State->IsABot() || Cast<AOneBrahamBotController>(State->GetOwner()) != nullptr);
	});
}

void AOneBrahamGameMode::Logout(AController* Exiting)
{
	AOneBrahamPlayerState* State = Exiting ? Exiting->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	const FString PlayerName = State ? State->GetPlayerName() : TEXT("A player");
	const int32 SquadId = State ? State->SquadId : INDEX_NONE;
	const bool bWasLeader = State ? State->bIsSquadLeader : false;

	PendingRespawns.Remove(Exiting);
	FailedSpawnsOf.Remove(Exiting);

	Super::Logout(Exiting);

	// Eliminate the departed player's squad presence and promote a new leader.
	RebuildSquadInfo();

	if (bWasLeader && SquadId != INDEX_NONE)
	{
		if (AOneBrahamGameState* GS = GetOBGameState())
		{
			if (GS->Squads.IsValidIndex(SquadId))
			{
				for (APlayerState* BaseState : GS->PlayerArray)
				{
					AOneBrahamPlayerState* Candidate = Cast<AOneBrahamPlayerState>(BaseState);
					if (Candidate != nullptr && Candidate->SquadId == SquadId)
					{
						Candidate->bIsSquadLeader = true;
						OB_MATCH_LOG("Squad %d leader promoted to '%s'.", SquadId, *Candidate->GetPlayerName());
						break;
					}
				}
			}
		}
	}

	FOneBrahamKillFeedEntry Entry;
	Entry.KillerName = TEXT("");
	Entry.VictimName = PlayerName;
	Entry.Verb = EOneBrahamKillFeedVerb::Left;
	if (AOneBrahamGameState* GS = GetOBGameState())
	{
		GS->AddKillFeedEntry(Entry);
	}

	EvaluateMatchEndConditions();
}

// ==========================================================================
//  Announcements
// ==========================================================================
void AOneBrahamGameMode::Announce(const FText& Text, bool bTannoy)
{
	if (AOneBrahamGameState* GS = GetOBGameState())
	{
		GS->MulticastAnnounce(Text, bTannoy);
	}

	OB_MATCH_LOG("ANNOUNCE: %s", *Text.ToString());
}

// ==========================================================================
//  Bots
// ==========================================================================
void AOneBrahamGameMode::PopulateWithBots(int32 MaxBots)
{
	UWorld* World = GetWorld();
	if (World == nullptr || MatchSettings == nullptr || MaxBots == 0 || SquadToTeamMap.Num() == 0)
	{
		return;
	}

	TArray<int32> Humans;
	TArray<int32> ExistingBots;
	CountSquadMembers(Humans, ExistingBots);

	// Existing bots occupy slots like anyone: count them in, and take them off
	// the cap, so calling this twice tops up rather than doubling the roster.
	TArray<int32> Occupied = Humans;
	int32 BotsAlready = 0;
	for (int32 Squad = 0; Squad < Occupied.Num(); ++Squad)
	{
		Occupied[Squad] += ExistingBots[Squad];
		BotsAlready += ExistingBots[Squad];
	}

	const int32 Remaining = MaxBots < 0 ? OneBrahamBotRules::FillAllSlots : FMath::Max(0, MaxBots - BotsAlready);
	if (Remaining == 0)
	{
		return;
	}

	const TArray<int32> Plan = OneBrahamBotRules::ComputeBotsPerSquad(
		SquadToTeamMap.Num(), MatchSettings->PlayersPerTeam, Occupied, Remaining);

	int32 BotsSpawned = 0;
	for (int32 Squad = 0; Squad < Plan.Num(); ++Squad)
	{
		for (int32 Index = 0; Index < Plan[Squad]; ++Index)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// Transient: a bot is never saved with the level or carried through travel.
			SpawnParams.ObjectFlags |= RF_Transient;

			AOneBrahamBotController* Bot = World->SpawnActor<AOneBrahamBotController>(
				AOneBrahamBotController::StaticClass(), FTransform::Identity, SpawnParams);
			if (Bot == nullptr)
			{
				OB_WARN("Could not spawn a bot controller for squad %d.", Squad);
				continue;
			}

			const int32 Ordinal = Occupied[Squad] + Index;
			Bot->InitBot(GetTeamForSquad(Squad), Squad, Ordinal, OneBrahamBotRules::SkillForBot(Ordinal),
				OneBrahamBotRules::MakeBotName(NextBotOrdinal++), MatchSettings->Mode);

			BotControllers.Add(Bot);
			++BotsSpawned;

			OB_MATCH_LOG("Bot '%s' joins squad %d (%s), skill %.2f.",
				Bot->PlayerState != nullptr ? *Bot->PlayerState->GetPlayerName() : *Bot->GetName(),
				Squad, *UEnum::GetValueAsString(GetTeamForSquad(Squad)), Bot->BotSkill);
		}
	}

	bBotPracticeMode = BotControllers.Num() > 0;
	RebuildSquadInfo();

	if (bBotPracticeMode && BotFloorGroup.Num() == 0 && !bBotFloorGraphPending)
	{
		BuildBotFloorGraph();
	}

	// Into the building now if the mode allows it; ASCENT holds everyone on the
	// plaza until the warmup ends and StartActiveMatch spawns the bots then.
	if (CanBotsSpawnNow())
	{
		for (AOneBrahamBotController* Bot : BotControllers)
		{
			SpawnBotPawn(Bot);
		}
	}

	FString PerSquad;
	for (int32 Squad = 0; Squad < Plan.Num(); ++Squad)
	{
		PerSquad += FString::Printf(TEXT("%s%d+%d"), Squad > 0 ? TEXT(" ") : TEXT(""), Humans[Squad], Plan[Squad] + ExistingBots[Squad]);
	}
	OB_MATCH_LOG("Bots: %d added (%d in the match) across %d squads [humans+bots per squad: %s], limit %s, pawns %s.",
		BotsSpawned, GetBotCount(), Plan.Num(), *PerSquad,
		MaxBots < 0 ? TEXT("fill") : *FString::FromInt(MaxBots),
		CanBotsSpawnNow() ? TEXT("now") : TEXT("when the match starts"));
}

void AOneBrahamGameMode::BuildBotFloorGraph()
{
	// Which floors the navmesh actually joins. A bot sent at an objective it has
	// no path to gets the search's "closest" partial path, which changes from one
	// re-plan to the next and ends wherever the node budget ran out - against
	// the frontage, in a revolving door - and the bot dithers between them. So
	// the match works out once which floors are connected, and a bot travels to
	// the highest floor it can really reach on the way to its objective.
	BotFloorAnchors.Reset();
	BotFloorGroup.Reset();
	bBotFloorGraphPending = false;

	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World != nullptr ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (Nav == nullptr)
	{
		return;
	}

	// A navmesh still building (dynamic generation rebuilding dirty tiles in a
	// packaged build's first frames) answers "no path" for floors it will join
	// a second later, and the graph is built once: every bot would be held on
	// the floor it spawned on for the whole match. Ask again when it is done;
	// until then FindBotTravelTarget sends bots straight at their objective.
	if (Nav->IsNavigationBuildInProgress())
	{
		bBotFloorGraphPending = true;
		OB_MATCH_LOG("Bots: the navmesh is still building; the floor graph waits for it.");
		return;
	}

	// One anchor per floor: its first spawn point (placed on walkable floor).
	for (const AOneBrahamSpawnPoint* Spawn : SpawnPoints)
	{
		if (Spawn != nullptr && !BotFloorAnchors.Contains(Spawn->FloorIndex))
		{
			BotFloorAnchors.Add(Spawn->FloorIndex, Spawn->GetActorLocation());
		}
	}

	TArray<int32> Floors;
	BotFloorAnchors.GetKeys(Floors);
	Floors.Sort();

	// Union-find over floors, joined by complete paths between floors up to two
	// apart (a flight of stairs, or an escalator over a double-height space).
	TMap<int32, int32> Parent;
	for (const int32 Floor : Floors)
	{
		Parent.Add(Floor, Floor);
	}
	auto Find = [&Parent](int32 Floor)
	{
		while (Parent[Floor] != Floor)
		{
			Floor = Parent[Floor];
		}
		return Floor;
	};

	FString Steps;
	for (int32 I = 0; I < Floors.Num(); ++I)
	{
		for (int32 J = I + 1; J < Floors.Num() && Floors[J] - Floors[I] <= 2; ++J)
		{
			if (Find(Floors[I]) == Find(Floors[J]))
			{
				continue;
			}

			const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
				World, BotFloorAnchors[Floors[I]], BotFloorAnchors[Floors[J]]);
			const bool bJoined = Path != nullptr && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() > 0;
			Steps += FString::Printf(TEXT(" %d-%d:%s"), Floors[I], Floors[J], bJoined ? TEXT("yes") : TEXT("no"));
			if (bJoined)
			{
				Parent[Find(Floors[J])] = Find(Floors[I]);
			}
		}
	}

	FString Groups;
	int32 LastGroup = INDEX_NONE;
	for (const int32 Floor : Floors)
	{
		const int32 Group = Find(Floor);
		BotFloorGroup.Add(Floor, Group);
		Groups += FString::Printf(TEXT("%s%d"), Group == LastGroup ? TEXT(",") : TEXT(" | "), Floor);
		LastGroup = Group;
	}

	UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] nav floor links:%s"), *Steps);
	OB_MATCH_LOG("Bots: the navmesh joins floors%s%s", *Groups,
		Floors.Num() > 0 && Find(Floors[0]) != Find(Floors.Last())
			? TEXT(" - bots climb as far as their group goes (the floors are not joined by walkable navmesh)") : TEXT(""));
}

bool AOneBrahamGameMode::FindBotTravelTarget(int32 SquadId, int32 OrdinalInSquad, const FVector& From,
	FVector& OutLocation, int32& OutFloor) const
{
	FVector Objective;
	int32 ObjectiveFloor = INDEX_NONE;
	if (!FindBotObjective(SquadId, OrdinalInSquad, Objective, ObjectiveFloor))
	{
		return false;
	}

	OutLocation = Objective;
	OutFloor = ObjectiveFloor;

	const int32 FromFloor = OneBrahamConstants::FloorIndexForZ(From.Z);
	const int32* FromGroup = BotFloorGroup.Find(FromFloor);
	const int32* GoalGroup = BotFloorGroup.Find(ObjectiveFloor);
	if (FromGroup == nullptr || GoalGroup == nullptr || *FromGroup == *GoalGroup)
	{
		// Same walkable piece of the tower (or nothing known): straight there.
		return true;
	}

	// Otherwise the floor of this group nearest the objective's, which for a
	// climb is the highest one the stairs reach.
	int32 Best = INDEX_NONE;
	for (const TPair<int32, int32>& Pair : BotFloorGroup)
	{
		if (Pair.Value == *FromGroup
			&& (Best == INDEX_NONE || FMath::Abs(Pair.Key - ObjectiveFloor) < FMath::Abs(Best - ObjectiveFloor)))
		{
			Best = Pair.Key;
		}
	}

	if (Best == INDEX_NONE)
	{
		return true;
	}

	// Spread a squad over that floor's spawn points rather than one spot.
	TArray<FVector> OnFloor;
	for (const AOneBrahamSpawnPoint* Spawn : SpawnPoints)
	{
		if (Spawn != nullptr && Spawn->FloorIndex == Best)
		{
			OnFloor.Add(Spawn->GetActorLocation());
		}
	}

	OutLocation = OnFloor.Num() > 0
		? OnFloor[(FMath::Abs(SquadId) + FMath::Abs(OrdinalInSquad)) % OnFloor.Num()]
		: BotFloorAnchors.FindRef(Best);
	OutFloor = Best;
	return true;
}

#if !UE_BUILD_SHIPPING
namespace
{
	TAutoConsoleVariable<int32> CVarBotWatch(
		TEXT("ob.Bots.Watch"),
		-1,
		TEXT("Dev: the first local player views through the eyes of bot N (index into the match's bots). -1 = off."),
		ECVF_Cheat);
}
#endif

int32 AOneBrahamGameMode::GetBotCount() const
{
	int32 Count = 0;
	for (const AOneBrahamBotController* Bot : BotControllers)
	{
		Count += IsValid(Bot) ? 1 : 0;
	}
	return Count;
}

void AOneBrahamGameMode::CountInFight(int32& HumansInSquads, int32& HumansInFight, int32& BotsInFight) const
{
	HumansInSquads = 0;
	HumansInFight = 0;
	BotsInFight = 0;

	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}
	for (const APlayerState* BaseState : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(BaseState);
		if (State == nullptr || State->SquadId == INDEX_NONE)
		{
			continue;
		}

		if (State->IsABot() || Cast<AOneBrahamBotController>(State->GetOwner()) != nullptr)
		{
			BotsInFight += State->IsEliminated() ? 0 : 1;
			continue;
		}

		++HumansInSquads;
		HumansInFight += State->IsEliminated() ? 0 : 1;
	}
}

bool AOneBrahamGameMode::CanBotsSpawnNow() const
{
	if (bMatchFinished)
	{
		return false;
	}

	// Nobody to play against: a dedicated server that boots into a match fills
	// the roster, but does not stage a bot match before its first player
	// arrives (the warmup does not wait for players, so a server left alone
	// ran twelve bots through the lockdown and the purge to "no survivors").
	// The pawns follow the first human in: the once-a-second retry in Tick.
	bool bAnyHuman = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It && !bAnyHuman; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		bAnyHuman = PlayerController != nullptr && PlayerController->PlayerState != nullptr
			&& !PlayerController->PlayerState->IsOnlyASpectator();
	}
	if (!bAnyHuman)
	{
		return false;
	}

	const EOneBrahamMatchPhase Phase = GetMatchPhase();
	if (Phase == EOneBrahamMatchPhase::Active || Phase == EOneBrahamMatchPhase::Extraction)
	{
		return true;
	}

	// The warmup is a spawned phase everywhere but ASCENT, which parks players
	// above the plaza (HandleStartingNewPlayer); bots wait with them.
	return Phase == EOneBrahamMatchPhase::Warmup && MatchSettings != nullptr
		&& MatchSettings->Mode != EOneBrahamMatchMode::Ascent;
}

void AOneBrahamGameMode::SpawnBotPawn(AOneBrahamBotController* Bot)
{
	if (!IsValid(Bot) || !CanBotsSpawnNow())
	{
		return;
	}

	// One pawn per life. A dead bot's corpse is the respawn queue's business
	// (FIRE DRILL), and in ASCENT a dead bot stays dead like anyone.
	if (Bot->GetPawn() != nullptr)
	{
		return;
	}

	const AOneBrahamPlayerState* State = Bot->GetPlayerState<AOneBrahamPlayerState>();
	const bool bRespawns = MatchSettings != nullptr && MatchSettings->bRespawnEnabled;
	if (!bRespawns && State != nullptr && State->LifeState == EOneBrahamLifeState::Dead)
	{
		return;
	}

	// Signed out by RestartPlayer (ASCENT with every floor sealed or purged):
	// there is no way in, so the once-a-second retry has nothing to try.
	if (State != nullptr && State->LifeState == EOneBrahamLifeState::Spectating)
	{
		return;
	}

	// The same spawn rules as a player: the plaza in ASCENT, squad clusters and
	// the threat rules everywhere.
	RestartPlayer(Bot);

	if (Bot->GetPawn() == nullptr)
	{
		OB_WARN("Bot '%s' failed to spawn a pawn (no free spawn point?).",
			Bot->PlayerState != nullptr ? *Bot->PlayerState->GetPlayerName() : *Bot->GetName());
	}
}

void AOneBrahamGameMode::CountSquadMembers(TArray<int32>& OutHumans, TArray<int32>& OutBots) const
{
	OutHumans.Init(0, SquadToTeamMap.Num());
	OutBots.Init(0, SquadToTeamMap.Num());

	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	for (APlayerState* BaseState : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(BaseState);
		if (State == nullptr || !OutHumans.IsValidIndex(State->SquadId))
		{
			continue;
		}

		const bool bBot = State->IsABot() || Cast<AOneBrahamBotController>(State->GetOwner()) != nullptr;
		++(bBot ? OutBots : OutHumans)[State->SquadId];
	}
}

void AOneBrahamGameMode::MakeRoomForHuman(int32 PreferredSquad)
{
	if (BotControllers.Num() == 0 || MatchSettings == nullptr)
	{
		return;
	}

	TArray<int32> Humans;
	TArray<int32> Bots;
	CountSquadMembers(Humans, Bots);

	for (int32 Squad = 0; Squad < Humans.Num(); ++Squad)
	{
		if (Humans[Squad] + Bots[Squad] < MatchSettings->PlayersPerTeam)
		{
			return;
		}
	}

	RemoveBotToMakeRoom(PreferredSquad);
}

bool AOneBrahamGameMode::RemoveBotToMakeRoom(int32 PreferredSquad)
{
	TArray<int32> Humans;
	TArray<int32> Bots;
	CountSquadMembers(Humans, Bots);

	const int32 Squad = Bots.IsValidIndex(PreferredSquad) && Bots[PreferredSquad] > 0
		? PreferredSquad
		: OneBrahamBotRules::PickSquadToYieldBot(Humans, Bots);
	if (Squad == INDEX_NONE)
	{
		return false;
	}

	// The squad's newest bot goes: the longest-serving ones keep their places.
	AOneBrahamBotController* Leaving = nullptr;
	for (AOneBrahamBotController* Bot : BotControllers)
	{
		if (IsValid(Bot) && Bot->PreferredSquadId == Squad && (Leaving == nullptr || Bot->SquadOrdinal > Leaving->SquadOrdinal))
		{
			Leaving = Bot;
		}
	}

	if (Leaving == nullptr)
	{
		return false;
	}

	OB_MATCH_LOG("Bot '%s' leaves squad %d to make room for a player.",
		Leaving->PlayerState != nullptr ? *Leaving->PlayerState->GetPlayerName() : *Leaving->GetName(), Squad);

	BotControllers.Remove(Leaving);
	PendingRespawns.Remove(Leaving);

	if (APawn* Pawn = Leaving->GetPawn())
	{
		Leaving->UnPossess();
		Pawn->Destroy();
	}

	// Destroying the controller logs it out (kill feed "left", squad rebuild)
	// and takes its PlayerState with it.
	Leaving->Destroy();
	bBotPracticeMode = GetBotCount() > 0;
	RebuildSquadInfo();
	return true;
}

bool AOneBrahamGameMode::FindBotObjective(int32 SquadId, int32 OrdinalInSquad, FVector& OutLocation, int32& OutFloor) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || MatchSettings == nullptr)
	{
		return false;
	}

	// The fallback anchor for any floor: a spawn point on it. Spawn points are
	// placed on walkable floor by the map builder, so they are on the navmesh.
	auto SpawnOnFloor = [this, OrdinalInSquad, SquadId](int32 Floor, FVector& Out) -> bool
	{
		TArray<const AOneBrahamSpawnPoint*> OnFloor;
		for (const AOneBrahamSpawnPoint* Spawn : SpawnPoints)
		{
			if (Spawn != nullptr && Spawn->FloorIndex == Floor)
			{
				OnFloor.Add(Spawn);
			}
		}
		if (OnFloor.Num() == 0)
		{
			return false;
		}
		Out = OnFloor[(FMath::Abs(SquadId) * 3 + FMath::Abs(OrdinalInSquad)) % OnFloor.Num()]->GetActorLocation();
		return true;
	};

	switch (MatchSettings->Mode)
	{
	case EOneBrahamMatchMode::Ascent:
	{
		// The climb ends on the Skydeck pad.
		TActorIterator<AOneBrahamExtractionPad> Pad(World);
		if (Pad)
		{
			OutLocation = Pad->GetActorLocation();
			OutFloor = OneBrahamConstants::FloorIndexForZ(OutLocation.Z);
			return true;
		}

		if (SpawnOnFloor(OneBrahamConstants::FloorSkydeck, OutLocation))
		{
			OutFloor = OneBrahamConstants::FloorSkydeck;
			return true;
		}
		return false;
	}

	case EOneBrahamMatchMode::FireDrill:
	{
		const TArray<FOneBrahamControlPointDef>& Points = MatchSettings->ControlPoints;
		const int32 Index = OneBrahamBotRules::PickObjectiveIndex(Points.Num(), SquadId, OrdinalInSquad);
		if (!Points.IsValidIndex(Index))
		{
			return false;
		}

		const FOneBrahamControlPointDef& Point = Points[Index];

		// The point's volume when the map has one, its floor otherwise.
		if (!Point.VolumeActorTag.IsNone())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(Point.VolumeActorTag))
				{
					OutLocation = It->GetActorLocation();
					OutFloor = OneBrahamConstants::FloorIndexForZ(OutLocation.Z);

					// The point's floor is the definition's, not the volume's
					// middle: the Reception volume fills the double-height lobby
					// and its centre is a storey up, so bots were sent to the
					// floor-2 gallery above the point (or, from the floors the
					// navmesh does not join to 2, to a spawn on floor 1 that was
					// not the point at all). Standing height on the point's slab.
					if (Point.FloorIndex >= 0)
					{
						OutFloor = Point.FloorIndex;
						OutLocation.Z = OneBrahamConstants::GroundFloorZ
							+ Point.FloorIndex * OneBrahamConstants::FloorHeightCm + 90.f;
					}
					return true;
				}
			}
		}

		if (SpawnOnFloor(Point.FloorIndex, OutLocation))
		{
			OutFloor = Point.FloorIndex;
			return true;
		}
		return false;
	}

	default:
		return false;
	}
}

// ==========================================================================
//  FIRE DRILL control points
// ==========================================================================
void AOneBrahamGameMode::TickControlPoints(float DeltaSeconds)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || MatchSettings == nullptr || MatchSettings->Mode != EOneBrahamMatchMode::FireDrill
		|| GS->MatchPhase != EOneBrahamMatchPhase::Active || GetWorld() == nullptr)
	{
		return;
	}

	// The three points were defined, sent the bots, and scored nothing: FIRE
	// DRILL was a kill count against a 200-point limit that a ten-minute drill
	// could not reach. Once a second is the resolution the definitions are in.
	ControlPointAccumulator += DeltaSeconds;
	if (ControlPointAccumulator < 1.f)
	{
		return;
	}
	const float Step = ControlPointAccumulator;
	ControlPointAccumulator = 0.f;

	const TArray<FOneBrahamControlPointDef>& Points = MatchSettings->ControlPoints;
	ControlPointStates.SetNum(Points.Num());
	if (ControlPointScoreCarry.Num() != 2)
	{
		ControlPointScoreCarry.Init(0.f, 2);
	}

	for (int32 Index = 0; Index < Points.Num() && !bMatchFinished; ++Index)
	{
		const FOneBrahamControlPointDef& Point = Points[Index];
		FControlPointState& State = ControlPointStates[Index];

		// The point's volume when the map has one; otherwise the whole floor is
		// the point (the tower's FIRE DRILL has no volumes yet).
		if (!State.bLocated)
		{
			State.bLocated = true;
			if (!Point.VolumeActorTag.IsNone())
			{
				for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				{
					if (It->ActorHasTag(Point.VolumeActorTag))
					{
						State.Bounds = It->GetComponentsBoundingBox(/*bNonColliding*/ true).ExpandBy(FVector(150.f, 150.f, 0.f));
						break;
					}
				}
			}
		}

		int32 Present[2] = { 0, 0 };
		for (const APlayerState* BaseState : GS->PlayerArray)
		{
			const AOneBrahamPlayerState* PS = Cast<AOneBrahamPlayerState>(BaseState);
			const APawn* Pawn = PS != nullptr ? PS->GetPawn() : nullptr;
			if (Pawn == nullptr || !PS->IsAlive())
			{
				continue;
			}

			const int32 TeamIndex = PS->Team == EOneBrahamTeam::InternalStaff ? 0
				: PS->Team == EOneBrahamTeam::ExternalContractors ? 1 : INDEX_NONE;
			const FVector Location = Pawn->GetActorLocation();
			if (TeamIndex == INDEX_NONE || OneBrahamConstants::FloorIndexForZ(Location.Z) != Point.FloorIndex
				|| (State.Bounds.IsValid && !State.Bounds.IsInsideXY(Location)))
			{
				continue;
			}

			++Present[TeamIndex];
		}

		const bool bContested = Present[0] > 0 && Present[1] > 0;
		const int32 Alone = bContested ? INDEX_NONE : (Present[0] > 0 ? 0 : (Present[1] > 0 ? 1 : INDEX_NONE));

		if (Alone != INDEX_NONE && Alone != State.OwnerTeam)
		{
			if (State.CapturingTeam != Alone)
			{
				State.CapturingTeam = Alone;
				State.CaptureProgressSeconds = 0.f;
			}

			State.CaptureProgressSeconds += Step;
			if (State.CaptureProgressSeconds >= Point.CaptureSeconds)
			{
				State.OwnerTeam = Alone;
				State.CapturingTeam = INDEX_NONE;
				State.CaptureProgressSeconds = 0.f;

				const FString TeamName = GetSquadDisplayName(Alone);
				OB_MATCH_LOG("Control point '%s' taken by %s.", *Point.PointId.ToString(), *TeamName);
				Announce(FText::Format(NSLOCTEXT("OneBraham", "FireDrill_PointTaken", "{0} is now held by {1}."),
					Point.DisplayName, FText::FromString(TeamName)), /*bTannoy*/ false);

				AddTeamScoreByIndex(Alone, MatchSettings->ScorePerCapture);
				continue;
			}
		}
		else if (Alone == INDEX_NONE && !bContested)
		{
			// Walked away from: a half-taken point drifts back.
			State.CaptureProgressSeconds = FMath::Max(0.f, State.CaptureProgressSeconds - Step);
		}

		// A held point earns for its holder every second somebody of theirs is
		// standing on it and nobody else is: holding is presence. (Scoring a
		// point nobody stood on made the 200-point drill a ninety-second one.)
		if (State.OwnerTeam != INDEX_NONE && Alone == State.OwnerTeam)
		{
			float& Carry = ControlPointScoreCarry[State.OwnerTeam];
			Carry += Point.ScorePerSecond * Step;
			const int32 Whole = FMath::FloorToInt(Carry);
			if (Whole > 0)
			{
				Carry -= static_cast<float>(Whole);
				AddTeamScoreByIndex(State.OwnerTeam, Whole);
			}
		}
	}
}

// ==========================================================================
//  Dev: watch a match to its end
// ==========================================================================
void AOneBrahamGameMode::DevWithdrawToSpectator(APlayerController* Player)
{
#if !UE_BUILD_SHIPPING
	AOneBrahamPlayerState* State = Player != nullptr ? Player->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	if (State == nullptr || bMatchFinished)
	{
		return;
	}

	PendingRespawns.Remove(Player);
	FailedSpawnsOf.Remove(Player);

	if (APawn* Pawn = Player->GetPawn())
	{
		Player->UnPossess();
		Pawn->Destroy();
	}

	const int32 OldSquad = State->SquadId;
	State->SquadId = INDEX_NONE;
	State->Team = EOneBrahamTeam::Spectator;
	State->LifeState = EOneBrahamLifeState::Spectating;
	State->bIsSquadLeader = false;
	State->ForceNetUpdate();
	RebuildSquadInfo();

	// A bot takes the slot, so the match is the full roster it would have been.
	if (BotLimit != 0)
	{
		PopulateWithBots(BotLimit);
	}

	if (IConsoleVariable* Follow = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Match.FollowBots")))
	{
		Follow->Set(1, ECVF_SetByCode);
	}

	OB_MATCH_LOG("Dev: '%s' withdrawn from squad %d to watch the match (%d bots).", *State->GetPlayerName(), OldSquad, GetBotCount());
#endif
}
