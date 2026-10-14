// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVoiceDirector.h
// ---------------------------------------------------------------------------
// Who says what, when something happens.
//
// The script has a hundred and five lines and the code used to ask for forty of
// them: the rest were written, costed for recording, and could never be said.
// Gameplay code now reports what happened (a hit, a kill, a reload, a refused
// door) and this decides who speaks and which line: the person it happened to,
// the nearest squadmate who saw it, or an enemy who can see them. One place,
// so the casting of a moment is reviewable as a table rather than scattered
// across twelve files.
//
// Everything here is server-only and goes through
// UOneBrahamVoiceSubsystem::SpeakContextualLine, which owns the cooldowns, the
// chances and the replication. Calling a report too often is harmless.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"

class AActor;
class AOneBrahamCharacter;
class UWorld;

struct ONEBRAHAM_API FOneBrahamVoiceDirector
{
	// --- Combat -----------------------------------------------------------
	/** Victim was hit by Instigator (a character, a projectile or the building). */
	static void OnDamaged(AOneBrahamCharacter* Victim, AActor* Instigator, float Damage);

	/** Victim died; Killer is null for the building. */
	static void OnKilled(AOneBrahamCharacter* Victim, AOneBrahamCharacter* Killer);

	static void OnDowned(AOneBrahamCharacter* Victim);
	static void OnBleedingOut(AOneBrahamCharacter* Victim);
	static void OnReviveStarted(AOneBrahamCharacter* Reviver, AOneBrahamCharacter* Patient);
	static void OnReviveHalfway(AOneBrahamCharacter* Reviver);
	static void OnExecuteStarted(AOneBrahamCharacter* Executor, AOneBrahamCharacter* Victim);

	// --- Weapons and kit --------------------------------------------------
	static void OnReload(AOneBrahamCharacter* Shooter, bool bMagazineEmpty);
	static void OnDryFire(AOneBrahamCharacter* Shooter);
	static void OnSidearmDrawn(AOneBrahamCharacter* Shooter);
	static void OnUtilityThrown(AOneBrahamCharacter* Thrower, FName UtilityId);
	static void OnBreachImminent(AOneBrahamCharacter* Planter);
	static void OnFlashed(AOneBrahamCharacter* Victim, float Strength);
	static void OnMeleeHit(AOneBrahamCharacter* Attacker);
	static void OnMedkitFinished(AOneBrahamCharacter* User);
	static void OnMedkitEmpty(AOneBrahamCharacter* User);
	static void OnLootTaken(AOneBrahamCharacter* Taker, bool bWeapon);

	// --- The building -----------------------------------------------------
	static void OnAccessGranted(AOneBrahamCharacter* User);
	static void OnAccessRefused(AOneBrahamCharacter* User, bool bHoldsAnyCard);
	static void OnOverrideStarted(AOneBrahamCharacter* User);
	static void OnOverrideProgress(AOneBrahamCharacter* User);
	static void OnOverrideRefused(AOneBrahamCharacter* User);
	static void OnLiftCalled(AOneBrahamCharacter* Caller);
	static void OnLiftOutOfService(AOneBrahamCharacter* Caller);
	static void OnLiftCrush(AOneBrahamCharacter* Victim);
	static void OnLockdownStage(UWorld* World, EOneBrahamLockdownStage Stage);
	static void OnShuttersClosing(UWorld* World, const FVector& Where);
	static void OnGlassBroken(UWorld* World, const FVector& Where, int32 FloorIndex);
	static void OnPropShorted(UWorld* World, const FVector& Where);
	static void OnHelicopterArriving(UWorld* World);

	// --- The match --------------------------------------------------------
	static void OnMatchStarted(UWorld* World);
	static void OnMatchHalfway(UWorld* World);
	static void OnMatchEnded(UWorld* World, int32 WinningSquadId);
	static void OnTwoSquadsLeft(UWorld* World);

	// --- Replies ----------------------------------------------------------
	/** A line was just said: a squadmate may answer it a beat later. */
	static void OnLineSpoken(AOneBrahamCharacter* Speaker, FName LineId);

	// --- Casting helpers (public for the tests) ---------------------------
	static int32 SquadOf(const AOneBrahamCharacter* Character);
	static bool IsUp(const AOneBrahamCharacter* Character);

	/** The nearest standing squadmate of Of within RangeCm, excluding Of. */
	static AOneBrahamCharacter* NearestSquadmate(const AOneBrahamCharacter* Of, float RangeCm);

	/** The nearest standing enemy of Of within RangeCm that can see Of. */
	static AOneBrahamCharacter* NearestEnemyWithSight(const AOneBrahamCharacter* Of, float RangeCm);

	/** Standing enemies of Of within RangeCm. */
	static int32 CountEnemiesNear(const AOneBrahamCharacter* Of, float RangeCm);

	/** One standing member of each squad, the leader where possible. */
	static TArray<AOneBrahamCharacter*> OneVoicePerSquad(UWorld* World);

private:
	static void Say(AOneBrahamCharacter* Speaker, const TCHAR* LineId, float Priority = 0.5f);
	static void SayLater(AOneBrahamCharacter* Speaker, const TCHAR* LineId, float DelaySeconds);
	static bool IsServer(const UWorld* World);
};
