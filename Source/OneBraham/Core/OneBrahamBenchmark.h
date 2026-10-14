// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBenchmark.h
// ---------------------------------------------------------------------------
// `ob.Bench.Skydeck` - the scripted flythrough benchmark.
//
// WHAT IT IS
//   A camera that walks the building from the ground plaza to the Skydeck,
//   one leg per floor of the layout table, while recording the frame time of
//   every frame the game produces. It exists because the performance numbers in
//   Docs/18 section 4 are only worth anything if two runs can be compared, and
//   "walk around and see how it feels" cannot be compared with anything.
//
//   The route is derived from UOneBrahamBuildingLayoutAsset rather than typed
//   in here: if a floor moves in the data, the benchmark walks the new floor.
//   The legs are named from the layout's short names, so the report reads like
//   the building ("LOBBY 16.1 ms") instead of like a test harness.
//
// WHAT IT RECORDS
//   Per frame: the frame time in milliseconds. Per leg: count, mean, median,
//   95th and 99th percentile, worst. Overall: the same, plus the leg count and
//   the run's wall-clock length. It writes Saved/Benchmarks/ob_bench_<stamp>.md
//   and logs a one-line summary, so a benchmark run leaves a file behind that
//   can be attached to a milestone note.
//
// WHAT IT DELIBERATELY DOES NOT DO
//   It does not change rendering settings, spawn load, or clear the level. It
//   measures the map as it is, because the question being asked is "is this
//   build inside the budget", not "can this machine run a synthetic scene".

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamBenchmark.generated.h"

/** One leg of the flythrough: one floor, and what it cost. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamBenchmarkLeg
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	FString Label;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	int32 FloorIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	int32 FrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float MeanMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float MedianMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float P95Ms = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float WorstMs = 0.f;
};

/** The result of one run, whether it finished or was aborted. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamBenchmarkResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	int32 FrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float MeanMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float MedianMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float P95Ms = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float P99Ms = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float WorstMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float MeanFps = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	float DurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	FString ReportPath;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Benchmark")
	TArray<FOneBrahamBenchmarkLeg> Legs;
};

/**
 * The flythrough. One instance per game world, alive with the world, idle until
 * `ob.Bench.Skydeck` starts it.
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamBenchmarkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bRunning; }

	/** Starts a run. Returns false with a reason in OutRefusal when it cannot:
	 *  already running, no camera, a dedicated server. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Benchmark")
	bool StartBenchmark(float SecondsPerLeg, FText& OutRefusal);

	/** Stops a run early and writes the report for the frames captured so far. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Benchmark")
	void AbortBenchmark();

	UFUNCTION(BlueprintPure, Category = "One Braham|Benchmark")
	bool IsRunning() const { return bRunning; }

	/** 0..1 through the whole flythrough, for an on-screen progress line. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Benchmark")
	float GetProgress01() const;

	/** The result of the last completed or aborted run. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Benchmark")
	const FOneBrahamBenchmarkResult& GetLastResult() const { return LastResult; }

private:
	/** Builds the leg list from the building layout. */
	void BuildRoute();

	/** Inside Tick while running: moves the camera and captures one frame. */
	void CaptureFrame(float DeltaTime);

	/** Ends the run, writes the report, restores the view target. */
	void Finish(bool bCompleted);

	/** Writes Saved/Benchmarks/ob_bench_<stamp>.md from LastResult. Returns the
	 *  path, or an empty string when the write failed. */
	FString WriteReport();

	/** Milliseconds of every frame captured this run. */
	TArray<float> FrameTimesMs;

	/** Frame counts per leg, filled as the route is walked. */
	TArray<int32> LegFrameCounts;

	/** The route: world location per leg, built once from the layout. */
	TArray<FVector> LegLocations;

	/** The labels for the route's legs. */
	TArray<FString> LegLabels;

	/** Floor index per leg, for the report. */
	TArray<int32> LegFloors;

	UPROPERTY()
	TObjectPtr<AActor> BenchmarkCamera;

	UPROPERTY()
	TObjectPtr<AActor> RestoreViewTarget;

	FOneBrahamBenchmarkResult LastResult;

	bool bRunning = false;
	float SecondsPerLeg = 6.f;
	float ElapsedThisLeg = 0.f;
	float TotalElapsed = 0.f;
	float LastFrameTime = 0.f;
	int32 LegIndex = 0;
	int32 PendingLegSamples = 0;
};
