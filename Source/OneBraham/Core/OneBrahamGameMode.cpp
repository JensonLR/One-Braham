// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameMode.h"
#include "Voice/OneBrahamVoiceDirector.h"
#include "Loot/OneBrahamLootSubsystem.h"
#include "Data/OneBrahamCharacterDataAsset.h"

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
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Data/OneBrahamWeaponDataAsset.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

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

		if (const UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
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

	// Bot practice: `?Bots=6` fills the roster on a listen server for testing.
	int32 BotCount = 0;
	bBotPracticeMode = FParse::Value(*Options, TEXT("Bots="), BotCount) && BotCount > 0;
	RequestedBotCount = bBotPracticeMode ? BotCount : 0;

	OB_MATCH_LOG("InitGame map='%s' mode=%s teams=%d teamSize=%d bots=%d dedicated=%s",
		*MapName,
		*UEnum::GetValueAsString(MatchSettings->Mode),
		SquadToTeamMap.Num(),
		MatchSettings->PlayersPerTeam,
		bBotPracticeMode ? BotCount : 0,
		IsRunningDedicatedServer() ? TEXT("yes") : TEXT("no"));
}

void AOneBrahamGameMode::ParseTravelOptions(const FString& Options)
{
	PendingModeString = UGameplayStatics::ParseOption(Options, TEXT("Mode"));

	if (PendingModeString.IsEmpty())
	{
		// Dedicated servers launched with `-Mode=FireDrill` on the command line.
		FParse::Value(FCommandLine::Get(), TEXT("Mode="), PendingModeString);
	}

	// Only the mode is the server's to choose. A player's name and archetype are
	// theirs, and come from their own machine.
	OB_MATCH_LOG("Travel options: Mode='%s'", *PendingModeString);
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

	// Night Shift and bot practice use the AI director; ASCENT explicitly does not.
	// `?Bots=N` is the number of bots, as the option says. It used to be parsed
	// and then ignored in favour of PlayersPerTeam - 1 per squad.
	if (bBotPracticeMode)
	{
		PopulateWithBots(RequestedBotCount);
	}
	else if (MatchSettings->Mode == EOneBrahamMatchMode::NightShift)
	{
		PopulateWithBots(8);
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
			PendingRespawns.Remove(Controller);
			if (Controller->GetPawn() == nullptr)
			{
				RestartPlayer(Controller);
			}
		}
	}

	// FIRE DRILL control point scoring and time limit.
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS != nullptr && GS->MatchPhase == EOneBrahamMatchPhase::Active && MatchSettings->Mode == EOneBrahamMatchMode::FireDrill)
	{
		if (MatchSettings->bScoreByTimeLimit && GS->PhaseElapsedSeconds >= MatchSettings->TimeLimitSeconds)
		{
			HandleMatchTimeLimitReached();
		}
	}

	// Half way through the clock, each squad notices.
	if (GS != nullptr && GS->MatchPhase == EOneBrahamMatchPhase::Active && MatchSettings->TimeLimitSeconds > 0
		&& GS->PhaseElapsedSeconds >= MatchSettings->TimeLimitSeconds * 0.5f)
	{
		FOneBrahamVoiceDirector::OnMatchHalfway(GetWorld());
	}

	// Keep the survivor count fresh for the HUD.
	if (GS != nullptr && GS != nullptr && (GS->MatchPhase == EOneBrahamMatchPhase::Active || GS->MatchPhase == EOneBrahamMatchPhase::Extraction))
	{
		GS->SurvivorsRemaining = GS->GetAlivePlayerCount();
	}
}

// ==========================================================================
//  Match flow
// ==========================================================================
void AOneBrahamGameMode::SpawnMatchLoot()
{
	UOneBrahamLootSubsystem* Loot = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UOneBrahamLootSubsystem>() : nullptr;
	if (Loot == nullptr || Loot->HasSpawned() || MatchSettings == nullptr)
	{
		return;
	}

	UOneBrahamWeaponDataAsset* Registry = UOneBrahamGameSettings::Get().WeaponRegistry.LoadSynchronous();
	Loot->SpawnMatchLoot(MatchSettings, Registry);
}

void AOneBrahamGameMode::StartWarmup()
{
	// The building is stocked before anyone is let in: a desk that is empty in
	// the warmup and holds a rifle a minute later is a desk nobody checks twice.
	SpawnMatchLoot();

	SetMatchPhase(EOneBrahamMatchPhase::Warmup);
	WarmupEndTime = GetWorld()->GetTimeSeconds() + static_cast<float>(MatchSettings->WarmupSeconds);

	// The lobby tannoy: five announcements through the warmup, in order, the
	// building's ordinary morning running a few minutes ahead of its last.
	LobbyAnnouncementIndex = 0;
	PlayNextLobbyAnnouncement();
	GetWorldTimerManager().SetTimer(LobbyAnnouncementHandle, this, &AOneBrahamGameMode::PlayNextLobbyAnnouncement,
		FMath::Max(6.f, static_cast<float>(MatchSettings->WarmupSeconds) / static_cast<float>(LobbyAnnouncementCount())), true);

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

int32 AOneBrahamGameMode::LobbyAnnouncementCount()
{
	return 5;
}

FText AOneBrahamGameMode::GetLobbyAnnouncement(int32 Index)
{
	switch (Index)
	{
	case 0: return NSLOCTEXT("OneBraham", "Lobby_0", "Good morning, and welcome to One Braham. Visitors, please sign in at reception.");
	case 1: return NSLOCTEXT("OneBraham", "Lobby_1", "Lanyards and passes must be worn and visible at all times inside the building.");
	case 2: return NSLOCTEXT("OneBraham", "Lobby_2", "The fire alarm will be tested later this morning. No action is required.");
	case 3: return NSLOCTEXT("OneBraham", "Lobby_3", "Would the owner of a grey estate in loading bay two please return to their vehicle.");
	case 4: return NSLOCTEXT("OneBraham", "Lobby_4", "The Skydeck remains closed to staff and visitors until further notice. Thank you.");
	default: return FText::GetEmpty();
	}
}

void AOneBrahamGameMode::PlayNextLobbyAnnouncement()
{
	if (LobbyAnnouncementIndex >= LobbyAnnouncementCount() || GetMatchPhase() != EOneBrahamMatchPhase::Warmup)
	{
		GetWorldTimerManager().ClearTimer(LobbyAnnouncementHandle);
		return;
	}

	Announce(GetLobbyAnnouncement(LobbyAnnouncementIndex++), true);
}

void AOneBrahamGameMode::HandleWarmupComplete()
{
	StartActiveMatch();
}

void AOneBrahamGameMode::StartActiveMatch()
{
	// Usually already done in the warmup; a mode with no warmup comes straight here.
	SpawnMatchLoot();

	SetMatchPhase(EOneBrahamMatchPhase::Active);
	MatchStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().ClearTimer(LobbyAnnouncementHandle);
	FOneBrahamVoiceDirector::OnMatchStarted(GetWorld());

	Announce(FText::FromString(TEXT("The lift lobby is open. Ascend.")), true);

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

	PendingWinningSquadId = WinningSquadId;

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
				ObjectiveText = TEXT("Security purge on lower floors - get above floor 13");
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

	FinishMatch(EOneBrahamMatchEnding::Extracted, WinningTeam, WinningSquadName, ExtractedCount, EOneBrahamExtractionFailReason::None);
}

void AOneBrahamGameMode::CompleteMatchNoSurvivors()
{
	if (bMatchFinished)
	{
		return;
	}

	FinishMatch(EOneBrahamMatchEnding::NoSurvivors, EOneBrahamTeam::None, FString(), 0, EOneBrahamExtractionFailReason::CallersEliminated);
}

void AOneBrahamGameMode::FinishMatch(EOneBrahamMatchEnding Ending, EOneBrahamTeam WinningTeam, const FString& WinningSquadName, int32 ExtractedCount, EOneBrahamExtractionFailReason FailReason)
{
	const bool bNoSurvivors = Ending == EOneBrahamMatchEnding::NoSurvivors;

	bMatchFinished = true;
	SetMatchPhase(EOneBrahamMatchPhase::Complete);

	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr)
	{
		return;
	}

	GS->MatchResult = BuildMatchResult(WinningTeam, WinningSquadName, bNoSurvivors, ExtractedCount);
	GS->MatchResult.Ending = Ending;
	GS->OnMatchResultReady.Broadcast(GS->MatchResult);

	// End-of-match announcements: the Tannoy gets the last word, as it should.
	FText Announcement;
	if (bNoSurvivors)
	{
		Announcement = FText::FromString(TEXT("All staff accounted for. There are no survivors. Please await further instruction."));
	}
	else if (Ending == EOneBrahamMatchEnding::LastSquadStanding)
	{
		Announcement = FText::Format(
			FText::FromString(TEXT("{0}, you are the only occupants remaining. The building is yours. Nobody else's.")),
			FText::FromString(WinningSquadName.IsEmpty() ? TEXT("Remaining staff") : WinningSquadName));
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
	FOneBrahamVoiceDirector::OnMatchEnded(GetWorld(), PendingWinningSquadId);

	OB_MATCH_LOG("MATCH COMPLETE: ending=%s winner=%s squad='%s' noSurvivors=%d extracted=%d duration=%.1fs reason=%s",
		*UEnum::GetValueAsString(Ending), *UEnum::GetValueAsString(WinningTeam), *WinningSquadName, bNoSurvivors ? 1 : 0, ExtractedCount,
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

	// One summary per player, from the player state itself. This used to be
	// assembled from the squad roster, which carries kills, downs and deaths and
	// nothing else: assists, revives, damage, floors, time and score were all
	// zero on the results screen, which Docs/08 says is about exactly those.
	// Squad order, so the screen reads squad by squad as it is designed to.
	if (const AOneBrahamGameState* GS = GetOBGameState())
	{
		TArray<const AOneBrahamPlayerState*> States;
		for (const APlayerState* Base : GS->PlayerArray)
		{
			if (const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Base))
			{
				States.Add(State);
			}
		}

		States.StableSort([](const AOneBrahamPlayerState& A, const AOneBrahamPlayerState& B)
		{
			return A.SquadId != B.SquadId ? A.SquadId < B.SquadId : A.GetPlayerId() < B.GetPlayerId();
		});

		for (const AOneBrahamPlayerState* State : States)
		{
			Result.PlayerSummaries.Add(State->MakeSummary());
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
	if (bKilledByBuilding && !Report.CauseId.IsNone())
	{
		// The building's own weapons: the feed says what it was, not "no weapon".
		Entry.WeaponId = Report.CauseId;
	}

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
		FinishMatch(EOneBrahamMatchEnding::ScoreLimit, WinningTeam,
			WinningTeam == EOneBrahamTeam::InternalStaff ? TEXT("Internal Staff") : TEXT("External Contractors"),
			0, EOneBrahamExtractionFailReason::None);
	}
}

void AOneBrahamGameMode::HandleMatchTimeLimitReached()
{
	AOneBrahamGameState* GS = GetOBGameState();
	if (GS == nullptr || GS->TeamScores.Num() == 0)
	{
		CompleteMatchNoSurvivors();
		return;
	}

	int32 BestIndex = 0;
	for (int32 Index = 1; Index < GS->TeamScores.Num(); ++Index)
	{
		if (GS->TeamScores[Index] > GS->TeamScores[BestIndex])
		{
			BestIndex = Index;
		}
	}

	const bool bDraw = GS->TeamScores.Num() > 1 && GS->TeamScores[0] == GS->TeamScores[1];
	const EOneBrahamTeam WinningTeam = bDraw
		? EOneBrahamTeam::None
		: (BestIndex == 0 ? EOneBrahamTeam::InternalStaff : EOneBrahamTeam::ExternalContractors);

	FinishMatch(EOneBrahamMatchEnding::TimeOut, WinningTeam,
		bDraw ? TEXT("Draw") : (BestIndex == 0 ? TEXT("Internal Staff") : TEXT("External Contractors")),
		0, EOneBrahamExtractionFailReason::LoiterExpired);
}

void AOneBrahamGameMode::EvaluateMatchEndConditions()
{
	if (bMatchFinished || MatchSettings == nullptr)
	{
		return;
	}

	// The roster is a snapshot of the player states, taken on join and leave. A
	// death or a down changes the player state only, so without this rebuild
	// every member still read Alive here and neither "no survivors" nor "last
	// squad standing" could ever fire, and the squad panel showed the dead as up.
	RebuildSquadInfo();

	// ASCENT ends in "no survivors" when every squad is eliminated before any
	// extraction. FIRE DRILL never ends this way because players respawn.
	if (MatchSettings->Mode != EOneBrahamMatchMode::Ascent)
	{
		return;
	}

	// Nothing ends a match that has not started: the warmup plaza is for
	// arriving, and a squad that has not filled yet is not "eliminated".
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

	// Squads that ever had anyone in them: an empty roster slot is not an
	// opponent, and a match that started with one squad (bot practice with the
	// bots disabled, a QA box) must not be "won" by it the moment it starts.
	int32 PopulatedSquads = 0;
	int32 AliveSquads = 0;
	const FOneBrahamSquadInfo* LastAlive = nullptr;

	for (const FOneBrahamSquadInfo& Squad : GS->Squads)
	{
		if (Squad.Members.Num() == 0)
		{
			continue;
		}

		++PopulatedSquads;

		if (Squad.IsAlive())
		{
			++AliveSquads;
			LastAlive = &Squad;
		}
	}

	const bool bExtractionInProgress = GS->GetExtractionComponent() != nullptr &&
		GS->GetExtractionComponent()->IsExtractionActive();

	if (AliveSquads == 0 && !bExtractionInProgress)
	{
		CompleteMatchNoSurvivors();
		return;
	}

	if (AliveSquads == 2 && PopulatedSquads >= 3)
	{
		FOneBrahamVoiceDirector::OnTwoSquadsLeft(GetWorld());
	}

	// Docs/06 section 7: every other squad eliminated, extraction not completed.
	// The survivors win where they stand; making them hold the winch against
	// nobody for 30 s is a wait, not a game.
	if (AliveSquads == 1 && PopulatedSquads >= 2 && LastAlive != nullptr)
	{
		const FString SquadName = LastAlive->SquadName.IsEmpty()
			? GetSquadDisplayName(LastAlive->SquadId)
			: LastAlive->SquadName.ToString();

		int32 Survivors = 0;
		for (const FOneBrahamSquadMemberInfo& Member : LastAlive->Members)
		{
			Survivors += Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed ? 1 : 0;
		}

		PendingWinningSquadId = LastAlive->SquadId;
		FinishMatch(EOneBrahamMatchEnding::LastSquadStanding, LastAlive->Team, SquadName, Survivors,
			EOneBrahamExtractionFailReason::None);
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

	FinishMatch(EOneBrahamMatchEnding::Extracted, Team, SquadName, MatchSettings ? MatchSettings->PlayersPerTeam : 4, EOneBrahamExtractionFailReason::None);
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

		// --- Enemy proximity penalty -------------------------------------
		// A spawn within 12 m of a live enemy is a free kill; within 30 m is
		// still unfair. This is the single most important spawn rule in the game.
		float NearestEnemySq = TNumericLimits<float>::Max();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const AOneBrahamPlayerController* OtherPC = Cast<AOneBrahamPlayerController>(It->Get());
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

			if (bEnemy)
			{
				NearestEnemySq = FMath::Min(NearestEnemySq, FVector::DistSquared2D(Spawn->GetActorLocation(), OtherCharacter->GetActorLocation()));
			}
			else
			{
				// Teammates nearby are a *bonus*: spawning next to your squad is
				// exactly what ASCENT wants.
				const float SquadmateDistSq = FVector::DistSquared2D(Spawn->GetActorLocation(), OtherCharacter->GetActorLocation());
				if (SquadmateDistSq < FMath::Square(1500.f))
				{
					Weight *= (OtherState->SquadId == RequestingSquad) ? 2.2f : 1.4f;
				}
			}
		}

		if (NearestEnemySq < FMath::Square(1200.f))
		{
			continue; // hard veto
		}
		if (NearestEnemySq < FMath::Square(3000.f))
		{
			Weight *= 0.15f;
		}

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
		// Relax the enemy-proximity rules rather than fail to spawn. Better a
		// slightly unfair spawn than a player stuck in the void.
		for (AOneBrahamSpawnPoint* Spawn : SpawnPoints)
		{
			if (Spawn != nullptr && Spawn->IsAvailable(Now))
			{
				Candidates.Add(Spawn);
				Weights.Add(1.f);
				TotalWeight += 1.f;
			}
		}
	}

	if (Candidates.Num() == 0)
	{
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

void AOneBrahamGameMode::QueueRespawn(AController* Controller, float DelaySeconds)
{
	if (Controller == nullptr)
	{
		return;
	}

	PendingRespawns.Add(Controller, FMath::Max(0.1f, DelaySeconds));
}

bool AOneBrahamGameMode::PlayerCanRestart_Implementation(AController* Player)
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

	// Name, archetype, outfit and clan tag are the joining player's own, and they
	// arrive from their machine (AOneBrahamPlayerController::SendProfileToServer,
	// validated in ServerSetAppearance). This used to copy *this* machine's
	// profile onto every player who joined, and the map URL's Name= and
	// Archetype= onto all of them too: on a listen host, twelve copies of the
	// host. What is set here is only the default until their choice lands.
	State->SessionMode = MatchSettings ? MatchSettings->Mode : EOneBrahamMatchMode::Ascent;

	// A stable per-player default from the registry's own cast, so two players
	// never look identical by accident before their own choice arrives. The cast
	// used to be a second, hand-typed list of six names here.
	if (!State->ArchetypeTag.IsValid())
	{
		const UOneBrahamCharacterDataAsset* Registry = UOneBrahamCharacterDataAsset::LoadRegistry();
		const int32 CastSize = Registry != nullptr ? Registry->Archetypes.Num() : 0;

		if (const FOneBrahamArchetypeDefinition* Default = CastSize > 0
			? Registry->FindArchetypeByIndex(FMath::Abs(State->GetPlayerId()) % CastSize)
			: nullptr)
		{
			State->ArchetypeTag = Default->ArchetypeTag;
		}
	}

	State->RemainingRevives = MatchSettings ? MatchSettings->MaxRevivesPerPlayerPerRound : 2;

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
void AOneBrahamGameMode::PopulateWithBots(int32 TotalBots)
{
	// Bot practice is explicitly NOT the shipping ASCENT experience: ASCENT has
	// no AI manager. Bots exist so QA can test the map, the weapon balance and
	// the extraction flow without 11 other people.
	UWorld* World = GetWorld();
	if (World == nullptr || TotalBots <= 0)
	{
		return;
	}

	bBotPracticeMode = true;

	// Dealt round the squads like cards: eleven bots across three squads is
	// 4/4/3, and never more than the squads can hold.
	int32 BotsSpawned = 0;
	const int32 TeamsToFill = FMath::Max(1, SquadToTeamMap.Num());
	const int32 SquadSize = MatchSettings != nullptr ? FMath::Max(1, MatchSettings->PlayersPerTeam) : 4;
	const int32 Total = FMath::Min(TotalBots, TeamsToFill * SquadSize);

	for (int32 Dealt = 0; Dealt < Total; ++Dealt)
	{
		const int32 TeamIndex = Dealt % TeamsToFill;
		const int32 Index = Dealt / TeamsToFill;
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AOneBrahamBotController* Bot = World->SpawnActor<AOneBrahamBotController>(
				AOneBrahamBotController::StaticClass(), FTransform::Identity, SpawnParams);

			if (Bot == nullptr)
			{
				continue;
			}

			// Bot difficulty scales with the match: practice bots are deliberately
			// mediocre so the map, not the AI, is what is being tested.
			Bot->BotSkill = FMath::Clamp(0.35f + 0.05f * Index, 0.f, 1.f);
			Bot->PreferredTeam = GetTeamForSquad(TeamIndex);
			++BotsSpawned;
		}
	}

	OB_MATCH_LOG("Bot practice mode: spawned %d bots across %d teams.", BotsSpawned, TeamsToFill);
}
