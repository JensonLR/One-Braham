// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamExtractionPad.h
// ---------------------------------------------------------------------------
// The winch pad on the Skydeck: the volume a squad has to hold for thirty
// seconds, and the searchlight that makes it findable from the stairwell.
//
// TWO JOBS
//   1. It tells the extraction component where the pad is. The component decides
//      the win condition by measuring players' distance to `HelipadLocation`, and
//      until this actor existed that location was whatever the component's
//      default was - the winch could be called, but "standing on the pad" would
//      have been measured against a point nobody had placed. `HandleRadiusCm` and
//      `ExtractionZoneRadiusCm` in the match settings are the same 900 cm, and
//      the painted square is deliberately smaller than the wire: the paint is a
//      target, the wire is the rule.
//   2. It is the presentation of the arrival. The component knows there is a
//      helicopter and how close it is; this actor is what a player sees: a
//      searchlight that starts sweeping when the winch is called, hardens as the
//      helicopter closes, turns red when the pad is contested, and goes dark when
//      the attempt fails.
//
// AUTHORITY
//   Registration is server-only (the component rejects it elsewhere). The light
//   reads replicated state, so every machine sweeps the same beam.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamExtractionPad.generated.h"

class UBoxComponent;
class USpotLightComponent;
class UOneBrahamExtractionComponent;
struct FLifetimeProperty;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamExtractionPad : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamExtractionPad();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Placement
	// ======================================================================
	/** Stable id for the builder and the logs: "Skydeck_Winch_Pad". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	FName PadId = NAME_None;

	/** The hold radius reported to the extraction component. Must agree with
	 *  `UOneBrahamMatchSettingsAsset::ExtractionZoneRadiusCm` (900 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float HoldRadiusCm = 900.f;

	/** Height of the reported zone: a player on the deck below the pad is out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float ZoneHeightCm = 300.f;

	/** Metres [sic] above the pad the searchlight hangs. Six, on the beacon mast. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float SearchlightHeightCm = 600.f;

	/** Degrees either side of the pad centre that the beam sweeps while inbound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float SweepDegrees = 55.f;

	/** Sweeps per second while the helicopter is inbound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float SweepHz = 0.35f;

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Winch")
	bool IsRegisteredWithWinch() const { return bRegistered; }

	/** True when the given world location counts as on the pad. Server or client:
	 *  it only reads replicated state and this actor's own placement. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Winch")
	bool IsOnPad(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Winch")
	UOneBrahamExtractionComponent* GetExtraction() const;

protected:
	// --- components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The winch zone: query-only, blocks nothing, visible in the editor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UBoxComponent> PadZone = nullptr;

	/** The arrival. Dark until somebody calls, then a beam with a mind of its own. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USpotLightComponent> Searchlight = nullptr;

	// --- state ------------------------------------------------------------
	/** Server: true once the pad has told the component where it is. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Winch")
	bool bRegistered = false;

	/** Server: retry accumulator for the case where the game state was not awake. */
	float RegisterRetrySeconds = 0.f;

	// --- internals --------------------------------------------------------
	/** Server: reports the pad's location and radius to the extraction component. */
	void RegisterWithWinch();

	/** Local: the beam, from replicated state. */
	void ApplyBeam();
};
