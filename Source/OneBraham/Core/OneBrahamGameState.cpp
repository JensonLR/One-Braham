// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameState.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameInstance.h"
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
	DOREPLIFETIME(AOneBrahamGameState, MatchSettingsPath);
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
	MatchSettingsPath = GetReplicableSettingsPath(InSettings);
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

	OB_MATCH_LOG("Match settings applied: mode=%s teams=%d teamSize=%d phase=%d path='%s'",
		*UEnum::GetValueAsString(MatchMode), InSettings->TeamCount, InSettings->PlayersPerTeam, static_cast<int32>(MatchPhase),
		MatchSettingsPath.IsNull() ? TEXT("(transient - clients resolve by mode)") : *MatchSettingsPath.ToString());
}

FSoftObjectPath AOneBrahamGameState::GetReplicableSettingsPath(const UOneBrahamMatchSettingsAsset* Settings)
{
	// IsAsset() is false for anything transient or outered to something other
	// than its package - which is exactly the GameInstance's runtime fallback
	// (outer: the GameInstance) and the GameMode's own last-ditch NewObject.
	// Those have no path a client could load, so none is replicated.
	if (Settings == nullptr || !Settings->IsAsset())
	{
		return FSoftObjectPath();
	}

	return FSoftObjectPath(Settings);
}

UOneBrahamMatchSettingsAsset* AOneBrahamGameState::GetMatchSettings() const
{
	if (MatchSettings != nullptr)
	{
		return MatchSettings;
	}

	// Resolving caches a pointer, which is bookkeeping rather than a change to
	// the match state, so the getter stays const for its forty callers.
	return const_cast<AOneBrahamGameState*>(this)->ResolveClientMatchSettings();
}

UOneBrahamMatchSettingsAsset* AOneBrahamGameState::ResolveClientMatchSettings()
{
	// Re-resolve whenever what the server told us changes, so a mode or path
	// that arrives after the first read replaces the default-mode guess.
	if (ClientMatchSettings != nullptr && ClientMatchSettingsMode == MatchMode && ClientMatchSettingsSource == MatchSettingsPath)
	{
		return ClientMatchSettings;
	}

	UOneBrahamMatchSettingsAsset* Resolved = nullptr;

	// The server's exact asset first - a custom lobby can force one that is not
	// at the per-mode path. It must agree with the replicated mode; a path that
	// does not is stale and the mode wins.
	if (!MatchSettingsPath.IsNull())
	{
		Resolved = Cast<UOneBrahamMatchSettingsAsset>(MatchSettingsPath.TryLoad());
		if (Resolved != nullptr && Resolved->Mode != MatchMode)
		{
			Resolved = nullptr;
		}
	}

	// Then the same per-mode resolution the server ran: the configured override,
	// /Game/OneBraham/Data/DA_MatchSettings_<Mode>, or a runtime default built
	// from the same EnsureDefaults the server's fallback used.
	if (Resolved == nullptr)
	{
		if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
		{
			Resolved = GameInstance->GetMatchSettings(MatchMode);
		}
	}

	if (Resolved == nullptr)
	{
		return nullptr;
	}

	ClientMatchSettings = Resolved;
	ClientMatchSettingsMode = MatchMode;
	ClientMatchSettingsSource = MatchSettingsPath;

	OB_MATCH_LOG("Client resolved match settings '%s' for mode %s.",
		*GetNameSafe(Resolved), *UEnum::GetValueAsString(MatchMode));

	return ClientMatchSettings;
}

// ==========================================================================
//  Replication callbacks
// ==========================================================================
void AOneBrahamGameState::OnRep_MatchMode()
{
	OB_MATCH_LOG("Client received match mode: %s", *UEnum::GetValueAsString(MatchMode));

	// Warm the cache now rather than on the first HUD tick that asks.
	GetMatchSettings();
}

void AOneBrahamGameState::OnRep_MatchSettingsPath()
{
	GetMatchSettings();
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
