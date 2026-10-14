// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHelicopter.h
// ---------------------------------------------------------------------------
// The extraction helicopter you can see: the ending, arriving.
//
// It flies in over Aldgate for the 45 s approach, hangs over the pad with its
// searchlight on the deck and the winch line down while a squad holds, and
// climbs away when the match ends either way. The rotor is the audio manager's
// bed at the pad; this is the part the players look up at.
//
// IT HAS NO STATE OF ITS OWN
//   Every machine works out where the helicopter is from the extraction
//   component's replicated state and time remaining, which is already what the
//   HUD's timer reads. Nothing is replicated, so it cannot disagree with the
//   match: if the HUD says 12 seconds to arrival, the helicopter is 12 seconds
//   out. The map places one; it spawns nothing and blocks nothing (no collision:
//   a shot at it is a shot at the sky).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamHelicopter.generated.h"

class UOneBrahamExtractionComponent;
class USpotLightComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamHelicopter : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamHelicopter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Where it comes in from, relative to the pad: east over the City, high. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Helicopter")
	FVector ApproachOffset = FVector(16000.f, -9000.f, 4000.f);

	/** Where it hovers, relative to the pad: high enough that the winch line reads. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Helicopter")
	FVector HoverOffset = FVector(0.f, 0.f, 1800.f);

	/** Seconds the climb-out takes after the match ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Helicopter")
	float DepartSeconds = 14.f;

	/** Rotor speed, degrees per second. Fast enough to blur, not to strobe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Helicopter")
	float RotorDegreesPerSecond = 1440.f;

	/**
	 * The pose for an extraction state, as a pure function of time, so a test
	 * can check the approach ends exactly at the hover point.
	 *
	 * @param Alpha  0-1 through the approach (Arriving) or the departure.
	 */
	static FTransform ComputePose(EOneBrahamExtractionState State, float Alpha, double TimeSeconds,
		const FVector& Pad, const FVector& InApproachOffset, const FVector& InHoverOffset);

protected:
	UOneBrahamExtractionComponent* FindExtraction() const;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Body = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> TailBoom = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Rotor = nullptr;

	/** The winch line to the pad, scaled to its length while a squad holds. */
	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> WinchLine = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<USpotLightComponent> Searchlight = nullptr;

	/** Aviation lighting: steady red to port, steady green to starboard, a
	 *  flashing red anti-collision beacon on the belly and a white tail strobe.
	 *  The first thing a squad on the Skydeck sees of its ride, through rain. */
	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<class UPointLightComponent> PortLight = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<class UPointLightComponent> StarboardLight = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<class UPointLightComponent> AntiCollisionBeacon = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<class UPointLightComponent> TailStrobe = nullptr;

	/** Flashes per second for the beacon and the strobe (aviation rules: 40-100 a minute). */
	static constexpr float BeaconHz = 1.f;

	/** The state last seen, and the local time it was entered, for the departure clock. */
	EOneBrahamExtractionState LastState = EOneBrahamExtractionState::Inactive;
	double StateEnteredTime = 0.0;

	/** Where the departure starts from: wherever it was when the match ended. */
	FVector DepartFrom = FVector::ZeroVector;
};
