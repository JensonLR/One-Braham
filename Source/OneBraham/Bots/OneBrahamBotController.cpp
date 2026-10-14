// Copyright (c) One Braham. All Rights Reserved.

#include "Bots/OneBrahamBotController.h"

#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "OneBraham.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Weapons/OneBrahamWeaponComponent.h"

namespace
{
	/** Floor height in cm; the map is fifteen of these with the lobby at Z = 0. */
	/** One number, defined once: see OneBrahamConstants in OneBraham.h. */
	constexpr float OBFloorHeightCm = OneBrahamConstants::FloorHeightCm;

	/** How long a bot remembers a target it can no longer see. */
	constexpr float OBTargetMemorySeconds = 6.f;

	/** Decision cadence for movement: 4 Hz is plenty and keeps the tick cheap. */
	constexpr float OBMovementDecisionInterval = 0.25f;

	/** Radius to wander around the home position when patrolling, cm. */
	constexpr float OBPatrolRadiusCm = 1600.f;

	/** Engagement ranges, cm. Bots do not fight across the whole floor. */
	constexpr float OBMinEngageRangeCm = 250.f;
	constexpr float OBIdealEngageRangeCm = 1100.f;
	constexpr float OBMaxEngageRangeCm = 4200.f;
}

AOneBrahamBotController::AOneBrahamBotController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	// Bots get a PlayerState so teams, damage attribution and (on a listen server)
	// the scoreboard behave exactly like they do for a human player.
	bWantsPlayerState = true;

	ConfigurePerception();
}

void AOneBrahamBotController::ConfigurePerception()
{
	OBPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("OBPerception"));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 5200.f;
	SightConfig->LoseSightRadius = 6200.f;
	SightConfig->PeripheralVisionAngleDegrees = 76.f;
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

	// Only the authority spawns pawns: a client-side bot is a server-authority
	// problem, and bots are a listen-server feature (see the header note).
	if (HasAuthority())
	{
		SpawnBotPawn();
	}
}

void AOneBrahamBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn != nullptr)
	{
		HomeLocation = InPawn->GetActorLocation();
	}

	BotState = EOneBrahamBotState::Patrol;
	PatrolPickAccumulator = OBMovementDecisionInterval;

	// Bot pawns never block the player's own spawn logic and never count as
	// "occupied floor" for the lockdown director beyond their actual presence.
	if (AOneBrahamCharacter* Character = GetBotCharacter())
	{
		Character->SetBotControlled(true);
	}

	UE_LOG(LogOneBrahamBots, Log, TEXT("[OB.BOTS] %s possessed by %s (team %s, skill %.2f)"),
		*InPawn->GetName(), *GetName(),
		*UEnum::GetValueAsString(PreferredTeam), BotSkill);
}

void AOneBrahamBotController::OnUnPossess()
{
	if (AOneBrahamCharacter* Character = GetBotCharacter())
	{
		Character->SetBotControlled(false);
	}

	BotState = EOneBrahamBotState::Idle;
	Super::OnUnPossess();
}

// ===========================================================================
//  Spawning
// ===========================================================================

void AOneBrahamBotController::SpawnBotPawn()
{
	if (bPawnSpawnAttempted && GetPawn() != nullptr)
	{
		return;
	}

	bPawnSpawnAttempted = true;

	AOneBrahamGameMode* GM = GetOBGameMode();
	if (GM == nullptr)
	{
		OB_WARN("Bot '%s' has no One Braham game mode; it will stand still.", *GetName());
		return;
	}

	// The game mode's spawn selection handles squad spread and enemy distance, so
	// practice bots appear in sane places instead of inside each other.
	GM->RestartPlayer(this);

	// Team assignment: the pawn exists now, so the PlayerState can be tagged. The
	// game mode assigns squads for human players during login; a bot is never
	// logged in, so it claims its slot here.
	if (AOneBrahamPlayerState* PS = GetPlayerState<AOneBrahamPlayerState>())
	{
		PS->Team = PreferredTeam;
		PS->SquadId = PreferredSquadId;
		PS->bIsSquadLeader = false;
		// The native tag constant, not a string: this line used to request
		// "OB.Character.Archetype.Intern", which has never existed - the archetype
		// is called Graduate in Docs/13 and in the character registry. Requested
		// with ErrorIfNotFound = false, a wrong name produces no error at all, just
		// an invalid tag on every bot's PlayerState, and any code keying on the
		// archetype silently takes its fallback branch. The constant cannot go
		// wrong: renaming the tag now breaks the build.
		PS->ArchetypeTag = OneBrahamTags::Character_Archetype_Graduate;
	}

	if (GetPawn() == nullptr)
	{
		OB_WARN("Bot '%s' failed to spawn a pawn (no spawn point on the target floor?).", *GetName());
	}
}

// ===========================================================================
//  Perception
// ===========================================================================

void AOneBrahamBotController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (Actor == nullptr || !HasAuthority())
	{
		return;
	}

	const bool bFriendly = IsFriendly(Actor);
	if (bFriendly)
	{
		// Hearing a colleague is a navigation hint, not a contact: bots drift
		// toward teammate gunfire, which is what a real squad does.
		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && Stimulus.WasSuccessfullySensed())
		{
			PatrolDestination = Actor->GetActorLocation();
		}
		return;
	}

	if (!Stimulus.WasSuccessfullySensed())
	{
		// Lost. Keep it as the target and let the memory timer decide.
		if (CurrentTarget.Get() == Actor)
		{
			TimeSinceLostSight = 0.f;
		}
		return;
	}

	// New contact: reaction time is the single most important skill knob, and it
	// is what stops bots from feeling like aimbots.
	const bool bNoTarget = !CurrentTarget.IsValid();
	if (bNoTarget || FMath::FRand() < 0.35f)
	{
		CurrentTarget = Actor;
		TimeSinceLostSight = 0.f;

		if (bNoTarget)
		{
			ReactionRemaining = FMath::Lerp(0.85f, 0.22f, BotSkill) * FMath::FRandRange(0.85f, 1.3f);
		}
	}
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
		// Dead, downed or not spawned yet: try again periodically (respawn).
		StateAccumulator += DeltaSeconds;
		if (StateAccumulator >= 1.f)
		{
			StateAccumulator = 0.f;
			SpawnBotPawn();
		}
		return;
	}

	HomeLocation = HomeLocation.IsNearlyZero() ? Character->GetActorLocation() : HomeLocation;
	CurrentFloor = FMath::FloorToInt(Character->GetActorLocation().Z / OBFloorHeightCm);

	UpdateCombatState(DeltaSeconds);
	UpdateMovement(DeltaSeconds);
	FaceTarget(DeltaSeconds);
	UpdateReload();
	UpdateFiring(DeltaSeconds);
}

void AOneBrahamBotController::UpdateCombatState(float DeltaSeconds)
{
	AOneBrahamCharacter* Character = GetBotCharacter();
	const UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;

	if (Health != nullptr && !Health->IsAlive())
	{
		BotState = EOneBrahamBotState::Downed;
		StopMovement();
		if (UOneBrahamWeaponComponent* Weapons = GetWeapons())
		{
			Weapons->SetTriggerHeld(false);
		}
		return;
	}

	AActor* Target = CurrentTarget.Get();

	// Target validity: dead, gone, or forgotten after breaking line of sight.
	if (Target != nullptr)
	{
		const AOneBrahamCharacter* TargetCharacter = Cast<AOneBrahamCharacter>(Target);
		const UOneBrahamHealthComponent* TargetHealth = TargetCharacter != nullptr ? TargetCharacter->GetHealthComponent() : nullptr;

		if ((TargetHealth != nullptr && !TargetHealth->IsAlive()) || IsFriendly(Target))
		{
			CurrentTarget = nullptr;
			Target = nullptr;
		}
	}

	if (Target != nullptr)
	{
		if (HasLineOfSight(Target))
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
		BotState = EOneBrahamBotState::Patrol;
		return;
	}

	if (ReactionRemaining > 0.f)
	{
		ReactionRemaining = FMath::Max(0.f, ReactionRemaining - DeltaSeconds);
		BotState = EOneBrahamBotState::Advance;
		return;
	}

	const float Distance = FVector::Dist(Character->GetActorLocation(), Target->GetActorLocation());
	const bool bVisible = HasLineOfSight(Target);

	// Low-skill bots retreat when hurt; high-skill bots hold and trade.
	if (Health != nullptr && Health->GetHealthFraction() < 0.35f && FMath::FRand() < (1.f - BotSkill))
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
	else if (Distance > OBIdealEngageRangeCm)
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
	PatrolPickAccumulator += DeltaSeconds;

	if (MovementAccumulator < OBMovementDecisionInterval)
	{
		return;
	}

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
	{
		// A new destination every few seconds, always near where the bot started:
		// bots hold their floor rather than wandering the whole tower, which they
		// cannot path across anyway (no lift logic).
		if (PatrolPickAccumulator >= 3.5f || PatrolDestination.IsNearlyZero() ||
			FVector::Dist2D(Character->GetActorLocation(), PatrolDestination) < 120.f)
		{
			PickPatrolDestination();
		}

		MoveToLocation(PatrolDestination, /*AcceptanceRadius*/ 90.f, /*bStopOnOverlap*/ true);
		return;
	}

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

		FVector Destination = BotLocation;

		if (BotState == EOneBrahamBotState::Advance)
		{
			// Advance to engagement range, not into the target's face.
			const float Distance = FVector::Dist2D(BotLocation, TargetLocation);
			const float Step = FMath::Min(Distance - OBIdealEngageRangeCm, 900.f);
			Destination = BotLocation + ToTarget * FMath::Max(Step, 150.f);
		}
		else if (BotState == EOneBrahamBotState::Retreat)
		{
			// Back off, with a lateral component so retreating is not a straight
			// line into the next burst.
			const FVector Lateral = FVector::CrossProduct(FVector::UpVector, ToTarget);
			Destination = BotLocation - ToTarget * 700.f + Lateral * FMath::FRandRange(-450.f, 450.f);
		}
		else
		{
			// Engage: strafe a little, stay at range. Bots do not strafe-shoot well,
			// which is intentional.
			const FVector Lateral = FVector::CrossProduct(FVector::UpVector, ToTarget);
			Destination = BotLocation + Lateral * FMath::FRandRange(-320.f, 320.f);
		}

		MoveToLocation(Destination, /*AcceptanceRadius*/ 70.f, /*bStopOnOverlap*/ true);
		return;
	}

	default:
		return;
	}
}

void AOneBrahamBotController::PickPatrolDestination()
{
	PatrolPickAccumulator = 0.f;

	const FVector Origin = !HomeLocation.IsNearlyZero() ? HomeLocation : FVector::ZeroVector;
	if (Origin.IsNearlyZero())
	{
		return;
	}

	// A short walk from home, biased toward the objective: bots drift toward the
	// highest floor they can reach rather than orbiting their spawn point.
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Radius = FMath::FRandRange(OBPatrolRadiusCm * 0.4f, OBPatrolRadiusCm);
	PatrolDestination = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
}

void AOneBrahamBotController::FaceTarget(float DeltaSeconds)
{
	AActor* Target = CurrentTarget.Get();
	AOneBrahamCharacter* Character = GetBotCharacter();
	if (Target == nullptr || Character == nullptr)
	{
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
	// machine. 6 rad/s is roughly a human whipping round.
	const FRotator Current = GetControlRotation();
	const FRotator Smoothed = FMath::RInterpTo(Current, DesiredRotation, DeltaSeconds, FMath::Lerp(4.f, 9.f, BotSkill));
	SetControlRotation(Smoothed);
}

// ===========================================================================
//  Firing
// ===========================================================================

void AOneBrahamBotController::UpdateReload()
{
	UOneBrahamWeaponComponent* Weapons = GetWeapons();
	if (Weapons == nullptr)
	{
		return;
	}

	const AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon();
	if (Weapon == nullptr)
	{
		return;
	}

	if (Weapon->IsMagazineEmpty() && !Weapons->IsReloading())
	{
		// Bots reload when they are not being shot at, or when they have to.
		const bool bEmergency = Weapons->GetReserveAmmoForCurrentWeapon() > 0 && GetCurrentTarget() != nullptr;
		const bool bSafe = !HasFiringSolution();

		if (bSafe || bEmergency)
		{
			BotState = EOneBrahamBotState::Reload;
			Weapons->RequestReload();
		}
	}
	else if (BotState == EOneBrahamBotState::Reload && !Weapons->IsReloading())
	{
		BotState = EOneBrahamBotState::Engage;
	}
}

void AOneBrahamBotController::UpdateFiring(float DeltaSeconds)
{
	UOneBrahamWeaponComponent* Weapons = GetWeapons();
	if (Weapons == nullptr)
	{
		return;
	}

	if (BurstCooldown > 0.f)
	{
		BurstCooldown = FMath::Max(0.f, BurstCooldown - DeltaSeconds);
	}

	if (!HasFiringSolution() || BurstCooldown > 0.f || BotState == EOneBrahamBotState::Reload)
	{
		Weapons->SetTriggerHeld(false);
		TimeFiring = 0.f;
		return;
	}

	AActor* Target = CurrentTarget.Get();
	const AOneBrahamCharacter* Character = GetBotCharacter();
	if (Target == nullptr || Character == nullptr)
	{
		Weapons->SetTriggerHeld(false);
		return;
	}

	// Only pull the trigger when the weapon is actually pointed at the target:
	// the wander in FaceTarget() means the bot walks its aim on, and firing while
	// pointing at a wall is how bots look stupid.
	const FVector ToTarget = (Target->GetActorLocation() - Character->GetEyeLocation()).GetSafeNormal();
	const float AimDot = FVector::DotProduct(GetControlRotation().Vector(), ToTarget);
	const float AimTolerance = FMath::Lerp(0.975f, 0.9985f, BotSkill);

	if (AimDot < AimTolerance)
	{
		Weapons->SetTriggerHeld(false);
		return;
	}

	const AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon();
	if (Weapon == nullptr || Weapon->WeaponState != EOneBrahamWeaponState::Ready)
	{
		Weapons->SetTriggerHeld(false);
		return;
	}

	// Burst discipline: three to six rounds, then a pause that gets shorter with
	// skill. Trigger discipline is most of what separates a good player from a
	// bad one, so it is most of what a bot has to fake.
	TimeFiring += DeltaSeconds;

	const float BurstLength = FMath::Lerp(0.28f, 0.55f, BotSkill);
	const float BurstPause = FMath::Lerp(0.85f, 0.32f, BotSkill) * FMath::FRandRange(0.85f, 1.2f);

	if (TimeFiring >= BurstLength)
	{
		TimeFiring = 0.f;
		BurstCooldown = BurstPause;
		Weapons->SetTriggerHeld(false);
		return;
	}

	// Musket-lock weapons (pump, bolt, single) need a re-press per shot, which is
	// what a human does; auto and burst weapons hold the trigger down.
	const bool bHeldFire = Weapons->GetCurrentFireMode() == EOneBrahamFireMode::Auto
		|| Weapons->GetCurrentFireMode() == EOneBrahamFireMode::Burst;

	if (bHeldFire)
	{
		Weapons->SetTriggerHeld(true);
	}
	else if (!Weapons->IsTriggerHeld())
	{
		Weapons->SetTriggerHeld(true);
	}

	// Fire discipline also means panicking at close range: a bot with a target in
	// its face empties the magazine, which is exactly what a person does.
	if (const AActor* TargetActor = CurrentTarget.Get())
	{
		if (FVector::Dist(Character->GetActorLocation(), TargetActor->GetActorLocation()) < OBMinEngageRangeCm)
		{
			BurstCooldown = 0.f;
		}
	}
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

	const AOneBrahamCharacter* Character = GetBotCharacter();
	if (Character == nullptr)
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

	// Visibility channel: opaque geometry only. Glass blocks it, which is correct
	// for "can I shoot this person" (see the trace channel table in Docs/18).
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	return !bBlocked;
}

bool AOneBrahamBotController::IsFriendly(const AActor* Other) const
{
	if (Other == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* MyState = GetPlayerState<AOneBrahamPlayerState>();
	const AOneBrahamPlayerState* OtherState = Cast<AOneBrahamPlayerState>(Other);

	if (MyState != nullptr && OtherState != nullptr)
	{
		return MyState->IsFriendlyWith(OtherState);
	}

	// Fall back to the pawn's player state when the actor itself is the pawn.
	if (const APawn* OtherPawn = Cast<APawn>(Other))
	{
		if (MyState != nullptr)
		{
			if (const AOneBrahamPlayerState* PawnState = OtherPawn->GetPlayerState<AOneBrahamPlayerState>())
			{
				return MyState->IsFriendlyWith(PawnState);
			}
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
		// read this, so a bot reads as "PRACTICE BOT 3" and not "PlayerState_1".
		PS->SetPlayerName(NewName.ToString());
	}
}

FString AOneBrahamBotController::GetBotDebugString() const
{
	const AOneBrahamCharacter* Character = GetBotCharacter();
	const AActor* Target = CurrentTarget.Get();

	return FString::Printf(
		TEXT("[%s] state=%s skill=%.2f team=%s floor=%d location=%s target=%s los=%s reaction=%.2f"),
		*GetName(),
		*UEnum::GetValueAsString(BotState),
		BotSkill,
		*UEnum::GetValueAsString(PreferredTeam),
		CurrentFloor,
		Character != nullptr ? *Character->GetActorLocation().ToCompactString() : TEXT("none"),
		Target != nullptr ? *Target->GetName() : TEXT("none"),
		HasFiringSolution() ? TEXT("yes") : TEXT("no"),
		ReactionRemaining);
}
