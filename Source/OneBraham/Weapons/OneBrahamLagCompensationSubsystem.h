// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLagCompensationSubsystem.h
// ---------------------------------------------------------------------------
// Server-side hit rewind: a shot is tested against where the targets were on
// the shooter's screen, not where they are on the server when the RPC lands.
//
// THE TIME BEING REWOUND TO
//   The server's state at time T reaches a client at T + RTT/2. The client
//   fires at what it sees, and that shot reaches the server at T + RTT. So the
//   shooter was aiming at the world as it was one full round trip ago, plus
//   the time simulated proxies spend smoothing toward their latest position
//   (ob.LagComp.ExtraMs). Half the round trip, which is what this replaced,
//   leaves every moving target half a ping ahead of the crosshair.
//
// WHAT MOVES
//   Each character's body mesh, which carries the per-bone physics bodies on
//   the OneBrahamHitbox profile that bullets register on. It is moved to its
//   position at the rewind time, interpolated between the two samples either
//   side, and put back once the trace is done, in the same frame: nothing is
//   replicated in between, so no client ever sees it. The capsule is not
//   moved (moving it would fire the access-zone and extraction overlaps); the
//   trace ignores the present-day capsules of rewound characters instead,
//   through AddIgnoredComponents.
//
//   The pose is the current pose at the old position. Rewinding every bone is
//   possible from the same history and costs a component-space transform
//   array per character per sample; at these ranges the position is what
//   decides a hit, and the pose is within a few centimetres.
//
// WHAT IT REPLACES
//   A snapshot list on every weapon component, each one sampling every player
//   thirty times a second (n squared), whose rewind chose a snapshot and then
//   moved nothing: the trace always ran against the present.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamLagCompensationSubsystem.generated.h"

class AOneBrahamCharacter;
class UPrimitiveComponent;
class USkeletalMeshComponent;

/**
 * Hit rewind for the Weapon channel, server only.
 *
 * Usage (one shot):
 *   FOneBrahamScopedRewind Rewind(World, ShooterCharacter, ShooterPingMs);
 *   ... traces, with Rewind.AddIgnoredComponents(Params) on each ...
 *   (the destructor restores everyone)
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamLagCompensationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Moves every character but Shooter to where it was ShooterPingMs (plus
	 *  smoothing) ago. Returns the server time rewound to, or a negative number
	 *  when nothing was moved (off, no history, or no ping). Must be paired with
	 *  EndRewind before the frame ends. */
	float BeginRewind(const AOneBrahamCharacter* Shooter, float ShooterPingMs);

	/** Puts every mesh moved by BeginRewind back. */
	void EndRewind();

	/** Present-day capsules of the rewound characters, for the trace to ignore. */
	void AddIgnoredComponents(struct FCollisionQueryParams& Params) const;

	/** True between BeginRewind and EndRewind. */
	bool IsRewound() const { return Moved.Num() > 0; }

	/** Samples held; for tests and ob.LagComp.Dump. */
	int32 GetSampleCount() const { return History.Num(); }

private:
	struct FCharacterSample
	{
		TWeakObjectPtr<AOneBrahamCharacter> Character;
		FVector MeshLocation = FVector::ZeroVector;
		FQuat MeshRotation = FQuat::Identity;
	};

	struct FWorldSample
	{
		double ServerTime = 0.0;
		TArray<FCharacterSample> Characters;
	};

	struct FMovedMesh
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		TWeakObjectPtr<UPrimitiveComponent> Capsule;
		FVector RelativeLocation = FVector::ZeroVector;
		FRotator RelativeRotation = FRotator::ZeroRotator;
	};

	void CaptureSample();

	/** Oldest first. A ring would avoid the shuffles; at 30 Hz for a second it
	 *  is thirty entries, and RemoveAt(0) on thirty is not where a frame goes. */
	TArray<FWorldSample> History;

	TArray<FMovedMesh> Moved;

	double LastSampleTime = -1.0;
};

/** RAII wrapper: rewinds in the constructor, restores in the destructor, so an
 *  early return in a trace function can never leave a body displaced. */
struct ONEBRAHAM_API FOneBrahamScopedRewind
{
	FOneBrahamScopedRewind(UWorld* World, const AOneBrahamCharacter* Shooter, float ShooterPingMs);
	~FOneBrahamScopedRewind();

	FOneBrahamScopedRewind(const FOneBrahamScopedRewind&) = delete;
	FOneBrahamScopedRewind& operator=(const FOneBrahamScopedRewind&) = delete;

	/** The server time rewound to, or negative if nothing moved. */
	float GetRewindTime() const { return RewindTime; }

private:
	TWeakObjectPtr<UOneBrahamLagCompensationSubsystem> Subsystem;
	float RewindTime = -1.f;
};
