// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamProximityVoiceComponent.h
// ---------------------------------------------------------------------------
// One per character. This is the transport for BOTH live voice chat and the
// character behaviour lines ("barks").
//
// ARCHITECTURE
//   Client   : captures the microphone at 16 kHz mono in 20 ms frames, encodes
//              with the engine voice codec when available, sends each frame to
//              the server as an unreliable RPC.
//   Server   : validates (rate budget, size, channel authority, mute state),
//              then relays the frame ONLY to players who should hear it - inside
//              proximity range, or on the same squad/team for radio channels.
//   Listener : decodes into a per-speaker procedural sound wave and plays it
//              through an audio component. Proximity frames are spatialised on
//              the speaker's head with a distance and occlusion low-pass; radio
//              frames are 2D with a radio filter.
//
// Everything degrades: no microphone, no codec or no Voice module still leaves a
// fully playable game, with barks, subtitles and the HUD indicators intact.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voice/OneBrahamVoiceCodec.h"
#include "OneBrahamProximityVoiceComponent.generated.h"

class AOneBrahamCharacter;
class AOneBrahamPlayerState;
class UAudioComponent;
class USoundWaveProcedural;
class USoundAttenuation;

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamProximityVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamProximityVoiceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// -----------------------------------------------------------------------
	//  Behaviour lines
	// -----------------------------------------------------------------------

	/**
	 * Server-side: speaks a behaviour line to everyone who can hear this
	 * character. Called by UOneBrahamVoiceSubsystem once a line has been chosen.
	 */
	bool ServerSpeakLine(FName LineId, float Priority);

	/**
	 * Every machine: plays the chosen line and forwards the subtitle to the HUD.
	 * Priority is used to duck lower-priority chatter on the receiving end.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSpeakLine(FName LineId, float Priority, float DurationSeconds);

	/** Last line this character said, for the HUD and the debug overlay. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	FName GetLastSpokenLineId() const { return LastSpokenLineId; }

	// -----------------------------------------------------------------------
	//  Live voice chat
	// -----------------------------------------------------------------------

	/** Push-to-talk state for the locally controlled character. Starts capture. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetLocalTransmitting(bool bTransmitting);

	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool IsLocalTransmitting() const { return bLocalTransmitting; }

	/** True when the microphone is open on this machine (device present). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool HasWorkingMicrophone() const { return Capture.IsCapturing(); }

	/** Player-level opt out. Stops capture and refuses incoming frames. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceChatEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool IsVoiceChatEnabled() const { return bVoiceChatEnabled; }

	/** The device the local microphone is using, for Settings -> Audio. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	FString GetCaptureDeviceName() const { return Capture.GetActiveDeviceName(); }

	// -----------------------------------------------------------------------
	//  Transport
	// -----------------------------------------------------------------------

	/** Owning client -> server. Unreliable: a lost 20 ms frame is inaudible. */
	UFUNCTION(Server, Unreliable)
	void ServerPushVoiceFrame(const TArray<uint8>& Frame, uint8 ChannelByte);

	/** Server -> listener. Channel byte is the SERVER's view of the talker's channel. */
	UFUNCTION(Client, Unreliable)
	void ClientReceiveVoiceFrame(int32 SpeakerPlayerId, uint8 ChannelByte, const TArray<uint8>& Frame);

	/** Stops and tears down the playback stream for a speaker (they disconnected). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void StopSpeakerStream(int32 SpeakerPlayerId);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void StopAllSpeakerStreams();

	/** Debug: number of active playback streams on this machine. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	int32 GetActiveStreamCount() const { return StreamComponents.Num(); }

	/** Frames a speaker may send per second: 20 ms frames are 50, plus slack for jitter. */
	static constexpr int32 MaxVoiceFramesPerSecond = 64;

protected:
	/** Server: the current one-second window of incoming frames. */
	double VoiceWindowStart = 0.0;
	int32 VoiceFramesInWindow = 0;

	// --- helpers ---------------------------------------------------------
	AOneBrahamCharacter* GetCharacter() const;
	AOneBrahamPlayerState* GetOwnerState() const;

	/** Mouth position for spatialised playback (falls back to the neck). */
	FVector GetMouthLocation() const;

	/** Creates the playback stream for a speaker if it does not exist yet. */
	UAudioComponent* EnsureSpeakerStream(int32 SpeakerPlayerId, bool bSpatialised);

	/** Applies channel + occlusion filtering to a live stream. */
	void UpdateStreamFiltering(int32 SpeakerPlayerId, float DeltaTime);

	/** Locally owned capture path: pull frames and send them. */
	void TickLocalCapture(float DeltaTime);

	/** Server: relay an accepted frame to every listener who should hear it. */
	void RelayFrameToListeners(AOneBrahamPlayerState* SpeakerState, uint8 ChannelByte, const TArray<uint8>& Frame);

	/** Builds the attenuation object used for spatialised proximity voice. */
	USoundAttenuation* GetVoiceAttenuation();

	/** Voice bus volume (Voice x Master) from the user's audio settings. */
	float GetBusVolumeForVoice() const;

	// --- state -----------------------------------------------------------
	FOneBrahamVoiceCapture Capture;
	FOneBrahamVoiceCodec Codec;

	/** Accumulated time since the last sent frame, for the 20 ms cadence. */
	float FrameTimer = 0.f;

	/** Frames sent in the current second, to self-limit before the server must. */
	int32 FramesSentThisSecond = 0;
	float SendWindowStart = 0.f;

	bool bLocalTransmitting = false;
	bool bVoiceChatEnabled = true;

	/** Line book-keeping for barks. */
	FName LastSpokenLineId = NAME_None;
	float LastSpokenTime = -1000.f;
	float ActiveLinePriority = 0.f;
	float ActiveLineEndTime = 0.f;

	/** Per-speaker playback streams. Strong refs so nothing is collected mid-sentence. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAudioComponent>> StreamComponents;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<USoundWaveProcedural>> StreamWaves;

	/** Channel each stream is currently playing on. */
	TMap<int32, uint8> StreamChannels;

	/** Smoothed occlusion alpha per speaker, so filtering changes are gradual. */
	TMap<int32, float> StreamOcclusion;

	/** Last frame time per speaker, used to tear down idle streams. */
	TMap<int32, float> StreamLastFrameTime;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> VoiceAttenuation = nullptr;
};
