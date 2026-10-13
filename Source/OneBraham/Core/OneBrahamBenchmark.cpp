// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamBenchmark.h"

#include "Camera/CameraActor.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OneBraham.h"

namespace
{
	/** How long each floor gets, in seconds. Long enough to settle: the first
	 *  frames of a leg are the ones with the previous floor's streaming in
	 *  flight, and a one second leg measures the streaming rather than the map. */
	constexpr float DefaultSecondsPerLeg = 6.f;

	/** Eye height above a floor slab, in centimetres. Roughly a standing view. */
	constexpr float EyeHeightCm = 170.f;

	/**
	 * The pan across each floor, in centimetres. The camera walks a shallow S
	 * rather than staring down a fixed axis: a flythrough that faces one wall
	 * reports the cost of that wall, and the point is the room.
	 */
	constexpr float PanAmplitudeCm = 900.f;

	/** Where in the room the camera looks, as a yaw sweep over the leg. */
	constexpr float YawSweepDegrees = 55.f;

	/** Percentile of a sorted array, 0..1. */
	float Percentile(const TArray<float>& Sorted, float Fraction)
	{
		if (Sorted.Num() == 0)
		{
			return 0.f;
		}

		const int32 Index = FMath::Clamp(
			FMath::RoundToInt(Fraction * (Sorted.Num() - 1)), 0, Sorted.Num() - 1);

		return Sorted[Index];
	}

	/** Fills a result's aggregate numbers from its frame samples. */
	void Summarise(const TArray<float>& Samples, FOneBrahamBenchmarkResult& Out)
	{
		Out.FrameCount = Samples.Num();

		if (Samples.Num() == 0)
		{
			return;
		}

		TArray<float> Sorted = Samples;
		Sorted.Sort();

		float Sum = 0.f;
		for (const float Sample : Sorted)
		{
			Sum += Sample;
		}

		Out.MeanMs = Sum / static_cast<float>(Sorted.Num());
		Out.MedianMs = Percentile(Sorted, 0.5f);
		Out.P95Ms = Percentile(Sorted, 0.95f);
		Out.P99Ms = Percentile(Sorted, 0.99f);
		Out.WorstMs = Sorted.Last();
		Out.MeanFps = Out.MeanMs > 0.f ? 1000.f / Out.MeanMs : 0.f;
	}
}

bool UOneBrahamBenchmarkSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only, and never on a dedicated server: there is no camera and
	// no rendering there, and a benchmark that measures a null RHI measures
	// nothing.
	const UWorld* World = Cast<UWorld>(Outer);

	return World != nullptr && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

TStatId UOneBrahamBenchmarkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamBenchmarkSubsystem, STATGROUP_Tickables);
}

bool UOneBrahamBenchmarkSubsystem::StartBenchmark(float InSecondsPerLeg, FText& OutRefusal)
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Bench_NoWorld", "There is no world to measure.");
		return false;
	}

	if (bRunning)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Bench_Already", "A benchmark is already running.");
		return false;
	}

	APlayerController* PC = World->GetFirstPlayerController();

	if (PC == nullptr || !PC->IsLocalController())
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Bench_NoPlayer",
			"The benchmark needs a local player: run it from a client or a listen server.");
		return false;
	}

	BuildRoute();

	if (LegLocations.Num() == 0)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Bench_NoRoute",
			"The building layout is empty, so there is no route to fly.");
		return false;
	}

	// The camera flies as a player controller's view target rather than as a
	// spectator pawn: the goal is the cost of the map as a player sees it.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACameraActor* Camera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), LegLocations[0], FRotator::ZeroRotator, SpawnParams);

	if (Camera == nullptr)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Bench_NoCamera", "Could not spawn the benchmark camera.");
		return false;
	}

	// The world as the player sees it, including the HUD: a benchmark that
	// removes the interface measures a build nobody plays.
	RestoreViewTarget = PC->GetViewTarget();
	BenchmarkCamera = Camera;
	PC->SetViewTargetWithBlend(Camera, 0.f);

	FrameTimesMs.Reset();
	LegFrameCounts.Reset();
	FrameTimesMs.Reserve(20000);

	for (int32 Leg = 0; Leg < LegLocations.Num(); ++Leg)
	{
		LegFrameCounts.Add(0);
	}

	SecondsPerLeg = FMath::Clamp(InSecondsPerLeg, 1.f, 120.f);
	ElapsedThisLeg = 0.f;
	TotalElapsed = 0.f;
	LastFrameTime = FPlatformTime::Seconds();
	LegIndex = 0;
	PendingLegSamples = 0;
	bRunning = true;

	OB_LOG("ob.Bench.Skydeck: %d leg(s) of %.1fs - %s", LegLocations.Num(), SecondsPerLeg,
		*FString::Join(LegLabels, TEXT(" -> ")));

	return true;
}

void UOneBrahamBenchmarkSubsystem::AbortBenchmark()
{
	if (!bRunning)
	{
		return;
	}

	Finish(/*bCompleted=*/false);
}

float UOneBrahamBenchmarkSubsystem::GetProgress01() const
{
	if (!bRunning || LegLocations.Num() == 0)
	{
		return 0.f;
	}

	const float LegFraction = FMath::Clamp(ElapsedThisLeg / FMath::Max(0.01f, SecondsPerLeg), 0.f, 1.f);

	return FMath::Clamp((static_cast<float>(LegIndex) + LegFraction) / static_cast<float>(LegLocations.Num()),
		0.f, 1.f);
}

void UOneBrahamBenchmarkSubsystem::BuildRoute()
{
	LegLocations.Reset();
	LegLabels.Reset();
	LegFloors.Reset();

	UWorld* World = GetWorld();
	const UOneBrahamBuildingLayoutAsset* Layout = UOneBrahamBuildingLayoutAsset::Get(World);

	if (Layout == nullptr)
	{
		return;
	}

	// One leg per floor row, in the order the building has them. The X/Y pans
	// alternate so consecutive floors are not the same camera position twice:
	// the report's per-floor numbers should be about the floor, not the stand.
	int32 Index = 0;

	for (const FOneBrahamFloorDescription& Floor : Layout->Floors)
	{
		const float Z = static_cast<float>(Floor.FloorIndex) * OneBrahamConstants::FloorHeightCm + EyeHeightCm;
		const float X = (Index % 2 == 0) ? -PanAmplitudeCm : PanAmplitudeCm * 0.5f;
		const float Y = (Index % 3 == 0) ? PanAmplitudeCm : -PanAmplitudeCm * 0.5f;

		LegLocations.Add(FVector(X, Y, Z));
		LegLabels.Add(Floor.ShortName.IsEmpty()
			? FString::Printf(TEXT("FLOOR %d"), Floor.FloorIndex)
			: Floor.ShortName.ToString().ToUpper());
		LegFloors.Add(Floor.FloorIndex);

		++Index;
	}
}

void UOneBrahamBenchmarkSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bRunning)
	{
		return;
	}

	CaptureFrame(DeltaTime);
}

void UOneBrahamBenchmarkSubsystem::CaptureFrame(float DeltaTime)
{
	// The frame time is measured from the platform clock rather than from the
	// tick's DeltaTime: DeltaTime is capped by the engine's smoothing, and the
	// number a performance budget cares about is the wall-clock one.
	const double Now = FPlatformTime::Seconds();
	const float FrameMs = LastFrameTime > 0.0
		? static_cast<float>((Now - LastFrameTime) * 1000.0)
		: DeltaTime * 1000.f;

	LastFrameTime = Now;

	// A frame longer than a second is a hitch (streaming, shader compile, or a
	// debugger breakpoint) and would poison the mean.
	if (FrameMs > 0.f && FrameMs < 1000.f)
	{
		FrameTimesMs.Add(FrameMs);

		if (LegFrameCounts.IsValidIndex(LegIndex))
		{
			++LegFrameCounts[LegIndex];
		}

		++PendingLegSamples;
	}

	ElapsedThisLeg += DeltaTime;
	TotalElapsed += DeltaTime;

	// Walk the leg: linear from this floor's stand to the next, with a yaw sweep
	// so the camera reads the room rather than a wall.
	if (LegLocations.IsValidIndex(LegIndex) && BenchmarkCamera != nullptr)
	{
		const float LegFraction = FMath::Clamp(ElapsedThisLeg / SecondsPerLeg, 0.f, 1.f);

		const FVector From = LegLocations[LegIndex];
		const FVector To = LegLocations.IsValidIndex(LegIndex + 1) ? LegLocations[LegIndex + 1] : From;
		const FVector Position = FMath::Lerp(From, To, LegFraction);

		const float Yaw = FMath::Sin(LegFraction * 2.f * PI) * YawSweepDegrees;
		const float Pitch = FMath::Lerp(-6.f, 4.f, LegFraction);

		BenchmarkCamera->SetActorLocationAndRotation(
			Position, FRotator(Pitch, Yaw, 0.f), /*bSweep=*/false, nullptr,
			ETeleportType::TeleportPhysics);
	}

	if (ElapsedThisLeg < SecondsPerLeg)
	{
		return;
	}

	ElapsedThisLeg = 0.f;

	if (LegIndex + 1 < LegLocations.Num())
	{
		++LegIndex;
		return;
	}

	Finish(/*bCompleted=*/true);
}

void UOneBrahamBenchmarkSubsystem::Finish(bool bCompleted)
{
	bRunning = false;

	UWorld* World = GetWorld();

	// Restore the player's view before anything else: a benchmark that leaves
	// the player in a camera is a bug report.
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (RestoreViewTarget != nullptr)
		{
			PC->SetViewTargetWithBlend(RestoreViewTarget, 0.35f, EViewTargetBlendFunction::VTBlend_Cubic);
		}
		else if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, 0.35f, EViewTargetBlendFunction::VTBlend_Cubic);
		}
	}

	if (BenchmarkCamera != nullptr)
	{
		BenchmarkCamera->Destroy();
		BenchmarkCamera = nullptr;
	}

	RestoreViewTarget = nullptr;

	LastResult = FOneBrahamBenchmarkResult();
	LastResult.DurationSeconds = TotalElapsed;
	LastResult.MapName = World != nullptr ? World->GetMapName() : FString(TEXT("unknown"));

	Summarise(FrameTimesMs, LastResult);

	// Per-leg numbers, recomputed from the per-leg frame counts: the samples are
	// one array in capture order, so each leg's slice is a contiguous run.
	int32 Cursor = 0;
	for (int32 Leg = 0; Leg < LegLabels.Num() && Leg < LegFrameCounts.Num(); ++Leg)
	{
		const int32 Count = FMath::Min(LegFrameCounts[Leg], FrameTimesMs.Num() - Cursor);

		FOneBrahamBenchmarkLeg Out;
		Out.Label = LegLabels[Leg];
		Out.FloorIndex = LegFloors.IsValidIndex(Leg) ? LegFloors[Leg] : 0;
		Out.FrameCount = FMath::Max(0, Count);

		if (Count > 0)
		{
			const TArray<float> Slice(FrameTimesMs.GetData() + Cursor, Count);
			TArray<float> Sorted = Slice;
			Sorted.Sort();

			float Sum = 0.f;
			for (const float Sample : Sorted)
			{
				Sum += Sample;
			}

			Out.MeanMs = Sum / static_cast<float>(Count);
			Out.MedianMs = Percentile(Sorted, 0.5f);
			Out.P95Ms = Percentile(Sorted, 0.95f);
			Out.WorstMs = Sorted.Last();
		}

		LastResult.Legs.Add(Out);
		Cursor += Count;
	}

	LastResult.ReportPath = WriteReport();

	OB_LOG("ob.Bench.Skydeck: %s - %d frames, mean %.2f ms (%.0f fps), p95 %.2f ms, worst %.2f ms%s",
		bCompleted ? TEXT("complete") : TEXT("aborted"),
		LastResult.FrameCount, LastResult.MeanMs, LastResult.MeanFps,
		LastResult.P95Ms, LastResult.WorstMs,
		LastResult.ReportPath.IsEmpty() ? TEXT(" (no report written)") : TEXT(""));

	if (!LastResult.ReportPath.IsEmpty())
	{
		OB_LOG("ob.Bench.Skydeck: report written to %s", *LastResult.ReportPath);
	}

	FrameTimesMs.Reset();
	LegFrameCounts.Reset();
}

FString UOneBrahamBenchmarkSubsystem::WriteReport()
{
	// Saved/Benchmarks, created on demand: a benchmark run should leave a file
	// behind that can be attached to a milestone note.
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Benchmarks");

	// Created if missing; a failure here is not fatal, because the write below
	// reports it and that is the message worth logging.
	IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString Path = Directory / FString::Printf(TEXT("ob_bench_%s.md"), *Stamp);

	TArray<FString> Lines;
	Lines.Add(TEXT("# ONE BRAHAM - flythrough benchmark"));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("Map:      `%s`"), *LastResult.MapName));
	Lines.Add(FString::Printf(TEXT("Route:    %d floors, %.0fs per floor"),
		LastResult.Legs.Num(), SecondsPerLeg));
	Lines.Add(FString::Printf(TEXT("Duration: %.1fs, %d frames"),
		LastResult.DurationSeconds, LastResult.FrameCount));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("## Frame time"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Mean | Median | p95 | p99 | Worst | Mean fps |"));
	Lines.Add(TEXT("| --- | --- | --- | --- | --- | --- |"));
	Lines.Add(FString::Printf(TEXT("| %.2f ms | %.2f ms | %.2f ms | %.2f ms | %.2f ms | %.1f |"),
		LastResult.MeanMs, LastResult.MedianMs, LastResult.P95Ms, LastResult.P99Ms,
		LastResult.WorstMs, LastResult.MeanFps));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("The budget in `Docs/18_Testing_Performance.md` section 4 is 16.6 ms mean at"));
	Lines.Add(TEXT("60 fps with p95 inside one frame drop. This report is worth comparing with"));
	Lines.Add(TEXT("the previous one only when both were run on the same hardware, at the same"));
	Lines.Add(TEXT("resolution and the same scalability level - so record them here:"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("- hardware: "));
	Lines.Add(TEXT("- resolution / scalability: "));
	Lines.Add(TEXT("- build / changelist: "));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("## Per floor"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Floor | Leg | Frames | Mean | Median | p95 | Worst |"));
	Lines.Add(TEXT("| --- | --- | --- | --- | --- | --- | --- |"));

	for (const FOneBrahamBenchmarkLeg& Leg : LastResult.Legs)
	{
		Lines.Add(FString::Printf(TEXT("| %d | %s | %d | %.2f ms | %.2f ms | %.2f ms | %.2f ms |"),
			Leg.FloorIndex, *Leg.Label, Leg.FrameCount, Leg.MeanMs, Leg.MedianMs,
			Leg.P95Ms, Leg.WorstMs));
	}

	Lines.Add(TEXT(""));
	Lines.Add(TEXT("A floor whose worst frame is far above its mean is a streaming or shader"));
	Lines.Add(TEXT("hitch rather than a load problem: look at what the leg streams in."));
	Lines.Add(TEXT(""));

	if (!FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *Path))
	{
		return FString();
	}

	return Path;
}
