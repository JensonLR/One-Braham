// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Core.*, OneBraham.Brand.*
//
// The numbers this game publishes - 12 players in three squads of four, a 30
// second extraction hold, 90 second lockdown stages, a 400 cm floor - are the
// numbers the code has to use. They appear in the design document, the trailer
// copy, the Steam page and this repository's own documentation, and the only
// thing that keeps them true is a test that fails when somebody retunes one of
// them without meaning to.
//
// Every assertion here reads the shipping source of truth rather than a literal:
// the match settings asset's EnsureDefaults() for mode rules, OneBrahamConstants
// for the compiled-in invariants, and the config file for the brand. A test that
// repeats the number it is checking tests nothing.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameSettings.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "OneBraham.h"
#include "UI/OneBrahamUIStyle.h"
#include "Voice/OneBrahamVoiceCodec.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** A transient match settings asset with the defaults of one mode. */
	UOneBrahamMatchSettingsAsset* MakeModeSettings(EOneBrahamMatchMode Mode)
	{
		UOneBrahamMatchSettingsAsset* Settings =
			NewObject<UOneBrahamMatchSettingsAsset>(GetTransientPackage(), NAME_None, RF_Transient);

		if (Settings != nullptr)
		{
			Settings->Mode = Mode;
			Settings->EnsureDefaults();
		}

		return Settings;
	}
}

// ===========================================================================
//  ASCENT - the shipping mode
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamAscentRulesTest, "OneBraham.Core.AscentRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamAscentRulesTest::RunTest(const FString& Parameters)
{
	UOneBrahamMatchSettingsAsset* Settings = MakeModeSettings(EOneBrahamMatchMode::Ascent);

	if (Settings == nullptr)
	{
		AddError(TEXT("Could not create a transient match settings asset."));
		return false;
	}

	// Twelve players, three squads of four, no AI. This is the first thing the
	// store page says.
	TestEqual(TEXT("ASCENT squads"), Settings->TeamCount, 3);
	TestEqual(TEXT("ASCENT players per squad"), Settings->PlayersPerTeam, 4);
	TestEqual(TEXT("ASCENT players"), Settings->TeamCount * Settings->PlayersPerTeam, 12);

	// Extraction: the winch is called, it flies for 45 s, and the squad holds the
	// pad for 30 s while the whole building knows where they are.
	TestTrue(TEXT("ASCENT has extraction"), Settings->bExtractionEnabled);
	TestEqual(TEXT("extraction hold is the compiled-in hold"),
		Settings->ExtractionHoldSeconds, OneBrahamConstants::ExtractionHoldSeconds);
	TestNearlyEqual(TEXT("extraction hold is 30 s"), Settings->ExtractionHoldSeconds, 30.f, 0.5f);
	TestNearlyEqual(TEXT("extraction radius"), Settings->ExtractionZoneRadiusCm, 900.f, 1.f);
	TestTrue(TEXT("the winch is called before it can be boarded"),
		Settings->ExtractionBroadcastSeconds > 0.f);

	// The building closes behind the players, the Skydeck is never purged, and the
	// purge climbs to floor 14, leaving only the canteen and the Skydeck on 15.
	TestTrue(TEXT("ASCENT has lockdown"), Settings->bLockdownEnabled);
	TestNearlyEqual(TEXT("lockdown stage, seconds"), Settings->LockdownStageSeconds, 90.f, 0.5f);
	TestNearlyEqual(TEXT("lockdown warning, seconds"), Settings->LockdownWarningSeconds, 15.f, 0.5f);
	TestEqual(TEXT("the purge ceiling is floor 14"),
		Settings->PurgeFloorCeiling, OneBrahamConstants::FloorPurgeCeiling);
	TestTrue(TEXT("the purge never reaches the Skydeck"),
		Settings->PurgeFloorCeiling < OneBrahamConstants::FloorSkydeck);

	// Combat: a downed state rather than death, and a revive that is slow enough
	// to be a decision rather than a formality.
	TestTrue(TEXT("ASCENT has a downed state"), Settings->bEnableDownedState);
	TestNearlyEqual(TEXT("bleed-out, seconds"), Settings->BleedOutSeconds,
		OneBrahamConstants::BleedOutSeconds, 0.01f);
	TestNearlyEqual(TEXT("revive, seconds"), Settings->ReviveSeconds,
		OneBrahamConstants::ReviveSeconds, 0.01f);
	TestTrue(TEXT("a revive leaves you vulnerable rather than whole"),
		Settings->ReviveHealth < Settings->StartingHealth);
	TestFalse(TEXT("ASCENT does not respawn"), Settings->bRespawnEnabled);

	// The override is the alternative to a keycard, and it is slow on purpose: the
	// alarm it trips is 4 s of the 25 s it takes.
	TestNearlyEqual(TEXT("solo override, seconds"), Settings->TerminalOverrideSeconds,
		OneBrahamConstants::OverrideHoldSeconds, 0.01f);
	TestNearlyEqual(TEXT("override with a card, seconds"),
		Settings->TerminalOverrideWithKeycardSeconds, OneBrahamConstants::OverrideKeycardSeconds, 0.01f);
	TestTrue(TEXT("a card is worth carrying"), Settings->TerminalOverrideWithKeycardSeconds
		< Settings->TerminalOverrideSeconds);

	return true;
}

// ===========================================================================
//  FIRE DRILL - the team mode
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFireDrillRulesTest, "OneBraham.Core.FireDrillRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFireDrillRulesTest::RunTest(const FString& Parameters)
{
	UOneBrahamMatchSettingsAsset* Settings = MakeModeSettings(EOneBrahamMatchMode::FireDrill);

	if (Settings == nullptr)
	{
		AddError(TEXT("Could not create a transient match settings asset."));
		return false;
	}

	// Internal Staff against External Contractors: two even teams of six or eight.
	TestEqual(TEXT("FIRE DRILL teams"), Settings->TeamCount, 2);
	TestTrue(TEXT("FIRE DRILL is even-sided"), Settings->PlayersPerTeam % 2 == 0);
	TestTrue(TEXT("FIRE DRILL is at least 6v6"), Settings->PlayersPerTeam >= 6);
	TestTrue(TEXT("FIRE DRILL is no larger than 8v8"), Settings->PlayersPerTeam <= 8);

	// Faster respawn: this is the mode's whole tempo difference.
	TestTrue(TEXT("FIRE DRILL respawns"), Settings->bRespawnEnabled);
	TestTrue(TEXT("FIRE DRILL respawns faster than a revive takes"),
		Settings->RespawnDelaySeconds < Settings->ReviveSeconds + OneBrahamConstants::ReviveSeconds);

	// No winch in a team match, and nothing to extract from.
	TestFalse(TEXT("FIRE DRILL has no extraction"), Settings->bExtractionEnabled);

	// The points are the building's own rooms: the Lobby, the Server Floor, and
	// the Canteen-Skydeck pair. A control point outside the building would be a
	// different game.
	TestTrue(TEXT("FIRE DRILL has control points"), Settings->ControlPoints.Num() >= 3);

	for (const FOneBrahamControlPointDef& Point : Settings->ControlPoints)
	{
		const FString Label = Point.PointId.ToString();

		TestTrue(*FString::Printf(TEXT("%s is named"), *Label), Point.PointId != NAME_None);
		TestTrue(*FString::Printf(TEXT("%s has a display name"), *Label),
			!Point.DisplayName.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s is inside the building"), *Label),
			Point.FloorIndex >= OneBrahamConstants::FloorPlaza
			&& Point.FloorIndex <= OneBrahamConstants::FloorSkydeck);
		TestTrue(*FString::Printf(TEXT("%s takes long enough to capture to be a fight"), *Label),
			Point.CaptureSeconds >= 2.f);
		TestTrue(*FString::Printf(TEXT("%s is worth holding"), *Label), Point.ScorePerSecond > 0.f);
	}

	return true;
}

// ===========================================================================
//  The compiled-in invariants
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamConstantsTest, "OneBraham.Core.Constants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamConstantsTest::RunTest(const FString& Parameters)
{
	// Squads and capacity.
	TestEqual(TEXT("maximum squads"), OneBrahamConstants::MaxSquads, 4);
	TestEqual(TEXT("squad size"), OneBrahamConstants::SquadSize, 4);
	TestEqual(TEXT("maximum players"), OneBrahamConstants::MaxPlayers, 16);
	TestTrue(TEXT("the server can hold an ASCENT match plus spectators"),
		OneBrahamConstants::MaxPlayers >= 12);

	// The building. Seventeen floors above the ground floor, 400 cm each: every
	// distance in the level, every fall and every "can my voice carry up the
	// stairwell" question follows from this one number.
	TestNearlyEqual(TEXT("floor height, cm"), OneBrahamConstants::FloorHeightCm, 400.f, 0.01f);
	TestNearlyEqual(TEXT("half a storey is where a sound becomes another floor"),
		OneBrahamConstants::HalfFloorHeightCm * 2.f, OneBrahamConstants::FloorHeightCm, 0.01f);
	TestEqual(TEXT("ground floor"), OneBrahamConstants::FloorPlaza, 0);
	TestEqual(TEXT("the Skydeck is floor 15"), OneBrahamConstants::FloorSkydeck, 15);

	// Z to floor, against the map: floor n's slab top is at (n - 1) * 400, and a
	// standing pawn's origin is about 90 cm above it.
	for (int32 Floor = 0; Floor <= ONE_BRAHAM_FLOORS; ++Floor)
	{
		const float SlabZ = (Floor - 1) * OneBrahamConstants::FloorHeightCm;
		TestEqual(FString::Printf(TEXT("a pawn standing on floor %d reads floor %d"), Floor, Floor),
			OneBrahamConstants::FloorIndexForZ(SlabZ + 90.f), Floor);
		TestEqual(FString::Printf(TEXT("a pawn jumping on floor %d still reads floor %d"), Floor, Floor),
			OneBrahamConstants::FloorIndexForZ(SlabZ + 350.f), Floor);
	}

	// The floors the game names have to be in the order the building is in. The
	// canteen constant said 15 for a while - the extraction floor - which is the
	// kind of thing nobody notices until a spawn or a purge uses it.
	TestTrue(TEXT("the lobby is above the plaza"),
		OneBrahamConstants::FloorLobby > OneBrahamConstants::FloorPlaza);
	TestTrue(TEXT("the mezzanine is above the lobby"),
		OneBrahamConstants::FloorMezzanine > OneBrahamConstants::FloorLobby);
	TestTrue(TEXT("the offices are above the mezzanine"),
		OneBrahamConstants::FloorOffices > OneBrahamConstants::FloorMezzanine);
	TestTrue(TEXT("the server floors are above the offices"),
		OneBrahamConstants::FloorServer > OneBrahamConstants::FloorOffices);
	TestTrue(TEXT("the executive floor is above the server floors"),
		OneBrahamConstants::FloorExecutive > OneBrahamConstants::FloorServer);
	TestTrue(TEXT("the canteen is above the executive floors"),
		OneBrahamConstants::FloorCanteen > OneBrahamConstants::FloorExecutive + 2);
	TestEqual(TEXT("the canteen shares the Skydeck floor"),
		OneBrahamConstants::FloorCanteen, OneBrahamConstants::FloorSkydeck);

	// The health model: 100 health, 50 armour, and armour that soaks 60% of
	// incoming damage until it breaks - the number Docs/04's worked example is
	// built on (a 30 damage shot takes 12 health and 18 armour). The code said
	// 0.55 for a while.
	UOneBrahamHealthComponent* Health = NewObject<UOneBrahamHealthComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Health == nullptr)
	{
		AddError(TEXT("Could not create a transient health component."));
		return false;
	}

	TestNearlyEqual(TEXT("maximum health"), Health->MaxHealth, 100.f, 0.01f);
	TestNearlyEqual(TEXT("maximum armour"), Health->MaxArmour, 50.f, 0.01f);
	TestNearlyEqual(TEXT("armour absorption"), Health->ArmourAbsorption, 0.60f, 0.001f);

	{
		const float Shot = 30.f;
		const float Absorbed = FMath::Min(Health->MaxArmour, Shot * Health->ArmourAbsorption);
		const float ThroughToHealth = Shot - Absorbed;

		TestNearlyEqual(TEXT("a 30 damage shot is absorbed down to 12"), ThroughToHealth, 12.f, 0.01f);
		TestNearlyEqual(TEXT("and costs 18 armour"), Absorbed, 18.f, 0.01f);
	}

	TestTrue(TEXT("a headshot is worth more than a body shot"), Health->HeadshotMultiplier > 1.f);

	// The match settings' health has to be the component's, or a mode would
	// promise a 100 health player and spawn a different one.
	{
		UOneBrahamMatchSettingsAsset* Ascent = MakeModeSettings(EOneBrahamMatchMode::Ascent);

		TestNearlyEqual(TEXT("the mode's health is the component's"),
			Ascent != nullptr ? Ascent->StartingHealth : -1.f, Health->MaxHealth, 0.01f);
	}

	// Extraction and the clock.
	TestNearlyEqual(TEXT("extraction hold"), OneBrahamConstants::ExtractionHoldSeconds, 30.f, 0.01f);
	TestNearlyEqual(TEXT("the winch flies for 45 s"), OneBrahamConstants::HelicopterArrivalSeconds, 45.f, 0.01f);
	TestNearlyEqual(TEXT("lockdown stage"), OneBrahamConstants::LockdownStageSeconds, 90.f, 0.01f);
	TestNearlyEqual(TEXT("bleed-out"), OneBrahamConstants::BleedOutSeconds, 60.f, 0.01f);
	TestNearlyEqual(TEXT("revive"), OneBrahamConstants::ReviveSeconds, 4.5f, 0.01f);
	TestNearlyEqual(TEXT("breach fuse"), OneBrahamConstants::BreachFuseSeconds, 3.f, 0.01f);

	// A breach charge has to be a commitment you can change your mind about, and a
	// terminal override has to be long enough that somebody has to cover you.
	TestTrue(TEXT("a breach fuse is long enough to be walked away from"),
		OneBrahamConstants::BreachFuseSeconds >= 2.f);
	TestTrue(TEXT("a solo override is slower than a card"),
		OneBrahamConstants::OverrideHoldSeconds > OneBrahamConstants::OverrideKeycardSeconds);
	TestNearlyEqual(TEXT("a solo override is 12 s"),
		OneBrahamConstants::OverrideHoldSeconds, 12.f, 0.01f);

	// Interaction reach: short enough that you commit, long enough that a door
	// does not demand pixel-perfect positioning. The server's tolerance is the
	// same number, which is what stops a prompt the server refuses.
	TestNearlyEqual(TEXT("interaction range, cm"), OneBrahamConstants::InteractRangeCm, 220.f, 0.01f);
	TestNearlyEqual(TEXT("the server's tolerance is the same reach"),
		UOneBrahamGameSettings::Get().MaxInteractDistanceTolerance,
		OneBrahamConstants::InteractRangeCm, 0.01f);

	// Voice: 16 kHz, 20 ms frames, 640 raw bytes, and a transport budget of
	// 1024 bytes a frame - the numbers the codec and the relay both use.
	TestEqual(TEXT("voice sample rate"), FOneBrahamVoiceCodec::SampleRate, 16000);
	TestEqual(TEXT("a frame is 20 ms"), FOneBrahamVoiceCodec::FrameSamples,
		FOneBrahamVoiceCodec::SampleRate / 50);
	TestEqual(TEXT("a raw frame is 640 bytes"), FOneBrahamVoiceCodec::RawFrameBytes,
		FOneBrahamVoiceCodec::FrameSamples * static_cast<int32>(sizeof(int16)));
	TestTrue(TEXT("a compressed frame fits the transport budget"),
		FOneBrahamVoiceCodec::RawFrameBytes <= FOneBrahamVoiceCodec::MaxTransportFrameBytes);
	TestEqual(TEXT("the settings' sample rate is the codec's"),
		UOneBrahamGameSettings::Get().VoiceSampleRate, FOneBrahamVoiceCodec::SampleRate);

	return true;
}

// ===========================================================================
//  Brand
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBrandPaletteTest, "OneBraham.Brand.Palette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBrandPaletteTest::RunTest(const FString& Parameters)
{
	const UOneBrahamGameSettings& Settings = UOneBrahamGameSettings::Get();

	// The seven brand colours as published, compared with a tolerance because the
	// config file stores them to six decimal places and the class defaults to
	// three.
	auto CheckColour = [this](const TCHAR* Name, const FLinearColor& Actual, float R, float G, float B)
	{
		TestNearlyEqual(FString::Printf(TEXT("%s.R"), Name), Actual.R, R, 0.01f);
		TestNearlyEqual(FString::Printf(TEXT("%s.G"), Name), Actual.G, G, 0.01f);
		TestNearlyEqual(FString::Printf(TEXT("%s.B"), Name), Actual.B, B, 0.01f);
		TestNearlyEqual(FString::Printf(TEXT("%s is opaque"), Name), Actual.A, 1.f, 0.001f);
	};

	CheckColour(TEXT("BrandPrimary"), Settings.BrandPrimary, 0.000f, 0.439f, 0.753f);
	CheckColour(TEXT("BrandNavy"), Settings.BrandNavy, 0.016f, 0.063f, 0.145f);
	CheckColour(TEXT("BrandSignalBlue"), Settings.BrandSignalBlue, 0.078f, 0.522f, 1.000f);
	CheckColour(TEXT("BrandSafetyYellow"), Settings.BrandSafetyYellow, 1.000f, 0.804f, 0.000f);
	CheckColour(TEXT("BrandEmergencyRed"), Settings.BrandEmergencyRed, 0.867f, 0.118f, 0.122f);
	CheckColour(TEXT("BrandLightGrey"), Settings.BrandLightGrey, 0.878f, 0.898f, 0.922f);

	// The interface reads its colours from the settings rather than from literals,
	// which is what makes the palette a single decision.
	TestTrue(TEXT("the UI style reads the settings' navy"),
		UOneBrahamUIStyle::Navy().Equals(Settings.BrandNavy, 0.001f));
	TestTrue(TEXT("the UI style reads the settings' signal blue"),
		UOneBrahamUIStyle::SignalBlue().Equals(Settings.BrandSignalBlue, 0.001f));

	// Safety yellow is warnings and emergency red is alarms: if they ever converge
	// the interface stops meaning anything.
	TestTrue(TEXT("alarm red is not warning yellow"),
		!Settings.BrandEmergencyRed.Equals(Settings.BrandSafetyYellow, 0.1f));
	TestTrue(TEXT("the interface is readable on the brand navy"),
		Settings.BrandLightGrey.GetLuminance() > Settings.BrandNavy.GetLuminance());

	// Config/DefaultEngine.ini carries the same block, because that is the copy a
	// designer edits in Project Settings and the copy that ships. Read as a file
	// rather than through the config cache: the point is what is in the .ini, not
	// what the running process happens to have merged.
	{
		const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
		FString Ini;

		if (!FFileHelper::LoadFileToString(Ini, *IniPath))
		{
			AddError(FString::Printf(TEXT("Could not read %s"), *IniPath));
			return false;
		}

		// Find the BrandNavy line and pull its three channels out of the tuple.
		auto ChannelValue = [](const FString& Line, const TCHAR* Channel) -> float
		{
			const int32 At = Line.Find(FString::Printf(TEXT("%s="), Channel), ESearchCase::CaseSensitive);

			return At == INDEX_NONE ? -1.f : FCString::Atof(*Line.Mid(At + 2));
		};

		FString NavyLine;
		TArray<FString> Lines;
		Ini.ParseIntoArrayLines(Lines, true);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("BrandNavy=(")))
			{
				NavyLine = Line;
				break;
			}
		}

		TestFalse(TEXT("Config/DefaultEngine.ini has the brand block"), NavyLine.IsEmpty());

		if (!NavyLine.IsEmpty())
		{
			TestNearlyEqual(TEXT("the ini's navy R"), ChannelValue(NavyLine, TEXT("R")), Settings.BrandNavy.R, 0.01f);
			TestNearlyEqual(TEXT("the ini's navy G"), ChannelValue(NavyLine, TEXT("G")), Settings.BrandNavy.G, 0.01f);
			TestNearlyEqual(TEXT("the ini's navy B"), ChannelValue(NavyLine, TEXT("B")), Settings.BrandNavy.B, 0.01f);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
