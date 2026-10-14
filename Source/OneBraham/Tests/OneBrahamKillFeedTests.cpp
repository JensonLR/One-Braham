// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.KillFeed
//
// Killing someone is the only thing that happens in this game more often than
// opening a door, and until this suite existed it was the least-wired system in
// it. Three faults, all found in one afternoon:
//
//   1. Nothing ever wrote a kill to the feed. `AOneBrahamGameState::KillFeed`
//      was filled by exactly one code path - `HandlePlayerLeft`, the disconnect
//      handler - so the feed showed players who quit and never showed a kill.
//   2. Nothing ever credited a killer. `AOneBrahamPlayerState::Kills` and
//      `ServerRegisterKill` existed with no caller, so a twelve-player match
//      ended with every scoreboard row reading 0.
//   3. The death screen was drawn on the server. `ShowDeathScreen` was a plain
//      function called from `ServerOnDied`, so a remote player's death screen
//      appeared on a machine with nobody sitting at it, and the cause text -
//      which `UOneBrahamHUDComponent` has accepted since it was written - was
//      never passed anything at all.
//
// The fixes are one call (`AOneBrahamGameMode::RegisterKill`) and one Client RPC
// (`AOneBrahamPlayerController::ClientShowDeathScreen`). This suite asserts the
// order of the verb decision, the wording of the death screen, and that the
// wiring in the three files is still there - because the wiring is the part that
// fails silently.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameMode.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  The verb
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamKillVerbTest, "OneBraham.KillFeed.Verb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamKillVerbTest::RunTest(const FString& Parameters)
{
	// The building outranks everything: a fall that the victim also shot
	// themselves into is still the building's kill, and the feed does not
	// credit a dead man for his own last mistake.
	TestTrue(TEXT("the building outranks a self-kill"),
		AOneBrahamGameMode::ChooseKillVerb(true, true, true, EOneBrahamHitZone::Head)
			== EOneBrahamKillFeedVerb::Building);

	TestTrue(TEXT("the building outranks a downed victim"),
		AOneBrahamGameMode::ChooseKillVerb(true, false, true, EOneBrahamHitZone::Torso)
			== EOneBrahamKillFeedVerb::Building);

	// His own frag is his own, and the feed says so rather than naming the
	// nearest enemy as the killer.
	TestTrue(TEXT("a self-kill is own utility"),
		AOneBrahamGameMode::ChooseKillVerb(false, true, false, EOneBrahamHitZone::Torso)
			== EOneBrahamKillFeedVerb::OwnUtility);

	TestTrue(TEXT("own utility outranks an execution"),
		AOneBrahamGameMode::ChooseKillVerb(false, true, true, EOneBrahamHitZone::Torso)
			== EOneBrahamKillFeedVerb::OwnUtility);

	// Finishing a downed player is an execution even when the round went
	// through the head. This is the branch order that matters most: an
	// execution is worth different copy, and the headshot marker in the feed
	// would be a lie about a player who was already on the floor.
	TestTrue(TEXT("finishing a downed player is an execution, not a headshot"),
		AOneBrahamGameMode::ChooseKillVerb(false, false, true, EOneBrahamHitZone::Head)
			== EOneBrahamKillFeedVerb::Executed);

	TestTrue(TEXT("a standing headshot is a headshot"),
		AOneBrahamGameMode::ChooseKillVerb(false, false, false, EOneBrahamHitZone::Head)
			== EOneBrahamKillFeedVerb::Headshot);

	TestTrue(TEXT("the neck counts as a headshot"),
		AOneBrahamGameMode::ChooseKillVerb(false, false, false, EOneBrahamHitZone::Neck)
			== EOneBrahamKillFeedVerb::Headshot);

	TestTrue(TEXT("a torso shot is a kill"),
		AOneBrahamGameMode::ChooseKillVerb(false, false, false, EOneBrahamHitZone::Torso)
			== EOneBrahamKillFeedVerb::Killed);

	TestTrue(TEXT("a shot in the leg is a kill"),
		AOneBrahamGameMode::ChooseKillVerb(false, false, false, EOneBrahamHitZone::LowerLeg)
			== EOneBrahamKillFeedVerb::Killed);

	// Left is the disconnect handler's verb. Nothing that happens inside a
	// match may produce it: a player who quits while being shot at is still a
	// kill, and a kill credited as "left" would hide that.
	for (int32 Building = 0; Building <= 1; ++Building)
	{
		for (int32 Self = 0; Self <= 1; ++Self)
		{
			for (int32 Downed = 0; Downed <= 1; ++Downed)
			{
				for (int32 Zone = 0; Zone <= static_cast<int32>(EOneBrahamHitZone::Foot); ++Zone)
				{
					const EOneBrahamKillFeedVerb Verb = AOneBrahamGameMode::ChooseKillVerb(
						Building != 0, Self != 0, Downed != 0, static_cast<EOneBrahamHitZone>(Zone));

					TestTrue(TEXT("no combination of circumstances produces the Left verb"),
						Verb != EOneBrahamKillFeedVerb::Left);

					const int32 VerbValue = static_cast<int32>(Verb);

					TestTrue(TEXT("every combination produces a verb"),
						VerbValue >= static_cast<int32>(EOneBrahamKillFeedVerb::Killed)
						&& VerbValue <= static_cast<int32>(EOneBrahamKillFeedVerb::OwnUtility));
				}
			}
		}
	}

	return true;
}

// ===========================================================================
//  The death screen
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDeathCauseTest, "OneBraham.KillFeed.DeathCause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDeathCauseTest::RunTest(const FString& Parameters)
{
	FOneBrahamKillFeedEntry Entry;
	Entry.KillerName = TEXT("m.okafor");
	Entry.VictimName = TEXT("you");
	Entry.WeaponId = TEXT("M4");
	Entry.Verb = EOneBrahamKillFeedVerb::Killed;

	const FString Killed = AOneBrahamCharacter::BuildDeathCauseText(Entry).ToString();
	TestTrue(TEXT("a kill names the killer"), Killed.Contains(TEXT("m.okafor")));
	TestTrue(TEXT("and the weapon that did it"), Killed.Contains(TEXT("M4")));
	TestFalse(TEXT("with no unformatted placeholder left in it"), Killed.Contains(TEXT("{")));

	Entry.Verb = EOneBrahamKillFeedVerb::Headshot;
	const FString Headshot = AOneBrahamCharacter::BuildDeathCauseText(Entry).ToString();
	TestTrue(TEXT("a headshot names the killer"), Headshot.Contains(TEXT("m.okafor")));
	TestFalse(TEXT("and reads differently from a body shot"), Headshot == Killed);

	Entry.Verb = EOneBrahamKillFeedVerb::Executed;
	const FString Executed = AOneBrahamCharacter::BuildDeathCauseText(Entry).ToString();
	TestTrue(TEXT("an execution names the killer"), Executed.Contains(TEXT("m.okafor")));

	// His own frag: the copy acknowledges it and does not name an enemy.
	Entry.KillerName = TEXT("");
	Entry.Verb = EOneBrahamKillFeedVerb::OwnUtility;
	const FString Own = AOneBrahamCharacter::BuildDeathCauseText(Entry).ToString();
	TestFalse(TEXT("own utility is not an empty line"), Own.IsEmpty());
	TestTrue(TEXT("own utility mentions the utility"),
		Own.Contains(TEXT("utility")) || Own.Contains(TEXT("Utility")));

	// The building kills the most players in any extraction shooter, and it
	// must not be reported as a player. With no killer name, every verb that
	// stands for "no hostile contact" produces the security line.
	Entry.KillerName = TEXT("");
	Entry.Verb = EOneBrahamKillFeedVerb::Building;
	const FString Building = AOneBrahamCharacter::BuildDeathCauseText(Entry).ToString();
	TestTrue(TEXT("a building kill names the building"), Building.Contains(TEXT("SECURITY")));
	TestFalse(TEXT("and never names the victim as the killer"),
		Building.Contains(TEXT("you")));

	// A player who quits gets told what they did, not accused of dying.
	Entry.KillerName = TEXT("");
	Entry.Verb = EOneBrahamKillFeedVerb::Left;
	const FString Left = AOneBrahamCharacter::BuildDeathCauseText(Entry).ToString();
	TestFalse(TEXT("leaving produces a line"), Left.IsEmpty());
	TestFalse(TEXT("that is the building's line"), Left == Building);

	// Every verb, with and without a killer, produces something a player can
	// read. An empty death screen is the failure mode this suite exists to
	// prevent, and it is invisible in a screenshot of the feed.
	for (int32 Verb = 0; Verb <= static_cast<int32>(EOneBrahamKillFeedVerb::OwnUtility); ++Verb)
	{
		for (int32 Named = 0; Named <= 1; ++Named)
		{
			FOneBrahamKillFeedEntry Case;
			Case.KillerName = Named != 0 ? TEXT("m.okafor") : FString();
			Case.VictimName = TEXT("you");
			Case.WeaponId = TEXT("MP5");
			Case.Verb = static_cast<EOneBrahamKillFeedVerb>(Verb);

			const FString Text = AOneBrahamCharacter::BuildDeathCauseText(Case).ToString();

			TestFalse(*FString::Printf(TEXT("verb %d (named: %d) produces a line"), Verb, Named),
				Text.IsEmpty());
			TestFalse(*FString::Printf(TEXT("verb %d (named: %d) leaves no placeholder"), Verb, Named),
				Text.Contains(TEXT("{")));
		}
	}

	return true;
}

// ===========================================================================
//  The wiring
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamKillWiringTest, "OneBraham.KillFeed.Wiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamKillWiringTest::RunTest(const FString& Parameters)
{
	// Read the sources. Not a compile-time proof - a compile-time proof of
	// "this function is called" does not exist - but the difference between
	// the state of this project yesterday and today, asserted on every build.
	auto Load = [this](const TCHAR* Relative, FString& Out) -> bool
	{
		const FString Path = FPaths::ProjectDir() / Relative;

		if (!FFileHelper::LoadFileToString(Out, *Path))
		{
			AddError(FString::Printf(TEXT("Could not read %s for the kill-path check."), Relative));
			return false;
		}

		return true;
	};

	FString Character;
	FString GameMode;
	FString Controller;
	FString HUDComponent;

	if (!Load(TEXT("Source/OneBraham/Characters/OneBrahamCharacter.cpp"), Character)
		|| !Load(TEXT("Source/OneBraham/Core/OneBrahamGameMode.cpp"), GameMode)
		|| !Load(TEXT("Source/OneBraham/Core/OneBrahamPlayerController.h"), Controller)
		|| !Load(TEXT("Source/OneBraham/UI/OneBrahamHUDComponent.cpp"), HUDComponent))
	{
		return false;
	}

	// 1. The death path records the kill.
	TestTrue(TEXT("the character's death path registers the kill with the game mode"),
		Character.Contains(TEXT("GameMode->RegisterKill(this, Killer, Report)")));

	// 2. The death screen is a Client RPC, and it carries the cause.
	TestTrue(TEXT("the death screen is a Client RPC"),
		Controller.Contains(TEXT("ClientShowDeathScreen"))
		&& Controller.Contains(TEXT("Client, Reliable")));

	TestTrue(TEXT("the death path sends the cause to the player who died"),
		Character.Contains(TEXT("ClientShowDeathScreen(true, BuildDeathCauseText(KillEntry))")));

	TestTrue(TEXT("and takes the screen down on respawn"),
		Character.Contains(TEXT("ClientShowDeathScreen(false")));

	// 3. The game mode's single kill entry point really does all three things
	//    a kill has to do: the feed, the personal score, the team score.
	TestTrue(TEXT("RegisterKill writes the feed"),
		GameMode.Contains(TEXT("GS->AddKillFeedEntry(Entry)")));

	TestTrue(TEXT("RegisterKill credits the killer"),
		GameMode.Contains(TEXT("KillerState->ServerRegisterKill(ScorePerKill)")));

	TestTrue(TEXT("RegisterKill scores the team in the points mode"),
		GameMode.Contains(TEXT("AddTeamScoreByIndex(")));

	// The assist half. The health component has tracked recent attackers since
	// it was written and nothing ever read them, so a squad's kill was one
	// player's kill and the other two got nothing for the damage that made it.
	TestTrue(TEXT("RegisterKill reads the victim's recent attackers"),
		GameMode.Contains(TEXT("VictimHealth->GetRecentAttackers(Attackers)")));

	TestTrue(TEXT("RegisterKill credits every attacker who is not the killer"),
		GameMode.Contains(TEXT("AttackerController == KillerController")) &&
		GameMode.Contains(TEXT("AttackerState->ServerRegisterAssist()")));

	TestTrue(TEXT("and the feed entry says the kill was assisted"),
		GameMode.Contains(TEXT("Entry.bAssisted = AssistCount > 0")));

	TestTrue(TEXT("and decides the verb through the one function the suite pins"),
		GameMode.Contains(TEXT("ChooseKillVerb(bKilledByBuilding, bSelfKill, Entry.bWasDowned, Report.HitZone)")));

	// 4. The HUD prints what it is given. SetDeathScreenVisible has taken a
	//    cause since it was written and was passed one for the first time today.
	TestTrue(TEXT("the HUD's death screen forwards the cause to the widget"),
		HUDComponent.Contains(TEXT("SetDeathScreenVisible(bVisible, CauseText)")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
