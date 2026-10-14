// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamTypes.h
// ---------------------------------------------------------------------------
// Shared enums, structs and delegates used across every ONE BRAHAM system.
// Nothing in this file may depend on a gameplay class (weapon/character/etc.)
// beyond forward declarations, so it can be included from anywhere cheaply.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "OneBrahamTypes.generated.h"

class AOneBrahamWeaponBase;

// ==========================================================================
//  Trace channels. MUST stay in sync with Config/DefaultEngine.ini
//  [/Script/Engine.CollisionProfile] DefaultChannelResponses ordering.
// ==========================================================================
UENUM(BlueprintType)
enum class EOneBrahamTraceChannel : uint8
{
	/** Bullet traces, melee, breaching. */
	Weapon = ECC_GameTraceChannel1,
	/** Breakable glass panels only. */
	Glass = ECC_GameTraceChannel2,
	/** Doors, terminals, lifts, shutters, pickups. */
	Interactable = ECC_GameTraceChannel3,
	/** Shoot-through cubicle walls, plasterboard, desk partitions. */
	Penetrable = ECC_GameTraceChannel4,
	/** Keycard / access volumes. Overlap-only. */
	AccessZone = ECC_GameTraceChannel5,
	/** Per-bone damage volumes on characters. */
	Hitbox = ECC_GameTraceChannel6,
	/** Meshes that muffle proximity voice. */
	VoiceOccluder = ECC_GameTraceChannel7,
	/** Extraction + lockdown trigger volumes. */
	Extraction = ECC_GameTraceChannel8,
	/** Extraction helicopter hull. */
	HelicopterBlocker = ECC_GameTraceChannel9,
	/** Blocks thrown grenades and breach charges. */
	ProjectileBlocker = ECC_GameTraceChannel10
};

// ==========================================================================
//  Teams, squads, modes
// ==========================================================================

/** Squads in ASCENT plus the two FIRE DRILL factions. */
UENUM(BlueprintType)
enum class EOneBrahamTeam : uint8
{
	None = 0			UMETA(DisplayName = "No Team"),
	/** ASCENT squad 1 - spawns at the Aldgate entrance. */
	Alpha = 1			UMETA(DisplayName = "Squad Alpha"),
	/** ASCENT squad 2 - spawns at the bus stop / smoking area. */
	Bravo = 2			UMETA(DisplayName = "Squad Bravo"),
	/** ASCENT squad 3 - spawns at the bike racks / service ramp. */
	Charlie = 3			UMETA(DisplayName = "Squad Charlie"),
	/** ASCENT squad 4 - overflow only, used by bot practice + 16p custom lobbies. */
	Delta = 4			UMETA(DisplayName = "Squad Delta"),
	/** FIRE DRILL faction A - people whose badges still work. */
	InternalStaff = 5	UMETA(DisplayName = "Internal Staff"),
	/** FIRE DRILL faction B - people whose badges never did. */
	ExternalContractors = 6 UMETA(DisplayName = "External Contractors"),
	/** Dead / spectating / not yet assigned. */
	Spectator = 7		UMETA(DisplayName = "Spectator")
};

UENUM(BlueprintType)
enum class EOneBrahamMatchMode : uint8
{
	/** 12 players, 3 squads of 4, ground floor to Skydeck extraction. */
	Ascent = 0		UMETA(DisplayName = "ASCENT"),
	/** 6v6 or 8v8 objective control. Internal Staff vs External Contractors. */
	FireDrill = 1	UMETA(DisplayName = "FIRE DRILL"),
	/** Post-launch 4-player co-op survival. Same map, no player opponents. */
	NightShift = 2	UMETA(DisplayName = "NIGHT SHIFT (Beta)")
};

UENUM(BlueprintType)
enum class EOneBrahamMatchPhase : uint8
{
	/** Loading, players can move but no scoring / extraction. */
	Warmup = 0,
	/** Everything live. */
	Active = 1,
	/** A squad has called the helicopter and the Skydeck fight is on. */
	Extraction = 2,
	/** Win/loss frozen, end-of-match screen up. */
	Complete = 3,
	/** Seamless travel back to the lobby map. */
	PostMatch = 4
};

UENUM(BlueprintType)
enum class EOneBrahamLifeState : uint8
{
	/** Up, shooting, can sprint. */
	Alive = 0,
	/** On the floor, crawling, can be revived or bleed out. */
	Downed = 1,
	/** Out for the round; will spectate until the match ends or (FIRE DRILL) respawn. */
	Dead = 2,
	/** Free camera spectator - eliminated players, late joiners, QA. */
	Spectating = 3,
	/** On the helicopter, extraction counted. */
	Extracted = 4
};

// ==========================================================================
//  Access control
// ==========================================================================

/** Keycard tiers, ordered. Higher tiers open every lower-tier zone. */
UENUM(BlueprintType)
enum class EOneBrahamAccessTier : uint8
{
	/** Anyone with a lanyard, i.e. anyone. Lobby, canteen, plaza. */
	Public = 0		UMETA(DisplayName = "Public"),
	/** Standard staff badge. Open-plan floors 3-8. */
	Staff = 1		UMETA(DisplayName = "Staff"),
	/** IT/Operations badge. Server & comms floor 9. */
	Secure = 2		UMETA(DisplayName = "Secure"),
	/** Executive badge. Floor 12 boardroom wing. */
	Executive = 3	UMETA(DisplayName = "Executive"),
	/** Facilities master badge. Floor 15 canteen to Skydeck stairwell. */
	Skydeck = 4		UMETA(DisplayName = "Skydeck"),
	/** Not a card: unlocked by holding a terminal. Never spawns as loot. */
	Override = 5	UMETA(DisplayName = "Override Only")
};

/** How a zone became unlocked - tracked for the killfeed, voice lines and stats. */
UENUM(BlueprintType)
enum class EOneBrahamUnlockMethod : uint8
{
	None = 0,
	/** A physical keycard was used on the reader. */
	Keycard = 1,
	/** A terminal override was held to completion. */
	Override = 2,
	/** A breaching charge destroyed the barrier. */
	Breach = 3,
	/** The lockdown director opened it as the building escalates. */
	Escalation = 4,
	/** Debug / QA. */
	Cheat = 5
};

USTRUCT(BlueprintType)
struct FOneBrahamZoneState
{
	GENERATED_BODY()

	/** Stable zone id, matches the UOneBrahamAccessZone components placed in the level. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	FName ZoneId = NAME_None;

	/** Human readable name used in UI ("Server Floor - Comms", "Skydeck Stairwell"). */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	EOneBrahamAccessTier RequiredTier = EOneBrahamAccessTier::Public;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	bool bUnlocked = false;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	EOneBrahamUnlockMethod UnlockedBy = EOneBrahamUnlockMethod::None;

	/** 0..1 progress on the terminal that is currently being held. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	float OverrideProgress = 0.f;

	/** PlayerState that completed the unlock, for the "ALEX FORCED THE SERVER DOOR" killfeed line. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Access")
	FString UnlockedByPlayerName;
};

// ==========================================================================
//  Extraction
// ==========================================================================

UENUM(BlueprintType)
enum class EOneBrahamExtractionState : uint8
{
	/** Nobody has touched a Skydeck terminal yet. */
	Inactive = 0,
	/** A squad called the helicopter; the beacon is lit and the whole building knows. */
	Broadcast = 1,
	/** 45 seconds of rotor noise and searchlight. Contested fighting on the deck. */
	Arriving = 2,
	/** Helicopter is on the pad; defenders must hold the zone for 30 seconds. */
	Holding = 3,
	/** Win. Squad is on board, match over. */
	Complete = 4,
	/** Hold was broken and the timer expired, or the calling squad was wiped. */
	Failed = 5
};

/** Reason an extraction attempt failed - drives the end-of-match screen copy. */
UENUM(BlueprintType)
enum class EOneBrahamExtractionFailReason : uint8
{
	None = 0,
	/** The extracted squad was eliminated while holding. */
	CallersEliminated = 1,
	/** Enemies contested and pushed the holding squad out of the zone. */
	Contested = 2,
	/** The helicopter ran out of loiter time and left. */
	LoiterExpired = 3,
	/** Security purge killed everyone on the deck first. */
	Purge = 4
};

// ==========================================================================
//  Lockdown
// ==========================================================================

/**
 * Building escalation stages. ASCENT uses this to squeeze players upward:
 * lower floors become uninhabitable so camping the lobby is not a strategy.
 */
UENUM(BlueprintType)
enum class EOneBrahamLockdownStage : uint8
{
	/** Everything calm. Lifts work. Announcements are friendly. */
	Normal = 0,
	/** "Attention: the building is now in lockdown." Shutters close on floors 1-3. */
	PreLockdown = 1,
	/** Fire shutters seal every stairwell below the highest occupied floor; lifts stop. */
	Lockdown = 2,
	/** Security purge on skipped floors: gas, sprinklers, lethal security response. */
	Purge = 3
};

// ==========================================================================
//  Combat / damage
// ==========================================================================

UENUM(BlueprintType)
enum class EOneBrahamHitZone : uint8
{
	Head = 0,
	Neck = 1,
	Torso = 2,
	UpperArm = 3,
	LowerArm = 4,
	Hand = 5,
	UpperLeg = 6,
	LowerLeg = 7,
	Foot = 8
};

UENUM(BlueprintType)
enum class EOneBrahamFootstepSurface : uint8
{
	Marble = 0,
	Carpet = 1,
	Concrete = 2,
	Metal = 3,
	Glass = 4,
	Wood = 5,
	Puddle = 6,
	Grating = 7,
	Fabric = 8,
	ServerRack = 9,
	Plasterboard = 10,
	Lino = 11,
	Decking = 12,
	Flesh = 13,
	BallisticGlass = 14
};

USTRUCT(BlueprintType)
struct FOneBrahamDamageReport
{
	GENERATED_BODY()

	/** Damage before range/zone/armour modifiers. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float BaseDamage = 0.f;

	/** Damage actually applied after every modifier. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float FinalDamage = 0.f;

	/** Damage after passing through one penetrable surface (cubicle walls etc). */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bWasPenetrated = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	EOneBrahamHitZone HitZone = EOneBrahamHitZone::Torso;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FName HitBoneName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FVector_NetQuantize HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FVector_NetQuantizeNormal ShotDirection = FVector::ForwardVector;

	/** Distance from muzzle to impact, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float Distance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bLethal = false;

	/** For damage with no instigating player: what in the building did it
	 *  ("LiftDoors", "Fall", "Purge"). The kill feed names it. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FName CauseId = NAME_None;
};

/** Everything the server and client need to describe a hit for VFX/audio/sound-occlusion. */
USTRUCT(BlueprintType)
struct FOneBrahamImpactInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	FVector_NetQuantizeNormal Normal = FVector::UpVector;

	/** Surface enum resolved from the hit physical material. */
	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	EOneBrahamFootstepSurface Surface = EOneBrahamFootstepSurface::Concrete;

	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	bool bHitCharacter = false;

	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	bool bHitGlass = false;

	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	bool bPenetrated = false;

	UPROPERTY(BlueprintReadOnly, Category = "Impact")
	FName HitBoneName = NAME_None;
};

// ==========================================================================
//  Weapons
// ==========================================================================

UENUM(BlueprintType)
enum class EOneBrahamWeaponCategory : uint8
{
	Pistol = 0,
	CompactPistol = 1,
	SuppressedPistol = 2,
	SMG = 3,
	PDW = 4,
	PumpShotgun = 5,
	SemiAutoShotgun = 6,
	Carbine556 = 7,
	CompactAssaultRifle = 8,
	MarksmanRifle = 9,
	/** Utility: grenades, breach charges, medkits. Never occupies a gun slot. */
	Utility = 10
};

UENUM(BlueprintType)
enum class EOneBrahamWeaponSlot : uint8
{
	/** Rifle / SMG / shotgun / marksman slot. */
	Primary = 0,
	/** Pistol slot. Concealed under a jacket, drawn fast. */
	Sidearm = 1,
	/** Four utility slots (frag, flash, smoke, breach). */
	Utility = 2,
	/** Keycards / override tool live here, never dropped on death. */
	Access = 3,
	/** Bare hands - used for doors, revives, fists. */
	Melee = 4
};

UENUM(BlueprintType)
enum class EOneBrahamFireMode : uint8
{
	Single = 0,
	Burst = 1,
	Auto = 2,
	Pump = 3,
	Bolt = 4
};

UENUM(BlueprintType)
enum class EOneBrahamAttachmentSlot : uint8
{
	Optic = 0,
	Muzzle = 1,
	Underbarrel = 2,
	Laser = 3,
	Flashlight = 4,
	Magazine = 5,
	Stock = 6
};

// ==========================================================================
//  Replicated map entries
//
//  Unreal does not replicate TMap: UnrealHeaderTool rejects a Replicated TMap
//  property outright, and a TMap inside a replicated struct never reaches a
//  client. Four maps in this project were declared Replicated, so the module
//  could not build and, had it built, no client would have had its weapons,
//  its ammo, its attachments or anyone's outfit. Each owner now keeps its TMap
//  as a local lookup and replicates one of these arrays; the RepNotify
//  rebuilds the map on every client.
// ==========================================================================

/** Cosmetic slot -> option, for the wire. */
USTRUCT(BlueprintType)
struct FOneBrahamCosmeticEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FName Slot = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FName Option = NAME_None;
};

/** Attachment slot -> attachment id, for the wire. */
USTRUCT(BlueprintType)
struct FOneBrahamAttachmentEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	EOneBrahamAttachmentSlot Slot = EOneBrahamAttachmentSlot::Optic;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	FName AttachmentId = NAME_None;
};

/** Weapon id -> reserve rounds, for the wire. */
USTRUCT(BlueprintType)
struct FOneBrahamReserveAmmoEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	FName WeaponId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	int32 Rounds = 0;
};

/** Inventory slot -> weapon actor, for the wire. */
USTRUCT(BlueprintType)
struct FOneBrahamOwnedWeaponEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	EOneBrahamWeaponSlot Slot = EOneBrahamWeaponSlot::Primary;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<AOneBrahamWeaponBase> Weapon = nullptr;
};

namespace OneBrahamReplication
{
	inline TArray<FOneBrahamCosmeticEntry> CosmeticsToArray(const TMap<FName, FName>& Map)
	{
		TArray<FOneBrahamCosmeticEntry> Out;
		for (const TPair<FName, FName>& Pair : Map)
		{
			// Field by field: a USTRUCT with GENERATED_BODY is not an aggregate.
			FOneBrahamCosmeticEntry& Entry = Out.AddDefaulted_GetRef();
			Entry.Slot = Pair.Key;
			Entry.Option = Pair.Value;
		}
		return Out;
	}

	inline TMap<FName, FName> CosmeticsToMap(const TArray<FOneBrahamCosmeticEntry>& Entries)
	{
		TMap<FName, FName> Out;
		for (const FOneBrahamCosmeticEntry& Entry : Entries)
		{
			Out.Add(Entry.Slot, Entry.Option);
		}
		return Out;
	}
}

/** Serialisable runtime state of one weapon instance. Replicated as a struct to keep net cost low. */
USTRUCT(BlueprintType)
struct FOneBrahamWeaponState
{
	GENERATED_BODY()

	/** Row name into the UOneBrahamWeaponDataAsset registry. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	FName WeaponId = NAME_None;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	int32 AmmoInMagazine = 0;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	int32 ReserveAmmo = 0;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	EOneBrahamFireMode CurrentFireMode = EOneBrahamFireMode::Auto;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	bool bSuppressorAttached = false;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	bool bOpticAttached = false;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	bool bLaserAttached = false;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	bool bFlashlightOn = false;

	/** Cosmetic skin row name - never affects stats (no pay-to-win). */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Weapon")
	FName SkinId = NAME_None;

	bool IsValidWeapon() const { return !WeaponId.IsNone(); }
};

// ==========================================================================
//  Squads / teams
// ==========================================================================

USTRUCT(BlueprintType)
struct FOneBrahamSquadMemberInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	int32 PlayerId = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FGameplayTag ArchetypeTag;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	EOneBrahamLifeState LifeState = EOneBrahamLifeState::Alive;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	float Health = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	int32 Kills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	int32 Downs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	int32 Deaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bIsSquadLeader = false;

	/** True when the squad can see this member's silhouette through walls. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bIsVisibleToSquad = true;
};

USTRUCT(BlueprintType)
struct FOneBrahamSquadInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	int32 SquadId = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	EOneBrahamTeam Team = EOneBrahamTeam::None;

	/** Squad display name shown in the lobby ("Braham Compliance", "Floor 9 Ops"). */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FText SquadName;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	TArray<FOneBrahamSquadMemberInfo> Members;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bEliminated = false;

	/** True while this squad owns the active extraction attempt. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bHasExtractionClaim = false;

	bool IsFull(int32 SquadSize) const { return Members.Num() >= SquadSize; }
	bool IsAlive() const
	{
		for (const FOneBrahamSquadMemberInfo& M : Members)
		{
			if (M.LifeState == EOneBrahamLifeState::Alive || M.LifeState == EOneBrahamLifeState::Downed)
			{
				return true;
			}
		}
		return false;
	}
};

// ==========================================================================
//  Killfeed / scoring / match results
// ==========================================================================

UENUM(BlueprintType)
enum class EOneBrahamKillFeedVerb : uint8
{
	/** Normal gunshot kill. */
	Killed = 0,
	/** Headshot kill - killfeed renders a distinct marker. */
	Headshot = 1,
	/** Executed a downed player. */
	Executed = 2,
	/** Died to the building (shutter, lift, fall, purge). */
	Building = 3,
	/** Disconnected / timed out. */
	Left = 4,
	/** Finished off by their own frag / breach charge. */
	OwnUtility = 5
};

USTRUCT(BlueprintType)
struct FOneBrahamKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	FString KillerName;

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	FString VictimName;

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	EOneBrahamTeam KillerTeam = EOneBrahamTeam::None;

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	EOneBrahamTeam VictimTeam = EOneBrahamTeam::None;

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	EOneBrahamKillFeedVerb Verb = EOneBrahamKillFeedVerb::Killed;

	/** Weapon id, or the trap id for building kills ("LiftDoors", "FireShutter"). */
	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	FName WeaponId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	bool bWasDowned = false;

	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	bool bAssisted = false;

	/** World time seconds when the entry was created (used for auto-expiry). */
	UPROPERTY(BlueprintReadOnly, Category = "Killfeed")
	float Timestamp = 0.f;
};

USTRUCT(BlueprintType)
struct FOneBrahamPlayerSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	FString PlayerName;

	/** APlayerState::GetPlayerId: how a machine finds its own row. Names are not unique. */
	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 PlayerId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	FGameplayTag ArchetypeTag;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	EOneBrahamTeam Team = EOneBrahamTeam::None;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 Kills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 Downs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 Assists = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 Deaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 Revives = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	float DamageDealt = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 FloorsClimbed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	float TimeAliveSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	bool bExtracted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 Score = 0;
};

/** How a match ended: the four endings of Docs/06 section 7, plus FIRE DRILL's score limit. */
UENUM(BlueprintType)
enum class EOneBrahamMatchEnding : uint8
{
	None = 0,
	/** A squad completed the 30 s winch hold. */
	Extracted = 1,
	/** Every other squad was eliminated before anyone extracted. */
	LastSquadStanding = 2,
	/** Every squad was eliminated, the purge included. */
	NoSurvivors = 3,
	/** The hard time limit was reached. */
	TimeOut = 4,
	/** FIRE DRILL: a team reached the score limit. */
	ScoreLimit = 5
};

USTRUCT(BlueprintType)
struct FOneBrahamMatchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;

	/** Which of the endings this was; the results screen's headline is chosen from it. */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	EOneBrahamMatchEnding Ending = EOneBrahamMatchEnding::None;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	EOneBrahamTeam WinningTeam = EOneBrahamTeam::None;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FText WinningSquadName;

	/** True when nobody got out - the "no survivors" ending. */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bNoSurvivors = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ExtractedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	float MatchDurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FOneBrahamPlayerSummary> PlayerSummaries;
};

// ==========================================================================
//  Voice
// ==========================================================================

UENUM(BlueprintType)
enum class EOneBrahamVoiceChannel : uint8
{
	/** Everyone within attenuation range hears you. Default channel. */
	Proximity = 0,
	/** Your squad only, regardless of distance. Radio-filtered. */
	Squad = 1,
	/** Whole team (FIRE DRILL). Radio-filtered. */
	Team = 2,
	/** Building announcements / director. Never player-driven. */
	Announcement = 3,
	/** Off. */
	None = 4
};

UENUM(BlueprintType)
enum class EOneBrahamVoiceChatMode : uint8
{
	/** Always transmitting. */
	OpenMic = 0,
	/** Push-to-talk. Default. */
	PushToTalk = 1,
	/** Voice activation with a noise-gate threshold. */
	VoiceActivation = 2,
	/** Mic muted entirely. */
	Muted = 3
};

// ==========================================================================
//  Delegates - shared across health, weapons, match, UI
// ==========================================================================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamHealthChanged, float, NewHealth, float, MaxHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamLifeStateChanged, EOneBrahamLifeState, OldState, EOneBrahamLifeState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamDamaged, AActor*, DamageInstigator, const FOneBrahamDamageReport&, Report);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamWeaponChanged, class AOneBrahamWeaponBase*, NewWeapon, class AOneBrahamWeaponBase*, OldWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamAmmoChanged, int32, AmmoInMagazine, int32, ReserveAmmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamReloadStateChanged, bool, bReloading);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamKillFeedUpdated, const FOneBrahamKillFeedEntry&, Entry);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamExtractionStateChanged, EOneBrahamExtractionState, NewState, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamLockdownChanged, EOneBrahamLockdownStage, Stage, float, SecondsToNextStage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamZoneUnlocked, FName, ZoneId, EOneBrahamUnlockMethod, Method);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamInteractionPromptChanged, const FText&, Prompt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamHoldProgressChanged, float, Progress);
