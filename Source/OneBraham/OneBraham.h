// Copyright (c) One Braham. All Rights Reserved.
//
// Unity-style module header. Everything in the OneBraham module includes this
// indirectly through Build.cs's shared PCH (OneBraham.h is the module PCH
// candidate), so keep it lean: only truly ubiquitous includes belong here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CoreMisc.h"
#include "Net/UnrealNetwork.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Stats/Stats.h"

// ==========================================================================
//  ONE BRAHAM - build identity
// ==========================================================================
#define ONE_BRAHAM_VERSION_MAJOR 1
#define ONE_BRAHAM_VERSION_MINOR 0
#define ONE_BRAHAM_VERSION_PATCH 0
#define ONE_BRAHAM_NET_PROTOCOL_VERSION 1

// Engine-side friendly-name used by Steam, crash reporter and server browser.
#define ONE_BRAHAM_DISPLAY_NAME TEXT("ONE BRAHAM")

// ==========================================================================
//  Log categories. Declared here, defined in OneBraham.cpp.
//  Config/DefaultEngine.ini sets per-category verbosity for shipping/dedicated.
// ==========================================================================
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBraham, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamNet, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamWeapons, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamMatch, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamVoice, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamUI, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamAccess, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamExtraction, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamAudio, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamAudit, Log, All);
ONEBRAHAM_API DECLARE_LOG_CATEGORY_EXTERN(LogOneBrahamBots, Log, All);

// ==========================================================================
//  Convenient, greppable logging macros. Every gameplay-relevant log line in
//  ONE BRAHAM is prefixed so QA can `grep "[OB]"` a client/server log.
// ==========================================================================
#define OB_LOG(Format, ...)        UE_LOG(LogOneBraham, Log, TEXT("[OB] " Format), ##__VA_ARGS__)
#define OB_WARN(Format, ...)       UE_LOG(LogOneBraham, Warning, TEXT("[OB] " Format), ##__VA_ARGS__)
#define OB_ERROR(Format, ...)      UE_LOG(LogOneBraham, Error, TEXT("[OB] " Format), ##__VA_ARGS__)
#define OB_NET_LOG(Format, ...)    UE_LOG(LogOneBrahamNet, Verbose, TEXT("[OB.NET] " Format), ##__VA_ARGS__)
#define OB_NET_WARN(Format, ...)   UE_LOG(LogOneBrahamNet, Warning, TEXT("[OB.NET] " Format), ##__VA_ARGS__)
#define OB_WEAPON_LOG(Format, ...) UE_LOG(LogOneBrahamWeapons, Verbose, TEXT("[OB.WPN] " Format), ##__VA_ARGS__)
#define OB_MATCH_LOG(Format, ...)  UE_LOG(LogOneBrahamMatch, Log, TEXT("[OB.MATCH] " Format), ##__VA_ARGS__)
#define OB_VOICE_LOG(Format, ...)  UE_LOG(LogOneBrahamVoice, Verbose, TEXT("[OB.VOICE] " Format), ##__VA_ARGS__)
#define OB_ACCESS_LOG(Format, ...) UE_LOG(LogOneBrahamAccess, Log, TEXT("[OB.ACCESS] " Format), ##__VA_ARGS__)
#define OB_EXTRACT_LOG(Format, ...) UE_LOG(LogOneBrahamExtraction, Log, TEXT("[OB.EXTRACT] " Format), ##__VA_ARGS__)
#define OB_AUDIT_LOG(Format, ...)  UE_LOG(LogOneBrahamAudit, Warning, TEXT("[OB.AUDIT] " Format), ##__VA_ARGS__)

// ==========================================================================
//  Authority helpers - used everywhere so the intent reads clearly at a call
//  site, e.g. `if (ONE_BRAHAM_HAS_AUTHORITY(this)) { ... }`
// ==========================================================================
#define ONE_BRAHAM_HAS_AUTHORITY(Obj) \
	((Obj) != nullptr && (Obj)->GetOwner() != nullptr && (Obj)->GetOwner()->HasAuthority())

#define ONE_BRAHAM_ACTOR_HAS_AUTHORITY(Actor) ((Actor) != nullptr && (Actor)->HasAuthority())

// ==========================================================================
//  Common gameplay constants. Mirrored in Config/DefaultGame.ini so designers
//  can retune without a recompile where a Data Asset is not appropriate.
// ==========================================================================
namespace OneBrahamConstants
{
	/** Maximum number of squads in an ASCENT match (3 squads of 4 shipping). */
	static constexpr int32 MaxSquads = 4;

	/** Maximum players per squad. */
	static constexpr int32 SquadSize = 4;

	/** Hard player cap for both modes. */
	static constexpr int32 MaxPlayers = 16;

	/** Floors 1..15, with the Skydeck on 15. Index 0 is the plaza/undercroft. */
	static constexpr int32 FloorPlaza = 0;
	static constexpr int32 FloorLobby = 1;
	static constexpr int32 FloorMezzanine = 2;
	static constexpr int32 FloorOffices = 3;      // 3..8 are open-plan/meeting
	static constexpr int32 FloorServer = 9;
	static constexpr int32 FloorExecutive = 12;
	static constexpr int32 FloorCanteen = 15;
	static constexpr int32 FloorSkydeck = 15;

	/** Extraction hold time, seconds (ASCENT). */
	static constexpr float ExtractionHoldSeconds = 30.f;

	/** Helicopter travel time from first broadcast to touch-down. */
	static constexpr float HelicopterArrivalSeconds = 45.f;

	/** Seconds between lockdown escalation stages. */
	static constexpr float LockdownStageSeconds = 90.f;

	/** Downed bleed-out time before death. */
	static constexpr float BleedOutSeconds = 60.f;

	/** Revive interaction time. */
	static constexpr float ReviveSeconds = 4.5f;

	/** Terminal override hold time (without a keycard). */
	static constexpr float OverrideHoldSeconds = 12.f;

	/** Terminal override hold time (with the matching keycard). */
	static constexpr float OverrideKeycardSeconds = 3.f;

	/** Breaching charge fuse. */
	static constexpr float BreachFuseSeconds = 3.f;

	/**
	 * Storey height of the building, in centimetres.
	 *
	 * ONE NUMBER, FOUR SYSTEMS. The map builder lays floors out on this pitch, the
	 * lockdown controller decides which floor a player is on with it, the bots use
	 * it for "can I hear them upstairs", and the audio manager uses half of it as
	 * the threshold for "through the floor". It is 400 cm because that is what a
	 * real 2000s office tower's floor-to-floor is, and because every combat range
	 * in the game was tuned against a building that has this storey height.
	 *
	 * If this ever changes, the map, the navmesh and every audio curve change with
	 * it. Do not change it casually.
	 */
	static constexpr float FloorHeightCm = 400.f;

	/** Half a storey: the threshold for "that sound came from another floor". */
	static constexpr float HalfFloorHeightCm = FloorHeightCm * 0.5f;
}
