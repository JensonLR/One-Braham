// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSpawnPoint.h
// ---------------------------------------------------------------------------
// A single multiplayer-safe spawn. Every spawn point in the tower is one of
// these; the GameMode never invents a location, it only picks from this list.
//
// Level design rules (enforced by the OneBraham.Map.SpawnSafety automation test):
//   * No spawn point may have line of sight to another team's spawn point.
//   * No spawn point may be inside geometry (a 60 cm capsule sweep must be clear).
//   * Squad spawn clusters must be 8-14 m apart: together but not stacked, so a
//     single frag cannot open the round.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamSpawnPoint.generated.h"

class UBillboardComponent;
class UArrowComponent;
class UBoxComponent;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamSpawnPoint();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Where the player is placed. Uses the arrow component's transform. */
	UFUNCTION(BlueprintPure, Category = "Spawn")
	FTransform GetSpawnTransform() const;

	/** 0 = plaza, 1 = lobby, 2 = mezzanine, 3-8 offices, 9-11 server, 12-14 exec, 15 skydeck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0", ClampMax = "15"))
	int32 FloorIndex = 1;

	/** OB.Access.Zone.* tag so the GameMode can prefer spawns in a given zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (Categories = "OB.Access"))
	FGameplayTag ZoneTag;

	/**
	 * Restricts the spawn to a team. None = any team (used for FIRE DRILL
	 * neutral points and for the Skydeck late-match spawns).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	EOneBrahamTeam TeamAffinity = EOneBrahamTeam::None;

	/** Higher weight = more likely to be chosen among valid candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/**
	 * Squad spawn cluster id. All members of a squad spawn on the same cluster
	 * id (different points), which is how squads stay together without stacking.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 SquadClusterId = -1;

	/** Disables the point after use for N seconds; 0 = reusable immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	/** A used spawn rests this long, so a killer cannot watch one point and
	 *  catch the next respawn on it. */
	float CooldownSeconds = 8.f;

	/** A point that is not enabled cannot be chosen (locked floors, pre-breach areas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bEnabled = true;

	/** Set by the GameMode; spawn points never cool down faster than the respawn delay. */
	UPROPERTY(BlueprintReadOnly, Category = "Spawn")
	float LastUsedTime = -1000.f;

	UFUNCTION(BlueprintPure, Category = "Spawn")
	bool IsAvailable(float CurrentTime) const;

	/** Runs the 60 cm capsule clearance test against the world. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Spawn")
	bool ValidateClearance() const;

	/** Called by the GameMode when this point is used. */
	void MarkUsed(float CurrentTime) { LastUsedTime = CurrentTime; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawn")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawn")
	TObjectPtr<UArrowComponent> FacingArrow;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif
};
