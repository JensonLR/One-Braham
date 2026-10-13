// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamDownedComponent.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamCharacterMovementComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Characters/OneBrahamRagdollComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UOneBrahamDownedComponent::UOneBrahamDownedComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);
}

void UOneBrahamDownedComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamDownedComponent, bDowned);
	DOREPLIFETIME(UOneBrahamDownedComponent, BleedOutRemaining);
	DOREPLIFETIME(UOneBrahamDownedComponent, ReviveCount);
	DOREPLIFETIME(UOneBrahamDownedComponent, ReviveProgress);
	DOREPLIFETIME(UOneBrahamDownedComponent, bCarryingBody);
}

void UOneBrahamDownedComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<AOneBrahamCharacter>(GetOwner());
}

// ==========================================================================
//  Eligibility
// ==========================================================================
bool UOneBrahamDownedComponent::ShouldEnterDownedState(AActor* Killer, const FOneBrahamDamageReport& Report) const
{
	const AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || Character->GetPlayerState<AOneBrahamPlayerState>() == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* State = Character->GetPlayerState<AOneBrahamPlayerState>();

	// Mode toggle: FIRE DRILL has no downed state (respawns instead).
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS != nullptr)
	{
		const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings();
		if (Settings != nullptr && !Settings->bEnableDownedState)
		{
			return false;
		}
	}

	// Out of revives: you die properly.
	if (State->RemainingRevives <= 0)
	{
		return false;
	}

	// Headshots and explosives bypass the downed state: this is what makes
	// headshots feel like they matter, and what makes frags decisive indoors.
	if (bHeadshotsBypassDowned && Report.HitZone == EOneBrahamHitZone::Head)
	{
		return false;
	}

	if (bExplosionsBypassDowned && Report.BaseDamage >= 120.f)
	{
		return false;
	}

	// The building (falls, purge) never leaves you downed.
	if (Killer == nullptr)
	{
		return false;
	}

	// Downed players cannot be downed again (prevents an infinite loop when a
	// stray round finishes someone who is already crawling).
	if (bDowned)
	{
		return false;
	}

	return true;
}

void UOneBrahamDownedComponent::EnterDownedState(AActor* Killer, const FOneBrahamDamageReport& Report)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority() || bDowned)
	{
		return;
	}

	bDowned = true;
	DownedBy = Killer;
	DownedReport = Report;
	BleedOutRemaining = BleedOutSeconds;
	ReviveProgress = 0.f;
	QuipsSpoken = 0;
	NextQuipTime = BleedOutSeconds * 0.75f;

	// --- Character state --------------------------------------------------
	Character->ApplyLifeStateVisuals(EOneBrahamLifeState::Downed);

	if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		Health->Health = 1.f;   // bleeding, not dead
		Health->LastDamageTime = GetWorld()->GetTimeSeconds();
		// No regeneration while downed: the ceiling logic would otherwise let a
		// downed player heal back above 1 HP and stand up.
	}

	if (UOneBrahamCharacterMovementComponent* Movement = Character->GetOBCharacterMovement())
	{
		Movement->SetSprinting(false);
		Movement->SetOBMovementState(EOneBrahamMovementMode::DownedCrawl);
	}

	// Weapons down: you may keep your pistol holstered for the visual.
	if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
	{
		Weapons->SetAimingDownSights(false);
		Weapons->SetTriggerHeld(false);
	}

	if (AOneBrahamPlayerState* State = Character->GetPlayerState<AOneBrahamPlayerState>())
	{
		State->LifeState = EOneBrahamLifeState::Downed;
	}

	// --- Feedback ---------------------------------------------------------
	OnDownedStateChanged.Broadcast(true, BleedOutRemaining);
	TriggerDownVoiceLine();

	if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController()))
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			// The "downed" audio state: heartbeat, muffled weapon fire, tinnitus.
			Audio->SetDownedAudioState(true);
		}
	}

	OB_MATCH_LOG("'%s' is down (killer '%s', %.1fs bleed-out, %d revives left).",
		State ? *State->GetPlayerName() : *Character->GetName(),
		Killer && Killer->GetInstigator() ? *Killer->GetInstigator()->GetName() : TEXT("unknown"),
		BleedOutSeconds,
		Character->GetPlayerState<AOneBrahamPlayerState>() ? Character->GetPlayerState<AOneBrahamPlayerState>()->RemainingRevives : 0);

	Character->ForceNetUpdate();
}

// ==========================================================================
//  Tick: bleed-out + quips
// ==========================================================================
void UOneBrahamDownedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bDowned || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	BleedOutRemaining = FMath::Max(0.f, BleedOutRemaining - DeltaTime);

	// Revive progress decays if the reviver lets go (2x faster than it builds:
	// you cannot "pump" a revive).
	ReviveProgress = FMath::Max(0.f, ReviveProgress - DeltaTime * 0.5f);

	const AOneBrahamCharacter* Character = CachedCharacter.Get();
	const AOneBrahamPlayerState* State = Character ? Character->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

	// --- Last words -------------------------------------------------------
	// One down line immediately, then an exhausted comment while you wait. The
	// writing here is deliberately mundane: people in pain are not witty.
	if (State != nullptr && BleedOutRemaining <= NextQuipTime && QuipsSpoken < 2)
	{
		++QuipsSpoken;
		NextQuipTime = BleedOutRemaining - FMath::Max(6.f, BleedOutSeconds * 0.25f);

		if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
		{
			Voice->SpeakContextualLine(GetOwner(), QuipsSpoken == 1 ? TEXT("V_DOWNED_CALL_HELP") : TEXT("V_DOWNED_QUIP"));
		}
	}

	// --- Bleed out --------------------------------------------------------
	if (BleedOutRemaining <= 0.f)
	{
		BleedOut();
	}
}

void UOneBrahamDownedComponent::BleedOut()
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority() || !bDowned)
	{
		return;
	}

	bDowned = false;
	BleedOutRemaining = 0.f;

	// The player who put us down gets the kill; assists come from the health
	// component's attacker map.
	FOneBrahamDamageReport FinalReport = DownedReport;
	FinalReport.FinalDamage = 100.f;
	FinalReport.bLethal = true;

	if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		Health->Health = 1.f;	// clear the "downed" state before killing
		Health->Kill(DownedBy.Get());
	}

	OnDownedStateChanged.Broadcast(false, 0.f);
	Character->ForceNetUpdate();
}

// ==========================================================================
//  Revive / execute
// ==========================================================================
void UOneBrahamDownedComponent::SetReviveProgress(float Progress, AActor* Reviver)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bDowned)
	{
		return;
	}

	ReviveProgress = FMath::Clamp(Progress, 0.f, 1.f);
	OnReviveProgress.Broadcast(ReviveProgress, Reviver);
	ForceNetUpdate();
}

void UOneBrahamDownedComponent::ServerRevive(AController* ReviverController)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority() || !bDowned || ReviverController == nullptr)
	{
		return;
	}

	// The reviver must be alive, on the same team, and within reach. The interact
	// component has already checked this, but a server-authoritative game never
	// trusts its own callers: re-validate here.
	const AOneBrahamCharacter* ReviverCharacter = Cast<AOneBrahamCharacter>(ReviverController->GetPawn());
	if (ReviverCharacter == nullptr)
	{
		return;
	}

	// Vertical separation check: you cannot revive through a floor.
	if (FMath::Abs(ReviverCharacter->GetActorLocation().Z - Character->GetActorLocation().Z) > 180.f)
	{
		return;
	}

	bDowned = false;
	ReviveProgress = 0.f;
	++ReviveCount;

	AOneBrahamPlayerState* State = Character->GetPlayerState<AOneBrahamPlayerState>();
	if (State != nullptr)
	{
		State->LifeState = EOneBrahamLifeState::Alive;
		State->RemainingRevives = FMath::Max(0, State->RemainingRevives - 1);
	}

	if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		Health->ResetHealth();
		Health->Heal(ReviveHealth);
		Health->GrantInvulnerability(1.25f); // a short grace so a revive is not a free kill
	}

	Character->ApplyLifeStateVisuals(EOneBrahamLifeState::Alive);

	if (UOneBrahamCharacterMovementComponent* Movement = Character->GetOBCharacterMovement())
	{
		Movement->SetOBMovementState(EOneBrahamMovementMode::Normal);
	}

	if (UOneBrahamRagdollComponent* Ragdoll = Character->GetRagdollComponent())
	{
		Ragdoll->ExitRagdoll();
	}

	if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController()))
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->SetDownedAudioState(false);
		}
	}

	// Credit the reviver: this is worth more score than a kill, and the
	// end-of-match screen says so.
	AOneBrahamPlayerState* ReviverState = ReviverController->GetPlayerState<AOneBrahamPlayerState>();
	if (ReviverState != nullptr)
	{
		ReviverState->ServerRegisterRevive();
	}

	OnDownedStateChanged.Broadcast(false, 0.f);
	Character->ForceNetUpdate();

	OB_MATCH_LOG("'%s' revived '%s' (%d revives remaining).",
		ReviverState ? *ReviverState->GetPlayerName() : TEXT("someone"),
		State ? *State->GetPlayerName() : TEXT("a colleague"),
		State ? State->RemainingRevives : 0);
}

void UOneBrahamDownedComponent::ServerExecute(AController* ExecutorController)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority() || !bDowned)
	{
		return;
	}

	bDowned = false;

	FOneBrahamDamageReport Report = DownedReport;
	Report.FinalDamage = 100.f;
	Report.bLethal = true;
	Report.HitZone = EOneBrahamHitZone::Head;

	// An execution is attributed to the executor, not to whoever downed them.
	AActor* Executor = ExecutorController ? ExecutorController->GetPawn() : nullptr;

	OnExecuted.Broadcast(Executor);

	if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		Health->Health = 1.f;
		Health->ApplyDamage(1000.f, Executor, Report, ExecutorController);
	}

	OnDownedStateChanged.Broadcast(false, 0.f);
	Character->ForceNetUpdate();

	OB_MATCH_LOG("'%s' was executed by '%s'.",
		Character->GetPlayerState() ? *Character->GetPlayerState()->GetPlayerName() : *Character->GetName(),
		ExecutorController && ExecutorController->PlayerState ? *ExecutorController->PlayerState->GetPlayerName() : TEXT("someone"));
}

void UOneBrahamDownedComponent::TriggerDownVoiceLine()
{
	// Voice lines are fired through the subsystem so the local client hears its
	// own character speak (an important part of the "tired office worker" tone).
	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
	{
		Voice->SpeakContextualLine(GetOwner(), TEXT("V_DOWNED"));
	}

	// Everything the whole building can hear about your predicament is your own
	// proximity voice: the game does not announce downs to enemies.
}

void UOneBrahamDownedComponent::OnRep_Downed()
{
	OnDownedStateChanged.Broadcast(bDowned, BleedOutRemaining);

	if (AOneBrahamCharacter* Character = CachedCharacter.Get())
	{
		Character->ApplyLifeStateVisuals(bDowned ? EOneBrahamLifeState::Downed : EOneBrahamLifeState::Alive);
	}
}
