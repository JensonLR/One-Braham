// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDeathRecapTests.cpp
// ---------------------------------------------------------------------------
// The death recap: the lines the death screen sets under the cause, from the
// hits the health component recorded.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/OneBrahamDeathRecap.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDeathRecapTest, "OneBraham.KillFeed.DeathRecap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDeathRecapTest::RunTest(const FString& Parameters)
{
	auto Hit = [](const TCHAR* Who, float Health, float Armour, EOneBrahamHitZone Zone, float Cm, double T)
	{
		FOneBrahamHitRecord R;
		R.AttackerName = Who;
		R.HealthDamage = Health;
		R.ArmourDamage = Armour;
		R.Zone = Zone;
		R.DistanceCm = Cm;
		R.Time = T;
		return R;
	};

	TArray<FOneBrahamHitRecord> Hits;
	OneBrahamDeathRecap::Record(Hits, Hit(TEXT("Alex"), 12.f, 18.f, EOneBrahamHitZone::Torso, 1800.f, 10.0));
	OneBrahamDeathRecap::Record(Hits, Hit(TEXT("Alex"), 30.f, 0.f, EOneBrahamHitZone::Torso, 1800.f, 10.5));
	OneBrahamDeathRecap::Record(Hits, Hit(TEXT("Sam"), 8.f, 0.f, EOneBrahamHitZone::UpperLeg, 3000.f, 11.0));
	OneBrahamDeathRecap::Record(Hits, Hit(TEXT("Alex"), 50.f, 0.f, EOneBrahamHitZone::Head, 1800.f, 11.2));

	const FString Recap = OneBrahamDeathRecap::Summarise(Hits, 11.2, TEXT("Alex"), 63.f, 0.f);
	TArray<FString> Lines;
	Recap.ParseIntoArrayLines(Lines);

	TestEqual(TEXT("one line per attacker, and one for the killer's health"), Lines.Num(), 3);
	TestEqual(TEXT("the most damaging attacker first"), Lines[0],
		FString(TEXT("Alex - 3 hits, 110 damage (1 head, 2 body) at 18 m")));
	TestEqual(TEXT("then the rest"), Lines[1], FString(TEXT("Sam - 1 hit, 8 damage (1 limb) at 30 m")));
	TestEqual(TEXT("and what the killer had left"), Lines[2], FString(TEXT("Alex had 63 health left")));

	TestTrue(TEXT("armour left is named when there is any"),
		OneBrahamDeathRecap::Summarise(Hits, 11.2, TEXT("Alex"), 40.f, 25.f).Contains(TEXT("40 health and 25 armour left")));

	// An old fight is not this fight.
	TestEqual(TEXT("hits outside the window are not part of it"),
		OneBrahamDeathRecap::Summarise(Hits, 11.2 + OneBrahamDeathRecap::WindowSeconds + 5.0, TEXT(""), -1.f, 0.f),
		FString());
	TestFalse(TEXT("an unknown killer's health is not invented"),
		OneBrahamDeathRecap::Summarise(Hits, 11.2, TEXT("Alex"), -1.f, 0.f).Contains(TEXT("left")));

	// The record is bounded.
	TArray<FOneBrahamHitRecord> Many;
	for (int32 i = 0; i < OneBrahamDeathRecap::MaxRecords + 10; ++i)
	{
		OneBrahamDeathRecap::Record(Many, Hit(TEXT("X"), 1.f, 0.f, EOneBrahamHitZone::Torso, 100.f, i));
	}
	TestEqual(TEXT("the record keeps only the last hits"), Many.Num(), OneBrahamDeathRecap::MaxRecords);
	TestEqual(TEXT("and drops the oldest"), Many[0].Time, static_cast<double>(10));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
