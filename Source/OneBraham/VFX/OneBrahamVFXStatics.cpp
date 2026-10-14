// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamVFXStatics.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Components/DecalComponent.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "OneBraham.h"
#include "VFX/OneBrahamImpactFallbackActor.h"

namespace
{
	/** Effect asset root. One folder, one naming convention, no exceptions. */
	const TCHAR* const EffectRoot = TEXT("/Game/OneBraham/VFX/");

	/** Per-effect cull distance, cm. Impacts are cheap but there can be hundreds. */
	float CullDistanceFor(EOneBrahamVFXEffect Effect)
	{
		switch (Effect)
		{
		case EOneBrahamVFXEffect::BulletImpact:
		case EOneBrahamVFXEffect::FleshImpact:
		case EOneBrahamVFXEffect::GlassImpact:
		case EOneBrahamVFXEffect::MetalImpact:
		case EOneBrahamVFXEffect::BloodMist:
		case EOneBrahamVFXEffect::DustPuff:
			return 5000.f;

		case EOneBrahamVFXEffect::SparkShower:
		case EOneBrahamVFXEffect::WaterSpray:
		case EOneBrahamVFXEffect::ShellEject:
		case EOneBrahamVFXEffect::BreachDebris:
		case EOneBrahamVFXEffect::CorpseImpact:
			return 7000.f;

		case EOneBrahamVFXEffect::MuzzleFlash:
		case EOneBrahamVFXEffect::Tracer:
			return 12000.f;

		case EOneBrahamVFXEffect::FragExplosion:
		case EOneBrahamVFXEffect::BreachExplosion:
		case EOneBrahamVFXEffect::FlashbangBurst:
		case EOneBrahamVFXEffect::SmokeCloud:
			return 25000.f;

		default:
			return 6000.f;
		}
	}

	/** Conventional asset stem for an effect. */
	FString EffectStem(EOneBrahamVFXEffect Effect)
	{
		switch (Effect)
		{
		case EOneBrahamVFXEffect::BulletImpact:		return TEXT("NS_OB_Impact_Concrete");
		case EOneBrahamVFXEffect::FleshImpact:		return TEXT("NS_OB_Impact_Flesh");
		case EOneBrahamVFXEffect::GlassImpact:		return TEXT("NS_OB_Impact_Glass");
		case EOneBrahamVFXEffect::MetalImpact:		return TEXT("NS_OB_Impact_Metal");
		case EOneBrahamVFXEffect::MuzzleFlash:		return TEXT("NS_OB_MuzzleFlash");
		case EOneBrahamVFXEffect::Tracer:			return TEXT("NS_OB_Tracer");
		case EOneBrahamVFXEffect::ShellEject:		return TEXT("NS_OB_ShellEject");
		case EOneBrahamVFXEffect::FragExplosion:	return TEXT("NS_OB_Explosion_Frag");
		case EOneBrahamVFXEffect::BreachExplosion:	return TEXT("NS_OB_Explosion_Breach");
		case EOneBrahamVFXEffect::FlashbangBurst:	return TEXT("NS_OB_Flashbang");
		case EOneBrahamVFXEffect::SmokeCloud:		return TEXT("NS_OB_Smoke");
		case EOneBrahamVFXEffect::BloodMist:		return TEXT("NS_OB_BloodMist");
		case EOneBrahamVFXEffect::WaterSpray:		return TEXT("NS_OB_SprinklerSpray");
		case EOneBrahamVFXEffect::SparkShower:		return TEXT("NS_OB_SparkShower");
		case EOneBrahamVFXEffect::DustPuff:			return TEXT("NS_OB_Dust");
		case EOneBrahamVFXEffect::BreachDebris:		return TEXT("NS_OB_BreachDebris");
		case EOneBrahamVFXEffect::CorpseImpact:		return TEXT("NS_OB_CorpseImpact");
		case EOneBrahamVFXEffect::Rain:				return TEXT("NS_OB_Rain");
		default:									return TEXT("NS_OB_Impact_Concrete");
		}
	}

	/** True for surfaces that are electrically live: a hit there throws sparks. */
	bool SurfaceSparks(EOneBrahamFootstepSurface Surface)
	{
		switch (Surface)
		{
		case EOneBrahamFootstepSurface::ServerRack:
		case EOneBrahamFootstepSurface::Grating:
			return true;

		default:
			return false;
		}
	}

	/** Cache: effect -> loaded system (or null once we know it is missing). */
	TMap<EOneBrahamVFXEffect, TWeakObjectPtr<UNiagaraSystem>> GEffectCache;
	TSet<EOneBrahamVFXEffect> GMissingEffects;
}

// ===========================================================================
//  Asset resolution
// ===========================================================================

FString UOneBrahamVFXStatics::GetEffectAssetPath(EOneBrahamVFXEffect Effect)
{
	return FString::Printf(TEXT("%s%s.%s"), EffectRoot, *EffectStem(Effect), *EffectStem(Effect));
}

UNiagaraSystem* UOneBrahamVFXStatics::LoadEffectSystem(EOneBrahamVFXEffect Effect)
{
	if (const TWeakObjectPtr<UNiagaraSystem>* Cached = GEffectCache.Find(Effect))
	{
		return Cached->Get();
	}

	if (GMissingEffects.Contains(Effect))
	{
		return nullptr;
	}

	const FString Path = GetEffectAssetPath(Effect);
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);

	// build_vfx.py creates every system so the paths resolve; a system nobody
	// has authored yet has no emitters and draws nothing. Treated as missing,
	// so the C++ fallback keeps drawing the effect until an artist fills it in.
	// Without this, running the content build made every impact invisible.
	if (System != nullptr && System->GetEmitterHandles().Num() == 0)
	{
		UE_LOG(LogOneBraham, Log,
			TEXT("[OB.VFX] '%s' exists but has no emitters yet - using the runtime fallback effect."), *Path);
		System = nullptr;
		GMissingEffects.Add(Effect);
		return nullptr;
	}

	if (System == nullptr)
	{
		// Logged once per effect, then never again: a missing VFX asset must not
		// produce a log line per bullet in a twelve-player firefight.
		GMissingEffects.Add(Effect);
		UE_LOG(LogOneBraham, Log,
			TEXT("[OB.VFX] '%s' is not in the project yet - using the runtime fallback effect. "
				 "See Docs/11_Niagara_VFX.md for the system spec."), *Path);
	}
	else
	{
		GEffectCache.Add(Effect, System);
	}

	return System;
}

void UOneBrahamVFXStatics::ReleaseCachedAssets()
{
	GEffectCache.Reset();
	GMissingEffects.Reset();
}

EOneBrahamVFXEffect UOneBrahamVFXStatics::ImpactEffectForSurface(EOneBrahamFootstepSurface Surface)
{
	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::ServerRack:
	case EOneBrahamFootstepSurface::Decking:
		return EOneBrahamVFXEffect::MetalImpact;

	case EOneBrahamFootstepSurface::Glass:
	case EOneBrahamFootstepSurface::BallisticGlass:
		return EOneBrahamVFXEffect::GlassImpact;

	case EOneBrahamFootstepSurface::Flesh:
		return EOneBrahamVFXEffect::FleshImpact;

	case EOneBrahamFootstepSurface::Puddle:
		return EOneBrahamVFXEffect::WaterSpray;

	case EOneBrahamFootstepSurface::Plasterboard:
		return EOneBrahamVFXEffect::DustPuff;

	default:
		return EOneBrahamVFXEffect::BulletImpact;
	}
}

FString UOneBrahamVFXStatics::GetDecalPathForSurface(EOneBrahamFootstepSurface Surface)
{
	// Decals are a separate, optional pass: a bullet hole on every surface in the
	// building is a texture budget conversation, not a code one.
	const TCHAR* Stem = TEXT("M_OB_Decal_BulletHole_Concrete");

	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::ServerRack:
	case EOneBrahamFootstepSurface::Decking:
		Stem = TEXT("M_OB_Decal_BulletHole_Metal");
		break;
	case EOneBrahamFootstepSurface::Glass:
	case EOneBrahamFootstepSurface::BallisticGlass:
		Stem = TEXT("M_OB_Decal_GlassCrack");
		break;
	case EOneBrahamFootstepSurface::Carpet:
	case EOneBrahamFootstepSurface::Fabric:
		Stem = TEXT("M_OB_Decal_BulletHole_Carpet");
		break;
	case EOneBrahamFootstepSurface::Plasterboard:
		Stem = TEXT("M_OB_Decal_BulletHole_Plaster");
		break;
	default:
		break;
	}

	return FString::Printf(TEXT("%sDecals/%s.%s"), EffectRoot, Stem, Stem);
}

// ===========================================================================
//  Culling
// ===========================================================================

float UOneBrahamVFXStatics::GetVFXIntensity()
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const EOneBrahamQualityPreset Preset = Settings != nullptr
		? Settings->GetQualityPreset()
		: EOneBrahamQualityPreset::Epic;

	// 1.0 = full Niagara effects. Below 0.35 the runtime fallback actor is used
	// instead of the Niagara system, which is how the minimum spec (which is
	// deliberately still able to hold 60 fps on the Skydeck) keeps its frame rate.
	switch (Preset)
	{
	case EOneBrahamQualityPreset::Low:			return 0.25f;
	case EOneBrahamQualityPreset::Medium:		return 0.55f;
	case EOneBrahamQualityPreset::High:			return 0.8f;
	case EOneBrahamQualityPreset::Epic:
	default:									return 1.f;
	}
}

bool UOneBrahamVFXStatics::ShouldSpawnForLocalViewer(const UObject* WorldContextObject, const FVector& Location, float MaxDistanceCm)
{
	return !IsCulledByViewer(WorldContextObject, Location, MaxDistanceCm);
}

bool UOneBrahamVFXStatics::IsCulledByViewer(const UObject* WorldContextObject, const FVector& Location, float CullDistanceCm)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return true;
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	if (PC == nullptr || PC->GetPawn() == nullptr)
	{
		return false;
	}

	// Effects behind the camera and far away are the cheapest win in the file.
	const FVector ViewerLocation = PC->GetPawn()->GetActorLocation();
	const float DistanceSquared = FVector::DistSquared(ViewerLocation, Location);
	if (DistanceSquared > FMath::Square(CullDistanceCm))
	{
		return true;
	}

	// Near effects are always spawned: a bullet hitting the wall next to your head
	// must puff dust even if it is technically off-screen.
	if (DistanceSquared < FMath::Square(600.f))
	{
		return false;
	}

	const FVector ToEffect = (Location - ViewerLocation).GetSafeNormal();
	const FVector ViewDirection = PC->GetControlRotation().Vector();
	const float Dot = FVector::DotProduct(ToEffect, ViewDirection);

	// Behind the camera and more than a few metres away: cull. The margin is
	// generous on purpose - the player should never hear nothing and see nothing
	// because an effect was rejected by a cone test.
	return Dot < -0.35f;
}

// ===========================================================================
//  Generic spawn
// ===========================================================================

UNiagaraComponent* UOneBrahamVFXStatics::SpawnEffect(UObject* WorldContextObject, EOneBrahamVFXEffect Effect, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}

	if (IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(Effect)))
	{
		return nullptr;
	}

	UNiagaraSystem* System = LoadEffectSystem(Effect);
	if (System == nullptr)
	{
		// The caller is expected to fall back to the C++ effect actor. Returning
		// null here is the signal for that, not an error.
		return nullptr;
	}

	return UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, System, Location, Rotation, Scale,
		/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::AutoRelease, /*bPreCullCheck*/ false);
}

// ===========================================================================
//  Impacts
// ===========================================================================

void UOneBrahamVFXStatics::SpawnImpactsForHits(UObject* WorldContextObject, const TArray<FOneBrahamImpactInfo>& Impacts)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return;
	}

	// Budget: one burst per impact, but never more than 24 in a single multicast.
	// A shotgun at point blank into a plasterboard wall would otherwise ask for
	// one effect per pellet.
	const int32 Count = FMath::Min(Impacts.Num(), 24);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		SpawnImpact(WorldContextObject, Impacts[Index]);
	}
}

void UOneBrahamVFXStatics::SpawnImpact(UObject* WorldContextObject, const FOneBrahamImpactInfo& Impact)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const FVector Location = Impact.Location;
	const FRotator Rotation = Impact.Normal.Rotation();
	const bool bCharacterHit = Impact.bHitCharacter;

	const EOneBrahamVFXEffect Effect = bCharacterHit
		? EOneBrahamVFXEffect::FleshImpact
		: ImpactEffectForSurface(Impact.Surface);

	const float Intensity = GetVFXIntensity();

	// --- Effect -----------------------------------------------------------
	bool bSpawnedNiagara = false;
	if (Intensity >= 0.35f)
	{
		if (UNiagaraComponent* Spawned = SpawnEffect(WorldContextObject, Effect, Location, Rotation, FVector(FMath::Lerp(0.8f, 1.f, Intensity))))
		{
			bSpawnedNiagara = true;

			// Every impact system exposes these two parameters by convention.
			Spawned->SetVariableFloat(FName(TEXT("ImpactScale")), Intensity);
			Spawned->SetVariableBool(FName(TEXT("bPenetrated")), Impact.bPenetrated);
		}
	}

	// --- Fallback ---------------------------------------------------------
	if (!bSpawnedNiagara)
	{
		if (AOneBrahamImpactFallbackActor* Fallback = AOneBrahamImpactFallbackActor::SpawnForImpact(World, Impact, Intensity))
		{
			// Nothing else to do: the actor manages its own lifetime.
			(void)Fallback;
		}
	}

	// --- Sparks on live equipment -----------------------------------------
	// A server rack, a riser cupboard and a lift motor all have 400 V in them.
	// The impact family stays metal (that is what the hole looks like) and the
	// sparks are a second, short-lived burst on top, because a rack that sparks
	// when shot is the single cheapest way to make floors 9-11 feel dangerous.
	if (!bCharacterHit && Intensity > 0.45f && SurfaceSparks(Impact.Surface))
	{
		SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::SparkShower, Location, Rotation, FVector(1.f));
	}

	// --- Blood mist on a character hit ------------------------------------
	if (bCharacterHit && Intensity > 0.5f)
	{
		SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::BloodMist, Location, Rotation, FVector(1.f));
	}

	// --- Decal ------------------------------------------------------------
	// Decals only on static world geometry: painting one on a person or an
	// explosive barrel is a bug the player will photograph.
	if (!bCharacterHit && Intensity > 0.4f && Impact.bHitGlass == false)
	{
		const FString DecalPath = GetDecalPathForSurface(Impact.Surface);
		if (UMaterialInterface* DecalMaterial = LoadObject<UMaterialInterface>(nullptr, *DecalPath))
		{
			UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(
				World, DecalMaterial, FVector(6.f, 6.f, 6.f), Location, Rotation, 25.f);

			if (Decal != nullptr)
			{
				// Random roll so two holes in the same wall are not identical, and
				// a small nudge along the normal to avoid z-fighting on the surface.
				Decal->SetRelativeRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
				Decal->SetWorldLocation(Location + Impact.Normal * 1.5f);
			}
		}
	}

	// --- Audio ------------------------------------------------------------
	// Impacts are played by every machine from this one replicated entry point,
	// so the sound and the dust always agree about where the bullet landed.
	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(World))
	{
		Audio->PlayImpactSound(Impact, /*VolumeScale*/ 1.f);
	}
}

void UOneBrahamVFXStatics::PlayNearMiss(UObject* WorldContextObject, const FVector& Location)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return;
	}

	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(World))
	{
		Audio->PlayNearMissWhizz(Location);
	}
}

// ===========================================================================
//  Explosions and utility
// ===========================================================================

void UOneBrahamVFXStatics::SpawnExplosion(UObject* WorldContextObject, const FVector& Location, float RadiusCm, bool bBreach)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const EOneBrahamVFXEffect Effect = bBreach ? EOneBrahamVFXEffect::BreachExplosion : EOneBrahamVFXEffect::FragExplosion;
	const float Intensity = GetVFXIntensity();
	const float Scale = FMath::Clamp(RadiusCm / 420.f, 0.6f, 2.2f);

	bool bSpawnedNiagara = false;
	if (Intensity >= 0.3f)
	{
		if (UNiagaraComponent* Spawned = SpawnEffect(WorldContextObject, Effect, Location, FRotator::ZeroRotator, FVector(Scale)))
		{
			bSpawnedNiagara = true;
			Spawned->SetVariableFloat(FName(TEXT("BlastRadiusCm")), RadiusCm);
		}
	}

	if (!bSpawnedNiagara)
	{
		// Fallback: a short, bright expanding flash plus smoke. Two actors, no
		// assets, and it reads correctly at forty metres.
		FOneBrahamImpactInfo FakeImpact;
		FakeImpact.Location = Location;
		FakeImpact.Normal = FVector::UpVector;
		FakeImpact.Surface = bBreach ? EOneBrahamFootstepSurface::Plasterboard : EOneBrahamFootstepSurface::Concrete;

		AOneBrahamImpactFallbackActor::SpawnForImpact(World, FakeImpact, Intensity, /*bExplosive*/ true);
	}

	// Dust and debris ring: what actually sells a blast inside a building is not
	// the fireball, it is the plasterboard that falls out of the ceiling.
	SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::DustPuff, Location, FRotator::ZeroRotator, FVector(Scale * 1.6f));
	SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::SparkShower, Location, FRotator::ZeroRotator, FVector(Scale));
}

void UOneBrahamVFXStatics::SpawnFlashbang(UObject* WorldContextObject, const FVector& Location, float RadiusCm)
{
	SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::FlashbangBurst, Location, FRotator::ZeroRotator, FVector(FMath::Clamp(RadiusCm / 1400.f, 0.7f, 1.6f)));
}

void UOneBrahamVFXStatics::SpawnSmokeCloud(UObject* WorldContextObject, const FVector& Location, float RadiusCm, float LifetimeSeconds)
{
	UNiagaraComponent* Smoke = SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::SmokeCloud, Location, FRotator::ZeroRotator, FVector(FMath::Clamp(RadiusCm / 620.f, 0.7f, 1.6f)));

	if (Smoke != nullptr)
	{
		Smoke->SetVariableFloat(FName(TEXT("CloudRadiusCm")), RadiusCm);
		Smoke->SetVariableFloat(FName(TEXT("CloudLifetime")), LifetimeSeconds);
	}
	else
	{
		// No smoke asset: a dust fallback keeps the *gameplay* promise that smoke
		// breaks line of sight visually, which matters more than how it looks.
		SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::DustPuff, Location, FRotator::ZeroRotator, FVector(2.4f));
	}
}

void UOneBrahamVFXStatics::SpawnGlassShatter(UObject* WorldContextObject, const FVector& Location, const FVector& Normal, float AreaCm2)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const FVector Scale = FVector(FMath::Clamp(AreaCm2 / 12000.f, 0.8f, 2.5f));
	SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::GlassImpact, Location, Normal.Rotation(), Scale);

	// Glass is loud and it is a commitment: breaking a partition on floor 9 tells
	// the entire floor that somebody is coming through it.
	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(World))
	{
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Window_Break")), Location, FMath::Clamp(Scale.X, 0.8f, 1.6f), FMath::FRandRange(0.95f, 1.05f), nullptr);
	}
}

void UOneBrahamVFXStatics::SpawnBreachDebris(UObject* WorldContextObject, const FVector& Location, const FVector& Normal)
{
	SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::BreachDebris, Location, Normal.Rotation(), FVector(1.2f));
	SpawnEffect(WorldContextObject, EOneBrahamVFXEffect::DustPuff, Location, Normal.Rotation(), FVector(1.8f));
}
