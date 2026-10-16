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

#include "Engine/World.h"
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

	// Keep the survivor count fresh for the HUD.
	if (GS != nullptr && (GS->MatchPhase == EOneBrahamMatchPhase::Active || GS->MatchPhase == EOneBrahamMatchPhase::Extraction))
	{
		GS->SurvivorsRemaining = GS->GetAlivePlayerCount();
	}
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
		Extraction->OnExtractionComplete.AddDynamic(this, &AOneBrahamGameMode::HandleExtractionComplete);
		Extraction->OnExtractionFailed.AddDynamic(this, &AOneBrahamGameMode::HandleExtractionFailed);
	}

	if (UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
	{
		Lockdown->OnStageChanged.AddDynamic(this, &AOneBrahamGameMode::HandleLockdownStageChanged);
	}

	UpdateObjectiveState();
}

void AOneBrahamGameMode::HandleExtractionComplete(int32 WinningSquadId, EOneBrahamTeam WinningTeam)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || bMatchFinished)
	{
		return;
	}

	// Count who actually made it onto the wire: a squad that extracts with two of
	// its four players has still won, and the result screen says so.
	int32 ExtractedCount = 0;
	const FOneBrahamSquadInfo Squad = GS->GetSquadInfo(WinningSquadId);
	for (const FOneBrahamSquadMemberInfo& Member : Squad.Members)
	{
		if (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Extracted)
		{
			++ExtractedCount;
		}
	}

	const FString SquadName = Squad.SquadName.IsEmpty()
		? GetSquadDisplayName(WinningSquadId)
		: Squad.SquadName.ToString();

	OB_MATCH_LOG("Extraction complete: squad %d (%s) left the building with %d aboard.",
		WinningSquadId, *SquadName, ExtractedCount);

	CompleteMatchWithExtraction(WinningTeam, SquadName, FMath::Max(1, ExtractedCount));
}

void AOneBrahamGameMode::HandleExtractionFailed(int32 SquadId, EOneBrahamExtractionFailReason Reason)
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
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

	FinishMatch(WinningTeam, WinningSquadName, /*bNoSurvivors*/ false, ExtractedCount, EOneBrahamExtractionFailReason::None);
}

void AOneBrahamGameMode::CompleteMatchNoSurvivors()
{
	if (bMatchFinished)
	{
		return;
	}

	FinishMatch(EOneBrahamTeam::None, FString(), /*bNoSurvivors*/ true, 0, EOneBrahamExtractionFailReason::CallersEliminated);
}

void AOneBrahamGameMode::FinishMatch(EOneBrahamTeam WinningTeam, const FString& WinningSquadName, bool bNoSurvivors, int32 ExtractedCount,
	EOneBrahamExtractionFailReason FailReason, const FText& AnnouncementOverride)
{
	bMatchFinished = true;
	SetMatchPhase(EOneBrahamMatchPhase::Complete);

	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	GS->MatchResult = BuildMatchResult(WinningTeam, WinningSquadName, bNoSurvivors, ExtractedCount);
	GS->OnMatchResultReady.Broadcast(GS->MatchResult);

	// End-of-match announcements: the Tannoy gets the last word, as it should.
	FText Announcement;
	if (!AnnouncementOverride.IsEmpty())
	{
		Announcement = AnnouncementOverride;
	}
	else if (bNoSurvivors)
	{
		Announcement = FText::FromString(TEXT("All staff accounted for. There are no survivors. Please await further instruction."));
	}
	else if (WinningTeam != EOneBrahamTeam::None)
	{
		Announcement = FText::Format(
			FText::FromString(TEXT("{0} has been extracted. Thank you for visiting One Braham.")),
			FText::FromString(WinningSquadName.IsEmpty() ? TEXT("A squad") : WinningSquadName));
	}
	else
	{
		Announcement = FText::FromString(TEXT("The extraction has failed. Security has been notified."));
	}

	Announce(Announcement, true);

	OB_MATCH_LOG("MATCH COMPLETE: winner=%s squad='%s' noSurvivors=%d extracted=%d duration=%.1fs reason=%s",
		*UEnum::GetValueAsString(WinningTeam), *WinningSquadName, bNoSurvivors ? 1 : 0, ExtractedCount,
		GetWorld()->GetTimeSeconds() - MatchStartTime,
		*UEnum::GetValueAsString(FailReason));

	// Award cosmetics/achievements (client-side, cosmetic only) then travel back
	// to the lobby after the post-match window.
	GetWorldTimerManager().SetTimer(MatchPhaseTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
		{
			GameInstance->TravelToMainMenu();
		}
	}), static_cast<float>(MatchSettings->PostMatchSeconds), false);
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

	if (const AOneBrahamGameState* GS = GetOBGameState())
	{
		for (const FOneBrahamSquadInfo& Squad : GS->Squads)
		{
			for (const FOneBrahamSquadMemberInfo& Member : Squad.Members)
			{
				FOneBrahamPlayerSummary Summary;
				Summary.PlayerName = Member.PlayerName;
				Summary.ArchetypeTag = Member.ArchetypeTag;
				Summary.Team = Squad.Team;
				Summary.Kills = Member.Kills;
				Summary.Downs = Member.Downs;
				Summary.Deaths = Member.Deaths;
				Summary.FloorsClimbed = 0;
				Summary.bExtracted = (Member.LifeState == EOneBrahamLifeState::Extracted);
				Result.PlayerSummaries.Add(Summary);
			}
		}
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
	if (KillerState != nullptr && !bSelfKill && !bKilledByBuilding)
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
	if (GS == nullptr || !GS->TeamScores.IsValidIndex(TeamIndex))
	{
		return;
	}

	GS->TeamScores[TeamIndex] += Amount;

	if (GS->TeamScores[TeamIndex] >= MatchSettings->ScoreToWin)
	{
		const EOneBrahamTeam WinningTeam = TeamIndex == 0 ? EOneBrahamTeam::InternalStaff : EOneBrahamTeam::ExternalContractors;
		FinishMatch(WinningTeam, WinningTeam == EOneBrahamTeam::InternalStaff ? TEXT("Internal Staff") : TEXT("External Contractors"),
			false, 0, EOneBrahamExtractionFailReason::None);
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

	FText Announcement;
	if (Outcome.bNoSurvivors)
	{
		Announcement = FText::FromString(TEXT("The building is now closed. Nobody has been extracted. Please await further instruction."));
	}
	else if (Outcome.bDraw)
	{
		Announcement = FText::FromString(TEXT("The fire drill has ended. Both departments are level. Please return to your desks."));
	}

	FinishMatch(Outcome.WinningTeam, Outcome.WinningSquadName, Outcome.bNoSurvivors, 0,
		EOneBrahamExtractionFailReason::None, Announcement);
}

void AOneBrahamGameMode::EvaluateMatchEndConditions()
{
	if (bMatchFinished || MatchSettings == nullptr)
	{
		return;
	}

	// ASCENT ends in "no survivors" when every squad is eliminated before any
	// extraction. FIRE DRILL never ends this way because players respawn.
	if (MatchSettings->Mode != EOneBrahamMatchMode::Ascent)
	{
		return;
	}

	// Only a match in progress ends. In the warmup everyone is parked as a
	// spectator, so a player leaving the lobby read as "every squad wiped".
	const EOneBrahamMatchPhase Phase = GetMatchPhase();
	if (Phase != EOneBrahamMatchPhase::Active && Phase != EOneBrahamMatchPhase::Extraction)
	{
		return;
	}

	const AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || GS->Squads.Num() == 0)
	{
		return;
	}

	const bool bExtractionActive = GS->GetExtractionComponent() != nullptr &&
		GS->GetExtractionComponent()->IsExtractionActive();

	// The match is for its humans. With bots filling the squads, a solo player
	// who died used to watch the bots until the purge or the 35-minute clock
	// (they cannot extract, so neither came soon), and a server whose players
	// had all died ran on for nobody. When no human is left in the fight, the
	// extraction has failed for everyone who mattered to it. (A player who
	// leaves is still in the array while Logout evaluates, so leaving does not
	// end the match for the others.)
	int32 HumansInSquads = 0;
	int32 HumansInFight = 0;
	int32 BotsInFight = 0;
	CountInFight(HumansInSquads, HumansInFight, BotsInFight);
	if (OneBrahamBotRules::ShouldEndWithNoHumansLeft(HumansInSquads, HumansInFight, BotsInFight, bExtractionActive))
	{
		OB_MATCH_LOG("Every human in a squad is out with %d bot(s) still in the fight: the match ends.", BotsInFight);
		FinishMatch(EOneBrahamTeam::None, FString(), /*bNoSurvivors*/ false, 0, EOneBrahamExtractionFailReason::CallersEliminated);
		return;
	}

	bool bAnySquadAlive = false;
	for (const FOneBrahamSquadInfo& Squad : GS->Squads)
	{
		if (Squad.IsAlive())
		{
			bAnySquadAlive = true;
			break;
		}
	}

	if (!bAnySquadAlive && !bExtractionActive)
	{
		CompleteMatchNoSurvivors();
	}
}

void AOneBrahamGameMode::DebugForceMatchResult(int32 WinningTeamIndex)
{
	if (WinningTeamIndex < 0)
	{
		CompleteMatchNoSurvivors();
		return;
	}

	const AOneBrahamGameState* GS = GetOBGameState();
	const FString SquadName = GS != nullptr && GS->Squads.IsValidIndex(WinningTeamIndex) && !GS->Squads[WinningTeamIndex].SquadName.IsEmpty()
		? GS->Squads[WinningTeamIndex].SquadName.ToString()
		: FString::Printf(TEXT("Squad %d"), WinningTeamIndex + 1);

	const EOneBrahamTeam Team = GS != nullptr && GS->Squads.IsValidIndex(WinningTeamIndex)
		? GS->Squads[WinningTeamIndex].Team
		: EOneBrahamTeam::Alpha;

	FinishMatch(Team, SquadName, false, MatchSettings ? MatchSettings->PlayersPerTeam : 4, EOneBrahamExtractionFailReason::None);
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
	const UWorld* World = GetWorld();
	if (World == nullptr || SpawnPoints.Num() == 0)
	{
		return nullptr;
	}

	const float Now = World->GetTimeSeconds();
	const AOneBrahamPlayerState* RequestingState = Controller ? Controller->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	const EOneBrahamTeam RequestingTeam = RequestingState ? RequestingState->Team : EOneBrahamTeam::None;
	const int32 RequestingSquad = RequestingState ? RequestingState->SquadId : INDEX_NONE;

	// --- Gather candidates ------------------------------------------------
	TArray<AOneBrahamSpawnPoint*> Candidates;
	AOneBrahamSpawnPoint* BestFallback = nullptr;
	float BestFallbackSafety = -1.f;
	TArray<float> Weights;
	float TotalWeight = 0.f;

	for (AOneBrahamSpawnPoint* Spawn : SpawnPoints)
	{
		if (Spawn == nullptr || !Spawn->IsAvailable(Now))
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
			const float Safety = OneBrahamSpawnRules::FallbackSafety(Threat);
			if (Safety > BestFallbackSafety)
			{
				BestFallbackSafety = Safety;
				BestFallback = Spawn;
			}
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

		Candidates.Add(Spawn);
		Weights.Add(Weight);
		TotalWeight += Weight;
	}

	if (Candidates.Num() == 0)
	{
		// Every spawn is threatened. Take the safest of the vetoed ones rather
		// than a random one - a random pick is how a camped spawn gets chosen.
		if (BestFallback != nullptr)
		{
			return BestFallback;
		}

		// Nothing was even considered (every spawn on cooldown or filtered):
		// ignore the cooldowns rather than leave a player unable to spawn.
		for (AOneBrahamSpawnPoint* Spawn : SpawnPoints)
		{
			if (Spawn != nullptr && Spawn->bEnabled)
			{
				return Spawn;
			}
		}
		return nullptr;
	}

	// --- Weighted pick ----------------------------------------------------
	float Roll = FMath::FRandRange(0.f, TotalWeight);
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0.f)
		{
			return Candidates[Index];
		}
	}

	return Candidates.Last();
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
	if (Controller == nullptr)
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
	// ASCENT is the climb: everyone walks in off the plaza (Docs/09, floor 0
	// is the spawn; Docs/06, "the lobby start"). Asking for any floor dropped
	// squads straight onto 12 with the induction telling them to walk in from
	// the street. A full plaza falls through to the general rule below.
	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::Ascent)
	{
		// A squad walks in together from its own corner of the plaza: the
		// cluster is the squad (Docs/06, "round-robins players onto clusters").
		// The opening wave puts four people on one point at the same instant,
		// so the point's rest timer is not a reason to send three of them to
		// another squad's corner - which is what the weighted pick did, and the
		// first seconds of every full match were a fight on one spawn point.
		// RestartPlayerAtPlayerStart stands the later arrivals beside the point.
		const AOneBrahamPlayerState* State = Player != nullptr ? Player->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
		if (State != nullptr && State->SquadId != INDEX_NONE)
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
					return Spawn;
				}
			}
		}

		if (AOneBrahamSpawnPoint* Spawn = SelectSpawnPointFor(Player, OneBrahamConstants::FloorPlaza))
		{
			return Spawn;
		}
	}

	// FIRE DRILL is a fight over three control points, so it is fought on their
	// floors: a spawn anywhere in the tower put the twelve players on twelve
	// floors the navmesh does not join, and a match could run its ten minutes
	// without two enemies meeting (bots or people). Each spawn goes to one of
	// the point floors, in a random order, under the usual safety rules on
	// that floor; a map with no spawn on any point floor falls through.
	if (MatchSettings != nullptr && MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
	{
		TArray<int32> PointFloors;
		for (const FOneBrahamControlPointDef& Point : MatchSettings->ControlPoints)
		{
			if (Point.FloorIndex >= 0)
			{
				PointFloors.AddUnique(Point.FloorIndex);
			}
		}

		for (int32 Index = PointFloors.Num() - 1; Index > 0; --Index)
		{
			PointFloors.Swap(Index, FMath::RandRange(0, Index));
		}

		for (const int32 Floor : PointFloors)
		{
			if (AOneBrahamSpawnPoint* Spawn = SelectSpawnPointFor(Player, Floor))
			{
				return Spawn;
			}
		}
	}

	// Prefer our own spawn points; only fall back to a legacy APlayerStart if a
	// level has not been authored yet (early greyblock, tutorial map).
	if (AOneBrahamSpawnPoint* Spawn = SelectSpawnPointFor(Player))
	{
		return Spawn;
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

void AOneBrahamGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer == nullptr)
	{
		return;
	}

	AActor* StartSpot = ChoosePlayerStart(NewPlayer);
	if (StartSpot == nullptr)
	{
		OB_ERROR("No spawn point available for '%s' - the match cannot continue for that player.",
			*NewPlayer->GetName());
		return;
	}

	RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
}

void AOneBrahamGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);

	if (StartSpot == nullptr)
	{
		return;
	}

	// The spot was occupied. ASCENT puts all twelve players on the plaza at the
	// same moment and the plaza has fewer points than that, so the second
	// player sent to a point found the first one standing on it and the spawn
	// failed outright (most of a full lobby - or of the bots filling it - never
	// entered the building). Same spawn point, a free patch of navmesh beside it.
	if (NewPlayer != nullptr && NewPlayer->GetPawn() == nullptr)
	{
		if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			const FVector Spot = StartSpot->GetActorLocation();
			FNavLocation SpotOnNav;
			const float FeetToSpawnZ = Nav->ProjectPointToNavigation(Spot, SpotOnNav, FVector(100.f, 100.f, 300.f))
				? Spot.Z - SpotOnNav.Location.Z : 90.f;

			// Beside means in the open with the spot: a point that is merely
			// reachable can be through a wall (the long way round, by the
			// navmesh), and a player stood outside the building is not "beside"
			// the spawn the squad was sent to.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(OneBrahamSpawnBeside), false);
			for (int32 Attempt = 0; Attempt < 16 && NewPlayer->GetPawn() == nullptr; ++Attempt)
			{
				FNavLocation Beside;
				if (!Nav->GetRandomReachablePointInRadius(Spot, 150.f + 30.f * Attempt, Beside))
				{
					continue;
				}

				const FVector Candidate = Beside.Location + FVector(0.f, 0.f, FeetToSpawnZ + 5.f);
				if (FMath::Abs(Candidate.Z - Spot.Z) > 60.f
					|| GetWorld()->LineTraceTestByChannel(Spot, Candidate, ECC_Pawn, Params))
				{
					continue;
				}

				Super::RestartPlayerAtTransform(NewPlayer, FTransform(StartSpot->GetActorRotation(), Candidate));
			}
		}

		if (NewPlayer->GetPawn() == nullptr)
		{
			return;
		}
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

	// Late joiners during a bot-practice or FIRE DRILL match spawn immediately.
	if (MatchSettings != nullptr && MatchSettings->Mode != EOneBrahamMatchMode::Ascent)
	{
		RestartPlayer(NewPlayer);
	}

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

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
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
