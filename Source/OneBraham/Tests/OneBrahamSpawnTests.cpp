// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSpawnTests.cpp
// ---------------------------------------------------------------------------
// The rules that stop spawn kills and spawn camping (OneBrahamSpawnRules.h),
// pinned one at a time. Each case builds the threat picture the game mode
// would see at a spawn point and checks the weight it gets. SpawnFallback
// pins what happens when the chosen point is blocked, and the ASCENT late join.

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

// A spawn that every rule picks can still be blocked: a bot standing on it, a
// player who spawned there and has not moved. These pin what happens next -
// the next-best point, never the same blocked one again and again - and where
// a player joining an ASCENT match already under way walks in.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSpawnFallbackTest, "OneBraham.Core.SpawnFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSpawnFallbackTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamSpawnRules;

	TArray<int32> Order;

	// Nothing in the way: the mode's own preference stands.
	{
		TArray<FSpawnAttempt> Attempts;
		Attempts.SetNum(3);
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("clear points keep the mode's order"), Order, TArray<int32>({ 0, 1, 2 }));
	}

	// The best point has somebody on it: a clear point goes first, and the
	// occupied one is still on the list (beside it may fit).
	{
		TArray<FSpawnAttempt> Attempts;
		Attempts.SetNum(3);
		Attempts[0].bClearNow = false;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("an occupied point goes behind the clear ones"), Order, TArray<int32>({ 1, 2, 0 }));
	}

	// ASCENT's squad corner: a squadmate standing on it is not a reason to
	// send the next one to another squad's corner.
	{
		TArray<FSpawnAttempt> Attempts;
		Attempts.SetNum(3);
		Attempts[0].bClearNow = false;
		Attempts[0].bSquadPoint = true;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("an occupied squad point stays first"), Order[0], 0);
	}

	// The mode's floors come first: standing beside a squadmate on the right
	// floor beats a clear point on a floor the mode only falls back to (ASCENT
	// above the way in, FIRE DRILL away from the control points).
	{
		TArray<FSpawnAttempt> Attempts;
		Attempts.SetNum(4);
		Attempts[0].bClearNow = false;
		Attempts[1].bClearNow = false;
		Attempts[2].bFallback = true;
		Attempts[3].bFallback = true;
		Attempts[3].bClearNow = false;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("beside a point on the mode's floors before a fallback floor"), Order, TArray<int32>({ 0, 1, 2, 3 }));

		Attempts[1].bClearNow = true;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("a clear point on the mode's floors first of all"), Order, TArray<int32>({ 1, 0, 2, 3 }));

		Attempts[1].FailedSerial = 0;
		Attempts[0].FailedSerial = 1;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("the mode's floors all failed: the fallback floors, then the failures"), Order, TArray<int32>({ 2, 3, 1, 0 }));
	}

	// The respawn that failed on a point tries another one next, even a point
	// that looks worse - and even when the failed one now reads clear.
	{
		TArray<FSpawnAttempt> Attempts;
		Attempts.SetNum(3);
		Attempts[0].FailedSerial = 0;
		Attempts[2].bClearNow = false;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("a point that just failed goes to the back"), Order, TArray<int32>({ 1, 2, 0 }));

		Attempts[0].bSquadPoint = true;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("even the squad's own point"), Order.Last(), 0);
	}

	// Every point blocked: the oldest failure is tried first, so a player
	// rotates through them instead of retrying the newest failure forever.
	{
		TArray<FSpawnAttempt> Attempts;
		Attempts.SetNum(3);
		Attempts[0].FailedSerial = 2;
		Attempts[1].FailedSerial = 0;
		Attempts[2].FailedSerial = 1;
		OrderSpawnAttempts(Attempts, Order);
		TestEqual(TEXT("all failed: oldest failure first"), Order, TArray<int32>({ 1, 2, 0 }));

		// Simulate the respawn queue: each retry fails the head of the list,
		// which becomes the most recent failure. Three retries visit three
		// different points.
		TSet<int32> Visited;
		int32 Serial = 3;
		for (int32 Retry = 0; Retry < 3; ++Retry)
		{
			OrderSpawnAttempts(Attempts, Order);
			Visited.Add(Order[0]);
			Attempts[Order[0]].FailedSerial = Serial++;
		}
		TestEqual(TEXT("three retries, three different points"), Visited.Num(), 3);
	}

	// An empty list is not a crash.
	{
		const TArray<FSpawnAttempt> None;
		OrderSpawnAttempts(None, Order);
		TestEqual(TEXT("no points, no order"), Order.Num(), 0);
	}

	// ASCENT late join: the plaza while it is open, else the lowest floor the
	// lockdown has not sealed or gassed.
	{
		const TArray<int32> Floors = { 0, 1, 3, 9, 15 };
		TestEqual(TEXT("an open plaza is the way in"),
			LateJoinFloor(Floors, [](int32) { return false; }), 0);
		TestEqual(TEXT("a sealed plaza sends them to the lowest open floor"),
			LateJoinFloor(Floors, [](int32 Floor) { return Floor <= 1; }), 3);
		TestEqual(TEXT("floors with no spawn point are skipped"),
			LateJoinFloor(Floors, [](int32 Floor) { return Floor <= 3; }), 9);
		TestEqual(TEXT("every floor closed: nowhere"),
			LateJoinFloor(Floors, [](int32) { return true; }), static_cast<int32>(INDEX_NONE));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
