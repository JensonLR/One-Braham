// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Online.*
//
// The career record is what achievements, Steam stats and cosmetic unlocks
// are all read from, so it is tested without Steam: a match result goes in,
// the record and the earned achievements come out.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Core/OneBrahamSaveSettings.h"
#include "Online/OneBrahamSteamSubsystem.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	FOneBrahamMatchResult MakeAscentResult(EOneBrahamMatchEnding Ending, EOneBrahamTeam Winner, float Seconds)
	{
		FOneBrahamMatchResult Result;
		Result.Mode = EOneBrahamMatchMode::Ascent;
		Result.Ending = Ending;
		Result.WinningTeam = Winner;
		Result.MatchDurationSeconds = Seconds;
		return Result;
	}

	FOneBrahamPlayerSummary MakeRow(int32 PlayerId, EOneBrahamTeam Team, int32 Floor, bool bExtracted, float Damage, int32 Kills)
	{
		FOneBrahamPlayerSummary Row;
		Row.PlayerId = PlayerId;
		Row.Team = Team;
		Row.FloorsClimbed = Floor;
		Row.bExtracted = bExtracted;
		Row.DamageDealt = Damage;
		Row.Kills = Kills;
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamCareerRecordTest, "OneBraham.Online.CareerRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamCareerRecordTest::RunTest(const FString& Parameters)
{
	UOneBrahamSaveSettings* Record = NewObject<UOneBrahamSaveSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("record"), Record))
	{
		return false;
	}

	// A result with no ending is not a finished match and records nothing.
	FOneBrahamMatchResult Unfinished = MakeAscentResult(EOneBrahamMatchEnding::None, EOneBrahamTeam::None, 0.f);
	TestFalse(TEXT("an unfinished result is refused"),
		UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Record, Unfinished, MakeRow(1, EOneBrahamTeam::Alpha, 3, false, 0.f, 0)));
	TestEqual(TEXT("nothing counted"), Record->TotalMatchesPlayed, 0);

	// The local row is found by player id, not by name.
	FOneBrahamMatchResult Win = MakeAscentResult(EOneBrahamMatchEnding::Extracted, EOneBrahamTeam::Alpha, 540.f);
	Win.PlayerSummaries.Add(MakeRow(7, EOneBrahamTeam::Bravo, 9, false, 300.f, 2));
	Win.PlayerSummaries.Add(MakeRow(3, EOneBrahamTeam::Alpha, OneBrahamConstants::FloorSkydeck, true, 0.f, 0));
	const FOneBrahamPlayerSummary* Mine = UOneBrahamSteamSubsystem::FindSummary(Win, 3);
	if (!TestNotNull(TEXT("own row found"), Mine))
	{
		return false;
	}
	TestNull(TEXT("no row for a stranger"), UOneBrahamSteamSubsystem::FindSummary(Win, 42));

	TestTrue(TEXT("a finished match is recorded"), UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Record, Win, *Mine));
	TestEqual(TEXT("matches"), Record->TotalMatchesPlayed, 1);
	TestEqual(TEXT("extractions"), Record->TotalExtractions, 1);
	TestEqual(TEXT("highest floor"), Record->LongestAscentFloorReached, static_cast<int32>(OneBrahamConstants::FloorSkydeck));
	TestEqual(TEXT("best time"), Record->BestAscentTimeSeconds, 540.f);
	TestTrue(TEXT("extracted without dealing damage"), Record->bExtractedWithoutDealingDamage);

	const TArray<FName> First = UOneBrahamSteamSubsystem::EvaluateAchievements(*Record);
	TestTrue(TEXT("Clocked In"), First.Contains(FName(TEXT("ACH_CLOCKED_IN"))));
	TestTrue(TEXT("Signed Out"), First.Contains(FName(TEXT("ACH_FIRST_EXTRACT"))));
	TestTrue(TEXT("Top Floor"), First.Contains(FName(TEXT("ACH_SKYDECK"))));
	TestTrue(TEXT("Express Lift"), First.Contains(FName(TEXT("ACH_EXPRESS_LIFT"))));
	TestTrue(TEXT("Conscientious Objector"), First.Contains(FName(TEXT("ACH_CONSCIENTIOUS_OBJECTOR"))));
	TestFalse(TEXT("five extractions not yet"), First.Contains(FName(TEXT("ACH_EXTRACT_5"))));
	TestTrue(TEXT("the no-damage cosmetic is earned"),
		Record->EarnedUnlockTags.Contains(FName(TEXT("OB.Cosmetic.Unlock.Milestone.NoDamageExtract"))));

	// Earned once: a second evaluation adds nothing.
	TestEqual(TEXT("no achievement twice"), UOneBrahamSteamSubsystem::EvaluateAchievements(*Record).Num(), 0);

	// A slower extraction does not replace the best time.
	FOneBrahamMatchResult Slow = MakeAscentResult(EOneBrahamMatchEnding::Extracted, EOneBrahamTeam::Alpha, 900.f);
	UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Record, Slow, MakeRow(3, EOneBrahamTeam::Alpha, 15, true, 50.f, 1));
	TestEqual(TEXT("best time kept"), Record->BestAscentTimeSeconds, 540.f);

	// Last squad standing on the server floors earns Hold the Floor; on floor 12 it does not.
	FOneBrahamMatchResult Standing = MakeAscentResult(EOneBrahamMatchEnding::LastSquadStanding, EOneBrahamTeam::Alpha, 700.f);
	UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Record, Standing, MakeRow(3, EOneBrahamTeam::Alpha, OneBrahamConstants::FloorExecutive, false, 80.f, 3));
	TestFalse(TEXT("floor 12 is not Server & Comms"), Record->bWonOnServerFloors);
	UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Record, Standing, MakeRow(3, EOneBrahamTeam::Alpha, OneBrahamConstants::FloorServer, false, 80.f, 3));
	TestTrue(TEXT("floor 9 is"), Record->bWonOnServerFloors);

	// A loss on the server floors is not a win there.
	UOneBrahamSaveSettings* Loser = NewObject<UOneBrahamSaveSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Loser, Standing, MakeRow(5, EOneBrahamTeam::Bravo, OneBrahamConstants::FloorServer, false, 80.f, 3));
	TestFalse(TEXT("losing squad"), Loser->bWonOnServerFloors);

	// FIRE DRILL counts wins, and only wins.
	FOneBrahamMatchResult Drill;
	Drill.Mode = EOneBrahamMatchMode::FireDrill;
	Drill.Ending = EOneBrahamMatchEnding::ScoreLimit;
	Drill.WinningTeam = EOneBrahamTeam::InternalStaff;
	UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Loser, Drill, MakeRow(5, EOneBrahamTeam::InternalStaff, 0, false, 10.f, 4));
	UOneBrahamSteamSubsystem::ApplyMatchToRecord(*Loser, Drill, MakeRow(5, EOneBrahamTeam::ExternalContractors, 0, false, 10.f, 4));
	TestEqual(TEXT("one FIRE DRILL win"), Loser->TotalFireDrillWins, 1);
	TestEqual(TEXT("kills from every match"), Loser->TotalKills, 11);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamAchievementTableTest, "OneBraham.Online.AchievementTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamAchievementTableTest::RunTest(const FString& Parameters)
{
	// Every id is unique and well formed, and a fresh profile has earned none.
	UOneBrahamSaveSettings* Fresh = NewObject<UOneBrahamSaveSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	TSet<FString> Seen;
	for (const FOneBrahamAchievementDefinition& Entry : UOneBrahamSteamSubsystem::GetAchievementDefinitions())
	{
		const FString Id(Entry.Id);
		TestTrue(FString::Printf(TEXT("%s starts ACH_"), *Id), Id.StartsWith(TEXT("ACH_")));
		TestFalse(FString::Printf(TEXT("%s is unique"), *Id), Seen.Contains(Id));
		Seen.Add(Id);
		TestFalse(FString::Printf(TEXT("%s is not earned by a fresh profile"), *Id), Entry.IsEarned(*Fresh));
	}

	// The rich presence tokens the code sends exist in the Steam token file.
	FString Vdf;
	const FString VdfPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Steam"), TEXT("rich_presence_english.vdf"));
	if (TestTrue(TEXT("rich presence token file"), FFileHelper::LoadFileToString(Vdf, *VdfPath)))
	{
		for (const TCHAR* Token : { TEXT("#Status_Menu"), TEXT("#Status_Searching"), TEXT("#Status_Ascent"),
			TEXT("#Status_FireDrill"), TEXT("#Status_NightShift"), TEXT("#Status_Spectating"), TEXT("#Status_Results") })
		{
			TestTrue(FString::Printf(TEXT("%s is defined"), Token), Vdf.Contains(FString::Printf(TEXT("\"%s\""), Token)));
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
