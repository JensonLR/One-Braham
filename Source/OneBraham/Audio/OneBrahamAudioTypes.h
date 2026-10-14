// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAudioTypes.h
// ---------------------------------------------------------------------------
// Audio vocabulary shared by the audio manager, the HUD, the voice subsystem and
// the weapon/utility code. Keep this header dependency-light: it is included
// from gameplay code that should never need to know what a submix is.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "Engine/DataAsset.h"
#include "OneBrahamAudioTypes.generated.h"

class USoundBase;
class USoundClass;
class USoundMix;
class USoundAttenuation;
class USoundConcurrency;
class UAudioComponent;

/**
 * Mix bus a sound belongs to. Every bus maps to a Sound Class created by the
 * audio bank asset, and every bus has its own user volume slider in Settings ->
 * Audio, which is a requirement for shipping on Steam.
 */
UENUM(BlueprintType)
enum class EOneBrahamAudioBus : uint8
{
	/** Weapons, explosions, impacts - the first slider a player turns down. */
	SFX			UMETA(DisplayName = "Effects"),
	/** Player and weapon foley that carries information (fuse, reload, steps). */
	Gameplay	UMETA(DisplayName = "Gameplay Cues"),
	/** Proximity voice, radio, and character behaviour lines. */
	Voice		UMETA(DisplayName = "Voice"),
	/** Looping tower ambience and room tone. */
	Ambience	UMETA(DisplayName = "Ambience"),
	/** Score and stingers. */
	Music		UMETA(DisplayName = "Music"),
	/** Menus, hit markers, UI blips - never spatially attenuated. */
	UI			UMETA(DisplayName = "Interface")
};

/** Distance categories. Drives both attenuation and the concurrency budget. */
UENUM(BlueprintType)
enum class EOneBrahamAudioCategory : uint8
{
	/** 2D, never attenuated. UI, hit markers, radio. */
	Interface		UMETA(DisplayName = "Interface"),
	/** Short range foley: footsteps, clothing, magazine handling. 25 m. */
	Foley			UMETA(DisplayName = "Foley"),
	/** Mechanical: doors, lifts, reloads, breach plant. 45 m. */
	Mechanical		UMETA(DisplayName = "Mechanical"),
	/** Weapon reports and impacts. Range comes from the weapon definition. */
	Weapon			UMETA(DisplayName = "Weapon"),
	/** Explosions, glass, structural damage. 120 m. */
	Impact			UMETA(DisplayName = "Impact"),
	/** Objective, alarm and extraction cues. Always clearly audible. */
	Objective		UMETA(DisplayName = "Objective"),
	/** Looping beds (ambience, rotor, alarm). Managed as a single instance. */
	Loop			UMETA(DisplayName = "Loop")
};

/** Music director state. The tower has a mood for every phase of a match. */
UENUM(BlueprintType)
enum class EOneBrahamMusicState : uint8
{
	/** Menu, lobby, warm-up. Calm, corporate, slightly sinister. */
	Lobby			UMETA(DisplayName = "Lobby"),
	/** Early match: sparse, low, room to hear footsteps. */
	MatchEarly		UMETA(DisplayName = "Match Early"),
	/** Mid match: pulse enters. */
	MatchMid		UMETA(DisplayName = "Match Mid"),
	/** Late match / few survivors: high tension, sparse percussion. */
	MatchLate		UMETA(DisplayName = "Match Late"),
	/** Lockdown or security purge: alarms and heavier percussion. */
	Lockdown		UMETA(DisplayName = "Lockdown"),
	/** Extraction live: the only track with a melody. */
	Extraction		UMETA(DisplayName = "Extraction"),
	/** Downed / spectating: hollow, filtered, near silence. */
	Downed			UMETA(DisplayName = "Downed"),
	/** Results screen. */
	Results			UMETA(DisplayName = "Results")
};

/**
 * One row of the audio bank.
 *
 * A row always has a *stable id* (e.g. "SFX_FuseBeep"). The asset reference is
 * optional: when it is null the manager falls back to the procedural
 * synthesiser, which is how the game stays audible with a clean checkout.
 */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamSoundBankEntry
{
	GENERATED_BODY()

	/** Stable id used from code. Never localised, never renamed casually. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	FName SoundId = NAME_None;

	/** Authored asset. Null = use the procedural fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	EOneBrahamAudioBus Bus = EOneBrahamAudioBus::SFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	EOneBrahamAudioCategory Category = EOneBrahamAudioCategory::Foley;

	/** Base volume before bus volume is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Volume = 1.f;

	/** Randomised pitch range - the single cheapest way to stop repetition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	FVector2D PitchRange = FVector2D(0.97f, 1.03f);

	/** Max 3D distance. Interface sounds ignore this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0"))
	float MaxDistanceCm = 4500.f;

	/** Air absorption: how much the high frequencies die with distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirAbsorption = 0.35f;

	/** How many of this sound may be active at once before the oldest is stolen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "1"))
	int32 MaxConcurrent = 6;

	/** Minimum seconds between two plays of this id from the same manager. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.f;

	/** Extra ground to gain when the local listener is indoors (building reverb). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool bGainsIndoors = false;

	/** True for a seamless loop played through a managed audio component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool bLooping = false;

	/** Classify a surface so footsteps and impacts pick the right foley. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool bSurfaceSensitive = false;
};

/**
 * The audio bank.
 *
 * Shipped at /Game/OneBraham/Audio/Banks/DA_AudioBank. A designer can retune
 * every cue, bus, level and distance in one place without touching C++, and the
 * C++ fallback table covers every id so a missing row can never silence a
 * mechanic (the manager logs a warning instead).
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamAudioBankAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank", meta = (TitleProperty = "SoundId"))
	TArray<FOneBrahamSoundBankEntry> Entries;

	/** Sound class hierarchy, created by Tools/Python/onebraham/create_audio_assets.py. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Mix")
	TSoftObjectPtr<USoundClass> MasterSoundClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Mix")
	TSoftObjectPtr<USoundMix> BaseSoundMix;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Mix")
	TSoftObjectPtr<USoundMix> VoiceDuckSoundMix;

	/** Master low-pass applied while the local player is deafened by a flashbang. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Mix")
	FVector2D FlashLowPassRange = FVector2D(400.f, 20000.f);

	/** Master low-pass applied while the local player is downed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Mix")
	FVector2D DownedLowPassRange = FVector2D(900.f, 20000.f);

	/**
	 * The cue each director state plays.
	 *
	 * Cue ids, not soft pointers, because a bed is played by id: the authoring
	 * route for music is the row for `MUS_MatchMid` in `Entries` (or the wave the
	 * recording brief names), the same route every other cue in the game uses.
	 * This map used to hold soft object paths instead, with no reader anywhere -
	 * a second mechanism for something that already had one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Music")
	TMap<EOneBrahamMusicState, FName> MusicCueByState;

	/** The cue for a director state; the lobby bed for a state we do not know. */
	FName GetMusicCueForState(EOneBrahamMusicState State) const;

	/**
	 * Zone id -> the cue that plays in it.
	 *
	 * The zone ids are the building layout's area ids - Ground_Plaza, Lobby,
	 * Mezzanine, OpenPlan, ServerComms, Executive, Canteen, Skydeck - plus
	 * Stairwell, because that is where you are between two of them. They are the
	 * same ids OneBrahamBuildingLayoutAsset returns, so the audio director cannot
	 * end up with a vocabulary of its own: this table and the layout are checked
	 * against each other by `check_audio_ids.py`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Ambience")
	TMap<FName, FName> AmbienceCueByZone;

	/** The zone used when a floor has no area: the stairwell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio Bank|Ambience")
	FName AmbienceFallbackZone = FName(TEXT("Stairwell"));

	/** The cue id for a zone; the generic city bed for a zone we do not know. */
	FName GetAmbienceCueForZone(FName ZoneId) const;

	/** Finds a row. Returns null when the id is not in the bank. */
	const FOneBrahamSoundBankEntry* FindEntry(FName SoundId) const;

	/** Adds any missing core row with a synthesised fallback. Idempotent. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Audio Bank")
	void EnsureCoreEntries();

	/** Logs every duplicated or empty id. Run from the editor validator. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Audio Bank")
	bool ValidateBank(TArray<FString>& OutProblems) const;
};
