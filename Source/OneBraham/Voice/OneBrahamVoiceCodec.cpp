// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceCodec.h"

#include "OneBraham.h"
#include "VoiceModule.h"
#include "VoicePackage.h"
#include "Interfaces/VoiceInterface.h"

// ===========================================================================
//  FOneBrahamVoiceCodec
// ===========================================================================
FOneBrahamVoiceCodec::~FOneBrahamVoiceCodec()
{
	Shutdown();
}

bool FOneBrahamVoiceCodec::Initialise()
{
	if (IsUsingEngineCodec())
	{
		return true;
	}

	// The Voice module owns the platform codec. If it is not present (dedicated
	// server, or a platform without an Opus plugin) we stay on raw PCM - voice
	// must never be the reason a build cannot talk to itself.
	if (!FVoiceModule::IsAvailable())
	{
		UE_LOG(LogOneBrahamVoice, Log,
			TEXT("[OB.VOICE] Voice module unavailable - using uncompressed 16 kHz PCM frames."));
		return false;
	}

	FVoiceModule& VoiceModule = FVoiceModule::Get();

	// The module initialises the codec objects itself and returns null when that
	// fails, so a valid pointer here is a ready-to-use encoder/decoder.
	Encoder = VoiceModule.CreateVoiceEncoder(SampleRate, NumChannels, EAudioEncodeHint::VoiceEncode_Voice);
	Decoder = VoiceModule.CreateVoiceDecoder(SampleRate, NumChannels);

	// Log the frame cap relationship once at startup: 640 raw bytes is comfortably
	// inside the 1024 byte budget from the build configuration, and the assertion
	// exists so that changing the sample rate cannot silently overflow it.
	checkf(RawFrameBytes + 2 <= MaxTransportFrameBytes,
		TEXT("Voice frame (%d bytes + header) exceeds MAX_VOICE_FRAME_BYTES (%d)."),
		RawFrameBytes, MaxTransportFrameBytes);

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Codec ready - %s frames at %d Hz, %d ms."),
		IsUsingEngineCodec() ? TEXT("compressed") : TEXT("raw PCM"),
		SampleRate, 1000 * FrameSamples / SampleRate);

	return IsUsingEngineCodec();
}

void FOneBrahamVoiceCodec::Shutdown()
{
	if (Encoder.IsValid())
	{
		Encoder->Destroy();
		Encoder.Reset();
	}

	if (Decoder.IsValid())
	{
		Decoder->Destroy();
		Decoder.Reset();
	}
}

bool FOneBrahamVoiceCodec::EncodeFrame(const TArray<uint8>& RawPcm, TArray<uint8>& OutFrame)
{
	if (RawPcm.Num() == 0)
	{
		OutFrame.Reset();
		return false;
	}

	const bool bCompressed = IsUsingEngineCodec();

	OutFrame.Reset();
	OutFrame.Reserve(bCompressed ? (RawPcm.Num() / 8 + 2) : (RawPcm.Num() + 2));

	const uint8 CodecFlag = bCompressed ? 1 : 0;
	const uint8 LengthHint = static_cast<uint8>(FMath::Clamp(RawPcm.Num() / 4, 0, 255));

	OutFrame.Add(CodecFlag);
	OutFrame.Add(LengthHint);

	if (bCompressed)
	{
		// Opus frames are a fraction of the raw size; reserve the maximum possible
		// and let the encoder tell us the real length.
		TArray<uint8> Compressed;
		Compressed.SetNumZeroed(FMath::Min(MaxTransportFrameBytes - 2, RawPcm.Num() + 64));

		// In: buffer capacity. Out: bytes actually written. The return value is the
		// count of trailing raw bytes the codec could not fit into a whole slice.
		uint32 CompressedSize = static_cast<uint32>(Compressed.Num());
		const int32 UnencodedBytes = Encoder->Encode(RawPcm.GetData(), static_cast<uint32>(RawPcm.Num()),
			Compressed.GetData(), CompressedSize);
		const bool bOk = (UnencodedBytes == 0);

		if (bOk && CompressedSize > 0 && (int32)CompressedSize <= (MaxTransportFrameBytes - 2))
		{
			OutFrame.Append(Compressed.GetData(), CompressedSize);
			return true;
		}

		// Encoder failed for this frame: fall back to raw for THIS frame only, and
		// flag it so the receiver decodes it correctly.
		OutFrame[0] = 0;
		OutFrame.Append(RawPcm);
		return true;
	}

	OutFrame.Append(RawPcm);
	return OutFrame.Num() <= MaxTransportFrameBytes;
}

bool FOneBrahamVoiceCodec::DecodeFrame(const TArray<uint8>& Frame, TArray<uint8>& OutRawPcm)
{
	if (Frame.Num() < 2 || Frame.Num() > MaxTransportFrameBytes)
	{
		OutRawPcm.Reset();
		return false;
	}

	const uint8 CodecFlag = Frame[0];
	const uint8 LengthHint = Frame[1];
	const int32 PayloadBytes = Frame.Num() - 2;

	OutRawPcm.Reset();

	if (CodecFlag == 1 && IsUsingEngineCodec())
	{
		// Size the output for the announced length with headroom; the decoder
		// reports the real decoded size back through RawSize.
		const int32 MaxRawBytes = FMath::Max(LengthHint * 4, RawFrameBytes) * 2;
		OutRawPcm.SetNumUninitialized(MaxRawBytes);

		uint32 RawSize = static_cast<uint32>(MaxRawBytes);
		Decoder->Decode(Frame.GetData() + 2, static_cast<uint32>(PayloadBytes), OutRawPcm.GetData(), RawSize);

		if (RawSize > 0 && RawSize <= static_cast<uint32>(MaxRawBytes))
		{
			OutRawPcm.SetNum(static_cast<int32>(RawSize), EAllowShrinking::No);
			return true;
		}

		OutRawPcm.Reset();

		UE_LOG(LogOneBrahamVoice, Verbose, TEXT("[OB.VOICE] Decode failed for a compressed frame; dropping."));
		return false;
	}

	if (CodecFlag != 0)
	{
		// Either a codec flag we do not understand, or a compressed frame arriving
		// at a listener with no decoder. Dropping is the only safe option: playing
		// compressed bytes as PCM is a very loud noise.
		return false;
	}

	// Raw frame: sanity-check the announced length against reality.
	if (LengthHint != 0)
	{
		const int32 Expected = LengthHint * 4;
		if (FMath::Abs(PayloadBytes - Expected) > 8)
		{
			UE_LOG(LogOneBrahamVoice, Verbose,
				TEXT("[OB.VOICE] Raw frame length mismatch (%d bytes, hint %d)."), PayloadBytes, Expected);
			return false;
		}
	}

	OutRawPcm.Append(Frame.GetData() + 2, PayloadBytes);
	return OutRawPcm.Num() > 0;
}

// ===========================================================================
//  FOneBrahamVoiceCapture
// ===========================================================================
FOneBrahamVoiceCapture::~FOneBrahamVoiceCapture()
{
	Stop();
}

bool FOneBrahamVoiceCapture::Start(const FString& DeviceName)
{
	if (bCapturing)
	{
		return true;
	}

	if (!FVoiceModule::IsAvailable())
	{
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Cannot start capture: the Voice module is unavailable."));
		return false;
	}

	// The module initialises the capture object for the requested device/format
	// and returns null when that fails.
	Capture = FVoiceModule::Get().CreateVoiceCapture(DeviceName,
		FOneBrahamVoiceCodec::SampleRate,
		FOneBrahamVoiceCodec::NumChannels);

	if (!Capture.IsValid())
	{
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] No voice capture device could be created."));
		return false;
	}

	if (!Capture->Start())
	{
		uint32 Unused = 0;
		const EVoiceCaptureState::Type CaptureState = Capture->GetCaptureState(Unused);
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Starting capture failed (state %s). Check the microphone in Settings -> Audio."),
			EVoiceCaptureState::ToString(CaptureState));
		Capture->Shutdown();
		Capture.Reset();
		return false;
	}

	ActiveDeviceName = DeviceName.IsEmpty() ? TEXT("Default communications device") : DeviceName;
	PendingBytes.Reset();
	bCapturing = true;

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Capture started on '%s' (%d Hz mono)."),
		*ActiveDeviceName, FOneBrahamVoiceCodec::SampleRate);

	return true;
}

void FOneBrahamVoiceCapture::Stop()
{
	if (!bCapturing)
	{
		return;
	}

	if (Capture.IsValid())
	{
		Capture->Stop();
		Capture->Shutdown();
		Capture.Reset();
	}

	PendingBytes.Reset();
	bCapturing = false;
}

bool FOneBrahamVoiceCapture::PullFrames(TArray<TArray<uint8>>& OutFrames)
{
	OutFrames.Reset();

	if (!bCapturing || !Capture.IsValid())
	{
		return false;
	}

	// Size the scratch buffer to whatever the device has queued, then drain it.
	uint32 AvailableBytes = 0;
	EVoiceCaptureState::Type State = Capture->GetCaptureState(AvailableBytes);

	if (State == EVoiceCaptureState::Ok && AvailableBytes > 0)
	{
		if (PullScratch.Num() < static_cast<int32>(AvailableBytes))
		{
			PullScratch.SetNumUninitialized(static_cast<int32>(AvailableBytes));
		}

		State = Capture->GetVoiceData(PullScratch.GetData(), static_cast<uint32>(PullScratch.Num()), AvailableBytes);
	}

	if (State == EVoiceCaptureState::NotCapturing)
	{
		// The device went away (a headset was unplugged). Stop cleanly rather than
		// spamming Warnings every tick.
		UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Capture device stopped; voice chat disabled until re-enabled."));
		Stop();
		return false;
	}

	if (State != EVoiceCaptureState::Ok || AvailableBytes == 0)
	{
		return false;
	}

	PendingBytes.Append(PullScratch.GetData(), AvailableBytes);

	// Emit whole 20 ms frames, keeping any partial frame for the next pull.
	while (PendingBytes.Num() >= FOneBrahamVoiceCodec::RawFrameBytes)
	{
		TArray<uint8>& Frame = OutFrames.AddDefaulted_GetRef();
		Frame.Append(PendingBytes.GetData(), FOneBrahamVoiceCodec::RawFrameBytes);
		PendingBytes.RemoveAt(0, FOneBrahamVoiceCodec::RawFrameBytes, EAllowShrinking::No);
	}

	return OutFrames.Num() > 0;
}
