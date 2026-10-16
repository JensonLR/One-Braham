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
#include "Subsystems/GameInstanceSubsystem.h"
#include "OneBrahamSmokeTest.generated.h"

class APlayerController;
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

	static constexpr int32 MaxShots = 16;
	static constexpr float MinTimeoutSeconds = 15.f;
	static constexpr float MaxTimeoutSeconds = 900.f;

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
		Measuring,
		Shooting,
		Flushing,
		Done
	};

	/** Core ticker callback; returns false once the run is over. */
	bool Tick(float DeltaTime);

	/** The pawn is in: logs the static checks, then starts the frame sample. */
	void RunChecks(UWorld* World, APlayerController* PC);

	/** One frame of the frame-time sample. */
	void TickMeasuring();

	/** Turns the camera and requests the next screenshot, one every interval. */
	void TickShooting(APlayerController* PC);

	/** Gives the last screenshot time to land, then checks the files. */
	void TickFlushing();

	/** Logs the verdict and asks the game to quit. */
	void Finish();

	/** The game world the local player is in, or null. */
	UWorld* GetPlayerWorld() const;

	/** Records a failed check. */
	void Fail(const FString& Reason);

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

	/** The yaw the player was facing when shooting began. */
	float BaseYaw = 0.f;

	/** The shot the camera has been turned for; the turn is made once per shot
	 *  so a controller that adjusts its rotation cannot stall the run. */
	int32 TurnedForShot = INDEX_NONE;

	TArray<FString> Failures;
};
