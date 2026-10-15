// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamImpactFallbackActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OneBraham.h"
#include "VFX/OneBrahamVFXSubsystem.h"

namespace
{
	// -----------------------------------------------------------------------
	//  Shapes and materials. Engine primitives are in every project; the FX
	//  materials are authored by build_vfx.py and each look degrades to the
	//  next candidate when they are not there yet.
	// -----------------------------------------------------------------------
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/** A material candidate and the parameter names it understands. */
	struct FLookCandidate
	{
		const TCHAR* Path;
		const TCHAR* ColourParam;
		const TCHAR* EmissiveParam;
		const TCHAR* OpacityParam;
	};

	const FLookCandidate GlowCandidates[] = {
		{ TEXT("/Game/OneBraham/VFX/Materials/M_OB_FX_Glow.M_OB_FX_Glow"), TEXT("Colour"), TEXT("EmissiveStrength"), TEXT("Opacity") },
		{ TEXT("/Game/OneBraham/Materials/M_OB_Emissive.M_OB_Emissive"), TEXT("EmissiveColour"), TEXT("EmissiveStrength"), nullptr },
		{ TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), TEXT("Color"), nullptr, nullptr },
	};

	const FLookCandidate HazeCandidates[] = {
		{ TEXT("/Game/OneBraham/VFX/Materials/M_OB_FX_Haze.M_OB_FX_Haze"), TEXT("Colour"), TEXT("EmissiveStrength"), TEXT("Opacity") },
		{ TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), TEXT("Color"), nullptr, nullptr },
	};

	const FLookCandidate SolidCandidates[] = {
		{ TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), TEXT("Color"), nullptr, nullptr },
	};

	/** Resolved material for a look. Weak: if GC takes it, it is resolved again. */
	struct FResolvedLook
	{
		TWeakObjectPtr<UMaterialInterface> Material;
		FName ColourParam = NAME_None;
		FName EmissiveParam = NAME_None;
		FName OpacityParam = NAME_None;
	};

	FResolvedLook GResolvedLooks[3];

	template <int32 N>
	void ResolveFrom(FResolvedLook& Out, const FLookCandidate (&Candidates)[N])
	{
		for (const FLookCandidate& Candidate : Candidates)
		{
			// LOAD_NoWarn | LOAD_Quiet: a missing optional FX material is the
			// expected state of a fresh checkout, not something to shout about.
			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Candidate.Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Out.Material = Material;
				Out.ColourParam = Candidate.ColourParam != nullptr ? FName(Candidate.ColourParam) : NAME_None;
				Out.EmissiveParam = Candidate.EmissiveParam != nullptr ? FName(Candidate.EmissiveParam) : NAME_None;
				Out.OpacityParam = Candidate.OpacityParam != nullptr ? FName(Candidate.OpacityParam) : NAME_None;
				return;
			}
		}
	}

	const FResolvedLook& GetLook(EOneBrahamFXLook Look)
	{
		FResolvedLook& Resolved = GResolvedLooks[static_cast<int32>(Look)];
		if (!Resolved.Material.IsValid())
		{
			switch (Look)
			{
			case EOneBrahamFXLook::Glow:	ResolveFrom(Resolved, GlowCandidates); break;
			case EOneBrahamFXLook::Haze:	ResolveFrom(Resolved, HazeCandidates); break;
			default:						ResolveFrom(Resolved, SolidCandidates); break;
			}
		}
		return Resolved;
	}

	UStaticMesh* GetShapeMesh(EOneBrahamFXShape Shape)
	{
		static TWeakObjectPtr<UStaticMesh> Cached[3];
		TWeakObjectPtr<UStaticMesh>& Slot = Cached[static_cast<int32>(Shape)];

		if (!Slot.IsValid())
		{
			const TCHAR* Path = Shape == EOneBrahamFXShape::Cube ? CubeMeshPath
				: (Shape == EOneBrahamFXShape::Cylinder ? CylinderMeshPath : SphereMeshPath);
			Slot = LoadObject<UStaticMesh>(nullptr, Path);
		}
		return Slot.Get();
	}

	/** Engine primitives are a metre across: a scale of 0.01 is one centimetre. */
	FORCEINLINE FVector Cm(float X, float Y, float Z) { return FVector(X, Y, Z) * 0.01f; }
	FORCEINLINE FVector Cm(float Size) { return FVector(Size * 0.01f); }

	int32 CountFor(float Min, float Max, float Intensity)
	{
		return FMath::Max(0, FMath::RoundToInt(FMath::Lerp(Min, Max, FMath::Clamp(Intensity, 0.f, 1.f))));
	}

	// Surface palette. Muted on purpose: the building is grey, beige and blue,
	// and an impact that is brighter than the wall it came out of looks pasted on.
	const FLinearColor ConcreteDust(0.42f, 0.40f, 0.37f);
	const FLinearColor ConcreteChip(0.33f, 0.32f, 0.30f);
	const FLinearColor MarbleDust(0.62f, 0.60f, 0.57f);
	const FLinearColor PlasterDust(0.82f, 0.80f, 0.76f);
	const FLinearColor FibreDust(0.17f, 0.16f, 0.15f);
	const FLinearColor WoodDust(0.42f, 0.31f, 0.20f);
	const FLinearColor GlassShard(0.70f, 0.82f, 0.88f);
	const FLinearColor WaterColour(0.52f, 0.58f, 0.62f);
	const FLinearColor SparkColour(1.f, 0.62f, 0.22f);
	const FLinearColor SmokeGrey(0.24f, 0.24f, 0.24f);

	// Blood is very dark in shadow and only reads red where the light catches
	// it. A bright red is the single fastest way to turn this into a different
	// genre, and Docs/11 asks for restraint.
	const FLinearColor BloodDroplet(0.20f, 0.012f, 0.010f);
	const FLinearColor BloodMist(0.28f, 0.03f, 0.025f);
}

// ===========================================================================
//  Construction and pooling
// ===========================================================================

AOneBrahamImpactFallbackActor::AOneBrahamImpactFallbackActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
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
	Flash->SetVisibility(false);
	Flash->SetLightColor(FLinearColor(1.f, 0.72f, 0.4f));

	// Builders hold a pointer to the particle they just added until it is
	// committed; reserving the cap up front means that pointer can never be
	// invalidated by a reallocation.
	Particles.Reserve(MaxParticles);
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::Acquire(UWorld* World, const FVector& Location, const FRotator& Rotation)
{
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(World);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	AOneBrahamImpactFallbackActor* Actor = Subsystem->AcquireFallbackActor(Location, Rotation);
	if (Actor != nullptr)
	{
		Actor->ResetEffect();
		Actor->bInUse = true;
		Actor->SetActorTickEnabled(true);
	}
	return Actor;
}

void AOneBrahamImpactFallbackActor::ResetEffect()
{
	for (FOneBrahamFXParticle& Particle : Particles)
	{
		if (UStaticMeshComponent* Mesh = Particle.Mesh)
		{
			if (Particle.bPhysics)
			{
				Mesh->SetSimulatePhysics(false);
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Mesh->SetVisibility(false);
		}
	}

	Particles.Reset();
	MeshPoolCursor = 0;

	if (Flash != nullptr)
	{
		Flash->SetIntensity(0.f);
		Flash->SetVisibility(false);
	}

	LifeSeconds = 0.f;
	FlashSeconds = 0.f;
	PeakLightIntensity = 0.f;
	Elapsed = 0.f;
	FloorZ = -UE_BIG_NUMBER;
	bInUse = false;
	bProtectedFromRecycling = false;
	SetActorTickEnabled(false);
}

// ===========================================================================
//  Particles
// ===========================================================================

FOneBrahamFXParticle* AOneBrahamImpactFallbackActor::AddParticle(EOneBrahamFXLook Look, EOneBrahamFXShape Shape, const FVector& Location,
	const FVector& Velocity, float Life, const FVector& StartScale, const FVector& EndScale, const FLinearColor& Colour)
{
	if (Particles.Num() >= MaxParticles)
	{
		return nullptr;
	}

	UStaticMesh* ShapeMesh = GetShapeMesh(Shape);
	if (ShapeMesh == nullptr)
	{
		return nullptr;
	}

	// Borrow a component from the actor's own pool: a reused actor reuses its
	// components, so a steady firefight creates no new UObjects at all.
	UStaticMeshComponent* Mesh = nullptr;
	if (MeshPoolCursor < MeshPool.Num())
	{
		Mesh = MeshPool[MeshPoolCursor];
	}
	else
	{
		Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetupAttachment(DebrisRoot);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetUsingAbsoluteLocation(true);
		Mesh->SetUsingAbsoluteRotation(true);
		Mesh->SetUsingAbsoluteScale(true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCastShadow(false);
		Mesh->SetReceivesDecals(false);
		Mesh->bSelectable = false;
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->RegisterComponent();
		MeshPool.Add(Mesh);
	}
	++MeshPoolCursor;

	if (Mesh->GetStaticMesh() != ShapeMesh)
	{
		Mesh->SetStaticMesh(ShapeMesh);
	}

	// The MID is per component and kept with it; it is only rebuilt when the
	// look (and so the parent material) changes.
	const FResolvedLook& Resolved = GetLook(Look);
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (Resolved.Material.IsValid() && (MID == nullptr || MID->Parent != Resolved.Material.Get()))
	{
		MID = UMaterialInstanceDynamic::Create(Resolved.Material.Get(), Mesh);
		Mesh->SetMaterial(0, MID);
	}

	FOneBrahamFXParticle& Particle = Particles.AddDefaulted_GetRef();
	Particle.Mesh = Mesh;
	Particle.Material = MID;
	Particle.Look = Look;
	Particle.Location = Location;
	Particle.Velocity = Velocity;
	Particle.Life = FMath::Max(0.02f, Life);
	Particle.StartScale = StartScale;
	Particle.EndScale = EndScale;
	Particle.Colour = Colour;
	Particle.Rotation = FRotator(FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f));
	Particle.bActive = true;

	// Shape is remembered only through the mesh; CommitParticle needs it for
	// physics setup, so callers pass it again.
	return &Particle;
}

void AOneBrahamImpactFallbackActor::CommitParticle(FOneBrahamFXParticle& Particle, EOneBrahamFXShape Shape)
{
	UStaticMeshComponent* Mesh = Particle.Mesh;
	if (Mesh == nullptr)
	{
		return;
	}

	LifeSeconds = FMath::Max(LifeSeconds, Particle.Delay + Particle.Life);

	FRotator Rotation = Particle.Rotation;
	if (Particle.bAlignToVelocity && !Particle.Velocity.IsNearlyZero())
	{
		Rotation = FRotationMatrix::MakeFromZ(Particle.Velocity.GetSafeNormal()).Rotator();
	}

	Mesh->SetWorldLocationAndRotation(Particle.Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);
	Mesh->SetWorldScale3D(Particle.StartScale);
	ApplyParticleVisuals(Particle, 0.f);
	Mesh->SetVisibility(Particle.Delay <= 0.f);

	if (Particle.bPhysics)
	{
		// Brass and debris collide with the building and nothing else: a casing
		// that trips a player, or that a player can kick across a room, is a
		// physics bug report waiting to happen.
		Mesh->SetCollisionObjectType(ECC_PhysicsBody);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Mesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		Mesh->SetSimulatePhysics(true);
		Mesh->SetMassOverrideInKg(NAME_None, Shape == EOneBrahamFXShape::Cylinder ? 0.012f : 0.25f, true);
		Mesh->SetLinearDamping(0.1f);
		Mesh->SetAngularDamping(0.6f);
		Mesh->SetPhysicsLinearVelocity(Particle.Velocity);
		Mesh->SetPhysicsAngularVelocityInDegrees(FVector(Particle.Spin.Pitch, Particle.Spin.Yaw, Particle.Spin.Roll));
	}
}

void AOneBrahamImpactFallbackActor::ApplyParticleVisuals(FOneBrahamFXParticle& Particle, float Alpha)
{
	UStaticMeshComponent* Mesh = Particle.Mesh;
	if (Mesh == nullptr)
	{
		return;
	}

	const FResolvedLook& Resolved = GetLook(Particle.Look);
	const float Opacity = FMath::Lerp(Particle.OpacityStart, Particle.OpacityEnd, Alpha);
	FVector Scale = FMath::Lerp(Particle.StartScale, Particle.EndScale, Alpha);

	// An opaque fallback material cannot fade, so it shrinks instead: the eye
	// reads "it dissipated" from either, and a hard pop reads as a bug.
	if (Resolved.OpacityParam.IsNone() && Particle.OpacityStart > KINDA_SMALL_NUMBER)
	{
		Scale *= FMath::Clamp(Opacity / Particle.OpacityStart, 0.f, 1.f);
	}

	if (!Particle.bPhysics)
	{
		Mesh->SetWorldScale3D(Scale.ComponentMax(FVector(0.0005f)));
	}

	if (UMaterialInstanceDynamic* MID = Particle.Material)
	{
		if (!Resolved.ColourParam.IsNone())
		{
			MID->SetVectorParameterValue(Resolved.ColourParam, Particle.Colour);
		}
		if (!Resolved.EmissiveParam.IsNone())
		{
			MID->SetScalarParameterValue(Resolved.EmissiveParam, FMath::Lerp(Particle.EmissiveStart, Particle.EmissiveEnd, Alpha));
		}
		if (!Resolved.OpacityParam.IsNone())
		{
			MID->SetScalarParameterValue(Resolved.OpacityParam, Opacity);
		}
	}
}

void AOneBrahamImpactFallbackActor::SetFlash(const FLinearColor& Colour, float PeakIntensity, float Radius, float Seconds)
{
	if (Flash == nullptr)
	{
		return;
	}

	PeakLightIntensity = PeakIntensity;
	FlashSeconds = Seconds;
	LifeSeconds = FMath::Max(LifeSeconds, Seconds);

	Flash->SetLightColor(Colour);
	Flash->SetAttenuationRadius(Radius);
	Flash->SetIntensity(PeakIntensity);
	Flash->SetVisibility(PeakIntensity > 0.f);
}

void AOneBrahamImpactFallbackActor::ResolveFloorPlane(const FVector& From)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// One trace per effect, against the building only. Every droplet and chip in
	// the effect lands on this plane; a per-particle trace would cost more than
	// the rest of the effect put together.
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, From + FVector(0.f, 0.f, 5.f), From - FVector(0.f, 0.f, 400.f), Objects))
	{
		FloorZ = Hit.ImpactPoint.Z;
	}
}

void AOneBrahamImpactFallbackActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bInUse)
	{
		SetActorTickEnabled(false);
		return;
	}

	Elapsed += DeltaSeconds;

	// --- Light ------------------------------------------------------------
	if (Flash != nullptr && FlashSeconds > 0.f)
	{
		// Square decay: bright for the first frames, gone before you can focus
		// on it. A slow flash reads as a bug.
		const float Alpha = FMath::Clamp(1.f - (Elapsed / FlashSeconds), 0.f, 1.f);
		Flash->SetIntensity(PeakLightIntensity * Alpha * Alpha);
		if (Alpha <= 0.f)
		{
			Flash->SetVisibility(false);
			FlashSeconds = 0.f;
		}
	}

	// --- Particles --------------------------------------------------------
	for (FOneBrahamFXParticle& Particle : Particles)
	{
		if (!Particle.bActive || Particle.Mesh == nullptr)
		{
			continue;
		}

		if (Particle.Delay > 0.f)
		{
			Particle.Delay -= DeltaSeconds;
			if (Particle.Delay <= 0.f)
			{
				Particle.Mesh->SetVisibility(true);
			}
			continue;
		}

		Particle.Age += DeltaSeconds;
		if (Particle.Age >= Particle.Life)
		{
			Particle.bActive = false;
			if (Particle.bPhysics)
			{
				Particle.Mesh->SetSimulatePhysics(false);
				Particle.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Particle.Mesh->SetVisibility(false);
			continue;
		}

		const float Alpha = Particle.Age / Particle.Life;

		if (!Particle.bPhysics)
		{
			Particle.Velocity.Z += (Particle.Buoyancy - Particle.Gravity) * DeltaSeconds;
			Particle.Velocity *= FMath::Max(0.f, 1.f - Particle.Drag * DeltaSeconds);
			Particle.Location += Particle.Velocity * DeltaSeconds;

			if (Particle.Location.Z < FloorZ && Particle.Velocity.Z < 0.f)
			{
				Particle.Location.Z = FloorZ + 0.2f;

				if (Particle.bDieOnFloor)
				{
					Particle.Age = Particle.Life;
				}
				else if (Particle.FloorBounce > 0.f)
				{
					Particle.Velocity.Z *= -Particle.FloorBounce;
					Particle.Velocity.X *= 0.6f;
					Particle.Velocity.Y *= 0.6f;
				}
				else
				{
					Particle.Velocity = FVector::ZeroVector;
					Particle.Spin = FRotator::ZeroRotator;
				}
			}

			FRotator Rotation;
			if (Particle.bAlignToVelocity && Particle.Velocity.SizeSquared() > 1.f)
			{
				Rotation = FRotationMatrix::MakeFromZ(Particle.Velocity.GetSafeNormal()).Rotator();
			}
			else
			{
				Particle.Rotation += Particle.Spin * DeltaSeconds;
				Rotation = Particle.Rotation;
			}

			Particle.Mesh->SetWorldLocationAndRotation(Particle.Location, Rotation);
		}

		ApplyParticleVisuals(Particle, Alpha);
	}

	if (Elapsed >= LifeSeconds)
	{
		ResetEffect();

		if (UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(this))
		{
			Subsystem->ReleaseFallbackActor(this);
		}
		else
		{
			Destroy();
		}
	}
}

// ===========================================================================
//  Building blocks
// ===========================================================================

void AOneBrahamImpactFallbackActor::BuildSparks(const FVector& Location, const FVector& Direction, int32 Count, float ConeDegrees,
	float MinSpeed, float MaxSpeed, float Life, float Gravity)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Velocity = FMath::VRandCone(Direction, FMath::DegreesToRadians(ConeDegrees)) * FMath::FRandRange(MinSpeed, MaxSpeed);
		const float Length = FMath::FRandRange(1.8f, 3.6f);

		// A spark is a stretched glowing needle along its velocity: that is what
		// the eye's persistence turns a hot fleck into.
		if (FOneBrahamFXParticle* Spark = AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Cylinder, Location, Velocity,
			Life * FMath::FRandRange(0.6f, 1.f), Cm(0.18f, 0.18f, Length), Cm(0.08f, 0.08f, Length * 0.4f), SparkColour))
		{
			Spark->Gravity = Gravity;
			Spark->Drag = 1.6f;
			Spark->EmissiveStart = 45.f;
			Spark->EmissiveEnd = 2.f;
			Spark->OpacityStart = 1.f;
			Spark->OpacityEnd = 0.6f;
			Spark->bAlignToVelocity = true;
			Spark->FloorBounce = 0.35f;
			CommitParticle(*Spark, EOneBrahamFXShape::Cylinder);
		}
	}
}

void AOneBrahamImpactFallbackActor::BuildDust(const FVector& Location, const FVector& Direction, int32 Count, float MaxScale, float Life,
	const FLinearColor& Colour, float Opacity)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Velocity = FMath::VRandCone(Direction, FMath::DegreesToRadians(40.f)) * FMath::FRandRange(40.f, 140.f);
		const float EndSize = MaxScale * FMath::FRandRange(0.6f, 1.f);

		if (FOneBrahamFXParticle* Puff = AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere,
			Location + FMath::VRand() * 2.f, Velocity, Life * FMath::FRandRange(0.7f, 1.f), Cm(EndSize * 0.15f), Cm(EndSize), Colour))
		{
			Puff->Drag = 2.6f;
			Puff->Gravity = 30.f;
			Puff->OpacityStart = Opacity;
			Puff->OpacityEnd = 0.f;
			Puff->Delay = Index * 0.02f;
			CommitParticle(*Puff, EOneBrahamFXShape::Sphere);
		}
	}
}

void AOneBrahamImpactFallbackActor::BuildChips(const FVector& Location, const FVector& Direction, int32 Count, float ConeDegrees,
	float MinSpeed, float MaxSpeed, float SizeCm, const FLinearColor& Colour)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Velocity = FMath::VRandCone(Direction, FMath::DegreesToRadians(ConeDegrees)) * FMath::FRandRange(MinSpeed, MaxSpeed);
		const float Size = SizeCm * FMath::FRandRange(0.5f, 1.2f);
		const FVector Scale = Cm(Size, Size * FMath::FRandRange(0.5f, 1.f), Size * FMath::FRandRange(0.4f, 0.9f));

		if (FOneBrahamFXParticle* Chip = AddParticle(EOneBrahamFXLook::Solid, EOneBrahamFXShape::Cube, Location, Velocity,
			FMath::FRandRange(0.6f, 1.1f), Scale, Scale * 0.6f, Colour))
		{
			Chip->Gravity = 980.f;
			Chip->Drag = 0.4f;
			Chip->Spin = FRotator(FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f));
			Chip->FloorBounce = 0.25f;
			CommitParticle(*Chip, EOneBrahamFXShape::Cube);
		}
	}
}

// ===========================================================================
//  Surface impacts
// ===========================================================================

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnForImpact(UWorld* World, const FOneBrahamImpactInfo& Impact, float Intensity,
	bool bExplosive, const FVector& ShotDirection)
{
	if (bExplosive)
	{
		return SpawnExplosion(World, Impact.Location, Impact.Normal, 420.f, Impact.Surface == EOneBrahamFootstepSurface::Plasterboard, Intensity);
	}

	if (Impact.bHitCharacter)
	{
		const FVector Direction = ShotDirection.IsNearlyZero() ? -FVector(Impact.Normal) : ShotDirection;
		return SpawnFlesh(World, Impact.Location, Impact.Normal, Direction, 0.6f, false, Intensity);
	}

	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Impact.Location, FVector(Impact.Normal).Rotation());
	if (Actor != nullptr)
	{
		Actor->BuildSurfaceImpact(Impact, ShotDirection, Intensity);
	}
	return Actor;
}

void AOneBrahamImpactFallbackActor::BuildSurfaceImpact(const FOneBrahamImpactInfo& Impact, const FVector& ShotDirection, float Intensity)
{
	Intensity = FMath::Clamp(Intensity, 0.15f, 1.f);

	const FVector Location = Impact.Location;
	const FVector Normal = FVector(Impact.Normal).GetSafeNormal();
	const FVector Shot = ShotDirection.IsNearlyZero() ? -Normal : ShotDirection.GetSafeNormal();

	// Debris leaves the surface between the normal and the mirror of the shot:
	// a round hitting a wall at an angle throws its dust *forward*, not straight out.
	const FVector Reflected = FMath::GetReflectionVector(Shot, Normal);
	const FVector Spray = (Normal * 0.65f + Reflected * 0.35f).GetSafeNormal();

	ResolveFloorPlane(Location);
	if (Normal.Z > 0.7f)
	{
		FloorZ = Location.Z;
	}

	switch (Impact.Surface)
	{
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::ServerRack:
	case EOneBrahamFootstepSurface::Decking:
		// Metal: a hot bright burst and almost no dust.
		BuildSparks(Location, Spray, CountFor(5.f, 14.f, Intensity), 55.f, 380.f, 1100.f, 0.45f, 700.f);
		BuildDust(Location, Normal, 1, 14.f, 0.5f, SmokeGrey, 0.35f);
		SetFlash(FLinearColor(1.f, 0.78f, 0.45f), 5200.f * Intensity, FMath::Lerp(160.f, 320.f, Intensity), 0.07f);
		break;

	case EOneBrahamFootstepSurface::Glass:
	case EOneBrahamFootstepSurface::BallisticGlass:
	{
		// Glass: shards travel with the round as much as back off the pane.
		const int32 Shards = CountFor(4.f, 11.f, Intensity);
		for (int32 Index = 0; Index < Shards; ++Index)
		{
			const FVector Direction = (Index % 2 == 0) ? Shot : Spray;
			const FVector Velocity = FMath::VRandCone(Direction, FMath::DegreesToRadians(40.f)) * FMath::FRandRange(150.f, 480.f);
			const float Size = FMath::FRandRange(0.8f, 2.4f);

			if (FOneBrahamFXParticle* Shard = AddParticle(EOneBrahamFXLook::Solid, EOneBrahamFXShape::Cube, Location, Velocity,
				FMath::FRandRange(0.7f, 1.2f), Cm(Size, Size * 0.7f, 0.12f), Cm(Size, Size * 0.7f, 0.12f) * 0.7f, GlassShard))
			{
				Shard->Gravity = 980.f;
				Shard->Spin = FRotator(FMath::FRandRange(-900.f, 900.f), FMath::FRandRange(-900.f, 900.f), 0.f);
				Shard->FloorBounce = 0.2f;
				CommitParticle(*Shard, EOneBrahamFXShape::Cube);
			}
		}
		BuildDust(Location, Spray, 1, 16.f, 0.6f, FLinearColor(0.85f, 0.88f, 0.9f), 0.3f);
		SetFlash(FLinearColor(0.72f, 0.88f, 1.f), 2600.f * Intensity, 180.f, 0.05f);
		break;
	}

	case EOneBrahamFootstepSurface::Plasterboard:
		// Plasterboard: the big pale cloud is the whole story.
		BuildDust(Location, Spray, CountFor(2.f, 4.f, Intensity), 46.f, 1.5f, PlasterDust, 0.6f);
		BuildChips(Location, Spray, CountFor(2.f, 5.f, Intensity), 40.f, 120.f, 380.f, 1.4f, PlasterDust);
		break;

	case EOneBrahamFootstepSurface::Carpet:
	case EOneBrahamFootstepSurface::Fabric:
		BuildDust(Location, Normal, CountFor(1.f, 2.f, Intensity), 16.f, 0.6f, FibreDust, 0.45f);
		BuildChips(Location, Spray, CountFor(1.f, 3.f, Intensity), 30.f, 60.f, 180.f, 0.6f, FibreDust);
		break;

	case EOneBrahamFootstepSurface::Puddle:
	{
		// Water: a crown of droplets thrown up, and a ring that spreads and goes.
		const int32 Drops = CountFor(5.f, 12.f, Intensity);
		for (int32 Index = 0; Index < Drops; ++Index)
		{
			const FVector Velocity = FMath::VRandCone(FVector::UpVector, FMath::DegreesToRadians(32.f)) * FMath::FRandRange(220.f, 520.f);
			const float Size = FMath::FRandRange(0.5f, 1.1f);
			if (FOneBrahamFXParticle* Drop = AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere, Location, Velocity,
				0.8f, Cm(Size, Size, Size * 2.f), Cm(Size * 0.6f), WaterColour))
			{
				Drop->Gravity = 980.f;
				Drop->OpacityStart = 0.75f;
				Drop->OpacityEnd = 0.4f;
				Drop->bAlignToVelocity = true;
				Drop->bDieOnFloor = true;
				CommitParticle(*Drop, EOneBrahamFXShape::Sphere);
			}
		}

		if (FOneBrahamFXParticle* Ring = AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere, Location, FVector::ZeroVector,
			0.45f, Cm(4.f, 4.f, 0.3f), Cm(30.f, 30.f, 0.2f), WaterColour))
		{
			Ring->Rotation = FRotator::ZeroRotator;
			Ring->OpacityStart = 0.5f;
			Ring->OpacityEnd = 0.f;
			CommitParticle(*Ring, EOneBrahamFXShape::Sphere);
		}
		break;
	}

	case EOneBrahamFootstepSurface::Wood:
		BuildDust(Location, Spray, CountFor(1.f, 2.f, Intensity), 20.f, 0.8f, WoodDust, 0.45f);
		BuildChips(Location, Spray, CountFor(2.f, 5.f, Intensity), 35.f, 180.f, 520.f, 1.2f, WoodDust);
		break;

	case EOneBrahamFootstepSurface::Marble:
		BuildDust(Location, Spray, CountFor(1.f, 3.f, Intensity), 24.f, 0.9f, MarbleDust, 0.5f);
		BuildChips(Location, Spray, CountFor(2.f, 6.f, Intensity), 35.f, 250.f, 750.f, 1.f, MarbleDust);
		BuildSparks(Location, Spray, CountFor(0.f, 2.f, Intensity), 30.f, 300.f, 700.f, 0.15f, 600.f);
		break;

	case EOneBrahamFootstepSurface::Concrete:
	case EOneBrahamFootstepSurface::Lino:
	default:
		// Concrete: a fast grey puff, hard chips that skitter, and the odd spark
		// from the aggregate - the "crack" of a round on a stairwell wall.
		BuildDust(Location, Spray, CountFor(2.f, 3.f, Intensity), 28.f, 1.f, ConcreteDust, 0.55f);
		BuildChips(Location, Spray, CountFor(2.f, 6.f, Intensity), 35.f, 250.f, 750.f, 1.f, ConcreteChip);
		BuildSparks(Location, Spray, CountFor(0.f, 2.f, Intensity), 30.f, 300.f, 700.f, 0.15f, 600.f);
		break;
	}

	// Every impact gets a dim, very short pop of light so a round landing in a
	// dark riser cupboard is still visible to the person who fired it.
	if (FlashSeconds <= 0.f)
	{
		SetFlash(FLinearColor(1.f, 0.72f, 0.4f), 1200.f * Intensity, 140.f, 0.04f);
	}
	if (Flash != nullptr)
	{
		Flash->SetWorldLocation(Location + Normal * 8.f);
	}
}

// ===========================================================================
//  Flesh
// ===========================================================================

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnFlesh(UWorld* World, const FVector& Location, const FVector& Normal,
	const FVector& ShotDirection, float Severity, bool bHeadshot, float Intensity)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, ShotDirection.Rotation());
	if (Actor != nullptr)
	{
		Actor->BuildFlesh(Location, Normal, ShotDirection, Severity, bHeadshot, Intensity);
	}
	return Actor;
}

void AOneBrahamImpactFallbackActor::BuildFlesh(const FVector& Location, const FVector& Normal, const FVector& ShotDirection,
	float Severity, bool bHeadshot, float Intensity)
{
	Intensity = FMath::Clamp(Intensity, 0.15f, 1.f);
	Severity = FMath::Clamp(Severity, 0.1f, 1.5f);

	const FVector Shot = ShotDirection.IsNearlyZero() ? -FVector(Normal).GetSafeNormal() : ShotDirection.GetSafeNormal();
	const FVector Back = (FVector(Normal).GetSafeNormal() - Shot * 0.5f).GetSafeNormal();
	const float HeadScale = bHeadshot ? 1.6f : 1.f;

	ResolveFloorPlane(Location);

	// --- Exit spray -------------------------------------------------------
	// Most of it goes with the round. Droplets are stretched along their
	// velocity and fall properly, so a spray reads as liquid, not as confetti.
	const int32 Exit = FMath::RoundToInt(CountFor(5.f, 12.f, Intensity) * Severity * HeadScale);
	for (int32 Index = 0; Index < Exit; ++Index)
	{
		const FVector Velocity = FMath::VRandCone(Shot, FMath::DegreesToRadians(bHeadshot ? 28.f : 20.f)) * FMath::FRandRange(260.f, 680.f);
		const float Size = FMath::FRandRange(0.5f, 1.4f) * (bHeadshot ? 1.2f : 1.f);

		if (FOneBrahamFXParticle* Drop = AddParticle(EOneBrahamFXLook::Solid, EOneBrahamFXShape::Sphere, Location + Shot * 4.f, Velocity,
			FMath::FRandRange(0.5f, 0.9f), Cm(Size, Size, Size * 2.6f), Cm(Size * 0.7f, Size * 0.7f, Size * 1.4f), BloodDroplet))
		{
			Drop->Gravity = 980.f;
			Drop->Drag = 0.6f;
			Drop->bAlignToVelocity = true;
			Drop->bDieOnFloor = true;
			CommitParticle(*Drop, EOneBrahamFXShape::Sphere);
		}
	}

	// --- Back-spatter -----------------------------------------------------
	const int32 Entry = CountFor(2.f, 5.f, Intensity);
	for (int32 Index = 0; Index < Entry; ++Index)
	{
		const FVector Velocity = FMath::VRandCone(Back, FMath::DegreesToRadians(40.f)) * FMath::FRandRange(100.f, 300.f);
		const float Size = FMath::FRandRange(0.4f, 0.9f);

		if (FOneBrahamFXParticle* Drop = AddParticle(EOneBrahamFXLook::Solid, EOneBrahamFXShape::Sphere, Location, Velocity,
			FMath::FRandRange(0.4f, 0.7f), Cm(Size, Size, Size * 2.f), Cm(Size * 0.6f), BloodDroplet))
		{
			Drop->Gravity = 980.f;
			Drop->Drag = 0.8f;
			Drop->bAlignToVelocity = true;
			Drop->bDieOnFloor = true;
			CommitParticle(*Drop, EOneBrahamFXShape::Sphere);
		}
	}

	// --- Mist ---------------------------------------------------------------
	// A short dark puff, gone in a third of a second. This is the thing that
	// tells a shooter at forty metres that the round connected.
	const int32 Mists = bHeadshot ? 3 : 2;
	for (int32 Index = 0; Index < Mists; ++Index)
	{
		const FVector Direction = Index == 0 ? Shot : FMath::VRandCone(Shot, FMath::DegreesToRadians(35.f));
		const float EndSize = FMath::FRandRange(18.f, 30.f) * HeadScale * FMath::Lerp(0.8f, 1.2f, FMath::Min(Severity, 1.f));

		if (FOneBrahamFXParticle* Mist = AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere, Location + Direction * 6.f,
			Direction * FMath::FRandRange(90.f, 200.f), FMath::FRandRange(0.25f, 0.4f), Cm(EndSize * 0.2f), Cm(EndSize), BloodMist))
		{
			Mist->Drag = 4.f;
			Mist->Gravity = 60.f;
			Mist->OpacityStart = 0.7f;
			Mist->OpacityEnd = 0.f;
			CommitParticle(*Mist, EOneBrahamFXShape::Sphere);
		}
	}
}

// ===========================================================================
//  Weapon effects
// ===========================================================================

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnMuzzleFlash(UWorld* World, const FTransform& Muzzle, float Scale)
{
	const FVector Location = Muzzle.GetLocation();
	const FVector Forward = Muzzle.GetRotation().GetForwardVector();

	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, Forward.Rotation());
	if (Actor == nullptr)
	{
		return nullptr;
	}

	Scale = FMath::Clamp(Scale, 0.3f, 2.f);
	const FLinearColor Core(1.f, 0.86f, 0.62f);
	const FLinearColor Petal(1.f, 0.6f, 0.25f);

	// Core: an elongated hot ball, two frames long.
	if (FOneBrahamFXParticle* Flare = Actor->AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Sphere, Location + Forward * 6.f * Scale,
		Forward * 1.f, 0.05f, Cm(5.f, 5.f, 16.f) * Scale, Cm(7.f, 7.f, 22.f) * Scale, Core))
	{
		Flare->bAlignToVelocity = true;
		Flare->EmissiveStart = 80.f;
		Flare->EmissiveEnd = 10.f;
		Flare->OpacityStart = 1.f;
		Flare->OpacityEnd = 0.2f;
		Actor->CommitParticle(*Flare, EOneBrahamFXShape::Sphere);
	}

	// Petals: the star shape a muzzle brake throws.
	const int32 Petals = 4;
	for (int32 Index = 0; Index < Petals; ++Index)
	{
		const FVector Direction = FMath::VRandCone(Forward, FMath::DegreesToRadians(55.f));
		if (FOneBrahamFXParticle* Ray = Actor->AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Cylinder,
			Location + Direction * 5.f * Scale, Direction * 1.f, 0.04f, Cm(1.2f, 1.2f, 9.f) * Scale, Cm(0.5f, 0.5f, 12.f) * Scale, Petal))
		{
			Ray->bAlignToVelocity = true;
			Ray->EmissiveStart = 50.f;
			Ray->EmissiveEnd = 5.f;
			Ray->OpacityEnd = 0.f;
			Actor->CommitParticle(*Ray, EOneBrahamFXShape::Cylinder);
		}
	}

	// A wisp of propellant smoke that hangs for half a second.
	if (FOneBrahamFXParticle* Wisp = Actor->AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere, Location + Forward * 10.f,
		Forward * 90.f + FVector(0.f, 0.f, 20.f), 0.55f, Cm(4.f) * Scale, Cm(18.f) * Scale, FLinearColor(0.55f, 0.55f, 0.55f)))
	{
		Wisp->Drag = 3.f;
		Wisp->Buoyancy = 40.f;
		Wisp->OpacityStart = 0.22f;
		Wisp->OpacityEnd = 0.f;
		Actor->CommitParticle(*Wisp, EOneBrahamFXShape::Sphere);
	}

	Actor->SetFlash(FLinearColor(1.f, 0.74f, 0.42f), 16000.f * Scale, 650.f * Scale, 0.06f);
	if (Actor->Flash != nullptr)
	{
		Actor->Flash->SetWorldLocation(Location + Forward * 12.f);
	}
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnTracer(UWorld* World, const FVector& Start, const FVector& End, float SpeedCmPerSecond)
{
	const FVector Path = End - Start;
	const float Distance = Path.Size();
	if (Distance < 150.f)
	{
		return nullptr;
	}

	const FVector Direction = Path / Distance;
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Start, Direction.Rotation());
	if (Actor == nullptr)
	{
		return nullptr;
	}

	// Real muzzle velocities (900 m/s) cross a floor in two frames, which reads
	// as a flicker rather than a streak. The fallback is capped at 450 m/s:
	// still faster than the eye can follow, slow enough to be seen at all.
	const float Speed = FMath::Clamp(SpeedCmPerSecond, 15000.f, 45000.f);
	const float Length = FMath::Min(140.f, Distance * 0.3f);

	// Start a little down the barrel line so the streak does not sit on the
	// muzzle flare.
	const FVector Origin = Start + Direction * (Length * 0.5f + 40.f);
	const float Life = FMath::Max(0.03f, (Distance - Length - 40.f) / Speed);

	if (FOneBrahamFXParticle* Streak = Actor->AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Cylinder, Origin, Direction * Speed,
		Life, Cm(0.45f, 0.45f, Length), Cm(0.35f, 0.35f, Length), FLinearColor(1.f, 0.78f, 0.48f)))
	{
		Streak->bAlignToVelocity = true;
		Streak->EmissiveStart = 30.f;
		Streak->EmissiveEnd = 22.f;
		Streak->OpacityStart = 0.9f;
		Streak->OpacityEnd = 0.7f;
		Actor->CommitParticle(*Streak, EOneBrahamFXShape::Cylinder);
	}
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnShellEject(UWorld* World, const FTransform& EjectPort, const FVector& InheritedVelocity, float CalibreScale)
{
	const FVector Location = EjectPort.GetLocation();
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, EjectPort.Rotator());
	if (Actor == nullptr)
	{
		return nullptr;
	}

	const FQuat Rotation = EjectPort.GetRotation();
	const FVector Right = Rotation.GetRightVector();
	const FVector Up = Rotation.GetUpVector();
	const FVector Back = -Rotation.GetForwardVector();

	// Out to the right, up, slightly back: the arc every ejection port throws.
	const FVector Velocity = InheritedVelocity
		+ Right * FMath::FRandRange(170.f, 260.f)
		+ Up * FMath::FRandRange(90.f, 170.f)
		+ Back * FMath::FRandRange(0.f, 50.f);

	CalibreScale = FMath::Clamp(CalibreScale, 0.6f, 2.2f);

	// Brass: 9 mm across, 2-2.5 cm long, depending on calibre.
	const FVector Scale = Cm(0.95f, 0.95f, 2.2f) * CalibreScale;
	if (FOneBrahamFXParticle* Casing = Actor->AddParticle(EOneBrahamFXLook::Solid, EOneBrahamFXShape::Cylinder, Location, Velocity,
		2.6f, Scale, Scale, FLinearColor(0.78f, 0.56f, 0.2f)))
	{
		Casing->bPhysics = true;
		Casing->Rotation = FRotationMatrix::MakeFromZ(Right).Rotator();
		Casing->Spin = FRotator(FMath::FRandRange(-1400.f, 1400.f), FMath::FRandRange(-600.f, 600.f), FMath::FRandRange(-1400.f, 1400.f));
		Actor->CommitParticle(*Casing, EOneBrahamFXShape::Cylinder);
	}
	return Actor;
}

// ===========================================================================
//  Explosives and utilities
// ===========================================================================

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnExplosion(UWorld* World, const FVector& Location, const FVector& Normal,
	float RadiusCm, bool bBreach, float Intensity)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, FRotator::ZeroRotator);
	if (Actor == nullptr)
	{
		return nullptr;
	}

	Intensity = FMath::Clamp(Intensity, 0.25f, 1.f);
	const FVector Up = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	const float Radius = FMath::Clamp(RadiusCm, 150.f, 1200.f);
	Actor->ResolveFloorPlane(Location);

	// --- Flash ------------------------------------------------------------
	Actor->SetFlash(bBreach ? FLinearColor(1.f, 0.85f, 0.65f) : FLinearColor(1.f, 0.58f, 0.22f),
		90000.f * Intensity, Radius * 3.f, bBreach ? 0.12f : 0.22f);

	// --- Fireball ---------------------------------------------------------
	// Frag only: a breaching charge is a hard white crack and a wall of dust,
	// not a fireball. Three overlapping spheres expanding and cooling.
	if (!bBreach)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector Offset = FMath::VRandCone(Up, FMath::DegreesToRadians(60.f)) * Radius * 0.12f;
			const float EndSize = Radius * FMath::FRandRange(0.45f, 0.65f);
			if (FOneBrahamFXParticle* Ball = Actor->AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Sphere, Location + Offset,
				Offset * 2.f, FMath::FRandRange(0.28f, 0.4f), Cm(EndSize * 0.2f), Cm(EndSize), FLinearColor(1.f, 0.45f, 0.12f)))
			{
				Ball->Drag = 3.f;
				Ball->EmissiveStart = 60.f;
				Ball->EmissiveEnd = 0.5f;
				Ball->OpacityStart = 0.95f;
				Ball->OpacityEnd = 0.f;
				Actor->CommitParticle(*Ball, EOneBrahamFXShape::Sphere);
			}
		}
	}

	// --- Smoke / dust -----------------------------------------------------
	const int32 Clouds = CountFor(3.f, 6.f, Intensity);
	const FLinearColor CloudColour = bBreach ? PlasterDust * 0.85f : FLinearColor(0.12f, 0.115f, 0.11f);
	for (int32 Index = 0; Index < Clouds; ++Index)
	{
		const FVector Direction = FMath::VRandCone(Up, FMath::DegreesToRadians(bBreach ? 50.f : 75.f));
		const float EndSize = Radius * FMath::FRandRange(0.6f, 0.95f);
		if (FOneBrahamFXParticle* Cloud = Actor->AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere, Location + Direction * 20.f,
			Direction * FMath::FRandRange(250.f, 520.f), FMath::FRandRange(3.f, 4.5f), Cm(EndSize * 0.25f), Cm(EndSize), CloudColour))
		{
			Cloud->Drag = 2.2f;
			Cloud->Buoyancy = bBreach ? 0.f : 45.f;
			Cloud->Gravity = bBreach ? 20.f : 0.f;
			Cloud->OpacityStart = 0.8f;
			Cloud->OpacityEnd = 0.f;
			Cloud->Delay = 0.03f * Index;
			Actor->CommitParticle(*Cloud, EOneBrahamFXShape::Sphere);
		}
	}

	// --- Debris (physics) ---------------------------------------------------
	const int32 Chunks = CountFor(5.f, 12.f, Intensity);
	const FLinearColor ChunkColour = bBreach ? PlasterDust * 0.9f : ConcreteChip;
	for (int32 Index = 0; Index < Chunks; ++Index)
	{
		const FVector Velocity = FMath::VRandCone(Up, FMath::DegreesToRadians(70.f)) * FMath::FRandRange(500.f, 1300.f);
		const float Size = FMath::FRandRange(2.5f, 7.f) * (bBreach ? 1.3f : 1.f);
		const FVector Scale = Cm(Size, Size * FMath::FRandRange(0.5f, 1.f), Size * FMath::FRandRange(0.3f, 0.7f));

		if (FOneBrahamFXParticle* Chunk = Actor->AddParticle(EOneBrahamFXLook::Solid, EOneBrahamFXShape::Cube,
			Location + Up * 10.f + FMath::VRand() * 8.f, Velocity, FMath::FRandRange(2.5f, 3.5f), Scale, Scale, ChunkColour))
		{
			Chunk->bPhysics = true;
			Chunk->Spin = FRotator(FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f));
			Actor->CommitParticle(*Chunk, EOneBrahamFXShape::Cube);
		}
	}

	// --- Sparks -----------------------------------------------------------
	Actor->BuildSparks(Location, Up, CountFor(4.f, 10.f, Intensity), 80.f, 600.f, 1500.f, 0.7f, 600.f);
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnFlashbang(UWorld* World, const FVector& Location, float RadiusCm)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, FRotator::ZeroRotator);
	if (Actor == nullptr)
	{
		return nullptr;
	}

	// Very bright, very short, and white. The blinding itself is the
	// character's post-process; this is what everybody else sees.
	Actor->SetFlash(FLinearColor(0.95f, 0.97f, 1.f), 400000.f, FMath::Clamp(RadiusCm * 1.4f, 800.f, 3000.f), 0.14f);

	if (FOneBrahamFXParticle* Core = Actor->AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Sphere, Location, FVector::ZeroVector,
		0.1f, Cm(20.f), Cm(60.f), FLinearColor::White))
	{
		Core->EmissiveStart = 200.f;
		Core->EmissiveEnd = 5.f;
		Core->OpacityEnd = 0.f;
		Actor->CommitParticle(*Core, EOneBrahamFXShape::Sphere);
	}

	Actor->BuildDust(Location, FVector::UpVector, 2, 60.f, 1.8f, FLinearColor(0.6f, 0.6f, 0.6f), 0.4f);
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnSmoke(UWorld* World, const FVector& Location, float RadiusCm, float LifetimeSeconds)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, FRotator::ZeroRotator);
	if (Actor == nullptr)
	{
		return nullptr;
	}

	// Smoke is gameplay: it must break line of sight. Nine overlapping spheres
	// that bloom out over a second and a half fill a corridor; if the FX
	// materials are missing they fall back to opaque grey, which blocks sight
	// even better than the real thing.
	const float Radius = FMath::Clamp(RadiusCm, 200.f, 900.f);
	const float Life = FMath::Clamp(LifetimeSeconds, 4.f, 40.f);
	const int32 Clouds = 9;
	Actor->bProtectedFromRecycling = true;

	for (int32 Index = 0; Index < Clouds; ++Index)
	{
		FVector Offset = FMath::VRand() * Radius * FMath::FRandRange(0.15f, 0.55f);
		Offset.Z = FMath::Abs(Offset.Z) * 0.6f + Radius * 0.2f;

		const float EndSize = Radius * FMath::FRandRange(0.9f, 1.25f);
		if (FOneBrahamFXParticle* Cloud = Actor->AddParticle(EOneBrahamFXLook::Haze, EOneBrahamFXShape::Sphere, Location + FVector(0.f, 0.f, 20.f),
			Offset * 1.6f, Life * FMath::FRandRange(0.9f, 1.f), Cm(EndSize * 0.3f), Cm(EndSize), FLinearColor(0.72f, 0.72f, 0.7f)))
		{
			Cloud->Drag = 1.4f;
			Cloud->OpacityStart = 0.96f;
			Cloud->OpacityEnd = 0.f;
			Cloud->Delay = Index * 0.08f;
			Cloud->Spin = FRotator(0.f, FMath::FRandRange(-6.f, 6.f), 0.f);
			Actor->CommitParticle(*Cloud, EOneBrahamFXShape::Sphere);
		}
	}
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnRicochet(UWorld* World, const FVector& Location, const FVector& Normal, const FVector& OutDirection)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, OutDirection.Rotation());
	if (Actor == nullptr)
	{
		return nullptr;
	}

	const FVector Out = OutDirection.GetSafeNormal();
	Actor->ResolveFloorPlane(Location);

	// A tight fan of sparks along the new path, and a short streak: the round
	// leaving, which is what makes a ricochet readable rather than a second hit.
	Actor->BuildSparks(Location, Out, 6, 18.f, 700.f, 1500.f, 0.3f, 400.f);

	const float Length = FMath::FRandRange(70.f, 110.f);
	if (FOneBrahamFXParticle* Streak = Actor->AddParticle(EOneBrahamFXLook::Glow, EOneBrahamFXShape::Cylinder, Location + Out * Length * 0.5f,
		Out * 18000.f, FMath::FRandRange(0.03f, 0.05f), Cm(0.3f, 0.3f, Length), Cm(0.15f, 0.15f, Length), FLinearColor(1.f, 0.8f, 0.55f)))
	{
		Streak->bAlignToVelocity = true;
		Streak->EmissiveStart = 25.f;
		Streak->EmissiveEnd = 5.f;
		Streak->OpacityEnd = 0.2f;
		Actor->CommitParticle(*Streak, EOneBrahamFXShape::Cylinder);
	}

	Actor->SetFlash(FLinearColor(1.f, 0.8f, 0.5f), 3000.f, 200.f, 0.05f);
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnSparkShower(UWorld* World, const FVector& Location, const FVector& Normal, float Scale)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, Normal.Rotation());
	if (Actor == nullptr)
	{
		return nullptr;
	}

	Actor->ResolveFloorPlane(Location);
	const FVector Out = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	Actor->BuildSparks(Location, (Out + FVector(0.f, 0.f, -0.3f)).GetSafeNormal(), FMath::RoundToInt(14.f * FMath::Clamp(Scale, 0.5f, 2.f)),
		60.f, 120.f, 520.f, 1.1f, 900.f);
	Actor->SetFlash(FLinearColor(0.75f, 0.85f, 1.f), 6000.f, 300.f, 0.12f);
	return Actor;
}

AOneBrahamImpactFallbackActor* AOneBrahamImpactFallbackActor::SpawnDustPuff(UWorld* World, const FVector& Location, const FVector& Normal, float Scale, const FLinearColor& Colour)
{
	AOneBrahamImpactFallbackActor* Actor = Acquire(World, Location, Normal.Rotation());
	if (Actor != nullptr)
	{
		const FVector Out = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
		Actor->BuildDust(Location, Out, 3, 40.f * FMath::Clamp(Scale, 0.3f, 3.f), 1.4f, Colour, 0.5f);
	}
	return Actor;
}
