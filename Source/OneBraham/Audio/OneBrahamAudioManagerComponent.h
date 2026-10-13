// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAudioManagerComponent.h
// ---------------------------------------------------------------------------
// Per-player audio director. One instance lives on every AOneBrahamPlayerController.
//
// WHY PER PLAYER
//   ONE BRAHAM's mix is opinionated and player-specific: whether the local player
//   is downed, deafened, holding a suppressed weapon, being talked over on the
//   radio, or standing under a helicopter all change what *this* machine should
//   be playing. A single world audio actor cannot express that, so the manager is
//   a controller component and every machine runs its own.
//
// WHAT IT OWNS
//   1. The bank lookup (DA_AudioBank) with a synthesised fallback for every cue,
//      so a clean checkout is never silent and no mechanic loses its sound.
//   2. Concurrency, cooldowns and pitch randomisation per sound id.
//   3. Category attenuation chains built at runtime (foley/mechanical/weapon/
//      impact/objective/loop), including listener focus and occlusion.
//   4. The beds: zone ambience, music director, lockdown alarm, helicopter rotor,
//      tinnitus ringing - each one managed, faded, and never duplicated.
//   5. Mix state: voice ducking (ref-counted), downed muffle, flashbang muffle,
//      bus volumes from the user's settings.
//
// WHAT IT DELIBERATELY DOES NOT DO
//   It never decides gameplay. Every gameplay cue that carries information is
//   triggered by the gameplay system that owns the event, on every machine that
//   needs to hear it. This component only plays what it is told.

#pragma once

#include "CoreMinimal.h"
#include "Audio/OneBrahamAudioTypes.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamAudioManagerComponent.generated.h"

class UAudioComponent;
class UOneBrahamAudioBankAsset;
class UOneBrahamWeaponDefinition;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
class UPhysicalMaterial;

/**
 * Audio priorities used when a mix decision has to be made. Higher wins a duck;
 * beds use them to decide what gets faded when something bigger happens.
 */
UENUM(BlueprintType)
enum class EOneBrahamAudioPriority : uint8
{
	Ambient = 0,
	Foley = 1,
	Mechanical = 2,
	Gameplay = 3,
	Combat = 4,
	Critical = 5
};

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamAudioManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamAudioManagerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ======================================================================
	//  Core playback
	// ======================================================================
	/** Non-positional. UI, hit markers, radio, own-fuse beeps. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	UAudioComponent* PlaySound2D(FName SoundId, float Volume = 1.f, float Pitch = 1.f);

	/**
	 * Positional with spatialisation, occlusion and listener focus.
	 *
	 * @param AttenuationOverride  An authored attenuation to use instead of the
	 *        one for the cue's category. This is how a gunshot two floors up gets
	 *        its own distance curve without needing its own sound category.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	UAudioComponent* PlaySoundAtLocation(FName SoundId, const FVector& Location, float Volume = 1.f, float Pitch = 1.f, AActor* SourceActor = nullptr, USoundAttenuation* AttenuationOverride = nullptr);

	/** Positional and locked to a component (weapon foley, mag drops into the gun). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	UAudioComponent* PlaySoundAttached(FName SoundId, USceneComponent* AttachTo, FName SocketName = NAME_None, float Volume = 1.f, float Pitch = 1.f);

	// ======================================================================
	//  Gameplay cue wrappers (called from the systems that own the event)
	// ======================================================================
	/** A gunshot from the weapon definition, or a synthesised fallback per calibre. */
	void PlayWeaponShot(const UOneBrahamWeaponDefinition* Definition, const FVector& MuzzleLocation, bool bSuppressed, AActor* Shooter);

	/** Mechanical weapon foley: bolt, mag out, mag in, charge handle, dry fire, safety. */
	void PlayWeaponMechanics(FName SoundId, AActor* Shooter, bool bLocalOnly = false);

	/** Reload stage cue, keyed by "Start" / "MagIn" / "Charge" / "Complete" / "Abort". */
	void PlayReloadCue(FName Stage, AActor* Shooter);

	/** Impact foley resolved from the physical material of whatever was hit. */
	void PlayImpact(FName SoundIdPrefix, const FHitResult& Hit, float VolumeScale = 1.f, AActor* IgnoredActor = nullptr);

	/**
	 * Impact cue from a replicated impact report. This is the single entry point
	 * used by UOneBrahamVFXStatics on every machine, so the dust and the sound
	 * always agree about where the round landed.
	 */
	void PlayImpactSound(const FOneBrahamImpactInfo& Impact, float VolumeScale = 1.f);

	/** A round went past the local player: the whizz/crack pair, 3D and short. */
	void PlayNearMissWhizz(const FVector& Location);

	/** Footstep scheduling. Call every frame; the manager owns the stride length. */
	void TickFootstepAudio(AActor* Stepper, EOneBrahamFootstepSurface Surface, float Speed2D, bool bCrouched, bool bSprinting, bool bInAir, bool bLocalPlayer);

	/** Grenade bounce: surface classified from the impact point, cheap and gated. */
	void PlayUtilityBounce(const FVector& Location, EOneBrahamFootstepSurface Surface = EOneBrahamFootstepSurface::Concrete);

	/** Fuse beep. 2D when it is the local player's own grenade, 3D otherwise. */
	void PlayFuseBeep(const FVector& Location, bool bLocal2D);

	/** Hit confirmation. Local only, always 2D, never attenuated. */
	void PlayHitFeedback(bool bHeadshot, bool bArmourAbsorbed, bool bKill, float DistanceCm);

	/** Taken damage: a body hit, a head ring, or armour absorbing the shot. */
	void PlayDamageTakenCue(bool bArmourAbsorbed, bool bHeadshot);

	/** Radio key-up / key-down click. */
	void PlayRadioOpenClose(bool bOpening, bool bSquadChannel);

	/** Local UI blip (menus, channel switch). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	void PlayLocalUISound(FName SoundId);

	/** The Tannoy two-tone that precedes a building announcement. */
	void PlayAnnouncementChime();

	/** Explosion set: body, debris tail, and a distance-scaled ear-ring for the local player. */
	void PlayExplosion(const FVector& Location, float RadiusCm, FName SoundId);

	// ======================================================================
	//  Beds and mix state
	// ======================================================================
	/** Looping zone ambience built from the room the local player is standing in. */
	void SetAmbienceZone(FName ZoneId, float FadeSeconds = 2.f);

	/** Music director. Ignored when the requested state is already playing. */
	void SetMusicState(EOneBrahamMusicState NewState, bool bForce = false);

	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	EOneBrahamMusicState GetMusicState() const { return CurrentMusicState; }

	/** Fire alarm / klaxon bed, faded in over the building announcement. */
	void SetLockdownAudio(bool bActive, float FadeSeconds = 1.5f);

	/** Helicopter rotor bed, positioned on the pad. `Approach01` speeds it up. */
	void SetExtractionAudio(bool bActive, const FVector& PadLocation, float Approach01);

	/** Downed state: heartbeat, muffled world, breathing. */
	void SetDownedAudioState(bool bDowned);

	/** Flashbang tinnitus, with a ringing bed for the duration. */
	void SetTinnitusState(bool bActive, float DurationSeconds);

	/** Spectating after death: everything gets distant. */
	void SetDeafened(bool bDeafened);

	/** Ref-counted voice duck (0.45x on the beds while anyone is talking). */
	void PushVoiceDuck();
	void PopVoiceDuck();

	/** Voice output gain, applied on top of the user's Voice bus slider. */
	void SetVoiceBusGain(float Gain, bool bMuted);

	// ======================================================================
	//  Queries
	// ======================================================================
	const FOneBrahamSoundBankEntry* FindBankEntry(FName SoundId) const;

	/** True when a cue exists as an asset or as a synthesised fallback. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	bool HasCue(FName SoundId) const;

	/** Bus volume after the user's sliders (0..1). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	float GetBusVolume(EOneBrahamAudioBus Bus) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	float GetVoiceDuckAmount() const { return VoiceDuckCount > 0 ? VoiceDuckTarget : 1.f; }

	/** Current master low-pass in Hz (flashbang 400, downed 900, normal 20000). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	float GetLowPassFrequency() const { return CurrentLowPassHz; }

	/** Number of managed looping beds currently alive. Used by the audit tool. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	int32 GetActiveBedCount() const;

	/** One-line-per-bus summary for the QA overlay and `ob.Audio.Dump`. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	FString DumpAudioState() const;

	/** Re-reads the user's volume/mute settings and re-applies the mix. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Audio")
	void ApplyUserSettings();

	/** Surfaces are classified once here, from the physical material enum. */
	static EOneBrahamFootstepSurface ClassifyGroundSurface(const UPhysicalMaterial* PhysicalMaterial);

	/** Sound id suffix for a surface, e.g. prefix "SFX_Footstep" -> "SFX_Footstep_Marble". */
	static FName BuildSurfaceSoundId(const FName Prefix, EOneBrahamFootstepSurface Surface);
	static const TCHAR* GetSurfaceSuffix(EOneBrahamFootstepSurface Surface);

	/**
	 * The authored attenuation asset a category prefers, or NAME_None.
	 *
	 * Attenuation is authored by Tools/Python/onebraham/build_audio.py as
	 * SA_OneBraham_*; this is the mapping from a category to its default. The
	 * audio manager loads the asset and falls back to building an equivalent
	 * object in memory, so the game is never silent - it is only ever untunable.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|Audio")
	static FName GetAuthoredAttenuationName(EOneBrahamAudioCategory Category);

	/** Explicit lookup by short name ("SA_OneBraham_Suppressed"). NAME_None if absent. */
	USoundAttenuation* GetAuthoredAttenuation(FName AssetName);

	/**
	 * True when a sound is at least half a storey above or below the local
	 * listener.
	 *
	 * Half a storey is the threshold the whole game uses for "through the floor":
	 * OneBrahamConstants::FloorHeightCm is 400, so 200 cm is exactly the point at which a listener
	 * is closer to the next storey than to this one. Using one number everywhere
	 * is how the audio, the bot hearing and the HUD compass stay in agreement.
	 */
	bool IsMuzzleAboveOrBelowListener(const FVector& Location) const;

protected:
	/** Loads the bank (and its mix objects) once per world. */
	void EnsureBankLoaded();

	/** Resolves a cue to something playable: bank asset, else synthesised, else null. */
	USoundBase* ResolveSound(FName SoundId, const FOneBrahamSoundBankEntry** OutEntry);

	/** Runtime attenuation chain for a category, built once and cached. */
	USoundAttenuation* GetAttenuation(EOneBrahamAudioCategory Category, float MaxDistanceCm);

	/** Budget check: cooldown + concurrency. Registers the play when it returns true. */
	bool CanPlayCue(FName SoundId, const FOneBrahamSoundBankEntry* Entry);
	void RegisterCuePlayed(FName SoundId, UAudioComponent* Component);

	/** Managed looping bed: one component per bed id, reused and faded. */
	UAudioComponent* AcquireBed(FName BedId, FName SoundId, bool bSpatialised, const FVector& Location, float Volume, float FadeSeconds);
	void StopBed(FName BedId, float FadeSeconds);

	/** Applies the effective low-pass to every managed component and, when the mix
	 *  assets exist, to the submix as well. */
	void UpdateLowPass(float DeltaTime);

	/** Music director: watches match phase, survivors and lockdown. */
	void UpdateMusicDirector(float DeltaTime);

	/** Empties cooldown/concurrency tables of dead components. */
	void PruneCueTables();

	UFUNCTION()
	void HandleUserSettingsChanged();

	// ======================================================================
	//  Cached state
	// ======================================================================
	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamAudioBankAsset> Bank = nullptr;

	/** One attenuation object per category, resolved at first use. */
	UPROPERTY(Transient)
	TMap<EOneBrahamAudioCategory, TObjectPtr<USoundAttenuation>> Attenuations;

	/**
	 * Authored attenuations, by asset name, including the null results.
	 *
	 * Caching the misses matters: a missing authored attenuation in a build that
	 * was never meant to have audio content would otherwise attempt a package
	 * load on every gunshot, which is a frame-time bug that only shows up in
	 * profiling and never in a log.
	 */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USoundAttenuation>> AuthoredAttenuations;

	/** Sound id -> world time of last play, for cooldowns. */
	TMap<FName, float> LastPlayTimes;

	/** Sound id -> components currently alive for it, for concurrency. */
	TMap<FName, TArray<TWeakObjectPtr<UAudioComponent>>> ActiveCues;

	/** Managed beds by id ("Ambience", "Music", "Lockdown", "Rotor", "Tinnitus"). */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAudioComponent>> Beds;

	/** Bed ids that were asked to stop, with the time the fade began. */
	TMap<FName, float> BedStopTimes;

	/** Per-actor stride accumulator for footsteps. */
	TMap<TWeakObjectPtr<const AActor>, float> FootstepDistance;

	// --- director state ---------------------------------------------------
	FName CurrentAmbienceZone = NAME_None;
	EOneBrahamMusicState CurrentMusicState = EOneBrahamMusicState::Lobby;
	EOneBrahamMusicState RequestedMusicState = EOneBrahamMusicState::Lobby;

	int32 VoiceDuckCount = 0;
	float VoiceDuckTarget = 1.f;
	float VoiceDuckCurrent = 1.f;

	/** Base volume the user's Voice slider produced, before talking gain. */
	float VoiceBusGain = 1.f;
	bool bVoiceBusMuted = false;

	/** True while a voice transmission is active on any channel. */
	bool bVoiceActive = false;

	bool bDownedAudio = false;
	bool bDeafened = false;
	bool bLockdownAudio = false;
	bool bExtractionAudio = false;
	float ExtractionApproach01 = 0.f;
	FVector ExtractionPadLocation = FVector::ZeroVector;

	float TinnitusRemaining = 0.f;

	/** Effective low-pass in Hz, smoothed towards its target each tick. */
	float CurrentLowPassHz = 20000.f;
	float TargetLowPassHz = 20000.f;

	/** Seconds between music director re-evaluations. */
	float MusicDirectorAccumulator = 0.f;

	/** Throttles the "missing cue" warning so a broken bank cannot spam the log. */
	TSet<FName> WarnedMissingIds;

	bool bAppliedUserSettingsOnce = false;

	/** Debug: counts cue plays this second, for `stat`-adjacent QA readouts. */
	int32 PlaysThisSecond = 0;
	float DebugAccumulator = 0.f;
};
