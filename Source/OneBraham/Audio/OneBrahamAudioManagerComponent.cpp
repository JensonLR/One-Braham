// Copyright (c) One Braham. All Rights Reserved.

#include "Audio/OneBrahamAudioManagerComponent.h"

#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"

#include "Audio/OneBrahamAudioTypes.h"
#include "Audio/OneBrahamProceduralAudio.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "OneBraham.h"

namespace
{
	/** Shipped bank. Missing asset simply means "use the fallback library". */
	const TCHAR* const BankPath = TEXT("/Game/OneBraham/Audio/Banks/DA_AudioBank.DA_AudioBank");

	/** Bus slider names as understood by UOneBrahamGameUserSettings::GetVolume. */
	const TCHAR* const MasterBusName = TEXT("Master");

	/** Bed ids. One live component each, reused for the life of the controller. */
	const FName BedAmbience(TEXT("Ambience"));
	const FName BedMusic(TEXT("Music"));
	const FName BedLockdown(TEXT("Lockdown"));
	const FName BedRotor(TEXT("Rotor"));
	const FName BedTinnitus(TEXT("Tinnitus"));
	const FName BedDowned(TEXT("Downed"));

	/** Low-pass values, Hz. The whole "world goes muffled" language of the game. */
	constexpr float OBLowPassOpen = 20000.f;
	constexpr float OBLowPassDowned = 900.f;
	constexpr float OBLowPassFlashed = 400.f;
	constexpr float OBLowPassSpectating = 1200.f;

	/** Voice duck: how far the beds drop while somebody is talking. */
	constexpr float OBVoiceDuckVolume = 0.45f;

	/** Speed of sound at 20 C, in cm/s. Every layer of a distant event waits for it. */
	constexpr float OBSpeedOfSoundCmPerSec = 34300.f;

	/** Below this delay the ear cannot tell; skip the timer. 10 ms is ~3.4 m. */
	constexpr float OBMinAudibleDelay = 0.012f;

	/** Hermite 0..1 between two edges: the crossfade shape for every layer. */
	FORCEINLINE float SmoothRange(float Edge0, float Edge1, float Value)
	{
		const float T = FMath::Clamp((Value - Edge0) / FMath::Max(KINDA_SMALL_NUMBER, Edge1 - Edge0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/**
	 * Runs a play after a delay, or now when the delay is inaudible. Weak on
	 * the manager, so a controller torn down mid-flight drops the pending layer
	 * rather than calling into a dead component.
	 */
	template <typename FuncType>
	void ScheduleAudio(UObject* Owner, UWorld* World, float DelaySeconds, FuncType&& Func)
	{
		if (DelaySeconds < OBMinAudibleDelay || World == nullptr)
		{
			Func();
			return;
		}

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateWeakLambda(Owner, Forward<FuncType>(Func)), DelaySeconds, false);
	}

	bool IsExplosionCue(FName SoundId)
	{
		static const FName Frag(TEXT("SFX_Frag_Explosion"));
		static const FName Breach(TEXT("SFX_Breach_Explosion"));
		static const FName Flash(TEXT("SFX_Flashbang_Bang"));
		return SoundId == Frag || SoundId == Breach || SoundId == Flash;
	}

	/** The blast radius a routed explosion assumes when the caller did not say. */
	float DefaultBlastRadiusCm(FName SoundId)
	{
		const FString Id = SoundId.ToString();
		if (Id.Contains(TEXT("Breach")))
		{
			return 650.f;
		}
		return Id.Contains(TEXT("Flashbang")) ? 450.f : 500.f;
	}

	/**
	 * The impact family for a surface. A round into plasterboard, a desk, a
	 * server rack and a puddle are four different sounds, because the
	 * difference tells you what is on the other side.
	 */
	const TCHAR* GetImpactSuffix(EOneBrahamFootstepSurface Surface)
	{
		switch (Surface)
		{
		case EOneBrahamFootstepSurface::Metal:
		case EOneBrahamFootstepSurface::Grating:
		case EOneBrahamFootstepSurface::Decking:		return TEXT("Metal");
		case EOneBrahamFootstepSurface::ServerRack:		return TEXT("ServerRack");
		case EOneBrahamFootstepSurface::Wood:			return TEXT("Wood");
		case EOneBrahamFootstepSurface::Plasterboard:	return TEXT("Plaster");
		case EOneBrahamFootstepSurface::Glass:
		case EOneBrahamFootstepSurface::BallisticGlass:	return TEXT("Glass");
		case EOneBrahamFootstepSurface::Carpet:
		case EOneBrahamFootstepSurface::Fabric:			return TEXT("Carpet");
		case EOneBrahamFootstepSurface::Flesh:			return TEXT("Flesh");
		case EOneBrahamFootstepSurface::Puddle:			return TEXT("Water");
		default:										return TEXT("Concrete");
		}
	}

	/** Footsteps: distance travelled per step, cm. Slower = shorter stride. */
	float StrideForSpeed(float Speed2D, bool bCrouched, bool bSprinting)
	{
		if (Speed2D < 40.f)
		{
			return 0.f;
		}
		if (bCrouched)
		{
			return 118.f;
		}
		if (bSprinting)
		{
			return 176.f;
		}
		// Jogging: shorter stride than a sprint, longer than a walk.
		return Speed2D > 250.f ? 158.f : 140.f;
	}
}

UOneBrahamAudioManagerComponent::UOneBrahamAudioManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
	SetIsReplicatedByDefault(false);
}

void UOneBrahamAudioManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureBankLoaded();

	if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Settings->OnSettingsChanged.AddDynamic(this, &UOneBrahamAudioManagerComponent::HandleUserSettingsChanged);
		ApplyUserSettings();
	}

	// Warm the attenuation chains so the first gunshot of a match never pops in.
	GetAttenuation(EOneBrahamAudioCategory::Foley, 2600.f);
	GetAttenuation(EOneBrahamAudioCategory::Mechanical, 2600.f);
	GetAttenuation(EOneBrahamAudioCategory::Weapon, 9000.f);
	GetAttenuation(EOneBrahamAudioCategory::Impact, 6000.f);
	GetAttenuation(EOneBrahamAudioCategory::Objective, 12000.f);

	// The music director starts wherever the match phase says it should - on
	// the machine that is listening. A listen server also holds a controller
	// (and so a manager) per remote player; those must stay silent, or the host
	// hears one music bed per connected client.
	if (IsLocalListener())
	{
		SetMusicState(EOneBrahamMusicState::Lobby);
	}
}

void UOneBrahamAudioManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Settings->OnSettingsChanged.RemoveDynamic(this, &UOneBrahamAudioManagerComponent::HandleUserSettingsChanged);
	}

	for (TPair<FName, TObjectPtr<UAudioComponent>>& Pair : Beds)
	{
		if (Pair.Value != nullptr)
		{
			Pair.Value->Stop();
		}
	}
	Beds.Reset();
	ActiveCues.Reset();
	FootstepDistance.Reset();

	if (HeartbeatComponent != nullptr)
	{
		HeartbeatComponent->Stop();
		HeartbeatComponent = nullptr;
	}
	if (HeartbeatControl.IsValid())
	{
		HeartbeatControl->Intensity.store(0.f);
	}

	Super::EndPlay(EndPlayReason);
}

void UOneBrahamAudioManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Directors, beds and the heartbeat are the listener's. A remote player's
	// controller on a listen server has nothing to say to the host's speakers.
	if (!IsLocalListener())
	{
		return;
	}

	// One combat cue rendered per frame until they are all cached, so the first
	// firefight never waits on the synthesiser.
	if (bPrewarmingCues)
	{
		bPrewarmingCues = OneBrahamProceduralAudio::PrewarmNextCombatCue();
	}

	UpdateLowPass(DeltaTime);
	UpdateHeartbeat(DeltaTime);

	// Music director runs at 2 Hz: it reacts to phases and survivor counts, not frames.
	MusicDirectorAccumulator += DeltaTime;
	if (MusicDirectorAccumulator >= 0.5f)
	{
		MusicDirectorAccumulator = 0.f;
		UpdateMusicDirector(DeltaTime);
		UpdateAmbienceZone();
		PruneCueTables();
	}

	// Tinnitus decay is authoritative on the local machine.
	if (TinnitusRemaining > 0.f)
	{
		TinnitusRemaining = FMath::Max(0.f, TinnitusRemaining - DeltaTime);
		if (TinnitusRemaining <= 0.f)
		{
			StopBed(BedTinnitus, 0.6f);
			RecomputeLowPassTarget();
		}
	}

	if (BlastDuckRemaining > 0.f)
	{
		BlastDuckRemaining = FMath::Max(0.f, BlastDuckRemaining - DeltaTime);
		if (BlastDuckRemaining <= 0.f)
		{
			RecomputeLowPassTarget();
		}
	}

	// Rotor bed: keep it glued to the pad and pitch it with the approach.
	if (bExtractionAudio && Beds.Contains(BedRotor))
	{
		if (UAudioComponent* Rotor = Beds.FindRef(BedRotor))
		{
			if (Rotor->IsPlaying() && !ExtractionPadLocation.IsNearlyZero())
			{
				Rotor->SetWorldLocation(ExtractionPadLocation);
				Rotor->SetPitchMultiplier(FMath::Lerp(0.86f, 1.12f, FMath::Clamp(ExtractionApproach01, 0.f, 1.f)));
			}
		}
	}

	// Voice duck is smoothed rather than stepped: a hard 0.45 jump clicks.
	const float DesiredDuck = VoiceDuckCount > 0 ? OBVoiceDuckVolume : 1.f;
	VoiceDuckCurrent = FMath::FInterpTo(VoiceDuckCurrent, DesiredDuck, DeltaTime, 8.f);
	if (UAudioComponent* Music = Beds.FindRef(BedMusic))
	{
		const float MusicBus = GetBusVolume(EOneBrahamAudioBus::Music);
		Music->SetVolumeMultiplier(MusicBus * VoiceDuckCurrent);
	}
	if (UAudioComponent* Ambience = Beds.FindRef(BedAmbience))
	{
		Ambience->SetVolumeMultiplier(GetBusVolume(EOneBrahamAudioBus::Ambience) * VoiceDuckCurrent);
	}

	// QA readout.
	DebugAccumulator += DeltaTime;
	if (DebugAccumulator >= 1.f)
	{
		PlaysThisSecond = 0;
		DebugAccumulator = 0.f;
	}
}

// ===========================================================================
//  Bank + attenuation
// ===========================================================================

void UOneBrahamAudioManagerComponent::EnsureBankLoaded()
{
	if (Bank != nullptr)
	{
		return;
	}

	Bank = LoadObject<UOneBrahamAudioBankAsset>(nullptr, BankPath);

	if (Bank == nullptr)
	{
		if (!bAppliedUserSettingsOnce)
		{
			UE_LOG(LogOneBrahamAudio, Log,
				TEXT("[OB.AUDIO] No audio bank at %s - running on the synthesised fallback library. "
					 "Create it with Tools/Python/onebraham/create_audio_assets.py."), BankPath);
		}
		return;
	}

	// Any row a designer has not filled in yet gets its procedural stand-in.
	Bank->EnsureCoreEntries();
}

USoundBase* UOneBrahamAudioManagerComponent::ResolveSound(FName SoundId, const FOneBrahamSoundBankEntry** OutEntry)
{
	if (OutEntry != nullptr)
	{
		*OutEntry = nullptr;
	}

	if (SoundId.IsNone())
	{
		return nullptr;
	}

	EnsureBankLoaded();

	const FOneBrahamSoundBankEntry* Entry = Bank != nullptr ? Bank->FindEntry(SoundId) : nullptr;
	if (OutEntry != nullptr)
	{
		*OutEntry = Entry;
	}

	if (Entry != nullptr)
	{
		if (USoundBase* Authored = Entry->Sound.Get())
		{
			return Authored;
		}

		// Not loaded: start a streaming load and use the fallback for this play.
		if (!Entry->Sound.IsNull())
		{
			// Non-blocking: this play falls back to the synthesiser, the next one
			// after the stream completes uses the authored asset.
			Entry->Sound.LoadAsync(FLoadSoftObjectPathAsyncDelegate());
		}
	}

	if (USoundBase* Synthesised = OneBrahamProceduralAudio::GetSynthesisedSound(SoundId))
	{
		return Synthesised;
	}

	if (!WarnedMissingIds.Contains(SoundId))
	{
		WarnedMissingIds.Add(SoundId);
		UE_LOG(LogOneBrahamAudio, Warning,
			TEXT("[OB.AUDIO] Cue '%s' has no authored asset and no synthesised fallback - it will be silent. "
				 "Add it to DA_AudioBank or to OneBrahamProceduralAudio.cpp."), *SoundId.ToString());
	}

	return nullptr;
}

FName UOneBrahamAudioManagerComponent::GetAuthoredAttenuationName(EOneBrahamAudioCategory Category)
{
	// The names are the ones Tools/Python/onebraham/build_audio.py authors, and
	// they are used verbatim by Docs/12_Audio.md. Keep all three in step: a
	// mismatch here does not break the game, it quietly loses the authored mix.
	switch (Category)
	{
	case EOneBrahamAudioCategory::Interface:	return FName(TEXT("SA_OneBraham_Interface"));
	case EOneBrahamAudioCategory::Foley:		return FName(TEXT("SA_OneBraham_Foley"));
	case EOneBrahamAudioCategory::Mechanical:	return FName(TEXT("SA_OneBraham_Mechanical"));
	case EOneBrahamAudioCategory::Weapon:		return FName(TEXT("SA_OneBraham_WeaponClose"));
	case EOneBrahamAudioCategory::Impact:		return FName(TEXT("SA_OneBraham_Impact"));
	case EOneBrahamAudioCategory::Objective:	return FName(TEXT("SA_OneBraham_Objective"));
	case EOneBrahamAudioCategory::Loop:			return FName(TEXT("SA_OneBraham_Loop"));
	default:									return NAME_None;
	}
}

USoundAttenuation* UOneBrahamAudioManagerComponent::GetAuthoredAttenuation(FName AssetName)
{
	if (AssetName.IsNone())
	{
		return nullptr;
	}

	if (TObjectPtr<USoundAttenuation>* Cached = AuthoredAttenuations.Find(AssetName))
	{
		return *Cached;
	}

	const FString Path = FString::Printf(
		TEXT("/Game/OneBraham/Audio/Attenuation/%s.%s"), *AssetName.ToString(), *AssetName.ToString());

	USoundAttenuation* Loaded = LoadObject<USoundAttenuation>(nullptr, *Path);
	AuthoredAttenuations.Add(AssetName, Loaded);

	if (Loaded == nullptr)
	{
		UE_LOG(LogOneBrahamAudio, Verbose,
			TEXT("Authored attenuation %s not found; the runtime-built equivalent will be used. ")
			TEXT("Run Scripts/BuildContent.bat --only audio."), *AssetName.ToString());
	}

	return Loaded;
}

USoundAttenuation* UOneBrahamAudioManagerComponent::GetAttenuation(EOneBrahamAudioCategory Category, float MaxDistanceCm)
{
	if (TObjectPtr<USoundAttenuation>* Existing = Attenuations.Find(Category))
	{
		if (*Existing != nullptr)
		{
			return *Existing;
		}
	}

	// Authored first: a designer's attenuation tweak in the content browser must
	// win over anything this function computes.
	if (USoundAttenuation* Authored = GetAuthoredAttenuation(GetAuthoredAttenuationName(Category)))
	{
		Attenuations.Add(Category, Authored);
		return Authored;
	}

	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);

	FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;

	// Inner radius where the sound is still at full level. Everything inside is
	// "in the room with you"; the falloff is what creates the corridor.
	const float InnerRadius = FMath::Clamp(MaxDistanceCm * 0.18f, 150.f, 2500.f);
	Settings.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
	Settings.FalloffDistance = FMath::Max(InnerRadius + 100.f, MaxDistanceCm);
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;

	if (Category == EOneBrahamAudioCategory::Interface)
	{
		// 2D by construction; still worth a sane object in case one leaks to 3D.
		Settings.FalloffDistance = 250.f;
		Settings.AttenuationShapeExtents = FVector(250.f, 0.f, 0.f);
		Settings.bSpatialize = false;
	}

	// Listener focus: what is in front of you is louder and carries further; what
	// is behind your head is duller. That single trick is most of the "you can be
	// snuck up on" feeling in a building full of hard surfaces.
	Settings.bEnableListenerFocus = true;
	Settings.FocusAzimuth = 60.f;
	Settings.NonFocusAzimuth = 140.f;
	Settings.FocusDistanceScale = 1.5f;
	Settings.NonFocusDistanceScale = 0.55f;
	Settings.FocusPriorityScale = 1.0f;
	Settings.NonFocusPriorityScale = 0.6f;
	Settings.NonFocusVolumeAttenuation = 0.55f;
	Settings.FocusAttackInterpSpeed = 4.f;
	Settings.FocusReleaseInterpSpeed = 2.f;

	// Occlusion: walls are walls. Ballistic glass and plasterboard matter here -
	// shooting through a partition should sound like it.
	Settings.bEnableOcclusion = Category != EOneBrahamAudioCategory::Interface;
	Settings.OcclusionTraceChannel = ECC_Visibility;
	Settings.OcclusionLowPassFilterFrequency = 900.f;
	Settings.OcclusionVolumeAttenuation = 0.45f;
	Settings.OcclusionInterpolationTime = 0.12f;
	Settings.bUseComplexCollisionForOcclusion = false;

	Attenuations.Add(Category, Attenuation);
	return Attenuation;
}

// ===========================================================================
//  Budgets
// ===========================================================================

bool UOneBrahamAudioManagerComponent::CanPlayCue(FName SoundId, const FOneBrahamSoundBankEntry* Entry)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();
	const float Cooldown = Entry != nullptr ? Entry->CooldownSeconds : 0.f;

	if (Cooldown > 0.f)
	{
		if (const float* Last = LastPlayTimes.Find(SoundId))
		{
			if (Now - *Last < Cooldown)
			{
				return false;
			}
		}
	}

	const int32 Budget = Entry != nullptr ? Entry->MaxConcurrent : 8;
	if (const TArray<TWeakObjectPtr<UAudioComponent>>* Live = ActiveCues.Find(SoundId))
	{
		int32 Alive = 0;
		for (const TWeakObjectPtr<UAudioComponent>& Weak : *Live)
		{
			if (Weak.IsValid() && Weak->IsPlaying())
			{
				++Alive;
			}
		}
		if (Alive >= Budget)
		{
			return false;
		}
	}

	LastPlayTimes.Add(SoundId, Now);
	return true;
}

void UOneBrahamAudioManagerComponent::RegisterCuePlayed(FName SoundId, UAudioComponent* Component)
{
	if (Component == nullptr)
	{
		return;
	}

	TArray<TWeakObjectPtr<UAudioComponent>>& Live = ActiveCues.FindOrAdd(SoundId);

	// Cheap compaction: this array is never more than a few dozen entries.
	for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
	{
		if (!Live[Index].IsValid())
		{
			Live.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	Live.Add(Component);
	++PlaysThisSecond;
}

void UOneBrahamAudioManagerComponent::PruneCueTables()
{
	for (auto It = ActiveCues.CreateIterator(); It; ++It)
	{
		TArray<TWeakObjectPtr<UAudioComponent>>& Live = It.Value();
		for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
		{
			if (!Live[Index].IsValid())
			{
				Live.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			}
		}
		if (Live.Num() == 0)
		{
			It.RemoveCurrent();
		}
	}

	// Footstep accumulators for characters that have gone away.
	for (auto It = FootstepDistance.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

// ===========================================================================
//  Bus volumes
// ===========================================================================

float UOneBrahamAudioManagerComponent::GetBusVolume(EOneBrahamAudioBus Bus) const
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const float Master = Settings != nullptr ? Settings->GetVolume(FName(MasterBusName)) : 1.f;

	// A bus with no dedicated slider inherits the closest thing the settings UI
	// actually offers, then the master fader is applied on top of everything.
	float BusValue = 1.f;
	switch (Bus)
	{
	case EOneBrahamAudioBus::SFX:		BusValue = Settings ? Settings->GetVolume(TEXT("SFX")) : 1.f; break;
	case EOneBrahamAudioBus::Gameplay:	BusValue = Settings ? Settings->GetVolume(TEXT("SFX")) : 1.f; break;
	case EOneBrahamAudioBus::Voice:		BusValue = Settings ? Settings->GetVolume(TEXT("Voice")) : 1.f; break;
	case EOneBrahamAudioBus::Ambience:	BusValue = Settings ? Settings->GetVolume(TEXT("SFX")) : 1.f; break;
	case EOneBrahamAudioBus::Music:		BusValue = Settings ? Settings->GetVolume(TEXT("Music")) : 1.f; break;
	case EOneBrahamAudioBus::UI:		BusValue = Settings ? Settings->GetVolume(TEXT("UI")) : 1.f; break;
	default:							BusValue = 1.f; break;
	}

	const float Effective = FMath::Clamp(BusValue * Master, 0.f, 1.f);

	if (Bus == EOneBrahamAudioBus::Voice)
	{
		if (bVoiceBusMuted)
		{
			return 0.f;
		}
		return FMath::Clamp(Effective * VoiceBusGain, 0.f, 2.f);
	}

	return Effective;
}

void UOneBrahamAudioManagerComponent::ApplyUserSettings()
{
	bAppliedUserSettingsOnce = true;

	// The settings object owns the sound class volumes; we only need our own beds
	// to follow the same faders, which happens every tick through GetBusVolume.
	if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Settings->ApplyAudioSettings();
	}

	if (UAudioComponent* Music = Beds.FindRef(BedMusic))
	{
		Music->SetVolumeMultiplier(GetBusVolume(EOneBrahamAudioBus::Music) * VoiceDuckCurrent);
	}
	if (UAudioComponent* Ambience = Beds.FindRef(BedAmbience))
	{
		Ambience->SetVolumeMultiplier(GetBusVolume(EOneBrahamAudioBus::Ambience) * VoiceDuckCurrent);
	}
}

void UOneBrahamAudioManagerComponent::HandleUserSettingsChanged()
{
	ApplyUserSettings();
}

// ===========================================================================
//  Core playback
// ===========================================================================

UAudioComponent* UOneBrahamAudioManagerComponent::PlaySound2D(FName SoundId, float Volume, float Pitch)
{
	const FOneBrahamSoundBankEntry* Entry = nullptr;
	USoundBase* Sound = ResolveSound(SoundId, &Entry);
	if (Sound == nullptr)
	{
		return nullptr;
	}

	if (!CanPlayCue(SoundId, Entry))
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	const float Bus = GetBusVolume(Entry != nullptr ? Entry->Bus : EOneBrahamAudioBus::SFX);
	const float FinalVolume = Volume * (Entry != nullptr ? Entry->Volume : 1.f) * Bus;
	if (FinalVolume <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, Sound, FinalVolume, Pitch, 0.f, nullptr, false, true);
	if (Component != nullptr)
	{
		// The ringing is inside the listener's head, so it is the one 2D cue the
		// world's muffle never touches.
		static const FName FlashRing(TEXT("SFX_Flashbang_Ring"));
		static const FName DownedRing(TEXT("SFX_Downed_Tinnitus"));
		const bool bInHead = SoundId == FlashRing || SoundId == DownedRing;

		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(bInHead ? OBLowPassOpen : CurrentLowPassHz);
		RegisterCuePlayed(SoundId, Component);
	}

	return Component;
}

UAudioComponent* UOneBrahamAudioManagerComponent::PlaySoundAtLocation(FName SoundId, const FVector& Location, float Volume, float Pitch, AActor* SourceActor, USoundAttenuation* AttenuationOverride)
{
	// An explosion is never one sound. Breach charges and the access-zone
	// barrier ask for their blast by id through this function, so this is where
	// every explosion in the game picks up its debris, its distant rumble, its
	// room and its effect on the listener's ears.
	if (!bInLayeredPlay && IsExplosionCue(SoundId))
	{
		return PlayExplosionLayers(Location, DefaultBlastRadiusCm(SoundId), SoundId, Volume);
	}

	return PlayCueAtLocation(SoundId, Location, Volume, Pitch, SourceActor, AttenuationOverride);
}

UAudioComponent* UOneBrahamAudioManagerComponent::PlayCueAtLocation(FName SoundId, const FVector& Location, float Volume, float Pitch, AActor* SourceActor, USoundAttenuation* AttenuationOverride)
{
	const FOneBrahamSoundBankEntry* Entry = nullptr;
	USoundBase* Sound = ResolveSound(SoundId, &Entry);
	if (Sound == nullptr)
	{
		return nullptr;
	}

	if (!CanPlayCue(SoundId, Entry))
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	const EOneBrahamAudioCategory Category = Entry != nullptr ? Entry->Category : EOneBrahamAudioCategory::Foley;
	const float MaxDistance = Entry != nullptr ? Entry->MaxDistanceCm : 4500.f;

	// Interface cues never attenuate even when someone asks for a location.
	if (Category == EOneBrahamAudioCategory::Interface)
	{
		return PlaySound2D(SoundId, Volume, Pitch);
	}

	const float Bus = GetBusVolume(Entry != nullptr ? Entry->Bus : EOneBrahamAudioBus::SFX);
	const float FinalVolume = Volume * (Entry != nullptr ? Entry->Volume : 1.f) * Bus;
	if (FinalVolume <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(
		World, Sound, Location, FRotator::ZeroRotator, FinalVolume, Pitch, 0.f,
		AttenuationOverride != nullptr ? AttenuationOverride : GetAttenuation(Category, MaxDistance),
		nullptr, /*bAutoDestroy*/ true);

	if (Component != nullptr)
	{
		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(CurrentLowPassHz);
		RegisterCuePlayed(SoundId, Component);
	}

	return Component;
}

UAudioComponent* UOneBrahamAudioManagerComponent::PlaySoundAttached(FName SoundId, USceneComponent* AttachTo, FName SocketName, float Volume, float Pitch)
{
	if (AttachTo == nullptr)
	{
		return PlaySound2D(SoundId, Volume, Pitch);
	}

	UAudioComponent* Component = PlaySoundAtLocation(SoundId, AttachTo->GetComponentLocation(), Volume, Pitch, AttachTo->GetOwner());
	if (Component != nullptr)
	{
		Component->AttachToComponent(AttachTo, FAttachmentTransformRules::SnapToTargetIncludingScale, SocketName);
	}

	return Component;
}

// ===========================================================================
//  Weapon cues
// ===========================================================================

bool UOneBrahamAudioManagerComponent::IsMuzzleAboveOrBelowListener(const FVector& Location) const
{
	const AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetOwner());
	const APawn* Pawn = PC != nullptr ? PC->GetPawn() : nullptr;

	if (Pawn == nullptr)
	{
		return false;
	}

	// A dedicated server has no listener; nothing to compare against, and nothing
	// to play either.
	return FMath::Abs(Location.Z - Pawn->GetActorLocation().Z) > OneBrahamConstants::HalfFloorHeightCm;
}

void UOneBrahamAudioManagerComponent::PlayWeaponShot(const UOneBrahamWeaponDefinition* Definition, const FVector& MuzzleLocation, bool bSuppressed, AActor* Shooter)
{
	FName SoundId = FName(TEXT("SFX_Gunshot_Rifle"));

	// Heavy = a cartridge whose distant report is a boom rather than a pop.
	bool bHeavy = true;
	float ShellPitch = 1.f;

	if (Definition != nullptr)
	{
		switch (Definition->Category)
		{
		case EOneBrahamWeaponCategory::Pistol:
		case EOneBrahamWeaponCategory::CompactPistol:
		case EOneBrahamWeaponCategory::SuppressedPistol:
			SoundId = FName(TEXT("SFX_Gunshot_Pistol"));
			bHeavy = false;
			ShellPitch = 1.15f;
			break;
		case EOneBrahamWeaponCategory::SMG:
		case EOneBrahamWeaponCategory::PDW:
			SoundId = FName(TEXT("SFX_Gunshot_SMG"));
			bHeavy = false;
			ShellPitch = 1.12f;
			break;
		case EOneBrahamWeaponCategory::PumpShotgun:
		case EOneBrahamWeaponCategory::SemiAutoShotgun:
			// A plastic hull: lower and duller than brass.
			SoundId = FName(TEXT("SFX_Gunshot_Shotgun"));
			ShellPitch = 0.72f;
			break;
		case EOneBrahamWeaponCategory::MarksmanRifle:
			SoundId = FName(TEXT("SFX_Gunshot_Marksman"));
			ShellPitch = 0.9f;
			break;
		case EOneBrahamWeaponCategory::Carbine556:
		case EOneBrahamWeaponCategory::CompactAssaultRifle:
		default:
			SoundId = FName(TEXT("SFX_Gunshot_Rifle"));
			break;
		}
	}

	if (bSuppressed)
	{
		SoundId = FName(TEXT("SFX_Gunshot_Suppressed"));
	}

	// --- where the listener is relative to the shot ------------------------
	FVector ListenerLocation = MuzzleLocation;
	const bool bHasListener = GetListenerLocation(ListenerLocation);

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const bool bOwnShot = Shooter != nullptr && PC != nullptr && Shooter == PC->GetPawn();

	const float DistanceCm = bHasListener && !bOwnShot ? FVector::Dist(ListenerLocation, MuzzleLocation) : 0.f;
	const float DistanceM = DistanceCm / 100.f;

	const bool bOtherFloor = !bOwnShot && IsMuzzleAboveOrBelowListener(MuzzleLocation);
	const int32 FloorsApart = bOtherFloor
		? FMath::Max(1, FMath::RoundToInt(FMath::Abs(MuzzleLocation.Z - ListenerLocation.Z) / OneBrahamConstants::FloorHeightCm))
		: 0;

	// Which attenuation this shot uses is the difference between "somebody is in
	// this room" and "somebody is two floors up and on the west side", and that
	// distinction is the core information of the whole game.
	//
	//   suppressed  -> a tight curve, so it barely leaves the room it was fired in
	//   through floor -> the long interior curve, quieter and longer-tailed
	//   otherwise   -> the close curve, which is what a shot in your room sounds like
	USoundAttenuation* ShotAttenuation = nullptr;

	if (bSuppressed)
	{
		ShotAttenuation = GetAuthoredAttenuation(FName(TEXT("SA_OneBraham_Suppressed")));
	}
	else if (bOtherFloor)
	{
		ShotAttenuation = GetAuthoredAttenuation(FName(TEXT("SA_OneBraham_WeaponInterior")));
	}

	// A slab of concrete per storey: each one takes the top end off. The
	// interior attenuation decides how loud; this decides how dull.
	const float FloorMuffleHz = bOtherFloor ? FMath::Clamp(3200.f / static_cast<float>(FloorsApart), 650.f, 3200.f) : 0.f;

	// Every layer waits for the sound to arrive. At 40 m that is 117 ms, which
	// is the gap between seeing a muzzle flash and hearing it - and the gap that
	// lets a player judge range by ear when a supersonic crack arrives first.
	const float ArrivalDelay = DistanceCm / OBSpeedOfSoundCmPerSec;

	// Crossfades by range. The close report carries the crack and the blast; it
	// gives way to the distant report, which is all a shot is from across a
	// floor plate or the plaza.
	const float CloseGain = 1.f - 0.8f * SmoothRange(15.f, 60.f, DistanceM);
	float DistantGain = bSuppressed ? 0.f : SmoothRange(10.f, 40.f, DistanceM);
	if (bOtherFloor && !bSuppressed)
	{
		// Through a slab, the boom is what carries.
		DistantGain = FMath::Max(DistantGain, 0.6f / static_cast<float>(FloorsApart));
	}

	const float Pitch = FMath::FRandRange(0.97f, 1.03f);
	const TWeakObjectPtr<AActor> WeakShooter(Shooter);
	UWorld* World = GetWorld();

	// 1. The close report.
	ScheduleAudio(this, World, ArrivalDelay, [this, SoundId, MuzzleLocation, CloseGain, Pitch, WeakShooter, ShotAttenuation, FloorMuffleHz]()
	{
		// A gunshot is the one cue that must never be dropped for budget reasons
		// beyond the bank's own limit: the combat loop is built on hearing where
		// shots come from.
		if (UAudioComponent* Close = PlayCueAtLocation(SoundId, MuzzleLocation, CloseGain, Pitch, WeakShooter.Get(), ShotAttenuation))
		{
			if (FloorMuffleHz > 0.f)
			{
				Close->SetLowPassFilterFrequency(FMath::Min(CurrentLowPassHz, FloorMuffleHz));
			}
		}
	});

	// 2. The action. Your own is in your head; anyone else's only within a few
	//    metres. A suppressed shot has it baked into its report already.
	if (!bSuppressed && !bOtherFloor && DistanceM < 12.f)
	{
		if (bOwnShot)
		{
			PlaySound2D(FName(TEXT("SFX_Gunshot_Mech")), 0.5f, FMath::FRandRange(0.96f, 1.04f));
		}
		else
		{
			const float MechGain = 1.f - SmoothRange(4.f, 12.f, DistanceM);
			ScheduleAudio(this, World, ArrivalDelay, [this, MuzzleLocation, MechGain, WeakShooter]()
			{
				PlayCueAtLocation(FName(TEXT("SFX_Gunshot_Mech")), MuzzleLocation, MechGain,
					FMath::FRandRange(0.96f, 1.04f), WeakShooter.Get(), nullptr);
			});
		}
	}

	// 3. The distant report.
	if (DistantGain > 0.02f)
	{
		const FName DistantId = bHeavy ? FName(TEXT("SFX_Gunshot_Distant_Heavy")) : FName(TEXT("SFX_Gunshot_Distant_Light"));
		USoundAttenuation* Distant = GetDistantAttenuation();

		ScheduleAudio(this, World, ArrivalDelay, [this, DistantId, MuzzleLocation, DistantGain, WeakShooter, Distant, FloorMuffleHz]()
		{
			if (UAudioComponent* Far = PlayCueAtLocation(DistantId, MuzzleLocation, DistantGain,
				FMath::FRandRange(0.95f, 1.05f), WeakShooter.Get(), Distant))
			{
				if (FloorMuffleHz > 0.f)
				{
					Far->SetLowPassFilterFrequency(FMath::Min(CurrentLowPassHz, FloorMuffleHz * 0.6f));
				}
			}
		});
	}

	// 4. The room. Only for shots on the listener's own storey: a shot through
	//    the floor already arrives on the interior curve, which is the room above
	//    heard through concrete. The tail is 2D because a diffuse field has no
	//    direction; it fades only gently with range, because the room is the
	//    same room wherever in it you stand.
	if (!bOtherFloor)
	{
		float Wet = 0.f;
		const FName TailId = GetRoomTailCue(Wet);
		const float CalibreSend = bHeavy ? 1.1f : 0.85f;
		const float TailGain = FMath::Clamp(Wet * 1.5f * CalibreSend * (bSuppressed ? 0.25f : 1.f)
			* FMath::Lerp(1.f, 0.4f, SmoothRange(5.f, 60.f, DistanceM)), 0.f, 1.f);

		if (!TailId.IsNone() && TailGain > 0.02f)
		{
			ScheduleAudio(this, World, ArrivalDelay, [this, TailId, TailGain]()
			{
				PlaySound2D(TailId, TailGain, FMath::FRandRange(0.97f, 1.03f));
			});
		}
	}

	// 5. The case hitting the floor, half a second later, for anyone close
	//    enough to hear brass. Pump guns eject on the pump, not the shot.
	const bool bPump = Definition != nullptr && Definition->Category == EOneBrahamWeaponCategory::PumpShotgun;
	if (!bOtherFloor && !bPump && DistanceM < 14.f && World != nullptr)
	{
		const AActor* ShooterActor = Shooter;
		const FVector Ejection = MuzzleLocation
			+ (ShooterActor != nullptr ? ShooterActor->GetActorRightVector() * 55.f - ShooterActor->GetActorForwardVector() * 40.f : FVector::ZeroVector);

		FVector Floor = Ejection;
		const EOneBrahamFootstepSurface Surface = TraceFloorSurface(Ejection, ShooterActor, Floor);
		const float FallDelay = FMath::FRandRange(0.38f, 0.6f);
		const float CasePitch = ShellPitch * FMath::FRandRange(0.95f, 1.05f);

		ScheduleAudio(this, World, ArrivalDelay + FallDelay, [this, Floor, Surface, CasePitch]()
		{
			PlayShellDrop(Floor, Surface, CasePitch);
		});
	}
}

void UOneBrahamAudioManagerComponent::PlayWeaponMechanics(FName SoundId, AActor* Shooter, bool bLocalOnly)
{
	if (Shooter == nullptr || bLocalOnly)
	{
		PlaySound2D(SoundId, 1.f, FMath::FRandRange(0.97f, 1.03f));
		return;
	}

	// Mechanics are attached to the shooter so the mag-in sound arrives with the
	// body, not with a static point in the room.
	if (const APawn* Pawn = Cast<APawn>(Shooter))
	{
		if (USceneComponent* Root = Pawn->GetRootComponent())
		{
			PlaySoundAttached(SoundId, Root, NAME_None, 1.f, FMath::FRandRange(0.97f, 1.03f));
			return;
		}
	}

	PlaySoundAtLocation(SoundId, Shooter->GetActorLocation(), 1.f, FMath::FRandRange(0.97f, 1.03f), Shooter);
}

void UOneBrahamAudioManagerComponent::PlayReloadCue(FName Stage, AActor* Shooter)
{
	static const FName StartStage(TEXT("Start"));
	static const FName MagInStage(TEXT("MagIn"));
	static const FName ChargeStage(TEXT("Charge"));
	static const FName CompleteStage(TEXT("Complete"));
	static const FName AbortStage(TEXT("Abort"));

	FName SoundId = FName(TEXT("SFX_Reload_Mag_In"));

	if (Stage == StartStage)
	{
		SoundId = FName(TEXT("SFX_Reload_Mag_Out"));
	}
	else if (Stage == ChargeStage)
	{
		SoundId = FName(TEXT("SFX_Reload_Bolt"));
	}
	else if (Stage == CompleteStage)
	{
		SoundId = FName(TEXT("SFX_FireMode_Switch"));
	}
	else if (Stage == AbortStage)
	{
		SoundId = FName(TEXT("SFX_Weapon_Draw"));
	}
	else if (Stage == MagInStage)
	{
		SoundId = FName(TEXT("SFX_Reload_Mag_In"));
	}

	PlayWeaponMechanics(SoundId, Shooter, false);
}

// ===========================================================================
//  World foley
// ===========================================================================

const TCHAR* UOneBrahamAudioManagerComponent::GetSurfaceSuffix(EOneBrahamFootstepSurface Surface)
{
	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Marble:			return TEXT("Hard");
	case EOneBrahamFootstepSurface::Carpet:			return TEXT("Carpet");
	case EOneBrahamFootstepSurface::Concrete:		return TEXT("Hard");
	case EOneBrahamFootstepSurface::Metal:			return TEXT("Metal");
	case EOneBrahamFootstepSurface::Glass:			return TEXT("Hard");
	case EOneBrahamFootstepSurface::Wood:			return TEXT("Hard");
	case EOneBrahamFootstepSurface::Puddle:			return TEXT("Water");
	case EOneBrahamFootstepSurface::Grating:		return TEXT("Metal");
	case EOneBrahamFootstepSurface::Fabric:			return TEXT("Carpet");
	case EOneBrahamFootstepSurface::ServerRack:		return TEXT("Metal");
	case EOneBrahamFootstepSurface::Plasterboard:	return TEXT("Carpet");
	case EOneBrahamFootstepSurface::Lino:			return TEXT("Hard");
	case EOneBrahamFootstepSurface::Decking:		return TEXT("Metal");
	case EOneBrahamFootstepSurface::Flesh:			return TEXT("Carpet");
	case EOneBrahamFootstepSurface::BallisticGlass: return TEXT("Hard");
	default:										return TEXT("Hard");
	}
}

FName UOneBrahamAudioManagerComponent::BuildSurfaceSoundId(const FName Prefix, EOneBrahamFootstepSurface Surface)
{
	return FName(*FString::Printf(TEXT("%s_%s"), *Prefix.ToString(), GetSurfaceSuffix(Surface)));
}

EOneBrahamFootstepSurface UOneBrahamAudioManagerComponent::ClassifyGroundSurface(const UPhysicalMaterial* PhysicalMaterial)
{
	if (PhysicalMaterial == nullptr)
	{
		return EOneBrahamFootstepSurface::Concrete;
	}

	// The project's physical material names are the contract here: every surface
	// type in Config/DefaultEngine.ini maps to one of these. Anything unknown is
	// concrete, because a London office is mostly concrete.
	const FString Name = PhysicalMaterial->GetName().ToLower();

	if (Name.Contains(TEXT("marble")))		{ return EOneBrahamFootstepSurface::Marble; }
	if (Name.Contains(TEXT("carpet")))		{ return EOneBrahamFootstepSurface::Carpet; }
	if (Name.Contains(TEXT("metal")))		{ return EOneBrahamFootstepSurface::Metal; }
	if (Name.Contains(TEXT("glass")))		{ return Name.Contains(TEXT("ballistic")) ? EOneBrahamFootstepSurface::BallisticGlass : EOneBrahamFootstepSurface::Glass; }
	if (Name.Contains(TEXT("wood")))		{ return EOneBrahamFootstepSurface::Wood; }
	if (Name.Contains(TEXT("water")) || Name.Contains(TEXT("puddle"))) { return EOneBrahamFootstepSurface::Puddle; }
	if (Name.Contains(TEXT("grating")))		{ return EOneBrahamFootstepSurface::Grating; }
	if (Name.Contains(TEXT("fabric")))		{ return EOneBrahamFootstepSurface::Fabric; }
	if (Name.Contains(TEXT("rack")))		{ return EOneBrahamFootstepSurface::ServerRack; }
	if (Name.Contains(TEXT("plaster")))		{ return EOneBrahamFootstepSurface::Plasterboard; }
	if (Name.Contains(TEXT("lino")) || Name.Contains(TEXT("vinyl"))) { return EOneBrahamFootstepSurface::Lino; }
	if (Name.Contains(TEXT("deck")))		{ return EOneBrahamFootstepSurface::Decking; }
	if (Name.Contains(TEXT("flesh")) || Name.Contains(TEXT("body"))) { return EOneBrahamFootstepSurface::Flesh; }

	return EOneBrahamFootstepSurface::Concrete;
}

void UOneBrahamAudioManagerComponent::TickFootstepAudio(AActor* Stepper, EOneBrahamFootstepSurface Surface, float Speed2D, bool bCrouched, bool bSprinting, bool bInAir, bool bLocalPlayer)
{
	if (Stepper == nullptr || bInAir)
	{
		if (Stepper != nullptr)
		{
			FootstepDistance.Remove(TWeakObjectPtr<const AActor>(Stepper));
		}
		return;
	}

	const float Stride = StrideForSpeed(Speed2D, bCrouched, bSprinting);
	if (Stride <= 0.f)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float DeltaTime = World->GetDeltaSeconds();
	float& Accumulated = FootstepDistance.FindOrAdd(TWeakObjectPtr<const AActor>(Stepper));
	Accumulated += Speed2D * DeltaTime;

	if (Accumulated < Stride)
	{
		return;
	}
	Accumulated = FMath::Fmod(Accumulated, Stride);

	const FName SoundId = BuildSurfaceSoundId(FName(TEXT("SFX_Footstep")), Surface);

	// Your own footsteps sit under the mix (0.55 of the remote level). You should
	// never lose a fight because your own shoes were too loud to hear the other
	// person's.
	const float Volume = bLocalPlayer ? 0.55f : 1.f;
	const float Pitch = FMath::FRandRange(0.94f, 1.06f);

	if (bLocalPlayer && Speed2D > 40.f)
	{
		// Own steps are 2D: no attenuation maths, no occlusion, no listener focus,
		// and crucially no ambiguity about which of the two sounds is your own.
		PlaySound2D(SoundId, Volume * 0.9f, Pitch);
	}
	else
	{
		PlaySoundAtLocation(SoundId, Stepper->GetActorLocation() - FVector(0.f, 0.f, 80.f), Volume, Pitch, Stepper);
	}
}

void UOneBrahamAudioManagerComponent::PlayUtilityBounce(const FVector& Location, EOneBrahamFootstepSurface Surface)
{
	// Impact-style foley with a pitch shift by surface, so you can hear a grenade
	// roll off a desk and onto carpet without looking.
	const float Pitch = Surface == EOneBrahamFootstepSurface::Metal || Surface == EOneBrahamFootstepSurface::Grating
		? FMath::FRandRange(1.04f, 1.14f)
		: FMath::FRandRange(0.88f, 0.98f);

	PlaySoundAtLocation(FName(TEXT("SFX_UtilityBounce")), Location, 1.f, Pitch, nullptr);
}

void UOneBrahamAudioManagerComponent::PlayFuseBeep(const FVector& Location, bool bLocal2D)
{
	// The beep is information: it is the only warning a frag gives. It is never
	// culled, never cooldown-blocked, and the close variant is deliberately sharp.
	if (bLocal2D)
	{
		PlaySound2D(FName(TEXT("SFX_FuseBeep_Close")), 1.f, 1.f);
	}
	else
	{
		const FOneBrahamSoundBankEntry* Entry = nullptr;
		USoundBase* Sound = ResolveSound(FName(TEXT("SFX_FuseBeep")), &Entry);
		UWorld* World = GetWorld();
		if (Sound == nullptr || World == nullptr)
		{
			return;
		}

		const float Bus = GetBusVolume(Entry != nullptr ? Entry->Bus : EOneBrahamAudioBus::Gameplay);
		UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(
			World, Sound, Location, FRotator::ZeroRotator,
			(Entry != nullptr ? Entry->Volume : 0.55f) * Bus, 1.f, 0.f,
			GetAttenuation(EOneBrahamAudioCategory::Objective, 4000.f), nullptr, true);

		if (Component != nullptr)
		{
			Component->SetLowPassFilterEnabled(true);
			Component->SetLowPassFilterFrequency(CurrentLowPassHz);
			RegisterCuePlayed(FName(TEXT("SFX_FuseBeep")), Component);
		}
	}
}

void UOneBrahamAudioManagerComponent::PlayImpact(FName SoundIdPrefix, const FHitResult& Hit, float VolumeScale, AActor* IgnoredActor)
{
	if (Hit.bBlockingHit == false && Hit.GetActor() == nullptr)
	{
		return;
	}

	// Every surface resolves through the physical material of the hit component.
	EOneBrahamFootstepSurface Surface = EOneBrahamFootstepSurface::Concrete;

	if (const UPrimitiveComponent* Component = Hit.GetComponent())
	{
		Surface = ClassifyGroundSurface(Component->GetBodyInstance() != nullptr
			? Component->GetBodyInstance()->GetSimplePhysicalMaterial()
			: nullptr);
	}

	const FName SoundId = FName(*FString::Printf(TEXT("%s_%s"), *SoundIdPrefix.ToString(), GetImpactSuffix(Surface)));
	PlaySoundAtLocation(SoundId, Hit.ImpactPoint, VolumeScale, FMath::FRandRange(0.95f, 1.06f), IgnoredActor);
}

void UOneBrahamAudioManagerComponent::PlayImpactSound(const FOneBrahamImpactInfo& Impact, float VolumeScale)
{
	// One id per material family. A round into a plasterboard partition, a desk
	// and a server rack should be three different sounds, because in this game
	// the difference is information: it tells you what is on the other side.
	const bool bFlesh = Impact.bHitCharacter;
	const FName SoundId = bFlesh
		? FName(TEXT("SFX_Bullet_Impact_Flesh"))
		: FName(*FString::Printf(TEXT("SFX_Bullet_Impact_%s"), GetImpactSuffix(Impact.Surface)));

	// A penetrated round arrives duller and quieter: it already went through
	// something on the way here.
	const float PenetrationScale = Impact.bPenetrated ? 0.7f : 1.f;
	const float Pitch = bFlesh ? FMath::FRandRange(0.96f, 1.05f) : FMath::FRandRange(0.94f, 1.08f);
	const float Volume = VolumeScale * PenetrationScale;

	// Headshots on an enemy helmet ring like a struck bell - the single most
	// satisfying sound in a serious shooter, and it carries information.
	const bool bHelmet = bFlesh && Impact.HitBoneName == FName(TEXT("head"));
	const FVector Location = Impact.Location;

	// An impact across the floor plate is heard after it is seen, like the shot.
	FVector Listener = Location;
	const float Delay = GetListenerLocation(Listener) ? FVector::Dist(Listener, Location) / OBSpeedOfSoundCmPerSec : 0.f;

	ScheduleAudio(this, GetWorld(), Delay, [this, SoundId, Location, Volume, Pitch, bHelmet]()
	{
		PlayCueAtLocation(SoundId, Location, Volume, Pitch, nullptr, nullptr);

		if (bHelmet)
		{
			PlayCueAtLocation(FName(TEXT("SFX_Helmet_Ping")), Location, 1.f, FMath::FRandRange(0.98f, 1.04f), nullptr, nullptr);
		}
	});
}

void UOneBrahamAudioManagerComponent::PlayNearMissWhizz(const FVector& Location, bool bSupersonic)
{
	// Only for rounds that actually pass close to the listener: a whizz from
	// forty metres away is a lie, and lies get noticed.
	const APawn* Listener = nullptr;
	if (const APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		Listener = PC->GetPawn();
	}

	if (Listener == nullptr)
	{
		return;
	}

	const float Distance = FVector::Dist(Listener->GetActorLocation(), Location);
	if (Distance > 320.f)
	{
		return;
	}

	// Closer = brighter and higher, which is the price of the miss.
	const float Closeness = FMath::GetMappedRangeValueClamped(FVector2D(320.f, 60.f), FVector2D(0.f, 1.f), Distance);

	if (bSupersonic)
	{
		// The crack arrives first - the round outruns its own report, so a
		// listener hears snap, then (distance / 343 m/s later) the shot. That
		// gap is how a soldier judges range, and it falls out of the layering.
		PlayCueAtLocation(FName(TEXT("SFX_Bullet_Crack")), Location, FMath::Lerp(0.55f, 1.f, Closeness),
			FMath::FRandRange(0.95f, 1.06f), nullptr, nullptr);
	}

	PlayCueAtLocation(FName(TEXT("SFX_Bullet_Whizz")), Location,
		FMath::Lerp(0.45f, 1.f, Closeness) * (bSupersonic ? 0.55f : 1.f),
		FMath::Lerp(0.92f, 1.14f, Closeness), nullptr, nullptr);
}

EOneBrahamFootstepSurface UOneBrahamAudioManagerComponent::TraceFloorSurface(const FVector& From, const AActor* IgnoredActor, FVector& OutFloor) const
{
	OutFloor = From - FVector(0.f, 0.f, 140.f);

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return EOneBrahamFootstepSurface::Concrete;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBShellDropFloor), false, IgnoredActor);
	Params.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
	{
		OutFloor = Hit.ImpactPoint;
		return ClassifyGroundSurface(Hit.PhysMaterial.Get());
	}

	return EOneBrahamFootstepSurface::Concrete;
}

void UOneBrahamAudioManagerComponent::PlayShellDrop(const FVector& Location, EOneBrahamFootstepSurface Surface, float Pitch)
{
	FName SoundId = FName(TEXT("SFX_Shell_Drop"));
	float Volume = 1.f;

	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Carpet:
	case EOneBrahamFootstepSurface::Fabric:
	case EOneBrahamFootstepSurface::Flesh:
		// Brass on carpet should be almost nothing.
		SoundId = FName(TEXT("SFX_Shell_Drop_Carpet"));
		break;
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::Decking:
	case EOneBrahamFootstepSurface::ServerRack:
		// Brass on a steel deck is a giveaway.
		SoundId = FName(TEXT("SFX_Shell_Drop_Metal"));
		break;
	case EOneBrahamFootstepSurface::Puddle:
		// It lands in water: a plop rather than a ring.
		SoundId = FName(TEXT("SFX_Bullet_Impact_Water"));
		Volume = 0.25f;
		break;
	case EOneBrahamFootstepSurface::Wood:
		Volume = 0.8f;
		break;
	default:
		break;
	}

	PlayCueAtLocation(SoundId, Location, Volume, Pitch, nullptr, nullptr);
}

void UOneBrahamAudioManagerComponent::PlayBodyFall(const FVector& Location, float ImpactSpeedCmPerSec, EOneBrahamFootstepSurface Surface)
{
	// A collapse from standing is ~400 cm/s at the hips; a fall down a stair
	// flight or off a ledge is twice that and should sound it.
	float Volume = FMath::Clamp(ImpactSpeedCmPerSec / 500.f, 0.4f, 1.25f);
	const float Pitch = FMath::FRandRange(0.92f, 1.04f);

	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Carpet:
	case EOneBrahamFootstepSurface::Fabric:
		Volume *= 0.7f;
		break;
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::Decking:
	case EOneBrahamFootstepSurface::ServerRack:
		// Kit on a steel deck rings out under the body.
		PlayCueAtLocation(FName(TEXT("SFX_Footstep_Metal")), Location, 0.8f, 0.85f, nullptr, nullptr);
		break;
	case EOneBrahamFootstepSurface::Puddle:
		PlayCueAtLocation(FName(TEXT("SFX_Bullet_Impact_Water")), Location, 0.9f, 0.7f, nullptr, nullptr);
		break;
	default:
		break;
	}

	PlayCueAtLocation(FName(TEXT("SFX_Body_Fall")), Location, Volume, Pitch, nullptr, nullptr);
}

void UOneBrahamAudioManagerComponent::PlayHitFeedback(bool bHeadshot, bool bArmourAbsorbed, bool bKill, float DistanceCm)
{
	// Hit markers are the most-played sound in the game and they are 2D on
	// purpose: the player needs to know *that* they hit, not where the target is.
	// Distance only affects pitch, which is the entire "how far away was that
	// headshot" language of the HUD.
	const float DistanceScale = FMath::GetMappedRangeValueClamped(
		FVector2D(0.f, 6000.f), FVector2D(0.94f, 1.12f), DistanceCm);

	if (bKill)
	{
		PlaySound2D(FName(TEXT("UI_KillConfirm")), 1.f, DistanceScale);
		return;
	}

	if (bArmourAbsorbed)
	{
		PlaySound2D(FName(TEXT("UI_HitMarker_Armour")), 1.f, DistanceScale);
	}
	else if (bHeadshot)
	{
		PlaySound2D(FName(TEXT("UI_HitMarker_Headshot")), 1.f, DistanceScale);
	}
	else
	{
		PlaySound2D(FName(TEXT("UI_HitMarker")), 1.f, DistanceScale);
	}
}

void UOneBrahamAudioManagerComponent::PlayDamageTakenCue(bool bArmourAbsorbed, bool bHeadshot)
{
	// Being hit is two sounds: the thud inside your head, and what hit you. Both
	// 2D - it happened to the listener - and both low, so it can never be mistaken
	// for your own hit marker, which is high and bright.
	PlaySound2D(FName(TEXT("UI_Damage_Taken")), bHeadshot ? 1.15f : 1.f, bArmourAbsorbed ? 1.08f : FMath::FRandRange(0.95f, 1.02f));

	if (bArmourAbsorbed)
	{
		// A plate taking the round: a dull metallic smack, pitched up so it
		// reads as close and small.
		PlaySound2D(FName(TEXT("SFX_Bullet_Impact_Metal")), 0.35f, FMath::FRandRange(1.15f, 1.25f));
	}
	else if (bHeadshot)
	{
		// A round off your own helmet: the bell, inside your head, and a short
		// ring after it.
		PlaySound2D(FName(TEXT("SFX_Helmet_Ping")), 0.7f, FMath::FRandRange(0.95f, 1.02f));
		SetTinnitusInternal(0.9f, 2400.f, 0.35f);
	}
	else
	{
		PlaySound2D(FName(TEXT("SFX_Bullet_Impact_Flesh")), 0.5f, FMath::FRandRange(0.9f, 1.f));
	}
}

void UOneBrahamAudioManagerComponent::PlayRadioOpenClose(bool bOpening, bool bSquadChannel)
{
	// Squad radio is a higher, tighter click than the building-wide team channel,
	// so a player can tell which net they are on with their eyes on the fight.
	PlaySound2D(bOpening ? FName(TEXT("UI_Voice_Radio_Open")) : FName(TEXT("UI_Voice_Radio_Close")),
		1.f, bSquadChannel ? 1.08f : 0.94f);

	bVoiceActive = bOpening;
}

void UOneBrahamAudioManagerComponent::PlayLocalUISound(FName SoundId)
{
	PlaySound2D(SoundId, 1.f, 1.f);
}

void UOneBrahamAudioManagerComponent::PlayAnnouncementChime()
{
	// Two-tone Tannoy chime, then the announcement itself is the voice line.
	PlaySound2D(FName(TEXT("UI_Banner")), 1.f, 1.f);
}

void UOneBrahamAudioManagerComponent::PlayExplosion(const FVector& Location, float RadiusCm, FName SoundId)
{
	PlayExplosionLayers(Location, RadiusCm, SoundId.IsNone() ? FName(TEXT("SFX_Frag_Explosion")) : SoundId, 1.f);
}

UAudioComponent* UOneBrahamAudioManagerComponent::PlayExplosionLayers(const FVector& Location, float RadiusCm, FName SoundId, float VolumeScale)
{
	TGuardValue<bool> RoutingGuard(bInLayeredPlay, true);

	const float Radius = FMath::Max(100.f, RadiusCm);
	const bool bFlash = SoundId == FName(TEXT("SFX_Flashbang_Bang"));

	FVector ListenerLocation = Location;
	const bool bHasListener = GetListenerLocation(ListenerLocation);
	const float DistanceCm = bHasListener ? FVector::Dist(ListenerLocation, Location) : 0.f;
	const float DistanceM = DistanceCm / 100.f;

	const bool bOtherFloor = IsMuzzleAboveOrBelowListener(Location);
	const int32 FloorsApart = bOtherFloor
		? FMath::Max(1, FMath::RoundToInt(FMath::Abs(Location.Z - ListenerLocation.Z) / OneBrahamConstants::FloorHeightCm))
		: 0;
	const float FloorMuffleHz = bOtherFloor ? FMath::Clamp(2400.f / static_cast<float>(FloorsApart), 450.f, 2400.f) : 0.f;

	const float ArrivalDelay = DistanceCm / OBSpeedOfSoundCmPerSec;
	const float Pitch = FMath::FRandRange(0.96f, 1.04f);
	UWorld* World = GetWorld();

	USoundAttenuation* BodyAttenuation = bOtherFloor ? GetAuthoredAttenuation(FName(TEXT("SA_OneBraham_WeaponInterior"))) : nullptr;
	const float CloseGain = VolumeScale * (1.f - 0.75f * SmoothRange(25.f, 80.f, DistanceM));

	// 1. The blast: crack, body and the long sub tail, in one cue.
	UAudioComponent* Blast = nullptr;
	auto PlayBlast = [this, SoundId, Location, CloseGain, Pitch, BodyAttenuation, FloorMuffleHz]() -> UAudioComponent*
	{
		TGuardValue<bool> Guard(bInLayeredPlay, true);
		UAudioComponent* Component = PlayCueAtLocation(SoundId, Location, CloseGain, Pitch, nullptr, BodyAttenuation);
		if (Component != nullptr && FloorMuffleHz > 0.f)
		{
			Component->SetLowPassFilterFrequency(FMath::Min(CurrentLowPassHz, FloorMuffleHz));
		}
		return Component;
	};

	if (ArrivalDelay < OBMinAudibleDelay)
	{
		Blast = PlayBlast();
	}
	else
	{
		ScheduleAudio(this, World, ArrivalDelay, [PlayBlast]() { PlayBlast(); });
	}

	// 2. Debris: the ceiling coming down a beat after the bang. Close range
	//    only - nobody hears plaster patter across a floor plate.
	const float DebrisGain = VolumeScale * (bFlash ? 0.3f : 0.85f) * (1.f - SmoothRange(20.f, 50.f, DistanceM)) * (bOtherFloor ? 0.3f : 1.f);
	if (DebrisGain > 0.02f)
	{
		ScheduleAudio(this, World, ArrivalDelay + FMath::FRandRange(0.08f, 0.16f), [this, Location, DebrisGain, FloorMuffleHz]()
		{
			if (UAudioComponent* Debris = PlayCueAtLocation(FName(TEXT("SFX_Explosion_Debris")), Location, DebrisGain,
				FMath::FRandRange(0.94f, 1.06f), nullptr, nullptr))
			{
				if (FloorMuffleHz > 0.f)
				{
					Debris->SetLowPassFilterFrequency(FMath::Min(CurrentLowPassHz, FloorMuffleHz));
				}
			}
		});
	}

	// 3. The distant rumble, which is all a grenade is from the far side of
	//    the building, and what comes through the floor slab.
	float DistantGain = SmoothRange(20.f, 60.f, DistanceM);
	if (bOtherFloor)
	{
		DistantGain = FMath::Max(DistantGain, 0.85f / static_cast<float>(FloorsApart));
	}
	DistantGain *= VolumeScale * (bFlash ? 0.6f : 1.f);

	if (DistantGain > 0.02f)
	{
		USoundAttenuation* Distant = GetDistantAttenuation();
		ScheduleAudio(this, World, ArrivalDelay, [this, Location, DistantGain, Distant, FloorMuffleHz]()
		{
			if (UAudioComponent* Far = PlayCueAtLocation(FName(TEXT("SFX_Explosion_Distant")), Location, DistantGain,
				FMath::FRandRange(0.94f, 1.04f), nullptr, Distant))
			{
				if (FloorMuffleHz > 0.f)
				{
					Far->SetLowPassFilterFrequency(FMath::Min(CurrentLowPassHz, FloorMuffleHz));
				}
			}
		});
	}

	// 4. The room. An explosion drives a space far harder than a gunshot.
	if (!bOtherFloor)
	{
		float Wet = 0.f;
		const FName TailId = GetRoomTailCue(Wet);
		const float TailGain = FMath::Clamp(Wet * 2.f * VolumeScale * FMath::Lerp(1.f, 0.5f, SmoothRange(5.f, 60.f, DistanceM)), 0.f, 1.f);
		if (!TailId.IsNone() && TailGain > 0.02f)
		{
			ScheduleAudio(this, World, ArrivalDelay, [this, TailId, TailGain]()
			{
				PlaySound2D(TailId, TailGain, FMath::FRandRange(0.82f, 0.9f));
			});
		}
	}

	// 5. The listener's ears. Inside 1.35x the radius they ring and the world
	//    goes behind a wall, harder the closer they were; out to 3.5x the radius
	//    the mix just ducks for a moment. A wall between you and the blast, or a
	//    floor slab, takes most of that away.
	if (bHasListener && !bFlash)
	{
		float Exposure = 1.f;

		if (bOtherFloor)
		{
			Exposure = 0.35f;
		}
		else if (World != nullptr)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBlastExposure), false);
			if (const APlayerController* PC = Cast<APlayerController>(GetOwner()))
			{
				Params.AddIgnoredActor(PC->GetPawn());
			}

			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, ListenerLocation, Location + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params))
			{
				Exposure = 0.6f;
			}
		}

		const float Strength = (1.f - SmoothRange(Radius * 0.35f, Radius * 1.35f, DistanceCm)) * Exposure;

		if (Strength > 0.05f)
		{
			const float RingSeconds = FMath::Lerp(0.9f, 4.5f, Strength);
			const float LowPassHz = FMath::Lerp(1800.f, 450.f, Strength);
			SetTinnitusInternal(RingSeconds, LowPassHz, FMath::Lerp(0.35f, 1.f, Strength));

			UE_LOG(LogOneBrahamAudio, Verbose, TEXT("[OB.AUDIO] Blast tinnitus: %.2fs at %.0f cm (strength %.2f, LP %.0f Hz)"),
				RingSeconds, DistanceCm, Strength, LowPassHz);
		}
		else if (DistanceCm < Radius * 3.5f)
		{
			const float Closeness = (1.f - SmoothRange(Radius * 1.35f, Radius * 3.5f, DistanceCm)) * Exposure;
			const bool bAlreadyDucked = BlastDuckRemaining > 0.f;
			BlastDuckLowPassHz = FMath::Min(bAlreadyDucked ? BlastDuckLowPassHz : OBLowPassOpen, FMath::Lerp(5000.f, 2200.f, Closeness));
			BlastDuckRemaining = FMath::Max(BlastDuckRemaining, FMath::Lerp(0.3f, 0.8f, Closeness));
			RecomputeLowPassTarget();
		}
	}

	return Blast;
}

// ===========================================================================
//  Beds
// ===========================================================================

UAudioComponent* UOneBrahamAudioManagerComponent::AcquireBed(FName BedId, FName SoundId, bool bSpatialised, const FVector& Location, float Volume, float FadeSeconds)
{
	if (TObjectPtr<UAudioComponent>* Existing = Beds.Find(BedId))
	{
		UAudioComponent* Component = *Existing;
		if (Component != nullptr && Component->IsPlaying())
		{
			// Already running: retarget a spatialised bed, fade back to level if it
			// was ducked or stopping.
			if (bSpatialised && !Location.IsNearlyZero())
			{
				Component->SetWorldLocation(Location);
			}
			Component->FadeIn(FadeSeconds, Volume, 0.f, EAudioFaderCurve::Linear);
			BedStopTimes.Remove(BedId);
			return Component;
		}
	}

	const FOneBrahamSoundBankEntry* Entry = nullptr;
	USoundBase* Sound = ResolveSound(SoundId, &Entry);
	if (Sound == nullptr)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	UAudioComponent* Component = bSpatialised
		? UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, 0.f, 1.f, 0.f,
			GetAttenuation(EOneBrahamAudioCategory::Loop, Entry != nullptr ? Entry->MaxDistanceCm : 12000.f), nullptr, false)
		: UGameplayStatics::SpawnSound2D(World, Sound, 0.f, 1.f, 0.f, nullptr, true, false);

	if (Component == nullptr)
	{
		return nullptr;
	}

	Component->bAutoDestroy = false;
	Component->SetLowPassFilterEnabled(true);
	Component->SetLowPassFilterFrequency(CurrentLowPassHz);
	Component->FadeIn(FMath::Max(0.05f, FadeSeconds), Volume, 0.f, EAudioFaderCurve::Linear);

	Beds.Add(BedId, Component);
	return Component;
}

void UOneBrahamAudioManagerComponent::StopBed(FName BedId, float FadeSeconds)
{
	TObjectPtr<UAudioComponent>* Found = Beds.Find(BedId);
	if (Found == nullptr || *Found == nullptr)
	{
		return;
	}

	UAudioComponent* Component = *Found;
	Component->FadeOut(FMath::Max(0.05f, FadeSeconds), 0.f, EAudioFaderCurve::Linear);

	// The component destroys itself once the fade completes; drop our handle.
	Beds.Remove(BedId);
	BedStopTimes.Add(BedId, GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.f);
}

void UOneBrahamAudioManagerComponent::SetAmbienceZone(FName ZoneId, float FadeSeconds)
{
	if (ZoneId == CurrentAmbienceZone)
	{
		return;
	}

	CurrentAmbienceZone = ZoneId;

	if (ZoneId.IsNone())
	{
		StopBed(BedAmbience, FadeSeconds);
		return;
	}

	// The zone vocabulary lives in the bank, keyed by the building layout's area
	// ids, because it is content: a designer should be able to point a zone at a
	// different bed without a recompile. This function used to carry a second,
	// private table in a fourth vocabulary ("Plaza", "Offices", "ServerRoom")
	// that no other file used, and nothing called it - so the building had no
	// ambience bed at all, in any zone, with content or without.
	EnsureBankLoaded();

	const FName CueId = Bank != nullptr ? Bank->GetAmbienceCueForZone(ZoneId)
		: FName(TEXT("AMB_London_Bed"));

	AcquireBed(BedAmbience, CueId, false, FVector::ZeroVector,
		GetBusVolume(EOneBrahamAudioBus::Ambience), FadeSeconds);
}

void UOneBrahamAudioManagerComponent::UpdateAmbienceZone()
{
	// Where is the listener? The floor index is the character's, the area id is
	// the building layout's - the same one floor-to-area table the HUD, the
	// spawner and the objective lines all read from.
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const AOneBrahamCharacter* Character = PC != nullptr ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;

	// A dedicated server has no listener and an unpossessed controller has no
	// room; the bed is per-client, so there is nothing to play.
	if (Character == nullptr)
	{
		SetAmbienceZone(NAME_None);
		return;
	}

	// The layout has no area for the stairwell: it is the concrete between two
	// floors, so it is what plays whenever a floor has no area of its own. The
	// bank names it, so a designer can rename it without touching this file.
	const FName FallbackZone = Bank != nullptr ? Bank->AmbienceFallbackZone
		: FName(TEXT("Stairwell"));

	const UOneBrahamBuildingLayoutAsset* Layout = UOneBrahamBuildingLayoutAsset::Get(Character);

	if (Layout == nullptr)
	{
		SetAmbienceZone(FallbackZone);
		return;
	}

	const FName AreaId = Layout->GetAreaIdForFloor(Character->GetCurrentFloorIndex());

	SetAmbienceZone(AreaId.IsNone() ? FallbackZone : AreaId);
}

void UOneBrahamAudioManagerComponent::SetMusicState(EOneBrahamMusicState NewState, bool bForce)
{
	if (NewState == RequestedMusicState && !bForce)
	{
		return;
	}

	// Extraction, lockdown and downed outrank the match layer: the player should
	// never be listening to "Match Mid" while a helicopter lands on their head.
	if (!bForce)
	{
		const bool bCurrentIsOverride = CurrentMusicState == EOneBrahamMusicState::Extraction
			|| CurrentMusicState == EOneBrahamMusicState::Lockdown
			|| CurrentMusicState == EOneBrahamMusicState::Downed;

		const bool bNewIsMatchLayer = NewState == EOneBrahamMusicState::MatchEarly
			|| NewState == EOneBrahamMusicState::MatchMid
			|| NewState == EOneBrahamMusicState::MatchLate;

		if (bCurrentIsOverride && bNewIsMatchLayer)
		{
			return;
		}
	}

	RequestedMusicState = NewState;
	CurrentMusicState = NewState;

	// The bank owns the state -> cue table, so the director's mood and the cue
	// the mix was built around cannot drift apart. This switch was a second copy
	// of the same eight names.
	EnsureBankLoaded();

	const FName SoundId = Bank != nullptr ? Bank->GetMusicCueForState(NewState)
		: FName(TEXT("MUS_Lobby"));

	// Cross-fade: stop the old bed with a slightly shorter fade than the new one
	// takes to arrive, so the transition breathes instead of cutting.
	AcquireBed(BedMusic, SoundId, false, FVector::ZeroVector,
		GetBusVolume(EOneBrahamAudioBus::Music), 2.5f);

	UE_LOG(LogOneBrahamAudio, Verbose, TEXT("[OB.AUDIO] Music state -> %s"), *SoundId.ToString());
}

void UOneBrahamAudioManagerComponent::SetLockdownAudio(bool bActive, float FadeSeconds)
{
	if (bActive == bLockdownAudio)
	{
		return;
	}

	bLockdownAudio = bActive;

	if (bActive)
	{
		AcquireBed(BedLockdown, FName(TEXT("SFX_FireAlarm")), false, FVector::ZeroVector,
			GetBusVolume(EOneBrahamAudioBus::Ambience), FadeSeconds);

		// The lockdown bed outranks the match music.
		SetMusicState(EOneBrahamMusicState::Lockdown);
	}
	else
	{
		StopBed(BedLockdown, FadeSeconds);
	}
}

void UOneBrahamAudioManagerComponent::SetExtractionAudio(bool bActive, const FVector& PadLocation, float Approach01)
{
	ExtractionApproach01 = FMath::Clamp(Approach01, 0.f, 1.f);

	if (bActive)
	{
		ExtractionPadLocation = PadLocation;
		AcquireBed(BedRotor, FName(TEXT("SFX_Helicopter_Rotor")), true, PadLocation,
			GetBusVolume(EOneBrahamAudioBus::SFX) * FMath::Lerp(0.55f, 1.f, ExtractionApproach01), 3.f);

		if (!bExtractionAudio)
		{
			PlaySound2D(FName(TEXT("SFX_Extraction_Start")), 1.f, 1.f);
			SetMusicState(EOneBrahamMusicState::Extraction);
		}
	}
	else if (bExtractionAudio)
	{
		StopBed(BedRotor, 4.f);
		PlaySoundAtLocation(FName(TEXT("SFX_Helicopter_Leave")), PadLocation, 1.f, 1.f, nullptr);
	}

	bExtractionAudio = bActive;
}

void UOneBrahamAudioManagerComponent::SetDownedAudioState(bool bDowned)
{
	if (bDowned == bDownedAudio)
	{
		return;
	}

	bDownedAudio = bDowned;

	// Downed: heartbeat, shallow breathing, and the whole building moved behind
	// a wall. It should be obvious you are in trouble without reading the HUD.
	//
	// The heartbeat itself is the live one UpdateHeartbeat drives, held high
	// while downed - unless a recorded SFX_Downed_Heartbeat has been assigned in
	// the bank, in which case that recording is what a designer asked for.
	EnsureBankLoaded();
	const FOneBrahamSoundBankEntry* Entry = FindBankEntry(FName(TEXT("SFX_Downed_Heartbeat")));
	const bool bAuthoredHeartbeat = Entry != nullptr && !Entry->Sound.IsNull();

	if (bDowned)
	{
		if (bAuthoredHeartbeat)
		{
			AcquireBed(BedDowned, FName(TEXT("SFX_Downed_Heartbeat")), false, FVector::ZeroVector,
				GetBusVolume(EOneBrahamAudioBus::Gameplay), 0.6f);
		}
		SetMusicState(EOneBrahamMusicState::Downed);
	}
	else
	{
		StopBed(BedDowned, 1.2f);
	}

	// The downed low-pass is the strongest muffle in the game apart from a
	// flashbang at point blank.
	RecomputeLowPassTarget();
}

void UOneBrahamAudioManagerComponent::SetTinnitusState(bool bActive, float DurationSeconds)
{
	if (!bActive)
	{
		TinnitusRemaining = 0.f;
		StopBed(BedTinnitus, 1.5f);
		RecomputeLowPassTarget();
		return;
	}

	// The flashbang: everything behind a 400 Hz wall, the ring at full level.
	SetTinnitusInternal(DurationSeconds, OBLowPassFlashed, 1.f);
}

void UOneBrahamAudioManagerComponent::SetTinnitusInternal(float DurationSeconds, float LowPassHz, float RingVolume)
{
	const bool bAlreadyRinging = TinnitusRemaining > 0.f;

	TinnitusRemaining = FMath::Max(TinnitusRemaining, DurationSeconds);

	// The strongest muffle wins while two overlap: a flashbang inside a grenade's
	// ring is still a flashbang.
	TinnitusLowPassHz = bAlreadyRinging ? FMath::Min(TinnitusLowPassHz, LowPassHz) : LowPassHz;

	if (UAudioComponent* Ring = AcquireBed(BedTinnitus, FName(TEXT("SFX_Flashbang_Ring")), false, FVector::ZeroVector,
		GetBusVolume(EOneBrahamAudioBus::Gameplay) * FMath::Clamp(RingVolume, 0.f, 1.f), 0.15f))
	{
		// The ring is inside the head: the muffle is applied to the world, not to
		// it. Filtered at the world's 400 Hz, a 4 kHz ring is simply not there.
		Ring->SetLowPassFilterFrequency(OBLowPassOpen);
	}

	// Ringing pushed to the front, everything else pushed to the back.
	RecomputeLowPassTarget();
}

void UOneBrahamAudioManagerComponent::SetDeafened(bool bInDeafened)
{
	bDeafened = bInDeafened;
	RecomputeLowPassTarget();
}

void UOneBrahamAudioManagerComponent::RecomputeLowPassTarget()
{
	// Every muffle the game has is a ceiling on the world's top end; the lowest
	// ceiling wins. Before this, each state wrote the target directly and the
	// last writer won, so a blast while downed could open the filter back up.
	float Target = OBLowPassOpen;

	if (bDeafened)
	{
		Target = FMath::Min(Target, OBLowPassSpectating);
	}
	if (bDownedAudio)
	{
		Target = FMath::Min(Target, OBLowPassDowned);
	}
	if (TinnitusRemaining > 0.f)
	{
		Target = FMath::Min(Target, TinnitusLowPassHz);
	}
	if (BlastDuckRemaining > 0.f)
	{
		Target = FMath::Min(Target, BlastDuckLowPassHz);
	}

	TargetLowPassHz = Target;
}

void UOneBrahamAudioManagerComponent::SetHeartbeatIntensity(float Intensity01)
{
	ExternalHeartbeat = FMath::Clamp(Intensity01, 0.f, 1.f);
}

void UOneBrahamAudioManagerComponent::UpdateHeartbeat(float DeltaTime)
{
	// Health is polled at 10 Hz rather than bound to a delegate: the replicated
	// health on an owning client arrives by OnRep, and the pawn changes on every
	// respawn. A poll survives both without bookkeeping.
	HeartbeatPollAccumulator += DeltaTime;
	if (HeartbeatPollAccumulator >= 0.1f)
	{
		HeartbeatPollAccumulator = 0.f;
		HealthHeartbeat = 0.f;

		const APlayerController* PC = Cast<APlayerController>(GetOwner());
		const AOneBrahamCharacter* Character = PC != nullptr ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
		const UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;

		if (Health != nullptr && Health->IsAlive())
		{
			// From 40% down to 10%: the zone where a player should start thinking
			// about a medkit, and then about nothing else.
			HealthHeartbeat = FMath::Clamp((0.4f - Health->GetHealthFraction()) / 0.3f, 0.f, 1.f);
		}
	}

	const FOneBrahamSoundBankEntry* DownedEntry = bDownedAudio ? FindBankEntry(FName(TEXT("SFX_Downed_Heartbeat"))) : nullptr;
	const bool bDownedUsesLiveHeart = bDownedAudio && (DownedEntry == nullptr || DownedEntry->Sound.IsNull());

	EffectiveHeartbeat = FMath::Max3(ExternalHeartbeat, HealthHeartbeat, bDownedUsesLiveHeart ? 0.85f : 0.f);
	if (bDeafened)
	{
		// Spectating: it is not your body any more.
		EffectiveHeartbeat = 0.f;
	}

	if (!HeartbeatControl.IsValid())
	{
		HeartbeatControl = MakeShared<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe>();
	}
	HeartbeatControl->Intensity.store(EffectiveHeartbeat, std::memory_order_relaxed);

	UWorld* World = GetWorld();

	if (EffectiveHeartbeat > 0.01f)
	{
		HeartbeatSilentSeconds = 0.f;

		if ((HeartbeatComponent == nullptr || !HeartbeatComponent->IsPlaying()) && World != nullptr)
		{
			if (HeartbeatWave == nullptr)
			{
				HeartbeatWave = OneBrahamProceduralAudio::MakeHeartbeatWave(HeartbeatControl);
			}

			HeartbeatComponent = UGameplayStatics::SpawnSound2D(World, HeartbeatWave,
				GetBusVolume(EOneBrahamAudioBus::Gameplay), 1.f, 0.f, nullptr, false, false);
		}
	}
	else if (HeartbeatComponent != nullptr)
	{
		// The generator fades itself out; release the voice once it has.
		HeartbeatSilentSeconds += DeltaTime;
		if (HeartbeatSilentSeconds > 2.f)
		{
			HeartbeatComponent->Stop();
			HeartbeatComponent = nullptr;
		}
	}

	if (HeartbeatComponent != nullptr)
	{
		// Never low-passed: the muffle is the world going away, and the heart is
		// what is left.
		HeartbeatComponent->SetVolumeMultiplier(GetBusVolume(EOneBrahamAudioBus::Gameplay) * 0.9f);
	}
}

void UOneBrahamAudioManagerComponent::PushVoiceDuck()
{
	++VoiceDuckCount;
	VoiceDuckTarget = OBVoiceDuckVolume;
}

void UOneBrahamAudioManagerComponent::PopVoiceDuck()
{
	VoiceDuckCount = FMath::Max(0, VoiceDuckCount - 1);
	VoiceDuckTarget = VoiceDuckCount > 0 ? OBVoiceDuckVolume : 1.f;
}

void UOneBrahamAudioManagerComponent::SetVoiceBusGain(float Gain, bool bMuted)
{
	VoiceBusGain = FMath::Clamp(Gain, 0.f, 4.f);
	bVoiceBusMuted = bMuted;
}

// ===========================================================================
//  Mix / direction
// ===========================================================================

void UOneBrahamAudioManagerComponent::UpdateLowPass(float DeltaTime)
{
	// Muffle is a smoothed move, never a step: a stepped filter is audible as a
	// click and reads as a bug even when it is doing exactly what it should.
	CurrentLowPassHz = FMath::FInterpTo(CurrentLowPassHz, TargetLowPassHz, DeltaTime, 6.f);

	for (const TPair<FName, TObjectPtr<UAudioComponent>>& Pair : Beds)
	{
		// The ring is inside the head and is never muffled with the world.
		if (Pair.Key == BedTinnitus)
		{
			continue;
		}

		if (Pair.Value != nullptr && Pair.Value->IsPlaying())
		{
			Pair.Value->SetLowPassFilterFrequency(CurrentLowPassHz);
		}
	}
}

void UOneBrahamAudioManagerComponent::UpdateMusicDirector(float DeltaTime)
{
	if (bDownedAudio || TinnitusRemaining > 0.25f)
	{
		return;
	}

	const AOneBrahamGameState* GS = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	if (bExtractionAudio)
	{
		SetMusicState(EOneBrahamMusicState::Extraction);
		return;
	}

	if (bLockdownAudio)
	{
		SetMusicState(EOneBrahamMusicState::Lockdown);
		return;
	}

	switch (GS->MatchPhase)
	{
	case EOneBrahamMatchPhase::Warmup:
		SetMusicState(EOneBrahamMusicState::Lobby);
		break;

	case EOneBrahamMatchPhase::Extraction:
		SetMusicState(EOneBrahamMusicState::Extraction);
		break;

	case EOneBrahamMatchPhase::Active:
	{
		// The match layer keys off how many people are still standing: six or more
		// is "early" (sparse, room to hear footsteps), four or fewer is "late"
		// (percussion and tension), and the band between is where the pulse enters.
		const int32 Survivors = GS->SurvivorsRemaining;

		if (Survivors <= 4)
		{
			SetMusicState(EOneBrahamMusicState::MatchLate);
		}
		else if (Survivors >= 6)
		{
			SetMusicState(EOneBrahamMusicState::MatchEarly);
		}
		else
		{
			SetMusicState(EOneBrahamMusicState::MatchMid);
		}
		break;
	}

	case EOneBrahamMatchPhase::Complete:
	case EOneBrahamMatchPhase::PostMatch:
		SetMusicState(EOneBrahamMusicState::Results);
		break;

	default:
		break;
	}
}

// ===========================================================================
//  Queries / debug
// ===========================================================================

UOneBrahamAudioManagerComponent* UOneBrahamAudioManagerComponent::GetLocal(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr && WorldContextObject != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		if (PC != nullptr && PC->IsLocalController())
		{
			return PC->GetAudioManager();
		}
	}

	return nullptr;
}

bool UOneBrahamAudioManagerComponent::IsLocalListener() const
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC != nullptr && PC->IsLocalController();
}

bool UOneBrahamAudioManagerComponent::GetListenerLocation(FVector& OutLocation) const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC == nullptr || GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (const APawn* Pawn = PC->GetPawn())
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}

	// Spectating: the camera is the ears.
	if (PC->PlayerCameraManager != nullptr)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}

	return false;
}

FName UOneBrahamAudioManagerComponent::GetRoomTailCue(float& OutWet) const
{
	// The layout's area ids against REVERB_PROFILES in build_audio.py (the wet
	// column). The tail cue's own decay is the profile's decay.
	struct FRoomProfile
	{
		const TCHAR* Zone;
		const TCHAR* Cue;
		float Wet;
	};

	static const FRoomProfile Profiles[] = {
		{ TEXT("Ground_Plaza"), TEXT("SFX_Gunshot_Tail_Plaza"),       0.25f },
		{ TEXT("Lobby"),        TEXT("SFX_Gunshot_Tail_Lobby"),       0.55f },
		{ TEXT("Mezzanine"),    TEXT("SFX_Gunshot_Tail_Mezzanine"),   0.28f },
		{ TEXT("OpenPlan"),     TEXT("SFX_Gunshot_Tail_OpenPlan"),    0.18f },
		{ TEXT("ServerComms"),  TEXT("SFX_Gunshot_Tail_ServerComms"), 0.30f },
		{ TEXT("Executive"),    TEXT("SFX_Gunshot_Tail_Executive"),   0.22f },
		{ TEXT("Canteen"),      TEXT("SFX_Gunshot_Tail_Canteen"),     0.45f },
		{ TEXT("Skydeck"),      TEXT("SFX_Gunshot_Tail_Skydeck"),     0.05f },
		{ TEXT("Stairwell"),    TEXT("SFX_Gunshot_Tail_Stairwell"),   0.65f }
	};

	for (const FRoomProfile& Profile : Profiles)
	{
		if (CurrentAmbienceZone == FName(Profile.Zone))
		{
			OutWet = Profile.Wet;
			return FName(Profile.Cue);
		}
	}

	// An unknown room is an office: the commonest space in the building.
	OutWet = 0.2f;
	return FName(TEXT("SFX_Gunshot_Tail_OpenPlan"));
}

USoundAttenuation* UOneBrahamAudioManagerComponent::GetDistantAttenuation()
{
	if (DistantAttenuation != nullptr)
	{
		return DistantAttenuation;
	}

	if (USoundAttenuation* Authored = GetAuthoredAttenuation(FName(TEXT("SA_OneBraham_WeaponDistant"))))
	{
		DistantAttenuation = Authored;
		return DistantAttenuation;
	}

	// Built like the category chains, but long: a distant report is still
	// audible across the plaza, and the near-field crossfade lives in the gain
	// the manager applies, not in the curve.
	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(2500.f, 0.f, 0.f);
	Settings.FalloffDistance = 45000.f;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;
	Settings.bEnableListenerFocus = false;
	Settings.bEnableOcclusion = true;
	Settings.OcclusionTraceChannel = ECC_Visibility;
	Settings.OcclusionLowPassFilterFrequency = 600.f;
	Settings.OcclusionVolumeAttenuation = 0.6f;
	Settings.OcclusionInterpolationTime = 0.2f;

	DistantAttenuation = Attenuation;
	return DistantAttenuation;
}

const FOneBrahamSoundBankEntry* UOneBrahamAudioManagerComponent::FindBankEntry(FName SoundId) const
{
	return Bank != nullptr ? Bank->FindEntry(SoundId) : nullptr;
}

bool UOneBrahamAudioManagerComponent::HasCue(FName SoundId) const
{
	if (Bank != nullptr && Bank->FindEntry(SoundId) != nullptr)
	{
		return true;
	}
	return OneBrahamProceduralAudio::HasSynthesisedSound(SoundId);
}

int32 UOneBrahamAudioManagerComponent::GetActiveBedCount() const
{
	int32 Count = 0;
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& Pair : Beds)
	{
		if (Pair.Value != nullptr && Pair.Value->IsPlaying())
		{
			++Count;
		}
	}
	return Count;
}

FString UOneBrahamAudioManagerComponent::DumpAudioState() const
{
	const FString BusLine = FString::Printf(
		TEXT("Bus volumes | SFX %.2f | Music %.2f | UI %.2f | Voice %.2f%s | Ambience %.2f"),
		GetBusVolume(EOneBrahamAudioBus::SFX),
		GetBusVolume(EOneBrahamAudioBus::Music),
		GetBusVolume(EOneBrahamAudioBus::UI),
		GetBusVolume(EOneBrahamAudioBus::Voice),
		bVoiceBusMuted ? TEXT(" (muted)") : TEXT(""),
		GetBusVolume(EOneBrahamAudioBus::Ambience));

	const FString DuckLine = FString::Printf(
		TEXT("Duck %.2f (count %d) | LP %.0f Hz (target %.0f)"),
		VoiceDuckCurrent, VoiceDuckCount, CurrentLowPassHz, TargetLowPassHz);

	const FString StateLine = FString::Printf(
		TEXT("Music %s | Ambience '%s' | Downed %s | Deafened %s | Lockdown %s | Rotor %s (%.2f)"),
		*UEnum::GetValueAsString(CurrentMusicState),
		*CurrentAmbienceZone.ToString(),
		bDownedAudio ? TEXT("yes") : TEXT("no"),
		bDeafened ? TEXT("yes") : TEXT("no"),
		bLockdownAudio ? TEXT("yes") : TEXT("no"),
		bExtractionAudio ? TEXT("yes") : TEXT("no"),
		ExtractionApproach01);

	const FString BankLine = FString::Printf(
		TEXT("Bank %s | beds %d | plays/s %d | tinnitus %.2f (%.0f Hz) | blast duck %.2f | heartbeat %.2f | room '%s'"),
		Bank != nullptr ? TEXT("loaded") : TEXT("FALLBACK ONLY"),
		GetActiveBedCount(), PlaysThisSecond, TinnitusRemaining, TinnitusLowPassHz, BlastDuckRemaining,
		EffectiveHeartbeat, *CurrentAmbienceZone.ToString());

	return FString::Printf(TEXT("%s\n%s\n%s\n%s"), *BusLine, *DuckLine, *StateLine, *BankLine);
}
