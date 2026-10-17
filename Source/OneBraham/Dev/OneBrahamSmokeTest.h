// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSmokeTest.h
// ---------------------------------------------------------------------------
// `-OBSmoke` - the unattended smoke test for a packaged (non-Shipping) build.
//
// WHAT IT IS
//   A game-instance subsystem that does nothing unless the command line carries
//   -OBSmoke. When it does, it waits for the local player controller to possess
//   a pawn, logs one "[OB.SMOKE]" line per check, measures the frame time for a
//   few seconds, takes screenshots from the player's view while turning the
//   camera between them, logs "[OB.SMOKE] PASS" or "[OB.SMOKE] FAIL: <reasons>"
//   and quits the game (exit code 0 or 1 where the platform passes it on; a Mac
//   build always exits 0, so the line is the verdict). Scripts/SmokePackaged.sh runs the
//   staged Mac app this way and reads the log, so a packaged build can be
//   checked with nobody at the screen.
//
//       OneBraham -OBSmoke -windowed -log                 4 shots, 120 s budget
//       OneBraham -OBSmoke -OBSmokeShots=6 -OBSmokeTimeout=180
//
//   Multiplayer (Scripts/SmokeLAN.sh runs both halves):
//       OneBraham L_Persistent_OneBraham?listen -OBSmoke -OBSmokeExpectPlayers=2 -OBSmokeHold=60
//       OneBraham 127.0.0.1 -OBSmoke -OBSmokeExpectPlayers=2
//
// THE CHECKS (each one a line, each failure a reason on the FAIL line)
//   map          the loaded map's name; FAIL when no game world is loaded
//   pawn         class and location of the possessed pawn; FAIL on the timeout
//   input        the controller's InputConfig; FAIL when it is null
//   mappings     installed Enhanced Input contexts and the key mappings they
//                produced; FAIL when either count is zero (no controls)
//   match        the game state's match settings and mode; FAIL when unset
//   sky          the AOneBrahamSkyDirector in the world; FAIL when absent
//   frames       mean / p95 / worst frame time over the sample window. Logged,
//                never failed on: the number depends on the machine
//   shot N       absolute path of each screenshot; FAIL when the file was not
//                written by the end of the run
//
// THE MULTIPLAYER CHECKS (only with -OBSmokeExpectPlayers=N, N >= 1)
//   players      waits, after the checks above, until the game state holds at
//                least N human player states (bots are not counted) and each
//                has a pawn, or PawnGraceSeconds after the count is reached;
//                then one "player" line each: name, team, squad, pawn and its
//                location, ping. FAIL on the timeout ("waiting for 2 players,
//                have 1")
//   net          the net driver and its connections: on a client the server
//                connection's address, state and lag (FAIL with no open server
//                connection); on a listen server every client connection
//   standalone   FAIL when the game is not networked at all: a client whose
//                connection failed falls back to the default map, which is
//                the tower, and would otherwise pass there on its own
//   net failure  any network or travel failure the engine reports (a host
//                that cannot be reached, a connection lost mid-run) is logged
//                when it happens and FAILs the run
//   facing       the screenshots start facing the nearest other human's pawn,
//                so a shot from either side shows the other one when nothing
//                stands between them; the line logs the distance and whether
//                a visibility trace reaches them (and what blocks it if not)
//
// A WHOLE MATCH (-OBSmokeFullMatch)
//   After the screenshots, instead of the verdict: the local player is taken
//   out of the fight (AOneBrahamGameMode::DevWithdrawToSpectator - a bot takes
//   the slot, the camera follows the highest living bot) and the run waits for
//   the match to end, logging its progress every 30 s. Then:
//     result       one line: the ending, the winner, the duration, the score
//     squad        one line per squad: members, in the fight, extracted
//     end screen   FAIL when UOneBrahamMatchEndWidget is not up on this machine
//     shot end     a screenshot of the end screen
//     coherent     OneBrahamMatchEnd::IsResultCoherent; FAIL with its reason
//     after        waits for the post-match travel (the menu, or a reload of
//                  the map while the menu is not built) and logs where it went;
//                  FAIL when nobody left the end screen
//   PASS only if the match reached Complete with a coherent result. The budget
//   defaults to MaxTimeoutSeconds; compress the match with the dev console
//   variables (Docs/18), e.g.
//       -OBSmoke -OBSmokeFullMatch -OBSmokeShots=1 -ExecCmds="ob.Match.WarmupSeconds 5,
//        ob.Lockdown.StageSeconds 40, ob.Extraction.ArrivalSeconds 10, ob.Match.PostMatchSeconds 12"
//   On a client the player is not withdrawn (only the server can do that): the
//   run waits for the server's match to end the same way.
//
// HOLDING (-OBSmokeHold=S)
//   After the verdict, stay up for at most S more seconds instead of quitting,
//   and quit as soon as a server has no client connections left. A listen host
//   that quit on its own PASS would pull the match out from under a client
//   still taking its screenshots; with a hold it leaves after the client does.
//
// WHY A GAME-INSTANCE SUBSYSTEM
//   It outlives map loads, so a build that boots into a loading map and then
//   travels still gets its pawn waited for, and it ticks from the core ticker
//   rather than a world, so a world that never finishes loading still hits the
//   timeout and still exits.
//
// SHIPPING
//   UHT cannot see a UCLASS behind #if !UE_BUILD_SHIPPING, so the class is
//   declared in every configuration and its body is compiled out of Shipping:
//   ShouldCreateSubsystem returns false there and nothing else is built.
//   -OBSmoke on a Shipping build is ignored.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OneBrahamSmokeTest.generated.h"

class APlayerController;
class UNetDriver;
class UWorld;

/** What the command line asked for. Parsed once, at Initialize. */
struct ONEBRAHAM_API FOneBrahamSmokeOptions
{
	/** -OBSmoke present. Everything else is ignored without it. */
	bool bEnabled = false;

	/** -OBSmokeShots=N, clamped to [0, MaxShots]. */
	int32 Shots = 4;

	/** -OBSmokeTimeout=S, the whole run's wall-clock budget, clamped to
	 *  [MinTimeoutSeconds, MaxTimeoutSeconds]. */
	float TimeoutSeconds = 120.f;

	/** -OBSmokeExpectPlayers=N, clamped to [0, MaxExpectPlayers]. 0 skips the
	 *  multiplayer checks. */
	int32 ExpectPlayers = 0;

	/** -OBSmokeHold=S, clamped to [0, MaxTimeoutSeconds]: how long to stay up
	 *  after the verdict while clients are still connected. */
	float HoldSeconds = 0.f;

	/** -OBSmokeFullMatch: watch the match to its end before the verdict. The
	 *  budget defaults to MaxTimeoutSeconds unless -OBSmokeTimeout says. */
	bool bFullMatch = false;

	static constexpr int32 MaxShots = 16;
	static constexpr int32 MaxExpectPlayers = 32;
	static constexpr float MinTimeoutSeconds = 15.f;
	/** Long enough for an uncompressed ASCENT: 35 minutes and the end screen. */
	static constexpr float MaxTimeoutSeconds = 2400.f;

	/** Reads the switches above from a command line. Pure, for tests. */
	static FOneBrahamSmokeOptions Parse(const TCHAR* CommandLine);
};

/**
 * The smoke test. One per game instance, idle and untickable unless -OBSmoke
 * was passed; never created on a dedicated server, in the editor, or in a
 * Shipping build.
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamSmokeTestSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The prefix of every line this writes; Scripts/SmokePackaged.sh greps it. */
	static const TCHAR* LogPrefix() { return TEXT("[OB.SMOKE]"); }

private:
	enum class EPhase : uint8
	{
		WaitingForPawn,
		WaitingForPlayers,
		Measuring,
		Shooting,
		Flushing,
		WatchingMatch,
		EndScreen,
		EndFlushing,
		AwaitingTravel,
		NextMatch,
		Holding,
		Done
	};

	/** Core ticker callback; returns false once the run is over. */
	bool Tick(float DeltaTime);

	/** The pawn is in: logs the static checks, then starts the frame sample. */
	void RunChecks(UWorld* World, APlayerController* PC);

	/** -OBSmokeExpectPlayers: true once enough players are in (and have pawns,
	 *  or the grace period ran out). */
	bool ArePlayersReady(UWorld* World);

	/** Logs every player, the net connection(s), and fails what is missing. */
	void RunPlayerChecks(UWorld* World, APlayerController* PC);

	/** The nearest pawn of another human player (not a bot), or null. */
	const APawn* FindNearestOtherPawn(UWorld* World, APlayerController* PC) const;

	/** One frame of the frame-time sample. */
	void TickMeasuring();

	/** Turns the camera and requests the next screenshot, one every interval. */
	void TickShooting(APlayerController* PC);

	/** Gives the last screenshot time to land, then checks the files. */
	void TickFlushing();

	/** -OBSmokeFullMatch: out of the fight, then wait for the end. */
	void BeginWatchingMatch(UWorld* World, APlayerController* PC);

	/** Waits for the match to reach Complete, logging its progress. */
	void TickWatchingMatch(UWorld* World);

	/** The end screen: logs the result, checks it, photographs it. */
	void TickEndScreen(UWorld* World, APlayerController* PC);

	/** Gives the end screenshot time to land. */
	void TickEndFlushing();

	/** Waits for the post-match travel to take everybody somewhere. */
	void TickAwaitingTravel();

	/** After a restart or a reload of the same map: the next match has begun and
	 *  the last one's end screen has gone, with the game's input back. */
	void TickNextMatch();

	/** Logs the verdict, then holds (-OBSmokeHold) or quits. */
	void Finish();

	/** After the verdict: true while the hold should go on. */
	bool TickHolding();

	/** Asks the game to quit with the verdict as the exit code. */
	void Quit();

	/** The game world the local player is in, or null. */
	UWorld* GetPlayerWorld() const;

	/** Records a failed check. */
	void Fail(const FString& Reason);

	/** The engine dropped a connection or could not travel (a client that
	 *  could not reach its host, a host that went away mid-run). Recorded apart
	 *  from Failures: a client that fails to connect falls back to the default
	 *  map, possesses a pawn there and would otherwise start the run afresh. */
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& Error);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);

	/** One line on where this game stands on the network, for a timeout
	 *  reason: "client of 127.0.0.1:7777 (Open), 3 player state(s)". */
	FString DescribeNet(UWorld* World) const;

	FOneBrahamSmokeOptions Options;
	FTSTicker::FDelegateHandle TickHandle;

	EPhase Phase = EPhase::WaitingForPawn;

	/** Wall clock at Initialize, and at the start of the current phase. */
	double StartSeconds = 0.0;
	double PhaseStartSeconds = 0.0;
	double LastFrameSeconds = 0.0;
	double LastShotSeconds = 0.0;

	/** Frame times captured in the Measuring phase, in milliseconds. */
	TArray<float> FrameTimesMs;

	/** Absolute paths of the screenshots requested so far. */
	TArray<FString> ShotPaths;

	/** The view the first screenshot is taken from: where the player was
	 *  facing when shooting began, or towards the nearest other player. */
	float BaseYaw = 0.f;
	float BasePitch = 0.f;

	/** -OBSmokeExpectPlayers: the player checks have run for this pawn. */
	bool bPlayersChecked = false;

	/** When the player count first reached ExpectPlayers (0 = not yet). */
	double PlayersReachedSeconds = 0.0;

	/** The shot the camera has been turned for; the turn is made once per shot
	 *  so a controller that adjusts its rotation cannot stall the run. */
	int32 TurnedForShot = INDEX_NONE;

	TArray<FString> Failures;

	/** Network and travel failures, kept across re-possession (see above). */
	TArray<FString> NetFailures;

	/** -OBSmokeFullMatch: the world the match was played in, the last progress
	 *  line, and how long to wait for the travel after the end screen. */
	TWeakObjectPtr<UWorld> MatchWorld;
	/** The match's game state: a seamless restart of the same map can keep the
	 *  world and replace everything in it. */
	TWeakObjectPtr<AActor> MatchGameState;
	FString MatchMapName;
	double LastProgressSeconds = 0.0;
	double TravelDeadlineSeconds = 0.0;

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
