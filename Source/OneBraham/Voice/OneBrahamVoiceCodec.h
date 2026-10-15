// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVoiceCodec.h
// ---------------------------------------------------------------------------
// Thin wrapper around the engine's Voice module codec, with a raw-PCM path.
//
// WHY THIS EXISTS
//   UE's voice capture/encode is platform dependent: on Windows with the Opus
//   codec available we send compressed frames, but a dedicated server build or a
//   platform without the codec plugin would otherwise have no voice at all. The
//   wrapper always succeeds: if no encoder can be created, frames travel as
//   16 kHz mono PCM16 and the server's byte budget still bounds the cost.
//
// WIRE FORMAT (one relayed frame)
//   byte 0      : codec flag (0 = raw PCM16, 1 = engine codec)
//   byte 1      : frame length in samples / 4, as a coarse sanity check
//   bytes 2..N  : payload
//
//   20 ms frames at 16 kHz mono: 640 bytes raw, typically 40-60 bytes encoded.
//   MAX_VOICE_FRAME_BYTES (1024, from OneBraham.Build.cs) is the hard cap and is
//   asserted at startup.

#pragma once

#include "CoreMinimal.h"

class IVoiceCapture;
class IVoiceEncoder;
class IVoiceDecoder;

/**
 * Encode/decode helper. Owns the engine codec objects and is safe to use when
 * they could not be created.
 *
 * Opus is stateful in both directions: the decoder predicts each frame from the
 * ones before it. So one instance encodes ONE outgoing stream, and each speaker
 * a listener hears gets a decoder of their own - sharing one decoder between
 * talkers feeds it interleaved streams and produces warbling and clicks.
 */
class ONEBRAHAM_API FOneBrahamVoiceCodec
{
public:
	/** 16 kHz mono, as mandated by the project's voice configuration. */
	static constexpr int32 SampleRate = 16000;
	static constexpr int32 NumChannels = 1;

	/** 20 ms of audio at 16 kHz. */
	static constexpr int32 FrameSamples = SampleRate / 50;
	static constexpr int32 RawFrameBytes = FrameSamples * sizeof(int16);

	/** Hard cap from the build configuration. */
	static constexpr int32 MaxTransportFrameBytes = 1024;

	~FOneBrahamVoiceCodec();

	/**
	 * Creates the engine encoder and decoder when the Voice module provides them.
	 * Returns true when compression is available in both directions.
	 */
	bool Initialise();

	/** Encoder only: the sending side never needs to decode its own voice. */
	bool InitialiseEncoder();

	/** Decoder only: one per remote speaker on the listening side. */
	bool InitialiseDecoder();

	void Shutdown();

	bool IsUsingEngineCodec() const { return Encoder.IsValid() && Decoder.IsValid(); }
	bool HasEncoder() const { return Encoder.IsValid(); }
	bool HasDecoder() const { return Decoder.IsValid(); }

	/** Raw PCM16 -> transport frame. False only when the input is malformed. */
	bool EncodeFrame(const TArray<uint8>& RawPcm, TArray<uint8>& OutFrame);

	/** Transport frame -> raw PCM16. False when the frame is malformed/too big. */
	bool DecodeFrame(const TArray<uint8>& Frame, TArray<uint8>& OutRawPcm);

private:
	/** Checks the frame cap relationship once; shared by both initialisers. */
	static void CheckFrameBudget();

	TSharedPtr<IVoiceEncoder> Encoder;
	TSharedPtr<IVoiceDecoder> Decoder;
};

/**
 * Owns the platform microphone capture. One per local player.
 *
 * Capture is deliberately pull-based from Tick rather than callback-driven: at
 * 20 ms frames the cost is trivial and the game thread already has the exact
 * update cadence we want, which keeps the jitter buffer honest on a frame hitch.
 *
 * The device object is created once and then started and stopped: re-creating
 * a CoreAudio/WASAPI unit on every push-to-talk press costs a few milliseconds
 * on the game thread and clips the first syllable.
 */
class ONEBRAHAM_API FOneBrahamVoiceCapture
{
public:
	~FOneBrahamVoiceCapture();

	/**
	 * Starts recording, creating the device object on first use. Returns false
	 * when unavailable: no Voice module, no device, or the OS has not granted
	 * microphone access (see OneBrahamMicrophonePermission).
	 */
	bool Start(const FString& DeviceName = FString());

	/** Stops recording but keeps the device object for the next Start. */
	void Stop();

	/** Stops and releases the device object (device change, leaving the match). */
	void Shutdown();

	bool IsCapturing() const { return bCapturing; }

	/**
	 * Pulls whatever audio is ready. Returns true when at least one full 20 ms
	 * frame was produced into OutFrames (each entry is RawFrameBytes long).
	 */
	bool PullFrames(TArray<TArray<uint8>>& OutFrames);

	/** Device name in use, for the settings menu. */
	FString GetActiveDeviceName() const { return ActiveDeviceName; }

private:
	TSharedPtr<IVoiceCapture> Capture;
	TArray<uint8> PullScratch;
	TArray<uint8> PendingBytes;
	bool bCapturing = false;
	FString ActiveDeviceName;

	/** The device the object was created for; a different request recreates it. */
	FString CreatedForDevice;
};
