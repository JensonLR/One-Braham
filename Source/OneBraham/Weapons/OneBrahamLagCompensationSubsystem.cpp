// Copyright (c) One Braham. All Rights Reserved.

#include "Weapons/OneBrahamLagCompensationSubsystem.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	int32 GOneBrahamLagComp = 1;
	FAutoConsoleVariableRef CVarOneBrahamLagComp(
		TEXT("ob.LagComp"),
		GOneBrahamLagComp,
		TEXT("Server hit rewind. 1 on (default), 0 traces against the present."),
		ECVF_Cheat);

	float GOneBrahamLagCompMaxMs = 250.f;
	FAutoConsoleVariableRef CVarOneBrahamLagCompMaxMs(
		TEXT("ob.LagComp.MaxMs"),
		GOneBrahamLagCompMaxMs,
		TEXT("The most a shot is rewound, in milliseconds. A shooter on a worse connection leads targets by the rest."),
		ECVF_Default);

	float GOneBrahamLagCompExtraMs = 50.f;
	FAutoConsoleVariableRef CVarOneBrahamLagCompExtraMs(
		TEXT("ob.LagComp.ExtraMs"),
		GOneBrahamLagCompExtraMs,
		TEXT("Added to the round trip: about half the simulated-proxy smoothing time, which is how far behind the "
			 "latest update a remote character is drawn."),
		ECVF_Default);

	/** Thirty samples a second: a sprinting character covers about 20 cm between
	 *  them, and the interpolation between two samples covers the rest. */
	constexpr double SampleInterval = 1.0 / 30.0;

	/** Kept a little past the cap so a capped rewind always has a sample either side. */
	double HistorySeconds()
	{
		return FMath::Max(0.1, static_cast<double>(GOneBrahamLagCompMaxMs + GOneBrahamLagCompExtraMs) / 1000.0 + 0.2);
	}
}

bool UOneBrahamLagCompensationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only; and only the server ever rewinds, but a listen host is
	// both, so the netmode is checked per call rather than here, where it is not
	// yet known.
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

bool UOneBrahamLagCompensationSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return GOneBrahamLagComp != 0 && World != nullptr && World->GetNetMode() != NM_Client;
}

TStatId UOneBrahamLagCompensationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamLagCompensationSubsystem, STATGROUP_Tickables);
}

void UOneBrahamLagCompensationSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (LastSampleTime >= 0.0 && Now - LastSampleTime < SampleInterval)
	{
		return;
	}

	LastSampleTime = Now;
	CaptureSample();
}

void UOneBrahamLagCompensationSubsystem::CaptureSample()
{
	UWorld* World = GetWorld();

	FWorldSample& Sample = History.AddDefaulted_GetRef();
	Sample.ServerTime = World->GetTimeSeconds();

	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		AOneBrahamCharacter* Character = *It;
		const USkeletalMeshComponent* Mesh = Character != nullptr ? Character->GetMesh() : nullptr;
		if (Mesh == nullptr || Character->IsPendingKillPending())
		{
			continue;
		}

		FCharacterSample& Entry = Sample.Characters.AddDefaulted_GetRef();
		Entry.Character = Character;
		Entry.MeshLocation = Mesh->GetComponentLocation();
		Entry.MeshRotation = Mesh->GetComponentQuat();
	}

	const double Oldest = Sample.ServerTime - HistorySeconds();
	int32 Expired = 0;
	while (Expired < History.Num() - 1 && History[Expired].ServerTime < Oldest)
	{
		++Expired;
	}

	if (Expired > 0)
	{
		History.RemoveAt(0, Expired, EAllowShrinking::No);
	}
}

float UOneBrahamLagCompensationSubsystem::BeginRewind(const AOneBrahamCharacter* Shooter, float ShooterPingMs)
{
	EndRewind();

	UWorld* World = GetWorld();
	if (GOneBrahamLagComp == 0 || World == nullptr || World->GetNetMode() == NM_Client || History.Num() == 0)
	{
		return -1.f;
	}

	// A listen host's own shots, and bots, are already looking at the present.
	if (ShooterPingMs <= 0.f)
	{
		return -1.f;
	}

	const double RewindSeconds = FMath::Min(ShooterPingMs + GOneBrahamLagCompExtraMs, GOneBrahamLagCompMaxMs) / 1000.0;
	const double TargetTime = World->GetTimeSeconds() - RewindSeconds;

	// The two samples either side of the target. Before the oldest sample the
	// oldest is used as is; after the newest there is nothing to rewind.
	int32 After = INDEX_NONE;
	for (int32 Index = 0; Index < History.Num(); ++Index)
	{
		if (History[Index].ServerTime >= TargetTime)
		{
			After = Index;
			break;
		}
	}

	if (After == INDEX_NONE)
	{
		return -1.f;
	}

	const FWorldSample& Later = History[After];
	const FWorldSample& Earlier = History[FMath::Max(0, After - 1)];
	const double Span = Later.ServerTime - Earlier.ServerTime;
	const float Alpha = Span > KINDA_SMALL_NUMBER
		? static_cast<float>(FMath::Clamp((TargetTime - Earlier.ServerTime) / Span, 0.0, 1.0))
		: 1.f;

	for (const FCharacterSample& LaterEntry : Later.Characters)
	{
		AOneBrahamCharacter* Character = LaterEntry.Character.Get();
		if (Character == nullptr || Character == Shooter)
		{
			continue;
		}

		USkeletalMeshComponent* Mesh = Character->GetMesh();
		if (Mesh == nullptr)
		{
			continue;
		}

		// The same character in the earlier sample; a character that had not
		// spawned yet is placed where it first appears.
		FVector Location = LaterEntry.MeshLocation;
		FQuat Rotation = LaterEntry.MeshRotation;
		for (const FCharacterSample& EarlierEntry : Earlier.Characters)
		{
			if (EarlierEntry.Character == LaterEntry.Character)
			{
				Location = FMath::Lerp(EarlierEntry.MeshLocation, LaterEntry.MeshLocation, Alpha);
				Rotation = FQuat::Slerp(EarlierEntry.MeshRotation, LaterEntry.MeshRotation, Alpha);
				break;
			}
		}

		FMovedMesh& MovedMesh = Moved.AddDefaulted_GetRef();
		MovedMesh.Mesh = Mesh;
		MovedMesh.Capsule = Character->GetCapsuleComponent();
		MovedMesh.RelativeLocation = Mesh->GetRelativeLocation();
		MovedMesh.RelativeRotation = Mesh->GetRelativeRotation();

		// Teleport: no sweep, and the physics bodies jump rather than being
		// dragged through whatever lies between.
		Mesh->SetWorldLocationAndRotation(Location, Rotation, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	}

	return static_cast<float>(TargetTime);
}

void UOneBrahamLagCompensationSubsystem::EndRewind()
{
	for (const FMovedMesh& MovedMesh : Moved)
	{
		if (USkeletalMeshComponent* Mesh = MovedMesh.Mesh.Get())
		{
			Mesh->SetRelativeLocationAndRotation(MovedMesh.RelativeLocation, MovedMesh.RelativeRotation,
				/*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	Moved.Reset();
}

void UOneBrahamLagCompensationSubsystem::AddIgnoredComponents(FCollisionQueryParams& Params) const
{
	for (const FMovedMesh& MovedMesh : Moved)
	{
		if (const UPrimitiveComponent* Capsule = MovedMesh.Capsule.Get())
		{
			Params.AddIgnoredComponent(Capsule);
		}
	}
}

// ==========================================================================
//  FOneBrahamScopedRewind
// ==========================================================================
FOneBrahamScopedRewind::FOneBrahamScopedRewind(UWorld* World, const AOneBrahamCharacter* Shooter, float ShooterPingMs)
{
	if (World == nullptr)
	{
		return;
	}

	if (UOneBrahamLagCompensationSubsystem* LagComp = World->GetSubsystem<UOneBrahamLagCompensationSubsystem>())
	{
		Subsystem = LagComp;
		RewindTime = LagComp->BeginRewind(Shooter, ShooterPingMs);
	}
}

FOneBrahamScopedRewind::~FOneBrahamScopedRewind()
{
	if (UOneBrahamLagCompensationSubsystem* LagComp = Subsystem.Get())
	{
		LagComp->EndRewind();
	}
}
