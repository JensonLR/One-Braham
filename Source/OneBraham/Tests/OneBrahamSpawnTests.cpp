// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSpawnTests.cpp
// ---------------------------------------------------------------------------
// The rules that stop spawn kills and spawn camping (OneBrahamSpawnRules.h),
// pinned one at a time. Each case builds the threat picture the game mode
// would see at a spawn point and checks the weight it gets.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/OneBrahamSpawnRules.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Core/OneBrahamSpawnPoint.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSpawnSafetyTest, "OneBraham.Core.SpawnSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSpawnSafetyTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamSpawnRules;

	// A spawn with nobody near it is untouched.
	{
		const FThreat Clear;
		TestNearlyEqual(TEXT("an empty spawn keeps its weight"), ThreatMultiplier(Clear), 1.f, 0.001f);
	}

	// An enemy in the same room is a veto, line of sight or not.
	{
		FThreat Threat;
		Threat.NearestEnemyCm = HardVetoCm - 1.f;
		TestEqual(TEXT("an enemy inside 12 m vetoes"), ThreatMultiplier(Threat), 0.f);
	}

	// An enemy who can see the spawn from across the floor is a veto too.
	{
		FThreat Threat;
		Threat.NearestEnemyCm = SightVetoCm - 1.f;
		Threat.NearestSightingEnemyCm = SightVetoCm - 1.f;
		TestEqual(TEXT("an enemy with sight inside 25 m vetoes"), ThreatMultiplier(Threat), 0.f);
	}

	// Further away, sight is still nearly as bad as a veto.
	{
		FThreat Threat;
		Threat.NearestEnemyCm = 4000.f;
		Threat.NearestSightingEnemyCm = 4000.f;
		TestTrue(TEXT("an enemy with sight at 40 m makes a spawn a last resort"),
			ThreatMultiplier(Threat) > 0.f && ThreatMultiplier(Threat) <= SightPenalty);
	}

	// Close but out of sight: allowed, heavily discounted.
	{
		FThreat Threat;
		Threat.NearestEnemyCm = 2000.f;
		TestNearlyEqual(TEXT("an unseen enemy at 20 m discounts"), ThreatMultiplier(Threat), NearPenalty, 0.001f);
	}

	// A fresh death next to the spawn means someone is camping it.
	{
		FThreat Threat;
		Threat.NearestRecentDeathCm = RecentDeathCm - 1.f;
		TestNearlyEqual(TEXT("a death beside the spawn discounts it"), ThreatMultiplier(Threat),
			RecentDeathPenalty, 0.001f);
	}

	// A respawn is never handed back to the player who just killed you.
	{
		FThreat Threat;
		Threat.KillerCm = KillerVetoCm - 1.f;
		TestEqual(TEXT("your killer within 40 m vetoes"), ThreatMultiplier(Threat), 0.f);

		FThreat Seen;
		Seen.KillerCm = 9000.f;
		Seen.bKillerHasSight = true;
		TestEqual(TEXT("your killer having sight vetoes at any range"), ThreatMultiplier(Seen), 0.f);
	}

	// Penalties stack: two warning signs are worse than one.
	{
		FThreat Threat;
		Threat.NearestEnemyCm = 2000.f;
		Threat.NearestRecentDeathCm = 500.f;
		TestTrue(TEXT("a near enemy and a fresh death stack"),
			ThreatMultiplier(Threat) < NearPenalty && ThreatMultiplier(Threat) > 0.f);
	}

	// When everything is vetoed, the safest spawn is the one furthest from a
	// threat, and being seen counts double.
	{
		FThreat Near;
		Near.NearestEnemyCm = 1000.f;
		FThreat FarButSeen;
		FarButSeen.NearestEnemyCm = 2000.f;
		FarButSeen.NearestSightingEnemyCm = 1800.f;
		FThreat Far;
		Far.NearestEnemyCm = 2400.f;
		TestTrue(TEXT("the fallback prefers distance"), FallbackSafety(Far) > FallbackSafety(Near));
		TestTrue(TEXT("and an unseen spawn over a seen one"), FallbackSafety(Far) > FallbackSafety(FarButSeen));
	}

	// Floors: a slab stops rounds, a stair connects one floor to the next.
	TestTrue(TEXT("an enemy on the same floor counts"), IsInFloorBand(9, 9));
	TestTrue(TEXT("an enemy one floor away counts"), IsInFloorBand(9, 10));
	TestFalse(TEXT("an enemy two floors away does not"), IsInFloorBand(9, 11));

	// The defaults that go with the rules.
	{
		UOneBrahamMatchSettingsAsset* Ascent = NewObject<UOneBrahamMatchSettingsAsset>(
			GetTransientPackage(), NAME_None, RF_Transient);
		Ascent->Mode = EOneBrahamMatchMode::Ascent;
		Ascent->EnsureDefaults();
		TestTrue(TEXT("ASCENT spawns are protected"), Ascent->SpawnGraceInvulnerabilitySeconds > 0.f);

		UOneBrahamMatchSettingsAsset* FireDrill = NewObject<UOneBrahamMatchSettingsAsset>(
			GetTransientPackage(), NAME_None, RF_Transient);
		FireDrill->Mode = EOneBrahamMatchMode::FireDrill;
		FireDrill->EnsureDefaults();
		TestTrue(TEXT("FIRE DRILL respawns are protected, briefly"),
			FireDrill->SpawnGraceInvulnerabilitySeconds > 0.f && FireDrill->SpawnGraceInvulnerabilitySeconds <= 5.f);

		TestTrue(TEXT("a spawn point rests after use"),
			GetDefault<AOneBrahamSpawnPoint>()->CooldownSeconds > 0.f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
