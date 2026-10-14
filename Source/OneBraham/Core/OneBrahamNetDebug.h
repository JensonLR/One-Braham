// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamNetDebug.h
// ---------------------------------------------------------------------------
// `ob.NetDebug 1` - the per-actor replication cost overlay.
//
// WHAT IT SHOWS, AND WHY IT IS THIS AND NOT SOMETHING ELSE
//   A server's bandwidth decision is made per actor, once per tick, by
//   AActor::GetNetPriority(): the scheduler sorts by that number and sends the
//   actors that win. It is therefore the honest "replication cost" of an actor -
//   not bytes, which the engine does not expose per actor anywhere public, but
//   the weight the engine actually uses to decide who gets the wire.
//
//   So the overlay lists the replicated actors near the local view, ordered by
//   the priority the engine will give them, with the update rate and the cull
//   distance that produced it, and the flags that change the answer
//   (bAlwaysRelevant, bOnlyRelevantToOwner, dormancy). A developer looking at a
//   bandwidth report can see the twelve actors that are eating it rather than a
//   total.
//
// WHERE IT LIVES
//   A world subsystem with a tick, because it has to be torn down with the
//   world, it must not exist in the editor's preview worlds, and it must be
//   free when the CVar is off. The CVar is registered by the subsystem's own
//   constructor path, so nothing has to be initialised in the right order.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamNetDebug.generated.h"

/** One line of the overlay: an actor and what the scheduler thinks of it. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamNetDebugEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	FString ClassName;

	/** What AActor::GetNetPriority returns for this actor right now. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	float NetPriority = 0.f;

	/** Updates a second the replication graph allows this actor. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	float NetUpdateFrequency = 0.f;

	/** Distance beyond which the actor is not considered, in metres. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	float CullDistanceMeters = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	bool bAlwaysRelevant = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	bool bOnlyRelevantToOwner = false;

	/** The actor's designed dormancy: DORM_Never means it is a candidate every
	 *  tick, DORM_DormantAll means the game has to wake it deliberately. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Debug")
	FString Dormancy;
};

/**
 * The `ob.NetDebug` overlay.
 *
 * Enabled by the CVar, which is declared in the .cpp. It is deliberately quiet
 * when off: no tick work beyond a single variable read, no screen messages, no
 * allocations.
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamNetDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Whether the overlay is on. Mirrors the CVar. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Debug")
	static bool IsOverlayEnabled();

	/** The current overlay contents, most expensive first. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Debug")
	const TArray<FOneBrahamNetDebugEntry>& GetEntries() const { return Entries; }

	/** Actors considered last refresh. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Debug")
	int32 GetConsideredActorCount() const { return ConsideredCount; }

	/** How many actors in the world replicate at all. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Debug")
	int32 GetReplicatedActorCount() const { return ReplicatedCount; }

	/** Lines drawn on screen, the first one being the header. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Debug")
	TArray<FString> BuildOverlayLines() const;

private:
	/** Rebuilds Entries from the world. Called at most once a second. */
	void Refresh();

	/** The last refresh's list, most expensive first. */
	UPROPERTY()
	TArray<FOneBrahamNetDebugEntry> Entries;

	int32 ConsideredCount = 0;
	int32 ReplicatedCount = 0;

	/** World seconds of the last refresh, so this runs at 1 Hz rather than 60. */
	float LastRefreshTime = -1.f;

	/** The screen message key, so the overlay replaces itself instead of
	 *  scrolling: a debug overlay that pushes the log off the screen is worse
	 *  than no overlay. */
	static constexpr int32 OverlayMessageKey = 0x0B1A;
};
