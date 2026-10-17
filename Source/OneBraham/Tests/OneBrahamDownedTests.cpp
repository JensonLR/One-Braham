// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Downed.*
//
// The downed rule and the revive. Both are checked on transient components with
// no owner: the world-dependent half of the rule (the mode toggle, the player
// state) lives in ShouldEnterDownedState, and the rest is IsDownableHit, which
// is what decides whether the building, a headshot or a frag puts you on the
// carpet or in the ground. The body's collision - a crawler or a corpse is not
// a wall in a doorway - is checked on a transient capsule the same way.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamTypes.h"
#include "Components/CapsuleComponent.h"

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

// ===========================================================================
//  A body on the floor is not a wall in the door
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDownedBodyCollisionTest, "OneBraham.Downed.BodyCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDownedBodyCollisionTest::RunTest(const FString& Parameters)
{
	const ECollisionChannel Interactable = (ECollisionChannel)EOneBrahamTraceChannel::Interactable;
	const ECollisionChannel Weapon = (ECollisionChannel)EOneBrahamTraceChannel::Weapon;

	// --- The rule, as data ---------------------------------------------------
	const AOneBrahamCharacter::FCapsuleCollisionRule Alive =
		AOneBrahamCharacter::GetCapsuleCollisionRule(EOneBrahamLifeState::Alive);
	TestTrue(TEXT("a standing player is a solid capsule"), Alive.Enabled == ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("that other people walk into"), Alive.PawnResponse == ECR_Block);
	TestTrue(TEXT("and that never hides the door behind it"), Alive.InteractableResponse == ECR_Overlap);
	TestTrue(TEXT("and that takes rounds"), Alive.WeaponResponse == ECR_Block);

	const AOneBrahamCharacter::FCapsuleCollisionRule Downed =
		AOneBrahamCharacter::GetCapsuleCollisionRule(EOneBrahamLifeState::Downed);
	TestTrue(TEXT("a crawler keeps a capsule"), Downed.Enabled == ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("that people step past, not into: a 150 cm stair door stays a door"), Downed.PawnResponse == ECR_Ignore);
	TestTrue(TEXT("that the interact trace still finds, so a revive is still possible"), Downed.InteractableResponse == ECR_Block);
	TestTrue(TEXT("and that still takes rounds"), Downed.WeaponResponse == ECR_Block);

	for (const EOneBrahamLifeState Gone : { EOneBrahamLifeState::Dead, EOneBrahamLifeState::Spectating, EOneBrahamLifeState::Extracted })
	{
		TestTrue(FString::Printf(TEXT("%s: the capsule goes; the ragdoll is the body"), *UEnum::GetValueAsString(Gone)),
			AOneBrahamCharacter::GetCapsuleCollisionRule(Gone).Enabled == ECollisionEnabled::NoCollision);
	}

	// --- A corpse is not stood back up by a stale PlayerState -----------------
	TestFalse(TEXT("a client corpse ignores Alive from anyone but its own LifeState"),
		AOneBrahamCharacter::CanClientApplyLifeState(EOneBrahamLifeState::Alive, /*bBodyIsCorpse*/ true));
	TestFalse(TEXT("and Downed"),
		AOneBrahamCharacter::CanClientApplyLifeState(EOneBrahamLifeState::Downed, /*bBodyIsCorpse*/ true));
	TestTrue(TEXT("but a corpse may still go to Spectating"),
		AOneBrahamCharacter::CanClientApplyLifeState(EOneBrahamLifeState::Spectating, /*bBodyIsCorpse*/ true));
	TestTrue(TEXT("and a living body takes any state"),
		AOneBrahamCharacter::CanClientApplyLifeState(EOneBrahamLifeState::Dead, /*bBodyIsCorpse*/ false)
		&& AOneBrahamCharacter::CanClientApplyLifeState(EOneBrahamLifeState::Alive, /*bBodyIsCorpse*/ false));

	// --- The rule, written onto a capsule ------------------------------------
	UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	if (Capsule == nullptr)
	{
		AddError(TEXT("Could not create a transient capsule."));
		return false;
	}

	AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(Capsule, EOneBrahamLifeState::Downed);
	TestTrue(TEXT("downed capsule: pawns pass"), Capsule->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
	TestTrue(TEXT("downed capsule: interact trace stops on it"), Capsule->GetCollisionResponseToChannel(Interactable) == ECR_Block);
	TestTrue(TEXT("downed capsule: rounds stop on it"), Capsule->GetCollisionResponseToChannel(Weapon) == ECR_Block);
	TestTrue(TEXT("downed capsule: still a pawn"), Capsule->GetCollisionObjectType() == ECC_Pawn);
	TestTrue(TEXT("downed capsule: collision on"), Capsule->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);

	// The revive: the profile comes back whole, not with the carpet's overrides.
	AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(Capsule, EOneBrahamLifeState::Alive);
	TestTrue(TEXT("revived capsule: pawns blocked again"), Capsule->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
	TestTrue(TEXT("revived capsule: back to overlap for the interact trace"), Capsule->GetCollisionResponseToChannel(Interactable) == ECR_Overlap);
	TestTrue(TEXT("revived capsule: camera still ignored"), Capsule->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore);

	AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(Capsule, EOneBrahamLifeState::Dead);
	TestTrue(TEXT("corpse capsule: no collision at all"), Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

	AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(Capsule, EOneBrahamLifeState::Alive);
	TestTrue(TEXT("respawned capsule: collision back on"), Capsule->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("respawned capsule: pawns blocked"), Capsule->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
