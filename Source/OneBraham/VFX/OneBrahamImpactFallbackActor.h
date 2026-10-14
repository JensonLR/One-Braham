// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamImpactFallbackActor.h
// ---------------------------------------------------------------------------
// The effect that exists before the effect exists.
//
// A serious shooter lives or dies on impact feedback: the player must see where
// their rounds are going. ONE BRAHAM's shipping impacts are Niagara systems, but
// the project has to be playable, reviewable and testable at any point in the
// content pipeline, so every impact also has a runtime-built stand-in:
//
//   * a short, bright point light oriented along the surface normal (the flash)
//   * two to five small physics crumbs (the debris) using engine primitives
//   * an optional decal (the hole)
//   * a dust card for soft surfaces
//
// It is spawned by UOneBrahamVFXStatics when the Niagara system is missing, and
// it is also the mandated minimum-spec path on the "Low" quality preset, where
// it costs a fraction of a GPU millisecond. It destroys itself; nobody owns it.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "GameFramework/Actor.h"
#include "OneBrahamImpactFallbackActor.generated.h"

class UDecalComponent;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(NotPlaceable, Transient)
class ONEBRAHAM_API AOneBrahamImpactFallbackActor : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamImpactFallbackActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Spawns a fallback effect for an impact.
	 *
	 * @param World      The world to spawn into.
	 * @param Impact     Replicated impact report (location, normal, surface).
	 * @param Intensity  0..1 from the user's quality preset. Controls crumb count
	 *                   and light radius, and is the reason the minimum spec can
	 *                   run the Skydeck fight at 60 fps.
	 * @param bExplosive True for a blast (bigger flash, longer life, more debris).
	 */
	static AOneBrahamImpactFallbackActor* SpawnForImpact(UWorld* World, const FOneBrahamImpactInfo& Impact, float Intensity, bool bExplosive = false);

	/** Builds the components for an impact. Called by SpawnForImpact. */
	void Initialise(const FOneBrahamImpactInfo& Impact, float Intensity, bool bExplosive);

protected:
	UPROPERTY(VisibleAnywhere, Category = "One Braham|VFX")
	TObjectPtr<UPointLightComponent> Flash = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "One Braham|VFX")
	TObjectPtr<UDecalComponent> Hole = nullptr;

	/** Crumbs live under this scene component so they can be released together. */
	UPROPERTY(VisibleAnywhere, Category = "One Braham|VFX")
	TObjectPtr<USceneComponent> DebrisRoot = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Crumbs;

	/** Total life of the actor in seconds. */
	float LifeSeconds = 0.4f;

	/** Peak light intensity, ramped down over LifeSeconds. */
	float PeakLightIntensity = 6000.f;

	float Elapsed = 0.f;
};
