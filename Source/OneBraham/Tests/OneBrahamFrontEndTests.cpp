// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.FrontEnd.*
//
// The front end's pure pieces: the session settings a host advertises and
// reads back, the URLs a solo player, a host and a joiner travel with, the
// connect string NULL hands a joiner before its host is listening, the
// credits the ODbL requires, the menu camera's drift, and the three maps the
// config names. None needs a world or an online subsystem, which is the
// point: these are the parts a two-machine test is too slow to find.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

#include "Core/OneBrahamFrontEndGameMode.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamSessionSubsystem.h"
#include "OnlineSessionSettings.h"
#include "OneBraham.h"
#include "UI/OneBrahamMainMenuWidget.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	FOneBrahamHostRequest MakeHost(bool bLAN)
	{
		FOneBrahamHostRequest Request;
		Request.Mode = EOneBrahamMatchMode::FireDrill;
		Request.MaxPlayers = 12;
		Request.bLAN = bLAN;
		Request.ServerName = TEXT("Alex's floor");
		Request.MapName = TEXT("L_Persistent_OneBraham");
		Request.Region = TEXT("EU-West");
		Request.Port = 7777;
		return Request;
	}
}

// --------------------------------------------------------------------------
//  Session settings
// --------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndLANSessionSettingsTest,
	"OneBraham.FrontEnd.SessionSettingsLAN",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndLANSessionSettingsTest::RunTest(const FString& Parameters)
{
	FOnlineSessionSettings Settings;
	OneBrahamSessionRules::BuildSessionSettings(MakeHost(true), Settings);

	TestTrue(TEXT("a LAN host creates a LAN match (NULL only advertises those)"), Settings.bIsLANMatch);
	TestFalse(TEXT("no presence on a LAN"), Settings.bUsesPresence);
	TestFalse(TEXT("no lobbies on a LAN"), Settings.bUseLobbiesIfAvailable);
	TestTrue(TEXT("advertised"), Settings.bShouldAdvertise);
	TestFalse(TEXT("ASCENT is one life: no join in progress"), Settings.bAllowJoinInProgress);
	TestEqual(TEXT("slots"), Settings.NumPublicConnections, 12);

	int32 Protocol = INDEX_NONE;
	TestTrue(TEXT("protocol advertised"), Settings.Get(OneBrahamSessionRules::KeyProtocol, Protocol));
	TestEqual(TEXT("protocol is this build's"), Protocol, ONE_BRAHAM_NET_PROTOCOL_VERSION);

	int32 Port = 0;
	TestTrue(TEXT("port advertised for NULL's port-0 connect string"), Settings.Get(OneBrahamSessionRules::KeyPort, Port));
	TestEqual(TEXT("port"), Port, 7777);

	TestTrue(TEXT("our own session is compatible"), OneBrahamSessionRules::IsCompatible(Settings));

	// What the JOIN list on the other machine reads back.
	const FOneBrahamServerInfo Info = OneBrahamSessionRules::ServerInfoFromSettings(Settings, 9, 23, TEXT("alex"));
	TestEqual(TEXT("mode reads back"), Info.Mode, EOneBrahamMatchMode::FireDrill);
	TestEqual(TEXT("name reads back"), Info.ServerName, FString(TEXT("Alex's floor")));
	TestEqual(TEXT("map reads back"), Info.MapName, FString(TEXT("L_Persistent_OneBraham")));
	TestEqual(TEXT("players are slots minus open slots"), Info.CurrentPlayers, 3);
	TestEqual(TEXT("max players"), Info.MaxPlayers, 12);
	TestEqual(TEXT("ping"), Info.PingMs, 23);
	TestTrue(TEXT("LAN flag reads back"), Info.bLAN);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndOnlineSessionSettingsTest,
	"OneBraham.FrontEnd.SessionSettingsOnline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndOnlineSessionSettingsTest::RunTest(const FString& Parameters)
{
	FOnlineSessionSettings Settings;
	OneBrahamSessionRules::BuildSessionSettings(MakeHost(false), Settings);

	TestFalse(TEXT("an online host is not a LAN match"), Settings.bIsLANMatch);
	TestTrue(TEXT("presence: how Steam lists it"), Settings.bUsesPresence);
	TestTrue(TEXT("lobbies where Steam has them"), Settings.bUseLobbiesIfAvailable);
	TestTrue(TEXT("join via presence"), Settings.bAllowJoinViaPresence);
	TestFalse(TEXT("lobby voice off: the game carries its own"), Settings.bUseLobbiesVoiceChatIfAvailable);

	// Slots clamp to what the game supports.
	FOneBrahamHostRequest Tiny = MakeHost(false);
	Tiny.MaxPlayers = 1;
	OneBrahamSessionRules::BuildSessionSettings(Tiny, Settings);
	TestEqual(TEXT("at least two slots"), Settings.NumPublicConnections, 2);

	FOneBrahamHostRequest Huge = MakeHost(false);
	Huge.MaxPlayers = 99;
	OneBrahamSessionRules::BuildSessionSettings(Huge, Settings);
	TestEqual(TEXT("no more than the game's maximum"), Settings.NumPublicConnections, ONE_BRAHAM_MAX_PLAYERS);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndCloseToJoinsTest,
	"OneBraham.FrontEnd.CloseToJoins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndCloseToJoinsTest::RunTest(const FString& Parameters)
{
	// Open while there is a match to join; closed once it is decided, so
	// nobody loads the tower for the end-of-match screen.
	TestFalse(TEXT("warmup is open"), OneBrahamSessionRules::ShouldCloseToJoins(EOneBrahamMatchPhase::Warmup));
	TestFalse(TEXT("a live match is open"), OneBrahamSessionRules::ShouldCloseToJoins(EOneBrahamMatchPhase::Active));
	TestFalse(TEXT("the extraction is open"), OneBrahamSessionRules::ShouldCloseToJoins(EOneBrahamMatchPhase::Extraction));
	TestTrue(TEXT("complete is closed"), OneBrahamSessionRules::ShouldCloseToJoins(EOneBrahamMatchPhase::Complete));
	TestTrue(TEXT("post-match is closed"), OneBrahamSessionRules::ShouldCloseToJoins(EOneBrahamMatchPhase::PostMatch));

	FOnlineSessionSettings Settings;
	OneBrahamSessionRules::BuildSessionSettings(MakeHost(false), Settings);
	const int32 Slots = Settings.NumPublicConnections;
	OneBrahamSessionRules::CloseSettingsToJoins(Settings);
	TestFalse(TEXT("no longer advertised"), Settings.bShouldAdvertise);
	TestFalse(TEXT("no join in progress"), Settings.bAllowJoinInProgress);
	TestFalse(TEXT("no join through presence"), Settings.bAllowJoinViaPresence);
	TestEqual(TEXT("the rest kept"), Settings.NumPublicConnections, Slots);
	FString Mode;
	TestTrue(TEXT("the keys kept"), Settings.Get(OneBrahamSessionRules::KeyMode, Mode));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndCompatibilityTest,
	"OneBraham.FrontEnd.SessionCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndCompatibilityTest::RunTest(const FString& Parameters)
{
	FOnlineSessionSettings Foreign;
	TestFalse(TEXT("a session with none of our keys (another game on the LAN beacon port) is not listed"),
		OneBrahamSessionRules::IsCompatible(Foreign));

	FOnlineSessionSettings OldBuild;
	OneBrahamSessionRules::BuildSessionSettings(MakeHost(true), OldBuild);
	OldBuild.Set(OneBrahamSessionRules::KeyProtocol, ONE_BRAHAM_NET_PROTOCOL_VERSION + 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	TestFalse(TEXT("another protocol is not listed"), OneBrahamSessionRules::IsCompatible(OldBuild));

	FOnlineSessionSettings NoProtocol;
	NoProtocol.Set(OneBrahamSessionRules::KeyMode, FString(TEXT("Ascent")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	TestTrue(TEXT("ours from before the protocol key still lists"), OneBrahamSessionRules::IsCompatible(NoProtocol));

	const FOneBrahamServerInfo Unnamed = OneBrahamSessionRules::ServerInfoFromSettings(NoProtocol, 0, 0, TEXT("sam"));
	TestEqual(TEXT("an unnamed session takes its owner's name"), Unnamed.ServerName, FString(TEXT("sam's floor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndResolveLANTest,
	"OneBraham.FrontEnd.ResolveLAN",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndResolveLANTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("NULL: online asked for, LAN given"), OneBrahamSessionRules::ResolveLAN(false, FName(TEXT("NULL"))));
	TestTrue(TEXT("no subsystem: LAN"), OneBrahamSessionRules::ResolveLAN(false, NAME_None));
	TestFalse(TEXT("Steam: online stays online"), OneBrahamSessionRules::ResolveLAN(false, FName(TEXT("STEAM"))));
	TestTrue(TEXT("Steam: LAN stays LAN"), OneBrahamSessionRules::ResolveLAN(true, FName(TEXT("STEAM"))));
	return true;
}

// --------------------------------------------------------------------------
//  Travel URLs
// --------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndTravelURLTest,
	"OneBraham.FrontEnd.TravelURL",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndTravelURLTest::RunTest(const FString& Parameters)
{
	FOneBrahamTravelRequest Solo;
	Solo.Mode = EOneBrahamMatchMode::Ascent;
	Solo.PlayerName = TEXT("Alex");
	Solo.Archetype = TEXT("BusinessAnalyst");
	Solo.ServerName = TEXT("Solo");

	TestEqual(TEXT("solo: the tower, the mode, the name, no listen"),
		OneBrahamSessionRules::BuildTravelURL(Solo),
		FString(TEXT("/Game/OneBraham/Maps/L_Persistent_OneBraham?Mode=Ascent?Name=Alex?Archetype=BusinessAnalyst?ServerName=Solo")));

	FOneBrahamTravelRequest Host = Solo;
	Host.Mode = EOneBrahamMatchMode::FireDrill;
	Host.bListen = true;
	Host.bLAN = true;
	Host.Bots = 0;
	Host.ServerName = TEXT("Alex's floor");
	const FString HostURL = OneBrahamSessionRules::BuildTravelURL(Host);
	TestTrue(TEXT("host: FireDrill"), HostURL.Contains(TEXT("?Mode=FireDrill")));
	TestTrue(TEXT("host: ?listen last"), HostURL.EndsWith(TEXT("?listen")));
	TestTrue(TEXT("host: LAN flag"), HostURL.Contains(TEXT("?LAN=1")));
	TestTrue(TEXT("host: explicit bot count"), HostURL.Contains(TEXT("?Bots=0")));

	FOneBrahamTravelRequest Absolute = Solo;
	Absolute.MapName = TEXT("/Game/OneBraham/Maps/L_FireDrill_Arena");
	TestTrue(TEXT("a full path is kept as it is"),
		OneBrahamSessionRules::BuildTravelURL(Absolute).StartsWith(TEXT("/Game/OneBraham/Maps/L_FireDrill_Arena?")));

	// A name is the player's to choose; it must not be able to add options.
	FOneBrahamTravelRequest Hostile = Solo;
	Hostile.PlayerName = TEXT("Bob?listen#portal=1");
	const FString Options = OneBrahamSessionRules::MakeTravelOptions(Hostile);
	TestTrue(TEXT("sanitised name"), Options.Contains(TEXT("Name=Boblistenportal1")));
	TestFalse(TEXT("no smuggled listen"), Options.Contains(TEXT("?listen")));
	TestFalse(TEXT("no portal"), Options.Contains(TEXT("#")));

	TestEqual(TEXT("spaces survive (the engine allows them in options)"),
		OneBrahamSessionRules::SanitiseOptionValue(TEXT("  Alex Smith  ")), FString(TEXT("Alex Smith")));
	TestEqual(TEXT("long names are cut"), OneBrahamSessionRules::SanitiseOptionValue(FString::ChrN(80, TEXT('a'))).Len(), 48);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndJoinURLTest,
	"OneBraham.FrontEnd.JoinURL",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndJoinURLTest::RunTest(const FString& Parameters)
{
	// NULL builds its address from the host's net driver port, which is 0 when
	// the session was created before the listen socket opened.
	TestEqual(TEXT("port 0 takes the advertised port"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("192.168.1.5:0"), 7778, FString()), FString(TEXT("192.168.1.5:7778")));
	TestEqual(TEXT("a real port is kept"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("192.168.1.5:7779"), 7778, FString()), FString(TEXT("192.168.1.5:7779")));
	TestEqual(TEXT("no port and no advertised one: the engine default"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("10.0.0.2"), 0, FString()), FString(TEXT("10.0.0.2:7777")));
	TestEqual(TEXT("a Steam id gets a port"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("steam.76561198000000000"), 7777, FString()), FString(TEXT("steam.76561198000000000:7777")));
	TestEqual(TEXT("bracketed IPv6 keeps its colons"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("[::1]:7777"), 0, FString()), FString(TEXT("[::1]:7777")));
	TestEqual(TEXT("options appended with one '?'"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("127.0.0.1:7777"), 0, TEXT("Name=Sam?Mode=Ascent")),
		FString(TEXT("127.0.0.1:7777?Name=Sam?Mode=Ascent")));
	TestEqual(TEXT("options that start with '?' are not doubled"),
		OneBrahamSessionRules::BuildJoinURL(TEXT("127.0.0.1:7777"), 0, TEXT("?Name=Sam")),
		FString(TEXT("127.0.0.1:7777?Name=Sam")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndModeParseTest,
	"OneBraham.FrontEnd.ModeNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndModeParseTest::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Text; EOneBrahamMatchMode Mode; };
	const FCase Cases[] = {
		{ TEXT("ascent"), EOneBrahamMatchMode::Ascent },
		{ TEXT("ASCENT"), EOneBrahamMatchMode::Ascent },
		{ TEXT("firedrill"), EOneBrahamMatchMode::FireDrill },
		{ TEXT("fire_drill"), EOneBrahamMatchMode::FireDrill },
		{ TEXT("Fire Drill"), EOneBrahamMatchMode::FireDrill },
		{ TEXT("FireDrill"), EOneBrahamMatchMode::FireDrill },
		{ TEXT("night-shift"), EOneBrahamMatchMode::NightShift },
	};

	for (const FCase& Case : Cases)
	{
		EOneBrahamMatchMode Parsed = EOneBrahamMatchMode::NightShift;
		TestTrue(FString::Printf(TEXT("'%s' parses"), Case.Text), OneBrahamSessionRules::ParseMatchMode(Case.Text, Parsed));
		TestEqual(FString::Printf(TEXT("'%s' is the right mode"), Case.Text), Parsed, Case.Mode);
	}

	EOneBrahamMatchMode Untouched = EOneBrahamMatchMode::Ascent;
	TestFalse(TEXT("nonsense does not parse"), OneBrahamSessionRules::ParseMatchMode(TEXT("deathmatch"), Untouched));
	TestEqual(TEXT("and leaves the mode alone"), Untouched, EOneBrahamMatchMode::Ascent);

	// The GameMode reads ?Mode= by the enum's name.
	TestEqual(TEXT("ModeKey Ascent"), OneBrahamSessionRules::ModeKey(EOneBrahamMatchMode::Ascent), FString(TEXT("Ascent")));
	TestEqual(TEXT("ModeKey FireDrill"), OneBrahamSessionRules::ModeKey(EOneBrahamMatchMode::FireDrill), FString(TEXT("FireDrill")));

	TestEqual(TEXT("signage label"), UOneBrahamMainMenuWidget::ModeLabel(EOneBrahamMatchMode::FireDrill).ToString(), FString(TEXT("FIRE DRILL")));
	return true;
}

// --------------------------------------------------------------------------
//  The menu
// --------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndMenuPiecesTest,
	"OneBraham.FrontEnd.MenuPieces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndMenuPiecesTest::RunTest(const FString& Parameters)
{
	using EPage = UOneBrahamMainMenuWidget::EPage;

	EPage Page = EPage::Home;
	TestTrue(TEXT("join parses"), UOneBrahamMainMenuWidget::ParsePage(TEXT("JOIN"), Page));
	TestTrue(TEXT("to JOIN"), Page == EPage::Join);
	TestTrue(TEXT("credits parses"), UOneBrahamMainMenuWidget::ParsePage(TEXT(" credits "), Page));
	TestTrue(TEXT("to CREDITS"), Page == EPage::Credits);
	TestFalse(TEXT("wardrobe is not a page (yet)"), UOneBrahamMainMenuWidget::ParsePage(TEXT("wardrobe"), Page));

	FOneBrahamServerInfo Row;
	Row.CurrentPlayers = 3;
	Row.MaxPlayers = 12;
	Row.PingMs = 38;
	TestEqual(TEXT("players"), UOneBrahamMainMenuWidget::PlayersLabel(Row).ToString(), FString(TEXT("3 / 12")));
	TestEqual(TEXT("ping"), UOneBrahamMainMenuWidget::PingLabel(Row).ToString(), FString(TEXT("38 ms")));
	Row.PingMs = 9999;
	TestEqual(TEXT("an unanswered ping is a dash"), UOneBrahamMainMenuWidget::PingLabel(Row).ToString(), FString(TEXT("-")));

	// A full floor stays in the list (the player can see it is there) but is
	// not joined: NULL never refuses a join, and the host's GameSession admits
	// up to its own MaxPlayers, which is not the mode's roster.
	TestFalse(TEXT("3 / 12 is not full"), UOneBrahamMainMenuWidget::IsFull(Row));
	Row.CurrentPlayers = 12;
	TestTrue(TEXT("12 / 12 is full"), UOneBrahamMainMenuWidget::IsFull(Row));
	Row.MaxPlayers = 0;
	TestFalse(TEXT("an unknown capacity is not called full"), UOneBrahamMainMenuWidget::IsFull(Row));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndCreditsTest,
	"OneBraham.FrontEnd.Credits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndCreditsTest::RunTest(const FString& Parameters)
{
	const TArray<FOneBrahamCreditBlock>& Credits = UOneBrahamMainMenuWidget::Credits();
	if (!TestTrue(TEXT("there are credits"), Credits.Num() > 0))
	{
		return false;
	}

	// The ODbL's required attribution, first and in its own words.
	const FString First = Credits[0].Body.ToString();
	TestTrue(TEXT("OpenStreetMap credit first"), First.Contains(TEXT("\u00A9 OpenStreetMap contributors")));
	TestTrue(TEXT("names the ODbL"), First.Contains(TEXT("ODbL")));
	TestTrue(TEXT("points at the copyright page"), First.Contains(TEXT("openstreetmap.org/copyright")));

	FString All;
	for (const FOneBrahamCreditBlock& Block : Credits)
	{
		TestFalse(TEXT("every block has a heading"), Block.Heading.IsEmpty());
		All += Block.Body.ToString() + TEXT("\n");
	}
	TestTrue(TEXT("Poly Haven credited"), All.Contains(TEXT("Poly Haven")));
	TestTrue(TEXT("ambientCG credited"), All.Contains(TEXT("ambientCG")));
	TestTrue(TEXT("Epic's stand-ins credited"), All.Contains(TEXT("Epic Games")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndCameraDriftTest,
	"OneBraham.FrontEnd.CameraDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndCameraDriftTest::RunTest(const FString& Parameters)
{
	const FVector Base(-5200.f, -9800.f, -230.f);
	const FVector LookAt(0.f, -1200.f, 3400.f);

	FVector Location;
	FRotator Rotation;
	AOneBrahamFrontEndCamera::DriftPose(Base, LookAt, 4.f, 120.f, 150.f, 0.f, 0.f, Location, Rotation);
	TestTrue(TEXT("at t=0 the camera is where it was placed"), Location.Equals(Base, 0.5f));
	TestTrue(TEXT("and looks at the point of interest"),
		Rotation.Vector().Equals((LookAt - Base).GetSafeNormal(), 0.001f));

	// Through a whole cycle: never further round than the orbit, never higher
	// than the rise, always the same distance from the tower in plan.
	const float PlanDistance = FVector2D(Base - LookAt).Size();
	for (float Time = 0.f; Time <= 150.f; Time += 7.5f)
	{
		AOneBrahamFrontEndCamera::DriftPose(Base, LookAt, 4.f, 120.f, 150.f, 12.f, Time, Location, Rotation);
		const float Turned = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector2D(Location - LookAt).GetSafeNormal() | FVector2D(Base - LookAt).GetSafeNormal(), -1.f, 1.f)));
		TestTrue(FString::Printf(TEXT("t=%.1f within the orbit"), Time), Turned <= 4.01f);
		TestTrue(FString::Printf(TEXT("t=%.1f within the rise"), Time), Location.Z >= Base.Z - 0.5f && Location.Z <= Base.Z + 120.5f);
		TestTrue(FString::Printf(TEXT("t=%.1f same distance"), Time), FMath::IsNearlyEqual(FVector2D(Location - LookAt).Size(), PlanDistance, 1.f));
		TestTrue(FString::Printf(TEXT("t=%.1f no roll"), Time), FMath::IsNearlyZero(Rotation.Roll));
	}
	return true;
}

// --------------------------------------------------------------------------
//  Classes and config
// --------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndGameModeTest,
	"OneBraham.FrontEnd.GameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndGameModeTest::RunTest(const FString& Parameters)
{
	const AOneBrahamFrontEndGameMode* Defaults = GetDefault<AOneBrahamFrontEndGameMode>();
	TestNull(TEXT("no pawn in the front end"), Defaults->DefaultPawnClass.Get());
	TestTrue(TEXT("the front-end controller"),
		Defaults->PlayerControllerClass.Get() == AOneBrahamFrontEndPlayerController::StaticClass());

	TestEqual(TEXT("the game instance uses the engine's session name"), UOneBrahamGameInstance::SessionName, FName(NAME_GameSession));
	TestEqual(TEXT("the session subsystem agrees"), UOneBrahamSessionSubsystem::SessionName(), FName(NAME_GameSession));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFrontEndMapsConfigTest,
	"OneBraham.FrontEnd.MapsConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFrontEndMapsConfigTest::RunTest(const FString& Parameters)
{
	const TCHAR* Section = TEXT("/Script/EngineSettings.GameMapsSettings");

	FString GameDefault, ServerDefault, Transition;
	GConfig->GetString(Section, TEXT("GameDefaultMap"), GameDefault, GEngineIni);
	GConfig->GetString(Section, TEXT("ServerDefaultMap"), ServerDefault, GEngineIni);
	GConfig->GetString(Section, TEXT("TransitionMap"), Transition, GEngineIni);

	TestTrue(TEXT("a player starts in the front end"), GameDefault.StartsWith(UOneBrahamGameInstance::MainMenuMapPath()));
	TestTrue(TEXT("a dedicated server starts in the tower"), ServerDefault.Contains(TEXT("L_Persistent_OneBraham")));
	TestTrue(TEXT("travel goes through L_Loading"), Transition.Contains(TEXT("L_Loading")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
