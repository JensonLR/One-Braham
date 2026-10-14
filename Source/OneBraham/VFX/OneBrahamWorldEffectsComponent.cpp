// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamWorldEffectsComponent.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "VFX/OneBrahamVFXStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

UOneBrahamWorldEffectsComponent::UOneBrahamWorldEffectsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

void UOneBrahamWorldEffectsComponent::BeginPlay()
{
	Super::BeginPlay();

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC == nullptr || !PC->IsLocalController() || IsRunningDedicatedServer())
	{
		SetComponentTickEnabled(false);
		return;
	}

	TubeCountdown = FMath::FRandRange(TubeMinGapSeconds * 0.5f, TubeMaxGapSeconds * 0.5f);
}

void UOneBrahamWorldEffectsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetRainActive(false);
	Super::EndPlay(EndPlayReason);
}

UOneBrahamAudioManagerComponent* UOneBrahamWorldEffectsComponent::GetAudio() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UOneBrahamAudioManagerComponent>() : nullptr;
}

int32 UOneBrahamWorldEffectsComponent::GetViewedFloor() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC == nullptr)
	{
		return INDEX_NONE;
	}
	if (const AOneBrahamCharacter* Viewed = Cast<AOneBrahamCharacter>(PC->GetViewTarget()))
	{
		return Viewed->GetCurrentFloorIndex();
	}
	if (const AOneBrahamCharacter* Own = Cast<AOneBrahamCharacter>(PC->GetPawn()))
	{
		return Own->GetCurrentFloorIndex();
	}
	// A free camera: wherever the camera is.
	return PC->PlayerCameraManager
		? OneBrahamConstants::FloorIndexForZ(static_cast<float>(PC->PlayerCameraManager->GetCameraLocation().Z))
		: INDEX_NONE;
}

void UOneBrahamWorldEffectsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EvaluateAccumulator += DeltaTime;
	if (EvaluateAccumulator >= 0.5f)
	{
		Evaluate();
		TubeCountdown -= EvaluateAccumulator;
		EvaluateAccumulator = 0.f;
	}

	if (bRainActive)
	{
		const APlayerController* PC = Cast<APlayerController>(GetOwner());
		if (PC != nullptr && PC->PlayerCameraManager != nullptr)
		{
			const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
			if (RainSystem != nullptr)
			{
				RainSystem->SetWorldLocation(Camera);
			}
			else
			{
				UpdateRainField(DeltaTime, Camera);
			}
		}
	}
}

void UOneBrahamWorldEffectsComponent::Evaluate()
{
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	const int32 Floor = GetViewedFloor();

	// Wet surfaces follow the match's weather everywhere at once: the surface
	// master multiplies this by each surface's own WetnessResponse.
	const float Wetness = GS != nullptr && GS->bRaining ? 1.f : 0.f;
	if (Wetness != AppliedWetness)
	{
		if (!WetnessCollection.IsValid())
		{
			WetnessCollection = LoadObject<UMaterialParameterCollection>(nullptr,
				TEXT("/Game/OneBraham/Materials/MPC_OneBraham_Global.MPC_OneBraham_Global"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (WetnessCollection.IsValid())
		{
			UKismetMaterialLibrary::SetScalarParameterValue(this, WetnessCollection.Get(), TEXT("RainWetness"), Wetness);
			AppliedWetness = Wetness;
		}
	}

	// Rain: outdoors, in a wet match.
	const bool bOutdoors = Floor == OneBrahamConstants::FloorPlaza || Floor == OneBrahamConstants::FloorSkydeck;
	SetRainActive(GS != nullptr && GS->bRaining && bOutdoors);

	// The Tube: felt on the plaza and in the lobby, heard on the mezzanine.
	if (TubeCountdown <= 0.f)
	{
		TubeCountdown = FMath::FRandRange(TubeMinGapSeconds, TubeMaxGapSeconds);
		if (Floor != INDEX_NONE && Floor <= OneBrahamConstants::FloorMezzanine)
		{
			if (UOneBrahamAudioManagerComponent* Audio = GetAudio())
			{
				Audio->PlaySound2D(FName(TEXT("SFX_Tube_Rumble")), Floor < OneBrahamConstants::FloorMezzanine ? 1.f : 0.45f);
			}
		}
	}
}

void UOneBrahamWorldEffectsComponent::SetRainActive(bool bActive)
{
	if (bActive == bRainActive)
	{
		return;
	}
	bRainActive = bActive;

	if (UOneBrahamAudioManagerComponent* Audio = GetAudio())
	{
		Audio->SetWeatherAudio(bActive);
	}

	if (!bActive)
	{
		if (RainSystem != nullptr)
		{
			RainSystem->DestroyComponent();
			RainSystem = nullptr;
		}
		if (RainStreaks != nullptr)
		{
			RainStreaks->DestroyComponent();
			RainStreaks = nullptr;
		}
		StreakOffsets.Reset();
		return;
	}

	// The authored system when there is one.
	if (UNiagaraSystem* System = UOneBrahamVFXStatics::LoadEffectSystem(EOneBrahamVFXEffect::Rain))
	{
		RainSystem = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, FVector::ZeroVector, FRotator::ZeroRotator,
			FVector(1.f), /*bAutoDestroy*/ false, /*bAutoActivate*/ true, ENCPoolMethod::None, /*bPreCullCheck*/ false);
		if (RainSystem != nullptr)
		{
			return;
		}
	}

	// The fallback: thin pale streaks from the engine's own shapes, tinted
	// through the basic shape material's colour parameter.
	UStaticMesh* Streak = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Streak == nullptr || GetOwner() == nullptr)
	{
		return;
	}

	RainStreaks = NewObject<UInstancedStaticMeshComponent>(GetOwner(), TEXT("RainStreaks"), RF_Transient);
	RainStreaks->SetStaticMesh(Streak);
	RainStreaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainStreaks->SetCastShadow(false);
	RainStreaks->SetUsingAbsoluteLocation(true);
	RainStreaks->SetUsingAbsoluteRotation(true);
	RainStreaks->SetUsingAbsoluteScale(true);
	if (Base != nullptr)
	{
		UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Base, RainStreaks);
		Tint->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.62f, 0.68f, 0.76f));
		RainStreaks->SetMaterial(0, Tint);
	}
	RainStreaks->RegisterComponent();

	StreakOffsets.Reset(RainStreakCount);
	TArray<FTransform> Transforms;
	Transforms.Reserve(RainStreakCount);
	// A cylinder is 100 cm tall and 100 across: 0.4 cm wide, 35 cm long.
	const FVector Scale(0.004f, 0.004f, 0.35f);
	// A slight westerly lean, the way London rain comes off the river.
	const FRotator Lean(0.f, 0.f, 8.f);
	for (int32 Index = 0; Index < RainStreakCount; ++Index)
	{
		const FVector Offset(FMath::FRandRange(-RainBoxHalfCm, RainBoxHalfCm),
			FMath::FRandRange(-RainBoxHalfCm, RainBoxHalfCm),
			FMath::FRandRange(-RainBoxHalfCm * 0.5f, RainBoxHalfCm * 0.5f));
		StreakOffsets.Add(Offset);
		Transforms.Add(FTransform(Lean, Offset, Scale));
	}
	RainStreaks->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
}

void UOneBrahamWorldEffectsComponent::UpdateRainField(float DeltaTime, const FVector& CameraLocation)
{
	if (RainStreaks == nullptr || StreakOffsets.Num() == 0)
	{
		return;
	}

	// The box follows the camera; each streak falls through it and wraps to the
	// top, so the rain is always round the player and never runs out.
	RainStreaks->SetWorldLocation(CameraLocation);
	const float Drop = RainFallCmPerSecond * DeltaTime;
	const float Drift = Drop * 0.14f;
	const float HalfHeight = RainBoxHalfCm * 0.5f;
	const FVector Scale(0.004f, 0.004f, 0.35f);
	const FRotator Lean(0.f, 0.f, 8.f);

	TArray<FTransform> Transforms;
	Transforms.Reserve(StreakOffsets.Num());
	for (FVector& Offset : StreakOffsets)
	{
		Offset.Z -= Drop;
		Offset.Y += Drift;
		if (Offset.Z < -HalfHeight)
		{
			Offset.Z += HalfHeight * 2.f;
		}
		if (Offset.Y > RainBoxHalfCm)
		{
			Offset.Y -= RainBoxHalfCm * 2.f;
		}
		Transforms.Add(FTransform(Lean, Offset, Scale));
	}
	RainStreaks->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}
