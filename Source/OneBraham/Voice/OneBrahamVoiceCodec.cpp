// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceCodec.h"

#include "OneBraham.h"
#include "VoiceModule.h"
#include "VoicePackage.h"
#include "Interfaces/VoiceInterface.h"
#include "Voice/OneBrahamMicrophonePermission.h"

// ===========================================================================
//  FOneBrahamVoiceCodec
// ===========================================================================
FOneBrahamVoiceCodec::~FOneBrahamVoiceCodec()
{
	Shutdown();
}

void FOneBrahamVoiceCodec::CheckFrameBudget()
{
	// 640 raw bytes is comfortably inside the 1024 byte budget from the build
	// configuration; the assertion exists so that changing the sample rate cannot
	// silently overflow it.
	checkf(RawFrameBytes + 2 <= MaxTransportFrameBytes,
		TEXT("Voice frame (%d bytes + header) exceeds MAX_VOICE_FRAME_BYTES (%d)."),
		RawFrameBytes, MaxTransportFrameBytes);
}

bool FOneBrahamVoiceCodec::Initialise()
{
	const bool bEncoder = InitialiseEncoder();
	const bool bDecoder = InitialiseDecoder();
	return bEncoder && bDecoder;
}

bool FOneBrahamVoiceCodec::InitialiseEncoder()
{
	if (Encoder.IsValid())
	{
		return true;
	}

	// The Voice module owns the platform codec. If it is not present (dedicated
	// server, [Voice] bEnabled=false, or a platform without Opus) we stay on raw
	// PCM - voice must never be the reason a build cannot talk to itself.
	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		UE_LOG(LogOneBrahamVoice, Log,
			TEXT("[OB.VOICE] Voice module unavailable or disabled ([Voice] bEnabled) - sending uncompressed 16 kHz PCM frames."));
		return false;
	}

	CheckFrameBudget();

	// The module initialises the codec object itself and returns null when that
	// fails, so a valid pointer here is a ready-to-use encoder.
	Encoder = FVoiceModule::Get().CreateVoiceEncoder(SampleRate, NumChannels, EAudioEncodeHint::VoiceEncode_Voice);

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Encoder ready - %s frames at %d Hz, %d ms."),
		Encoder.IsValid() ? TEXT("Opus") : TEXT("raw PCM"), SampleRate, 1000 * FrameSamples / SampleRate);

	return Encoder.IsValid();
}

bool FOneBrahamVoiceCodec::InitialiseDecoder()
{
	if (Decoder.IsValid())
	{
		return true;
	}

	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		return false;
	}

	CheckFrameBudget();
	Decoder = FVoiceModule::Get().CreateVoiceDecoder(SampleRate, NumChannels);

	return Decoder.IsValid();
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

	const bool bCompressed = Encoder.IsValid();

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

	if (CodecFlag == 1 && Decoder.IsValid())
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
	Shutdown();
}

bool FOneBrahamVoiceCapture::Start(const FString& DeviceName)
{
	if (bCapturing)
	{
		return true;
	}

	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Cannot start capture: the Voice module is unavailable or [Voice] bEnabled is false."));
		return false;
	}

	// Never touch the device without the OS's permission: on macOS that is either
	// a TCC termination (no usage string) or a capture of pure silence (denied).
	const EOneBrahamMicrophonePermission Permission = OneBrahamMicrophonePermission::GetStatus();
	if (!OneBrahamMicrophonePermission::IsUsable(Permission))
	{
		UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Cannot start capture: microphone permission is %s."),
			OneBrahamMicrophonePermission::ToString(Permission));
		return false;
	}

	// A different device asks for a new object; the same one is reused.
	if (Capture.IsValid() && CreatedForDevice != DeviceName)
	{
		Shutdown();
	}

	if (!Capture.IsValid())
	{
		// The module initialises the capture object for the requested device and
		// format, and returns null when that fails. Note that the macOS backend
		// always records from the system default input device, whatever is named.
		Capture = FVoiceModule::Get().CreateVoiceCapture(DeviceName,
			FOneBrahamVoiceCodec::SampleRate,
			FOneBrahamVoiceCodec::NumChannels);

		if (!Capture.IsValid())
		{
			UE_LOG(LogOneBrahamVoice, Warning,
				TEXT("[OB.VOICE] No voice capture device could be created."));
			return false;
		}

		CreatedForDevice = DeviceName;
	}

	if (!Capture->Start())
	{
		uint32 Unused = 0;
		const EVoiceCaptureState::Type CaptureState = Capture->GetCaptureState(Unused);
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Starting capture failed (state %s). Check the microphone in Settings -> Audio."),
			EVoiceCaptureState::ToString(CaptureState));
		Shutdown();
		return false;
	}

	ActiveDeviceName = DeviceName.IsEmpty() ? TEXT("Default communications device") : DeviceName;
	PendingBytes.Reset();
	bCapturing = true;

	UE_LOG(LogOneBrahamVoice, Verbose, TEXT("[OB.VOICE] Capture started on '%s' (%d Hz mono)."),
		*ActiveDeviceName, FOneBrahamVoiceCodec::SampleRate);

	return true;
}

void FOneBrahamVoiceCapture::Stop()
{
	if (!bCapturing)
	{
		return;
	}

	// The macOS backend asserts if Stop is called on a unit that is not running
	// (it can drop out of capturing on a device error), so ask first.
	if (Capture.IsValid() && Capture->IsCapturing())
	{
		Capture->Stop();
	}

	PendingBytes.Reset();
	bCapturing = false;
}

void FOneBrahamVoiceCapture::Shutdown()
{
	Stop();

	if (Capture.IsValid())
	{
		Capture->Shutdown();
		Capture.Reset();
	}

	CreatedForDevice.Reset();
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
		// The device went away (a headset was unplugged). Release the object so the
		// next attempt opens whatever the system default is now.
		UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Capture device stopped; will retry on the next transmission."));
		Shutdown();
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
