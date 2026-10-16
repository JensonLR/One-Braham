// Copyright (c) One Braham. All Rights Reserved.

#include "Bots/OneBrahamBotController.h"

#include "AI/OneBrahamBotRules.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamCharacterMovementComponent.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "OneBraham.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"

namespace
{
	/** How long a bot remembers a target it can no longer see. */
	constexpr float OBTargetMemorySeconds = 6.f;

	/** Decision cadence for movement: 4 Hz is plenty and keeps the tick cheap. */
	constexpr float OBMovementDecisionInterval = 0.25f;

	/** Sight scan cadence and reach. */
	constexpr float OBScanInterval = 0.25f;
	constexpr float OBSightRadiusCm = 5200.f;
	/** Half-angle of the sight cone (Docs/06 s5: 100 degrees horizontal). */
	constexpr float OBSightHalfAngleDegrees = 50.f;
	/** Inside this a bot notices an enemy whichever way it is facing. */
	constexpr float OBAwarenessRadiusCm = 900.f;

	/** Engagement ranges, cm. Bots do not fight across the whole floor. */
	constexpr float OBMinEngageRangeCm = 250.f;
	constexpr float OBIdealEngageRangeCm = 1100.f;

	/** How often a travelling bot re-plans its path (partial paths grow as it climbs). */
	constexpr float OBRepathSeconds = 3.f;

	/** Radius of the random walk that gets a stuck bot out of a dead end. */
	constexpr float OBUnstickRadiusCm = 1200.f;
	constexpr float OBUnstickSeconds = 3.f;

	/** Radius a bot holds around its objective once it has arrived. */
	constexpr float OBHoldRadiusCm = 800.f;

	/** Single-shot weapons: the fastest a bot re-presses the trigger. */
	constexpr float OBMinTapIntervalSeconds = 0.22f;

	TAutoConsoleVariable<float> CVarBotStatusInterval(
		TEXT("ob.Bots.StatusInterval"),
		0.f,
		TEXT("Seconds between [OB.BOTS] status lines per bot (location, state, shots, damage). 0 = off."),
		ECVF_Default);

	FAutoConsoleCommandWithWorld GBotDumpCommand(
		TEXT("ob.Bots.Dump"),
		TEXT("Logs one line per bot: state, floor, location, target, shots and damage."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (World == nullptr)
			{
				return;
			}

			int32 Count = 0;
			for (TActorIterator<AOneBrahamBotController> It(World); It; ++It)
			{
				UE_LOG(LogOneBrahamBots, Display, TEXT("[OB.BOTS] %s"), *It->GetBotDebugString());
				++Count;
			}
			UE_LOG(LogOneBrahamBots, Display, TEXT("[OB.BOTS] %d bot(s)"), Count);
		}));

	/** The name a controller goes by in the log: its player name, else its object name. */
	FString BotLabel(const AController* Controller)
	{
		if (Controller == nullptr)
		{
			return TEXT("none");
		}
		return Controller->PlayerState != nullptr && !Controller->PlayerState->GetPlayerName().IsEmpty()
			? Controller->PlayerState->GetPlayerName()
			: Controller->GetName();
	}

	/** The pawn behind a damage instigator: the pawn itself, its weapon, or its controller. */
	APawn* ResolveInstigatorPawn(AActor* Instigator)
	{
		for (AActor* Actor = Instigator; Actor != nullptr; Actor = Actor->GetOwner())
		{
			if (APawn* Pawn = Cast<APawn>(Actor))
			{
				return Pawn;
			}
			if (const AController* Controller = Cast<AController>(Actor))
			{
				return Controller->GetPawn();
			}
		}
		return Instigator != nullptr ? Instigator->GetInstigator() : nullptr;
	}
}

AOneBrahamBotController::AOneBrahamBotController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	// Every other frame at 60: aim has to be smooth for the people watching the
	// bot turn; the decisions themselves run on their own slower clocks.
	PrimaryActorTick.TickInterval = 1.f / 30.f;

	// Bots get a PlayerState so teams, damage attribution and the scoreboard
	// behave exactly like they do for a human player.
	bWantsPlayerState = true;

	// The bot sets its own control rotation (UpdateAim); copying the pawn's
	// orientation back every tick would erase the pitch of every shot.
	bSetControlRotationFromPawnOrientation = false;

	ConfigurePerception();
}

void AOneBrahamBotController::ConfigurePerception()
{
	OBPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("OBPerception"));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = OBSightRadiusCm;
	SightConfig->LoseSightRadius = 6200.f;
	SightConfig->PeripheralVisionAngleDegrees = OBSightHalfAngleDegrees;
	SightConfig->SetMaxAge(OBTargetMemorySeconds);
	SightConfig->AutoSuccessRangeFromLastSeenLocation = 900.f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 6500.f;
	HearingConfig->SetMaxAge(4.f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	OBPerception->ConfigureSense(*SightConfig);
	OBPerception->ConfigureSense(*HearingConfig);
	OBPerception->SetDominantSense(SightConfig->GetSenseImplementation());
	SetPerceptionComponent(*OBPerception);
}

void AOneBrahamBotController::BeginPlay()
{
	Super::BeginPlay();

	if (OBPerception != nullptr)
	{
		OBPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AOneBrahamBotController::HandleTargetPerceptionUpdated);
	}

	// The pawn is the game mode's to spawn (AOneBrahamGameMode::SpawnBotPawn):
	// it knows the phase, the mode's spawn rules and whether this bot has a
	// life left. A bot that spawned itself here used to do so before it had a
	// team, so every practice bot was on team None and fought everybody.
}

void AOneBrahamBotController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] summary %s: shots=%d hits=%d damage=%.0f travelled=%.0fm highestFloor=%d stalls=%d"),
		*BotLabel(this), ShotsFired, ShotsHit, DamageDealt, DistanceTravelledCm / 100.f,
		HighestFloorReached, StallCount);

	Super::EndPlay(EndPlayReason);
}

void AOneBrahamBotController::InitBot(EOneBrahamTeam Team, int32 SquadId, int32 OrdinalInSquad, float Skill,
	const FString& DisplayName, EOneBrahamMatchMode Mode)
{
	PreferredTeam = Team;
	PreferredSquadId = SquadId;
	SquadOrdinal = OrdinalInSquad;
	BotSkill = FMath::Clamp(Skill, 0.f, 1.f);

	AOneBrahamPlayerState* PS = GetPlayerState<AOneBrahamPlayerState>();
	if (PS == nullptr)
	{
		OB_WARN("Bot '%s' has no AOneBrahamPlayerState; it cannot join a squad.", *GetName());
		return;
	}

	PS->Team = Team;
	PS->SquadId = SquadId;
	PS->bIsSquadLeader = false;
	PS->SessionMode = Mode;
	PS->SetPlayerName(DisplayName);

	// The native tag constant, not a string: this line used to request
	// "OB.Character.Archetype.Intern", which has never existed - the archetype
	// is called Graduate in Docs/13 and in the character registry. Requested
	// with ErrorIfNotFound = false, a wrong name produces no error at all, just
	// an invalid tag on every bot's PlayerState, and any code keying on the
	// archetype silently takes its fallback branch.
	PS->ArchetypeTag = OneBrahamTags::Character_Archetype_Graduate;
}

void AOneBrahamBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn == nullptr)
	{
		return;
	}

	++LivesStarted;
	HomeLocation = InPawn->GetActorLocation();
	LastTickLocation = HomeLocation;
	StallCheckLocation = HomeLocation;
	StallCheckSeconds = 0.f;
	UnstickRemaining = 0.f;
	RepathAccumulator = OBRepathSeconds;
	CurrentTarget = nullptr;
	ReactionRemaining = 0.f;
	bLoggedFirstPath = false;
	BotState = EOneBrahamBotState::Patrol;

	// Face the way the spawn point faces: the control rotation is ours now.
	SetControlRotation(InPawn->GetActorRotation());

	// Bot pawns never feed the anti-cheat speed report and have no microphone.
	if (AOneBrahamCharacter* Character = GetBotCharacter())
	{
		Character->SetBotControlled(true);

		if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			Health->OnDamageTaken.AddUniqueDynamic(this, &AOneBrahamBotController::HandleDamageTaken);
		}
	}

	RefreshObjective();

	// The navmesh is the most common reason a bot stands still; say up front
	// whether this one is standing on it.
	bool bOnNav = false;
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Projected;
		bOnNav = Nav->ProjectPointToNavigation(HomeLocation, Projected, FVector(200.f, 200.f, 300.f));
	}

	// (The kit arrives after possession, in ServerOnRespawned: the status line
	// and the summary carry the ammo and the shots.)
	UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] %s possessed %s at %s (floor %d, team %s, squad %d, skill %.2f, onNavmesh=%s, objective=%s floor %d)"),
		*BotLabel(this), *InPawn->GetName(), *HomeLocation.ToCompactString(),
		OneBrahamConstants::FloorIndexForZ(HomeLocation.Z),
		*UEnum::GetValueAsString(PreferredTeam), PreferredSquadId, BotSkill,
		bOnNav ? TEXT("yes") : TEXT("NO"),
		bHasObjective ? *ObjectiveLocation.ToCompactString() : TEXT("none"), ObjectiveFloor);
}

void AOneBrahamBotController::OnUnPossess()
{
	if (AOneBrahamCharacter* Character = GetBotCharacter())
	{
		Character->SetBotControlled(false);

		if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			Health->OnDamageTaken.RemoveDynamic(this, &AOneBrahamBotController::HandleDamageTaken);
		}
	}

	StopMovement();
	CurrentTarget = nullptr;
	BotState = EOneBrahamBotState::Idle;
	Super::OnUnPossess();
}

// ===========================================================================
//  Perception
// ===========================================================================

void AOneBrahamBotController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (Actor == nullptr || !HasAuthority() || GetBotCharacter() == nullptr)
	{
		return;
	}

	if (IsFriendly(Actor))
	{
		return;
	}

	if (!Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	// Hearing an enemy is a reason to turn round, not a firing solution: the
	// target is taken, and the line-of-sight test decides when to shoot.
	if (IsValidEnemy(Actor))
	{
		AcquireTarget(Actor);
	}
}

void AOneBrahamBotController::HandleDamageTaken(float Damage, AActor* DamageInstigator, FVector InstigatorLocation)
{
	// "A bot that has been shot at investigates" (Docs/06 s5): whoever shot it
	// becomes its target if it had none, so it turns to face the fire.
	APawn* Shooter = ResolveInstigatorPawn(DamageInstigator);
	if (Shooter != nullptr && Shooter != GetPawn() && !CurrentTarget.IsValid() && IsValidEnemy(Shooter))
	{
		AcquireTarget(Shooter);
	}
}

void AOneBrahamBotController::AcquireTarget(AActor* NewTarget)
{
	if (NewTarget == nullptr || NewTarget == CurrentTarget.Get())
	{
		return;
	}

	// Reaction time is the single most important skill knob, and it is what
	// stops bots from feeling like aimbots. Switching between two targets in
	// the same fight is quicker than noticing the first one.
	const bool bHadTarget = CurrentTarget.IsValid();
	CurrentTarget = NewTarget;
	TimeSinceLostSight = 0.f;
	ReactionRemaining = FMath::Lerp(0.85f, 0.22f, BotSkill) * FMath::FRandRange(0.85f, 1.3f) * (bHadTarget ? 0.5f : 1.f);
}

void AOneBrahamBotController::ScanForEnemies()
{
	const AOneBrahamCharacter* Character = GetBotCharacter();
	UWorld* World = GetWorld();
	if (Character == nullptr || World == nullptr)
	{
		return;
	}

	const FVector Eye = Character->GetEyeLocation();
	const FVector Forward = GetControlRotation().Vector().GetSafeNormal2D();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(OBSightHalfAngleDegrees));

	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();

	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		AOneBrahamCharacter* Other = *It;
		if (Other == Character || !IsValidEnemy(Other))
		{
			continue;
		}

		const FVector ToOther = Other->GetActorLocation() - Eye;
		const float Distance = ToOther.Size();
		if (Distance > OBSightRadiusCm || Distance >= BestDistance)
		{
			continue;
		}

		const bool bInCone = FVector::DotProduct(Forward, ToOther.GetSafeNormal2D()) >= CosHalfAngle;
		if (!bInCone && Distance > OBAwarenessRadiusCm)
		{
			continue;
		}

		if (!HasLineOfSight(Other))
		{
			continue;
		}

		Best = Other;
		BestDistance = Distance;
	}

	if (Best == nullptr)
	{
		return;
	}

	// Keep a target that is still visible unless the new one is much closer:
	// flicking between two people is what a bot with no memory does.
	AActor* Current = CurrentTarget.Get();
	if (Current != nullptr && Current != Best && HasLineOfSight(Current))
	{
		const float CurrentDistance = FVector::Dist(Current->GetActorLocation(), Eye);
		if (BestDistance > CurrentDistance * 0.6f)
		{
			return;
		}
	}

	AcquireTarget(Best);
}

// ===========================================================================
//  Tick
// ===========================================================================

void AOneBrahamBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	AOneBrahamCharacter* Character = GetBotCharacter();
	if (Character == nullptr)
	{
		// Not spawned yet, or between lives: the game mode brings it back.
		BotState = EOneBrahamBotState::Idle;
		return;
	}

	// The capsule must not collide with what the bot is carrying. A held
	// weapon hangs off the hand, in front of the capsule, and its mesh answers
	// the Pawn channel: the capsule's move sweep met the bot's own rifle and the
	// bot stood still, pushing, wherever it had turned to face a wall. The kit
	// changes on every respawn, so the list is refreshed as it goes.
	// Other people's weapons are the same problem one step removed: two bots
	// shoulder to shoulder at a spawn locked each other in place by their
	// rifles. Carried weapons only; a weapon on the floor is left to the world.
	CarriedIgnoreAccumulator += DeltaSeconds;
	if (CarriedIgnoreAccumulator >= 1.f || LivesStartedAtLastIgnore != LivesStarted)
	{
		CarriedIgnoreAccumulator = 0.f;
		LivesStartedAtLastIgnore = LivesStarted;
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			for (TActorIterator<AOneBrahamWeaponBase> It(GetWorld()); It; ++It)
			{
				AOneBrahamWeaponBase* Weapon = *It;
				// Carried, attached or not: a holstered sidearm is not attached to
				// anything and stays where its holder spawned, a pawn-blocking
				// box on the spawn point that the next bot sent there stood in.
				if (Weapon != nullptr && !Weapon->IsGroundPickup() && !Capsule->GetMoveIgnoreActors().Contains(Weapon))
				{
					Capsule->IgnoreActorWhenMoving(Weapon, true);
				}
			}
		}
	}

	const FVector Location = Character->GetActorLocation();
	const float Step = FVector::Dist(Location, LastTickLocation);
	if (Step < 600.f)
	{
		// A larger jump is a respawn or a teleport, not walking.
		DistanceTravelledCm += Step;
	}
	LastTickLocation = Location;

	CurrentFloor = OneBrahamConstants::FloorIndexForZ(Location.Z);
	HighestFloorReached = FMath::Max(HighestFloorReached, CurrentFloor);

	ScanAccumulator += DeltaSeconds;
	if (ScanAccumulator >= OBScanInterval)
	{
		ScanAccumulator = 0.f;
		ScanForEnemies();
	}

	UpdateCombatState(DeltaSeconds);
	UpdateMovement(DeltaSeconds);
	UpdateAim(DeltaSeconds);
	UpdateReload();
	UpdateFiring(DeltaSeconds);
	UpdateStatusLog(DeltaSeconds);
}

void AOneBrahamBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	// The engine's version points the controller at its focus, or copies the
	// pawn's facing back, and in both cases zeroes the pitch unless the focus is
	// a pawn: a bot aiming at someone on the stairs above it would aim level
	// every frame. UpdateAim owns the control rotation; all this does is turn
	// the body to match it.
	APawn* const MyPawn = GetPawn();
	if (MyPawn != nullptr && bUpdatePawn)
	{
		const FRotator Control = GetControlRotation();
		if (!MyPawn->GetActorRotation().Equals(FRotator(0.f, Control.Yaw, 0.f), 1e-3f))
		{
			MyPawn->FaceRotation(Control, DeltaTime);
		}
	}
}

void AOneBrahamBotController::UpdateCombatState(float DeltaSeconds)
{
	AOneBrahamCharacter* Character = GetBotCharacter();
	const UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;
	const UOneBrahamDownedComponent* Downed = Character != nullptr ? Character->GetDownedComponent() : nullptr;

	if ((Health != nullptr && !Health->IsAlive()) || (Downed != nullptr && Downed->IsDowned()))
	{
		if (BotState != EOneBrahamBotState::Downed)
		{
			StopMovement();
		}
		BotState = EOneBrahamBotState::Downed;
		CurrentTarget = nullptr;
		return;
	}

	AActor* Target = CurrentTarget.Get();

	// Target validity: dead, downed, gone, or forgotten after breaking line of sight.
	if (Target != nullptr && !IsValidEnemy(Target))
	{
		CurrentTarget = nullptr;
		Target = nullptr;
	}

	bool bVisible = false;
	if (Target != nullptr)
	{
		bVisible = HasLineOfSight(Target);
		if (bVisible)
		{
			TimeSinceLostSight = 0.f;
		}
		else
		{
			TimeSinceLostSight += DeltaSeconds;
			if (TimeSinceLostSight > OBTargetMemorySeconds)
			{
				CurrentTarget = nullptr;
				Target = nullptr;
			}
		}
	}

	if (Target == nullptr)
	{
		if (BotState != EOneBrahamBotState::Reload)
		{
			BotState = EOneBrahamBotState::Patrol;
		}
		return;
	}

	if (ReactionRemaining > 0.f)
	{
		ReactionRemaining = FMath::Max(0.f, ReactionRemaining - DeltaSeconds);
	}

	if (BotState == EOneBrahamBotState::Reload)
	{
		return;
	}

	const float Distance = FVector::Dist(Character->GetActorLocation(), Target->GetActorLocation());

	// Low-skill bots back off when hurt; high-skill bots hold and trade. The roll
	// is per decision, not per frame, so a hurt bot does not jitter.
	if (Health != nullptr && Health->GetHealthFraction() < 0.35f && BotSkill < 0.5f)
	{
		BotState = EOneBrahamBotState::Retreat;
	}
	else if (!bVisible)
	{
		BotState = EOneBrahamBotState::Advance;
	}
	else if (Distance < OBMinEngageRangeCm)
	{
		BotState = EOneBrahamBotState::Retreat;
	}
	else if (Distance > OBIdealEngageRangeCm * 1.6f)
	{
		BotState = EOneBrahamBotState::Advance;
	}
	else
	{
		BotState = EOneBrahamBotState::Engage;
	}
}

// ===========================================================================
//  Movement
// ===========================================================================

void AOneBrahamBotController::UpdateMovement(float DeltaSeconds)
{
	MovementAccumulator += DeltaSeconds;
	RepathAccumulator += DeltaSeconds;
	StallCheckSeconds += DeltaSeconds;
	UnstickRemaining = FMath::Max(0.f, UnstickRemaining - DeltaSeconds);

	if (MovementAccumulator < OBMovementDecisionInterval)
	{
		return;
	}

	const float DecisionDelta = MovementAccumulator;
	MovementAccumulator = 0.f;

	AOneBrahamCharacter* Character = GetBotCharacter();
	if (Character == nullptr)
	{
		return;
	}

	switch (BotState)
	{
	case EOneBrahamBotState::Downed:
	case EOneBrahamBotState::Idle:
		StopMovement();
		return;

	case EOneBrahamBotState::Patrol:
	case EOneBrahamBotState::Reload:
		UpdateObjectiveMovement(DecisionDelta);
		return;

	case EOneBrahamBotState::Advance:
	case EOneBrahamBotState::Engage:
	case EOneBrahamBotState::Retreat:
	{
		AActor* Target = CurrentTarget.Get();
		if (Target == nullptr)
		{
			return;
		}

		const FVector BotLocation = Character->GetActorLocation();
		const FVector TargetLocation = Target->GetActorLocation();
		const FVector ToTarget = (TargetLocation - BotLocation).GetSafeNormal2D();
		const FVector Lateral = FVector::CrossProduct(FVector::UpVector, ToTarget);

		if (BotState == EOneBrahamBotState::Advance)
		{
			// Out of sight: walk to where the target is (the navmesh finds the
			// way round, or up the stairs). In sight but far: close to range.
			if (!HasLineOfSight(Target))
			{
				MoveBotTo(TargetLocation, 150.f);
			}
			else
			{
				const float Distance = FVector::Dist2D(BotLocation, TargetLocation);
				const float StepCm = FMath::Clamp(Distance - OBIdealEngageRangeCm, 150.f, 900.f);
				MoveBotTo(BotLocation + ToTarget * StepCm + Lateral * FMath::FRandRange(-200.f, 200.f), 70.f);
			}
		}
		else if (BotState == EOneBrahamBotState::Retreat)
		{
			// Back off, with a lateral component so retreating is not a straight
			// line into the next burst.
			MoveBotTo(BotLocation - ToTarget * 700.f + Lateral * FMath::FRandRange(-450.f, 450.f), 70.f);
		}
		else if (FMath::FRand() < 0.35f)
		{
			// Engage: a sidestep now and then. Bots do not strafe-shoot well,
			// which is intentional.
			MoveBotTo(BotLocation + Lateral * FMath::FRandRange(-320.f, 320.f), 50.f);
		}
		return;
	}

	default:
		return;
	}
}

void AOneBrahamBotController::UpdateObjectiveMovement(float DeltaSeconds)
{
	AOneBrahamCharacter* Character = GetBotCharacter();
	if (Character == nullptr)
	{
		return;
	}

	const FVector Location = Character->GetActorLocation();

	if (!bHasObjective && !RefreshObjective())
	{
		// No objective in this map: hold the area around the spawn.
		if (GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			MoveBotTo(RandomNavPointNear(HomeLocation, OBHoldRadiusCm), 90.f);
		}
		return;
	}

	// --- Stall check ---------------------------------------------------------
	// A bot whose path ends at a locked door, or whose partial path stops at the
	// edge of what the navmesh query could see, walks somewhere random for a few
	// seconds and asks again. The re-plan from a new place usually finds more.
	const bool bArrived = FVector::Dist(Location, ObjectiveLocation) < OneBrahamBotRules::ObjectiveArrivalCm
		&& FMath::Abs(Location.Z - ObjectiveLocation.Z) < OneBrahamConstants::HalfFloorHeightCm;

	if (StallCheckSeconds >= OneBrahamBotRules::StallSeconds)
	{
		const float Moved = FVector::Dist(Location, StallCheckLocation);
		if (!bArrived && OneBrahamBotRules::IsStalled(Moved, StallCheckSeconds) && UnstickRemaining <= 0.f)
		{
			++StallCount;

			// What is in the way: the navmesh says walk on, the capsule says no.
			// Named in the log, because it is almost always a piece of the map
			// that blocks pawns but was left out of the navmesh (or the reverse).
			FString Blocker = TEXT("nothing");
			const UPathFollowingComponent* Following = GetPathFollowingComponent();
			const FNavPathSharedPtr Path = Following != nullptr ? Following->GetPath() : nullptr;
			if (Path.IsValid() && Path->IsValid() && GetWorld() != nullptr)
			{
				const int32 NextIndex = FMath::Min(static_cast<int32>(Following->GetNextPathIndex()), Path->GetPathPoints().Num() - 1);
				const FVector Next = NextIndex >= 0 ? FVector(Path->GetPathPoints()[NextIndex].Location) : Location;
				const FVector Ahead = Location + (Next - Location).GetSafeNormal2D() * 120.f;
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBotStall), false, Character);
				TArray<AActor*> Carried;
				Character->GetAttachedActors(Carried, true, true);
				Params.AddIgnoredActors(Carried);
				if (GetWorld()->SweepSingleByChannel(Hit, Location, Ahead, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(34.f, 80.f), Params) && Hit.GetActor() != nullptr)
				{
					Blocker = FString::Printf(TEXT("%s (%s)"), *Hit.GetActor()->GetActorNameOrLabel(), *GetNameSafe(Hit.GetComponent()));
				}
				// The move the character itself would make: its own capsule, its
				// own channel and responses, ten centimetres along its velocity.
				if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
				{
					const FVector Dir = Character->GetVelocity().GetSafeNormal2D();
					FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
					FHitResult MoveHit;
					const bool bMoveHit = GetWorld()->SweepSingleByChannel(MoveHit, Location, Location + Dir * 10.f,
						Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Params, Responses);
					Blocker += FString::Printf(TEXT(", capsule move: %s%s%s pen %.1f normal %s"),
						bMoveHit ? TEXT("HIT ") : TEXT("free"),
						bMoveHit && MoveHit.GetActor() != nullptr ? *MoveHit.GetActor()->GetActorNameOrLabel() : TEXT(""),
						bMoveHit && MoveHit.bStartPenetrating ? TEXT(" (start penetrating)") : TEXT(""),
						MoveHit.PenetrationDepth, *MoveHit.ImpactNormal.ToCompactString());
				}

				// Anything the capsule is already pressed into, a little inflated.
				TArray<FOverlapResult> Touching;
				GetWorld()->OverlapMultiByChannel(Touching, Location, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(40.f, 86.f), Params);
				FString TouchList;
				for (const FOverlapResult& Touch : Touching)
				{
					if (Touch.bBlockingHit && Touch.GetActor() != nullptr)
					{
						TouchList += Touch.GetActor()->GetActorNameOrLabel() + TEXT(" ");
					}
				}
				if (const UOneBrahamCharacterMovementComponent* OBMove = Character->GetOBCharacterMovement())
				{
					Blocker += FString::Printf(TEXT(", maxSpeed %.0f, obState %d, locked %d"), OBMove->GetMaxSpeed(),
						static_cast<int32>(OBMove->GetOBMovementMode()), OBMove->IsMovementLocked() ? 1 : 0);
				}
				Blocker += FString::Printf(TEXT(", touching [%s], next path point %s (%d/%d), status %d, velocity %s, accel %.0f, mode %d"),
					*TouchList, *Next.ToCompactString(), NextIndex, Path->GetPathPoints().Num(),
					static_cast<int32>(Following->GetStatus()), *Character->GetVelocity().ToCompactString(),
					Character->GetCharacterMovement() != nullptr ? Character->GetCharacterMovement()->GetCurrentAcceleration().Size() : -1.f,
					Character->GetCharacterMovement() != nullptr ? static_cast<int32>(Character->GetCharacterMovement()->MovementMode.GetValue()) : -1);
			}

			// Out of the corner first, in a straight line the capsule can really
			// walk: the navmesh is what walked it in, so it is not asked how to
			// walk out. A random navigable point was the old answer, and a point
			// that is only "reachable" can be outside the building, which is the
			// long way round through the revolving doors and into the next trap.
			UnstickRemaining = OBUnstickSeconds;
			if (FindFreeStep(Location, (ObjectiveLocation - Location).GetSafeNormal2D(), TravelDestination))
			{
				MoveToLocation(TravelDestination, 20.f, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ false,
					/*bProjectDestinationToNavigation*/ false, /*bCanStrafe*/ true, nullptr, /*bAllowPartialPaths*/ true);
			}
			else
			{
				TravelDestination = RandomNavPointNear(Location, OBUnstickRadiusCm);
				MoveBotTo(TravelDestination, 90.f);
			}
			UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] %s stalled at %s (moved %.0f cm in %.1f s, blocked by %s); stepping to %s"),
				*BotLabel(this), *Location.ToCompactString(), Moved, StallCheckSeconds, *Blocker,
				*TravelDestination.ToCompactString());
		}
		StallCheckLocation = Location;
		StallCheckSeconds = 0.f;
	}

	if (UnstickRemaining > 0.f)
	{
		return;
	}

	if (bArrived)
	{
		// On the objective: hold it, walking between points around it. A bot
		// standing still on the pad is a target, not a defender.
		if (GetMoveStatus() == EPathFollowingStatus::Idle || RepathAccumulator >= OBRepathSeconds * 2.f)
		{
			RepathAccumulator = 0.f;
			TravelDestination = RandomNavPointNear(ObjectiveLocation, OBHoldRadiusCm);
			MoveBotTo(TravelDestination, 90.f);
		}
		return;
	}

	// Travelling. Re-plan on a clock: a partial path (the query ran out of nodes
	// on a fifteen-floor climb) gets longer every time the bot is further up.
	if (GetMoveStatus() == EPathFollowingStatus::Idle || RepathAccumulator >= OBRepathSeconds)
	{
		RepathAccumulator = 0.f;
		RefreshObjective();
		TravelDestination = ObjectiveLocation;
		MoveBotTo(TravelDestination, 120.f);
	}
}

void AOneBrahamBotController::MoveBotTo(const FVector& Destination, float AcceptanceRadius)
{
	// Pathfinding on, destination projected onto the navmesh, strafing allowed
	// (the bot aims itself; a move focus would fight UpdateAim), partial paths
	// accepted: half a route up the stairs beats standing at the bottom.
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Destination, AcceptanceRadius,
		/*bStopOnOverlap*/ true, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPaths*/ true);

	if (bLoggedFirstPath || BotState != EOneBrahamBotState::Patrol)
	{
		return;
	}

	bLoggedFirstPath = true;

	const UPathFollowingComponent* Following = GetPathFollowingComponent();
	const FNavPathSharedPtr Path = Following != nullptr ? Following->GetPath() : nullptr;
	if (Path.IsValid() && Path->IsValid())
	{
		const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
		const FVector End = Points.Num() > 0 ? FVector(Points.Last().Location) : FVector::ZeroVector;
		FString PointList;
		for (const FNavPathPoint& Point : Points)
		{
			PointList += FString::Printf(TEXT(" (%.0f,%.0f,%.0f)"), Point.Location.X, Point.Location.Y, Point.Location.Z);
		}
		UE_LOG(LogOneBrahamBots, Verbose, TEXT("[OB.BOTS] %s first path points:%s"), *BotLabel(this), *PointList);
		UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] %s first path: %d points, partial=%s, ends at %s (floor %d), goal %s (floor %d)"),
			*BotLabel(this), Points.Num(), Path->IsPartial() ? TEXT("yes") : TEXT("no"),
			*End.ToCompactString(), OneBrahamConstants::FloorIndexForZ(End.Z),
			*Destination.ToCompactString(), OneBrahamConstants::FloorIndexForZ(Destination.Z));
	}
	else
	{
		UE_LOG(LogOneBrahamBots, Warning, TEXT("[OB.BOTS] %s has no path to %s (move request result %d): is there navmesh here?"),
			*BotLabel(this), *Destination.ToCompactString(), static_cast<int32>(Result));
	}
}

bool AOneBrahamBotController::FindFreeStep(const FVector& From, const FVector& PreferredDirection, FVector& OutPoint) const
{
	const AOneBrahamCharacter* Character = GetBotCharacter();
	const UCapsuleComponent* Capsule = Character != nullptr ? Character->GetCapsuleComponent() : nullptr;
	const UWorld* World = GetWorld();
	if (Capsule == nullptr || World == nullptr)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBotFreeStep), false, Character);
	TArray<AActor*> Carried;
	Character->GetAttachedActors(Carried, true, true);
	Params.AddIgnoredActors(Carried);
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());

	// A slightly thinner capsule, lifted off the floor: the question is which
	// way is open, not whether the bot is touching anything right now.
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() - 2.f,
		Capsule->GetScaledCapsuleHalfHeight() - 10.f);
	const FVector Start = From + FVector(0.f, 0.f, 12.f);
	const FVector Forward = PreferredDirection.IsNearlyZero() ? FVector::ForwardVector : PreferredDirection;

	// Twelve directions, the preferred one first, then alternately either side.
	constexpr int32 Directions = 12;
	constexpr float StepCm = 160.f;
	for (int32 Index = 0; Index < Directions; ++Index)
	{
		const int32 Side = (Index % 2 == 0) ? 1 : -1;
		const float Angle = Side * ((Index + 1) / 2) * (360.f / Directions);
		const FVector Direction = Forward.RotateAngleAxis(Angle, FVector::UpVector);

		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Start, Start + Direction * StepCm, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), Shape, Params, Responses))
		{
			continue;
		}

		OutPoint = From + Direction * StepCm;
		return true;
	}

	return false;
}

FVector AOneBrahamBotController::RandomNavPointNear(const FVector& Origin, float RadiusCm) const
{
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Result;
		// Reachable from Origin, so the point is on the same connected walkable
		// area and not on the far side of a wall.
		if (Nav->GetRandomReachablePointInRadius(Origin, RadiusCm, Result))
		{
			return Result.Location;
		}
	}

	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	return Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * RadiusCm * FMath::FRandRange(0.4f, 1.f);
}

bool AOneBrahamBotController::RefreshObjective()
{
	const AOneBrahamGameMode* GM = GetOBGameMode();
	FVector Location = FVector::ZeroVector;
	int32 Floor = INDEX_NONE;

	const APawn* MyPawn = GetPawn();
	bHasObjective = GM != nullptr && MyPawn != nullptr
		&& GM->FindBotTravelTarget(PreferredSquadId, SquadOrdinal, MyPawn->GetActorLocation(), Location, Floor);
	if (bHasObjective)
	{
		ObjectiveLocation = Location;
		ObjectiveFloor = Floor;
	}
	return bHasObjective;
}

void AOneBrahamBotController::UpdateAim(float DeltaSeconds)
{
	AOneBrahamCharacter* Character = GetBotCharacter();
	if (Character == nullptr || BotState == EOneBrahamBotState::Downed)
	{
		return;
	}

	const FRotator Current = GetControlRotation();
	AActor* Target = CurrentTarget.Get();

	if (Target == nullptr)
	{
		// Nobody to shoot: look where the legs are going, level.
		const FVector Velocity = Character->GetVelocity();
		if (Velocity.SizeSquared2D() > FMath::Square(60.f))
		{
			const FRotator Desired(0.f, Velocity.Rotation().Yaw, 0.f);
			SetControlRotation(FMath::RInterpTo(Current, Desired, DeltaSeconds, 5.f));
		}
		return;
	}

	const FVector EyeLocation = Character->GetEyeLocation();
	const FVector ToTarget = Target->GetActorLocation() - EyeLocation;
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	// Aim error scales inversely with skill, and is applied as a slow wander so a
	// bot's fire walks onto the target instead of snapping to it.
	const float AimError = FMath::Lerp(6.5f, 1.4f, BotSkill);
	const float WanderPhase = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() * 2.1f : 0.f;
	const float YawError = FMath::Sin(WanderPhase) * AimError;
	const float PitchError = FMath::Cos(WanderPhase * 1.37f) * AimError * 0.6f;

	const FRotator DesiredRotation = ToTarget.Rotation() + FRotator(PitchError, YawError, 0.f);

	// Turn rate is limited: a bot that snaps 180 degrees in one frame reads as a
	// machine.
	SetControlRotation(FMath::RInterpTo(Current, DesiredRotation, DeltaSeconds, FMath::Lerp(4.f, 9.f, BotSkill)));
}

// ===========================================================================
//  Firing
// ===========================================================================

void AOneBrahamBotController::UpdateReload()
{
	UOneBrahamWeaponComponent* Weapons = GetWeapons();
	const AOneBrahamWeaponBase* Weapon = Weapons != nullptr ? Weapons->GetCurrentWeapon() : nullptr;
	if (Weapon == nullptr || BotState == EOneBrahamBotState::Downed)
	{
		return;
	}

	if (Weapons->IsReloading())
	{
		BotState = EOneBrahamBotState::Reload;
		return;
	}

	if (BotState == EOneBrahamBotState::Reload)
	{
		// Done: UpdateCombatState picks the fighting state again next tick.
		BotState = CurrentTarget.IsValid() ? EOneBrahamBotState::Engage : EOneBrahamBotState::Patrol;
	}

	const bool bEmpty = Weapon->IsMagazineEmpty();
	// Top up a half-empty magazine when nobody is around; reload an empty one
	// whenever there is reserve to do it with.
	const bool bLowAndSafe = !CurrentTarget.IsValid()
		&& Weapon->GetAmmoInMagazine() < Weapon->GetMagazineSize() / 2;

	if ((bEmpty || bLowAndSafe) && Weapons->GetReserveAmmoForCurrentWeapon() > 0)
	{
		Weapons->RequestReload();
		if (Weapons->IsReloading())
		{
			BotState = EOneBrahamBotState::Reload;
		}
	}
}

void AOneBrahamBotController::UpdateFiring(float DeltaSeconds)
{
	if (BurstCooldown > 0.f)
	{
		BurstCooldown = FMath::Max(0.f, BurstCooldown - DeltaSeconds);
	}

	UOneBrahamWeaponComponent* Weapons = GetWeapons();
	AOneBrahamCharacter* Character = GetBotCharacter();
	AActor* Target = CurrentTarget.Get();
	UWorld* World = GetWorld();

	if (Weapons == nullptr || Character == nullptr || Target == nullptr || World == nullptr
		|| !HasFiringSolution() || BurstCooldown > 0.f
		|| BotState == EOneBrahamBotState::Reload || BotState == EOneBrahamBotState::Downed
		|| Weapons->IsReloading())
	{
		TimeFiring = 0.f;
		return;
	}

	AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon();
	if (Weapon == nullptr || Weapon->Definition == nullptr
		|| Weapon->GetWeaponState() != EOneBrahamWeaponState::Ready || Weapon->IsMagazineEmpty())
	{
		TimeFiring = 0.f;
		return;
	}

	// Only pull the trigger when the weapon is actually pointed at the target:
	// the wander in UpdateAim means the bot walks its aim on, and firing while
	// pointing at a wall is how bots look stupid.
	const FVector Eye = Character->GetEyeLocation();
	const FVector ToTarget = (Target->GetActorLocation() - Eye).GetSafeNormal();
	const FVector AimDirection = GetControlRotation().Vector();
	const float AimTolerance = FMath::Lerp(0.975f, 0.9985f, BotSkill);
	if (FVector::DotProduct(AimDirection, ToTarget) < AimTolerance)
	{
		TimeFiring = 0.f;
		return;
	}

	// Burst discipline: a short burst, then a pause that gets shorter with skill.
	// Trigger discipline is most of what separates a good player from a bad
	// one, so it is most of what a bot has to fake. Point blank, it panics and
	// holds the trigger down, which is exactly what a person does.
	const float Distance = FVector::Dist(Character->GetActorLocation(), Target->GetActorLocation());
	TimeFiring += DeltaSeconds;
	const float BurstLength = FMath::Lerp(0.28f, 0.55f, BotSkill);
	if (TimeFiring >= BurstLength && Distance >= OBMinEngageRangeCm)
	{
		TimeFiring = 0.f;
		BurstCooldown = FMath::Lerp(0.85f, 0.32f, BotSkill) * FMath::FRandRange(0.85f, 1.2f);
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now < NextShotTime)
	{
		return;
	}

	// The shot goes through the weapon's own server path - the one a player's
	// validated request ends in - so ammo, spread, damage, falloff, friendly
	// fire, the FX multicast and the kill feed are exactly a player's. The
	// component's ServerRequestFire is the client RPC's validation step and
	// needs a player controller to report violations to; a bot has nothing to
	// validate.
	const EOneBrahamFireMode FireMode = Weapons->GetCurrentFireMode();
	const bool bHeldFire = FireMode == EOneBrahamFireMode::Auto || FireMode == EOneBrahamFireMode::Burst;
	const float Interval = Weapon->Definition->GetSecondsBetweenShots();
	NextShotTime = Now + (bHeldFire ? Interval : FMath::Max(Interval, OBMinTapIntervalSeconds));

	// Firing ends spawn protection, for a bot as for anyone (Docs/06 spawn safety).
	if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		Health->ClearInvulnerability();
	}

	const int32 AmmoBefore = Weapon->GetAmmoInMagazine();
	TArray<FOneBrahamImpactInfo> Impacts;
	AActor* HitActor = nullptr;
	const float Damage = Weapon->ServerFireShot(Eye, AimDirection, Impacts, HitActor);

	if (Weapon->GetAmmoInMagazine() < AmmoBefore)
	{
		++ShotsFired;
		DamageDealt += Damage;
		if (Damage > 0.f)
		{
			++ShotsHit;
		}

		// Gunfire is heard: other bots' hearing turns them toward the fight.
		UAISense_Hearing::ReportNoiseEvent(World, Eye, 1.f, Character, 0.f, NAME_None);
	}

	if (Weapon->IsMagazineEmpty())
	{
		TimeFiring = 0.f;
		Weapons->RequestReload();
	}
}

// ===========================================================================
//  Status
// ===========================================================================

void AOneBrahamBotController::UpdateStatusLog(float DeltaSeconds)
{
	const float Interval = CVarBotStatusInterval.GetValueOnGameThread();
	if (Interval <= 0.f)
	{
		return;
	}

	StatusAccumulator += DeltaSeconds;
	if (StatusAccumulator < Interval)
	{
		return;
	}
	StatusAccumulator = 0.f;

	UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] status %s"), *GetBotDebugString());
}

// ===========================================================================
//  Helpers
// ===========================================================================

bool AOneBrahamBotController::HasFiringSolution() const
{
	AActor* Target = CurrentTarget.Get();
	if (Target == nullptr || ReactionRemaining > 0.f)
	{
		return false;
	}

	const UOneBrahamWeaponComponent* Weapons = GetWeapons();
	if (Weapons == nullptr || Weapons->GetCurrentWeapon() == nullptr)
	{
		return false;
	}

	return HasLineOfSight(Target);
}

bool AOneBrahamBotController::HasLineOfSight(const AActor* Other) const
{
	const AOneBrahamCharacter* Character = GetBotCharacter();
	const UWorld* World = GetWorld();
	if (Character == nullptr || Other == nullptr || World == nullptr)
	{
		return false;
	}

	const FVector Start = Character->GetEyeLocation();
	const FVector End = Other->GetActorLocation() + FVector(0.f, 0.f, 40.f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBotLOS), /*bTraceComplex*/ false, Character);
	Params.AddIgnoredActor(Other);

	// Weapons and anything attached to either pawn must not block the line.
	TArray<AActor*> Attached;
	Character->GetAttachedActors(Attached);
	Other->GetAttachedActors(Attached, /*bResetArray*/ false);
	Params.AddIgnoredActors(Attached);

	// Visibility channel: opaque geometry only. Glass blocks it, which is correct
	// for "can I shoot this person" (see the trace channel table in Docs/18).
	return !World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

bool AOneBrahamBotController::IsValidEnemy(const AActor* Other) const
{
	const AOneBrahamCharacter* OtherCharacter = Cast<AOneBrahamCharacter>(Other);
	if (OtherCharacter == nullptr || OtherCharacter == GetPawn() || IsFriendly(OtherCharacter))
	{
		return false;
	}

	// A pawn with no player state is a corpse that has been let go of, or a
	// prop character; neither is anyone's opponent.
	if (OtherCharacter->GetPlayerState() == nullptr)
	{
		return false;
	}

	// Dead, or inside spawn grace: rounds into an invulnerable player are a
	// magazine wasted, and the grace exists so a squad can get off the plaza.
	const UOneBrahamHealthComponent* Health = OtherCharacter->GetHealthComponent();
	if (Health != nullptr && (!Health->IsAlive() || Health->IsInvulnerable()))
	{
		return false;
	}

	// Downed players are left to bleed out: finishing them is an execution, a
	// decision bots do not make (Docs/06 s5).
	const UOneBrahamDownedComponent* Downed = OtherCharacter->GetDownedComponent();
	return Downed == nullptr || !Downed->IsDowned();
}

bool AOneBrahamBotController::IsFriendly(const AActor* Other) const
{
	if (Other == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* MyState = GetPlayerState<AOneBrahamPlayerState>();
	if (MyState == nullptr)
	{
		return false;
	}

	if (const AOneBrahamPlayerState* OtherState = Cast<AOneBrahamPlayerState>(Other))
	{
		return MyState->IsFriendlyWith(OtherState);
	}

	if (const APawn* OtherPawn = Cast<APawn>(Other))
	{
		if (const AOneBrahamPlayerState* PawnState = OtherPawn->GetPlayerState<AOneBrahamPlayerState>())
		{
			return MyState->IsFriendlyWith(PawnState);
		}
	}

	return false;
}

AOneBrahamCharacter* AOneBrahamBotController::GetBotCharacter() const
{
	return Cast<AOneBrahamCharacter>(GetPawn());
}

UOneBrahamWeaponComponent* AOneBrahamBotController::GetWeapons() const
{
	AOneBrahamCharacter* Character = GetBotCharacter();
	return Character != nullptr ? Character->GetWeaponComponent() : nullptr;
}

AOneBrahamGameMode* AOneBrahamBotController::GetOBGameMode() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetAuthGameMode<AOneBrahamGameMode>() : nullptr;
}

void AOneBrahamBotController::SetBotName(const FText& NewName)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AOneBrahamPlayerState* PS = GetPlayerState<AOneBrahamPlayerState>())
	{
		// PlayerState names are replicated strings; the scoreboard and kill feed
		// read this, so a bot reads as "BOT Hughes" and not "PlayerState_1".
		PS->SetPlayerName(NewName.ToString());
	}
}

FString AOneBrahamBotController::GetBotDebugString() const
{
	const AOneBrahamCharacter* Character = GetBotCharacter();
	const AActor* Target = CurrentTarget.Get();
	const UOneBrahamWeaponComponent* Weapons = GetWeapons();
	const AOneBrahamWeaponBase* Weapon = Weapons != nullptr ? Weapons->GetCurrentWeapon() : nullptr;

	return FString::Printf(
		TEXT("%s state=%s squad=%d team=%s floor=%d loc=%s travelled=%.0fm target=%s los=%s ammo=%d shots=%d hits=%d dmg=%.0f stalls=%d"),
		*BotLabel(this),
		*UEnum::GetValueAsString(BotState),
		PreferredSquadId,
		*UEnum::GetValueAsString(PreferredTeam),
		CurrentFloor,
		Character != nullptr ? *Character->GetActorLocation().ToCompactString() : TEXT("none"),
		DistanceTravelledCm / 100.f,
		Target != nullptr ? *BotLabel(Cast<APawn>(Target) ? Cast<APawn>(Target)->GetController() : nullptr) : TEXT("none"),
		HasFiringSolution() ? TEXT("yes") : TEXT("no"),
		Weapon != nullptr ? Weapon->GetAmmoInMagazine() : -1,
		ShotsFired, ShotsHit, DamageDealt, StallCount);
}
