// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamVFXSubsystem.h"

#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OneBraham.h"
#include "VFX/OneBrahamImpactFallbackActor.h"

namespace
{
	/** Authored by Tools/Python/onebraham/build_vfx.py (build_fx_materials). */
	const TCHAR* const BloodDecalMaterialPath = TEXT("/Game/OneBraham/VFX/Decals/M_OB_Decal_Blood.M_OB_Decal_Blood");

	/** Projection depth of a blood decal, cm. Deep enough to wrap a skirting
	 *  board or a stair nosing, shallow enough not to bleed through a desk. */
	constexpr float BloodDecalDepth = 12.f;
}

UOneBrahamVFXSubsystem* UOneBrahamVFXSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	return World->GetSubsystem<UOneBrahamVFXSubsystem>();
}

bool UOneBrahamVFXSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only: an editor preview world does not need a blood
	// budget, and a dedicated server never draws any of this.
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UOneBrahamVFXSubsystem::Deinitialize()
{
	ClearBlood();
	BulletHoles.Reset();
	ActiveFallbacks.Reset();
	ParkedFallbacks.Reset();
	Super::Deinitialize();
}

TStatId UOneBrahamVFXSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamVFXSubsystem, STATGROUP_Tickables);
}

ETickableTickType UOneBrahamVFXSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UOneBrahamVFXSubsystem::IsTickable() const
{
	// Nothing to grow or fade: no tick. Most of a match has no blood in view.
	return BloodDecals.Num() > 0;
}

// ===========================================================================
//  Orientation
// ===========================================================================

FRotator UOneBrahamVFXSubsystem::DecalRotationForSurface(const FVector& Normal, const FVector& AlongSurface, float RollDegreesIfRound)
{
	// A decal projects along its local +X. Pointing X *into* the surface (minus
	// the normal) is what makes the same code work on a floor, a wall and a
	// ceiling; the old code kept only the yaw, so every decal on a floor or a
	// ceiling was projected sideways and vanished.
	const FVector Into = (-Normal).GetSafeNormal();
	if (Into.IsNearlyZero())
	{
		return FRotator(-90.f, 0.f, RollDegreesIfRound);
	}

	const FVector Along = FVector::VectorPlaneProject(AlongSurface, Into);
	if (Along.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		// Y runs along the streak, so DecalSize.Y is the long axis.
		return FRotationMatrix::MakeFromXY(Into, Along.GetSafeNormal()).Rotator();
	}

	FRotator Rotation = Into.Rotation();
	Rotation.Roll = RollDegreesIfRound;
	return Rotation;
}

// ===========================================================================
//  Blood
// ===========================================================================

UMaterialInterface* UOneBrahamVFXSubsystem::GetBloodMaterial()
{
	if (!bBloodMaterialResolved)
	{
		bBloodMaterialResolved = true;
		BloodMaterial = LoadObject<UMaterialInterface>(nullptr, BloodDecalMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

		if (BloodMaterial == nullptr)
		{
			UE_LOG(LogOneBraham, Log,
				TEXT("[OB.VFX] '%s' is not in the project yet - blood decals are off until build_vfx.py has run. "
					 "Spray and pools still play."), BloodDecalMaterialPath);
		}
	}

	return BloodMaterial;
}

UDecalComponent* UOneBrahamVFXSubsystem::SpawnBloodDecal(const FVector& Location, const FVector& Normal, const FVector& AlongSurface,
	float RadiusCm, float Elongation, float HoldSeconds, USceneComponent* AttachTo, bool bPool, float GrowSeconds)
{
	UWorld* World = GetWorld();
	UMaterialInterface* Material = GetBloodMaterial();
	if (World == nullptr || Material == nullptr || RadiusCm <= 0.f)
	{
		return nullptr;
	}

	while (BloodDecals.Num() >= MaxBloodDecals)
	{
		RecycleOneBloodDecal();
	}

	const float Length = RadiusCm * FMath::Max(1.f, Elongation);
	const FVector2D EndExtent(Length, RadiusCm);
	const FVector2D StartExtent = bPool ? EndExtent * 0.2f : EndExtent;

	const FRotator Rotation = DecalRotationForSurface(Normal, AlongSurface, FMath::FRandRange(-180.f, 180.f));
	const FVector Size(BloodDecalDepth, StartExtent.X, StartExtent.Y);

	// Lifted a centimetre off the surface so the projection box straddles it.
	const FVector SpawnLocation = Location + Normal.GetSafeNormal() * 1.f;

	UDecalComponent* Decal = nullptr;
	if (AttachTo != nullptr && AttachTo->Mobility == EComponentMobility::Movable)
	{
		// A fire door with blood on it has to take the blood with it when it swings.
		Decal = UGameplayStatics::SpawnDecalAttached(Material, Size, AttachTo, NAME_None,
			SpawnLocation, Rotation, EAttachLocation::KeepWorldPosition, 0.f);
	}
	else
	{
		Decal = UGameplayStatics::SpawnDecalAtLocation(World, Material, Size, SpawnLocation, Rotation, 0.f);
	}

	if (Decal == nullptr)
	{
		return nullptr;
	}

	// Blood sits above bullet holes and scuffs (sort 0) so a hole in a spray
	// reads as a hole in the spray, not a clean patch.
	Decal->SetSortOrder(bPool ? 2 : 3);
	Decal->SetFadeScreenSize(0.0015f);

	UMaterialInstanceDynamic* MID = Decal->CreateDynamicMaterialInstance();
	const float BaseOpacity = bPool ? 0.92f : FMath::FRandRange(0.78f, 0.95f);
	if (MID != nullptr)
	{
		// Parameter contract with build_vfx.py's M_OB_Decal_Blood: Seed varies the
		// splatter shape, Splatter is 0 for a round pool and 1 for a spray.
		MID->SetScalarParameterValue(TEXT("Seed"), FMath::FRandRange(0.f, 100.f));
		MID->SetScalarParameterValue(TEXT("Splatter"), bPool ? 0.f : 1.f);
		MID->SetScalarParameterValue(TEXT("Opacity"), BaseOpacity);
		MID->SetScalarParameterValue(TEXT("Wetness"), 1.f);
	}

	FOneBrahamManagedDecal& Entry = BloodDecals.AddDefaulted_GetRef();
	Entry.Decal = Decal;
	Entry.Material = MID;
	Entry.SpawnTime = World->GetTimeSeconds();
	Entry.HoldSeconds = HoldSeconds;
	Entry.FadeSeconds = bPool ? 8.f : 4.f;
	Entry.GrowSeconds = bPool ? FMath::Max(0.f, GrowSeconds) : 0.f;
	Entry.StartExtent = StartExtent;
	Entry.EndExtent = EndExtent;
	Entry.BaseOpacity = BaseOpacity;
	Entry.bPool = bPool;

	return Decal;
}

void UOneBrahamVFXSubsystem::RecycleOneBloodDecal()
{
	if (BloodDecals.Num() == 0)
	{
		return;
	}

	// Oldest splatter first; pools only when nothing else is left. Entries are
	// appended in spawn order, so the first match is the oldest.
	int32 Victim = BloodDecals.IndexOfByPredicate([](const FOneBrahamManagedDecal& Entry) { return !Entry.bPool; });
	if (Victim == INDEX_NONE)
	{
		Victim = 0;
	}

	if (UDecalComponent* Decal = BloodDecals[Victim].Decal.Get())
	{
		Decal->DestroyComponent();
	}

	BloodDecals.RemoveAt(Victim, EAllowShrinking::No);
}

void UOneBrahamVFXSubsystem::ClearBlood()
{
	for (FOneBrahamManagedDecal& Entry : BloodDecals)
	{
		if (UDecalComponent* Decal = Entry.Decal.Get())
		{
			Decal->DestroyComponent();
		}
	}

	BloodDecals.Reset();
}

void UOneBrahamVFXSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Size changes push render state; ten times a second is smooth for a pool
	// spreading at a couple of centimetres per second, and a fraction of the cost.
	GrowthAccumulator += DeltaTime;
	const bool bPushGrowth = GrowthAccumulator >= 0.1f;
	if (bPushGrowth)
	{
		GrowthAccumulator = 0.f;
	}

	for (int32 Index = BloodDecals.Num() - 1; Index >= 0; --Index)
	{
		FOneBrahamManagedDecal& Entry = BloodDecals[Index];
		UDecalComponent* Decal = Entry.Decal.Get();
		if (Decal == nullptr)
		{
			BloodDecals.RemoveAt(Index, EAllowShrinking::No);
			continue;
		}

		const float Age = Now - Entry.SpawnTime;

		// --- Growth (pools) ------------------------------------------------
		if (bPushGrowth && Entry.GrowSeconds > 0.f && Age <= Entry.GrowSeconds + 0.2f)
		{
			// Ease-out: blood spreads fastest at first and slows as it thins.
			const float Alpha = FMath::Clamp(Age / Entry.GrowSeconds, 0.f, 1.f);
			const float Eased = 1.f - FMath::Square(1.f - Alpha);
			const FVector2D Extent = FMath::Lerp(Entry.StartExtent, Entry.EndExtent, Eased);
			Decal->DecalSize = FVector(BloodDecalDepth, Extent.X, Extent.Y);
			Decal->MarkRenderStateDirty();
		}

		// --- Fade ----------------------------------------------------------
		if (Age > Entry.HoldSeconds)
		{
			const float FadeAlpha = FMath::Clamp((Age - Entry.HoldSeconds) / FMath::Max(0.1f, Entry.FadeSeconds), 0.f, 1.f);

			if (UMaterialInstanceDynamic* MID = Entry.Material.Get())
			{
				MID->SetScalarParameterValue(TEXT("Opacity"), Entry.BaseOpacity * (1.f - FadeAlpha));
				// Blood dries as it fades: darker and matte.
				MID->SetScalarParameterValue(TEXT("Wetness"), 1.f - FadeAlpha);
			}

			if (FadeAlpha >= 1.f)
			{
				Decal->DestroyComponent();
				BloodDecals.RemoveAt(Index, EAllowShrinking::No);
			}
		}
	}
}

// ===========================================================================
//  Bullet holes
// ===========================================================================

void UOneBrahamVFXSubsystem::RegisterBulletHole(UDecalComponent* Decal)
{
	if (Decal == nullptr)
	{
		return;
	}

	// Drop entries whose decal has already timed out, then enforce the cap.
	BulletHoles.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Entry) { return !Entry.IsValid(); });

	while (BulletHoles.Num() >= MaxBulletHoles)
	{
		if (UDecalComponent* Oldest = BulletHoles[0].Get())
		{
			Oldest->DestroyComponent();
		}
		BulletHoles.RemoveAt(0, EAllowShrinking::No);
	}

	BulletHoles.Add(Decal);
}

// ===========================================================================
//  Fallback actor pool
// ===========================================================================

AOneBrahamImpactFallbackActor* UOneBrahamVFXSubsystem::AcquireFallbackActor(const FVector& Location, const FRotator& Rotation)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	ActiveFallbacks.RemoveAll([](const TObjectPtr<AOneBrahamImpactFallbackActor>& Actor) { return !IsValid(Actor); });
	ParkedFallbacks.RemoveAll([](const TObjectPtr<AOneBrahamImpactFallbackActor>& Actor) { return !IsValid(Actor); });

	AOneBrahamImpactFallbackActor* Actor = nullptr;

	const int32 Oldest = ActiveFallbacks.Num() >= MaxActiveFallbackActors
		? ActiveFallbacks.IndexOfByPredicate([](const TObjectPtr<AOneBrahamImpactFallbackActor>& Active) { return Active->CanBeRecycled(); })
		: INDEX_NONE;

	if (Oldest != INDEX_NONE)
	{
		// At the cap: the oldest effect is cut short and its actor reused. The
		// oldest is nearly always a brass casing at rest or a fading dust puff -
		// the cheapest thing on screen to lose. Smoke is never chosen.
		Actor = ActiveFallbacks[Oldest];
		ActiveFallbacks.RemoveAt(Oldest, EAllowShrinking::No);
		Actor->ResetEffect();
	}
	else if (ParkedFallbacks.Num() > 0)
	{
		Actor = ParkedFallbacks.Pop(EAllowShrinking::No);
	}
	else
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Actor = World->SpawnActor<AOneBrahamImpactFallbackActor>(AOneBrahamImpactFallbackActor::StaticClass(), Location, Rotation, Params);
	}

	if (Actor != nullptr)
	{
		Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);
		ActiveFallbacks.Add(Actor);
	}

	return Actor;
}

void UOneBrahamVFXSubsystem::ReleaseFallbackActor(AOneBrahamImpactFallbackActor* Actor)
{
	if (Actor == nullptr)
	{
		return;
	}

	ActiveFallbacks.RemoveSingle(Actor);

	// Park a bounded number; anything beyond that goes away properly so a
	// one-off grenade storm does not leave sixty hidden actors behind.
	if (ParkedFallbacks.Num() < MaxActiveFallbackActors / 2)
	{
		ParkedFallbacks.AddUnique(Actor);
	}
	else
	{
		Actor->Destroy();
	}
}
