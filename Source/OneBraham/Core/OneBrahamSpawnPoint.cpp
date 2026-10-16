// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamSpawnPoint.h"

#include "OneBraham.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "UObject/ConstructorHelpers.h"

AOneBrahamSpawnPoint::AOneBrahamSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // spawn points are server-side only

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(SceneRoot);
	FacingArrow->ArrowSize = 1.5f;
	FacingArrow->ArrowColor = FColor(0, 112, 192); // One Braham blue
	FacingArrow->bIsScreenSizeScaled = true;

#if WITH_EDITORONLY_DATA
	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon != nullptr)
	{
		// A tiny blue triangle marks spawns in the viewport so level designers can
		// see spawn coverage at a glance without opening the details panel.
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> SpawnIcon(TEXT("/Engine/EditorResources/S_Player"));
		if (SpawnIcon.Succeeded())
		{
			EditorIcon->Sprite = SpawnIcon.Get();
		}
		EditorIcon->SetupAttachment(SceneRoot);
		EditorIcon->SetHiddenInGame(true);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif

	// The actor itself must never block a trace.
	SetActorEnableCollision(false);
}

void AOneBrahamSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Colour-code the arrow by floor so a designer scrolling the outliner can
	// find "all floor 9 spawns" instantly.
	if (FacingArrow != nullptr)
	{
		const FLinearColor ZoneColour =
			FloorIndex <= 0 ? FLinearColor(0.5f, 0.5f, 0.5f) :
			FloorIndex == 1 ? FLinearColor(0.00f, 0.44f, 0.75f) :
			FloorIndex == 2 ? FLinearColor(0.08f, 0.52f, 1.00f) :
			FloorIndex <= 8 ? FLinearColor(0.30f, 0.70f, 0.95f) :
			FloorIndex <= 11 ? FLinearColor(0.10f, 0.90f, 0.60f) :
			FloorIndex <= 14 ? FLinearColor(1.00f, 0.80f, 0.00f) :
			FLinearColor(0.90f, 0.20f, 0.20f);
		FacingArrow->ArrowColor = ZoneColour.ToFColor(true);
	}
}

void AOneBrahamSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	// Fail loudly in development if a spawn was placed inside a wall: that is a
	// "player stuck in geometry" support ticket waiting to happen.
#if !UE_BUILD_SHIPPING
	if (!ValidateClearance())
	{
		OB_WARN("Spawn point '%s' on floor %d is obstructed (capsule clearance test failed). "
			"Move it or disable it before packaging.", *GetName(), FloorIndex);
	}
#endif
}

FTransform AOneBrahamSpawnPoint::GetSpawnTransform() const
{
	// Spawn 4 cm above the arrow so the capsule never starts interpenetrating a
	// floor tile, and roll is zeroed (characters never spawn on their side).
	FTransform Result = FacingArrow != nullptr ? FacingArrow->GetComponentTransform() : GetActorTransform();
	FVector Location = Result.GetLocation() + FVector(0.f, 0.f, 4.f);
	FRotator Rotation = Result.Rotator();
	Rotation.Roll = 0.f;
	return FTransform(Rotation, Location);
}

bool AOneBrahamSpawnPoint::IsAvailable(float CurrentTime) const
{
	if (!bEnabled)
	{
		return false;
	}

	if (CooldownSeconds <= 0.f)
	{
		return true;
	}

	return (CurrentTime - LastUsedTime) >= CooldownSeconds;
}

bool AOneBrahamSpawnPoint::ValidateClearance() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return true;
	}

	const FVector Location = GetSpawnTransform().GetLocation();

	// A standing capsule is 88 cm tall, 34 cm radius; test a slightly larger
	// shape so spawns do not clip doorframes either.
	constexpr float CapsuleRadius = 40.f;
	constexpr float CapsuleHalfHeight = 92.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBSpawnClearance), false, this);
	FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);

	// Offset the test up by the half height so the capsule stands on the floor
	// rather than being buried in it.
	const FVector TestLocation = Location + FVector(0.f, 0.f, CapsuleHalfHeight);

	return !World->OverlapBlockingTestByChannel(TestLocation, FQuat::Identity, ECC_Pawn, Capsule, Params);
}

bool AOneBrahamSpawnPoint::IsClearForPawn(float CapsuleRadius, float CapsuleHalfHeight, const AActor* IgnoreActor) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return true;
	}

	// The game mode spawns the pawn centred on the actor. Lift the test a few
	// centimetres and shorten it to match, so the floor slab under the feet is
	// not mistaken for an obstruction; anything a player could stand in is.
	constexpr float FloorSlackCm = 8.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBSpawnOccupied), false, this);
	if (IgnoreActor != nullptr)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}

	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius,
		FMath::Max(CapsuleRadius, CapsuleHalfHeight - FloorSlackCm * 0.5f));
	return !World->OverlapBlockingTestByChannel(GetActorLocation() + FVector(0.f, 0.f, FloorSlackCm * 0.5f),
		FQuat::Identity, ECC_Pawn, Capsule, Params);
}
