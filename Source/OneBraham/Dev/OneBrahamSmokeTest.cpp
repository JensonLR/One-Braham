// Copyright (c) One Braham. All Rights Reserved.

#include "Dev/OneBrahamSmokeTest.h"

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if !UE_BUILD_SHIPPING
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Core/OneBrahamTypes.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "EnhancedInputSubsystems.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "Extraction/OneBrahamMatchEnding.h"
#include "HAL/IConsoleManager.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "UI/OneBrahamHUDComponent.h"
#include "UI/OneBrahamMatchEndWidget.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputMappingContext.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "OneBraham.h"
#include "UnrealClient.h"
#include "World/OneBrahamSkyDirector.h"

#include <stdio.h>

/** Every smoke line goes through here, so the prefix the script greps is in one place.
 *  A Test build compiles UE_LOG out (bUseLoggingInShipping is off for the game
 *  target, and it governs Test too), so there the lines go straight to stdout:
 *  without them the script would see a run with no verdict. */
#if NO_LOGGING
#define OB_SMOKE_LOG(Format, ...) \
	do \
	{ \
		const FString OBSmokeLine = FString::Printf(TEXT("[OB.SMOKE] " Format), ##__VA_ARGS__); \
		printf("%s\n", TCHAR_TO_UTF8(*OBSmokeLine)); \
		fflush(stdout); \
	} while (0)
#else
#define OB_SMOKE_LOG(Format, ...) \
	UE_LOG(LogOneBraham, Display, TEXT("[OB.SMOKE] " Format), ##__VA_ARGS__)
#endif

namespace
{
	/** After the pawn arrives, how long before anything is measured. The first
	 *  frames after possession are the HUD building and the mapping contexts
	 *  rebuilding (Enhanced Input applies them on the next tick), and a check
	 *  made in that frame reports the race rather than the build. */
	constexpr double SettleSeconds = 2.0;

	/** The frame-time sample window. */
	constexpr double MeasureSeconds = 4.0;

	/** Between screenshots: long enough for the view to settle after a turn
	 *  (temporal AA, auto-exposure and streaming all lag a camera cut). */
	constexpr double ShotIntervalSeconds = 2.0;

	/** The camera turn between shots, so each one faces a different way. */
	constexpr float ShotYawStepDegrees = 90.f;

	/** How long the last screenshot gets to reach the disk. */
	constexpr double FlushSeconds = 6.0;

	/** -OBSmokeExpectPlayers: once the player count is reached, how long to
	 *  wait for every player's pawn to turn up as well. A client only sees the
	 *  pawns relevant to it, and a player between lives has none, so missing
	 *  pawns after this are logged rather than waited on for ever. */
	constexpr double PawnGraceSeconds = 10.0;

	/** The first screenshot's pitch towards another player is clamped to this,
	 *  so a player on the floor above does not make a shot of the ceiling. */
	constexpr float MaxFacingPitchDegrees = 30.f;

	/** -OBSmokeFullMatch: a progress line this often while the match runs. */
	constexpr double ProgressSeconds = 30.0;

	/** After Complete, how long before the end screen is read and photographed:
	 *  the result replicates and the widget fills itself in on its own refresh. */
	constexpr double EndScreenSettleSeconds = 3.0;

	/** After the end screenshot, the post-match window plus this before a run
	 *  that never left the end screen fails. */
	constexpr double TravelGraceSeconds = 45.0;

	/** The squads as the end rules see them, from the replicated squad list. */
	void SmokeGatherStandings(const AOneBrahamGameState* GameState, TArray<FOneBrahamSquadStanding>& Out)
	{
		Out.Reset();
		if (GameState == nullptr)
		{
			return;
		}

		for (const FOneBrahamSquadInfo& Squad : GameState->Squads)
		{
			FOneBrahamSquadStanding& Standing = Out.AddDefaulted_GetRef();
			Standing.SquadId = Squad.SquadId;
			Standing.Members = Squad.Members.Num();
			for (const FOneBrahamSquadMemberInfo& Member : Squad.Members)
			{
				Standing.InFight += (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed) ? 1 : 0;
				Standing.Extracted += Member.LifeState == EOneBrahamLifeState::Extracted ? 1 : 0;
			}
		}
	}

	/** Human players in the game state: bots are not who a LAN test waits for. */
	int32 SmokeCountHumanPlayers(const UWorld* World)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;

		if (GameState == nullptr)
		{
			return 0;
		}

		int32 Humans = 0;
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (PlayerState != nullptr && !PlayerState->IsABot())
			{
				++Humans;
			}
		}

		return Humans;
	}

	/** A player's pawn. APlayerState::GetPawn is set on a client when the pawn's
	 *  PlayerState replicates; the iterator covers a pawn that arrived first. */
	APawn* SmokeFindPawnFor(UWorld* World, const APlayerState* PlayerState)
	{
		if (PlayerState == nullptr)
		{
			return nullptr;
		}

		if (APawn* Pawn = PlayerState->GetPawn())
		{
			return Pawn;
		}

		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (It->GetPlayerState() == PlayerState)
			{
				return *It;
			}
		}

		return nullptr;
	}

	const TCHAR* SmokeNetModeName(ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("standalone");
		case NM_DedicatedServer: return TEXT("dedicated server");
		case NM_ListenServer: return TEXT("listen server");
		case NM_Client: return TEXT("client");
		default: return TEXT("unknown");
		}
	}

	float SmokePercentile(const TArray<float>& Sorted, float Fraction)
	{
		if (Sorted.Num() == 0)
		{
			return 0.f;
		}

		const int32 Index = FMath::Clamp(
			FMath::RoundToInt(Fraction * (Sorted.Num() - 1)), 0, Sorted.Num() - 1);

		return Sorted[Index];
	}
}
#endif // !UE_BUILD_SHIPPING

FOneBrahamSmokeOptions FOneBrahamSmokeOptions::Parse(const TCHAR* CommandLine)
{
	FOneBrahamSmokeOptions Out;

	if (CommandLine == nullptr)
	{
		return Out;
	}

	Out.bEnabled = FParse::Param(CommandLine, TEXT("OBSmoke"));

	int32 Shots = Out.Shots;
	if (FParse::Value(CommandLine, TEXT("OBSmokeShots="), Shots))
	{
		Out.Shots = FMath::Clamp(Shots, 0, MaxShots);
	}

	float Timeout = Out.TimeoutSeconds;
	if (FParse::Value(CommandLine, TEXT("OBSmokeTimeout="), Timeout))
	{
		Out.TimeoutSeconds = FMath::Clamp(Timeout, MinTimeoutSeconds, MaxTimeoutSeconds);
	}

	int32 ExpectPlayers = Out.ExpectPlayers;
	if (FParse::Value(CommandLine, TEXT("OBSmokeExpectPlayers="), ExpectPlayers))
	{
		Out.ExpectPlayers = FMath::Clamp(ExpectPlayers, 0, MaxExpectPlayers);
	}

	float Hold = Out.HoldSeconds;
	if (FParse::Value(CommandLine, TEXT("OBSmokeHold="), Hold))
	{
		Out.HoldSeconds = FMath::Clamp(Hold, 0.f, MaxTimeoutSeconds);
	}

	// A whole match is a long run: the most the budget allows, unless the
	// command line set one.
	Out.bFullMatch = FParse::Param(CommandLine, TEXT("OBSmokeFullMatch"));
	if (Out.bFullMatch && !FParse::Value(CommandLine, TEXT("OBSmokeTimeout="), Timeout))
	{
		Out.TimeoutSeconds = MaxTimeoutSeconds;
	}

	return Out;
}

bool UOneBrahamSmokeTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	// A packaged game or `-game` only: the editor has its own test runner, and a
	// dedicated server has no player to wait for and no view to photograph.
	if (GIsEditor || IsRunningDedicatedServer())
	{
		return false;
	}

	const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);

	if (GameInstance == nullptr || GameInstance->IsDedicatedServerInstance())
	{
		return false;
	}

	return FParse::Param(FCommandLine::Get(), TEXT("OBSmoke"));
#endif
}

void UOneBrahamSmokeTestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if !UE_BUILD_SHIPPING
	Options = FOneBrahamSmokeOptions::Parse(FCommandLine::Get());

	if (!Options.bEnabled)
	{
		return;
	}

	StartSeconds = FPlatformTime::Seconds();
	PhaseStartSeconds = StartSeconds;
	Phase = EPhase::WaitingForPawn;

	// The core ticker rather than a world tick: a world that never finishes
	// loading must still reach the timeout and still quit.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOneBrahamSmokeTestSubsystem::Tick));

	// A client that cannot reach its host only says so through these: the
	// engine then loads the default map standalone and the run carries on.
	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UOneBrahamSmokeTestSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UOneBrahamSmokeTestSubsystem::HandleTravelFailure);
	}

	OB_SMOKE_LOG("start: %d shot(s), %.0fs budget, %d player(s) expected, %.0fs hold%s, build %s",
		Options.Shots, Options.TimeoutSeconds, Options.ExpectPlayers, Options.HoldSeconds,
		Options.bFullMatch ? TEXT(", full match") : TEXT(""),
		LexToString(FApp::GetBuildConfiguration()));
#endif
}

void UOneBrahamSmokeTestSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();

	Super::Deinitialize();
}

bool UOneBrahamSmokeTestSubsystem::Tick(float DeltaTime)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (Phase == EPhase::Done)
	{
		return false;
	}

	// The verdict is in; only the hold is left, and it has its own limit.
	if (Phase == EPhase::Holding)
	{
		return TickHolding();
	}

	const double Now = FPlatformTime::Seconds();

	UWorld* World = GetPlayerWorld();
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;

	// The one budget for the whole run, load included. Whatever phase it ends
	// in is the reason: "timeout waiting for a pawn" is the useful bug report.
	if (Now - StartSeconds > Options.TimeoutSeconds)
	{
		static const TCHAR* PhaseNames[] = { TEXT("a pawn"), TEXT("the players"), TEXT("the frame sample"),
			TEXT("the screenshots"), TEXT("the screenshots to be written"), TEXT("the match to end"),
			TEXT("the end screen"), TEXT("the end screenshot to be written"), TEXT("the post-match travel"),
			TEXT("the next match to clear the end screen"), TEXT("nothing"), TEXT("nothing") };
		static_assert(UE_ARRAY_COUNT(PhaseNames) == static_cast<int32>(EPhase::Done) + 1, "one name per phase");

		FString Waiting = PhaseNames[static_cast<uint8>(Phase)];

		if (Phase == EPhase::WaitingForPlayers)
		{
			Waiting = FString::Printf(TEXT("%d players, have %d"), Options.ExpectPlayers, SmokeCountHumanPlayers(World));
		}
		else if (Phase == EPhase::WaitingForPawn && Options.ExpectPlayers > 0)
		{
			// In a shared match "no pawn" has two very different causes: never
			// connected, or connected and never spawned. Say which.
			Waiting += FString::Printf(TEXT(" (%s)"), *DescribeNet(World));
		}

		Fail(FString::Printf(TEXT("timeout after %.0fs waiting for %s"), Options.TimeoutSeconds, *Waiting));
		Finish();
		return Phase != EPhase::Done;
	}

	switch (Phase)
	{
	case EPhase::WaitingForPawn:
		if (World != nullptr && PC != nullptr && PC->GetPawn() != nullptr)
		{
			OB_SMOKE_LOG("possessed after %.1fs", Now - StartSeconds);
			Phase = EPhase::Measuring;
			PhaseStartSeconds = Now;
			LastFrameSeconds = 0.0;
			FrameTimesMs.Reset();

			// A second possession re-runs the checks; only the last pawn counts.
			Failures.Reset();
			bPlayersChecked = false;
		}
		break;

	case EPhase::WaitingForPlayers:
		if (World == nullptr || PC == nullptr || PC->GetPawn() == nullptr)
		{
			OB_SMOKE_LOG("lost the pawn after %.1fs - waiting again", Now - StartSeconds);
			Phase = EPhase::WaitingForPawn;
			PhaseStartSeconds = Now;
			break;
		}

		if (ArePlayersReady(World))
		{
			RunPlayerChecks(World, PC);
			bPlayersChecked = true;

			// Straight back into the frame sample: the settle has been had.
			Phase = EPhase::Measuring;
			PhaseStartSeconds = Now - SettleSeconds;
			LastFrameSeconds = Now;
		}
		break;

	case EPhase::Measuring:
		// The pawn can be lost to a map change or a respawn while settling; the
		// checks would then report a world nobody is standing in, so go back
		// to waiting (the overall budget still applies).
		if (World == nullptr || PC == nullptr || PC->GetPawn() == nullptr)
		{
			OB_SMOKE_LOG("lost the pawn after %.1fs - waiting again", Now - StartSeconds);
			Phase = EPhase::WaitingForPawn;
			PhaseStartSeconds = Now;
			break;
		}

		if (Now - PhaseStartSeconds < SettleSeconds)
		{
			break;
		}

		if (LastFrameSeconds == 0.0)
		{
			RunChecks(World, PC);
			LastFrameSeconds = Now;

			if (Options.ExpectPlayers > 0 && !bPlayersChecked)
			{
				OB_SMOKE_LOG("players: waiting for %d (have %d)", Options.ExpectPlayers, SmokeCountHumanPlayers(World));
				Phase = EPhase::WaitingForPlayers;
				PhaseStartSeconds = Now;
				PlayersReachedSeconds = 0.0;
			}
			break;
		}

		TickMeasuring();
		break;

	case EPhase::Shooting:
		if (PC == nullptr)
		{
			Fail(TEXT("lost the player controller while taking screenshots"));
			Finish();
			return Phase != EPhase::Done;
		}

		TickShooting(PC);
		break;

	case EPhase::Flushing:
		TickFlushing();
		break;

	case EPhase::WatchingMatch:
		TickWatchingMatch(World);
		break;

	case EPhase::EndScreen:
		TickEndScreen(World, PC);
		break;

	case EPhase::EndFlushing:
		TickEndFlushing();
		break;

	case EPhase::AwaitingTravel:
		TickAwaitingTravel();
		break;

	case EPhase::NextMatch:
		TickNextMatch();
		break;

	default:
		break;
	}

	return Phase != EPhase::Done;
#endif
}

// The private helpers below exist only outside Shipping. Nothing references
// them in Shipping (Tick returns before the switch), so they need no stubs.
#if !UE_BUILD_SHIPPING

UWorld* UOneBrahamSmokeTestSubsystem::GetPlayerWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;

	return (World != nullptr && World->IsGameWorld() && World->HasBegunPlay()) ? World : nullptr;
}

void UOneBrahamSmokeTestSubsystem::Fail(const FString& Reason)
{
	Failures.Add(Reason);
}

void UOneBrahamSmokeTestSubsystem::RunChecks(UWorld* World, APlayerController* PC)
{
	// map ------------------------------------------------------------------
	OB_SMOKE_LOG("map: %s (%s), net mode %d", *World->GetMapName(), *World->GetOutermost()->GetName(),
		static_cast<int32>(World->GetNetMode()));

	// pawn -----------------------------------------------------------------
	const APawn* Pawn = PC->GetPawn();
	OB_SMOKE_LOG("pawn: %s at %s", *Pawn->GetClass()->GetName(), *Pawn->GetActorLocation().ToCompactString());

	// input config ---------------------------------------------------------
	const AOneBrahamPlayerController* OBController = Cast<AOneBrahamPlayerController>(PC);

	if (OBController == nullptr)
	{
		OB_SMOKE_LOG("input: controller is %s, not AOneBrahamPlayerController", *PC->GetClass()->GetName());
		Fail(FString::Printf(TEXT("controller is %s"), *PC->GetClass()->GetName()));
	}
	else if (OBController->InputConfig == nullptr)
	{
		OB_SMOKE_LOG("input: InputConfig is NOT set");
		Fail(TEXT("InputConfig not set"));
	}
	else
	{
		OB_SMOKE_LOG("input: InputConfig %s", *OBController->InputConfig->GetPathName());
	}

	// mapping contexts and the key mappings they produced ------------------
	// The live mapping list is what a key press is matched against, so it is
	// the number that says whether the controls work, whatever contexts the
	// controller believes it pushed. The applied-context map itself is not
	// public, so the contexts are the controller's three, asked by name.
	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer)
		: nullptr;
	const UEnhancedPlayerInput* PlayerInput = InputSubsystem ? InputSubsystem->GetPlayerInput() : nullptr;

	if (PlayerInput == nullptr)
	{
		OB_SMOKE_LOG("mappings: no Enhanced Input player input");
		Fail(TEXT("no Enhanced Input player input"));
	}
	else
	{
		TArray<FString> ContextNames;

		if (OBController != nullptr)
		{
			const UInputMappingContext* Contexts[] = { OBController->CommonMappingContext,
				OBController->GameplayMappingContext, OBController->SpectatorMappingContext };

			for (const UInputMappingContext* Context : Contexts)
			{
				int32 Priority = 0;
				if (Context != nullptr && InputSubsystem->HasMappingContext(Context, Priority))
				{
					ContextNames.Add(FString::Printf(TEXT("%s@%d"), *Context->GetName(), Priority));
				}
			}
		}

		const TConstArrayView<const FEnhancedActionKeyMapping> Mappings = PlayerInput->GetEnhancedActionMappingsView();

		TSet<const UInputAction*> Actions;
		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			Actions.Add(Mapping.Action);
		}

		OB_SMOKE_LOG("mappings: %d key mapping(s) over %d action(s); controller contexts installed [%s]",
			Mappings.Num(), Actions.Num(), *FString::Join(ContextNames, TEXT(", ")));

		if (OBController != nullptr && ContextNames.Num() == 0)
		{
			Fail(TEXT("none of the controller's mapping contexts is installed"));
		}

		if (Mappings.Num() == 0)
		{
			Fail(TEXT("no key mappings installed"));
		}
	}

	// match settings -------------------------------------------------------
	const AOneBrahamGameState* GameState = World->GetGameState<AOneBrahamGameState>();
	const UOneBrahamMatchSettingsAsset* MatchSettings = GameState ? GameState->GetMatchSettings() : nullptr;

	if (GameState == nullptr)
	{
		OB_SMOKE_LOG("match: no AOneBrahamGameState");
		Fail(TEXT("no AOneBrahamGameState"));
	}
	else if (MatchSettings == nullptr)
	{
		OB_SMOKE_LOG("match: match settings NOT set");
		Fail(TEXT("match settings not set"));
	}
	else
	{
		OB_SMOKE_LOG("match: %s, mode %s", *MatchSettings->GetName(),
			*StaticEnum<EOneBrahamMatchMode>()->GetNameStringByValue(static_cast<int64>(MatchSettings->Mode)));
	}

	// sky director ---------------------------------------------------------
	int32 SkyDirectors = 0;
	for (TActorIterator<AOneBrahamSkyDirector> It(World); It; ++It)
	{
		++SkyDirectors;
	}

	OB_SMOKE_LOG("sky: %d sky director(s)", SkyDirectors);

	if (SkyDirectors == 0)
	{
		Fail(TEXT("no sky director in the world"));
	}
}

bool UOneBrahamSmokeTestSubsystem::ArePlayersReady(UWorld* World)
{
	const AGameStateBase* GameState = World->GetGameState();
	const int32 Humans = SmokeCountHumanPlayers(World);

	if (GameState == nullptr || Humans < Options.ExpectPlayers)
	{
		PlayersReachedSeconds = 0.0;
		return false;
	}

	const double Now = FPlatformTime::Seconds();

	if (PlayersReachedSeconds == 0.0)
	{
		PlayersReachedSeconds = Now;
		OB_SMOKE_LOG("players: %d in after %.1fs", Humans, Now - StartSeconds);
	}

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState != nullptr && !PlayerState->IsABot() && SmokeFindPawnFor(World, PlayerState) == nullptr)
		{
			return Now - PlayersReachedSeconds >= PawnGraceSeconds;
		}
	}

	return true;
}

void UOneBrahamSmokeTestSubsystem::RunPlayerChecks(UWorld* World, APlayerController* PC)
{
	const ENetMode NetMode = World->GetNetMode();
	const bool bAuthority = NetMode != NM_Client;
	const AGameStateBase* GameState = World->GetGameState();
	const int32 Humans = SmokeCountHumanPlayers(World);

	OB_SMOKE_LOG("players: %d human(s) of %d expected, %d player state(s), as %s", Humans, Options.ExpectPlayers,
		GameState ? GameState->PlayerArray.Num() : 0, SmokeNetModeName(NetMode));

	if (Humans < Options.ExpectPlayers)
	{
		Fail(FString::Printf(TEXT("%d of %d players"), Humans, Options.ExpectPlayers));
	}

	// players -------------------------------------------------------------
	if (GameState != nullptr)
	{
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (PlayerState == nullptr)
			{
				continue;
			}

			const AOneBrahamPlayerState* OBState = Cast<AOneBrahamPlayerState>(PlayerState);
			const FString Team = OBState
				? StaticEnum<EOneBrahamTeam>()->GetNameStringByValue(static_cast<int64>(OBState->Team))
				: FString(TEXT("n/a"));
			const int32 Squad = OBState ? OBState->SquadId : INDEX_NONE;
			const APawn* Pawn = SmokeFindPawnFor(World, PlayerState);

			OB_SMOKE_LOG("player: '%s'%s%s team %s, squad %d, pawn %s at %s, ping %.0f ms",
				*PlayerState->GetPlayerName(),
				PlayerState == PC->PlayerState ? TEXT(" (local)") : TEXT(""),
				PlayerState->IsABot() ? TEXT(" (bot)") : TEXT(""),
				*Team, Squad,
				Pawn ? *Pawn->GetName() : TEXT("none"),
				Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("-"),
				PlayerState->GetPingInMilliseconds());

			// The server spawns everyone, so a human without a pawn there is a
			// real fault. A client only replicates the pawns relevant to it.
			if (Pawn == nullptr && bAuthority && !PlayerState->IsABot() && !PlayerState->IsOnlyASpectator())
			{
				Fail(FString::Printf(TEXT("player '%s' has no pawn on the server"), *PlayerState->GetPlayerName()));
			}
		}
	}

	// net -----------------------------------------------------------------
	const UNetDriver* Driver = World->GetNetDriver();

	if (NetMode == NM_Client)
	{
		UNetConnection* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;

		if (Connection == nullptr)
		{
			OB_SMOKE_LOG("net: client with no server connection");
			Fail(TEXT("no server connection"));
		}
		else
		{
			OB_SMOKE_LOG("net: client via %s to %s, %s, avg lag %.0f ms, ping %.0f ms",
				*Driver->GetClass()->GetName(), *Connection->LowLevelGetRemoteAddress(/*bAppendPort=*/true),
				LexToString(Connection->GetConnectionState()), Connection->AvgLag * 1000.f,
				PC->PlayerState ? PC->PlayerState->GetPingInMilliseconds() : -1.f);

			if (Connection->GetConnectionState() != USOCK_Open)
			{
				Fail(FString::Printf(TEXT("server connection is %s"), LexToString(Connection->GetConnectionState())));
			}
		}
	}
	else if (Driver != nullptr)
	{
		OB_SMOKE_LOG("net: %s via %s, %d client connection(s)", SmokeNetModeName(NetMode),
			*Driver->GetClass()->GetName(), Driver->ClientConnections.Num());

		for (UNetConnection* Connection : Driver->ClientConnections)
		{
			if (Connection != nullptr)
			{
				OB_SMOKE_LOG("net: client %s, %s, avg lag %.0f ms",
					*Connection->LowLevelGetRemoteAddress(/*bAppendPort=*/true),
					LexToString(Connection->GetConnectionState()), Connection->AvgLag * 1000.f);
			}
		}

		// A listen server is one of the players itself; a dedicated one is not.
		const int32 RemoteExpected = Options.ExpectPlayers - (NetMode == NM_ListenServer ? 1 : 0);

		if (Driver->ClientConnections.Num() < RemoteExpected)
		{
			Fail(FString::Printf(TEXT("%d client connection(s), expected %d"), Driver->ClientConnections.Num(), RemoteExpected));
		}
	}
	else
	{
		// Every multiplayer run needs a driver, a one-player one included: a
		// client whose connection failed lands on the default map (the tower)
		// standalone, possesses a pawn there, and counts one human - itself.
		OB_SMOKE_LOG("net: %s, no net driver", SmokeNetModeName(NetMode));
		Fail(FString::Printf(TEXT("%s with no net driver - a host needs ?listen, and a client that could not connect falls back to the default map"),
			SmokeNetModeName(NetMode)));
	}
}

FString UOneBrahamSmokeTestSubsystem::DescribeNet(UWorld* World) const
{
	if (World == nullptr)
	{
		return TEXT("no game world yet");
	}

	const ENetMode NetMode = World->GetNetMode();
	const UNetDriver* Driver = World->GetNetDriver();
	const AGameStateBase* GameState = World->GetGameState();
	const int32 PlayerStates = GameState ? GameState->PlayerArray.Num() : 0;

	if (NetMode == NM_Client)
	{
		UNetConnection* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;

		if (Connection == nullptr)
		{
			return TEXT("client with no server connection");
		}

		return FString::Printf(TEXT("client of %s (%s), %d player state(s)"),
			*Connection->LowLevelGetRemoteAddress(/*bAppendPort=*/true),
			LexToString(Connection->GetConnectionState()), PlayerStates);
	}

	return FString::Printf(TEXT("%s, %d client connection(s), %d player state(s)"), SmokeNetModeName(NetMode),
		Driver ? Driver->ClientConnections.Num() : 0, PlayerStates);
}

void UOneBrahamSmokeTestSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& Error)
{
	// After the verdict a dropped connection is the other side leaving.
	if (Phase == EPhase::Done || Phase == EPhase::Holding)
	{
		return;
	}

	// The full engine text on the line; the type alone on the FAIL line (the
	// text runs to a whole connection dump).
	OB_SMOKE_LOG("net: network failure %s: %s", ENetworkFailure::ToString(FailureType), *Error);
	NetFailures.AddUnique(FString::Printf(TEXT("network failure %s"), ENetworkFailure::ToString(FailureType)));
}

void UOneBrahamSmokeTestSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if (Phase == EPhase::Done || Phase == EPhase::Holding)
	{
		return;
	}

	OB_SMOKE_LOG("net: travel failure %s: %s", ETravelFailure::ToString(FailureType), *Error);
	NetFailures.AddUnique(FString::Printf(TEXT("travel failure %s"), ETravelFailure::ToString(FailureType)));
}

const APawn* UOneBrahamSmokeTestSubsystem::FindNearestOtherPawn(UWorld* World, APlayerController* PC) const
{
	const APawn* Own = PC->GetPawn();
	const FVector From = Own ? Own->GetActorLocation() : PC->GetFocalLocation();

	const APawn* Nearest = nullptr;
	double NearestDistSq = TNumericLimits<double>::Max();

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Pawn = *It;

		// Other humans only: a bot's pawn carries a player state too, and the
		// shot is meant to show the other process's player.
		const APlayerState* PlayerState = Pawn->GetPlayerState();
		if (Pawn == Own || PlayerState == nullptr || PlayerState->IsABot())
		{
			continue;
		}

		const double DistSq = FVector::DistSquared(From, Pawn->GetActorLocation());

		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Pawn;
		}
	}

	return Nearest;
}

void UOneBrahamSmokeTestSubsystem::TickMeasuring()
{
	// Wall clock between core ticks, which is one per frame: the tick's own
	// DeltaTime is smoothed and clamped by the engine, and a hitch is exactly
	// what this should see.
	const double Now = FPlatformTime::Seconds();
	FrameTimesMs.Add(static_cast<float>((Now - LastFrameSeconds) * 1000.0));
	LastFrameSeconds = Now;

	if (Now - PhaseStartSeconds < SettleSeconds + MeasureSeconds)
	{
		return;
	}

	TArray<float> Sorted = FrameTimesMs;
	Sorted.Sort();

	float Sum = 0.f;
	for (const float Sample : Sorted)
	{
		Sum += Sample;
	}

	const float MeanMs = Sorted.Num() > 0 ? Sum / static_cast<float>(Sorted.Num()) : 0.f;

	// Logged, never failed on: the number belongs to the machine as much as
	// to the build. ob.Bench.Skydeck is the measurement worth comparing.
	OB_SMOKE_LOG("frames: %d over %.0fs, mean %.2f ms (%.0f fps), p95 %.2f ms, worst %.2f ms",
		Sorted.Num(), MeasureSeconds, MeanMs, MeanMs > 0.f ? 1000.f / MeanMs : 0.f,
		SmokePercentile(Sorted, 0.95f), Sorted.Num() > 0 ? Sorted.Last() : 0.f);

	if (Sorted.Num() == 0)
	{
		Fail(TEXT("no frames rendered in the sample window"));
	}

	Phase = EPhase::Shooting;
	PhaseStartSeconds = Now;
	LastShotSeconds = Now;
	TurnedForShot = INDEX_NONE;

	BasePitch = 0.f;

	if (UWorld* World = GetPlayerWorld())
	{
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World))
		{
			BaseYaw = PC->GetControlRotation().Yaw;

			// In a shared match the first shot is of the other player: a shot
			// from each side is the proof a log line cannot give that the two
			// games render the same match.
			if (Options.ExpectPlayers > 1)
			{
				if (const APawn* Other = FindNearestOtherPawn(World, PC))
				{
					FVector EyeLocation;
					FRotator EyeRotation;
					PC->GetPlayerViewPoint(EyeLocation, EyeRotation);

					const FVector ToOther = Other->GetActorLocation() - EyeLocation;
					const FRotator Towards = ToOther.Rotation();

					BaseYaw = Towards.Yaw;
					BasePitch = FMath::Clamp(FRotator::NormalizeAxis(Towards.Pitch), -MaxFacingPitchDegrees, MaxFacingPitchDegrees);

					// Whether the shot can show them at all: squads spawn apart,
					// so a wall between the two is the usual reason it does not.
					FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(OBSmokeFacing), /*bTraceComplex=*/false, PC->GetPawn());
					TraceParams.AddIgnoredActor(Other);
					FHitResult Hit;
					const bool bBlocked = World->LineTraceSingleByChannel(
						Hit, EyeLocation, Other->GetActorLocation(), ECC_Visibility, TraceParams);
					const AActor* Blocker = Hit.GetActor();

					const APlayerState* OtherState = Other->GetPlayerState();
					OB_SMOKE_LOG("facing: '%s' %.1f m away, yaw %.0f pitch %.0f, line of sight %s",
						OtherState ? *OtherState->GetPlayerName() : TEXT("?"), ToOther.Size() / 100.f, BaseYaw, BasePitch,
						bBlocked
							? *FString::Printf(TEXT("blocked by %s at %.1f m"), Blocker ? *Blocker->GetName() : TEXT("world geometry"), Hit.Distance / 100.f)
							: TEXT("clear"));
				}
				else
				{
					OB_SMOKE_LOG("facing: no other human player's pawn is known here - shots keep the spawn facing");
				}
			}
		}
	}
}

void UOneBrahamSmokeTestSubsystem::TickShooting(APlayerController* PC)
{
	// A request is captured by the next frame the viewport draws. Until it has
	// been, the camera must not move, or the shot is of the next direction.
	if (FScreenshotRequest::IsScreenshotRequested())
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 Taken = ShotPaths.Num();

	if (Taken >= Options.Shots)
	{
		Phase = EPhase::Flushing;
		PhaseStartSeconds = Now;
		return;
	}

	// Turn first, then wait the interval, then shoot: the turn for shot N is
	// made after shot N-1 has been captured.
	// Only the first shot keeps the pitch: it is the one aimed at a player.
	const FRotator Facing(Taken == 0 ? BasePitch : 0.f, FRotator::NormalizeAxis(BaseYaw + ShotYawStepDegrees * Taken), 0.f);

	if (TurnedForShot != Taken)
	{
		PC->SetControlRotation(Facing);
		TurnedForShot = Taken;
		LastShotSeconds = Now;
		return;
	}

	if (Now - LastShotSeconds < ShotIntervalSeconds)
	{
		return;
	}

	// A host and a client on one machine share the screenshot folder, and can
	// start shooting in the same second: the side goes in the name.
	static const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const ENetMode NetMode = PC->GetWorld() ? PC->GetWorld()->GetNetMode() : NM_Standalone;
	const TCHAR* Side = NetMode == NM_Client ? TEXT("_client") : NetMode == NM_ListenServer ? TEXT("_host") : TEXT("");
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::ScreenShotDir() / FString::Printf(TEXT("OBSmoke_%s%s_%02d.png"), *Stamp, Side, Taken + 1));

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);

	// With the HUD: the point is what a player sees, and a missing HUD is a
	// smoke-test finding in its own right.
	FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI=*/true, /*bAddUniqueSuffix=*/false);

	ShotPaths.Add(Path);
	LastShotSeconds = Now;

	OB_SMOKE_LOG("shot %d/%d: yaw %.0f -> %s", Taken + 1, Options.Shots, Facing.Yaw, *Path);
}

void UOneBrahamSmokeTestSubsystem::TickFlushing()
{
	const double Now = FPlatformTime::Seconds();

	int32 Missing = 0;
	for (const FString& Path : ShotPaths)
	{
		if (!IFileManager::Get().FileExists(*Path))
		{
			++Missing;
		}
	}

	if (Missing > 0 && Now - PhaseStartSeconds < FlushSeconds)
	{
		return;
	}

	for (const FString& Path : ShotPaths)
	{
		if (!IFileManager::Get().FileExists(*Path))
		{
			Fail(FString::Printf(TEXT("screenshot not written: %s"), *FPaths::GetCleanFilename(Path)));
		}
	}

	if (ShotPaths.Num() > 0)
	{
		OB_SMOKE_LOG("screenshots: %d of %d written to %s", ShotPaths.Num() - Missing, ShotPaths.Num(),
			*FPaths::GetPath(ShotPaths[0]));
	}

	if (Options.bFullMatch)
	{
		UWorld* World = GetPlayerWorld();
		APlayerController* PC = World != nullptr && GetGameInstance() != nullptr ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
		BeginWatchingMatch(World, PC);
		return;
	}

	Finish();
}

// ==========================================================================
//  -OBSmokeFullMatch
// ==========================================================================
void UOneBrahamSmokeTestSubsystem::BeginWatchingMatch(UWorld* World, APlayerController* PC)
{
	if (World == nullptr)
	{
		Fail(TEXT("no game world to watch a match in"));
		Finish();
		return;
	}

	MatchWorld = World;
	MatchMapName = World->GetMapName();
	LastProgressSeconds = FPlatformTime::Seconds();
	Phase = EPhase::WatchingMatch;
	PhaseStartSeconds = LastProgressSeconds;

	// The player's own death would decide the match ("staff withdrawn"), so on
	// the server they leave the fight and a bot takes their place. A client
	// cannot do that, and waits for the server's match to end as it stands.
	if (AOneBrahamGameMode* GameMode = World->GetAuthGameMode<AOneBrahamGameMode>())
	{
		GameMode->DevWithdrawToSpectator(PC);
		OB_SMOKE_LOG("full match: the local player is watching; the bots play it out");
	}
	else
	{
		OB_SMOKE_LOG("full match: a client - watching the server's match as a player");
	}
}

void UOneBrahamSmokeTestSubsystem::TickWatchingMatch(UWorld* World)
{
	if (World == nullptr)
	{
		// Mid-travel or between maps: the match world went away before its end.
		if (!MatchWorld.IsValid())
		{
			Fail(TEXT("the match's world went away before the match ended"));
			Finish();
		}
		return;
	}

	if (World != MatchWorld.Get())
	{
		Fail(FString::Printf(TEXT("travelled to %s before the match ended"), *World->GetMapName()));
		Finish();
		return;
	}

	const AOneBrahamGameState* GameState = World->GetGameState<AOneBrahamGameState>();
	if (GameState == nullptr)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();

	if (GameState->MatchPhase == EOneBrahamMatchPhase::Complete || GameState->MatchPhase == EOneBrahamMatchPhase::PostMatch)
	{
		OB_SMOKE_LOG("match: complete after %.0fs of watching", Now - PhaseStartSeconds);
		Phase = EPhase::EndScreen;
		PhaseStartSeconds = Now;
		return;
	}

	if (Now - LastProgressSeconds < ProgressSeconds)
	{
		return;
	}
	LastProgressSeconds = Now;

	TArray<FOneBrahamSquadStanding> Standings;
	SmokeGatherStandings(GameState, Standings);
	FString Squads;
	for (const FOneBrahamSquadStanding& Standing : Standings)
	{
		Squads += FString::Printf(TEXT("%s%d:%d/%d"), Squads.IsEmpty() ? TEXT("") : TEXT(" "), Standing.SquadId, Standing.InFight, Standing.Members);
	}

	const UOneBrahamLockdownComponent* Lockdown = GameState->GetLockdownComponent();
	const UOneBrahamExtractionComponent* Extraction = GameState->GetExtractionComponent();

	OB_SMOKE_LOG("progress: %s %.0f of %.0fs, lockdown %s (highest floor %d), winch %s (squad %d, %.0f%%), squads in fight [%s], scores [%s]",
		*StaticEnum<EOneBrahamMatchPhase>()->GetNameStringByValue(static_cast<int64>(GameState->MatchPhase)),
		GameState->PhaseElapsedSeconds, GameState->MatchTimeLimitSeconds,
		Lockdown != nullptr ? *StaticEnum<EOneBrahamLockdownStage>()->GetNameStringByValue(static_cast<int64>(Lockdown->GetStage())) : TEXT("-"),
		Lockdown != nullptr ? Lockdown->GetHighestOccupiedFloor() : -1,
		Extraction != nullptr ? *StaticEnum<EOneBrahamExtractionState>()->GetNameStringByValue(static_cast<int64>(Extraction->GetExtractionState())) : TEXT("-"),
		Extraction != nullptr ? Extraction->GetClaimingSquadId() : -1,
		Extraction != nullptr ? Extraction->GetHoldProgress01() * 100.f : 0.f,
		*Squads,
		*FString::JoinBy(GameState->TeamScores, TEXT(","), [](int32 Score) { return FString::FromInt(Score); }));
}

void UOneBrahamSmokeTestSubsystem::TickEndScreen(UWorld* World, APlayerController* PC)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - PhaseStartSeconds < EndScreenSettleSeconds)
	{
		return;
	}

	const AOneBrahamGameState* GameState = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GameState == nullptr || PC == nullptr)
	{
		Fail(TEXT("no game state or player at the end of the match"));
		Finish();
		return;
	}

	// result ---------------------------------------------------------------
	const FOneBrahamMatchResult& Result = GameState->MatchResult;
	const UOneBrahamExtractionComponent* Extraction = GameState->GetExtractionComponent();
	const EOneBrahamMatchEnding Ending = Extraction != nullptr ? Extraction->GetMatchEnding() : EOneBrahamMatchEnding::None;
	const int32 WinningSquadId = Extraction != nullptr ? Extraction->GetEndingWinningSquadId() : INDEX_NONE;

	OB_SMOKE_LOG("result: ending '%s', winner %s (squad %d, %s), duration %.0fs, extracted %d, mode %s, scores [%s]",
		*OneBrahamMatchEnd::LogNameFor(Ending),
		Result.WinningSquadName.IsEmpty() ? TEXT("nobody") : *Result.WinningSquadName.ToString(), WinningSquadId,
		*StaticEnum<EOneBrahamTeam>()->GetNameStringByValue(static_cast<int64>(Result.WinningTeam)),
		Result.MatchDurationSeconds, Result.ExtractedCount,
		*StaticEnum<EOneBrahamMatchMode>()->GetNameStringByValue(static_cast<int64>(GameState->MatchMode)),
		*FString::JoinBy(GameState->TeamScores, TEXT(","), [](int32 Score) { return FString::FromInt(Score); }));

	TArray<FOneBrahamSquadStanding> Standings;
	SmokeGatherStandings(GameState, Standings);
	for (const FOneBrahamSquadStanding& Standing : Standings)
	{
		const FText Name = GameState->Squads.IsValidIndex(Standing.SquadId) ? GameState->Squads[Standing.SquadId].SquadName : FText::GetEmpty();
		int32 Kills = 0;
		int32 Downs = 0;
		for (const FOneBrahamPlayerSummary& Summary : Result.PlayerSummaries)
		{
			if (GameState->Squads.IsValidIndex(Standing.SquadId) && Summary.Team == GameState->Squads[Standing.SquadId].Team)
			{
				Kills += Summary.Kills;
				Downs += Summary.Downs;
			}
		}

		OB_SMOKE_LOG("squad %d '%s': %d member(s), %d alive, %d extracted, %d kill(s), %d down(s)%s",
			Standing.SquadId, *Name.ToString(), Standing.Members, Standing.InFight, Standing.Extracted, Kills, Downs,
			Standing.SquadId == WinningSquadId ? TEXT(" - WINNER") : TEXT(""));
	}

	// coherent -------------------------------------------------------------
	int32 ScoreToWin = 200;
	if (const UOneBrahamMatchSettingsAsset* Settings = GameState->GetMatchSettings())
	{
		ScoreToWin = Settings->ScoreToWin;
	}
	if (const IConsoleVariable* Override = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Match.ScoreToWin")))
	{
		ScoreToWin = Override->GetInt() > 0 ? Override->GetInt() : ScoreToWin;
	}

	FString Problem;
	if (OneBrahamMatchEnd::IsResultCoherent(Ending, GameState->MatchMode, WinningSquadId, Result.ExtractedCount,
		Standings, GameState->TeamScores, ScoreToWin, Problem))
	{
		OB_SMOKE_LOG("coherent: yes");
	}
	else
	{
		OB_SMOKE_LOG("coherent: NO - %s", *Problem);
		Fail(FString::Printf(TEXT("incoherent result: %s"), *Problem));
	}

	if (Result.MatchDurationSeconds <= 0.f)
	{
		Fail(TEXT("the result has no duration"));
	}

	// end screen -------------------------------------------------------------
	const UOneBrahamHUDComponent* HUD = PC->FindComponentByClass<UOneBrahamHUDComponent>();
	const UOneBrahamMatchEndWidget* EndScreen = HUD != nullptr ? HUD->GetMatchEndWidget() : nullptr;
	if (EndScreen == nullptr || !EndScreen->IsShowing())
	{
		OB_SMOKE_LOG("end screen: NOT up");
		Fail(TEXT("the end screen is not up"));
	}
	else
	{
		OB_SMOKE_LOG("end screen: up, showing '%s'", *OneBrahamMatchEnd::LogNameFor(EndScreen->GetShownEnding()));
		if (EndScreen->GetShownEnding() != Ending)
		{
			Fail(TEXT("the end screen shows a different ending from the server's"));
		}
	}

	// shot ----------------------------------------------------------------------
	static const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const ENetMode NetMode = World->GetNetMode();
	const TCHAR* Side = NetMode == NM_Client ? TEXT("_client") : NetMode == NM_ListenServer ? TEXT("_host") : TEXT("");
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::ScreenShotDir() / FString::Printf(TEXT("OBSmoke_%s%s_end.png"), *Stamp, Side));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI=*/true, /*bAddUniqueSuffix=*/false);
	ShotPaths.Add(Path);
	OB_SMOKE_LOG("shot end -> %s", *Path);

	// How long to wait for the travel: the post-match window as the server set
	// it, and a grace on top.
	const double LeaveIn = Extraction != nullptr && Extraction->GetLeaveServerTime() > 0.0
		? Extraction->GetLeaveServerTime() - GameState->GetServerWorldTimeSeconds() : 60.0;
	TravelDeadlineSeconds = Now + FMath::Max(0.0, LeaveIn) + TravelGraceSeconds;
	MatchGameState = const_cast<AOneBrahamGameState*>(GameState);

	Phase = EPhase::EndFlushing;
	PhaseStartSeconds = Now;
}

void UOneBrahamSmokeTestSubsystem::TickEndFlushing()
{
	const double Now = FPlatformTime::Seconds();
	const FString& Path = ShotPaths.Last();

	if (!IFileManager::Get().FileExists(*Path) && Now - PhaseStartSeconds < FlushSeconds)
	{
		return;
	}

	if (!IFileManager::Get().FileExists(*Path))
	{
		Fail(FString::Printf(TEXT("end screenshot not written: %s"), *FPaths::GetCleanFilename(Path)));
	}

	OB_SMOKE_LOG("after: waiting up to %.0fs for the post-match travel", TravelDeadlineSeconds - Now);
	Phase = EPhase::AwaitingTravel;
	PhaseStartSeconds = Now;
}

void UOneBrahamSmokeTestSubsystem::TickAwaitingTravel()
{
	const double Now = FPlatformTime::Seconds();
	UWorld* World = GetPlayerWorld();

	// A new world that has begun play (the menu, or the map again), or the
	// same world with a new match in it (a seamless restart of the same map).
	const AOneBrahamGameState* GameState = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	const bool bNewMatch = GameState != nullptr && (GameState != MatchGameState.Get()
		|| GameState->MatchPhase == EOneBrahamMatchPhase::Warmup || GameState->MatchPhase == EOneBrahamMatchPhase::Active);
	if (World != nullptr && (World != MatchWorld.Get() || bNewMatch))
	{
		const bool bSameMap = World->GetMapName() == MatchMapName;
		OB_SMOKE_LOG("after: travelled to %s %.0fs after the end screen (%s)", *World->GetMapName(), Now - PhaseStartSeconds,
			bSameMap ? TEXT("the map reloaded") : TEXT("a different map"));

		// The menu is another lane's to check. The same map again is a next
		// match, which must not be played behind the last one's results.
		if (!bSameMap)
		{
			Finish();
			return;
		}

		Phase = EPhase::NextMatch;
		PhaseStartSeconds = Now;
		return;
	}

	if (Now > TravelDeadlineSeconds)
	{
		Fail(TEXT("nobody left the end screen: no post-match travel"));
		Finish();
	}
}

void UOneBrahamSmokeTestSubsystem::TickNextMatch()
{
	const double Now = FPlatformTime::Seconds();
	UWorld* World = GetPlayerWorld();
	APlayerController* PC = World != nullptr && GetGameInstance() != nullptr ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
	const AOneBrahamGameState* GameState = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	const UOneBrahamHUDComponent* HUD = PC != nullptr ? PC->FindComponentByClass<UOneBrahamHUDComponent>() : nullptr;
	const bool bRunning = GameState != nullptr
		&& (GameState->MatchPhase == EOneBrahamMatchPhase::Warmup || GameState->MatchPhase == EOneBrahamMatchPhase::Active);

	// A frame or two for the HUD component to notice the new game state.
	if (bRunning && HUD != nullptr && Now - PhaseStartSeconds > 2.0)
	{
		const UOneBrahamMatchEndWidget* EndScreen = HUD->GetMatchEndWidget();
		const bool bEndScreenGone = EndScreen == nullptr || !EndScreen->IsInViewport();
		UGameViewportClient* Viewport = World->GetGameViewport();
		const bool bGameInput = Viewport != nullptr && !Viewport->IgnoreInput();

		OB_SMOKE_LOG("next match: %s, end screen %s, game input %s, HUD %s",
			*StaticEnum<EOneBrahamMatchPhase>()->GetNameStringByValue(static_cast<int64>(GameState->MatchPhase)),
			bEndScreenGone ? TEXT("gone") : TEXT("STILL UP"), bGameInput ? TEXT("yes") : TEXT("NO"),
			HUD->IsHUDVisible() ? TEXT("visible") : TEXT("hidden"));

		if (!bEndScreenGone)
		{
			Fail(TEXT("the next match began behind the last one's end screen"));
		}
		if (!bGameInput)
		{
			Fail(TEXT("the next match began with the game's input still off"));
		}
		Finish();
		return;
	}

	if (Now - PhaseStartSeconds > 90.0)
	{
		Fail(TEXT("the next match never began after the restart"));
		Finish();
	}
}

void UOneBrahamSmokeTestSubsystem::Finish()
{
	if (Phase == EPhase::Done || Phase == EPhase::Holding)
	{
		return;
	}

	Phase = EPhase::Done;

	for (const FString& NetFailure : NetFailures)
	{
		Failures.AddUnique(NetFailure);
	}

	if (Failures.Num() == 0)
	{
		OB_SMOKE_LOG("PASS (%.1fs)", FPlatformTime::Seconds() - StartSeconds);
	}
	else
	{
		OB_SMOKE_LOG("FAIL: %s", *FString::Join(Failures, TEXT("; ")));
	}

	if (Options.HoldSeconds > 0.f)
	{
		OB_SMOKE_LOG("holding for up to %.0fs while clients are connected", Options.HoldSeconds);
		Phase = EPhase::Holding;
		PhaseStartSeconds = FPlatformTime::Seconds();
		return;
	}

	Quit();
}

bool UOneBrahamSmokeTestSubsystem::TickHolding()
{
	const double Held = FPlatformTime::Seconds() - PhaseStartSeconds;

	// A server leaves once nobody is connected to it; anything else (a client,
	// a standalone game, a server whose clients never came) just waits out the
	// hold. The short floor gives a client that is still mid-handshake a chance
	// to show up as a connection before an empty driver ends the hold.
	const UWorld* World = GetPlayerWorld();
	const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	const bool bServing = Driver != nullptr && Driver->IsServer();
	// A full match's host that went back to the menu serves nobody any more:
	// its clients were sent home with it, and there is nothing to wait for.
	const bool bNobodyLeft = (bServing ? Driver->ClientConnections.Num() == 0 : Options.bFullMatch) && Held > 2.0;

	if (Held < Options.HoldSeconds && !bNobodyLeft)
	{
		return true;
	}

	OB_SMOKE_LOG("hold over after %.1fs (%s)", Held, bNobodyLeft ? TEXT("no clients left") : TEXT("time"));
	Phase = EPhase::Done;
	Quit();
	return false;
}

void UOneBrahamSmokeTestSubsystem::Quit()
{
	Phase = EPhase::Done;

	// Not forced: the engine shuts down in order and the log is flushed, which
	// the script reading it depends on. The exit code carries the verdict on
	// Windows and Linux only: Mac has no RequestExitWithStatus of its own, so
	// the generic one logs a warning and a clean Mac exit is always 0. The
	// PASS/FAIL line is the verdict; the code is a second opinion where it exists.
	FPlatformMisc::RequestExitWithStatus(/*Force=*/false, Failures.Num() == 0 ? 0 : 1,
		TEXT("OneBrahamSmokeTest"));
}

#endif // !UE_BUILD_SHIPPING

// ==========================================================================
//  OneBraham.Dev.SmokeOptions - the command-line switches, parsed in isolation
// ==========================================================================
#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSmokeOptionsTest, "OneBraham.Dev.SmokeOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSmokeOptionsTest::RunTest(const FString& Parameters)
{
	const FOneBrahamSmokeOptions Off = FOneBrahamSmokeOptions::Parse(TEXT("-windowed -log"));
	TestFalse(TEXT("no -OBSmoke, not enabled"), Off.bEnabled);
	TestEqual(TEXT("no players expected by default"), Off.ExpectPlayers, 0);
	TestEqual(TEXT("no hold by default"), Off.HoldSeconds, 0.f);

	const FOneBrahamSmokeOptions Host = FOneBrahamSmokeOptions::Parse(
		TEXT("L_Persistent_OneBraham?listen -OBSmoke -OBSmokeShots=3 -OBSmokeTimeout=240 -OBSmokeExpectPlayers=2 -OBSmokeHold=60"));
	TestTrue(TEXT("host: enabled"), Host.bEnabled);
	TestEqual(TEXT("host: shots"), Host.Shots, 3);
	TestEqual(TEXT("host: timeout"), Host.TimeoutSeconds, 240.f);
	TestEqual(TEXT("host: players"), Host.ExpectPlayers, 2);
	TestEqual(TEXT("host: hold"), Host.HoldSeconds, 60.f);

	const FOneBrahamSmokeOptions Wild = FOneBrahamSmokeOptions::Parse(
		TEXT("-OBSmoke -OBSmokeExpectPlayers=500 -OBSmokeHold=-5 -OBSmokeShots=99 -OBSmokeTimeout=1"));
	TestEqual(TEXT("players clamp high"), Wild.ExpectPlayers, FOneBrahamSmokeOptions::MaxExpectPlayers);
	TestEqual(TEXT("hold clamps at zero"), Wild.HoldSeconds, 0.f);
	TestEqual(TEXT("shots clamp high"), Wild.Shots, FOneBrahamSmokeOptions::MaxShots);
	TestEqual(TEXT("timeout clamps low"), Wild.TimeoutSeconds, FOneBrahamSmokeOptions::MinTimeoutSeconds);

	TestFalse(TEXT("null command line"), FOneBrahamSmokeOptions::Parse(nullptr).bEnabled);

	// A whole match: the longest budget unless one is given.
	TestFalse(TEXT("no full match by default"), Off.bFullMatch);
	const FOneBrahamSmokeOptions Full = FOneBrahamSmokeOptions::Parse(TEXT("-OBSmoke -OBSmokeFullMatch -OBSmokeShots=1"));
	TestTrue(TEXT("full match: on"), Full.bFullMatch);
	TestEqual(TEXT("full match: the longest budget"), Full.TimeoutSeconds, FOneBrahamSmokeOptions::MaxTimeoutSeconds);
	const FOneBrahamSmokeOptions FullTimed = FOneBrahamSmokeOptions::Parse(TEXT("-OBSmoke -OBSmokeFullMatch -OBSmokeTimeout=600"));
	TestEqual(TEXT("full match: a given budget is kept"), FullTimed.TimeoutSeconds, 600.f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
