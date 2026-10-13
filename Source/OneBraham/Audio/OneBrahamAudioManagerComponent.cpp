// Copyright (c) One Braham. All Rights Reserved.

#include "Audio/OneBrahamAudioManagerComponent.h"

#include "Audio/OneBrahamAudioTypes.h"
#include "Audio/OneBrahamProceduralAudio.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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

	// The music director starts wherever the match phase says it should.
	SetMusicState(EOneBrahamMusicState::Lobby);
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

	Super::EndPlay(EndPlayReason);
}

void UOneBrahamAudioManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateLowPass(DeltaTime);

	// Music director runs at 2 Hz: it reacts to phases and survivor counts, not frames.
	MusicDirectorAccumulator += DeltaTime;
	if (MusicDirectorAccumulator >= 0.5f)
	{
		MusicDirectorAccumulator = 0.f;
		UpdateMusicDirector(DeltaTime);
		PruneCueTables();
	}

	// Tinnitus decay is authoritative on the local machine.
	if (TinnitusRemaining > 0.f)
	{
		TinnitusRemaining = FMath::Max(0.f, TinnitusRemaining - DeltaTime);
		if (TinnitusRemaining <= 0.f)
		{
			StopBed(BedTinnitus, 0.6f);
			TargetLowPassHz = bDownedAudio ? OBLowPassDowned : (bDeafened ? OBLowPassSpectating : OBLowPassOpen);
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
			Entry->Sound.LoadAsync();
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
		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(CurrentLowPassHz);
		RegisterCuePlayed(SoundId, Component);
	}

	return Component;
}

UAudioComponent* UOneBrahamAudioManagerComponent::PlaySoundAtLocation(FName SoundId, const FVector& Location, float Volume, float Pitch, AActor* SourceActor, USoundAttenuation* AttenuationOverride)
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

	if (bSuppressed)
	{
		SoundId = FName(TEXT("SFX_Gunshot_Suppressed"));
	}
	else if (Definition != nullptr)
	{
		switch (Definition->Category)
		{
		case EOneBrahamWeaponCategory::Pistol:
		case EOneBrahamWeaponCategory::CompactPistol:
		case EOneBrahamWeaponCategory::SuppressedPistol:
			SoundId = FName(TEXT("SFX_Gunshot_Pistol"));
			break;
		case EOneBrahamWeaponCategory::SMG:
		case EOneBrahamWeaponCategory::PDW:
			SoundId = FName(TEXT("SFX_Gunshot_SMG"));
			break;
		case EOneBrahamWeaponCategory::PumpShotgun:
		case EOneBrahamWeaponCategory::SemiAutoShotgun:
			SoundId = FName(TEXT("SFX_Gunshot_Shotgun"));
			break;
		case EOneBrahamWeaponCategory::Carbine556:
		case EOneBrahamWeaponCategory::CompactAssaultRifle:
			SoundId = FName(TEXT("SFX_Gunshot_Rifle"));
			break;
		case EOneBrahamWeaponCategory::MarksmanRifle:
			SoundId = FName(TEXT("SFX_Gunshot_Marksman"));
			break;
		default:
			SoundId = FName(TEXT("SFX_Gunshot_Rifle"));
			break;
		}
	}

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
	else if (IsMuzzleAboveOrBelowListener(MuzzleLocation))
	{
		ShotAttenuation = GetAuthoredAttenuation(FName(TEXT("SA_OneBraham_WeaponInterior")));
	}

	// A gunshot is the one cue that must never be dropped for budget reasons: if
	// the budget is full we still play, because the alternative is a silent shot
	// in a game whose entire combat loop is built on hearing where shots come from.
	PlaySoundAtLocation(SoundId, MuzzleLocation, 1.f, FMath::FRandRange(0.97f, 1.03f), Shooter, ShotAttenuation);

	// Suppressed weapons get their mechanical action as part of the report, which
	// is what makes a suppressed SMG still identifiable across an open office.
	if (bSuppressed)
	{
		PlaySoundAtLocation(FName(TEXT("SFX_Shell_Drop")), MuzzleLocation, 0.8f, FMath::FRandRange(0.95f, 1.05f), Shooter);
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

	// Flesh and glass read differently enough that they get their own ids; every
	// other surface resolves through the physical material of the hit component.
	EOneBrahamFootstepSurface Surface = EOneBrahamFootstepSurface::Concrete;

	if (const UPrimitiveComponent* Component = Hit.GetComponent())
	{
		Surface = ClassifyGroundSurface(Component->GetBodyInstance() != nullptr
			? Component->GetBodyInstance()->GetSimplePhysicalMaterial()
			: nullptr);
	}

	FName Suffix;
	switch (Surface)
	{
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::ServerRack:
	case EOneBrahamFootstepSurface::Decking:
		Suffix = FName(TEXT("Metal"));
		break;
	case EOneBrahamFootstepSurface::Wood:
	case EOneBrahamFootstepSurface::Plasterboard:
		Suffix = FName(TEXT("Wood"));
		break;
	case EOneBrahamFootstepSurface::Glass:
	case EOneBrahamFootstepSurface::BallisticGlass:
		Suffix = FName(TEXT("Glass"));
		break;
	case EOneBrahamFootstepSurface::Carpet:
	case EOneBrahamFootstepSurface::Fabric:
		Suffix = FName(TEXT("Carpet"));
		break;
	case EOneBrahamFootstepSurface::Flesh:
		Suffix = FName(TEXT("Flesh"));
		break;
	default:
		Suffix = FName(TEXT("Concrete"));
		break;
	}

	const FName SoundId = FName(*FString::Printf(TEXT("%s_%s"), *SoundIdPrefix.ToString(), *Suffix.ToString()));
	PlaySoundAtLocation(SoundId, Hit.ImpactPoint, VolumeScale, FMath::FRandRange(0.95f, 1.06f), IgnoredActor);
}

void UOneBrahamAudioManagerComponent::PlayImpactSound(const FOneBrahamImpactInfo& Impact, float VolumeScale)
{
	// One id per material family. A round into a plasterboard partition, a desk
	// and a server rack should be three different sounds, because in this game
	// the difference is information: it tells you what is on the other side.
	FName Suffix = FName(TEXT("Concrete"));
	switch (Impact.Surface)
	{
	case EOneBrahamFootstepSurface::Metal:
	case EOneBrahamFootstepSurface::Grating:
	case EOneBrahamFootstepSurface::ServerRack:
	case EOneBrahamFootstepSurface::Decking:
		Suffix = FName(TEXT("Metal"));
		break;
	case EOneBrahamFootstepSurface::Wood:
	case EOneBrahamFootstepSurface::Plasterboard:
		Suffix = FName(TEXT("Wood"));
		break;
	case EOneBrahamFootstepSurface::Glass:
	case EOneBrahamFootstepSurface::BallisticGlass:
		Suffix = FName(TEXT("Glass"));
		break;
	case EOneBrahamFootstepSurface::Carpet:
	case EOneBrahamFootstepSurface::Fabric:
		Suffix = FName(TEXT("Carpet"));
		break;
	case EOneBrahamFootstepSurface::Flesh:
		Suffix = FName(TEXT("Flesh"));
		break;
	default:
		break;
	}

	const bool bFlesh = Impact.bHitCharacter;
	const FName SoundId = bFlesh
		? FName(TEXT("SFX_Bullet_Impact_Flesh"))
		: FName(*FString::Printf(TEXT("SFX_Bullet_Impact_%s"), *Suffix.ToString()));

	// A penetrated round arrives duller and quieter: it already went through
	// something on the way here.
	const float PenetrationScale = Impact.bPenetrated ? 0.7f : 1.f;
	const float Pitch = bFlesh ? FMath::FRandRange(0.96f, 1.05f) : FMath::FRandRange(0.94f, 1.08f);

	PlaySoundAtLocation(SoundId, Impact.Location, VolumeScale * PenetrationScale, Pitch, nullptr);

	// Headshots on an enemy helmet ring like a struck bell - the single most
	// satisfying sound in a serious shooter, and it carries information.
	if (bFlesh && Impact.HitBoneName == FName(TEXT("head")))
	{
		PlaySoundAtLocation(FName(TEXT("SFX_Helmet_Ping")), Impact.Location, 1.f, FMath::FRandRange(0.98f, 1.04f), nullptr);
	}
}

void UOneBrahamAudioManagerComponent::PlayNearMissWhizz(const FVector& Location)
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
	PlaySoundAtLocation(FName(TEXT("SFX_Bullet_Whizz")), Location, FMath::Lerp(0.45f, 1.f, Closeness), FMath::Lerp(0.92f, 1.14f, Closeness), nullptr);
}

// ===========================================================================
//  Local feedback
// ===========================================================================

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
	PlaySound2D(FName(TEXT("UI_Damage_Taken")), bHeadshot ? 1.15f : 1.f, bArmourAbsorbed ? 0.92f : 1.f);
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
	PlaySoundAtLocation(SoundId, Location, 1.f, FMath::FRandRange(0.96f, 1.04f), nullptr);

	// Inside the blast radius the local player gets a temporary ear-ring: the
	// world muffles for a second and a half, which is the difference between
	// "a grenade went off nearby" and "a grenade went off next to my head".
	const APawn* Listener = nullptr;
	if (const APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		Listener = PC->GetPawn();
	}

	if (Listener != nullptr)
	{
		const float Distance = FVector::Dist(Listener->GetActorLocation(), Location);
		if (Distance < RadiusCm * 1.35f)
		{
			const float Strength = FMath::GetMappedRangeValueClamped(
				FVector2D(RadiusCm * 0.35f, RadiusCm * 1.35f), FVector2D(1.f, 0.f), Distance);

			const float RingSeconds = FMath::Lerp(0.9f, 3.4f, Strength);
			SetTinnitusState(true, RingSeconds);

			UE_LOG(LogOneBrahamAudio, Verbose, TEXT("[OB.AUDIO] Blast tinnitus: %.2fs at %.0f cm (strength %.2f)"),
				RingSeconds, Distance, Strength);
		}
	}
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

	// Zone ids are the map's audio zones ("Lobby", "Skydeck", "ServerRoom", ...).
	// The bank maps them to authored beds; the fallback map covers a clean checkout.
	static const TMap<FName, FName> ZoneToCue =
	{
		{ FName(TEXT("Plaza")),			FName(TEXT("AMB_Plaza_Street")) },
		{ FName(TEXT("Lobby")),			FName(TEXT("AMB_Lobby")) },
		{ FName(TEXT("Mezzanine")),		FName(TEXT("AMB_Lift_Lobby")) },
		{ FName(TEXT("Offices")),		FName(TEXT("AMB_Office_Hum")) },
		{ FName(TEXT("ServerRoom")),	FName(TEXT("AMB_Server_Room")) },
		{ FName(TEXT("Executive")),		FName(TEXT("AMB_Executive")) },
		{ FName(TEXT("Canteen")),		FName(TEXT("AMB_Canteen")) },
		{ FName(TEXT("Skydeck")),		FName(TEXT("AMB_Skydeck_Wind")) },
		{ FName(TEXT("Stairwell")),		FName(TEXT("AMB_Stairwell_Door")) }
	};

	if (ZoneId.IsNone())
	{
		StopBed(BedAmbience, FadeSeconds);
		return;
	}

	const FName* Cue = ZoneToCue.Find(ZoneId);
	const FName SoundId = Cue != nullptr ? *Cue : FName(TEXT("AMB_London_Bed"));

	AcquireBed(BedAmbience, SoundId, false, FVector::ZeroVector, GetBusVolume(EOneBrahamAudioBus::Ambience), FadeSeconds);
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

	FName SoundId = FName(TEXT("MUS_Lobby"));
	switch (NewState)
	{
	case EOneBrahamMusicState::Lobby:		SoundId = FName(TEXT("MUS_Lobby")); break;
	case EOneBrahamMusicState::MatchEarly:	SoundId = FName(TEXT("MUS_MatchEarly")); break;
	case EOneBrahamMusicState::MatchMid:	SoundId = FName(TEXT("MUS_MatchMid")); break;
	case EOneBrahamMusicState::MatchLate:	SoundId = FName(TEXT("MUS_MatchLate")); break;
	case EOneBrahamMusicState::Lockdown:	SoundId = FName(TEXT("MUS_Lockdown")); break;
	case EOneBrahamMusicState::Extraction:	SoundId = FName(TEXT("MUS_Extraction")); break;
	case EOneBrahamMusicState::Downed:		SoundId = FName(TEXT("MUS_Downed")); break;
	case EOneBrahamMusicState::Results:		SoundId = FName(TEXT("MUS_Results")); break;
	default:								SoundId = FName(TEXT("MUS_Lobby")); break;
	}

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

	if (bDowned)
	{
		// Downed: heartbeat, shallow breathing, and the whole building moved behind
		// a wall. It should be obvious you are in trouble without reading the HUD.
		AcquireBed(BedDowned, FName(TEXT("SFX_Downed_Heartbeat")), false, FVector::ZeroVector,
			GetBusVolume(EOneBrahamAudioBus::Gameplay), 0.6f);
		SetMusicState(EOneBrahamMusicState::Downed);
	}
	else
	{
		StopBed(BedDowned, 1.2f);
	}

	// The downed low-pass is the strongest muffle in the game apart from a
	// flashbang at point blank.
	TargetLowPassHz = bDowned ? OBLowPassDowned : (bDeafened ? OBLowPassSpectating : OBLowPassOpen);
}

void UOneBrahamAudioManagerComponent::SetTinnitusState(bool bActive, float DurationSeconds)
{
	if (!bActive)
	{
		TinnitusRemaining = 0.f;
		StopBed(BedTinnitus, 1.5f);
		TargetLowPassHz = bDownedAudio ? OBLowPassDowned : (bDeafened ? OBLowPassSpectating : OBLowPassOpen);
		return;
	}

	TinnitusRemaining = FMath::Max(TinnitusRemaining, DurationSeconds);

	AcquireBed(BedTinnitus, FName(TEXT("SFX_Flashbang_Ring")), false, FVector::ZeroVector,
		GetBusVolume(EOneBrahamAudioBus::Gameplay), 0.15f);

	// Ringing pushed to the front, everything else pushed to the back.
	TargetLowPassHz = OBLowPassFlashed;
}

void UOneBrahamAudioManagerComponent::SetDeafened(bool bDeafened)
{
	bDeafened = bDeafened;

	if (bDeafened)
	{
		TargetLowPassHz = OBLowPassSpectating;
	}
	else if (!bDownedAudio && TinnitusRemaining <= 0.f)
	{
		TargetLowPassHz = OBLowPassOpen;
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

	const UOneBrahamGameState* GS = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
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
		TEXT("Bank %s | beds %d | plays/s %d | tinnitus %.2f"),
		Bank != nullptr ? TEXT("loaded") : TEXT("FALLBACK ONLY"),
		GetActiveBedCount(), PlaysThisSecond, TinnitusRemaining);

	return FString::Printf(TEXT("%s\n%s\n%s\n%s"), *BusLine, *DuckLine, *StateLine, *BankLine);
}
