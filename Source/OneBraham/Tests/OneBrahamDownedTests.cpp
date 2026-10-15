// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Downed.*
//
// The downed rule and the revive. Both are checked on transient components with
// no owner: the world-dependent half of the rule (the mode toggle, the player
// state) lives in ShouldEnterDownedState, and the rest is IsDownableHit, which
// is what decides whether the building, a headshot or a frag puts you on the
// carpet or in the ground.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamTypes.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  Who goes down and who goes out
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDownedRuleTest, "OneBraham.Downed.Rule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDownedRuleTest::RunTest(const FString& Parameters)
{
	UOneBrahamDownedComponent* Downed = NewObject<UOneBrahamDownedComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Downed == nullptr)
	{
		AddError(TEXT("Could not create a transient downed component."));
		return false;
	}

	FOneBrahamDamageReport Torso;
	Torso.BaseDamage = 30.f;
	Torso.HitZone = EOneBrahamHitZone::Torso;

	TestTrue(TEXT("a player's torso shot downs a player with revives banked"),
		Downed->IsDownableHit(true, Torso, 1));

	// The building - the purge, a fall - has no killer, and it never downs. The
	// purge used to pass the GameState here, which is how it downed people.
	TestFalse(TEXT("an environmental kill is never a down"), Downed->IsDownableHit(false, Torso, 1));
	TestFalse(TEXT("not even with every revive banked"), Downed->IsDownableHit(false, Torso, 3));

	TestFalse(TEXT("out of revives is out"), Downed->IsDownableHit(true, Torso, 0));

	FOneBrahamDamageReport Head = Torso;
	Head.HitZone = EOneBrahamHitZone::Head;
	TestFalse(TEXT("a headshot bypasses the downed state"), Downed->IsDownableHit(true, Head, 1));

	Downed->bHeadshotsBypassDowned = false;
	TestTrue(TEXT("unless the mode turns the bypass off"), Downed->IsDownableHit(true, Head, 1));
	Downed->bHeadshotsBypassDowned = true;

	FOneBrahamDamageReport Frag = Torso;
	Frag.BaseDamage = 150.f;
	TestFalse(TEXT("an explosion bypasses the downed state"), Downed->IsDownableHit(true, Frag, 1));

	Downed->bDowned = true;
	TestFalse(TEXT("a player already crawling is not downed twice"), Downed->IsDownableHit(true, Torso, 1));

	return true;
}

// ===========================================================================
//  A revive is not a respawn
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDownedReviveHealthTest, "OneBraham.Downed.ReviveHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDownedReviveHealthTest::RunTest(const FString& Parameters)
{
	UOneBrahamHealthComponent* Health = NewObject<UOneBrahamHealthComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);

	UOneBrahamDownedComponent* Downed = NewObject<UOneBrahamDownedComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Health == nullptr || Downed == nullptr)
	{
		AddError(TEXT("Could not create transient health components."));
		return false;
	}

	// On the carpet: 1 HP, and armour that took a beating.
	Health->Health = 1.f;
	Health->Armour = 12.f;

	Health->ReviveToHealth(Downed->ReviveHealth);

	TestNearlyEqual(TEXT("a revive stands you up at the revive health"), Health->GetHealth(), Downed->ReviveHealth, 0.01f);
	TestNearlyEqual(TEXT("and leaves the armour where the fight left it"), Health->GetArmour(), 12.f, 0.01f);
	TestTrue(TEXT("and alive"), Health->IsAlive());
	TestTrue(TEXT("below full health"), Health->GetHealth() < Health->GetMaxHealth());

	Health->ReviveToHealth(1000.f);
	TestNearlyEqual(TEXT("never above maximum"), Health->GetHealth(), Health->GetMaxHealth(), 0.01f);

	Health->ReviveToHealth(0.f);
	TestTrue(TEXT("and never a revive into a corpse"), Health->IsAlive());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
