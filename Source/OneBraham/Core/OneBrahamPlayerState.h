// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPlayerState.h
// ---------------------------------------------------------------------------
// Per-player replicated state: squad, archetype, life state, score, voice
// routing and cosmetic selection. Deliberately thin - anything that changes
// every frame (movement, ammo, health deltas) lives on the pawn or its
// components, because PlayerState replicates to everyone at high priority.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamPlayerState.generated.h"

class UOneBrahamCosmeticDataAsset;
class AOneBrahamCharacter;

UCLASS(BlueprintType)
class ONEBRAHAM_API AOneBrahamPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AOneBrahamPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void ClientInitialize(AController* C) override;

	// ======================================================================
	//  Identity
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_Team, BlueprintReadOnly, Category = "One Braham|Player")
	EOneBrahamTeam Team = EOneBrahamTeam::None;

	UPROPERTY(ReplicatedUsing = OnRep_SquadId, BlueprintReadOnly, Category = "One Braham|Player")
	int32 SquadId = INDEX_NONE;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	bool bIsSquadLeader = false;

	/** OB.Character.Archetype.* - purely cosmetic + voice casting, never stats. */
	UPROPERTY(ReplicatedUsing = OnRep_Archetype, BlueprintReadOnly, Category = "One Braham|Player")
	FGameplayTag ArchetypeTag;

	/** OB.Match.Mode.* of the session this player joined, used for friendly checks. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	EOneBrahamMatchMode SessionMode = EOneBrahamMatchMode::Ascent;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	FString ClanTag;

	// ======================================================================
	//  Loadout
	// ======================================================================
	/**
	 * What the player chose in the locker: the two weapons they carry, their
	 * utility, and the weapon skin.
	 *
	 * It lives on the player state rather than on the pawn because it has to
	 * outlive the pawn. A player is re-armed on every spawn - once a match in
	 * ASCENT, every forty seconds in FIRE DRILL - and the weapon component reads
	 * this every time. It is also the only copy the server trusts: the client
	 * sends it once, when it joins, and from then on the server is the authority
	 * on what this player carries.
	 *
	 * Cosmetic in the sense that matters: every weapon and every utility listed
	 * here is available to every player from their first match, so choosing one
	 * over another is a preference, not an unlock.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Loadout")
	FName LoadoutPrimaryWeaponId = NAME_None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Loadout")
	FName LoadoutSidearmWeaponId = NAME_None;

	/** Utility item ids the player asked for, e.g. Utility_Frag. Max four. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Loadout")
	TArray<FName> LoadoutUtilityIds;

	/** A cosmetic skin id. NAME_None is the issue finish the weapon ships in. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Loadout")
	FName LoadoutWeaponSkinId = NAME_None;

	/**
	 * True once the server has received a loadout from this player.
	 *
	 * Until then a spawn gets the standard-issue kit, which is what an AI bot
	 * gets for its whole life: a bot has no client to send anything, and a
	 * player who has not sent theirs yet must still be able to shoot back.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Loadout")
	bool bLoadoutReceived = false;

	/** Server: stores a sanitised loadout. Returns false if one was already held. */
	bool StoreLoadout(FName PrimaryWeaponId, FName SidearmWeaponId,
		const TArray<FName>& UtilityIds, FName WeaponSkinId);

	// ======================================================================
	//  Life state
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_LifeState, BlueprintReadOnly, Category = "One Braham|Player")
	EOneBrahamLifeState LifeState = EOneBrahamLifeState::Alive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	float Health = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	float MaxHealth = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	int32 RemainingRevives = 2;

	/** Highest floor (0 = plaza, 15 = Skydeck) this player has reached. Drives the HUD. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Player")
	int32 CurrentFloor = 0;

	// ======================================================================
	//  Score / stats
	// ======================================================================
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	int32 Kills = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	int32 Downs = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	int32 Assists = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	int32 Deaths = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	int32 RevivesPerformed = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	float DamageDealt = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	int32 ObjectiveScore = 0;

	/** Server time when this player spawned - used for the "time alive" stat. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Stats")
	float LastSpawnServerTime = 0.f;

	// ======================================================================
	//  Voice
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_VoiceChannel, BlueprintReadOnly, Category = "One Braham|Voice")
	EOneBrahamVoiceChannel VoiceChannel = EOneBrahamVoiceChannel::Proximity;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Voice")
	bool bSelfMuted = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Voice")
	bool bIsTransmitting = false;

	/** Per-listener mute list (server keeps the authoritative copy so a muted player is muted everywhere). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Voice")
	TArray<int32> MutedPlayerIds;

	/** Individual players who muted *this* player - used to suppress the outgoing packet early. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Voice")
	TArray<int32> MutedByPlayerIds;

	// ======================================================================
	//  Cosmetics (replicated so other clients render the correct outfit)
	// ======================================================================
	/** Slot tag name -> cosmetic id. Populated from UOneBrahamSaveSettings on login. */
	UPROPERTY(ReplicatedUsing = OnRep_Cosmetics, BlueprintReadOnly, Category = "One Braham|Cosmetics")
	TMap<FName, FName> EquippedCosmetics;

	// ======================================================================
	//  API
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Player")
	AOneBrahamCharacter* GetOneBrahamCharacter() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Player")
	bool IsAlive() const { return LifeState == EOneBrahamLifeState::Alive; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Player")
	bool IsDowned() const { return LifeState == EOneBrahamLifeState::Downed; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Player")
	bool IsEliminated() const { return LifeState == EOneBrahamLifeState::Dead || LifeState == EOneBrahamLifeState::Spectating; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Player")
	bool IsFriendlyWith(const AOneBrahamPlayerState* Other) const;

	/** Server: applies a kill to the scoreboard and returns the new score delta. */
	void ServerRegisterKill(int32 ScorePerKill);

	/** Server: applies an assist. */
	void ServerRegisterAssist();

	/** Server: applies a death. */
	void ServerRegisterDeath();

	/** Server: records damage dealt (used by the end-of-match screen). */
	void ServerRegisterDamage(float Amount);

	/** Server: applies a revive. */
	void ServerRegisterRevive();

	/** Client -> server: switch voice channel (proximity / squad / team). */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "One Braham|Voice")
	void ServerSetVoiceChannel(EOneBrahamVoiceChannel NewChannel);

	/** Client -> server: mute/unmute self. */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "One Braham|Voice")
	void ServerSetSelfMuted(bool bMuted);

	/** Client -> server: mute another player for this listener. */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "One Braham|Voice")
	void ServerSetPlayerMuted(int32 TargetPlayerId, bool bMuted);

	/** Server -> all: tells clients this player started/stopped talking (UI mic icon). */
	UFUNCTION(NetMulticast, Unreliable, Category = "One Braham|Voice")
	void MulticastSetTransmitting(bool bTransmitting);

	/** Snapshot used by the end-of-match screen. */
	FOneBrahamPlayerSummary MakeSummary() const;

protected:
	UFUNCTION() void OnRep_Team();
	UFUNCTION() void OnRep_SquadId();
	UFUNCTION() void OnRep_Archetype();
	UFUNCTION() void OnRep_LifeState();
	UFUNCTION() void OnRep_VoiceChannel();
	UFUNCTION() void OnRep_Cosmetics();
};
