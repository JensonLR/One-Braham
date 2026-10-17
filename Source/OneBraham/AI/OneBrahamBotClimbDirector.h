// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBotClimbDirector.h
// ---------------------------------------------------------------------------
// How bot squads climb the tower in ASCENT.
//
// THE PROBLEM IT SOLVES
//   Left to the navmesh, every bot's shortest way to the Skydeck is the spiral
//   stair B. All three squads met in its 120 cm floor-1 lobby inside twenty
//   seconds of the start, fought there, and the downed bodies plugged the
//   150 cm stair doors and the spiral's landing: one squad's bots got up and
//   the other eleven lay in a doorway.
//
// WHAT IT DOES
//   * Routes. When the match goes live each squad is given its own way off
//     the ground floor - fire stair A, spiral B or the escalators to floor 1
//     (while the navmesh walks from their top on to a stair: the lift lobby's
//     door can leave them a dead end) -
//     the set with the shortest total walk from where the squads spawned
//     (OneBrahamBotRules::AssignOpeningRoutes). From then on, whenever a bot is
//     out on a floor, its squad's route from that floor is re-chosen as the
//     cheapest usable one: distance, enemies and bodies in the doorway and the
//     stairwell above, a stall a team-mate had there, a shut shutter or a
//     locked door (from the lockdown and access state, not by walking into
//     it), minus a preference for the squad's own stair and for the one it is
//     already on (OneBrahamBotRules::PickClimbRoute).
//   * Legs. A bot walks to its route's way in on its floor, then up two floors
//     at a time to the stair lobby (a path the navmesh query always completes),
//     re-aimed as it climbs, so it climbs without stopping.
//   * Departures. Squads leave the plaza three seconds apart and their members
//     0.8 s apart: a file through the doors, not a scrum. A squad that has to
//     share its way off the ground floor with one that left before it holds
//     on the plaza (up to 12 s) until it will reach the door five seconds
//     behind that squad's last member (OneBrahamBotRules::SharedOpeningHolds).
//     A bot that is shot at while it waits fights back.
//   * The squad. A bot three floors ahead of its squad's last climber waits for
//     them in the hallway outside its stair door, for at most twelve seconds a
//     floor, and never while the lockdown is about to seal the floors below.
//   * Blockages. A climber that has not moved 1.2 m in four seconds tells its
//     squad the route is jammed there, and the squad re-chooses (going back to
//     the other stair's door on the floor it is on).
//   * Fights. The controller still fights: an enemy in sight inside 12 m is
//     the controller's to advance on, engage or back off from. One further
//     off, or out of sight, is shot at on the way past - the bot keeps
//     climbing, its aim and trigger still the controller's - rather than
//     chased across the floor. A stand-up fight that is a stalemate (ten
//     seconds and nobody hit for eight), has gone on too long (35 s) or is
//     holding the bot below an imminent lockdown stage (4 s) is broken off: the
//     bot climbs for ten seconds, shooting as it goes.
//   * The lockdown. Inside thirty seconds of the next stage (or its warning),
//     or on a purged floor, a bot below the highest occupied floor is urgent:
//     no waiting for the squad, and fights are broken off at once.
//   * Lifts are not used: AOneBrahamBotController does not know how to call one.
//
// HOW IT DRIVES A BOT
//   AOneBrahamBotController walks to the objective when nothing needs shooting.
//   The director is a world subsystem that ticks after every actor, so on any
//   frame the controller has issued its own travel move to a bot the director
//   is steering, the director re-issues the climb move through the controller's
//   public AAIController::MoveToLocation, and the bot follows the climb. It
//   steps aside for the controller's unstick step (a short straight move), for
//   fights, for a downed bot, and on the objective's floor, where the
//   controller's own objective travel and hold take over.
//   It runs on the server, in ASCENT, and only for AOneBrahamBotController.
//
// LOGGING ([OB.CLIMB], LogOneBrahamBots)
//   Route choices and re-routes with their reason, break-offs, every new
//   highest floor per squad with its time since the match went live, a squad
//   summary every 30 s, and a final per-squad and per-bot summary.

#pragma once

#include "CoreMinimal.h"
#include "AI/OneBrahamBotRules.h"
#include "AITypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamBotClimbDirector.generated.h"

class AOneBrahamBotController;
class AOneBrahamCharacter;
class AOneBrahamFireShutter;
class AOneBrahamAccessZone;
class UOneBrahamLockdownComponent;

UCLASS()
class ONEBRAHAM_API UOneBrahamBotClimbDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem / UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

	/** Highest floor any bot of the squad has stood on this match (INDEX_NONE before the start). */
	int32 GetSquadHighestFloor(int32 SquadId) const;

	/** The route a squad is on now. */
	OneBrahamBotRules::EClimbRoute GetSquadRoute(int32 SquadId) const;

	/** One line per squad: highest floor and when, route, breaks, re-routes. */
	FString GetSummary() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** What a bot is doing as far as the climb is concerned. */
	enum class EClimbPhase : uint8
	{
		/** Not steering: no pawn, downed, or not started. */
		Off,
		/** Waiting out its departure stagger on the plaza. */
		Departing,
		/** Walking to its route's way in on its floor. */
		Approach,
		/** On the route, two floors at a time. */
		Climb,
		/** Holding outside its stair door for the squad. */
		Wait,
		/** Fighting: the controller has it. */
		Fight,
		/** Out of a fight, climbing while it shoots. */
		BreakOff,
		/** In a fight at range: climbing on while it shoots. */
		Skirmish,
		/** The controller's unstick step is running. */
		Yield,
		/** On the objective's floor: the controller's objective travel takes over. */
		Handoff
	};

	struct FBotState
	{
		TWeakObjectPtr<AOneBrahamBotController> Bot;
		int32 SquadId = 0;
		int32 Ordinal = 0;
		EClimbPhase Phase = EClimbPhase::Off;
		OneBrahamBotRules::EClimbRoute Route = OneBrahamBotRules::EClimbRoute::None;

		/** Lives seen: a new one restarts the departure. */
		int32 Lives = 0;
		double DepartAt = 0.0;
		/** This life began with the match: it also waits out its squad's shared-door hold. */
		bool bOpeningDeparture = false;

		FVector Waypoint = FVector::ZeroVector;
		FAIRequestID Request = FAIRequestID::InvalidRequest;
		double YieldUntil = 0.0;

		/** Blocked test: where it was, and when. */
		FVector CheckLocation = FVector::ZeroVector;
		double CheckTime = 0.0;

		int32 Floor = INDEX_NONE;
		double FloorSince = 0.0;
		double WaitedOnFloor = 0.0;
		int32 HighestFloor = INDEX_NONE;
		double HighestFloorTime = 0.0;

		/** Fight bookkeeping. */
		double FightStart = 0.0;
		double LastExchange = 0.0;
		float LastDamageDealt = 0.f;
		float LastHealth = -1.f;
		double BreakOffUntil = 0.0;

		/** The floor it last found every way up shut on (logged once). */
		int32 TrappedFloor = INDEX_NONE;

		int32 Fights = 0;
		int32 BreakOffs = 0;
		int32 Reroutes = 0;
	};

	/** A squad's route from one floor, and when it is chosen again. */
	struct FFloorChoice
	{
		OneBrahamBotRules::EClimbRoute Route = OneBrahamBotRules::EClimbRoute::None;
		double NextChoice = 0.0;
	};

	struct FSquadState
	{
		/** Its own way off the ground floor (AssignOpeningRoutes). */
		OneBrahamBotRules::EClimbRoute Opening = OneBrahamBotRules::EClimbRoute::None;
		/** Seconds it holds on the plaza behind a squad that shares its opening route (SharedOpeningHolds). */
		float OpeningHold = 0.f;
		/** The route it last chose, for the summary. */
		OneBrahamBotRules::EClimbRoute Route = OneBrahamBotRules::EClimbRoute::None;
		/** Floor -> the squad's route from it: members on one floor go the same way. */
		TMap<int32, FFloorChoice> Choices;
		/** (route, floor) -> time the stall memory runs out. */
		TMap<uint32, double> Blocked;
		int32 HighestFloor = INDEX_NONE;
		double HighestFloorTime = 0.0;
		FString HighestFloorBy;
		int32 Reroutes = 0;
		int32 BreakOffs = 0;
	};

	/** A pawn in the tower this tick. */
	struct FPawnSample
	{
		FVector Location = FVector::ZeroVector;
		int32 SquadId = INDEX_NONE;
		uint8 Team = 0;
		bool bStanding = false;
	};

	void RefreshBots();
	/** Gives each squad its own way off the ground floor, from where it spawned. */
	void AssignOpenings(double Now);
	void SamplePawns();
	void UpdateLockdown();
	void UpdateBot(FBotState& State, double Now);

	/** The squad's route for a bot standing on Floor at From (re-chosen at most twice a second). */
	OneBrahamBotRules::EClimbRoute ChooseRoute(FBotState& State, const FVector& From, int32 Floor, double Now, const TCHAR* Reason);

	/** Shutters, locked doors and routes that do not climb from here. */
	bool IsRouteUsable(OneBrahamBotRules::EClimbRoute Route, int32 Floor) const;

	/** True while the top of the escalators is walkable on to a stair on floor 1 (the lift lobby's door open). */
	bool AreEscalatorsConnected() const;

	/** Enemies, bodies and friends in a route's doorway on Floor and its stairwell up to Floor + 2. */
	void CountOnRoute(OneBrahamBotRules::EClimbRoute Route, int32 Floor, uint8 MyTeam, int32& OutEnemies, int32& OutBodies, int32& OutFriends) const;

	/** The lowest floor of the squad's standing, still-climbing bots. */
	int32 GetSquadTrailFloor(int32 SquadId) const;

	bool IsUrgent(int32 Floor) const;

	/** Issues the climb move unless the bot is already on it. */
	void SteerTo(FBotState& State, const FVector& Destination, float Acceptance);

	void NoteFloor(FBotState& State, int32 Floor, double Now);
	void MarkBlocked(FBotState& State, int32 Floor, double Now, const TCHAR* Why);

	double MatchSeconds(double Now) const;
	static uint32 BlockKey(OneBrahamBotRules::EClimbRoute Route, int32 Floor) { return (static_cast<uint32>(Route) << 8) | static_cast<uint32>(Floor & 0xFF); }
	static const TCHAR* ClimbPhaseName(EClimbPhase Phase);
	static FString ClimbSquadLabel(int32 SquadId);

	bool bActive = false;
	bool bAscent = false;
	bool bStarted = false;
	bool bOpeningsAssigned = false;
	double MatchStartTime = 0.0;
	int32 ObjectiveFloor = OneBrahamConstants::FloorSkydeck;

	float DecisionAccumulator = 0.f;
	float RefreshAccumulator = 10.f;
	float SummaryAccumulator = 0.f;

	TArray<FBotState> Bots;
	TMap<int32, FSquadState> Squads;
	TArray<FPawnSample> Pawns;

	/** AreEscalatorsConnected's answer, re-asked every few seconds. */
	mutable bool bEscalatorsConnected = false;
	mutable double EscalatorCheckExpires = 0.0;
	mutable bool bEscalatorsCheckLogged = false;
	mutable bool bEscalatorsLastLogged = false;

	TArray<TWeakObjectPtr<AOneBrahamFireShutter>> Shutters;
	TArray<TWeakObjectPtr<AOneBrahamAccessZone>> Doors;

	/** Lockdown, read once per decision. */
	bool bDirectorRunning = false;
	bool bWarningActive = false;
	float SecondsToNextStage = 0.f;
	int32 HighestOccupiedFloor = 0;
	TWeakObjectPtr<UOneBrahamLockdownComponent> Lockdown;
};
