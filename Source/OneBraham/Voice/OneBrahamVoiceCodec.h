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
	 * Creates the engine encoder/decoder when the Voice module provides them.
	 * Returns true when compression is available.
	 */
	bool Initialise();

	void Shutdown();

	bool IsUsingEngineCodec() const { return Encoder.IsValid() && Decoder.IsValid(); }

	/** Raw PCM16 -> transport frame. False only when the input is malformed. */
	bool EncodeFrame(const TArray<uint8>& RawPcm, TArray<uint8>& OutFrame);

	/** Transport frame -> raw PCM16. False when the frame is malformed/too big. */
	bool DecodeFrame(const TArray<uint8>& Frame, TArray<uint8>& OutRawPcm);

private:
	TSharedPtr<IVoiceEncoder> Encoder;
	TSharedPtr<IVoiceDecoder> Decoder;
};

/**
 * Owns the platform microphone capture. One per local player.
 *
 * Capture is deliberately pull-based from Tick rather than callback-driven: at
 * 20 ms frames the cost is trivial and the game thread already has the exact
 * update cadence we want, which keeps the jitter buffer honest on a frame hitch.
 */
class ONEBRAHAM_API FOneBrahamVoiceCapture
{
public:
	~FOneBrahamVoiceCapture();

	/** Starts the default recording device. Returns false when unavailable. */
	bool Start(const FString& DeviceName = FString());

	void Stop();
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
};
