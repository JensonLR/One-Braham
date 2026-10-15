// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamDeathRecap.h"
#include "GameFramework/PlayerState.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UOneBrahamHealthComponent::UOneBrahamHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f; // regen does not need 60 Hz
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = false;
}

void UOneBrahamHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Health, armour and the last-damage timestamp are the only things that need
	// to reach clients. Damage attribution stays server-side for the audit trail.
	DOREPLIFETIME(UOneBrahamHealthComponent, Health);
	DOREPLIFETIME(UOneBrahamHealthComponent, Armour);
	DOREPLIFETIME(UOneBrahamHealthComponent, LastDamageTime);
}

void UOneBrahamHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Every life starts at 100 + 50 (Docs/04): the TTK table in Docs/05 is
	// written against a target wearing full armour.
	Health = MaxHealth;
	Armour = MaxArmour;
	bDead = false;
}

void UOneBrahamHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// --- Health regeneration ---------------------------------------------
	// Regeneration only restores up to the ceiling: the building does not heal
	// you, it just stops the bleeding. Never while downed: a body on the carpet
	// sits at 1 HP until it is revived, executed or bleeds out.
	const AOneBrahamCharacter* OwnerCharacter = Cast<AOneBrahamCharacter>(GetOwner());
	const bool bOwnerDowned = OwnerCharacter != nullptr && OwnerCharacter->GetDownedComponent() != nullptr &&
		OwnerCharacter->GetDownedComponent()->IsDowned();

	if (!bOwnerDowned && HealthRegenPerSecond > 0.f && (Now - LastDamageTime) >= HealthRegenDelay && Health < FMath::Min(MaxHealth, HealthRegenCeiling))
	{
		const float Before = Health;
		Health = FMath::Min(Health + HealthRegenPerSecond * (HealthRegenCeiling >= MaxHealth ? DeltaTime : DeltaTime * 1.5f), FMath::Min(HealthRegenCeiling, MaxHealth));

		if (!FMath::IsNearlyEqual(Before, Health))
		{
			OnHealthChanged.Broadcast(Health, MaxHealth, Health - Before);
		}
	}

	// --- Attacker bookkeeping --------------------------------------------
	if (Now - LastAttackerPruneTime > 1.f)
	{
		LastAttackerPruneTime = Now;
		for (auto It = RecentAttackers.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || (Now - LastDamageTime) > 5.f)
			{
				It.RemoveCurrent();
			}
		}
	}

	// Expire invulnerability.
	if (InvulnerableUntil > 0.f && Now >= InvulnerableUntil)
	{
		ClearInvulnerability();
	}
}

bool UOneBrahamHealthComponent::IsInvulnerable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && InvulnerableUntil > 0.f && World->GetTimeSeconds() < InvulnerableUntil;
}

void UOneBrahamHealthComponent::InitializeHealth(float NewMaxHealth, bool bRefill)
{
	MaxHealth = FMath::Max(1.f, NewMaxHealth);
	if (bRefill)
	{
		Health = MaxHealth;
	}
	else
	{
		Health = FMath::Min(Health, MaxHealth);
	}

	bDead = false;
	OnHealthChanged.Broadcast(Health, MaxHealth, 0.f);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UOneBrahamHealthComponent::ResetHealth()
{
	Health = MaxHealth;
	Armour = MaxArmour;
	bDead = false;
	LastDamageTime = -1000.f;
	RecentAttackers.Reset();
	RecentHits.Reset();

	OnHealthChanged.Broadcast(Health, MaxHealth, 0.f);
	OnArmourChanged.Broadcast(Armour, MaxArmour);
}

void UOneBrahamHealthComponent::ReviveToHealth(float NewHealth)
{
	AActor* Owner = GetOwner();
	if (Owner != nullptr && !Owner->HasAuthority())
	{
		return;
	}

	const float Before = Health;
	Health = FMath::Clamp(NewHealth, 1.f, MaxHealth);
	bDead = false;

	// Whoever put us down does not keep assist credit into the next fight.
	LastDamageInstigator.Reset();
	RecentAttackers.Reset();
	RecentHits.Reset();

	const APawn* OwnerPawn = Cast<APawn>(Owner);
	if (AOneBrahamPlayerState* OwnerState = OwnerPawn ? OwnerPawn->GetPlayerState<AOneBrahamPlayerState>() : nullptr)
	{
		OwnerState->Health = Health;
	}

	OnHealthChanged.Broadcast(Health, MaxHealth, Health - Before);

	if (Owner != nullptr)
	{
		Owner->ForceNetUpdate();
	}
}

float UOneBrahamHealthComponent::GetZoneMultiplier(EOneBrahamHitZone Zone) const
{
	switch (Zone)
	{
	case EOneBrahamHitZone::Head:		return HeadshotMultiplier;
	case EOneBrahamHitZone::Neck:		return NeckMultiplier;
	case EOneBrahamHitZone::Torso:		return TorsoMultiplier;
	case EOneBrahamHitZone::UpperArm:
	case EOneBrahamHitZone::LowerArm:
	case EOneBrahamHitZone::Hand:
	case EOneBrahamHitZone::UpperLeg:
	case EOneBrahamHitZone::LowerLeg:
	case EOneBrahamHitZone::Foot:		return LimbMultiplier;
	default:							return TorsoMultiplier;
	}
}

float UOneBrahamHealthComponent::ApplyDamage(float RawDamage, AActor* DamageInstigator, const FOneBrahamDamageReport& IncomingReport, AController* InstigatorController)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		// A client attempting to call ApplyDamage is either a bug or a cheat.
		if (Owner != nullptr && DamageInstigator != nullptr)
		{
			OB_AUDIT_LOG("Client-side ApplyDamage attempt on '%s' from '%s' - ignored.",
				*Owner->GetName(), *DamageInstigator->GetName());
		}
		return 0.f;
	}

	if (bDead || RawDamage <= 0.f)
	{
		return 0.f;
	}

	// --- Rejection paths --------------------------------------------------
	if (IsInvulnerable())
	{
		return 0.f;
	}

	// Downed players take reduced damage from gunfire but can be executed; the
	// downed component handles the execution path, so here we simply scale.
	const AOneBrahamCharacter* OwnerCharacter = Cast<AOneBrahamCharacter>(Owner);
	const bool bOwnerDowned = OwnerCharacter != nullptr && OwnerCharacter->GetDownedComponent() != nullptr &&
		OwnerCharacter->GetDownedComponent()->IsDowned();

	// --- Friendly fire policy --------------------------------------------
	float FriendlyFireScale = 1.f;
	if (DamageInstigator != nullptr && DamageInstigator != Owner)
	{
		const APawn* InstigatorPawn = Cast<APawn>(DamageInstigator);
		const AOneBrahamPlayerState* InstigatorState = InstigatorPawn ? InstigatorPawn->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
		const AOneBrahamPlayerState* OwnerState = OwnerCharacter ? OwnerCharacter->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

		if (InstigatorState != nullptr && OwnerState != nullptr &&
			OneBrahamTags::AreTeamsFriendly(InstigatorState->Team, OwnerState->Team, InstigatorState->SessionMode))
		{
			// ONE BRAHAM ships with friendly fire off in ASCENT (a 12-player mode
			// where teamwiping your own squad is never interesting) and reduced in
			// FIRE DRILL (so grenades still matter, but not as a griefing tool).
			const EOneBrahamMatchMode Mode = InstigatorState->SessionMode;
			FriendlyFireScale = (Mode == EOneBrahamMatchMode::FireDrill) ? 0.35f : 0.f;

			if (FriendlyFireScale <= 0.f)
			{
				// Track it anyway: a "team damage prevented" stat is genuinely
				// useful for QA and for the end-of-match screen.
				OB_WEAPON_LOG("Friendly fire blocked: '%s' -> '%s' (%.1f damage).",
					*DamageInstigator->GetName(), *Owner->GetName(), RawDamage);
				return 0.f;
			}
		}
	}

	// --- The actual damage maths -----------------------------------------
	FOneBrahamDamageReport Report = IncomingReport;
	Report.BaseDamage = RawDamage;
	Report.HitLocation = IncomingReport.HitLocation;
	Report.ShotDirection = IncomingReport.ShotDirection;

	float Damage = RawDamage;
	Damage *= GetZoneMultiplier(Report.HitZone);
	Damage *= FriendlyFireScale;
	Damage *= GlobalDamageScale;

	// Penetration: a shot through a cubicle wall loses 35% of its energy.
	if (Report.bWasPenetrated)
	{
		Damage *= 0.65f;
	}

	// Downed players are tougher to finish with bullets, but executions bypass it.
	if (bOwnerDowned)
	{
		Damage *= 0.6f;
	}

	// --- Armour -----------------------------------------------------------
	float ArmourTaken = 0.f;
	if (Armour > 0.f)
	{
		const float Absorbed = FMath::Min(Armour, Damage * ArmourAbsorption);
		ArmourTaken = Absorbed;
		Armour -= Absorbed;
		Damage -= Absorbed;
		OnArmourChanged.Broadcast(Armour, MaxArmour);
	}

	Damage = FMath::Max(0.f, Damage);
	Report.FinalDamage = Damage;

	const float Before = Health;
	Health = FMath::Max(0.f, Health - Damage);
	const float ActualDamage = Before - Health;

	UWorld* World = GetWorld();
	LastDamageTime = World ? World->GetTimeSeconds() : 0.f;
	LastDamageInstigator = DamageInstigator;
	LastDamageReport = Report;

	// --- Damage feedback ---------------------------------------------------
	// The delegate is for the systems that live on the authority: score, assists,
	// Blueprint hooks, the question of whether this should have been a head shot.
	// It is *not* how the local player finds out. This function returns early on
	// every client, so a broadcast from here reaches a remote player's HUD never,
	// and the indicator drew on the host's screen and nowhere else. The client
	// RPC below is what reaches the machine with someone sitting at it.
	if (ActualDamage > 0.f)
	{
		FVector InstigatorLocation = FVector::ZeroVector;

		if (DamageInstigator != nullptr)
		{
			InstigatorLocation = DamageInstigator->GetActorLocation();
		}
		else if (InstigatorController != nullptr && InstigatorController->GetPawn() != nullptr)
		{
			InstigatorLocation = InstigatorController->GetPawn()->GetActorLocation();
		}

		OnDamageTaken.Broadcast(ActualDamage, DamageInstigator, InstigatorLocation);

		if (AOneBrahamPlayerController* OwnerPC = OwnerCharacter != nullptr
			? Cast<AOneBrahamPlayerController>(OwnerCharacter->GetController()) : nullptr)
		{
			OwnerPC->ClientDamageTaken(ActualDamage, InstigatorLocation);
		}
	}

	// --- The death recap's record of the fight ---------------------------
	if (ActualDamage > 0.f || ArmourTaken > 0.f)
	{
		FOneBrahamHitRecord Hit;
		if (const APlayerState* AttackerState = InstigatorController != nullptr ? InstigatorController->PlayerState : nullptr)
		{
			Hit.AttackerName = AttackerState->GetPlayerName();
		}
		Hit.HealthDamage = ActualDamage;
		Hit.ArmourDamage = ArmourTaken;
		Hit.Zone = Report.HitZone;
		Hit.DistanceCm = Report.Distance;
		Hit.Time = World != nullptr ? World->GetTimeSeconds() : 0.0;
		OneBrahamDeathRecap::Record(RecentHits, Hit);
	}

	// --- Attacker tracking (for assists) ---------------------------------
	if (DamageInstigator != nullptr && DamageInstigator != Owner)
	{
		float& Accumulated = RecentAttackers.FindOrAdd(DamageInstigator);
		Accumulated += ActualDamage;
	}

	// --- Registrations ----------------------------------------------------
	if (AOneBrahamPlayerState* InstigatorState = InstigatorController ? InstigatorController->GetPlayerState<AOneBrahamPlayerState>() : nullptr)
	{
		// The medkit-check: a player who takes health from an enemy while their
		// projectile is in flight should not be credited for a self-inflicted hit.
		if (DamageInstigator != Owner)
		{
			InstigatorState->ServerRegisterDamage(ActualDamage);
		}
	}

	if (AOneBrahamPlayerState* OwnerState = OwnerCharacter ? OwnerCharacter->GetPlayerState<AOneBrahamPlayerState>() : nullptr)
	{
		OwnerState->Health = Health;
	}

	OnHealthChanged.Broadcast(Health, MaxHealth, -ActualDamage);

	Owner->ForceNetUpdate();

	// --- Death ------------------------------------------------------------
	if (Health <= 0.f && !bDead)
	{
		bDead = true;
		Report.bLethal = true;
		LastDamageReport = Report;

		// In ASCENT, a lethal hit does not necessarily mean dead: the downed
		// component takes over if the mode and the circumstances allow it.
		if (AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Owner))
		{
			UOneBrahamDownedComponent* Downed = Character->GetDownedComponent();
			if (Downed != nullptr && Downed->ShouldEnterDownedState(DamageInstigator, Report))
			{
				Downed->EnterDownedState(DamageInstigator, Report);
				// Downed players are technically "alive" for HP purposes so that
				// bleeding out can be modelled by the downed component's own timer.
				Health = 1.f;
				bDead = false;
				return ActualDamage;
			}
		}

		OnDeath.Broadcast(DamageInstigator, Report);

		// After the broadcast, so the kill feed still reads "was downed" for a
		// body finished by gunfire; then the corpse stops being revivable.
		if (AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Owner))
		{
			if (UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
			{
				Downed->ClearDownedOnDeath();
			}
		}
	}

	return ActualDamage;
}

float UOneBrahamHealthComponent::Heal(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Before = Health;
	Health = FMath::Min(Health + Amount, MaxHealth);
	const float Healed = Health - Before;

	if (Healed > 0.f)
	{
		OnHealthChanged.Broadcast(Health, MaxHealth, Healed);
		GetOwner()->ForceNetUpdate();
	}

	return Healed;
}

float UOneBrahamHealthComponent::AddArmour(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Before = Armour;
	Armour = FMath::Min(Armour + Amount, MaxArmour);
	OnArmourChanged.Broadcast(Armour, MaxArmour);
	return Armour - Before;
}

void UOneBrahamHealthComponent::GrantInvulnerability(float Seconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}

	const UWorld* World = GetWorld();
	InvulnerableUntil = (World ? World->GetTimeSeconds() : 0.f) + Seconds;
	OnInvulnerabilityChanged.Broadcast(true);
}

void UOneBrahamHealthComponent::ClearInvulnerability()
{
	InvulnerableUntil = -1.f;
	OnInvulnerabilityChanged.Broadcast(false);
}

void UOneBrahamHealthComponent::Kill(AActor* Killer, FName CauseId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead)
	{
		return;
	}

	AActor* Owner = GetOwner();

	FOneBrahamDamageReport Report;
	Report.BaseDamage = Health;
	Report.FinalDamage = Health;
	Report.HitZone = EOneBrahamHitZone::Torso;
	Report.HitLocation = Owner->GetActorLocation();
	Report.ShotDirection = FVector::DownVector;
	Report.bLethal = true;

	// Not routed through ApplyDamage: armour soaked part of the 1 HP a bleed-out
	// "killed", and a lethal hit there could put the player straight back down.
	const float Before = Health;
	Health = 0.f;
	bDead = true;

	const UWorld* World = GetWorld();
	LastDamageTime = World ? World->GetTimeSeconds() : 0.f;
	LastDamageInstigator = Killer;
	LastDamageReport = Report;

	AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Owner);
	if (AOneBrahamPlayerState* OwnerState = Character ? Character->GetPlayerState<AOneBrahamPlayerState>() : nullptr)
	{
		OwnerState->Health = Health;
	}

	OnHealthChanged.Broadcast(Health, MaxHealth, -Before);
	Owner->ForceNetUpdate();

	OnDeath.Broadcast(Killer, Report);

	if (UOneBrahamDownedComponent* Downed = Character ? Character->GetDownedComponent() : nullptr)
	{
		Downed->ClearDownedOnDeath();
	}
}

void UOneBrahamHealthComponent::GetRecentAttackers(TArray<AActor*>& OutAttackers) const
{
	OutAttackers.Reset();

	const float AssistThreshold = MaxHealth * 0.2f;
	for (const TPair<TWeakObjectPtr<AActor>, float>& Pair : RecentAttackers)
	{
		if (Pair.Key.IsValid() && Pair.Value >= AssistThreshold)
		{
			OutAttackers.Add(Pair.Key.Get());
		}
	}
}

// ==========================================================================
//  Replication
// ==========================================================================
void UOneBrahamHealthComponent::OnRep_Health()
{
	// Clients receive health as a blunt value (no deltas) so a lagging client
	// cannot reconstruct a peek at who is shooting whom.
	OnHealthChanged.Broadcast(Health, MaxHealth, 0.f);

	if (AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(GetOwner()))
	{
		if (AOneBrahamPlayerState* State = Character->GetPlayerState<AOneBrahamPlayerState>())
		{
			State->Health = Health;
		}
	}
}

void UOneBrahamHealthComponent::OnRep_Armour()
{
	OnArmourChanged.Broadcast(Armour, MaxArmour);
}

FVector UOneBrahamHealthComponent::GetLastDamageInstigatorLocation() const
{
	if (const AActor* Instigator = LastDamageInstigator.Get())
	{
		return Instigator->GetActorLocation();
	}

	return FVector::ZeroVector;
}
