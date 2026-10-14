// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceDirector.h"

#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"

namespace
{
	/** How far a squadmate can be and still have seen it happen, cm. */
	constexpr float WitnessRangeCm = 2500.f;

	/** Three hits inside this window is being pinned down. */
	constexpr double SuppressionWindowSeconds = 2.5;
	constexpr int32 SuppressionHits = 3;

	/** Health below which a hit becomes a call for a medkit or a retreat. */
	constexpr float WoundedHealth = 35.f;

	TMap<TWeakObjectPtr<AOneBrahamCharacter>, TArray<double>>& RecentHits()
	{
		static TMap<TWeakObjectPtr<AOneBrahamCharacter>, TArray<double>> Hits;
		return Hits;
	}

	/** Once per world per match: the two-squads warning, the halfway line. */
	TSet<TPair<const UWorld*, FName>>& SaidOnce()
	{
		static TSet<TPair<const UWorld*, FName>> Said;
		return Said;
	}

	bool FirstTime(const UWorld* World, const TCHAR* Key)
	{
		bool bAlready = false;
		SaidOnce().Add(TPair<const UWorld*, FName>(World, FName(Key)), &bAlready);
		return !bAlready;
	}

	bool HasClearSight(const AOneBrahamCharacter* From, const AOneBrahamCharacter* To)
	{
		const UWorld* World = From != nullptr ? From->GetWorld() : nullptr;
		if (World == nullptr || To == nullptr)
		{
			return false;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(OBVoiceSight), false, From);
		Params.AddIgnoredActor(To);
		return !World->LineTraceTestByChannel(From->GetActorLocation() + FVector(0.f, 0.f, 60.f),
			To->GetActorLocation() + FVector(0.f, 0.f, 60.f), ECC_Visibility, Params);
	}

	EOneBrahamWeaponCategory CategoryOf(const AActor* Actor)
	{
		const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Actor);
		const UOneBrahamWeaponComponent* Weapons = Character != nullptr ? Character->GetWeaponComponent() : nullptr;
		const AOneBrahamWeaponBase* Weapon = Weapons != nullptr ? Weapons->GetCurrentWeapon() : nullptr;
		return (Weapon != nullptr && Weapon->Definition != nullptr) ? Weapon->Definition->Category : EOneBrahamWeaponCategory::Utility;
	}
}

// ==========================================================================
//  Helpers
// ==========================================================================
bool FOneBrahamVoiceDirector::IsServer(const UWorld* World)
{
	return World != nullptr && World->GetNetMode() != NM_Client;
}

int32 FOneBrahamVoiceDirector::SquadOf(const AOneBrahamCharacter* Character)
{
	const AOneBrahamPlayerState* State = Character != nullptr ? Character->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	return State != nullptr ? State->SquadId : INDEX_NONE;
}

bool FOneBrahamVoiceDirector::IsUp(const AOneBrahamCharacter* Character)
{
	return Character != nullptr && Character->LifeState == EOneBrahamLifeState::Alive;
}

AOneBrahamCharacter* FOneBrahamVoiceDirector::NearestSquadmate(const AOneBrahamCharacter* Of, float RangeCm)
{
	const int32 Squad = SquadOf(Of);
	if (Of == nullptr || Squad == INDEX_NONE)
	{
		return nullptr;
	}

	AOneBrahamCharacter* Best = nullptr;
	float BestDistance = RangeCm;
	for (TActorIterator<AOneBrahamCharacter> It(Of->GetWorld()); It; ++It)
	{
		AOneBrahamCharacter* Other = *It;
		if (Other == Of || !IsUp(Other) || SquadOf(Other) != Squad)
		{
			continue;
		}

		const float Distance = FVector::Dist(Other->GetActorLocation(), Of->GetActorLocation());
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Other;
		}
	}
	return Best;
}

AOneBrahamCharacter* FOneBrahamVoiceDirector::NearestEnemyWithSight(const AOneBrahamCharacter* Of, float RangeCm)
{
	const int32 Squad = SquadOf(Of);
	if (Of == nullptr)
	{
		return nullptr;
	}

	AOneBrahamCharacter* Best = nullptr;
	float BestDistance = RangeCm;
	for (TActorIterator<AOneBrahamCharacter> It(Of->GetWorld()); It; ++It)
	{
		AOneBrahamCharacter* Other = *It;
		if (Other == Of || !IsUp(Other) || (Squad != INDEX_NONE && SquadOf(Other) == Squad))
		{
			continue;
		}

		const float Distance = FVector::Dist(Other->GetActorLocation(), Of->GetActorLocation());
		if (Distance <= BestDistance && HasClearSight(Other, Of))
		{
			BestDistance = Distance;
			Best = Other;
		}
	}
	return Best;
}

int32 FOneBrahamVoiceDirector::CountEnemiesNear(const AOneBrahamCharacter* Of, float RangeCm)
{
	const int32 Squad = SquadOf(Of);
	int32 Count = 0;
	if (Of == nullptr)
	{
		return 0;
	}

	for (TActorIterator<AOneBrahamCharacter> It(Of->GetWorld()); It; ++It)
	{
		const AOneBrahamCharacter* Other = *It;
		if (Other != Of && IsUp(Other) && (Squad == INDEX_NONE || SquadOf(Other) != Squad)
			&& FVector::Dist(Other->GetActorLocation(), Of->GetActorLocation()) <= RangeCm)
		{
			++Count;
		}
	}
	return Count;
}

TArray<AOneBrahamCharacter*> FOneBrahamVoiceDirector::OneVoicePerSquad(UWorld* World)
{
	TMap<int32, AOneBrahamCharacter*> BySquad;
	if (World == nullptr)
	{
		return {};
	}

	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		AOneBrahamCharacter* Character = *It;
		const AOneBrahamPlayerState* State = Character->GetPlayerState<AOneBrahamPlayerState>();
		if (!IsUp(Character) || State == nullptr || State->SquadId == INDEX_NONE)
		{
			continue;
		}

		AOneBrahamCharacter*& Chosen = BySquad.FindOrAdd(State->SquadId);
		if (Chosen == nullptr || State->bIsSquadLeader)
		{
			Chosen = Character;
		}
	}

	TArray<AOneBrahamCharacter*> Result;
	BySquad.GenerateValueArray(Result);
	return Result;
}

void FOneBrahamVoiceDirector::Say(AOneBrahamCharacter* Speaker, const TCHAR* LineId, float Priority)
{
	if (Speaker == nullptr || !IsServer(Speaker->GetWorld()))
	{
		return;
	}

	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(Speaker))
	{
		Voice->SpeakContextualLine(Speaker, FName(LineId), Priority);
	}
}

void FOneBrahamVoiceDirector::SayLater(AOneBrahamCharacter* Speaker, const TCHAR* LineId, float DelaySeconds)
{
	UWorld* World = Speaker != nullptr ? Speaker->GetWorld() : nullptr;
	if (World == nullptr || !IsServer(World))
	{
		return;
	}

	// A beat before the answer: two people talking over each other is noise,
	// and an answer in the same frame sounds like one person.
	const FName Line(LineId);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Speaker, [Speaker, Line]()
	{
		if (IsUp(Speaker))
		{
			if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(Speaker))
			{
				Voice->SpeakContextualLine(Speaker, Line, 0.4f);
			}
		}
	}), DelaySeconds, false);
}

// ==========================================================================
//  Combat
// ==========================================================================
void FOneBrahamVoiceDirector::OnDamaged(AOneBrahamCharacter* Victim, AActor* Instigator, float Damage)
{
	if (!IsUp(Victim) || Damage <= 0.f || !IsServer(Victim->GetWorld()))
	{
		return;
	}

	const double Now = Victim->GetWorld()->GetTimeSeconds();
	TArray<double>& Hits = RecentHits().FindOrAdd(Victim);
	Hits.RemoveAll([Now](double Time) { return Now - Time > SuppressionWindowSeconds; });
	Hits.Add(Now);

	AOneBrahamCharacter* Shooter = Cast<AOneBrahamCharacter>(Instigator);
	if (Shooter == nullptr && Instigator != nullptr)
	{
		Shooter = Cast<AOneBrahamCharacter>(Instigator->GetInstigator());
	}

	const UOneBrahamHealthComponent* Health = Victim->GetHealthComponent();
	const float Remaining = Health != nullptr ? Health->GetHealth() : 100.f;

	// The person who was hit says the most useful thing they can.
	if (Hits.Num() >= SuppressionHits)
	{
		Say(Victim, TEXT("V_SUPPRESSED"));
	}
	else if (Remaining < WoundedHealth)
	{
		const UOneBrahamInventoryComponent* Inventory = Victim->GetInventoryComponent();
		Say(Victim, (Inventory != nullptr && Inventory->ServerHasMedkit()) ? TEXT("V_RETREATING") : TEXT("V_HEAL_REQUEST"));
	}
	else if (Shooter != nullptr && CategoryOf(Shooter) == EOneBrahamWeaponCategory::MarksmanRifle)
	{
		Say(Victim, TEXT("V_ENEMY_SNIPER"));
	}
	else
	{
		Say(Victim, TEXT("V_TAKING_FIRE"));
	}

	if (Shooter == nullptr || SquadOf(Shooter) == SquadOf(Victim))
	{
		return;
	}

	// A squadmate who saw it adds what the victim could not: how many, and
	// where the shot came from.
	AOneBrahamCharacter* Witness = NearestSquadmate(Victim, WitnessRangeCm);
	if (Witness == nullptr)
	{
		return;
	}

	const FVector ToShooter = (Shooter->GetActorLocation() - Victim->GetActorLocation()).GetSafeNormal2D();
	const bool bFromBehind = FVector::DotProduct(Victim->GetActorForwardVector(), ToShooter) < -0.5f;

	if (bFromBehind)
	{
		Say(Witness, TEXT("V_ENEMY_FLANKING"));
	}
	else if (CountEnemiesNear(Victim, WitnessRangeCm) >= 2)
	{
		Say(Witness, TEXT("V_CONTACT_MULTIPLE"));
	}
	else if (HasClearSight(Witness, Shooter))
	{
		Say(Witness, TEXT("V_ENEMY_SPOTTED"));
	}
}

void FOneBrahamVoiceDirector::OnKilled(AOneBrahamCharacter* Victim, AOneBrahamCharacter* Killer)
{
	if (Victim == nullptr || !IsServer(Victim->GetWorld()))
	{
		return;
	}

	RecentHits().Remove(Victim);

	if (IsUp(Killer) && SquadOf(Killer) != SquadOf(Victim))
	{
		// The killer: was it the end of the fight, or just of him?
		if (CountEnemiesNear(Killer, WitnessRangeCm) == 0)
		{
			const int32 Floor = Killer->GetCurrentFloorIndex();
			const bool bIndoors = Floor >= 1 && Floor < OneBrahamConstants::FloorSkydeck;
			Say(Killer, bIndoors ? TEXT("V_ROOM_CLEAR") : TEXT("V_CLEAR"));
		}
		else
		{
			Say(Killer, FMath::RandBool() ? TEXT("V_ENEMY_DOWN") : TEXT("V_KILL_CONFIRMED"));
		}

		if (AOneBrahamCharacter* Mate = NearestSquadmate(Killer, WitnessRangeCm))
		{
			SayLater(Mate, TEXT("V_GOOD_KILL"), 1.2f);
		}
	}

	// The victim's squad: who saw it, and who is left.
	if (AOneBrahamCharacter* Mate = NearestSquadmate(Victim, WitnessRangeCm))
	{
		Say(Mate, (Killer != nullptr && HasClearSight(Mate, Killer)) ? TEXT("V_SPECTATE_COMMENT") : TEXT("V_BAD_LUCK"));
	}

	const int32 Squad = SquadOf(Victim);
	if (Squad == INDEX_NONE)
	{
		return;
	}

	AOneBrahamCharacter* Survivor = nullptr;
	int32 Standing = 0;
	int32 Downed = 0;
	for (TActorIterator<AOneBrahamCharacter> It(Victim->GetWorld()); It; ++It)
	{
		AOneBrahamCharacter* Member = *It;
		if (Member == Victim || SquadOf(Member) != Squad)
		{
			continue;
		}
		if (IsUp(Member))
		{
			++Standing;
			Survivor = Member;
		}
		else if (Member->LifeState == EOneBrahamLifeState::Downed)
		{
			++Downed;
		}
	}

	if (Standing == 1 && Survivor != nullptr)
	{
		SayLater(Survivor, Downed > 0 ? TEXT("V_LAST_MAN_STANDING") : TEXT("V_SQUAD_WIPED"), 1.5f);
	}
}

void FOneBrahamVoiceDirector::OnDowned(AOneBrahamCharacter* Victim)
{
	if (Victim == nullptr || !IsServer(Victim->GetWorld()))
	{
		return;
	}

	AOneBrahamCharacter* Mate = NearestSquadmate(Victim, 4000.f);
	if (Mate == nullptr)
	{
		return;
	}

	// Two of the squad on the floor and the fight still on: somebody calls it.
	int32 Down = 0;
	for (TActorIterator<AOneBrahamCharacter> It(Victim->GetWorld()); It; ++It)
	{
		Down += (SquadOf(*It) == SquadOf(Victim) && !IsUp(*It)) ? 1 : 0;
	}

	if (Down >= 2 && CountEnemiesNear(Mate, WitnessRangeCm) > 0)
	{
		Say(Mate, TEXT("V_FALL_BACK"));
	}
	else
	{
		SayLater(Mate, TEXT("V_MAN_DOWN"), 0.8f);
	}
}

void FOneBrahamVoiceDirector::OnBleedingOut(AOneBrahamCharacter* Victim)
{
	Say(Victim, TEXT("V_DOWNED_FINAL"), 0.9f);
}

void FOneBrahamVoiceDirector::OnReviveStarted(AOneBrahamCharacter* Reviver, AOneBrahamCharacter* Patient)
{
	// In the open with someone watching: say so, and keep going anyway.
	if (Patient != nullptr && NearestEnemyWithSight(Patient, 3000.f) != nullptr)
	{
		Say(Reviver, TEXT("V_REVIVE_BLOCKED"));
		return;
	}
	Say(Reviver, TEXT("V_REVIVING"));
}

void FOneBrahamVoiceDirector::OnReviveHalfway(AOneBrahamCharacter* Reviver)
{
	Say(Reviver, TEXT("V_HEALING"));
}

void FOneBrahamVoiceDirector::OnExecuteStarted(AOneBrahamCharacter* Executor, AOneBrahamCharacter* Victim)
{
	// The downed player sees it coming: the squad gets two seconds' warning.
	if (Victim != nullptr && IsServer(Victim->GetWorld()))
	{
		if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(Victim))
		{
			Voice->SpeakContextualLine(Victim, FName(TEXT("V_EXECUTE_WARNING")), 1.f);
		}
	}
}

// ==========================================================================
//  Weapons and kit
// ==========================================================================
void FOneBrahamVoiceDirector::OnReload(AOneBrahamCharacter* Shooter, bool bMagazineEmpty)
{
	if (!IsUp(Shooter))
	{
		return;
	}

	if (bMagazineEmpty)
	{
		Say(Shooter, TEXT("V_RELOAD_URGENT"), 0.9f);
	}
	else if (CountEnemiesNear(Shooter, 2000.f) > 0)
	{
		Say(Shooter, TEXT("V_COVER_ME"));
	}
	else
	{
		Say(Shooter, TEXT("V_RELOADING"));
	}

	// The other side can hear a magazine come out.
	if (AOneBrahamCharacter* Enemy = NearestEnemyWithSight(Shooter, 1500.f))
	{
		Say(Enemy, TEXT("V_ENEMY_RELOADING"));
	}
}

void FOneBrahamVoiceDirector::OnDryFire(AOneBrahamCharacter* Shooter)
{
	Say(Shooter, TEXT("V_DRY_FIRE"), 0.8f);
}

void FOneBrahamVoiceDirector::OnSidearmDrawn(AOneBrahamCharacter* Shooter)
{
	// Only worth saying with the fight on: in a quiet room it is just a swap.
	if (Shooter != nullptr && CountEnemiesNear(Shooter, 3000.f) > 0)
	{
		Say(Shooter, TEXT("V_WEAPON_SWITCH"));
	}
}

void FOneBrahamVoiceDirector::OnUtilityThrown(AOneBrahamCharacter* Thrower, FName UtilityId)
{
	if (UtilityId == TEXT("Utility_Flashbang"))
	{
		Say(Thrower, TEXT("V_FLASH_OUT"));
	}
	else if (UtilityId == TEXT("Utility_Smoke"))
	{
		Say(Thrower, TEXT("V_SMOKE_OUT"));
	}
	else if (UtilityId == TEXT("Utility_Frag"))
	{
		// The thrower's own "frag out" is the inventory's; the people it is
		// thrown at can see the arm go back.
		if (AOneBrahamCharacter* Target = NearestEnemyWithSight(Thrower, 1800.f))
		{
			Say(Target, TEXT("V_GRENADE_INCOMING"), 1.f);
		}
	}
}

void FOneBrahamVoiceDirector::OnBreachImminent(AOneBrahamCharacter* Planter)
{
	Say(Planter, TEXT("V_BREACH_FIRE_IN_HOLE"), 1.f);
}

void FOneBrahamVoiceDirector::OnFlashed(AOneBrahamCharacter* Victim, float Strength)
{
	if (Strength >= 0.6f)
	{
		Say(Victim, TEXT("V_FLASHED"), 0.9f);
	}
}

void FOneBrahamVoiceDirector::OnMeleeHit(AOneBrahamCharacter* Attacker)
{
	Say(Attacker, TEXT("V_MELEE_HIT"));
}

void FOneBrahamVoiceDirector::OnMedkitFinished(AOneBrahamCharacter* User)
{
	Say(User, TEXT("V_HEAL_COMPLETE"));
}

void FOneBrahamVoiceDirector::OnMedkitEmpty(AOneBrahamCharacter* User)
{
	Say(User, TEXT("V_NO_MEDKIT"));
}

void FOneBrahamVoiceDirector::OnLootTaken(AOneBrahamCharacter* Taker, bool bWeapon)
{
	Say(Taker, bWeapon ? TEXT("V_WEAPON_FOUND") : TEXT("V_AMMO_FOUND"), 0.3f);
}

// ==========================================================================
//  The building
// ==========================================================================
void FOneBrahamVoiceDirector::OnAccessGranted(AOneBrahamCharacter* User)
{
	Say(User, TEXT("V_KEYCARD_USED"));
}

void FOneBrahamVoiceDirector::OnAccessRefused(AOneBrahamCharacter* User, bool bHoldsAnyCard)
{
	Say(User, bHoldsAnyCard ? TEXT("V_KEYCARD_NEEDED") : TEXT("V_DOOR_LOCKED"));
}

void FOneBrahamVoiceDirector::OnOverrideStarted(AOneBrahamCharacter* User)
{
	Say(User, TEXT("V_OVERRIDE_START"), 0.8f);
}

void FOneBrahamVoiceDirector::OnOverrideProgress(AOneBrahamCharacter* User)
{
	Say(User, TEXT("V_OVERRIDE_IN_PROGRESS"));
}

void FOneBrahamVoiceDirector::OnOverrideRefused(AOneBrahamCharacter* User)
{
	Say(User, TEXT("V_OVERRIDE_DENIED"));
}

void FOneBrahamVoiceDirector::OnLiftCalled(AOneBrahamCharacter* Caller)
{
	Say(Caller, TEXT("V_LIFT_CALLED"));
}

void FOneBrahamVoiceDirector::OnLiftOutOfService(AOneBrahamCharacter* Caller)
{
	Say(Caller, TEXT("V_LIFTS_DOWN"));
}

void FOneBrahamVoiceDirector::OnLiftCrush(AOneBrahamCharacter* Victim)
{
	if (AOneBrahamCharacter* Mate = NearestSquadmate(Victim, 1500.f))
	{
		Say(Mate, TEXT("V_LIFT_DOORS"), 0.9f);
	}
}

void FOneBrahamVoiceDirector::OnLockdownStage(UWorld* World, EOneBrahamLockdownStage Stage)
{
	if (!IsServer(World))
	{
		return;
	}

	for (AOneBrahamCharacter* Voice : OneVoicePerSquad(World))
	{
		switch (Stage)
		{
		case EOneBrahamLockdownStage::PreLockdown:
			Say(Voice, TEXT("V_UP_WE_GO"));
			break;

		case EOneBrahamLockdownStage::Lockdown:
			Say(Voice, TEXT("V_LOCKDOWN_START"), 0.9f);
			if (AOneBrahamCharacter* Mate = NearestSquadmate(Voice, WitnessRangeCm))
			{
				SayLater(Mate, TEXT("V_FLOOR_SECURED"), 5.f);
			}
			break;

		case EOneBrahamLockdownStage::Purge:
			Say(Voice, TEXT("V_SECURITY_PURGE"), 1.f);
			break;

		default:
			break;
		}
	}
}

void FOneBrahamVoiceDirector::OnShuttersClosing(UWorld* World, const FVector& Where)
{
	if (!IsServer(World))
	{
		return;
	}

	AOneBrahamCharacter* Nearest = nullptr;
	float Best = 1500.f;
	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), Where);
		if (IsUp(*It) && Distance < Best)
		{
			Best = Distance;
			Nearest = *It;
		}
	}
	Say(Nearest, TEXT("V_SHUTTERS_CLOSED"));
}

void FOneBrahamVoiceDirector::OnGlassBroken(UWorld* World, const FVector& Where, int32 FloorIndex)
{
	if (!IsServer(World))
	{
		return;
	}

	AOneBrahamCharacter* Nearest = nullptr;
	float Best = 900.f;
	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), Where);
		if (IsUp(*It) && Distance < Best)
		{
			Best = Distance;
			Nearest = *It;
		}
	}

	// On the roof broken glass is a missing wall; anywhere else it is underfoot.
	Say(Nearest, FloorIndex == OneBrahamConstants::FloorSkydeck ? TEXT("V_GLASS_GONE") : TEXT("V_STEP_GLASS"));
}

void FOneBrahamVoiceDirector::OnPropShorted(UWorld* World, const FVector& Where)
{
	if (!IsServer(World))
	{
		return;
	}

	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		if (IsUp(*It) && FVector::Dist(It->GetActorLocation(), Where) < 800.f)
		{
			Say(*It, TEXT("V_PROP_SHORT"));
			return;
		}
	}
}

void FOneBrahamVoiceDirector::OnHelicopterArriving(UWorld* World)
{
	if (!IsServer(World))
	{
		return;
	}

	// Whoever is high enough to see it over the City.
	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		if (IsUp(*It) && It->GetCurrentFloorIndex() >= OneBrahamConstants::FloorSkydeck - 2)
		{
			SayLater(*It, TEXT("V_HELI_SPOTTED"), 6.f);
			return;
		}
	}
}

// ==========================================================================
//  The match
// ==========================================================================
void FOneBrahamVoiceDirector::OnMatchStarted(UWorld* World)
{
	for (AOneBrahamCharacter* Voice : OneVoicePerSquad(World))
	{
		Say(Voice, TEXT("V_MATCH_START"));
	}
}

void FOneBrahamVoiceDirector::OnMatchHalfway(UWorld* World)
{
	if (!FirstTime(World, TEXT("Halfway")))
	{
		return;
	}
	for (AOneBrahamCharacter* Voice : OneVoicePerSquad(World))
	{
		Say(Voice, TEXT("V_MATCH_HALFWAY"));
	}
}

void FOneBrahamVoiceDirector::OnMatchEnded(UWorld* World, int32 WinningSquadId)
{
	for (AOneBrahamCharacter* Voice : OneVoicePerSquad(World))
	{
		Say(Voice, SquadOf(Voice) == WinningSquadId ? TEXT("V_VICTORY") : TEXT("V_DEFEAT"), 1.f);
	}

	// A new match in the same world says its once-only lines again.
	for (auto It = SaidOnce().CreateIterator(); It; ++It)
	{
		if (It->Key == World)
		{
			It.RemoveCurrent();
		}
	}
}

void FOneBrahamVoiceDirector::OnTwoSquadsLeft(UWorld* World)
{
	if (!FirstTime(World, TEXT("TwoSquadsLeft")))
	{
		return;
	}
	for (AOneBrahamCharacter* Voice : OneVoicePerSquad(World))
	{
		Say(Voice, TEXT("V_LAST_SQUAD_WARNING"), 0.9f);
	}
}

// ==========================================================================
//  Replies
// ==========================================================================
void FOneBrahamVoiceDirector::OnLineSpoken(AOneBrahamCharacter* Speaker, FName LineId)
{
	if (Speaker == nullptr || !IsServer(Speaker->GetWorld()))
	{
		return;
	}

	AOneBrahamCharacter* Mate = NearestSquadmate(Speaker, 2000.f);
	if (Mate == nullptr)
	{
		return;
	}

	const UOneBrahamHealthComponent* Health = Mate->GetHealthComponent();
	const bool bMateHurt = Health != nullptr && Health->GetHealth() < 40.f;

	if (LineId == TEXT("V_RELOADING") || LineId == TEXT("V_RELOAD_URGENT") || LineId == TEXT("V_COVER_ME"))
	{
		SayLater(Mate, TEXT("V_COVERING"), 1.f);
	}
	else if (LineId == TEXT("V_PUSH_NOW") || LineId == TEXT("V_HOLD_POSITION"))
	{
		SayLater(Mate, bMateHurt ? TEXT("V_WAIT") : TEXT("V_ACK"), 1.1f);
	}
	else if (LineId == TEXT("V_PING_MOVE"))
	{
		SayLater(Mate, bMateHurt ? TEXT("V_WAIT") : (FMath::RandBool() ? TEXT("V_GO_LEFT") : TEXT("V_GO_RIGHT")), 1.1f);
	}
}
