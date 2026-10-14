// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamWorldEffectsComponent.h
// ---------------------------------------------------------------------------
// The local player's weather, and the building's own noises.
//
// RAIN
//   The game state says whether this match is wet (AOneBrahamGameState::
//   bRaining, rolled by the game mode). Rain falls outdoors only: the plaza
//   (floor 0) and the Skydeck (floor 15). While the viewed character is out
//   there, this lays the rain bed over the zone's ambience and draws rain round
//   the camera. NS_OB_Rain draws it once an artist has given the system
//   emitters. Until then a field of thin streaks does, built from engine
//   shapes, so a wet Skydeck looks wet on a clean clone.
//
// THE TUBE
//   The Circle line runs under Aldgate. Every minute or two a train passes
//   beneath the building: the lobby and the plaza feel it, the mezzanine just
//   hears it, and nobody above floor 2 notices.
//
// Local and cosmetic: it runs on the player's own controller, on nobody
// else's machine, and changes nothing in play.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OneBrahamWorldEffectsComponent.generated.h"

class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class UOneBrahamAudioManagerComponent;

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamWorldEffectsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamWorldEffectsComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** True while rain is falling where the local player is looking from. */
	UFUNCTION(BlueprintPure, Category = "One Braham|World")
	bool IsRainActive() const { return bRainActive; }

	/** Streaks in the fallback rain field. */
	static constexpr int32 RainStreakCount = 220;

	/** Half-size of the box of rain round the camera, cm. */
	static constexpr float RainBoxHalfCm = 1200.f;

	/** Fall speed, cm/s. Real rain is 9 m/s. */
	static constexpr float RainFallCmPerSecond = 900.f;

	/** Seconds between trains: the shortest and the longest gap. */
	static constexpr float TubeMinGapSeconds = 70.f;
	static constexpr float TubeMaxGapSeconds = 150.f;

protected:
	/** 2 Hz: where the player is, whether it is raining there, whether a train is due. */
	void Evaluate();

	void SetRainActive(bool bActive);
	void UpdateRainField(float DeltaTime, const FVector& CameraLocation);

	/** The floor the local player is seeing: their own body, or whoever they spectate. */
	int32 GetViewedFloor() const;

	UOneBrahamAudioManagerComponent* GetAudio() const;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> RainSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> RainStreaks = nullptr;

	/** Each streak's place in the box, relative to the camera, cm. */
	TArray<FVector> StreakOffsets;

	bool bRainActive = false;

	/** The global collection the surface master reads RainWetness from. */
	TWeakObjectPtr<class UMaterialParameterCollection> WetnessCollection;
	float AppliedWetness = -1.f;
	float EvaluateAccumulator = 0.f;
	float TubeCountdown = 0.f;
};
