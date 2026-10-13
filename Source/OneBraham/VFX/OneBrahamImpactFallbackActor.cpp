// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamImpactFallbackActor.h"

#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "OneBraham.h"

namespace
{
	/** Engine primitives: present in every project, no content pipeline needed. */
	const TCHAR* const CrumbMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const DefaultMaterialPath = TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");

	/** Crumbs per impact at full intensity, by surface. Soft surfaces shed more. */
	int32 CrumbCountFor(EOneBrahamFootstepSurface Surface, float Intensity, bool bExplosive)
	{
		if (bExplosive)
		{
			return FMath::RoundToInt(FMath::Lerp(4.f, 12.f, Intensity));
		}

		switch (Surface)
		{
		case EOneBrahamFootstepSurface::Plasterboard:
			return FMath::RoundToInt(FMath::Lerp(2.f, 5.f, Intensity));
		case EOneBrahamFootstepSurface::Concrete:
		case EOneBrahamFootstepSurface::Marble:
			return FMath::RoundToInt(FMath::Lerp(1.f, 4.f, Intensity));
		case EOneBrahamFootstepSurface::Metal:
		case EOneBrahamFootstepSurface::Grating:
		case EOneBrahamFootstepSurface::ServerRack:
			return FMath::RoundToInt(FMath::Lerp(1.f, 3.f, Intensity));
		case EOneBrahamFootstepSurface::Glass:
		case EOneBrahamFootstepSurface::BallisticGlass:
			return FMath::RoundToInt(FMath::Lerp(3.f, 7.f, Intensity));
		default:
			return FMath::RoundToInt(FMath::Lerp(1.f, 3.f, Intensity));
		}
	}

	/** Light colour: glass and metal throw a cold spark, plasterboard is warm dust. */
	FLinearColor LightColourFor(EOneBrahamFootstepSurface Surface, bool bExplosive)
	{
		if (bExplosive)
		{
			return FLinearColor(1.f, 0.55f, 0.18f);
		}

		switch (Surface)
		{
		case EOneBrahamFootstepSurface::Metal:
		case EOneBrahamFootstepSurface::Grating:
		case EOneBrahamFootstepSurface::ServerRack:
		case EOneBrahamFootstepSurface::Decking:
			return FLinearColor(1.f, 0.82f, 0.45f);
		case EOneBrahamFootstepSurface::Glass:
		case EOneBrahamFootstepSurface::BallisticGlass:
			return FLinearColor(0.72f, 0.88f, 1.f);
		default:
			return FLinearColor(1.f, 0.72f, 0.4f);
		}
	}
}

AOneBrahamImpactFallbackActor::AOneBrahamImpactFallbackActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	SetCanBeDamaged(false);
	SetReplicates(false);
	bNetLoadOnClient = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DebrisRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DebrisRoot"));
	DebrisRoot->SetupAttachment(Root);

	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(Root);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetCastShadows(false);
	Flash->SetAttenuationRadius(260.f);
	Flash->SetIntensity(0.f);
	Flash->SetLightColor(FLinearColor(1.f, 0.72f, 0.4f));

	Hole = CreateDefaultSubobject<UDecalComponent>(TEXT("Hole"));
	Hole->SetupAttachment(Root);
	Hole->SetMobility(EComponentMobility::Movable);
	Hole->SetVisibility(false);
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnForImpact(UWorld* World, const FOneBrahamImpactInfo& Impact, float Intensity, bool bExplosive)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	AOneBrahamImpactFallbackActor* Actor = World->SpawnActor<AOneBrahamImpactFallbackActor>(
		AOneBrahamImpactFallbackActor::StaticClass(), Impact.Location, Impact.Normal.Rotation(), Params);

	if (Actor == nullptr)
	{
		return nullptr;
	}

	Actor->Initialise(Impact, Intensity, bExplosive);
	return Actor;
}

void AOneBrahamImpactFallbackActor::Initialise(const FOneBrahamImpactInfo& Impact, float Intensity, bool bExplosive)
{
	Intensity = FMath::Clamp(Intensity, 0.15f, 1.f);

	LifeSeconds = bExplosive ? 0.85f : FMath::Lerp(0.22f, 0.42f, Intensity);
	PeakLightIntensity = bExplosive ? 26000.f * Intensity : 4200.f * Intensity;

	if (Flash != nullptr)
	{
		Flash->SetLightColor(LightColourFor(Impact.Surface, bExplosive));
		Flash->SetAttenuationRadius(bExplosive ? 900.f : FMath::Lerp(140.f, 300.f, Intensity));
		Flash->SetIntensity(PeakLightIntensity);
	}

	// --- Decal ------------------------------------------------------------
	if (Hole != nullptr && !Impact.bHitCharacter && !bExplosive)
	{
		const FString DecalPath = FString::Printf(
			TEXT("/Game/OneBraham/VFX/Decals/M_OB_Decal_BulletHole_Concrete.M_OB_Decal_BulletHole_Concrete"));

		if (UMaterialInterface* DecalMaterial = LoadObject<UMaterialInterface>(nullptr, *DecalPath))
		{
			Hole->SetDecalMaterial(DecalMaterial);
			Hole->SetDecalSize(FVector(4.f, 4.f, 4.f));
			Hole->SetVisibility(true);
			Hole->SetLifeSpan(LifeSeconds + 22.f);
		}
	}

	// --- Debris -----------------------------------------------------------
	UStaticMesh* CrumbMesh = LoadObject<UStaticMesh>(nullptr, CrumbMeshPath);
	UMaterialInterface* CrumbMaterial = LoadObject<UMaterialInterface>(nullptr, DefaultMaterialPath);

	if (CrumbMesh == nullptr || DebrisRoot == nullptr)
	{
		return;
	}

	const int32 Count = CrumbCountFor(Impact.Surface, Intensity, bExplosive);
	const FVector Normal = Impact.Normal.GetSafeNormal();
	const bool bSoft = Impact.Surface == EOneBrahamFootstepSurface::Carpet
		|| Impact.Surface == EOneBrahamFootstepSurface::Fabric
		|| Impact.Surface == EOneBrahamFootstepSurface::Plasterboard;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* Crumb = NewObject<UStaticMeshComponent>(this);
		if (Crumb == nullptr)
		{
			break;
		}

		Crumb->SetupAttachment(DebrisRoot);
		Crumb->SetStaticMesh(CrumbMesh);
		Crumb->SetMobility(EComponentMobility::Movable);
		Crumb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Crumb->SetCastShadow(false);
		Crumb->RegisterComponent();

		if (CrumbMaterial != nullptr)
		{
			Crumb->SetMaterial(0, CrumbMaterial);
		}

		// Crumbs are sprayed back along the surface normal, in a cone, with a bias
		// for the soft surfaces (dust falls and settles, metal pings and bounces).
		const float SpreadDegrees = bExplosive ? 62.f : 34.f;
		const FVector Direction = FMath::VRandCone(Normal, FMath::DegreesToRadians(SpreadDegrees));
		const float Speed = bExplosive
			? FMath::FRandRange(420.f, 1150.f)
			: (bSoft ? FMath::FRandRange(40.f, 190.f) : FMath::FRandRange(160.f, 620.f));

		const float Scale = bExplosive ? FMath::FRandRange(0.03f, 0.085f) : FMath::FRandRange(0.012f, 0.03f);
		Crumb->SetRelativeScale3D(FVector(Scale));
		Crumb->SetRelativeLocation(FMath::VRand() * 2.f);

		if (Crumb->IsSimulatingPhysics() == false)
		{
			Crumb->SetSimulatePhysics(true);
		}
		Crumb->SetPhysicsLinearVelocity(Direction * Speed);
		Crumb->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * FMath::FRandRange(60.f, 540.f));

		// Fade the crumbs out with the actor rather than letting them stack up in
		// a twelve-player firefight: this effect can run forty times a second.
		Crumb->SetLifeSpan(LifeSeconds + 0.9f);
		Crumbs.Add(Crumb);
	}
}

void AOneBrahamImpactFallbackActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Elapsed += DeltaSeconds;

	if (Flash != nullptr)
	{
		// Square decay: bright for the first two frames, gone before you can focus
		// on it. A slow flash reads as a bug.
		const float Alpha = FMath::Clamp(1.f - (Elapsed / FMath::Max(0.01f, LifeSeconds * 0.6f)), 0.f, 1.f);
		Flash->SetIntensity(PeakLightIntensity * Alpha * Alpha);
	}

	if (Elapsed >= LifeSeconds)
	{
		Destroy();
	}
}
