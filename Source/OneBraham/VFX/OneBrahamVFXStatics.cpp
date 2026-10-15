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
#include "VFX/OneBrahamCameraShake.h"
#include "VFX/OneBrahamImpactFallbackActor.h"
#include "VFX/OneBrahamVFXSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraEmitterHandle.h"

// ===========================================================================
//  ob.Gore
// ===========================================================================
// Registered here, next to the only code that reads it, rather than in the
// user settings: the settings screen (owned by UI/GameUserSettings) can drive
// it with a single console command, and a platform or a rating board that
// needs blood off can force it from DefaultEngine.ini's [ConsoleVariables]
// without a code change.
namespace
{
	int32 GOneBrahamGore = 1;

	void OnGoreChanged(IConsoleVariable* Variable)
	{
		if (GOneBrahamGore != 0 || GEngine == nullptr)
		{
			return;
		}

		// Switching gore off mid-match clears what is already on the walls:
		// "off" has to mean off, not "no more from now on".
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(Context.World()))
			{
				Subsystem->ClearBlood();
			}
		}
	}

	FAutoConsoleVariableRef CVarOneBrahamGore(
		TEXT("ob.Gore"),
		GOneBrahamGore,
		TEXT("Blood and gore. 1 = on (default): blood spray, splatter and drip decals, pools under downed and dead ")
		TEXT("bodies, wound shading. 0 = off: none of it; character hits show a neutral fabric puff instead."),
		FConsoleVariableDelegate::CreateStatic(&OnGoreChanged),
		ECVF_Default);
}

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

	if (System == nullptr)
	{
		// Logged once per effect, then never again: a missing VFX asset must not
		// produce a log line per bullet in a twelve-player firefight.
		GMissingEffects.Add(Effect);
		UE_LOG(LogOneBraham, Log,
			TEXT("[OB.VFX] '%s' is not in the project yet - using the runtime fallback effect. "
				 "See Docs/11_Niagara_VFX.md for the system spec."), *Path);
		return nullptr;
	}

	if (!IsNiagaraSystemUsable(System))
	{
		// The content build creates every system with its name and its user
		// parameters and *no emitters*, ready for the FX pass. Such a system
		// spawns, activates and draws nothing - so until an artist has filled it
		// in it counts as missing, and the procedural fallback plays instead.
		GMissingEffects.Add(Effect);
		UE_LOG(LogOneBraham, Log,
			TEXT("[OB.VFX] '%s' has no emitters yet (created empty by build_vfx.py) - using the runtime fallback effect."),
			*Path);
		return nullptr;
	}

	GEffectCache.Add(Effect, System);
	return System;
}

bool UOneBrahamVFXStatics::IsNiagaraSystemUsable(const UNiagaraSystem* System)
{
	if (System == nullptr)
	{
		return false;
	}

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetIsEnabled())
		{
			return true;
		}
	}

	return false;
}

bool UOneBrahamVFXStatics::IsGoreEnabled()
{
	return GOneBrahamGore != 0;
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

	// A dedicated server has no viewer and draws nothing (Docs/11 section 6,
	// rule 5). Without this check the "no player controller" branch below
	// would happily spawn every effect on the server.
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return true;
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	if (PC == nullptr)
	{
		return false;
	}

	// The camera, not the pawn: a dead player spectating a teammate must see the
	// teammate's fight, not the empty room their own corpse is lying in.
	FVector ViewerLocation;
	if (PC->PlayerCameraManager != nullptr)
	{
		ViewerLocation = PC->PlayerCameraManager->GetCameraLocation();
	}
	else if (PC->GetPawn() != nullptr)
	{
		ViewerLocation = PC->GetPawn()->GetActorLocation();
	}
	else
	{
		return false;
	}

	// Effects behind the camera and far away are the cheapest win in the file.
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
	const FVector ViewDirection = PC->PlayerCameraManager != nullptr
		? PC->PlayerCameraManager->GetCameraRotation().Vector()
		: PC->GetControlRotation().Vector();
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
		// Null is still the return value (there is no Niagara component to hand
		// back), but the ambient families that callers outside this file spawn
		// directly - a shutter landing, a body hitting the floor, a rack
		// shorting - get their procedural stand-in here, because none of those
		// callers has a fallback of its own. Explosions, flashes and smoke have
		// dedicated entry points that do their own fallback and are not doubled.
		const FVector Normal = Rotation.Vector();
		switch (Effect)
		{
		case EOneBrahamVFXEffect::DustPuff:
			AOneBrahamImpactFallbackActor::SpawnDustPuff(World, Location, FVector::UpVector, Scale.X, FLinearColor(0.62f, 0.6f, 0.56f));
			break;
		case EOneBrahamVFXEffect::CorpseImpact:
			AOneBrahamImpactFallbackActor::SpawnDustPuff(World, Location, FVector::UpVector, 0.6f * Scale.X, FLinearColor(0.4f, 0.38f, 0.35f));
			break;
		case EOneBrahamVFXEffect::SparkShower:
			AOneBrahamImpactFallbackActor::SpawnSparkShower(World, Location, Normal, Scale.X);
			break;
		case EOneBrahamVFXEffect::BreachDebris:
		case EOneBrahamVFXEffect::WaterSpray:
		{
			FOneBrahamImpactInfo Impact;
			Impact.Location = Location;
			Impact.Normal = Normal.IsNearlyZero() ? FVector::UpVector : Normal;
			Impact.Surface = Effect == EOneBrahamVFXEffect::WaterSpray ? EOneBrahamFootstepSurface::Puddle : EOneBrahamFootstepSurface::Plasterboard;
			AOneBrahamImpactFallbackActor::SpawnForImpact(World, Impact, 1.f);
			break;
		}
		default:
			break;
		}
		return nullptr;
	}

	return UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, System, Location, Rotation, Scale,
		/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::AutoRelease, /*bPreCullCheck*/ false);
}


// ===========================================================================
//  Local helpers
// ===========================================================================

namespace
{
	UWorld* WorldOf(const UObject* WorldContextObject)
	{
		return GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	}

	/** Spawns a usable Niagara system for an effect, or returns null (caller falls back).
	 *  Culling has already been decided by the caller. */
	UNiagaraComponent* SpawnNiagaraIfUsable(UWorld* World, EOneBrahamVFXEffect Effect, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
	{
		UNiagaraSystem* System = UOneBrahamVFXStatics::LoadEffectSystem(Effect);
		if (System == nullptr)
		{
			return nullptr;
		}

		return UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location, Rotation, Scale,
			/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::AutoRelease, /*bPreCullCheck*/ false);
	}

	/** A small camera flinch on this machine, honouring the accessibility settings. */
	void PlayLocalFlinch(UWorld* World, float Scale)
	{
		APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		if (PC == nullptr || PC->PlayerCameraManager == nullptr || Scale <= 0.f)
		{
			return;
		}

		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		if (Settings != nullptr && !Settings->bCameraShake)
		{
			return;
		}

		const float UserScale = Settings != nullptr
			? Settings->CameraShakeIntensity * (Settings->bScreenShakeReduction ? 0.3f : 1.f)
			: 1.f;

		PC->PlayerCameraManager->StartCameraShake(UOneBrahamCameraShake::StaticClass(), Scale * UserScale);
	}

	bool IsHeadBone(FName Bone)
	{
		const FString Name = Bone.ToString();
		return Name.Contains(TEXT("head")) || Name.Contains(TEXT("neck")) || Name.Contains(TEXT("face"));
	}

	/** The building only: world static and world dynamic, never a pawn. */
	FCollisionObjectQueryParams BuildingObjects()
	{
		FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}

	/** Blood never paints a person, a weapon or a physics prop that will roll away. */
	bool CanCarryBlood(const FHitResult& Hit)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (Component == nullptr || Cast<APawn>(Hit.GetActor()) != nullptr)
		{
			return false;
		}
		return !Component->IsSimulatingPhysics() && Component->bReceivesDecals;
	}

	const FLinearColor FabricPuff(0.32f, 0.31f, 0.3f);

	constexpr float BloodCullDistance = 5000.f;
	constexpr float BloodSplatterHoldSeconds = 45.f;
}

// ===========================================================================
//  Impacts
// ===========================================================================

void UOneBrahamVFXStatics::SpawnImpactsForHits(UObject* WorldContextObject, const TArray<FOneBrahamImpactInfo>& Impacts, const FVector& ShotOrigin)
{
	UWorld* World = WorldOf(WorldContextObject);
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
		SpawnImpact(WorldContextObject, Impacts[Index], ShotOrigin);
	}
}

float UOneBrahamVFXStatics::RicochetChance(EOneBrahamFootstepSurface Surface, const FVector& IncomingDirection, const FVector& Normal)
{
	float SurfaceFactor = 0.f;
	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::ServerRack:
	case EOneBrahamFootstepSurface::Decking:
		SurfaceFactor = 1.f;
		break;

	case EOneBrahamFootstepSurface::Concrete:
	case EOneBrahamFootstepSurface::Marble:
		SurfaceFactor = 0.55f;
		break;

	default:
		return 0.f;
	}

	// Sine of the angle between the path and the surface. A round has to arrive
	// within about 22 degrees of the surface to glance off it at all; the
	// shallower the angle, the likelier.
	const float SinGrazing = FMath::Abs(FVector::DotProduct(IncomingDirection.GetSafeNormal(), Normal.GetSafeNormal()));
	const float MaxSin = 0.375f;
	if (SinGrazing >= MaxSin)
	{
		return 0.f;
	}

	return SurfaceFactor * FMath::Lerp(0.7f, 0.12f, SinGrazing / MaxSin);
}

void UOneBrahamVFXStatics::SpawnImpact(UObject* WorldContextObject, const FOneBrahamImpactInfo& Impact, const FVector& ShotOrigin)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FVector Location = Impact.Location;
	const FVector Normal = FVector(Impact.Normal).GetSafeNormal();
	const FRotator Rotation = Normal.Rotation();
	const bool bCharacterHit = Impact.bHitCharacter;
	const FVector ShotDirection = ShotOrigin.IsNearlyZero() ? FVector::ZeroVector : (Location - ShotOrigin).GetSafeNormal();

	// --- Audio ------------------------------------------------------------
	// Impacts are played by every machine from this one replicated entry point,
	// so the sound and the dust always agree about where the bullet landed. It
	// is played before the visual cull: you hear rounds land behind you.
	if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->PlayImpactSound(Impact, /*VolumeScale*/ 1.f);
		}
	}

	const EOneBrahamVFXEffect Effect = bCharacterHit
		? EOneBrahamVFXEffect::FleshImpact
		: ImpactEffectForSurface(Impact.Surface);

	if (IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(Effect)))
	{
		return;
	}

	const float Intensity = GetVFXIntensity();
	const bool bNiagaraTier = Intensity >= 0.35f;

	// --- Character hit ----------------------------------------------------
	if (bCharacterHit)
	{
		if (!IsGoreEnabled())
		{
			// Gore off: the shooter still needs to see that the round landed.
			if (!bNiagaraTier || SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::DustPuff, Location, Rotation, FVector(0.35f)) == nullptr)
			{
				AOneBrahamImpactFallbackActor::SpawnDustPuff(World, Location, Normal, 0.35f, FabricPuff);
			}
			return;
		}

		bool bSpawnedNiagara = false;
		if (bNiagaraTier)
		{
			const FRotator SprayRotation = ShotDirection.IsNearlyZero() ? Rotation : ShotDirection.Rotation();
			if (UNiagaraComponent* Spawned = SpawnNiagaraIfUsable(World, Effect, Location, SprayRotation, FVector(FMath::Lerp(0.8f, 1.f, Intensity))))
			{
				bSpawnedNiagara = true;
				Spawned->SetVariableFloat(FName(TEXT("ImpactScale")), Intensity);
				Spawned->SetVariableBool(FName(TEXT("bPenetrated")), Impact.bPenetrated);

				if (Intensity > 0.5f)
				{
					SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::BloodMist, Location, SprayRotation, FVector(1.f));
				}
			}
		}

		if (!bSpawnedNiagara)
		{
			AOneBrahamImpactFallbackActor::SpawnFlesh(World, Location, Normal,
				ShotDirection.IsNearlyZero() ? -Normal : ShotDirection, 0.7f, IsHeadBone(Impact.HitBoneName), Intensity);
		}
		return;
	}

	// --- Surface effect ---------------------------------------------------
	bool bSpawnedNiagara = false;
	if (bNiagaraTier)
	{
		if (UNiagaraComponent* Spawned = SpawnNiagaraIfUsable(World, Effect, Location, Rotation, FVector(FMath::Lerp(0.8f, 1.f, Intensity))))
		{
			bSpawnedNiagara = true;

			// Every impact system exposes these two parameters by convention.
			Spawned->SetVariableFloat(FName(TEXT("ImpactScale")), Intensity);
			Spawned->SetVariableBool(FName(TEXT("bPenetrated")), Impact.bPenetrated);
		}
	}

	if (!bSpawnedNiagara)
	{
		AOneBrahamImpactFallbackActor::SpawnForImpact(World, Impact, Intensity, /*bExplosive*/ false, ShotDirection);
	}

	// --- Sparks on live equipment -----------------------------------------
	// A server rack, a riser cupboard and a lift motor all have 400 V in them.
	// The impact family stays metal (that is what the hole looks like) and the
	// sparks are a second, short-lived burst on top, because a rack that sparks
	// when shot is the single cheapest way to make floors 9-11 feel dangerous.
	if (Intensity > 0.45f && SurfaceSparks(Impact.Surface))
	{
		if (SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::SparkShower, Location, Rotation, FVector(1.f)) == nullptr)
		{
			AOneBrahamImpactFallbackActor::SpawnSparkShower(World, Location, Normal, 0.6f);
		}
	}

	// --- Ricochet ---------------------------------------------------------
	// Decided on each machine: it is cosmetic and carries no damage, so it
	// does not need to agree between clients, and deciding it here costs no
	// bandwidth. The shot origin is the weapon's muzzle, which every machine has.
	if (!ShotDirection.IsNearlyZero() && !Impact.bPenetrated)
	{
		const float Chance = RicochetChance(Impact.Surface, ShotDirection, Normal);
		if (Chance > 0.f && FMath::FRand() < Chance)
		{
			const FVector Reflected = FMath::GetReflectionVector(ShotDirection, Normal);
			const FVector Out = FMath::VRandCone(Reflected, FMath::DegreesToRadians(10.f));
			SpawnRicochet(WorldContextObject, Location, Normal, Out);
		}
	}

	// --- Decal ------------------------------------------------------------
	// Decals only on world geometry: painting one on a person or an explosive
	// barrel is a bug the player will photograph.
	if (Intensity > 0.4f && !Impact.bHitGlass)
	{
		const FString DecalPath = GetDecalPathForSurface(Impact.Surface);
		if (UMaterialInterface* DecalMaterial = LoadObject<UMaterialInterface>(nullptr, *DecalPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			// Project *into* the surface along the normal, with a random roll so
			// two holes in the same wall are not identical. This used to keep only
			// the yaw, which projected every floor and ceiling hole sideways.
			const FRotator DecalRotation = UOneBrahamVFXSubsystem::DecalRotationForSurface(Normal, FVector::ZeroVector, FMath::FRandRange(-180.f, 180.f));

			UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(
				World, DecalMaterial, FVector(6.f, 6.f, 6.f), Location + Normal * 1.5f, DecalRotation, 25.f);

			if (UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(World))
			{
				Subsystem->RegisterBulletHole(Decal);
			}
		}
	}
}

void UOneBrahamVFXStatics::SpawnRicochet(UObject* WorldContextObject, const FVector& Location, const FVector& Normal, const FVector& OutDirection)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || IsCulledByViewer(WorldContextObject, Location, 6000.f))
	{
		return;
	}

	AOneBrahamImpactFallbackActor::SpawnRicochet(World, Location, Normal, OutDirection);

	// The whine of a round leaving: the whizz cue, pitched up, a metre along
	// the new path so it pans with the round.
	if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Bullet_Whizz")), Location + OutDirection * 100.f, 0.6f, FMath::FRandRange(1.15f, 1.4f), nullptr);
		}
	}
}

void UOneBrahamVFXStatics::PlayNearMiss(UObject* WorldContextObject, const FVector& Location)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->PlayNearMissWhizz(Location);
		}
	}

	// Suppression: a flinch so small it is felt rather than seen. Enough to make
	// a player want to be somewhere else; not enough to take their aim away.
	PlayLocalFlinch(World, 0.12f);
}

void UOneBrahamVFXStatics::PlayLocalCameraFlinch(UObject* WorldContextObject, float Scale)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World != nullptr && World->GetNetMode() != NM_DedicatedServer)
	{
		PlayLocalFlinch(World, Scale);
	}
}

// ===========================================================================
//  Blood
// ===========================================================================

void UOneBrahamVFXStatics::SpawnBloodForHit(UObject* WorldContextObject, const AActor* Victim, const FVector& Location, const FVector& ShotDirection,
	float Damage, bool bHeadshot)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || !IsGoreEnabled() || IsCulledByViewer(WorldContextObject, Location, BloodCullDistance))
	{
		return;
	}

	UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(World);
	if (Subsystem == nullptr)
	{
		return;
	}

	const FVector Shot = ShotDirection.GetSafeNormal();
	if (Shot.IsNearlyZero())
	{
		return;
	}

	// Severity: a 9 mm torso hit is about 0.7, a carbine headshot about 1.6.
	const float Severity = FMath::Clamp(Damage / 45.f, 0.3f, 1.2f) * (bHeadshot ? 1.4f : 1.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBloodTrace), /*bTraceComplex*/ false);
	if (Victim != nullptr)
	{
		Params.AddIgnoredActor(Victim);
	}

	// --- Splatter behind the victim ---------------------------------------
	// Where the round (and what it carried) went: up to 2.5 m along the shot,
	// 3 m for a headshot. The mark streaks along the shot and is longer the
	// shallower it lands, which is what makes it read as "came from over there".
	const float Reach = bHeadshot ? 300.f : 250.f;
	const int32 Sprays = Severity > 1.f ? 3 : (Severity > 0.6f ? 2 : 1);

	for (int32 Index = 0; Index < Sprays; ++Index)
	{
		const FVector Direction = Index == 0 ? Shot : FMath::VRandCone(Shot, FMath::DegreesToRadians(14.f));

		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Location + Direction * 5.f, Location + Direction * Reach, BuildingObjects(), Params)
			|| !CanCarryBlood(Hit))
		{
			continue;
		}

		const float Travelled = FMath::Clamp(Hit.Distance / Reach, 0.f, 1.f);
		const float Glance = 1.f - FMath::Abs(FVector::DotProduct(Direction, Hit.ImpactNormal));
		const float Radius = FMath::Lerp(12.f, 30.f, FMath::Clamp(Severity / 1.6f, 0.f, 1.f))
			* FMath::Lerp(0.8f, 1.3f, Travelled)
			* (Index == 0 ? 1.f : 0.55f);

		Subsystem->SpawnBloodDecal(Hit.ImpactPoint, Hit.ImpactNormal, Direction, Radius, 1.f + Glance * 1.8f,
			BloodSplatterHoldSeconds, Hit.GetComponent());
	}

	// --- Drips beneath ----------------------------------------------------
	// A few drops fall straight down from the wound, slightly ahead of it.
	const int32 Drips = 1 + FMath::RoundToInt(Severity * 2.f);
	for (int32 Index = 0; Index < Drips; ++Index)
	{
		const FVector Offset = FVector(Shot.X, Shot.Y, 0.f) * FMath::FRandRange(0.f, 45.f)
			+ FVector(FMath::FRandRange(-18.f, 18.f), FMath::FRandRange(-18.f, 18.f), 0.f);
		SpawnBloodDrip(WorldContextObject, Victim, Location + Offset, FMath::FRandRange(3.f, 7.f) * (bHeadshot ? 1.3f : 1.f));
	}
}

void UOneBrahamVFXStatics::SpawnBloodDrip(UObject* WorldContextObject, const AActor* IgnoreActor, const FVector& Above, float RadiusCm)
{
	UWorld* World = WorldOf(WorldContextObject);
	UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(World);
	if (Subsystem == nullptr || !IsGoreEnabled())
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBloodDrip), false);
	if (IgnoreActor != nullptr)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, Above, Above - FVector(0.f, 0.f, 300.f), BuildingObjects(), Params)
		&& CanCarryBlood(Hit) && Hit.ImpactNormal.Z > 0.5f)
	{
		Subsystem->SpawnBloodDecal(Hit.ImpactPoint, Hit.ImpactNormal, FVector::ZeroVector, RadiusCm, FMath::FRandRange(1.f, 1.3f),
			BloodSplatterHoldSeconds, Hit.GetComponent());
	}
}

void UOneBrahamVFXStatics::SpawnBloodPool(UObject* WorldContextObject, const FVector& FloorLocation, float RadiusCm, float GrowSeconds, float HoldSeconds)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || !IsGoreEnabled() || IsCulledByViewer(WorldContextObject, FloorLocation, BloodCullDistance * 1.5f))
	{
		return;
	}

	if (UOneBrahamVFXSubsystem* Subsystem = UOneBrahamVFXSubsystem::Get(World))
	{
		Subsystem->SpawnBloodDecal(FloorLocation, FVector::UpVector, FVector::ZeroVector, RadiusCm, FMath::FRandRange(1.1f, 1.45f),
			HoldSeconds, nullptr, /*bPool*/ true, GrowSeconds);
	}
}

// ===========================================================================
//  Weapon fallbacks
// ===========================================================================

void UOneBrahamVFXStatics::SpawnMuzzleFlashFallback(UObject* WorldContextObject, const FTransform& Muzzle, float Scale)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World != nullptr && !IsCulledByViewer(WorldContextObject, Muzzle.GetLocation(), CullDistanceFor(EOneBrahamVFXEffect::MuzzleFlash)))
	{
		AOneBrahamImpactFallbackActor::SpawnMuzzleFlash(World, Muzzle, Scale);
	}
}

void UOneBrahamVFXStatics::SpawnTracerFallback(UObject* WorldContextObject, const FVector& Start, const FVector& End, float SpeedCmPerSecond)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World != nullptr && !IsCulledByViewer(WorldContextObject, Start, CullDistanceFor(EOneBrahamVFXEffect::Tracer)))
	{
		AOneBrahamImpactFallbackActor::SpawnTracer(World, Start, End, SpeedCmPerSecond);
	}
}

void UOneBrahamVFXStatics::SpawnShellEjectFallback(UObject* WorldContextObject, const FTransform& EjectPort, const FVector& InheritedVelocity, float CalibreScale)
{
	UWorld* World = WorldOf(WorldContextObject);

	// Brass is the first thing to go on a busy screen: it has the shortest cull
	// distance of any weapon effect and is skipped entirely on the Low preset.
	if (World != nullptr && GetVFXIntensity() > 0.3f
		&& !IsCulledByViewer(WorldContextObject, EjectPort.GetLocation(), 2500.f))
	{
		AOneBrahamImpactFallbackActor::SpawnShellEject(World, EjectPort, InheritedVelocity, CalibreScale);
	}
}

// ===========================================================================
//  Explosions and utility
// ===========================================================================

void UOneBrahamVFXStatics::SpawnExplosion(UObject* WorldContextObject, const FVector& Location, float RadiusCm, bool bBreach)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr)
	{
		return;
	}

	const EOneBrahamVFXEffect Effect = bBreach ? EOneBrahamVFXEffect::BreachExplosion : EOneBrahamVFXEffect::FragExplosion;
	if (IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(Effect)))
	{
		return;
	}

	const float Intensity = GetVFXIntensity();
	const float Scale = FMath::Clamp(RadiusCm / 420.f, 0.6f, 2.2f);

	bool bSpawnedNiagara = false;
	if (Intensity >= 0.3f)
	{
		if (UNiagaraComponent* Spawned = SpawnNiagaraIfUsable(World, Effect, Location, FRotator::ZeroRotator, FVector(Scale)))
		{
			bSpawnedNiagara = true;
			Spawned->SetVariableFloat(FName(TEXT("BlastRadiusCm")), RadiusCm);
		}
	}

	if (!bSpawnedNiagara)
	{
		// Fallback: flash, fireball, rolling smoke, physics debris and sparks -
		// one pooled actor, no assets, and it reads correctly at forty metres.
		AOneBrahamImpactFallbackActor::SpawnExplosion(World, Location, FVector::UpVector, RadiusCm, bBreach, Intensity);
		return;
	}

	// Dust and debris ring: what actually sells a blast inside a building is not
	// the fireball, it is the plasterboard that falls out of the ceiling.
	SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::DustPuff, Location, FRotator::ZeroRotator, FVector(Scale * 1.6f));
	SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::SparkShower, Location, FRotator::ZeroRotator, FVector(Scale));
}

void UOneBrahamVFXStatics::SpawnFlashbang(UObject* WorldContextObject, const FVector& Location, float RadiusCm)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(EOneBrahamVFXEffect::FlashbangBurst)))
	{
		return;
	}

	if (SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::FlashbangBurst, Location, FRotator::ZeroRotator,
		FVector(FMath::Clamp(RadiusCm / 1400.f, 0.7f, 1.6f))) == nullptr)
	{
		AOneBrahamImpactFallbackActor::SpawnFlashbang(World, Location, RadiusCm);
	}
}

void UOneBrahamVFXStatics::SpawnSmokeCloud(UObject* WorldContextObject, const FVector& Location, float RadiusCm, float LifetimeSeconds)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(EOneBrahamVFXEffect::SmokeCloud)))
	{
		return;
	}

	UNiagaraComponent* Smoke = SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::SmokeCloud, Location, FRotator::ZeroRotator,
		FVector(FMath::Clamp(RadiusCm / 620.f, 0.7f, 1.6f)));

	if (Smoke != nullptr)
	{
		Smoke->SetVariableFloat(FName(TEXT("CloudRadiusCm")), RadiusCm);
		Smoke->SetVariableFloat(FName(TEXT("CloudLifetime")), LifetimeSeconds);
	}
	else
	{
		// No smoke asset: the procedural cloud keeps the *gameplay* promise that
		// smoke breaks line of sight, which matters more than how it looks.
		AOneBrahamImpactFallbackActor::SpawnSmoke(World, Location, RadiusCm, LifetimeSeconds);
	}
}

void UOneBrahamVFXStatics::SpawnGlassShatter(UObject* WorldContextObject, const FVector& Location, const FVector& Normal, float AreaCm2)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr)
	{
		return;
	}

	const FVector Scale = FVector(FMath::Clamp(AreaCm2 / 12000.f, 0.8f, 2.5f));

	if (!IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(EOneBrahamVFXEffect::GlassImpact))
		&& SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::GlassImpact, Location, Normal.Rotation(), Scale) == nullptr)
	{
		// Two glass impacts, one each side of the pane: a breaking partition
		// sheds on both sides.
		FOneBrahamImpactInfo Shatter;
		Shatter.Location = Location;
		Shatter.Normal = Normal;
		Shatter.Surface = EOneBrahamFootstepSurface::Glass;
		AOneBrahamImpactFallbackActor::SpawnForImpact(World, Shatter, 1.f, false, -Normal);
		Shatter.Normal = -Normal;
		AOneBrahamImpactFallbackActor::SpawnForImpact(World, Shatter, 1.f, false, Normal);
	}

	// Glass is loud and it is a commitment: breaking a partition on floor 9 tells
	// the entire floor that somebody is coming through it.
	if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Window_Break")), Location, FMath::Clamp(Scale.X, 0.8f, 1.6f), FMath::FRandRange(0.95f, 1.05f), nullptr);
		}
	}
}

void UOneBrahamVFXStatics::SpawnBreachDebris(UObject* WorldContextObject, const FVector& Location, const FVector& Normal)
{
	UWorld* World = WorldOf(WorldContextObject);
	if (World == nullptr || IsCulledByViewer(WorldContextObject, Location, CullDistanceFor(EOneBrahamVFXEffect::BreachDebris)))
	{
		return;
	}

	const bool bDebris = SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::BreachDebris, Location, Normal.Rotation(), FVector(1.2f)) != nullptr;
	const bool bDust = SpawnNiagaraIfUsable(World, EOneBrahamVFXEffect::DustPuff, Location, Normal.Rotation(), FVector(1.8f)) != nullptr;

	if (!bDebris)
	{
		FOneBrahamImpactInfo Breach;
		Breach.Location = Location;
		Breach.Normal = Normal;
		Breach.Surface = EOneBrahamFootstepSurface::Plasterboard;
		AOneBrahamImpactFallbackActor::SpawnForImpact(World, Breach, 1.f, false, -Normal);
	}

	if (!bDust)
	{
		AOneBrahamImpactFallbackActor::SpawnDustPuff(World, Location, Normal, 1.8f, FLinearColor(0.78f, 0.76f, 0.72f));
	}
}
