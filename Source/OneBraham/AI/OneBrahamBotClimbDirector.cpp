// Copyright (c) One Braham. All Rights Reserved.

#include "AI/OneBrahamBotClimbDirector.h"

#include "Access/OneBrahamAccessZone.h"
#include "Access/OneBrahamFireShutter.h"
#include "Bots/OneBrahamBotController.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Extraction/OneBrahamExtractionPad.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "OneBraham.h"

#include "Algo/AllOf.h"

namespace
{
	using OneBrahamBotRules::EClimbRoute;

	/** Decisions four times a second; move enforcement every frame. */
	constexpr float OBClimbDecisionInterval = 0.25f;
	constexpr float OBClimbRefreshInterval = 1.f;
	constexpr float OBClimbSummaryInterval = 30.f;

	/** A squad's route on a floor is re-chosen at most this often. */
	constexpr double OBRouteChoiceSeconds = 2.0;

	/** Within this of its way in, a bot starts up the route. */
	constexpr float OBEntryReachedCm = 160.f;

	/** A move of the controller's ending this close to the bot is its unstick step: let it run. */
	constexpr float OBUnstickGoalCm = 320.f;
	constexpr double OBYieldSeconds = 3.2;

	/** A climber that has not gained a floor for this long no longer holds its squad back. */
	constexpr double OBTrailGiveUpSeconds = 60.0;

	/** From a standing capsule's middle to just above the slab it stands on. */
	constexpr float OBFeetBelowCentreCm = 70.f;

	/** How often the escalators' onward path is asked for again. */
	constexpr double OBEscalatorCheckSeconds = 5.0;
	/** From their top to a stair door, a walk this much longer than the crow flies is a detour (back down them). */
	constexpr float OBEscalatorDetourFactor = 1.8f;
	constexpr float OBEscalatorDetourSlackCm = 600.f;

	/** A doorway "has" whoever stands within this of its middle. */
	constexpr float OBDoorwayRadiusCm = 300.f;

	constexpr EClimbRoute OBAllRoutes[] = { EClimbRoute::FireStairA, EClimbRoute::SpiralB, EClimbRoute::Escalators };

	bool IsStairRoute(EClimbRoute Route)
	{
		return Route == EClimbRoute::FireStairA || Route == EClimbRoute::SpiralB;
	}

	FString BotName(const AController* Controller)
	{
		if (Controller == nullptr)
		{
			return TEXT("none");
		}
		return Controller->PlayerState != nullptr && !Controller->PlayerState->GetPlayerName().IsEmpty()
			? Controller->PlayerState->GetPlayerName()
			: Controller->GetName();
	}

	bool IsFighting(EOneBrahamBotState State)
	{
		return State == EOneBrahamBotState::Advance || State == EOneBrahamBotState::Engage
			|| State == EOneBrahamBotState::Retreat;
	}
}

// ===========================================================================
//  Subsystem
// ===========================================================================

bool UOneBrahamBotClimbDirector::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Bots are the server's; a client never steers one.
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->GetNetMode() != NM_Client;
}

bool UOneBrahamBotClimbDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOneBrahamBotClimbDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Whether this is ASCENT is only known once the game state has its
	// settings; the tick finds out and switches itself off for anything else.
	bActive = InWorld.GetNetMode() != NM_Client;
}

void UOneBrahamBotClimbDirector::Deinitialize()
{
	if (bStarted)
	{
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] final after %.0f s:\n%s"),
			GetWorld() != nullptr ? MatchSeconds(GetWorld()->GetTimeSeconds()) : 0.0, *GetSummary());
	}

	Bots.Reset();
	Squads.Reset();
	Pawns.Reset();
	bActive = false;

	Super::Deinitialize();
}

TStatId UOneBrahamBotClimbDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamBotClimbDirector, STATGROUP_Tickables);
}

// ===========================================================================
//  Tick
// ===========================================================================

void UOneBrahamBotClimbDirector::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !bActive)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();

	RefreshAccumulator += DeltaTime;
	if (RefreshAccumulator >= OBClimbRefreshInterval)
	{
		RefreshAccumulator = 0.f;
		RefreshBots();
		if (!bActive)
		{
			return;
		}
	}

	if (!bAscent || !bStarted)
	{
		return;
	}

	// Every frame: the controller may have issued its own travel move this
	// frame. The director ticks after every actor, so it has the last word.
	for (FBotState& State : Bots)
	{
		AOneBrahamBotController* Bot = State.Bot.Get();
		UPathFollowingComponent* Following = Bot != nullptr ? Bot->GetPathFollowingComponent() : nullptr;
		if (Following == nullptr || Bot->GetPawn() == nullptr)
		{
			continue;
		}

		const bool bOurs = State.Request.IsValid() && Following->GetCurrentRequestId() == State.Request;

		switch (State.Phase)
		{
		case EClimbPhase::Departing:
			// Held on the plaza - but a bot that is shot at while it waits
			// fights back, moves and all.
			if (Following->GetStatus() != EPathFollowingStatus::Idle && !IsFighting(Bot->GetBotState()))
			{
				Bot->StopMovement();
			}
			break;

		case EClimbPhase::Approach:
		case EClimbPhase::Climb:
		case EClimbPhase::Wait:
		case EClimbPhase::BreakOff:
		case EClimbPhase::Skirmish:
			if (!bOurs && Following->GetStatus() != EPathFollowingStatus::Idle)
			{
				const bool bArmed = State.Phase == EClimbPhase::BreakOff || State.Phase == EClimbPhase::Skirmish;

				// The controller's unstick step is a short straight walk out of a
				// corner: it is the right thing to do, so it runs to the end.
				const FNavPathSharedPtr Path = Following->GetPath();
				const FVector PawnLocation = Bot->GetPawn()->GetActorLocation();
				if (!bArmed && Path.IsValid() && Path->GetPathPoints().Num() > 0)
				{
					const FVector Goal = Path->GetPathPoints().Last().Location;
					if (FVector::Dist2D(Goal, PawnLocation) < OBUnstickGoalCm && FMath::Abs(Goal.Z - PawnLocation.Z) < 150.f
						&& !IsFighting(Bot->GetBotState()))
					{
						State.Phase = EClimbPhase::Yield;
						State.YieldUntil = Now + OBYieldSeconds;
						State.Request = Following->GetCurrentRequestId();
						break;
					}
				}

				// A fight's moves (advance, sidestep, back off) are the
				// controller's unless the bot is climbing with its gun up.
				if (!bArmed && IsFighting(Bot->GetBotState()))
				{
					break;
				}

				State.Request = FAIRequestID::InvalidRequest;
				SteerTo(State, State.Waypoint, State.Phase == EClimbPhase::Wait ? 60.f : 80.f);
			}
			break;

		default:
			break;
		}
	}

	DecisionAccumulator += DeltaTime;
	if (DecisionAccumulator < OBClimbDecisionInterval)
	{
		return;
	}
	DecisionAccumulator = 0.f;

	SamplePawns();
	UpdateLockdown();
	if (!bOpeningsAssigned)
	{
		AssignOpenings(Now);
	}
	for (FBotState& State : Bots)
	{
		UpdateBot(State, Now);
	}

	SummaryAccumulator += OBClimbDecisionInterval;
	if (SummaryAccumulator >= OBClimbSummaryInterval)
	{
		SummaryAccumulator = 0.f;
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] at %.0f s:\n%s"), MatchSeconds(Now), *GetSummary());
	}
}

void UOneBrahamBotClimbDirector::RefreshBots()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const AOneBrahamGameState* GameState = World->GetGameState<AOneBrahamGameState>();
	if (!bAscent)
	{
		const UOneBrahamMatchSettingsAsset* Settings = GameState != nullptr ? GameState->GetMatchSettings() : nullptr;
		if (Settings == nullptr)
		{
			return;
		}
		if (Settings->Mode != EOneBrahamMatchMode::Ascent)
		{
			// FIRE DRILL and NIGHT SHIFT have no climb: the controller's own
			// objective travel is the whole of it.
			bActive = false;
			return;
		}
		bAscent = true;

		for (TActorIterator<AOneBrahamFireShutter> It(World); It; ++It)
		{
			Shutters.Add(*It);
		}
		for (TActorIterator<AOneBrahamAccessZone> It(World); It; ++It)
		{
			Doors.Add(*It);
		}
		if (TActorIterator<AOneBrahamExtractionPad> Pad(World); Pad)
		{
			ObjectiveFloor = OneBrahamConstants::FloorIndexForZ(Pad->GetActorLocation().Z);
		}
		Lockdown = GameState->GetLockdownComponent();

		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] ASCENT: steering bot squads up the tower (objective floor %d, %d shutters, %d doors)"),
			ObjectiveFloor, Shutters.Num(), Doors.Num());
	}

	if (!bStarted && GameState != nullptr && GameState->MatchPhase == EOneBrahamMatchPhase::Active)
	{
		bStarted = true;
		MatchStartTime = World->GetTimeSeconds();
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] the match is live: squads leave the plaza %.1f s apart, members %.1f s apart"),
			OneBrahamBotRules::SquadDepartureStaggerSeconds, OneBrahamBotRules::MemberDepartureStaggerSeconds);
	}

	// Bots come and go (a human joining takes a bot's place).
	Bots.RemoveAll([](const FBotState& State) { return !State.Bot.IsValid(); });
	for (TActorIterator<AOneBrahamBotController> It(World); It; ++It)
	{
		AOneBrahamBotController* Bot = *It;
		if (Bots.ContainsByPredicate([Bot](const FBotState& State) { return State.Bot.Get() == Bot; }))
		{
			continue;
		}

		FBotState& State = Bots.AddDefaulted_GetRef();
		State.Bot = Bot;
		State.SquadId = Bot->PreferredSquadId;
		State.Ordinal = Bot->SquadOrdinal;
		Squads.FindOrAdd(State.SquadId);
	}
}

void UOneBrahamBotClimbDirector::SamplePawns()
{
	Pawns.Reset();
	for (TActorIterator<AOneBrahamCharacter> It(GetWorld()); It; ++It)
	{
		const AOneBrahamCharacter* Character = *It;
		FPawnSample& Sample = Pawns.AddDefaulted_GetRef();
		Sample.Location = Character->GetActorLocation();

		const UOneBrahamHealthComponent* Health = Character->GetHealthComponent();
		const UOneBrahamDownedComponent* Downed = Character->GetDownedComponent();
		const AOneBrahamPlayerState* State = Character->GetPlayerState<AOneBrahamPlayerState>();
		Sample.bStanding = State != nullptr && (Health == nullptr || Health->IsAlive()) && (Downed == nullptr || !Downed->IsDowned());
		if (State != nullptr)
		{
			Sample.SquadId = State->SquadId;
			Sample.Team = static_cast<uint8>(State->Team);
		}
	}
}

void UOneBrahamBotClimbDirector::UpdateLockdown()
{
	const UOneBrahamLockdownComponent* Director = Lockdown.Get();
	bDirectorRunning = Director != nullptr && Director->IsDirectorRunning();
	bWarningActive = Director != nullptr && Director->IsWarningActive();
	SecondsToNextStage = Director != nullptr ? Director->GetSecondsToNextStage() : 0.f;
	HighestOccupiedFloor = Director != nullptr ? Director->GetHighestOccupiedFloor() : 0;
}

bool UOneBrahamBotClimbDirector::IsUrgent(int32 Floor) const
{
	const UOneBrahamLockdownComponent* Director = Lockdown.Get();
	return OneBrahamBotRules::IsClimbUrgent(bDirectorRunning, bWarningActive, SecondsToNextStage, Floor,
		HighestOccupiedFloor, Director != nullptr && Director->IsFloorBeingPurged(Floor));
}

// ===========================================================================
//  One bot
// ===========================================================================

void UOneBrahamBotClimbDirector::UpdateBot(FBotState& State, double Now)
{
	using namespace OneBrahamBotRules;

	AOneBrahamBotController* Bot = State.Bot.Get();
	AOneBrahamCharacter* Character = Bot != nullptr ? Cast<AOneBrahamCharacter>(Bot->GetPawn()) : nullptr;
	if (Character == nullptr || Bot->GetBotState() == EOneBrahamBotState::Downed || Bot->GetBotState() == EOneBrahamBotState::Idle)
	{
		State.Phase = EClimbPhase::Off;
		State.Request = FAIRequestID::InvalidRequest;
		return;
	}

	// The floor under its feet, not under its capsule's middle: half way up the
	// escalators the middle of a bot is already above floor 1's slab, and a bot
	// that thought it had arrived turned round for the stairs and walked back
	// down them.
	const FVector Location = Character->GetActorLocation();
	const int32 Floor = OneBrahamConstants::FloorIndexForZ(Location.Z - OBFeetBelowCentreCm);

	// A new life: start again, with the departure stagger if this is the start
	// of the match (a respawn mid-match goes at once).
	if (Bot->LivesStarted != State.Lives)
	{
		State.Lives = Bot->LivesStarted;
		State.Phase = EClimbPhase::Departing;
		State.bOpeningDeparture = MatchSeconds(Now) < 20.0;
		State.DepartAt = Now + (State.bOpeningDeparture ? DepartureDelaySeconds(State.SquadId, State.Ordinal) : 0.0);
		State.Route = EClimbRoute::None;
		State.Request = FAIRequestID::InvalidRequest;
		State.CheckLocation = Location;
		State.CheckTime = Now;
		State.Floor = INDEX_NONE;
		State.LastDamageDealt = Bot->GetDamageDealt();
		State.LastHealth = -1.f;
	}

	NoteFloor(State, Floor, Now);

	// On the objective's floor the controller walks to the pad and holds it.
	if (Floor >= ObjectiveFloor)
	{
		if (State.Phase != EClimbPhase::Handoff)
		{
			UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] %s (%s) is on the objective's floor %d at %.0f s: the controller takes it to the pad"),
				*BotName(Bot), *ClimbSquadLabel(State.SquadId), Floor, MatchSeconds(Now));
		}
		State.Phase = EClimbPhase::Handoff;
		return;
	}
	if (State.Phase == EClimbPhase::Handoff)
	{
		State.Phase = EClimbPhase::Approach;
	}

	if (State.Phase == EClimbPhase::Departing)
	{
		// The shared-door hold is read here, not when the life began: the
		// openings are assigned a moment after the first bots are seen.
		const FSquadState* Squad = Squads.Find(State.SquadId);
		const double Hold = State.bOpeningDeparture && Squad != nullptr ? Squad->OpeningHold : 0.0;
		if (Now < State.DepartAt + Hold || (State.bOpeningDeparture && !bOpeningsAssigned))
		{
			return;
		}
		State.Phase = EClimbPhase::Approach;
		State.CheckLocation = Location;
		State.CheckTime = Now;
	}

	if (State.Phase == EClimbPhase::Yield)
	{
		if (Now < State.YieldUntil)
		{
			return;
		}
		State.Phase = EClimbPhase::Approach;
		State.Request = FAIRequestID::InvalidRequest;
		State.CheckLocation = Location;
		State.CheckTime = Now;
	}

	// --- Fights --------------------------------------------------------------
	// Any damage dealt or taken is an exchange: the fight is still a fight.
	const UOneBrahamHealthComponent* Health = Character->GetHealthComponent();
	const float HealthNow = Health != nullptr ? Health->GetHealth() : 0.f;
	if (Bot->GetDamageDealt() > State.LastDamageDealt || (State.LastHealth >= 0.f && HealthNow < State.LastHealth))
	{
		State.LastExchange = Now;
	}
	State.LastDamageDealt = Bot->GetDamageDealt();
	State.LastHealth = HealthNow;

	const bool bUrgent = IsUrgent(Floor);
	const bool bInFight = Bot->GetCurrentTarget() != nullptr
		&& (IsFighting(Bot->GetBotState()) || Bot->GetBotState() == EOneBrahamBotState::Reload);

	if (bInFight)
	{
		const bool bInContact = State.Phase == EClimbPhase::Fight || State.Phase == EClimbPhase::Skirmish || State.Phase == EClimbPhase::BreakOff;
		if (!bInContact)
		{
			// A new contact.
			State.FightStart = Now;
			State.LastExchange = FMath::Max(State.LastExchange, Now - 1.0);
			++State.Fights;
		}
		else if (State.Phase == EClimbPhase::BreakOff && Now >= State.BreakOffUntil)
		{
			// The break-off is spent and somebody is still there: a new fight.
			State.FightStart = Now;
			State.LastExchange = Now;
			State.Phase = EClimbPhase::Fight;
		}

		if (State.Phase != EClimbPhase::BreakOff)
		{
			const float FightSeconds = static_cast<float>(Now - State.FightStart);
			const float Quiet = static_cast<float>(Now - State.LastExchange);
			if (ShouldBreakOffFight(FightSeconds, Quiet, bUrgent))
			{
				State.Phase = EClimbPhase::BreakOff;
				State.BreakOffUntil = Now + BreakOffSeconds;
				State.Request = FAIRequestID::InvalidRequest;
				State.CheckLocation = Location;
				State.CheckTime = Now;
				++State.BreakOffs;
				++Squads.FindOrAdd(State.SquadId).BreakOffs;
				UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] %s (%s) breaks off a %.0f s fight on floor %d (%s, %.0f s without a hit) and climbs on"),
					*BotName(Bot), *ClimbSquadLabel(State.SquadId), FightSeconds, Floor,
					bUrgent ? TEXT("lockdown") : (FightSeconds >= MaxFightSeconds ? TEXT("too long") : TEXT("stalemate")), Quiet);
			}
			else
			{
				// Close and in sight: the controller fights it. Otherwise the
				// bot shoots on its way past and keeps climbing.
				const AActor* Target = Bot->GetCurrentTarget();
				const float Distance = Target != nullptr ? FVector::Dist(Target->GetActorLocation(), Location) : 0.f;
				if (ShouldStandAndFight(Distance, Bot->HasFiringSolution()))
				{
					State.Phase = EClimbPhase::Fight;
					return;
				}
				if (State.Phase != EClimbPhase::Skirmish)
				{
					State.Request = FAIRequestID::InvalidRequest;
					State.CheckLocation = Location;
					State.CheckTime = Now;
				}
				State.Phase = EClimbPhase::Skirmish;
			}
		}
	}
	else if (State.Phase == EClimbPhase::Fight || State.Phase == EClimbPhase::Skirmish
		|| (State.Phase == EClimbPhase::BreakOff && Now >= State.BreakOffUntil))
	{
		// The fight is over: back to the climb from wherever it left the bot.
		State.Phase = EClimbPhase::Approach;
		State.Request = FAIRequestID::InvalidRequest;
		State.CheckLocation = Location;
		State.CheckTime = Now;
	}

	const bool bArmed = State.Phase == EClimbPhase::BreakOff || State.Phase == EClimbPhase::Skirmish;

	if (State.Phase == EClimbPhase::Off)
	{
		State.Phase = EClimbPhase::Approach;
	}

	// --- Blocked? --------------------------------------------------------------
	if (Now - State.CheckTime >= ClimbBlockedSeconds)
	{
		const float Moved = FVector::Dist(Location, State.CheckLocation);
		if (State.Phase != EClimbPhase::Wait && IsClimbBlocked(Moved, static_cast<float>(Now - State.CheckTime)))
		{
			MarkBlocked(State, Floor, Now, TEXT("no progress"));
		}
		State.CheckLocation = Location;
		State.CheckTime = Now;
	}

	// --- Where to --------------------------------------------------------------
	// Already on a way up: keep going up it, unless the squad has just found it
	// jammed (then the bot is "out on its floor" and walks to another).
	const EClimbRoute OnRoute = RouteAt(Location);
	if (OnRoute != EClimbRoute::None && State.Route == EClimbRoute::None && RouteClimbsFromFloor(OnRoute, Floor))
	{
		const FSquadState* Squad = Squads.Find(State.SquadId);
		const double* Jammed = Squad != nullptr ? Squad->Blocked.Find(BlockKey(OnRoute, Floor)) : nullptr;
		if (Jammed == nullptr || *Jammed <= Now)
		{
			State.Route = OnRoute;
		}
	}
	FVector Destination;
	float Acceptance = 80.f;

	if (OnRoute != EClimbRoute::None && OnRoute == State.Route && RouteClimbsFromFloor(OnRoute, Floor))
	{
		// On the way up: two floors at a time, re-aimed as it climbs.
		const int32 Target = ClimbTargetFloor(OnRoute, Floor, ObjectiveFloor);
		Destination = RouteLandingPoint(OnRoute, Target, State.Ordinal);
		if (!bArmed)
		{
			State.Phase = EClimbPhase::Climb;
		}
	}
	else
	{
		// Out on a floor (or in a stairwell it has been told to leave): the
		// squad's route from here, approached first, then taken.
		const EClimbRoute Route = ChooseRoute(State, Location, Floor, Now, TEXT("floor"));
		if (Route == EClimbRoute::None)
		{
			// Every way up is shut from this floor: the controller's own travel
			// is all that is left (and the lockdown has probably won).
			if (State.TrappedFloor != Floor)
			{
				State.TrappedFloor = Floor;
				UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] %s (%s) has no way up from floor %d at %.0f s (sealed or locked): left to the controller"),
					*BotName(Bot), *ClimbSquadLabel(State.SquadId), Floor, MatchSeconds(Now));
			}
			State.Phase = EClimbPhase::Off;
			State.Request = FAIRequestID::InvalidRequest;
			return;
		}

		State.Route = Route;
		const FVector Entry = RouteEntryPoint(Route, Floor, State.Ordinal);
		const bool bAtEntry = FVector::Dist2D(Location, Entry) < OBEntryReachedCm && FMath::Abs(Location.Z - Entry.Z) < 150.f;
		const bool bClimbing = bAtEntry || OnRoute == Route;
		if (bClimbing)
		{
			Destination = RouteLandingPoint(Route, ClimbTargetFloor(Route, Floor, ObjectiveFloor), State.Ordinal);
			if (!bArmed)
			{
				State.Phase = EClimbPhase::Climb;
			}
		}
		else
		{
			Destination = Entry;
			Acceptance = 90.f;
			if (!bArmed)
			{
				State.Phase = EClimbPhase::Approach;
			}
		}
	}

	// --- The squad -------------------------------------------------------------
	// Too far ahead: hold in the hallway outside this floor's stair door (never
	// in the stairwell, where a waiting bot is a cork) until they close up.
	if (!bArmed && IsStairRoute(State.Route) && IsRouteUsable(State.Route, Floor)
		&& ShouldWaitForSquad(Floor, GetSquadTrailFloor(State.SquadId), static_cast<float>(State.WaitedOnFloor), bUrgent))
	{
		if (State.Phase != EClimbPhase::Wait)
		{
			UE_LOG(LogOneBrahamBots, Verbose, TEXT("[OB.CLIMB] %s (%s) waits on floor %d for the squad (last climber on %d)"),
				*BotName(Bot), *ClimbSquadLabel(State.SquadId), Floor, GetSquadTrailFloor(State.SquadId));
		}
		State.Phase = EClimbPhase::Wait;
		State.WaitedOnFloor += OBClimbDecisionInterval;
		Destination = RouteEntryPoint(State.Route, Floor, State.Ordinal);
		Acceptance = 60.f;
		State.CheckLocation = Location;
		State.CheckTime = Now;
	}

	SteerTo(State, Destination, Acceptance);
}

EClimbRoute UOneBrahamBotClimbDirector::ChooseRoute(FBotState& State, const FVector& From, int32 Floor, double Now, const TCHAR* Reason)
{
	using namespace OneBrahamBotRules;

	FSquadState& Squad = Squads.FindOrAdd(State.SquadId);
	FFloorChoice& Choice = Squad.Choices.FindOrAdd(Floor);

	// The squad decides for the floor and its members on it follow, until the
	// choice is stale or the route stops being usable.
	const double* BlockedUntil = Squad.Blocked.Find(BlockKey(Choice.Route, Floor));
	const bool bStillGood = Choice.Route != EClimbRoute::None && Now < Choice.NextChoice
		&& IsRouteUsable(Choice.Route, Floor) && (BlockedUntil == nullptr || *BlockedUntil <= Now);
	if (bStillGood)
	{
		return Choice.Route;
	}

	const AOneBrahamBotController* Bot = State.Bot.Get();
	const AOneBrahamPlayerState* PS = Bot != nullptr ? Bot->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	const uint8 MyTeam = PS != nullptr ? static_cast<uint8>(PS->Team) : 0;

	TArray<FClimbRouteOption, TInlineAllocator<3>> Options;
	for (const EClimbRoute Route : OBAllRoutes)
	{
		FClimbRouteOption& Option = Options.AddDefaulted_GetRef();
		Option.Route = Route;
		Option.bUsable = IsRouteUsable(Route, Floor);
		Option.DistanceCm = FVector::Dist2D(From, RouteEntryPoint(Route, Floor, State.Ordinal));
		CountOnRoute(Route, Floor, MyTeam, Option.Enemies, Option.Bodies, Option.Friends);
		const double* Until = Squad.Blocked.Find(BlockKey(Route, Floor));
		Option.RecentBlockSeconds = Until != nullptr ? static_cast<float>(FMath::Max(0.0, *Until - Now)) : 0.f;
	}

	// Sticky to what the squad chose here before, or came up by.
	const FFloorChoice* Below = Squad.Choices.Find(Floor - 1);
	const EClimbRoute Current = Choice.Route != EClimbRoute::None ? Choice.Route : (Below != nullptr ? Below->Route : EClimbRoute::None);
	const int32 Pick = PickClimbRoute(Options, PreferredRoute(Squad.Opening, Floor), Current);
	const EClimbRoute Chosen = Pick != INDEX_NONE ? Options[Pick].Route : EClimbRoute::None;

	if (Chosen != Choice.Route)
	{
		FString Detail;
		for (const FClimbRouteOption& Option : Options)
		{
			Detail += Option.bUsable
				? FString::Printf(TEXT(" %s[%.0fm e%d b%d f%d blk%.0f]"), ClimbRouteName(Option.Route), Option.DistanceCm / 100.f,
					Option.Enemies, Option.Bodies, Option.Friends, Option.RecentBlockSeconds)
				: FString::Printf(TEXT(" %s[shut]"), ClimbRouteName(Option.Route));
		}
		const bool bSwitch = Choice.Route != EClimbRoute::None;
		if (bSwitch)
		{
			++Squad.Reroutes;
			++State.Reroutes;
		}
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] %s %s %s from floor %d at %.0f s (%s, by %s):%s"),
			*ClimbSquadLabel(State.SquadId), bSwitch ? TEXT("re-routes to") : TEXT("takes"), ClimbRouteName(Chosen), Floor,
			MatchSeconds(Now), Reason, *BotName(Bot), *Detail);
	}

	Choice.Route = Chosen;
	Choice.NextChoice = Now + OBRouteChoiceSeconds;
	Squad.Route = Chosen;
	return Chosen;
}

void UOneBrahamBotClimbDirector::AssignOpenings(double Now)
{
	using namespace OneBrahamBotRules;

	// Where each squad stands: the middle of its bots' pawns on the ground floor.
	TMap<int32, FVector> Sum;
	TMap<int32, int32> Count;
	for (const FBotState& State : Bots)
	{
		const AOneBrahamBotController* Bot = State.Bot.Get();
		const APawn* Pawn = Bot != nullptr ? Bot->GetPawn() : nullptr;
		if (Pawn != nullptr && OneBrahamConstants::FloorIndexForZ(Pawn->GetActorLocation().Z) == OneBrahamConstants::FloorPlaza)
		{
			Sum.FindOrAdd(State.SquadId) += Pawn->GetActorLocation();
			++Count.FindOrAdd(State.SquadId);
		}
	}

	TArray<int32> SquadIds;
	Squads.GetKeys(SquadIds);
	SquadIds.Sort();
	const bool bEveryoneHere = SquadIds.Num() > 0 && Algo::AllOf(SquadIds, [&Count](int32 Id) { return Count.Contains(Id); });
	if (!bEveryoneHere && MatchSeconds(Now) < 3.0)
	{
		return;
	}
	bOpeningsAssigned = true;

	TArray<TArray<float>> Distances;
	TArray<TArray<float>> Walks;
	for (const int32 Id : SquadIds)
	{
		TArray<float>& Row = Distances.AddDefaulted_GetRef();
		TArray<float>& Walk = Walks.AddDefaulted_GetRef();
		const int32* N = Count.Find(Id);
		for (const EClimbRoute Route : OpeningRoutes)
		{
			// A shut way up (a dead-end escalator, a sealed stair) is nobody's:
			// two squads share a stair before one is sent up a dead end.
			const float Distance = N != nullptr ? FVector::Dist2D(Sum[Id] / *N, RouteEntryPoint(Route, OneBrahamConstants::FloorPlaza, 0)) : 0.f;
			Row.Add(IsRouteUsable(Route, OneBrahamConstants::FloorPlaza) ? Distance : Distance + 10.f * SharedOpeningCostCm);
			Walk.Add(Distance);
		}
	}

	const TArray<EClimbRoute> Opening = AssignOpeningRoutes(Distances);

	// A shared door: the later squad holds until the earlier one is through it.
	TArray<float> WalkToOpening;
	for (int32 Index = 0; Index < SquadIds.Num(); ++Index)
	{
		float Walk = 0.f;
		for (int32 Route = 0; Opening.IsValidIndex(Index) && Route < OpeningRouteCount; ++Route)
		{
			if (OpeningRoutes[Route] == Opening[Index])
			{
				Walk = Walks[Index][Route];
			}
		}
		WalkToOpening.Add(Walk);
	}
	const TArray<float> Holds = SharedOpeningHolds(SquadIds, Opening, WalkToOpening);

	FString Detail;
	for (int32 Index = 0; Index < SquadIds.Num(); ++Index)
	{
		FSquadState& Squad = Squads[SquadIds[Index]];
		Squad.Opening = Opening.IsValidIndex(Index) ? Opening[Index] : EClimbRoute::None;
		Squad.OpeningHold = Holds.IsValidIndex(Index) ? Holds[Index] : 0.f;
		Detail += Squad.OpeningHold > 0.f
			? FString::Printf(TEXT(" %s %s (shared: holds %.1f s);"), *ClimbSquadLabel(SquadIds[Index]), ClimbRouteName(Squad.Opening), Squad.OpeningHold)
			: FString::Printf(TEXT(" %s %s;"), *ClimbSquadLabel(SquadIds[Index]), ClimbRouteName(Squad.Opening));
	}
	UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] ways off the ground floor:%s"), *Detail);
}

bool UOneBrahamBotClimbDirector::IsRouteUsable(EClimbRoute Route, int32 Floor) const
{
	using namespace OneBrahamBotRules;

	if (!RouteClimbsFromFloor(Route, Floor))
	{
		return false;
	}
	if (Route == EClimbRoute::Escalators)
	{
		return AreEscalatorsConnected();
	}

	// The stair door on this floor: a shutter down in it, or a locked door, is
	// a wall. Read from the actors, so a breached door or a shutter held open by
	// someone standing under it reads as it really is.
	const FVector Door = RouteDoorPoint(Route, Floor);
	for (const TWeakObjectPtr<AOneBrahamFireShutter>& Weak : Shutters)
	{
		const AOneBrahamFireShutter* Shutter = Weak.Get();
		if (Shutter != nullptr && FVector::Dist2D(Shutter->GetActorLocation(), Door) < 200.f
			&& OneBrahamConstants::FloorIndexForZ(Shutter->GetActorLocation().Z + 50.f) == Floor
			&& Shutter->BlocksWalking())
		{
			return false;
		}
	}
	for (const TWeakObjectPtr<AOneBrahamAccessZone>& Weak : Doors)
	{
		const AOneBrahamAccessZone* Zone = Weak.Get();
		if (Zone != nullptr && Zone->FloorIndex == Floor && FVector::Dist2D(Zone->GetActorLocation(), Door) < 200.f
			&& !Zone->IsOpen() && !Zone->IsBreached())
		{
			return false;
		}
	}
	return true;
}

bool UOneBrahamBotClimbDirector::AreEscalatorsConnected() const
{
	using namespace OneBrahamBotRules;

	// The escalators arrive in a gallery whose way on to the core is a door
	// (the lift lobby's). Shut, the only way from the top of the escalators to
	// a stair is back down them: a squad sent up them turned round at the top,
	// walked down, and back up, for a minute. So they are a way up only while
	// the navmesh - which the doors' barriers cut and re-join as they lock and
	// open - walks from their top to a stair door on floor 1 without a detour.
	UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	if (World == nullptr || Now < EscalatorCheckExpires)
	{
		return bEscalatorsConnected;
	}
	EscalatorCheckExpires = Now + OBEscalatorCheckSeconds;

	bEscalatorsConnected = false;
	const FVector Top = RouteLandingPoint(EClimbRoute::Escalators, OneBrahamConstants::FloorPlaza, 0);
	FString Detail;
	for (const EClimbRoute Stair : { EClimbRoute::FireStairA, EClimbRoute::SpiralB })
	{
		const FVector Door = RouteEntryPoint(Stair, OneBrahamConstants::FloorLobby, 0);
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Top, Door);
		const bool bComplete = Path != nullptr && Path->IsValid() && !Path->IsPartial();
		const float Length = bComplete ? Path->GetPathLength() : -1.f;
		const bool bDirect = bComplete && Length <= FVector::Dist(Top, Door) * OBEscalatorDetourFactor + OBEscalatorDetourSlackCm;
		bEscalatorsConnected |= bDirect;
		Detail += FString::Printf(TEXT(" %s %.0fm%s"), ClimbRouteName(Stair), Length / 100.f, bDirect ? TEXT("") : TEXT(" (no)"));
	}

	if (!bEscalatorsCheckLogged || bEscalatorsConnected != bEscalatorsLastLogged)
	{
		bEscalatorsCheckLogged = true;
		bEscalatorsLastLogged = bEscalatorsConnected;
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] the escalators %s (top to floor 1's stair doors:%s)"),
			bEscalatorsConnected ? TEXT("lead on to the stairs") : TEXT("are a dead end: not used"), *Detail);
	}
	return bEscalatorsConnected;
}

void UOneBrahamBotClimbDirector::CountOnRoute(EClimbRoute Route, int32 Floor, uint8 MyTeam,
	int32& OutEnemies, int32& OutBodies, int32& OutFriends) const
{
	using namespace OneBrahamBotRules;

	OutEnemies = OutBodies = OutFriends = 0;
	const FVector Door = RouteDoorPoint(Route, Floor);
	for (const FPawnSample& Sample : Pawns)
	{
		const int32 SampleFloor = OneBrahamConstants::FloorIndexForZ(Sample.Location.Z);
		const bool bInDoorway = SampleFloor == Floor && FVector::Dist2D(Sample.Location, Door) < OBDoorwayRadiusCm;
		const bool bOnRoute = IsOnRoute(Route, Sample.Location) && SampleFloor >= Floor && SampleFloor <= Floor + ClimbLegFloors;
		if (!bInDoorway && !bOnRoute)
		{
			continue;
		}

		if (!Sample.bStanding)
		{
			++OutBodies;
		}
		else if (Sample.Team == MyTeam)
		{
			++OutFriends;
		}
		else
		{
			++OutEnemies;
		}
	}
}

int32 UOneBrahamBotClimbDirector::GetSquadTrailFloor(int32 SquadId) const
{
	const UOneBrahamLockdownComponent* Director = Lockdown.Get();
	const double Now = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;

	int32 Trail = INDEX_NONE;
	for (const FBotState& Other : Bots)
	{
		if (Other.SquadId != SquadId || Other.Floor == INDEX_NONE)
		{
			continue;
		}
		switch (Other.Phase)
		{
		case EClimbPhase::Off:
		case EClimbPhase::Departing:
		case EClimbPhase::Handoff:
			continue;
		default:
			break;
		}

		// Somebody who has not gained a floor in a minute, or is behind a sealed
		// floor, is not coming: the squad does not wait for them.
		if (Now - Other.FloorSince > OBTrailGiveUpSeconds || (Director != nullptr && Director->IsFloorSealed(Other.Floor)))
		{
			continue;
		}
		Trail = Trail == INDEX_NONE ? Other.Floor : FMath::Min(Trail, Other.Floor);
	}
	return Trail;
}

void UOneBrahamBotClimbDirector::SteerTo(FBotState& State, const FVector& Destination, float Acceptance)
{
	AOneBrahamBotController* Bot = State.Bot.Get();
	UPathFollowingComponent* Following = Bot != nullptr ? Bot->GetPathFollowingComponent() : nullptr;
	if (Following == nullptr)
	{
		return;
	}

	const bool bOnIt = State.Request.IsValid() && Following->GetCurrentRequestId() == State.Request
		&& Following->GetStatus() != EPathFollowingStatus::Idle
		&& FVector::Dist(State.Waypoint, Destination) < 100.f;
	if (bOnIt)
	{
		return;
	}

	// The controller's own move settings: pathfinding, projected onto the
	// navmesh, strafing (its aim is its own), partial paths accepted.
	const EPathFollowingRequestResult::Type Result = Bot->MoveToLocation(Destination, Acceptance,
		/*bStopOnOverlap*/ true, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPaths*/ true);

	State.Waypoint = Destination;
	State.Request = Result == EPathFollowingRequestResult::RequestSuccessful
		? Following->GetCurrentRequestId() : FAIRequestID::InvalidRequest;

	if (Result == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogOneBrahamBots, Verbose, TEXT("[OB.CLIMB] %s: no path to %s (%s)"), *BotName(Bot),
			*Destination.ToCompactString(), OneBrahamBotRules::ClimbRouteName(State.Route));
	}
}

void UOneBrahamBotClimbDirector::NoteFloor(FBotState& State, int32 Floor, double Now)
{
	if (Floor == State.Floor)
	{
		return;
	}

	State.Floor = Floor;
	State.FloorSince = Now;
	State.WaitedOnFloor = 0.0;

	if (Floor <= State.HighestFloor)
	{
		return;
	}
	State.HighestFloor = Floor;
	State.HighestFloorTime = MatchSeconds(Now);

	FSquadState& Squad = Squads.FindOrAdd(State.SquadId);
	if (Floor > Squad.HighestFloor)
	{
		Squad.HighestFloor = Floor;
		Squad.HighestFloorTime = MatchSeconds(Now);
		Squad.HighestFloorBy = BotName(State.Bot.Get());
		if (bStarted)
		{
			UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] %s reaches floor %d at %.0f s (%s, %s)"),
				*ClimbSquadLabel(State.SquadId), Floor, Squad.HighestFloorTime, *Squad.HighestFloorBy,
				OneBrahamBotRules::ClimbRouteName(State.Route));
		}
	}
}

void UOneBrahamBotClimbDirector::MarkBlocked(FBotState& State, int32 Floor, double Now, const TCHAR* Why)
{
	using namespace OneBrahamBotRules;

	if (State.Route == EClimbRoute::None)
	{
		return;
	}

	FSquadState& Squad = Squads.FindOrAdd(State.SquadId);
	const uint32 Key = BlockKey(State.Route, Floor);
	const double* Existing = Squad.Blocked.Find(Key);
	const bool bNew = Existing == nullptr || *Existing <= Now;
	Squad.Blocked.Add(Key, Now + RouteBlockMemorySeconds);

	// Re-choose now, from where the bot stands.
	Squad.Choices.FindOrAdd(Floor).NextChoice = 0.0;

	// A jam inside a stairwell: walk out to the floor and take another way.
	const EClimbRoute Jammed = State.Route;
	State.Route = EClimbRoute::None;
	State.Request = FAIRequestID::InvalidRequest;

	if (bNew)
	{
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.CLIMB] %s (%s) is blocked on %s at floor %d (%s): the squad re-chooses"),
			*BotName(State.Bot.Get()), *ClimbSquadLabel(State.SquadId), ClimbRouteName(Jammed), Floor, Why);
	}
}

// ===========================================================================
//  Queries and logging
// ===========================================================================

int32 UOneBrahamBotClimbDirector::GetSquadHighestFloor(int32 SquadId) const
{
	const FSquadState* Squad = Squads.Find(SquadId);
	return Squad != nullptr ? Squad->HighestFloor : INDEX_NONE;
}

OneBrahamBotRules::EClimbRoute UOneBrahamBotClimbDirector::GetSquadRoute(int32 SquadId) const
{
	const FSquadState* Squad = Squads.Find(SquadId);
	return Squad != nullptr ? Squad->Route : OneBrahamBotRules::EClimbRoute::None;
}

double UOneBrahamBotClimbDirector::MatchSeconds(double Now) const
{
	return bStarted ? Now - MatchStartTime : 0.0;
}

FString UOneBrahamBotClimbDirector::GetSummary() const
{
	TArray<int32> SquadIds;
	Squads.GetKeys(SquadIds);
	SquadIds.Sort();

	FString Out;
	for (const int32 SquadId : SquadIds)
	{
		const FSquadState& Squad = Squads[SquadId];
		int32 Standing = 0;
		int32 Members = 0;
		int32 Fights = 0;
		int32 Shots = 0;
		float Damage = 0.f;
		FString Members_;
		for (const FBotState& State : Bots)
		{
			if (State.SquadId != SquadId)
			{
				continue;
			}
			++Members;
			const AOneBrahamBotController* Bot = State.Bot.Get();
			const bool bUp = Bot != nullptr && Bot->GetPawn() != nullptr && Bot->GetBotState() != EOneBrahamBotState::Downed;
			Standing += bUp ? 1 : 0;
			Fights += State.Fights;
			Shots += Bot != nullptr ? Bot->GetShotsFired() : 0;
			Damage += Bot != nullptr ? Bot->GetDamageDealt() : 0.f;
			Members_ += FString::Printf(TEXT("\n    %s: floor %d, best %d at %.0f s, %s, %s, fights %d, break-offs %d, re-routes %d"),
				*BotName(Bot), State.Floor, State.HighestFloor, State.HighestFloorTime, ClimbPhaseName(State.Phase),
				bUp ? TEXT("up") : TEXT("down"), State.Fights, State.BreakOffs, State.Reroutes);
		}

		Out += FString::Printf(TEXT("  %s: highest floor %d at %.0f s (%s), route %s, %d/%d bots standing, fights %d, break-offs %d, re-routes %d, shots %d, damage %.0f%s\n"),
			*ClimbSquadLabel(SquadId), Squad.HighestFloor, Squad.HighestFloorTime, *Squad.HighestFloorBy,
			OneBrahamBotRules::ClimbRouteName(Squad.Route), Standing, Members, Fights, Squad.BreakOffs, Squad.Reroutes,
			Shots, Damage, *Members_);
	}
	return Out;
}

const TCHAR* UOneBrahamBotClimbDirector::ClimbPhaseName(EClimbPhase Phase)
{
	switch (Phase)
	{
	case EClimbPhase::Departing: return TEXT("departing");
	case EClimbPhase::Approach:  return TEXT("approach");
	case EClimbPhase::Climb:     return TEXT("climb");
	case EClimbPhase::Wait:      return TEXT("wait");
	case EClimbPhase::Fight:     return TEXT("fight");
	case EClimbPhase::BreakOff:  return TEXT("break-off");
	case EClimbPhase::Skirmish:  return TEXT("skirmish");
	case EClimbPhase::Yield:     return TEXT("unstick");
	case EClimbPhase::Handoff:   return TEXT("objective");
	default:                return TEXT("off");
	}
}

FString UOneBrahamBotClimbDirector::ClimbSquadLabel(int32 SquadId)
{
	static const TCHAR* Names[] = { TEXT("Alpha"), TEXT("Bravo"), TEXT("Charlie"), TEXT("Delta") };
	return SquadId >= 0 && SquadId < UE_ARRAY_COUNT(Names) ? FString(Names[SquadId]) : FString::Printf(TEXT("Squad %d"), SquadId);
}
