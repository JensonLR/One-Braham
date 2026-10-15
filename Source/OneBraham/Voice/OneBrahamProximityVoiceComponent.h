// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamProximityVoiceComponent.h
// ---------------------------------------------------------------------------
// One per character. This is the transport for BOTH live voice chat and the
// character behaviour lines ("barks").
//
// TWO HOSTS, ONE CLASS
//   Every character carries one of these for its behaviour lines ("barks"),
//   which are multicast from the character so they come from its head.
//   Live voice chat is hosted by the copy on the player's PlayerState instead
//   (AOneBrahamPlayerState::VoiceComponent). The PlayerState exists before the
//   pawn and outlives it, so a dead player, a spectator, or a player between
//   respawns still captures, sends, receives and keeps their playback streams.
//   The live-chat API on a character's copy forwards to its PlayerState's copy,
//   so existing callers (the HUD) keep working unchanged.
//
// ARCHITECTURE (live chat)
//   Client   : captures the microphone at 16 kHz mono in 20 ms frames, gates
//              them by mode (push-to-talk, voice activation, open mic), encodes
//              with Opus when available and sends each frame to the server as
//              an unreliable RPC, paced from a bounded outgoing queue.
//   Server   : validates (rate budget, size, channel, mute state), then relays
//              the frame ONLY to players who should hear it - the pure rule in
//              OneBrahamVoiceRouting::ResolveRoute - on the channel they should
//              hear it on.
//   Listener : decodes with a decoder of that speaker's own into a per-speaker
//              procedural sound wave. Proximity frames are spatialised on the
//              speaker's head with distance, wall, floor and crouch shaping;
//              radio frames are 2D with a band-pass radio filter.
//
// Everything degrades: no microphone, no permission, no codec or no Voice
// module still leaves a fully playable game, with barks, subtitles and the HUD
// indicators intact.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voice/OneBrahamVoiceCodec.h"
#include "Voice/OneBrahamVoiceTypes.h"
#include "OneBrahamProximityVoiceComponent.generated.h"

class AOneBrahamCharacter;
class AOneBrahamPlayerState;
class UAudioComponent;
class USoundWaveProcedural;
class USoundAttenuation;
class UOneBrahamAudioManagerComponent;

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
	//  Live voice chat (forwarded to the PlayerState's copy from a character)
	// -----------------------------------------------------------------------

	/** Push-to-talk key state for the local player. In voice-activation and
	 *  open-mic modes, holding it forces transmission. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetLocalTransmitting(bool bTransmitting);

	/** True while this machine is actually sending voice (key held, or the
	 *  voice-activation gate open). Drives the local HUD mic icon. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool IsLocalTransmitting() const;

	/** False when the microphone cannot be used: no device, the Voice module is
	 *  off, or the OS refused permission. The HUD shows "mic unavailable". */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool HasWorkingMicrophone() const;

	/** True when the OS has refused (or cannot be asked for) microphone access,
	 *  so the settings menu can say why rather than just "unavailable". */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool IsMicrophonePermissionDenied() const;

	/** Player-level opt out. Stops capture and refuses incoming frames. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetVoiceChatEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool IsVoiceChatEnabled() const;

	/** The device the local microphone is using, for Settings -> Audio. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	FString GetCaptureDeviceName() const;

	/** The copy that carries live chat for this player (their PlayerState's), or
	 *  null when it does not exist yet. Returns this on a PlayerState. */
	UOneBrahamProximityVoiceComponent* GetChatHost() const;

	/** True for the copy that lives on a PlayerState. */
	bool IsChatHost() const;

	/** Drops a speaker locally at once, ahead of the server's mute list arriving. */
	void SetSpeakerMutedLocally(int32 SpeakerPlayerId, bool bMuted);

	// -----------------------------------------------------------------------
	//  Transport
	// -----------------------------------------------------------------------

	/** Owning client -> server. Unreliable: a lost 20 ms frame is inaudible. */
	UFUNCTION(Server, Unreliable)
	void ServerPushVoiceFrame(const TArray<uint8>& Frame, uint8 ChannelByte);

	/** Server -> listener. Channel byte is the channel the SERVER routed it on. */
	UFUNCTION(Client, Unreliable)
	void ClientReceiveVoiceFrame(int32 SpeakerPlayerId, uint8 ChannelByte, const TArray<uint8>& Frame);

	/**
	 * Owning client -> server: this player started/stopped transmitting. Sets the
	 * replicated AOneBrahamPlayerState::bIsTransmitting the squad panel reads.
	 * Reliable: it changes a few times a phrase at most, and a lost "stopped"
	 * would leave a mic icon lit for the rest of the match.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetTransmitting(bool bTransmitting);

	/** Stops and tears down the playback stream for a speaker (they disconnected). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void StopSpeakerStream(int32 SpeakerPlayerId);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void StopAllSpeakerStreams();

	/** Debug: number of active playback streams on this machine. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	int32 GetActiveStreamCount() const;

protected:
	/** Per-speaker playback state that is not a UObject. */
	struct FSpeakerStream
	{
		/** Opus is stateful: every speaker gets a decoder of their own. */
		TUniquePtr<FOneBrahamVoiceCodec> Decoder;

		/** Channel the audio component is currently configured for. */
		uint8 ChannelByte = static_cast<uint8>(EOneBrahamVoiceChannel::None);

		/** Last time a frame arrived: drives both the duck and the idle teardown. */
		float LastFrameTime = 0.f;

		/** True while this stream holds one reference on the world-mix duck. */
		bool bHoldsDuck = false;

		/** Occlusion is traced at 10 Hz, not every frame. */
		float OcclusionTimer = 0.f;
		int32 BlockingSurfaces = 0;
		int32 FloorsApart = 0;

		/** Smoothed output so filtering glides instead of clicking. */
		float Gain = 0.f;
		float LowPassHz = OneBrahamVoiceRouting::ClearLowPassHz;
		bool bShapingPrimed = false;
	};

	// --- helpers ---------------------------------------------------------
	AOneBrahamCharacter* GetCharacter() const;
	AOneBrahamPlayerState* GetOwnerState() const;

	/** True for the chat host of the player on this machine. */
	bool IsLocalPlayerHost() const;

	/** One-time local setup (settings binding, permission prompt). */
	void EnsureLocalSetup();

	/** Re-reads mode, enable flag and device from the user settings. */
	void RefreshFromSettings();

	UFUNCTION()
	void HandleVoiceSettingsChanged(bool bEnabled);

	/** Shared receive path for both RPC hosts. */
	void HandleIncomingFrame(int32 SpeakerPlayerId, uint8 ChannelByte, const TArray<uint8>& Frame);

	/** Creates the audio component + wave for a speaker on a channel. */
	UAudioComponent* CreateSpeakerAudio(int32 SpeakerPlayerId, EOneBrahamVoiceChannel Channel);

	/** Tears down the audio objects of a stream, optionally keeping its decoder. */
	void DestroySpeakerAudio(int32 SpeakerPlayerId);

	/** Applies channel + distance + occlusion shaping to a live stream. */
	void UpdateStreamFiltering(int32 SpeakerPlayerId, float DeltaTime, bool bSnap);

	/** Where the local listener's ears are: the camera, which follows the
	 *  spectate target, so a spectator hears what the watched player hears. */
	bool GetListenerEarLocation(FVector& OutLocation) const;

	/** Locally owned capture path: pull, gate, encode, queue and send. */
	void TickLocalCapture(float DeltaTime);

	/** Encodes one raw frame onto the bounded outgoing queue. */
	void EnqueueOutgoingFrame(const TArray<uint8>& RawFrame);

	/** Sends queued frames at the 20 ms cadence. */
	void FlushOutgoingQueue(float DeltaTime);

	/** Tells the server (and the local HUD) whether voice is going out. */
	void SetSendingVoice(bool bSending);

	/** Server: relay an accepted frame to every listener who should hear it. */
	void RelayFrameToListeners(AOneBrahamPlayerState* SpeakerState, const TArray<uint8>& Frame);

	/** Plays the local radio key-up/key-down click. */
	void PlayRadioClick(bool bOpen) const;

	/** The local player's audio manager, for the duck and the radio click. */
	UOneBrahamAudioManagerComponent* GetLocalAudioManager() const;

	/** Attenuation for behaviour lines: a real falloff to the proximity range. */
	USoundAttenuation* GetVoiceAttenuation();

	/** Attenuation for live chat: spatialisation only - the gain is computed by
	 *  OneBrahamVoiceRouting::ShapeProximityVoice so it can include walls,
	 *  floors and crouching, and so the curve is the one the tests assert. */
	USoundAttenuation* GetChatAttenuation();

	/** Voice bus volume (Voice x Master) from the user's audio settings. */
	float GetBusVolumeForVoice() const;

	// --- capture / send state ---------------------------------------------
	FOneBrahamVoiceCapture Capture;

	/** Encoder for this player's own outgoing stream. */
	FOneBrahamVoiceCodec Codec;
	bool bEncoderInitialiseTried = false;

	/** Encoded frames waiting for their 20 ms slot. Bounded: on overflow the
	 *  oldest frame goes, because late voice is worse than a missing syllable. */
	TArray<TArray<uint8>> OutgoingQueue;

	/** Token bucket for the send cadence: refills at 50 frames/s. */
	float SendTokens = 0.f;

	/** Frames sent in the current second, to self-limit before the server must. */
	int32 FramesSentThisSecond = 0;
	float SendWindowStart = 0.f;

	/** Voice activation / open-mic gate. */
	FOneBrahamVoiceActivityDetector Gate;
	bool bGateWasOpen = false;

	/** The frame before the gate opened, sent first so the opening consonant
	 *  is not clipped. */
	TArray<uint8> PreRollFrame;

	bool bPushToTalkHeld = false;

	/** Keeps the key "held" for a moment after release: the device delivers the
	 *  last word a little after the finger comes up. */
	float PushToTalkTailRemaining = 0.f;

	/** True while voice is actually going out from this machine. */
	bool bSendingVoice = false;

	/** Local settings mirror. */
	bool bVoiceChatEnabled = true;
	EOneBrahamVoiceChatMode VoiceMode = EOneBrahamVoiceChatMode::PushToTalk;
	FString PreferredInputDevice;

	/** Microphone health, for the HUD. */
	bool bMicrophoneUnavailable = false;
	bool bMicrophonePermissionDenied = false;
	float CaptureRetryTime = 0.f;

	bool bLocalSetupDone = false;

	/** Line book-keeping for barks. */
	FName LastSpokenLineId = NAME_None;
	float LastSpokenTime = -1000.f;
	float ActiveLinePriority = 0.f;
	float ActiveLineEndTime = 0.f;

	// --- playback state ---------------------------------------------------
	/** Per-speaker playback streams. Strong refs so nothing is collected mid-sentence. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAudioComponent>> StreamComponents;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<USoundWaveProcedural>> StreamWaves;

	/** Decoders, channel, duck and shaping per speaker. */
	TMap<int32, FSpeakerStream> Streams;

	/** Speakers muted on this machine, applied before the server agrees. */
	TSet<int32> LocallyMutedSpeakers;

	/** The audio manager our duck references were pushed on, so they are popped
	 *  on the same one even if the controller changes. */
	TWeakObjectPtr<UOneBrahamAudioManagerComponent> DuckedAudioManager;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> VoiceAttenuation = nullptr;

	/** The range VoiceAttenuation was built for, so a match-settings range that
	 *  arrives after the first bark rebuilds it. */
	float VoiceAttenuationRangeCm = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> ChatAttenuation = nullptr;
};
