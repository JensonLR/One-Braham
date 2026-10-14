// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVoiceTypes.h
// ---------------------------------------------------------------------------
// Shared voice vocabulary: character barks (behaviour lines), the shipped voice
// script, and the live proximity/radio chat transport.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "OneBrahamVoiceTypes.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * How important a behaviour line is.
 *
 * The subsystem runs a single-line-at-a-time budget per character, and priority
 * decides who wins when two systems want to talk. Tactical information always
 * beats flavour, because a player needs "reloading!" more than "lovely carpet".
 */
UENUM(BlueprintType)
enum class EOneBrahamVoicePriority : uint8
{
	/** Idle muttering. First thing to be dropped. */
	Ambient		= 0,
	/** Character flavour (a low, dry comment). */
	Flavour		= 1,
	/** Useful information. */
	Tactical	= 2,
	/** Combat-critical: contact, reload, downed, grenade. */
	Critical	= 3
};

/** One line of the voice script. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamVoiceLine
{
	GENERATED_BODY()

	/** Stable id, e.g. V_DOWNED_CALL_HELP. The script in Docs/14_Voice_Script.md
	 *  and the recorded assets are keyed by this, so it is never renamed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FName LineId = NAME_None;

	/** On-screen subtitle. British English, dry, never a pun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FText Subtitle;

	/** The recorded line. When null the subsystem generates nothing audible and
	 *  only shows the subtitle, which keeps the game readable pre-recording. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TSoftObjectPtr<USoundBase> Sound;

	/** Higher wins when two lines compete. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	EOneBrahamVoicePriority Priority = EOneBrahamVoicePriority::Tactical;

	/** Global cooldown for this exact line, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float CooldownSeconds = 8.f;

	/** Per-request chance of actually speaking, 0..1. Keeps lines from becoming
	 *  a metronome. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float Chance = 1.f;

	/** Which archetypes may say it. Empty = all six. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<FGameplayTag> ArchetypeFilter;

	/** Subtitle display time; 0 = derive from the sound length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float SubtitleSeconds = 0.f;
};

/**
 * The complete behaviour-line set for the game.
 *
 * Shipped as /Game/OneBraham/Voice/DA_VoiceLines. The code-only fallback table
 * in OneBrahamVoiceLineSet.cpp carries the same ids so nothing is ever missing;
 * a designer can then assign the recorded assets line by line.
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamVoiceLineSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (TitleProperty = "LineId"))
	TArray<FOneBrahamVoiceLine> Lines;

	/** Adds any missing core line from the C++ fallback table. Idempotent. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Voice")
	void EnsureCoreLines();

	/** All lines with this id (there is normally exactly one). */
	void FindLines(FName LineId, TArray<const FOneBrahamVoiceLine*>& OutLines) const;

	/** Pick a variant at random, honouring the archetype filter. Null if none. */
	const FOneBrahamVoiceLine* PickLine(FName LineId, FGameplayTag Archetype, FRandomStream& Stream) const;
};

/** Live voice chat transport settings, resolved from the match settings asset. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamVoiceChatConfig
{
	GENERATED_BODY()

	/** Frames per second the server will relay per talker before it throttles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 MaxFramesPerSecond = 60;

	/** Bytes per second the server will relay per talker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 MaxBytesPerSecond = 64000;

	/** Frames per second sent by the local capture path (20 ms frames). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 SendFramesPerSecond = 50;

	/** Proximity chat falls off to nothing past this. Mirrors the match setting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	float ProximityRangeCm = 3500.f;

	/** Jitter buffer depth in frames. 3 frames = 60 ms, imperceptible locally,
	 *  enough to ride out a mobile-style connection spike. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 JitterBufferFrames = 3;

	/** Radio-filter cutoff for Squad/Team channels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	float RadioLowPassHz = 3200.f;
};

/** Per-talker relay accounting, used to enforce the transport budget server side. */
USTRUCT()
struct FOneBrahamVoiceBudget
{
	GENERATED_BODY()

	/** Ring of frame timestamps for the last second. */
	TArray<float> RecentFrameTimes;
	TArray<int32> RecentFrameSizes;
	float WindowStart = 0.f;
	int32 DroppedFrames = 0;

	/** True when the talker is inside budget. Records the frame when it passes. */
	bool TryConsume(float Now, int32 FrameBytes, int32 MaxFramesPerSecond, int32 MaxBytesPerSecond);
	void Reset();
};
