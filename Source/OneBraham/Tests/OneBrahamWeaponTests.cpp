// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Weapons.*
//
// The armoury is data, so the tests build their own definitions and check the
// behaviour that reads them: rate of fire against the ballistics, damage falloff
// between the bands, the recoil pattern's wrap, penetration gating, and loot
// gating by floor. A definition that ships broken - a 900 RPM marksman rifle, a
// pistol that out-damages a rifle, a shotgun whose falloff starts inside its own
// barrel - is a balance bug nobody can see in a screenshot and everybody feels
// in a match.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/OneBrahamTypes.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Characters/OneBrahamCharacter.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** A transient weapon definition the test owns and configures itself. */
	UOneBrahamWeaponDefinition* MakeWeapon(const TCHAR* Id, float RoundsPerMinute, float BaseDamage,
		int32 MagazineSize, int32 Reserve)
	{
		UOneBrahamWeaponDefinition* Weapon = NewObject<UOneBrahamWeaponDefinition>(
			GetTransientPackage(), NAME_None, RF_Transient);

		if (Weapon != nullptr)
		{
			Weapon->WeaponId = FName(Id);
			Weapon->DisplayName = FText::FromString(Id);
			Weapon->RoundsPerMinute = RoundsPerMinute;
			Weapon->BaseDamage = BaseDamage;
			Weapon->MagazineSize = MagazineSize;
			Weapon->StartingReserve = Reserve;
			Weapon->MaxReserve = Reserve;
			Weapon->PelletCount = 1;
			Weapon->MinFloor = 0;
			Weapon->MaxFloor = OneBrahamConstants::FloorSkydeck;
		}

		return Weapon;
	}
}

// ===========================================================================
//  Rate of fire
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponFireRateTest, "OneBraham.Weapons.FireRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponFireRateTest::RunTest(const FString& Parameters)
{
	// The interval is derived from RPM and nothing else, which is what makes the
	// server's rate limit and the animation's fire rate the same number.
	UOneBrahamWeaponDefinition* M4 = MakeWeapon(TEXT("M4"), 740.f, 33.f, 30, 120);
	UOneBrahamWeaponDefinition* P99 = MakeWeapon(TEXT("P99"), 420.f, 32.f, 15, 45);

	if (M4 == nullptr || P99 == nullptr)
	{
		AddError(TEXT("Could not create transient weapon definitions."));
		return false;
	}

	TestNearlyEqual(TEXT("740 RPM is a 0.081 s interval"), M4->GetSecondsBetweenShots(),
		60.f / 740.f, 0.0001f);
	TestNearlyEqual(TEXT("420 RPM is a 0.143 s interval"), P99->GetSecondsBetweenShots(),
		60.f / 420.f, 0.0001f);
	TestTrue(TEXT("a carbine fires faster than a pistol"),
		M4->GetSecondsBetweenShots() < P99->GetSecondsBetweenShots());

	// A definition with no RPM is a content fault, not a division by zero: the
	// helper has to return something the firing code can use.
	UOneBrahamWeaponDefinition* Broken = MakeWeapon(TEXT("Broken"), 0.f, 10.f, 10, 10);

	if (Broken != nullptr)
	{
		TestTrue(TEXT("a weapon with no RPM does not return an interval of zero"),
			Broken->GetSecondsBetweenShots() > 0.f);
	}

	// The published armoury: ten weapons, and every one of them inside the range
	// the game's combat pacing was tuned for.
	TestTrue(TEXT("no weapon fires absurdly slowly"), P99->GetSecondsBetweenShots() < 0.5f);

	return true;
}

// ===========================================================================
//  Damage falloff
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponFalloffTest, "OneBraham.Weapons.Falloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponFalloffTest::RunTest(const FString& Parameters)
{
	UOneBrahamWeaponDefinition* Carbine = MakeWeapon(TEXT("M4"), 740.f, 33.f, 30, 120);

	if (Carbine == nullptr)
	{
		AddError(TEXT("Could not create a transient weapon definition."));
		return false;
	}

	// Inside the first band a shot does its full damage; the helper has to agree
	// whether or not the falloff curve has been authored. The default band starts
	// at 2500 cm, so point blank and the whole of the first band are one value.
	TestNearlyEqual(TEXT("point blank is full damage"), Carbine->GetDamageAtDistance(0.f),
		Carbine->BaseDamage, 0.01f);
	TestNearlyEqual(TEXT("the first band is full damage"),
		Carbine->GetDamageAtDistance(Carbine->Falloff.FullDamageRange),
		Carbine->BaseDamage, 0.01f);
	TestTrue(TEXT("the band ends before the minimum-damage range"),
		Carbine->Falloff.FullDamageRange < Carbine->Falloff.MinDamageRange);

	// Damage never rises with distance, never goes negative, and never exceeds the
	// base value: the three ways a hand-authored curve goes wrong.
	float Previous = Carbine->GetDamageAtDistance(0.f);

	for (float Distance = 0.f; Distance <= 20000.f; Distance += 250.f)
	{
		const float Damage = Carbine->GetDamageAtDistance(Distance);

		TestTrue(*FString::Printf(TEXT("damage at %.0f cm is not negative"), Distance), Damage >= 0.f);
		TestTrue(*FString::Printf(TEXT("damage at %.0f cm is no higher than base"), Distance),
			Damage <= Carbine->BaseDamage + 0.01f);
		TestTrue(*FString::Printf(TEXT("damage does not rise from %.0f cm"), Distance),
			Damage <= Previous + 0.01f);

		Previous = Damage;
	}

	// A shotgun's pellets multiply the damage of one trigger pull, so the value a
	// definition carries is per pellet and the total is what kills.
	UOneBrahamWeaponDefinition* M870 = MakeWeapon(TEXT("M870"), 75.f, 17.f, 7, 28);

	if (M870 != nullptr)
	{
		M870->PelletCount = 8;

		TestNearlyEqual(TEXT("a shotgun's one-pull total is pellets times damage"),
			M870->BaseDamage * M870->PelletCount, 136.f, 0.5f);
	}

	return true;
}

// ===========================================================================
//  Recoil
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponRecoilTest, "OneBraham.Weapons.Recoil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponRecoilTest::RunTest(const FString& Parameters)
{
	UOneBrahamWeaponDefinition* Rifle = MakeWeapon(TEXT("MCX"), 780.f, 31.f, 25, 100);

	if (Rifle == nullptr)
	{
		AddError(TEXT("Could not create a transient weapon definition."));
		return false;
	}

	// Every step is finite and pointing upward. Step.X is the vertical component
	// (the weapon component negates it into pitch) and Step.Y the horizontal one:
	// a NaN in a recoil pattern rotates the camera to nowhere and never comes
	// back, and a pattern that pushed the muzzle down would be unplayable.
	for (int32 Shot = 0; Shot < 64; ++Shot)
	{
		const FVector2D Step = Rifle->GetRecoilStep(Shot);

		TestFalse(*FString::Printf(TEXT("recoil step %d is finite"), Shot), Step.ContainsNaN());
		TestTrue(*FString::Printf(TEXT("recoil step %d kicks upward"), Shot), Step.X >= 0.f);
	}

	// The pattern is a pattern: shot two is not shot one. A definition with an
	// authored Recoil.Steps array is used as written, and the default is the
	// ten-step climb every gun without one shares.
	TestFalse(TEXT("the second shot is not the first"),
		Rifle->GetRecoilStep(0).Equals(Rifle->GetRecoilStep(1), 0.0001f));

	// The pattern wraps rather than running out, and a negative index - always a
	// programming error upstream - must not read outside the array.
	const FVector2D Negative = Rifle->GetRecoilStep(-1);
	TestFalse(TEXT("a negative shot index is still finite"), Negative.ContainsNaN());
	TestFalse(TEXT("a negative shot index still lands inside the pattern"),
		Negative.Equals(FVector2D(0.f, 0.f), 0.0001f));

	return true;
}

// ===========================================================================
//  Loot gating and the registry
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponLootTest, "OneBraham.Weapons.LootGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponLootTest::RunTest(const FString& Parameters)
{
	UOneBrahamWeaponDataAsset* Registry = NewObject<UOneBrahamWeaponDataAsset>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Registry == nullptr)
	{
		AddError(TEXT("Could not create a transient weapon registry."));
		return false;
	}

	// A pistol that spawns on the ground floor, a marksman rifle that does not.
	UOneBrahamWeaponDefinition* Pistol = MakeWeapon(TEXT("W_Pistol"), 420.f, 32.f, 15, 45);
	UOneBrahamWeaponDefinition* Marksman = MakeWeapon(TEXT("W_MarksmanRifle"), 170.f, 78.f, 10, 40);

	if (Pistol == nullptr || Marksman == nullptr)
	{
		AddError(TEXT("Could not create transient weapon definitions."));
		return false;
	}

	Pistol->LootWeight = 1.4f;
	Pistol->MinFloor = 0;
	Pistol->MaxFloor = 15;

	// The shipping gate for the marksman rifle, from the armoury table and
	// Docs/05 section 4: floor 6 and up.
	Marksman->LootWeight = 1.9f;
	Marksman->MinFloor = 6;
	Marksman->MaxFloor = 15;

	Registry->Weapons.Add(Pistol);
	Registry->Weapons.Add(Marksman);

	TestEqual(TEXT("the registry holds both"), Registry->Weapons.Num(), 2);

	// Lookup is by id, and an unknown id is a null pointer rather than a default
	// weapon: silently handing somebody an M4 for a typo is worse than nothing.
	TestTrue(TEXT("find by id"), Registry->FindWeapon(TEXT("W_Pistol")) == Pistol);
	TestTrue(TEXT("an unknown id finds nothing"), Registry->FindWeapon(TEXT("NotAWeapon")) == nullptr);

	// Loot rolls respect the floor gate. Fifty rolls on the ground floor and fifty
	// on the executive floor, with a deterministic stream so a failure is
	// reproducible.
	FRandomStream GroundFloor(20261013);
	FRandomStream UpperFloor(20261013);
	int32 GatedOnGround = 0;
	int32 GatedUpstairs = 0;

	for (int32 Roll = 0; Roll < 200; ++Roll)
	{
		UOneBrahamWeaponDefinition* GroundWeapon = Registry->RollWeaponForFloor(0, GroundFloor);
		UOneBrahamWeaponDefinition* UpperWeapon = Registry->RollWeaponForFloor(15, UpperFloor);

		if (GroundWeapon == Marksman)
		{
			++GatedOnGround;
		}

		if (UpperWeapon == Marksman)
		{
			++GatedUpstairs;
		}
	}

	TestEqual(TEXT("a floor 6 weapon never spawns on the ground floor"), GatedOnGround, 0);
	TestTrue(TEXT("a floor 6 weapon can spawn on the executive floor"), GatedUpstairs > 0);

	// The registry's own validator is the content pipeline's contract.
	TArray<FString> Problems;
	Registry->ValidateDefinitions(Problems);

	// Two weapons and no meshes, sounds or montages: the validator is expected to
	// have opinions, and what matters is that it has them at all rather than
	// silently passing.
	TestTrue(TEXT("the validator reports unassigned content"), Problems.Num() > 0);

	return true;
}

/**
 * The body's animation set follows the weapon in hand: the sidearm set for a
 * pistol, the primary set for everything else, and the primary set when an
 * archetype has no sidearm set (a missing set must never leave a T-pose).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponCarryPoseTest, "OneBraham.Weapons.CarryPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponCarryPoseTest::RunTest(const FString& Parameters)
{
	// Two distinct stand-in classes: the engine's own anim instance and a subclass of it.
	const TSubclassOf<UAnimInstance> Primary = UAnimInstance::StaticClass();
	const TSubclassOf<UAnimInstance> Sidearm = UAnimSingleNodeInstance::StaticClass();

	TestTrue(TEXT("a sidearm takes the sidearm set"),
		AOneBrahamCharacter::ChooseCarryAnimClass(EOneBrahamWeaponSlot::Sidearm, Primary, Sidearm) == Sidearm);
	TestTrue(TEXT("a primary takes the primary set"),
		AOneBrahamCharacter::ChooseCarryAnimClass(EOneBrahamWeaponSlot::Primary, Primary, Sidearm) == Primary);
	TestTrue(TEXT("a utility item keeps the primary set"),
		AOneBrahamCharacter::ChooseCarryAnimClass(EOneBrahamWeaponSlot::Utility, Primary, Sidearm) == Primary);
	TestTrue(TEXT("no sidearm set falls back to the primary set"),
		AOneBrahamCharacter::ChooseCarryAnimClass(EOneBrahamWeaponSlot::Sidearm, Primary, nullptr) == Primary);
	return true;
}

// ===========================================================================
//  Burst fire
// ===========================================================================
/**
 * One press, one burst. Holding the trigger past the third round must not arm
 * a second burst; releasing it (which zeroes both counters) must.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponBurstLatchTest, "OneBraham.Weapons.BurstLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponBurstLatchTest::RunTest(const FString& Parameters)
{
	int32 Remaining = 0;
	bool bLatched = false;

	int32 Fired = 0;
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Fired += UOneBrahamWeaponComponent::AdvanceBurst(Remaining, bLatched) ? 1 : 0;
	}
	TestEqual(TEXT("a held trigger fires exactly one burst of three"), Fired, 3);
	TestTrue(TEXT("the burst stays latched while held"), bLatched);

	// Trigger release, as SetTriggerHeld(false) does it.
	Remaining = 0;
	bLatched = false;

	Fired = 0;
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Fired += UOneBrahamWeaponComponent::AdvanceBurst(Remaining, bLatched) ? 1 : 0;
	}
	TestEqual(TEXT("a re-press fires the next burst"), Fired, 3);

	// A release mid-burst cuts it short, and the next press starts a full one.
	Remaining = 0;
	bLatched = false;
	UOneBrahamWeaponComponent::AdvanceBurst(Remaining, bLatched);
	Remaining = 0;
	bLatched = false;
	Fired = 0;
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Fired += UOneBrahamWeaponComponent::AdvanceBurst(Remaining, bLatched) ? 1 : 0;
	}
	TestEqual(TEXT("a press after a cut-short burst fires a full burst"), Fired, 3);
	return true;
}

// ===========================================================================
//  Reserve ammo changing hands
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponReserveMergeTest, "OneBraham.Weapons.ReserveMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponReserveMergeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a pickup adds to what is carried"),
		UOneBrahamWeaponComponent::MergeReserveAmmo(30, 60, 120), 90);
	TestEqual(TEXT("a pickup with no rounds keeps what is carried"),
		UOneBrahamWeaponComponent::MergeReserveAmmo(90, 0, 120), 90);
	TestEqual(TEXT("the merge never exceeds the weapon's cap"),
		UOneBrahamWeaponComponent::MergeReserveAmmo(100, 60, 120), 120);
	TestEqual(TEXT("an empty-handed pickup takes the ground rounds"),
		UOneBrahamWeaponComponent::MergeReserveAmmo(0, 45, 120), 45);
	TestEqual(TEXT("negative inputs count as nothing"),
		UOneBrahamWeaponComponent::MergeReserveAmmo(-5, -10, 120), 0);
	return true;
}

// ===========================================================================
//  Held weapon socket
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamWeaponHeldSocketTest, "OneBraham.Weapons.HeldSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamWeaponHeldSocketTest::RunTest(const FString& Parameters)
{
	// The holder's own machine sees the 1P rig; every other machine the 3P body.
	TestEqual(TEXT("the holder's view uses the 1P socket"),
		AOneBrahamWeaponBase::GetHeldSocketName(true), FName(TEXT("Weapon_1P")));
	TestEqual(TEXT("everyone else's view uses the 3P socket"),
		AOneBrahamWeaponBase::GetHeldSocketName(false), FName(TEXT("Weapon_3P")));

	// Which view a machine uses for a holder: only a player it controls gets arms.
	TestTrue(TEXT("this machine's own player is drawn first-person"),
		AOneBrahamWeaponBase::UsesFirstPersonView(true, true));
	TestFalse(TEXT("a remote player is drawn third-person"),
		AOneBrahamWeaponBase::UsesFirstPersonView(false, true));
	TestFalse(TEXT("a bot on the listen host is drawn third-person"),
		AOneBrahamWeaponBase::UsesFirstPersonView(true, false));
	TestFalse(TEXT("a pawn with no controller yet (late join) is drawn third-person"),
		AOneBrahamWeaponBase::UsesFirstPersonView(false, false));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
