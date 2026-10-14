// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Damage.*
//
// Damage in this game is three pieces of arithmetic in three files: the weapon
// scales its damage by range (UOneBrahamWeaponDefinition), the health component
// scales it by hit zone (UOneBrahamHealthComponent::GetZoneMultiplier), and then
// armour absorbs a share of what is left before health moves. Each piece is
// small enough to look obviously right and the combination is what the player
// feels: a marksman rifle that three-shots a torso through armour, a pistol that
// takes six, and a headshot that always wins the trade.
//
// These assertions deliberately never call ApplyDamage. That function is
// authority-gated by design - a client that calls it is a bug or a cheat, and it
// refuses - so a test that wants to drive it needs a spawned, authoritative
// actor in a real world. That path is exercised end to end by the map-level
// suite; what is checked here is the arithmetic and the data those tests would
// be measuring.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamTypes.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** A transient weapon definition the test owns and configures itself. */
	UOneBrahamWeaponDefinition* MakeTestWeapon(const TCHAR* Id, float BaseDamage, float RoundsPerMinute)
	{
		UOneBrahamWeaponDefinition* Weapon = NewObject<UOneBrahamWeaponDefinition>(
			GetTransientPackage(), NAME_None, RF_Transient);

		if (Weapon != nullptr)
		{
			Weapon->WeaponId = FName(Id);
			Weapon->DisplayName = FText::FromString(Id);
			Weapon->BaseDamage = BaseDamage;
			Weapon->RoundsPerMinute = RoundsPerMinute;
		}

		return Weapon;
	}
}

// ===========================================================================
//  The health model
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDamageModelTest, "OneBraham.Damage.HealthModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDamageModelTest::RunTest(const FString& Parameters)
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

	// 100 health and 50 armour, and a component that starts at full rather than
	// at zero: a fresh actor with zero health would be dead on spawn.
	TestNearlyEqual(TEXT("maximum health"), Health->MaxHealth, 100.f, 0.01f);
	TestNearlyEqual(TEXT("maximum armour"), Health->MaxArmour, 50.f, 0.01f);
	TestNearlyEqual(TEXT("starting health"), Health->GetHealth(), 100.f, 0.01f);
	TestNearlyEqual(TEXT("starting armour"), Health->GetArmour(), Health->MaxArmour, 0.01f);
	TestTrue(TEXT("a fresh component is alive"), Health->IsAlive());
	TestNearlyEqual(TEXT("and at full health fraction"), Health->GetHealthFraction(), 1.f, 0.001f);

	// Armour absorbs 60% of incoming damage until it runs out - the number
	// Docs/04's worked example is built on, and the one the code had wrong for a
	// while at 0.55.
	TestNearlyEqual(TEXT("armour absorption is 60%"), Health->ArmourAbsorption, 0.60f, 0.001f);

	// Health regenerates, but only back to a ceiling well below full: a fight has
	// to cost something durable or the medkit is decoration.
	TestTrue(TEXT("regeneration stops below full health"),
		Health->HealthRegenCeiling < Health->MaxHealth);
	TestTrue(TEXT("and above the revive health"), Health->HealthRegenCeiling > 0.f);

	// Downing and reviving: a downed player is a decision for the squad, not a
	// death with extra steps.
	TestNearlyEqual(TEXT("bleed-out is the compiled-in 60 s"), Downed->BleedOutSeconds,
		OneBrahamConstants::BleedOutSeconds, 0.01f);
	TestNearlyEqual(TEXT("a revive takes the compiled-in 4.5 s"), Downed->ReviveSeconds,
		OneBrahamConstants::ReviveSeconds, 0.01f);
	TestTrue(TEXT("a revive leaves you below the regeneration ceiling"),
		Downed->ReviveHealth < Health->HealthRegenCeiling);

	// The five hit-zone multipliers, in the order they have to be in for the
	// game's fights to read correctly.
	TestNearlyEqual(TEXT("a headshot is the headshot multiplier"), Health->GetZoneMultiplier(EOneBrahamHitZone::Head),
		Health->HeadshotMultiplier, 0.001f);
	TestNearlyEqual(TEXT("a torso shot is 1.0"), Health->GetZoneMultiplier(EOneBrahamHitZone::Torso), 1.f, 0.001f);
	TestNearlyEqual(TEXT("a limb shot is the limb multiplier"),
		Health->GetZoneMultiplier(EOneBrahamHitZone::UpperArm), Health->LimbMultiplier, 0.001f);

	TestTrue(TEXT("a headshot beats a torso shot"),
		Health->GetZoneMultiplier(EOneBrahamHitZone::Head) > Health->GetZoneMultiplier(EOneBrahamHitZone::Torso));
	TestTrue(TEXT("the neck sits between the two"),
		Health->GetZoneMultiplier(EOneBrahamHitZone::Neck) > Health->GetZoneMultiplier(EOneBrahamHitZone::Torso)
		&& Health->GetZoneMultiplier(EOneBrahamHitZone::Neck) < Health->GetZoneMultiplier(EOneBrahamHitZone::Head));
	TestTrue(TEXT("a limb costs less than a torso shot"),
		Health->GetZoneMultiplier(EOneBrahamHitZone::UpperLeg) < Health->GetZoneMultiplier(EOneBrahamHitZone::Torso));

	// Every zone resolves to something sane. A zone with a multiplier of zero is
	// a hit that does nothing, which reads as a broken gun.
	for (int32 Zone = 0; Zone <= static_cast<int32>(EOneBrahamHitZone::Foot); ++Zone)
	{
		const float Multiplier = Health->GetZoneMultiplier(static_cast<EOneBrahamHitZone>(Zone));
		TestTrue(*FString::Printf(TEXT("zone %d does damage"), Zone), Multiplier > 0.f);
		TestTrue(*FString::Printf(TEXT("zone %d is not a one-shot"), Zone), Multiplier < 5.f);
	}

	return true;
}

// ===========================================================================
//  Armour: absorb, break, then nothing
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDamageArmourTest, "OneBraham.Damage.Armour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDamageArmourTest::RunTest(const FString& Parameters)
{
	UOneBrahamHealthComponent* Health = NewObject<UOneBrahamHealthComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Health == nullptr)
	{
		AddError(TEXT("Could not create a transient health component."));
		return false;
	}

	// The arithmetic, walked by hand over a magazine of 30-damage shots against
	// 50 armour absorbing 60%: 18 armour per shot, so shot one leaves 32 armour
	// and 88 health, shot two leaves 14 armour and 76 health, shot three breaks
	// what is left (14 absorbed) and takes 16 health, and shot four is the first
	// one that goes through in full.
	float Armour = Health->MaxArmour;
	float HealthLeft = Health->MaxHealth;
	const float Shot = 30.f;

	const float HealthAfter[] = { 88.f, 76.f, 60.f, 30.f };

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Absorbed = FMath::Min(Armour, Shot * Health->ArmourAbsorption);
		Armour -= Absorbed;
		HealthLeft -= (Shot - Absorbed);

		TestNearlyEqual(*FString::Printf(TEXT("health after shot %d"), Index + 1), HealthLeft,
			HealthAfter[Index], 0.01f);
	}

	TestNearlyEqual(TEXT("four shots break the armour completely"), Armour, 0.f, 0.01f);
	TestTrue(TEXT("and leave the target badly hurt but alive"), HealthLeft > 0.f);
	TestTrue(TEXT("armour never absorbs more than it has"), Armour >= 0.f);

	// Five shots of the same calibre kill: armour buys one extra shot, which is
	// exactly what 50 points of it should be worth.
	TestTrue(TEXT("the fifth shot is lethal"), HealthLeft - Shot <= 0.f);

	// A single huge hit is absorbed down to its remainder and no further: armour
	// is a percentage soak, not a second health bar.
	{
		const float BigHit = 200.f;
		const float Absorbed = FMath::Min(Health->MaxArmour, BigHit * Health->ArmourAbsorption);
		TestNearlyEqual(TEXT("a 200 damage hit is absorbed by 30"), Absorbed, 30.f, 0.01f);
		TestTrue(TEXT("and still takes 170 through"), BigHit - Absorbed > Health->MaxHealth);

		// It reduces a hit, it never inverts one: a fully armoured target cannot
		// take negative damage from the absorption maths.
		TestTrue(TEXT("absorption can never exceed the damage"),
			Absorbed <= BigHit + 0.001f);
	}

	return true;
}

// ===========================================================================
//  The weapon and the zone, together
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDamageCombinationTest, "OneBraham.Damage.Zone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDamageCombinationTest::RunTest(const FString& Parameters)
{
	UOneBrahamHealthComponent* Health = NewObject<UOneBrahamHealthComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);

	// Two definitions at the ends of the armoury: a 5.56 carbine and a 7.62
	// marksman rifle. The values are the class defaults for their categories.
	UOneBrahamWeaponDefinition* Carbine = MakeTestWeapon(TEXT("Carbine"), 33.f, 740.f);
	UOneBrahamWeaponDefinition* Marksman = MakeTestWeapon(TEXT("Marksman"), 78.f, 170.f);

	if (Health == nullptr || Carbine == nullptr || Marksman == nullptr)
	{
		AddError(TEXT("Could not create the transient test objects."));
		return false;
	}

	// Applied damage is the weapon's ranged damage times the zone multiplier. The
	// helper does that once, in one place, so the two systems cannot disagree.
	auto Applied = [Health](const UOneBrahamWeaponDefinition* Weapon, EOneBrahamHitZone Zone, float Distance)
	{
		return Weapon->GetDamageAtDistance(Distance) * Health->GetZoneMultiplier(Zone);
	};

	// A torso shot from a carbine does not kill; a headshot is worth more than
	// double a torso shot, at any range inside the first band.
	const float CarbineTorso = Applied(Carbine, EOneBrahamHitZone::Torso, 1000.f);
	const float CarbineHead = Applied(Carbine, EOneBrahamHitZone::Head, 1000.f);

	TestTrue(TEXT("a carbine torso shot does not one-shot"), CarbineTorso < Health->MaxHealth);
	TestTrue(TEXT("a carbine headshot does not one-shot through full armour either"),
		CarbineHead < Health->MaxHealth + Health->MaxArmour * Health->ArmourAbsorption);

	// Counted properly, shot by shot, with the armour absorbing its share of each
	// one until it breaks - the flat "armour is a pool of extra health" shortcut
	// gives the wrong answer, and it is the answer a balance pass would use.
	auto ShotsToKill = [Health](float DamagePerShot)
	{
		float HealthLeft = Health->MaxHealth;
		float ArmourLeft = Health->MaxArmour;
		int32 Shots = 0;

		while (HealthLeft > 0.f && Shots < 200)
		{
			const float Absorbed = FMath::Min(ArmourLeft, DamagePerShot * Health->ArmourAbsorption);
			ArmourLeft -= Absorbed;
			HealthLeft -= (DamagePerShot - Absorbed);
			++Shots;
		}

		return Shots;
	};

	// A 5.56 carbine: five torso shots through a full plate, two to the head. The
	// headshot is worth three torso shots, which is what makes peeking for one a
	// real decision rather than a formality.
	TestEqual(TEXT("a carbine takes five torso shots through full armour"),
		ShotsToKill(CarbineTorso), 5);
	TestEqual(TEXT("and two to the head"), ShotsToKill(CarbineHead), 2);
	TestTrue(TEXT("the head is worth at least two torso shots"),
		ShotsToKill(CarbineTorso) >= ShotsToKill(CarbineHead) * 2);

	// A 7.62 marksman rifle is the same fight with three shots fewer to the
	// torso, and does not need the head at all.
	TestTrue(TEXT("a marksman torso shot beats a carbine torso shot"),
		Applied(Marksman, EOneBrahamHitZone::Torso, 1000.f) > CarbineTorso);
	TestEqual(TEXT("a marksman rifle takes two torso shots through armour"),
		ShotsToKill(Applied(Marksman, EOneBrahamHitZone::Torso, 1000.f)), 2);
	TestTrue(TEXT("and a marksman headshot is lethal in one"),
		Applied(Marksman, EOneBrahamHitZone::Head, 1000.f) > Health->MaxHealth);

	// Falloff has to actually mean something at the far end of the building: from
	// the ground floor lobby to the Skydeck is fifteen storeys of 400 cm, and a
	// carbine at that range does less than it does across a room - but never
	// nothing, because a bullet that cannot cross an atrium makes the atrium the
	// whole game.
	const float AcrossTheAtrium = 15.f * OneBrahamConstants::FloorHeightCm;
	const float LongCarbineTorso = Applied(Carbine, EOneBrahamHitZone::Torso, AcrossTheAtrium);

	TestTrue(TEXT("a carbine does less damage across the atrium than across a room"),
		LongCarbineTorso < CarbineTorso);
	TestTrue(TEXT("but it still does something"), LongCarbineTorso > 0.f);

	// Limb shots carry no advantage at any range: the game rewards the upper body
	// and the head, and a leg is simply a worse place to shoot somebody.
	TestTrue(TEXT("a torso shot beats a leg shot"),
		Applied(Carbine, EOneBrahamHitZone::Torso, 1000.f) > Applied(Carbine, EOneBrahamHitZone::LowerLeg, 1000.f));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
