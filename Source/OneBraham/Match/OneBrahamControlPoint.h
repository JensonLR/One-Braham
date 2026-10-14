// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamControlPoint.h
// ---------------------------------------------------------------------------
// One FIRE DRILL point: Reception, the Server Floor or the Canteen.
//
// THE RULES (Docs/06 §1)
//   * A team with living players inside the radius, and no enemy alive inside
//     it, captures: progress climbs over CaptureSeconds, and the point is
//     theirs at the top. A point already theirs stays full.
//   * Both teams inside: contested. Nothing moves.
//   * Nobody inside: nothing moves, and the owner keeps it.
//   * Capturing a point the other team holds first drains their claim, then
//     builds yours: a flip takes twice as long as a neutral capture.
//   * An owned point pays ScorePerSecond to its owner, every second, while the
//     match is live. First to the match's ScoreToWin wins.
//
// The server decides all of it; every machine sees the owner, the progress and
// whether it is contested, and the platform's ring and light take the owner's
// colour so a point can be read across the arena.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneBrahamControlPoint.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class ONEBRAHAM_API AOneBrahamControlPoint : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamControlPoint();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: sets the point up from its definition. */
	void Configure(FName InPointId, const FText& InDisplayName, float InRadiusCm, float InCaptureSeconds, float InScorePerSecond);

	/**
	 * The capture rule, pure: given who is inside, where a point goes in one
	 * step. Owner and the result are team indices (0 staff, 1 contractors,
	 * INDEX_NONE nobody); Progress is the owner's claim (or, while flipping,
	 * the capturer's), 0..1.
	 */
	static void StepCapture(int32 TeamAInside, int32 TeamBInside, float DeltaSeconds, float InCaptureSeconds,
		int32& InOutOwner, int32& InOutCapturer, float& InOutProgress, bool& bOutContested);

	UFUNCTION(BlueprintPure, Category = "One Braham|Fire Drill")
	FName GetPointId() const { return PointId; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Fire Drill")
	FText GetDisplayName() const { return DisplayName; }

	/** 0 staff, 1 contractors, INDEX_NONE neutral. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Fire Drill")
	int32 GetOwnerTeamIndex() const { return OwnerTeam; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Fire Drill")
	float GetProgress01() const { return Progress; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Fire Drill")
	bool IsContested() const { return bContested; }

	/** The team gaining ground right now, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Fire Drill")
	int32 GetCapturingTeamIndex() const { return CapturingTeam; }

	/** Players more than this far above or below the platform are not on it. */
	static constexpr float HeightToleranceCm = 300.f;

protected:
	UFUNCTION()
	void OnRep_State();

	/** Server: who is standing on the point, by team. */
	void CountInside(int32& OutTeamA, int32& OutTeamB) const;

	/** Every machine: the ring and the light take the owner's colour. */
	void ApplyColours();

	/** Unscaled, so the light above the ring is not flattened with it. */
	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> PointRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Ring = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> Light = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial = nullptr;

	UPROPERTY(Replicated)
	FName PointId = NAME_None;

	UPROPERTY(Replicated)
	FText DisplayName;

	UPROPERTY(Replicated)
	float RadiusCm = 700.f;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	int32 OwnerTeam = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	int32 CapturingTeam = INDEX_NONE;

	UPROPERTY(Replicated)
	float Progress = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bContested = false;

	float CaptureSeconds = 5.f;
	float ScorePerSecond = 1.f;

	/** Score earned but not yet paid: points are whole, time is not. */
	float ScoreAccumulator = 0.f;
};
