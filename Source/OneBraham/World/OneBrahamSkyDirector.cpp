// Copyright (c) One Braham. All Rights Reserved.

#include "World/OneBrahamSkyDirector.h"

#include "OneBraham.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** The dome's master material (build_district.build_sky_material). */
	const FName OBSkyDomeMaterialName(TEXT("M_OB_SkyDome"));

	/** The dusk photograph as build_sky imports it: T_Sky_<Preset> in the Sky folder. */
	const TCHAR* OBDuskSkyCubePath = TEXT("/Game/OneBraham/Environment/Sky/T_Sky_Dusk.T_Sky_Dusk");

	/** Tick rate while the sky is settled: the stage is polled, nothing moves. */
	constexpr float OBSkyIdleTickSeconds = 0.25f;

	/** A skylight without real-time capture is recaptured this often during the blend. */
	constexpr float OBSkyRecaptureSeconds = 2.f;

	/** Below this the dome's photograph is the day's; at or above, the dusk's (single-cube material). */
	constexpr float OBSkyCubeSwapAlpha = 0.5f;

	bool UsesSkyDomeMaterial(const UStaticMeshComponent* Component)
	{
		if (Component == nullptr || Component->GetNumMaterials() == 0)
		{
			return false;
		}
		UMaterialInterface* Material = Component->GetMaterial(0);
		const UMaterial* Base = Material != nullptr ? Material->GetBaseMaterial() : nullptr;
		return Base != nullptr && Base->GetFName() == OBSkyDomeMaterialName;
	}

	UStaticMeshComponent* FindDomeComponent(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (UsesSkyDomeMaterial(Mesh))
			{
				return Mesh;
			}
		}
		return nullptr;
	}

	template <typename ActorType>
	ActorType* FirstActorOf(UWorld* World)
	{
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}
}

AOneBrahamSkyDirector::AOneBrahamSkyDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = OBSkyIdleTickSeconds;

	// Every machine derives the sky from the replicated lockdown stage; the
	// director itself carries no state worth sending.
	bReplicates = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	DuskSkyCube = TSoftObjectPtr<UTextureCube>(FSoftObjectPath(OBDuskSkyCubePath));

	// The dusk preset (build_district.SKY_PRESETS["dusk"]) and the Belfast
	// photograph's sun (SourceArt/open_assets.json): azimuth 217.62, 2.11 up.
	DuskState.SunRotation = FRotator(-2.11f, 127.62f, 0.f);
	DuskState.SunIntensityLux = 2500.f;
	DuskState.SunColour = FLinearColor(FColor(255, 196, 140));
	DuskState.SkyLightIntensity = 1.f;
	DuskState.FogDensity = 0.005f;
	DuskState.FogInscatteringLuminance = FLinearColor(855.f, 558.f, 378.f);
	DuskState.DomeLuminance = 900.f;
	DuskState.DomeHaze = FLinearColor(0.95f, 0.62f, 0.42f);
}

// ==========================================================================
//  Pure maths
// ==========================================================================

float AOneBrahamSkyDirector::AdvanceProgress(float InProgress, bool bTowardDusk, float DeltaSeconds, float InBlendSeconds)
{
	const float Target = bTowardDusk ? 1.f : 0.f;
	if (InBlendSeconds <= KINDA_SMALL_NUMBER)
	{
		return Target;
	}
	const float Step = FMath::Max(DeltaSeconds, 0.f) / InBlendSeconds;
	const float Clamped = FMath::Clamp(InProgress, 0.f, 1.f);
	return bTowardDusk ? FMath::Min(Clamped + Step, 1.f) : FMath::Max(Clamped - Step, 0.f);
}

float AOneBrahamSkyDirector::StepProgress(float InProgress, bool bFirstLook, EOneBrahamLockdownStage Stage,
	EOneBrahamLockdownStage Finale, float DeltaSeconds, float InBlendSeconds)
{
	const bool bFinale = IsFinaleStage(Stage, Finale);
	if (bFirstLook)
	{
		// The first look is snapped: a player joining mid-Purge arrives at
		// dusk, not at the start of a sunset the others saw minutes ago.
		return bFinale ? 1.f : 0.f;
	}
	if (Stage == EOneBrahamLockdownStage::Normal)
	{
		// Back to Normal is a new match (or QA resetting): it starts in daylight.
		return 0.f;
	}
	return AdvanceProgress(InProgress, bFinale, DeltaSeconds, InBlendSeconds);
}

float AOneBrahamSkyDirector::EaseProgress(float InProgress)
{
	const float X = FMath::Clamp(InProgress, 0.f, 1.f);
	return X * X * (3.f - 2.f * X);
}

float AOneBrahamSkyDirector::BlendLuminance(float A, float B, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	if (A <= 0.f || B <= 0.f)
	{
		return FMath::Lerp(A, B, T);
	}
	return A * FMath::Pow(B / A, T);
}

bool AOneBrahamSkyDirector::IsFinaleStage(EOneBrahamLockdownStage Stage, EOneBrahamLockdownStage Finale)
{
	return static_cast<uint8>(Stage) >= static_cast<uint8>(Finale);
}

FOneBrahamSkyState AOneBrahamSkyDirector::BlendSkyStates(const FOneBrahamSkyState& Day, const FOneBrahamSkyState& Dusk, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	if (T <= 0.f)
	{
		return Day;
	}
	if (T >= 1.f)
	{
		return Dusk;
	}

	FOneBrahamSkyState Out;
	Out.SunRotation = FQuat::Slerp(Day.SunRotation.Quaternion(), Dusk.SunRotation.Quaternion(), T).Rotator();
	Out.SunIntensityLux = BlendLuminance(Day.SunIntensityLux, Dusk.SunIntensityLux, T);
	Out.SunColour = FMath::Lerp(Day.SunColour, Dusk.SunColour, T);
	Out.SkyLightIntensity = BlendLuminance(Day.SkyLightIntensity, Dusk.SkyLightIntensity, T);
	Out.FogDensity = FMath::Lerp(Day.FogDensity, Dusk.FogDensity, T);
	Out.FogInscatteringLuminance = FLinearColor(
		BlendLuminance(Day.FogInscatteringLuminance.R, Dusk.FogInscatteringLuminance.R, T),
		BlendLuminance(Day.FogInscatteringLuminance.G, Dusk.FogInscatteringLuminance.G, T),
		BlendLuminance(Day.FogInscatteringLuminance.B, Dusk.FogInscatteringLuminance.B, T),
		1.f);
	Out.DomeLuminance = BlendLuminance(Day.DomeLuminance, Dusk.DomeLuminance, T);
	Out.DomeHaze = FMath::Lerp(Day.DomeHaze, Dusk.DomeHaze, T);
	return Out;
}

// ==========================================================================
//  Lifecycle
// ==========================================================================

void AOneBrahamSkyDirector::BeginPlay()
{
	Super::BeginPlay();

	// A dedicated server renders nothing; the stage it replicates is enough.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}

	ResolveScene();
	CaptureDayState();

	if (!DuskSkyCube.IsNull())
	{
		DuskCube = DuskSkyCube.LoadSynchronous();
	}
	if (DuskCube == nullptr)
	{
		OB_WARN("Sky director: dusk photograph %s not found - the light goes to dusk under the day's sky. "
			"Build the dusk preset (build_district.build_sky).", *DuskSkyCube.ToString());
	}

	if (bDomeCrossfades)
	{
		if (DuskCube != nullptr)
		{
			DomeMID->SetTextureParameterValue(DuskCubeParameter, DuskCube);
		}
		else
		{
			// Without the dusk photograph the material's second cube is a
			// placeholder: never fade the sky into it.
			bDomeCrossfades = false;
		}
	}

	OB_LOG("Sky director: sun %s, skylight %s, fog %s, dome %s (%s); dusk at %s over %.0f s.",
		Sun != nullptr ? TEXT("found") : TEXT("missing"),
		SkyLight != nullptr ? TEXT("found") : TEXT("missing"),
		HeightFog != nullptr ? TEXT("found") : TEXT("missing"),
		DomeMID != nullptr ? TEXT("found") : TEXT("missing"),
		bDomeCrossfades ? TEXT("crossfade") : TEXT("swap"),
		*UEnum::GetValueAsString(FinaleStage), BlendSeconds);
}

void AOneBrahamSkyDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UOneBrahamLockdownComponent* Lockdown = FindLockdown();
	if (Lockdown == nullptr)
	{
		return;   // the game state has not replicated yet
	}

	const EOneBrahamLockdownStage Stage = Lockdown->GetStage();
	const bool bFinale = IsFinaleStage(Stage, FinaleStage);

	Progress = StepProgress(Progress, !bStageObserved, Stage, FinaleStage, DeltaSeconds, BlendSeconds);
	bStageObserved = true;

	const float Alpha = EaseProgress(Progress);
	const bool bSettled = FMath::IsNearlyEqual(Progress, bFinale ? 1.f : 0.f);
	if (!FMath::IsNearlyEqual(Alpha, AppliedAlpha, 1.e-4f))
	{
		ApplyAlpha(Alpha);
		AppliedAlpha = Alpha;
	}

	// Full rate while the light moves, a slow poll while it does not.
	const float WantedInterval = bSettled ? OBSkyIdleTickSeconds : 0.f;
	if (!FMath::IsNearlyEqual(GetActorTickInterval(), WantedInterval))
	{
		SetActorTickInterval(WantedInterval);
	}
}

// ==========================================================================
//  The scene
// ==========================================================================

void AOneBrahamSkyDirector::ResolveScene()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The content build sets these; a hand-placed director finds the map's own.
	if (Sun == nullptr)
	{
		Sun = FirstActorOf<ADirectionalLight>(World);
	}
	if (SkyLight == nullptr)
	{
		SkyLight = FirstActorOf<ASkyLight>(World);
	}
	if (HeightFog == nullptr)
	{
		HeightFog = FirstActorOf<AExponentialHeightFog>(World);
	}

	UStaticMeshComponent* DomeMesh = FindDomeComponent(SkyDome);
	if (DomeMesh == nullptr)
	{
		for (TActorIterator<AActor> It(World); It && DomeMesh == nullptr; ++It)
		{
			DomeMesh = FindDomeComponent(*It);
			if (DomeMesh != nullptr)
			{
				SkyDome = *It;
			}
		}
	}
	if (DomeMesh != nullptr)
	{
		DomeMID = DomeMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	if (Sun != nullptr && Sun->GetLightComponent() != nullptr
		&& Sun->GetLightComponent()->Mobility != EComponentMobility::Movable)
	{
		OB_WARN("Sky director: the sun is not Movable - it will change colour and strength but cannot sink.");
	}
}

void AOneBrahamSkyDirector::CaptureDayState()
{
	if (Sun != nullptr)
	{
		if (const ULightComponent* Light = Sun->GetLightComponent())
		{
			DayState.SunRotation = Light->GetComponentRotation();
			DayState.SunIntensityLux = Light->Intensity;
			DayState.SunColour = FLinearColor(Light->LightColor);
		}
	}
	if (SkyLight != nullptr)
	{
		if (const USkyLightComponent* Light = SkyLight->GetLightComponent())
		{
			DayState.SkyLightIntensity = Light->Intensity;
		}
	}
	if (HeightFog != nullptr)
	{
		if (const UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			DayState.FogDensity = Fog->FogDensity;
			DayState.FogInscatteringLuminance = Fog->FogInscatteringLuminance;
		}
	}
	if (DomeMID != nullptr)
	{
		float Luminance = 0.f;
		if (DomeMID->GetScalarParameterValue(FHashedMaterialParameterInfo(LuminanceParameter), Luminance))
		{
			DayState.DomeLuminance = Luminance;
		}
		FLinearColor Haze;
		if (DomeMID->GetVectorParameterValue(FHashedMaterialParameterInfo(HazeParameter), Haze))
		{
			DayState.DomeHaze = Haze;
		}
		UTexture* Cube = nullptr;
		if (DomeMID->GetTextureParameterValue(FHashedMaterialParameterInfo(SkyCubeParameter), Cube))
		{
			DayCube = Cast<UTextureCube>(Cube);
		}
		// A crossfade needs both halves: the blend scalar and the second cube.
		float UnusedBlend = 0.f;
		UTexture* UnusedCube = nullptr;
		bDomeCrossfades = DomeMID->GetScalarParameterValue(FHashedMaterialParameterInfo(DuskBlendParameter), UnusedBlend)
			&& DomeMID->GetTextureParameterValue(FHashedMaterialParameterInfo(DuskCubeParameter), UnusedCube);
	}
}

UOneBrahamLockdownComponent* AOneBrahamSkyDirector::FindLockdown()
{
	if (CachedLockdown.IsValid())
	{
		return CachedLockdown.Get();
	}
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS != nullptr && GS->GetLockdownComponent() != nullptr)
	{
		CachedLockdown = GS->GetLockdownComponent();
	}
	return CachedLockdown.Get();
}

void AOneBrahamSkyDirector::ApplyAlpha(float Alpha)
{
	const FOneBrahamSkyState State = BlendSkyStates(DayState, DuskState, Alpha);

	if (Sun != nullptr)
	{
		if (ULightComponent* Light = Sun->GetLightComponent())
		{
			if (Light->Mobility == EComponentMobility::Movable)
			{
				Light->SetWorldRotation(State.SunRotation);
			}
			Light->SetIntensity(State.SunIntensityLux);
			Light->SetLightColor(State.SunColour, /*bSRGB*/ true);
		}
	}

	if (SkyLight != nullptr)
	{
		if (USkyLightComponent* Light = SkyLight->GetLightComponent())
		{
			Light->SetIntensity(State.SkyLightIntensity);
			// A real-time capture follows the dome by itself. A captured one is
			// refreshed every couple of seconds and once more when the sky settles.
			if (!Light->IsRealTimeCaptureEnabled())
			{
				RecaptureAccumulator += GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.f;
				const bool bEnd = Alpha <= 0.f || Alpha >= 1.f;
				if (bEnd || RecaptureAccumulator >= OBSkyRecaptureSeconds)
				{
					RecaptureAccumulator = 0.f;
					Light->RecaptureSky();
				}
			}
		}
	}

	if (HeightFog != nullptr)
	{
		if (UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			Fog->SetFogDensity(State.FogDensity);
			Fog->SetFogInscatteringColor(State.FogInscatteringLuminance);
		}
	}

	if (DomeMID != nullptr)
	{
		DomeMID->SetScalarParameterValue(LuminanceParameter, State.DomeLuminance);
		DomeMID->SetVectorParameterValue(HazeParameter, State.DomeHaze);

		if (bDomeCrossfades)
		{
			DomeMID->SetScalarParameterValue(DuskBlendParameter, Alpha);
		}
		else if (DuskCube != nullptr && DayCube != nullptr)
		{
			// Both photographs known, or the swap could never be undone.
			const bool bWantDusk = Alpha >= OBSkyCubeSwapAlpha;
			if (bWantDusk != bDomeShowsDusk)
			{
				UTextureCube* Cube = bWantDusk ? DuskCube.Get() : DayCube.Get();
				if (Cube != nullptr)
				{
					DomeMID->SetTextureParameterValue(SkyCubeParameter, Cube);
					bDomeShowsDusk = bWantDusk;
				}
			}
		}
	}
}
