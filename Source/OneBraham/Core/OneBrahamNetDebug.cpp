// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamNetDebug.h"

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "OneBraham.h"

namespace
{
	/** `ob.NetDebug 1` - on/off. Read every tick, written by the console. */
	static int32 GOneBrahamNetDebug = 0;

	FAutoConsoleVariableRef CVarOneBrahamNetDebug(
		TEXT("ob.NetDebug"),
		GOneBrahamNetDebug,
		TEXT("Draw the per-actor replication cost overlay (0 = off, 1 = on).\n")
		TEXT("Lists the replicated actors near the local view in the order the\n")
		TEXT("replication scheduler will spend bandwidth on them, from\n")
		TEXT("AActor::GetNetPriority, with their update rate and cull distance."),
		ECVF_Cheat);

	/** How many actors the overlay lists. */
	static int32 GOneBrahamNetDebugLines = 12;

	FAutoConsoleVariableRef CVarOneBrahamNetDebugLines(
		TEXT("ob.NetDebug.Lines"),
		GOneBrahamNetDebugLines,
		TEXT("How many actors the ob.NetDebug overlay lists (default 12)."),
		ECVF_Cheat);

	/** The radius, in centimetres, around the local view that is considered. */
	static float GOneBrahamNetDebugRadius = 4000.f;

	FAutoConsoleVariableRef CVarOneBrahamNetDebugRadius(
		TEXT("ob.NetDebug.Radius"),
		GOneBrahamNetDebugRadius,
		TEXT("How far around the local view ob.NetDebug looks, in centimetres."),
		ECVF_Cheat);
}

bool UOneBrahamNetDebugSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only. A preview world in the editor has no net driver
	// and no players, and an overlay that draws there is noise.
	const UWorld* World = Cast<UWorld>(Outer);

	return World != nullptr && World->IsGameWorld();
}

bool UOneBrahamNetDebugSubsystem::IsTickable() const
{
	return IsOverlayEnabled();
}

TStatId UOneBrahamNetDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamNetDebugSubsystem, STATGROUP_Tickables);
}

bool UOneBrahamNetDebugSubsystem::IsOverlayEnabled()
{
	return GOneBrahamNetDebug != 0;
}

void UOneBrahamNetDebugSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();

	if (World == nullptr || !IsOverlayEnabled())
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Once a second is enough for a bandwidth question, and it keeps the
	// GetNetPriority calls (one per replicated actor) off the frame budget.
	if (LastRefreshTime >= 0.f && Now - LastRefreshTime < 1.f)
	{
		// Redraw at the refresh rate but with the previous content, so the
		// overlay does not flicker between refreshes.
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(OverlayMessageKey, 1.5f,
				FColor(120, 200, 255), FString::Join(BuildOverlayLines(), TEXT("\n")));
		}

		return;
	}

	LastRefreshTime = Now;
	Refresh();

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(OverlayMessageKey, 1.5f,
			FColor(120, 200, 255), FString::Join(BuildOverlayLines(), TEXT("\n")));
	}
}

void UOneBrahamNetDebugSubsystem::Refresh()
{
	UWorld* World = GetWorld();

	Entries.Reset();
	ConsideredCount = 0;
	ReplicatedCount = 0;

	if (World == nullptr)
	{
		return;
	}

	// The local view: on a listen server that is the host's camera, on a client
	// the local player's. When there is neither (a dedicated server with no
	// spectator) the overlay still works, from the world origin.
	FVector ViewLocation = FVector::ZeroVector;
	FVector ViewDirection = FVector::ForwardVector;
	AActor* Viewer = nullptr;
	AActor* ViewTarget = nullptr;

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator ViewRotation = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		ViewDirection = ViewRotation.Vector();

		// The viewer is the net object owned by the client whose bandwidth is
		// being spent; the view target is what that viewer is looking at.
		Viewer = PC;
		ViewTarget = PC->GetViewTarget();
	}

	const float RadiusSquared = FMath::Square(GOneBrahamNetDebugRadius);
	const float TimeSinceLastTick = 1.f / 60.f;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;

		if (Actor == nullptr || !Actor->GetIsReplicated())
		{
			continue;
		}

		++ReplicatedCount;

		if (Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Actor->GetActorLocation(), ViewLocation);

		// Always-relevant actors are considered from anywhere: they are the ones
		// that cost the most, and hiding them because the developer is in the
		// wrong part of the map would hide the answer.
		if (!Actor->bAlwaysRelevant && DistanceSquared > RadiusSquared)
		{
			continue;
		}

		++ConsideredCount;

		FOneBrahamNetDebugEntry Entry;
		Entry.ActorName = Actor->GetName();
		Entry.ClassName = Actor->GetClass() != nullptr ? Actor->GetClass()->GetName() : TEXT("?");
		Entry.NetUpdateFrequency = Actor->GetNetUpdateFrequency();
		Entry.CullDistanceMeters = Actor->GetNetCullDistanceSquared() > 0.f
			? FMath::Sqrt(Actor->GetNetCullDistanceSquared()) / 100.f
			: 0.f;
		Entry.bAlwaysRelevant = Actor->bAlwaysRelevant;
		Entry.bOnlyRelevantToOwner = Actor->bOnlyRelevantToOwner;
		// The actor's designed dormancy, not a live query: AActor::IsDormant is
		// not public API in 5.5, and the design value is the one a developer can
		// do something about.
		switch (Actor->NetDormancy)
		{
		case DORM_Never:          Entry.Dormancy = TEXT("never");           break;
		case DORM_Awake:          Entry.Dormancy = TEXT("awake");           break;
		case DORM_DormantAll:     Entry.Dormancy = TEXT("dormant-all");     break;
		case DORM_DormantPartial: Entry.Dormancy = TEXT("dormant-partial"); break;
		case DORM_Initial:        Entry.Dormancy = TEXT("initial");         break;
		default:                  Entry.Dormancy = TEXT("?");               break;
		}

		// The engine's own answer, with the arguments the replication graph
		// passes it: viewer, view target, the channel (none here), the time
		// since the actor was last replicated, and whether the viewer is on a
		// low-bandwidth connection.
		Entry.NetPriority = Actor->GetNetPriority(
			ViewLocation, ViewDirection, Viewer, ViewTarget,
			/*InChannel=*/nullptr, TimeSinceLastTick, /*bLowBandwidth=*/false);

		Entries.Add(MoveTemp(Entry));
	}

	// Most expensive first: the scheduler's order is the point of the overlay.
	Entries.Sort([](const FOneBrahamNetDebugEntry& A, const FOneBrahamNetDebugEntry& B)
	{
		return A.NetPriority > B.NetPriority;
	});

	if (Entries.Num() > FMath::Max(1, GOneBrahamNetDebugLines))
	{
		Entries.SetNum(FMath::Max(1, GOneBrahamNetDebugLines));
	}
}

TArray<FString> UOneBrahamNetDebugSubsystem::BuildOverlayLines() const
{
	TArray<FString> Lines;

	UWorld* World = GetWorld();
	const bool bServer = World != nullptr && World->GetNetMode() != NM_Client;

	Lines.Add(FString::Printf(
		TEXT("ob.NetDebug - %s | replicated: %d | in range: %d | showing: %d"),
		bServer ? TEXT("SERVER") : TEXT("CLIENT"),
		ReplicatedCount, ConsideredCount, Entries.Num()));

	if (Entries.Num() == 0)
	{
		Lines.Add(TEXT("  no replicated actors in range"));
		return Lines;
	}

	Lines.Add(TEXT("  priority  upd/s  cull      class / name"));

	for (const FOneBrahamNetDebugEntry& Entry : Entries)
	{
		FString Flags;

		if (Entry.bAlwaysRelevant)
		{
			Flags += TEXT(" always");
		}

		if (Entry.bOnlyRelevantToOwner)
		{
			Flags += TEXT(" owner-only");
		}

		if (!Entry.Dormancy.IsEmpty())
		{
			Flags += FString::Printf(TEXT(" dormancy=%s"), *Entry.Dormancy);
		}

		const FString Cull = Entry.CullDistanceMeters > 0.f
			? FString::Printf(TEXT("%.0fm"), Entry.CullDistanceMeters)
			: FString(TEXT("never"));

		Lines.Add(FString::Printf(TEXT("  %8.2f  %5.1f  %-8s  %s%s"),
			Entry.NetPriority, Entry.NetUpdateFrequency, *Cull, *Entry.ClassName, *Flags));
	}

	return Lines;
}
