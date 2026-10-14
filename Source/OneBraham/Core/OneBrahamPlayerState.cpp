// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamPlayerState.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamGameState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AOneBrahamPlayerState::AOneBrahamPlayerState()
{
	// PlayerState replicates to everyone; keep its net update rate modest and
	// let the pawn carry the fast-moving data.
	SetNetUpdateFrequency(20.f);
	SetMinNetUpdateFrequency(5.f);

	// Squad leaders are promoted by the GameMode; a fresh player assumes they
	// are not a leader until told otherwise.
	bIsSquadLeader = false;
}

void AOneBrahamPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamPlayerState, Team);
	DOREPLIFETIME(AOneBrahamPlayerState, SquadId);
	DOREPLIFETIME(AOneBrahamPlayerState, bIsSquadLeader);
	DOREPLIFETIME(AOneBrahamPlayerState, ArchetypeTag);
	DOREPLIFETIME(AOneBrahamPlayerState, SessionMode);
	DOREPLIFETIME(AOneBrahamPlayerState, ClanTag);
	DOREPLIFETIME(AOneBrahamPlayerState, LoadoutPrimaryWeaponId);
	DOREPLIFETIME(AOneBrahamPlayerState, LoadoutSidearmWeaponId);
	DOREPLIFETIME(AOneBrahamPlayerState, LoadoutUtilityIds);
	DOREPLIFETIME(AOneBrahamPlayerState, LoadoutWeaponSkinId);
	DOREPLIFETIME(AOneBrahamPlayerState, bLoadoutReceived);
	DOREPLIFETIME(AOneBrahamPlayerState, LifeState);
	DOREPLIFETIME(AOneBrahamPlayerState, Health);
	DOREPLIFETIME(AOneBrahamPlayerState, MaxHealth);
	DOREPLIFETIME(AOneBrahamPlayerState, RemainingRevives);
	DOREPLIFETIME(AOneBrahamPlayerState, CurrentFloor);
	DOREPLIFETIME(AOneBrahamPlayerState, Kills);
	DOREPLIFETIME(AOneBrahamPlayerState, Downs);
	DOREPLIFETIME(AOneBrahamPlayerState, Assists);
	DOREPLIFETIME(AOneBrahamPlayerState, Deaths);
	DOREPLIFETIME(AOneBrahamPlayerState, RevivesPerformed);
	DOREPLIFETIME(AOneBrahamPlayerState, DamageDealt);
	DOREPLIFETIME(AOneBrahamPlayerState, ObjectiveScore);
	DOREPLIFETIME(AOneBrahamPlayerState, LastSpawnServerTime);

	// Voice state is only relevant to squadmates and the player themselves.
	DOREPLIFETIME_CONDITION(AOneBrahamPlayerState, VoiceChannel, COND_OwnerOnly);
	DOREPLIFETIME(AOneBrahamPlayerState, bSelfMuted);
	DOREPLIFETIME(AOneBrahamPlayerState, bIsTransmitting);
	DOREPLIFETIME_CONDITION(AOneBrahamPlayerState, MutedPlayerIds, COND_OwnerOnly);
	DOREPLIFETIME(AOneBrahamPlayerState, ReplicatedCosmetics);
}

void AOneBrahamPlayerState::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Rebuilt every send so no write site has to remember to sync; replication
	// still only transmits when the array actually differs.
	ReplicatedCosmetics.Reset(EquippedCosmetics.Num());
	for (const TPair<FName, FName>& Pair : EquippedCosmetics)
	{
		ReplicatedCosmetics.Add({ Pair.Key, Pair.Value });
	}
}

void AOneBrahamPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Seamless travel: keep squad/team/score/voice/cosmetics across the map change
	// so the end-of-match screen and the next lobby are correct.
	if (AOneBrahamPlayerState* OneBrahamState = Cast<AOneBrahamPlayerState>(PlayerState))
	{
		OneBrahamState->Team = Team;
		OneBrahamState->SquadId = SquadId;
		OneBrahamState->bIsSquadLeader = bIsSquadLeader;
		OneBrahamState->ArchetypeTag = ArchetypeTag;
		OneBrahamState->SessionMode = SessionMode;
		OneBrahamState->ClanTag = ClanTag;
		OneBrahamState->LifeState = EOneBrahamLifeState::Alive;
		OneBrahamState->Health = MaxHealth;
		OneBrahamState->MaxHealth = MaxHealth;
		OneBrahamState->RemainingRevives = 2;
		OneBrahamState->CurrentFloor = 0;
		OneBrahamState->Kills = Kills;
		OneBrahamState->Downs = Downs;
		OneBrahamState->Assists = Assists;
		OneBrahamState->Deaths = Deaths;
		OneBrahamState->RevivesPerformed = RevivesPerformed;
		OneBrahamState->DamageDealt = DamageDealt;
		OneBrahamState->ObjectiveScore = ObjectiveScore;
		OneBrahamState->VoiceChannel = VoiceChannel;
		OneBrahamState->bSelfMuted = bSelfMuted;
		OneBrahamState->MutedPlayerIds = MutedPlayerIds;
		OneBrahamState->EquippedCosmetics = EquippedCosmetics;
	}
}

void AOneBrahamPlayerState::ClientInitialize(AController* C)
{
	Super::ClientInitialize(C);

	// The GameMode stashes the archetype + cosmetics chosen in the lobby into the
	// PlayerState before possession; re-broadcast so the character mesh updates
	// even if the OnRep fired before the pawn existed.
	OnRep_Archetype();
	OnRep_Cosmetics();
}

AOneBrahamCharacter* AOneBrahamPlayerState::GetOneBrahamCharacter() const
{
	return GetPawn() ? Cast<AOneBrahamCharacter>(GetPawn()) : nullptr;
}

bool AOneBrahamPlayerState::IsFriendlyWith(const AOneBrahamPlayerState* Other) const
{
	if (Other == nullptr || Other == this)
	{
		return Other == this;
	}

	return OneBrahamTags::AreTeamsFriendly(Team, Other->Team, SessionMode);
}

// ==========================================================================
//  Scoring
// ==========================================================================
void AOneBrahamPlayerState::ServerRegisterKill(int32 ScorePerKill)
{
	if (!HasAuthority())
	{
		return;
	}

	++Kills;
	SetScore(GetScore() + static_cast<float>(ScorePerKill));
	ForceNetUpdate();
}

void AOneBrahamPlayerState::ServerRegisterAssist()
{
	if (!HasAuthority())
	{
		return;
	}

	++Assists;
	// Assists are worth a third of a kill; the Fire Drill scoreboard rounds down.
	SetScore(GetScore() + 0.33f);
	ForceNetUpdate();
}

void AOneBrahamPlayerState::ServerRegisterDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	++Deaths;
	ForceNetUpdate();
}

void AOneBrahamPlayerState::ServerRegisterDamage(float Amount)
{
	if (!HasAuthority())
	{
		return;
	}

	DamageDealt += FMath::Max(0.f, Amount);
	// Damage is aggregated on the PlayerState but only sent when the scoreboard
	// opens, so no ForceNetUpdate here: the 20 Hz net rate covers it.
}

void AOneBrahamPlayerState::ServerRegisterRevive()
{
	if (!HasAuthority())
	{
		return;
	}

	++RevivesPerformed;
	SetScore(GetScore() + 2.f);
	ForceNetUpdate();
}

FOneBrahamPlayerSummary AOneBrahamPlayerState::MakeSummary() const
{
	FOneBrahamPlayerSummary Summary;
	Summary.PlayerName = GetPlayerName();
	Summary.ArchetypeTag = ArchetypeTag;
	Summary.Team = Team;
	Summary.Kills = Kills;
	Summary.Downs = Downs;
	Summary.Assists = Assists;
	Summary.Deaths = Deaths;
	Summary.Revives = RevivesPerformed;
	Summary.DamageDealt = DamageDealt;
	Summary.FloorsClimbed = CurrentFloor;
	Summary.TimeAliveSeconds = 0.f;
	Summary.bExtracted = (LifeState == EOneBrahamLifeState::Extracted);
	Summary.Score = FMath::RoundToInt(GetScore());
	return Summary;
}

// ==========================================================================
//  Voice
// ==========================================================================
void AOneBrahamPlayerState::ServerSetVoiceChannel_Implementation(EOneBrahamVoiceChannel NewChannel)
{
	if (!HasAuthority())
	{
		return;
	}

	// Spectators and dead players can only use their team channel: a dead squad
	// must not be able to give live positional intel over proximity chat.
	const bool bIsEliminated = IsEliminated();
	if (bIsEliminated && NewChannel == EOneBrahamVoiceChannel::Proximity)
	{
		return;
	}

	VoiceChannel = NewChannel;
	ForceNetUpdate();
}

void AOneBrahamPlayerState::ServerSetSelfMuted_Implementation(bool bMuted)
{
	if (!HasAuthority())
	{
		return;
	}

	bSelfMuted = bMuted;
	if (bMuted)
	{
		MulticastSetTransmitting(false);
	}
	ForceNetUpdate();
}

void AOneBrahamPlayerState::ServerSetPlayerMuted_Implementation(int32 TargetPlayerId, bool bMuted)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bMuted)
	{
		if (!MutedPlayerIds.Contains(TargetPlayerId))
		{
			MutedPlayerIds.Add(TargetPlayerId);
		}
	}
	else
	{
		MutedPlayerIds.Remove(TargetPlayerId);
	}

	// Ask the target to rebuild its suppressed-listener cache so we stop
	// receiving their packets entirely, rather than discarding them locally.
	if (const UWorld* World = GetWorld())
	{
		if (AOneBrahamGameState* GS = World->GetGameState<AOneBrahamGameState>())
		{
			for (APlayerState* Other : GS->PlayerArray)
			{
				AOneBrahamPlayerState* OtherState = Cast<AOneBrahamPlayerState>(Other);
				if (OtherState == nullptr || OtherState->GetPlayerId() != TargetPlayerId)
				{
					continue;
				}

				if (bMuted)
				{
					OtherState->MutedByPlayerIds.AddUnique(GetPlayerId());
				}
				else
				{
					OtherState->MutedByPlayerIds.Remove(GetPlayerId());
				}
			}
		}
	}

	ForceNetUpdate();
}

void AOneBrahamPlayerState::MulticastSetTransmitting_Implementation(bool bTransmitting)
{
	bIsTransmitting = bTransmitting;
}

// ==========================================================================
//  OnRep
// ==========================================================================
void AOneBrahamPlayerState::OnRep_Team()
{
	OB_NET_LOG("PlayerState '%s' team -> %s", *GetPlayerName(), *UEnum::GetValueAsString(Team));
}

void AOneBrahamPlayerState::OnRep_SquadId()
{
	OB_NET_LOG("PlayerState '%s' squad -> %d", *GetPlayerName(), SquadId);
}

void AOneBrahamPlayerState::OnRep_Archetype()
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->ApplyArchetype(ArchetypeTag);
	}
}

void AOneBrahamPlayerState::OnRep_LifeState()
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->ApplyLifeStateVisuals(LifeState);
	}
}

void AOneBrahamPlayerState::OnRep_VoiceChannel()
{
	OB_VOICE_LOG("Voice channel is now %s", *UEnum::GetValueAsString(VoiceChannel));
}

bool AOneBrahamPlayerState::StoreLoadout(FName PrimaryWeaponId, FName SidearmWeaponId,
	const TArray<FName>& UtilityIds, FName WeaponSkinId)
{
	if (!HasAuthority())
	{
		return false;
	}

	// The ids are names a client chose, so they are treated as input: length
	// capped, count capped, and every one of them resolved by whoever grants it.
	// Nothing here decides whether an id is *real* - the weapon registry does
	// that when the weapon is spawned - but a client cannot use this to write an
	// arbitrary string into replicated state.
	auto SanitiseId = [](FName Id) -> FName
	{
		return Id.ToString().Len() <= 24 ? Id : NAME_None;
	};

	LoadoutPrimaryWeaponId = SanitiseId(PrimaryWeaponId);
	LoadoutSidearmWeaponId = SanitiseId(SidearmWeaponId);
	LoadoutWeaponSkinId = SanitiseId(WeaponSkinId);

	LoadoutUtilityIds.Reset();
	const int32 UtilityCap = 4;
	for (const FName& UtilityId : UtilityIds)
	{
		if (LoadoutUtilityIds.Num() >= UtilityCap)
		{
			break;
		}

		if (!SanitiseId(UtilityId).IsNone())
		{
			LoadoutUtilityIds.Add(UtilityId);
		}
	}

	const bool bFirstReceipt = !bLoadoutReceived;
	bLoadoutReceived = true;
	ForceNetUpdate();

	OB_LOG("Loadout for '%s': %s / %s, %d utility items, skin '%s'%s.",
		*GetPlayerName(),
		LoadoutPrimaryWeaponId.IsNone() ? TEXT("none") : *LoadoutPrimaryWeaponId.ToString(),
		LoadoutSidearmWeaponId.IsNone() ? TEXT("none") : *LoadoutSidearmWeaponId.ToString(),
		LoadoutUtilityIds.Num(),
		LoadoutWeaponSkinId.IsNone() ? TEXT("none") : *LoadoutWeaponSkinId.ToString(),
		bFirstReceipt ? TEXT(" (first)") : TEXT(" (stored for the next life)"));

	return bFirstReceipt;
}

void AOneBrahamPlayerState::OnRep_Cosmetics()
{
	EquippedCosmetics.Reset();
	for (const FOneBrahamNamePairEntry& Entry : ReplicatedCosmetics)
	{
		EquippedCosmetics.Add(Entry.Key, Entry.Value);
	}

	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->ApplyCosmetics(EquippedCosmetics);
	}
}
