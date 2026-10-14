// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameState.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"

AOneBrahamGameState::AOneBrahamGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10 Hz is plenty for match bookkeeping

	LockdownComponent = CreateDefaultSubobject<UOneBrahamLockdownComponent>(TEXT("LockdownComponent"));
	ExtractionComponent = CreateDefaultSubobject<UOneBrahamExtractionComponent>(TEXT("ExtractionComponent"));
}

void AOneBrahamGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamGameState, MatchMode);
	DOREPLIFETIME(AOneBrahamGameState, MatchPhase);
	DOREPLIFETIME(AOneBrahamGameState, ServerName);
	DOREPLIFETIME(AOneBrahamGameState, bIsDedicatedServerMatch);
	DOREPLIFETIME(AOneBrahamGameState, PhaseElapsedSeconds);
	DOREPLIFETIME(AOneBrahamGameState, MatchTimeLimitSeconds);
	DOREPLIFETIME(AOneBrahamGameState, Squads);
	DOREPLIFETIME(AOneBrahamGameState, TeamScores);
	DOREPLIFETIME(AOneBrahamGameState, SurvivorsRemaining);
	DOREPLIFETIME(AOneBrahamGameState, KillFeed);
	DOREPLIFETIME(AOneBrahamGameState, MatchResult);
}

void AOneBrahamGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// PhaseElapsedSeconds is used for the HUD clock; replicating it every frame
	// would waste ~4 KB/min of bandwidth across 16 clients for no benefit.
	if (HasAuthority())
	{
		if (MatchPhase == EOneBrahamMatchPhase::Active || MatchPhase == EOneBrahamMatchPhase::Extraction)
		{
			PhaseElapsedSeconds += DeltaSeconds;
		}
	}
}

void AOneBrahamGameState::SetMatchSettings(UOneBrahamMatchSettingsAsset* InSettings)
{
	if (!HasAuthority() || InSettings == nullptr)
	{
		return;
	}

	MatchSettings = InSettings;
	MatchMode = InSettings->Mode;
	MatchTimeLimitSeconds = InSettings->bScoreByTimeLimit ? static_cast<float>(InSettings->TimeLimitSeconds) : 0.f;

	// FIRE DRILL uses two fixed teams; ASCENT uses squads.
	if (MatchMode == EOneBrahamMatchMode::FireDrill)
	{
		TeamScores.Init(0, 2);
	}
	else if (TeamScores.Num() == 0)
	{
		TeamScores.Init(0, InSettings->TeamCount);
	}

	OB_MATCH_LOG("Match settings applied: mode=%s teams=%d teamSize=%d phase=%d",
		*UEnum::GetValueAsString(MatchMode), InSettings->TeamCount, InSettings->PlayersPerTeam, static_cast<int32>(MatchPhase));
}

// ==========================================================================
//  Replication callbacks
// ==========================================================================
void AOneBrahamGameState::OnRep_MatchMode()
{
	OB_MATCH_LOG("Client received match mode: %s", *UEnum::GetValueAsString(MatchMode));
}

void AOneBrahamGameState::OnRep_MatchPhase()
{
	OB_MATCH_LOG("Client received match phase: %s", *UEnum::GetValueAsString(MatchPhase));
	OnMatchPhaseChanged.Broadcast(MatchPhase);
}

void AOneBrahamGameState::OnRep_Squads()
{
	OnSquadsChanged.Broadcast(FText::GetEmpty());
}

void AOneBrahamGameState::OnRep_TeamScores()
{
}

void AOneBrahamGameState::OnRep_KillFeed()
{
	// Only broadcast rows the local client has not seen yet; the array is capped
	// at MaxKillFeedEntries so a burst of kills does not spray the HUD.
	if (KillFeed.Num() > LastAcknowledgedKillFeedCount)
	{
		for (int32 Index = LastAcknowledgedKillFeedCount; Index < KillFeed.Num(); ++Index)
		{
			OnKillFeedEntryAdded.Broadcast(KillFeed[Index]);
		}
	}
	else if (KillFeed.Num() > 0 && LastAcknowledgedKillFeedCount == 0)
	{
		// First replication after joining: show the most recent entry only.
		OnKillFeedEntryAdded.Broadcast(KillFeed.Last());
	}

	LastAcknowledgedKillFeedCount = KillFeed.Num();
}

void AOneBrahamGameState::OnRep_MatchResult()
{
	const bool bHasResult = MatchResult.Mode == MatchMode &&
		(MatchResult.WinningTeam != EOneBrahamTeam::None || MatchResult.bNoSurvivors);

	if (bHasResult || MatchResult.MatchDurationSeconds > 0.f)
	{
		OnMatchResultReady.Broadcast(MatchResult);
	}
}

// ==========================================================================
//  Helpers
// ==========================================================================
FOneBrahamSquadInfo AOneBrahamGameState::GetSquadInfo(int32 SquadId) const
{
	for (const FOneBrahamSquadInfo& Squad : Squads)
	{
		if (Squad.SquadId == SquadId)
		{
			return Squad;
		}
	}
	return FOneBrahamSquadInfo();
}

int32 AOneBrahamGameState::GetSquadIdForPlayer(int32 PlayerId) const
{
	for (const FOneBrahamSquadInfo& Squad : Squads)
	{
		for (const FOneBrahamSquadMemberInfo& Member : Squad.Members)
		{
			if (Member.PlayerId == PlayerId)
			{
				return Squad.SquadId;
			}
		}
	}
	return INDEX_NONE;
}

bool AOneBrahamGameState::IsSquadEliminated(int32 SquadId) const
{
	for (const FOneBrahamSquadInfo& Squad : Squads)
	{
		if (Squad.SquadId == SquadId)
		{
			return Squad.bEliminated;
		}
	}
	return true;
}

int32 AOneBrahamGameState::GetAlivePlayerCount() const
{
	int32 Count = 0;
	for (const FOneBrahamSquadInfo& Squad : Squads)
	{
		for (const FOneBrahamSquadMemberInfo& Member : Squad.Members)
		{
			if (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed)
			{
				++Count;
			}
		}
	}
	return Count;
}

void AOneBrahamGameState::AddKillFeedEntry(const FOneBrahamKillFeedEntry& Entry)
{
	if (!HasAuthority())
	{
		return;
	}

	FOneBrahamKillFeedEntry NewEntry = Entry;
	NewEntry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	KillFeed.Add(NewEntry);
	while (KillFeed.Num() > MaxKillFeedEntries)
	{
		KillFeed.RemoveAt(0, 1, EAllowShrinking::No);
	}

	// Server also fires the delegate so a listen host sees its own killfeed
	// without waiting for a round trip.
	OnKillFeedEntryAdded.Broadcast(NewEntry);
	LastAcknowledgedKillFeedCount = KillFeed.Num();
}

void AOneBrahamGameState::MulticastAnnounce_Implementation(const FText& Announcement, bool bPlayTannoySound)
{
	OnAnnouncement.Broadcast(Announcement);

	// The Tannoy itself is driven by the audio manager on each client so the
	// announcement pans from the correct ceiling speaker near the listener.
	UE_LOG(LogOneBrahamAudio, Verbose, TEXT("[OB.ANNOUNCE] %s (tannoy=%s)"),
		*Announcement.ToString(), bPlayTannoySound ? TEXT("yes") : TEXT("no"));
}

void AOneBrahamGameState::MulticastWarningBanner_Implementation(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	// The HUD listens for this through UOneBrahamHUDComponent -> ShowBanner.
	// This RPC used to end at the log line below, which meant the sentence that
	// stood here - "UI listens for this" - was the only place the banner went.
	OnWarningBanner.Broadcast(Text, DurationSeconds, Colour);

	UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.BANNER] %s (%.1fs)"), *Text.ToString(), DurationSeconds);
}

void AOneBrahamGameState::LogNetBudget()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	int32 PlayerCount = 0;
	int32 RelevantActorCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->IsValid())
		{
			++PlayerCount;
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (AActor* Actor = *It)
		{
			if (Actor->GetIsReplicated() && Actor->GetLocalRole() == ROLE_Authority)
			{
				++RelevantActorCount;
			}
		}
	}

	OB_LOG("Players=%d ReplicatedActors=%d Squads=%d KillFeed=%d Phase=%s Mode=%s",
		PlayerCount, RelevantActorCount, Squads.Num(), KillFeed.Num(),
		*UEnum::GetValueAsString(MatchPhase), *UEnum::GetValueAsString(MatchMode));

	// Engine-level counters, printed so the soak-test parser can pick them up.
	if (GEngine != nullptr)
	{
		GEngine->Exec(World, TEXT("stat net"));
		GEngine->Exec(World, TEXT("stat netprofiler"));
	}
}
