// Copyright (c) One Braham. All Rights Reserved.

#include "Dev/OneBrahamSmokeTest.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if !UE_BUILD_SHIPPING
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamTypes.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
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

	OB_SMOKE_LOG("start: %d shot(s), %.0fs budget, build %s", Options.Shots, Options.TimeoutSeconds,
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

	const double Now = FPlatformTime::Seconds();

	// The one budget for the whole run, load included. Whatever phase it ends
	// in is the reason: "timeout waiting for a pawn" is the useful bug report.
	if (Now - StartSeconds > Options.TimeoutSeconds)
	{
		static const TCHAR* PhaseNames[] = { TEXT("a pawn"), TEXT("the frame sample"),
			TEXT("the screenshots"), TEXT("the screenshots to be written"), TEXT("nothing") };

		Fail(FString::Printf(TEXT("timeout after %.0fs waiting for %s"),
			Options.TimeoutSeconds, PhaseNames[static_cast<uint8>(Phase)]));
		Finish();
		return false;
	}

	UWorld* World = GetPlayerWorld();
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;

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
			break;
		}

		TickMeasuring();
		break;

	case EPhase::Shooting:
		if (PC == nullptr)
		{
			Fail(TEXT("lost the player controller while taking screenshots"));
			Finish();
			return false;
		}

		TickShooting(PC);
		break;

	case EPhase::Flushing:
		TickFlushing();
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

	if (UWorld* World = GetPlayerWorld())
	{
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World))
		{
			BaseYaw = PC->GetControlRotation().Yaw;
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
	const FRotator Facing(0.f, FRotator::NormalizeAxis(BaseYaw + ShotYawStepDegrees * Taken), 0.f);

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

	static const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::ScreenShotDir() / FString::Printf(TEXT("OBSmoke_%s_%02d.png"), *Stamp, Taken + 1));

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

	Finish();
}

void UOneBrahamSmokeTestSubsystem::Finish()
{
	if (Phase == EPhase::Done)
	{
		return;
	}

	Phase = EPhase::Done;

	if (Failures.Num() == 0)
	{
		OB_SMOKE_LOG("PASS (%.1fs)", FPlatformTime::Seconds() - StartSeconds);
	}
	else
	{
		OB_SMOKE_LOG("FAIL: %s", *FString::Join(Failures, TEXT("; ")));
	}

	// Not forced: the engine shuts down in order and the log is flushed, which
	// the script reading it depends on. The exit code carries the verdict on
	// Windows and Linux only: Mac has no RequestExitWithStatus of its own, so
	// the generic one logs a warning and a clean Mac exit is always 0. The
	// PASS/FAIL line is the verdict; the code is a second opinion where it exists.
	FPlatformMisc::RequestExitWithStatus(/*Force=*/false, Failures.Num() == 0 ? 0 : 1,
		TEXT("OneBrahamSmokeTest"));
}

#endif // !UE_BUILD_SHIPPING
