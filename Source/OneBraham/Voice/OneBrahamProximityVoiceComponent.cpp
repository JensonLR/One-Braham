// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamProximityVoiceComponent.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Voice/OneBrahamMicrophonePermission.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "Voice/OneBrahamVoiceTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "VoiceModule.h"

namespace
{
	/** 20 ms frames: the sweet spot between latency and packet overhead. */
	constexpr float VoiceFrameSeconds = 1.f / 50.f;

	/** A speaker stream with no frames for this long is torn down. */
	constexpr float StreamIdleTimeoutSeconds = 1.25f;

	/** The world mix stays ducked this long after a speaker's last frame, so the
	 *  beds come back within the same breath as the voice, not between words. */
	constexpr float DuckHoldSeconds = 0.45f;

	/** Barks fade in properly: a line starting on a hard cut sounds like a bug. */
	constexpr float BarkFadeInSeconds = 0.04f;

	/** Radio channels are band-passed to a bandwidth that reads as "over the radio". */
	constexpr float RadioLowPassHz = 3200.f;
	constexpr float RadioHighPassHz = 300.f;

	/** A talker is only allowed a little over the nominal cadence. Kept under the
	 *  server's relay budget (60/s) so the client never trips it on its own. */
	constexpr int32 MaxFramesPerSecondFromClient = 55;

	/** Up to four frames (80 ms) may go out in one tick after a hitch. */
	constexpr float MaxSendBurstFrames = 4.f;

	/** 200 ms of outgoing audio at most; beyond that the oldest frame is dropped. */
	constexpr int32 MaxOutgoingQueueFrames = 10;

	/** Never let a listener's buffer grow past 240 ms: latency creeps otherwise. */
	constexpr int32 MaxBufferedPlaybackFrames = 12;

	/** Push-to-talk keeps sending this long after the key comes up. */
	constexpr float PushToTalkTailSeconds = 0.15f;

	/** Occlusion traces per stream per second. */
	constexpr float OcclusionUpdateSeconds = 0.1f;

	/** Voice activation hang time and the open-mic equivalents. */
	constexpr float VoiceActivationHangSeconds = 0.4f;
	constexpr float OpenMicHangSeconds = 1.0f;
	constexpr float OpenMicGateFraction = 0.35f;

	/** QA loopback (registered in OneBraham.cpp): echo routed frames to the talker. */
	bool IsVoiceLoopbackEnabled()
	{
		static IConsoleVariable* LoopbackCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.VoiceLoopback"));
		return LoopbackCVar != nullptr && LoopbackCVar->GetInt() != 0;
	}

	/** The player state with this id, via the replicated PlayerArray: unlike the
	 *  controller iterator it holds every player on a client, not just the local one. */
	AOneBrahamPlayerState* FindPlayerStateById(const UWorld* World, int32 PlayerId)
	{
		const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
		if (GameState == nullptr)
		{
			return nullptr;
		}

		for (APlayerState* State : GameState->PlayerArray)
		{
			if (State != nullptr && State->GetPlayerId() == PlayerId)
			{
				return Cast<AOneBrahamPlayerState>(State);
			}
		}

		return nullptr;
	}

	/** The point a character's voice comes from. */
	FVector GetMouthLocationOf(const AOneBrahamCharacter* Character)
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (Mesh->DoesSocketExist(TEXT("Head")))
			{
				return Mesh->GetSocketLocation(TEXT("Head"));
			}
		}

		return Character->GetEyeLocation();
	}

	int32 FloorOf(float WorldZ)
	{
		return OneBrahamConstants::FloorIndexForZ(WorldZ);
	}

	/** Multiplies PCM16 in place, clamping rather than wrapping on overload. */
	void ApplyGain(TArray<uint8>& RawPcm, float Gain)
	{
		if (FMath::IsNearlyEqual(Gain, 1.f))
		{
			return;
		}

		int16* Samples = reinterpret_cast<int16*>(RawPcm.GetData());
		const int32 NumSamples = RawPcm.Num() / static_cast<int32>(sizeof(int16));

		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Samples[Index] = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Samples[Index] * Gain), -32768, 32767));
		}
	}
}

UOneBrahamProximityVoiceComponent::UOneBrahamProximityVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Per frame: capture must keep the 20 ms cadence honest and filtering has to
	// glide with the listener's ear. Copies that have nothing to do switch their
	// tick off in BeginPlay.
	PrimaryComponentTick.TickInterval = 0.f;
	SetIsReplicatedByDefault(true);

	bVoiceChatEnabled = true;
}

void UOneBrahamProximityVoiceComponent::BeginPlay()
{
	Super::BeginPlay();

	// A character's copy only multicasts barks, which need no tick. A dedicated
	// server's chat hosts only relay, which happens inside the RPC. Neither needs
	// to tick; a received frame re-enables the tick of the copy that plays it.
	if (!IsChatHost() || IsRunningDedicatedServer())
	{
		SetComponentTickEnabled(false);
	}

	// The encoder and the decoders are created lazily, where audio is actually
	// processed: every machine holds a chat host per player, and allocating Opus
	// state for eleven players who will never send through this copy is waste.

	UE_LOG(LogOneBrahamVoice, Verbose,
		TEXT("[OB.VOICE] Voice component ready on '%s' (%s)."),
		GetOwner() != nullptr ? *GetOwner()->GetName() : TEXT("<none>"),
		IsChatHost() ? TEXT("chat host") : TEXT("barks"));
}

void UOneBrahamProximityVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Leaving the match: release the microphone first so we do not keep a device
	// open across a level travel, then tear down every stream (and its duck).
	if (bLocalSetupDone)
	{
		if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
		{
			Settings->OnVoiceSettingsChanged.RemoveDynamic(this, &UOneBrahamProximityVoiceComponent::HandleVoiceSettingsChanged);
		}
	}

	bPushToTalkHeld = false;
	bSendingVoice = false;
	OutgoingQueue.Reset();
	Capture.Shutdown();
	StopAllSpeakerStreams();

	Super::EndPlay(EndPlayReason);
}

void UOneBrahamProximityVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (World == nullptr || !IsChatHost())
	{
		return;
	}

	// --- Local capture ----------------------------------------------------
	EnsureLocalSetup();

	if (bLocalSetupDone)
	{
		TickLocalCapture(DeltaTime);
	}

	// --- Playback streams -------------------------------------------------
	// Filtering tracks the listener's ear, so it updates per frame, not per frame
	// of voice data: a voice behind a closing lift door should glide, not click.
	const float Now = World->GetTimeSeconds();
	TArray<int32> StaleStreams;

	for (TPair<int32, FSpeakerStream>& Pair : Streams)
	{
		const int32 SpeakerId = Pair.Key;
		FSpeakerStream& Stream = Pair.Value;
		const float SinceLastFrame = Now - Stream.LastFrameTime;

		if (SinceLastFrame > StreamIdleTimeoutSeconds)
		{
			StaleStreams.Add(SpeakerId);
			continue;
		}

		UpdateStreamFiltering(SpeakerId, DeltaTime, false);

		// One duck reference per live transmission, held while frames keep
		// arriving and released DuckHoldSeconds after the last one. This replaces
		// a timer per received frame (fifty timers a second per talker) with one
		// comparison per stream per tick.
		const bool bShouldDuck = SinceLastFrame <= DuckHoldSeconds;
		if (bShouldDuck && !Stream.bHoldsDuck)
		{
			if (UOneBrahamAudioManagerComponent* Audio = GetLocalAudioManager())
			{
				Audio->PushVoiceDuck();
				DuckedAudioManager = Audio;
				Stream.bHoldsDuck = true;
			}
		}
		else if (!bShouldDuck && Stream.bHoldsDuck)
		{
			if (UOneBrahamAudioManagerComponent* Audio = DuckedAudioManager.Get())
			{
				Audio->PopVoiceDuck();
			}
			Stream.bHoldsDuck = false;
		}
	}

	for (const int32 SpeakerId : StaleStreams)
	{
		StopSpeakerStream(SpeakerId);
	}
}

// ==========================================================================
//  Behaviour lines
// ==========================================================================
bool UOneBrahamProximityVoiceComponent::ServerSpeakLine(FName LineId, float Priority)
{
	UWorld* World = GetWorld();
	AOneBrahamCharacter* Character = GetCharacter();
	if (World == nullptr || Character == nullptr || LineId.IsNone())
	{
		return false;
	}

	if (!Character->HasAuthority())
	{
		// Client call: the subsystem already refuses these, but a defensive guard
		// here keeps a stray Blueprint call from desyncing the barks.
		return false;
	}

	const UOneBrahamVoiceSubsystem* VoiceSubsystem = UOneBrahamVoiceSubsystem::Get(GetOwner());
	const UOneBrahamVoiceLineSet* Lines = VoiceSubsystem != nullptr ? VoiceSubsystem->GetLineSet() : nullptr;

	float Duration = 2.5f;

	if (Lines != nullptr)
	{
		TArray<const FOneBrahamVoiceLine*> Candidates;
		Lines->FindLines(LineId, Candidates);

		if (Candidates.Num() == 0)
		{
			OB_VOICE_LOG("Character '%s' asked for unknown line '%s'.", *Character->GetName(), *LineId.ToString());
			return false;
		}

		const FOneBrahamVoiceLine* Line = Candidates[0];
		Duration = Line->SubtitleSeconds > 0.f ? Line->SubtitleSeconds : 2.5f;

		if (USoundBase* Sound = Line->Sound.LoadSynchronous())
		{
			// The recorded length beats the authored guess, so subtitles and the
			// microphone indicator match what the player actually hears.
			Duration = FMath::Max(0.75f, Sound->GetDuration());
		}
	}

	LastSpokenLineId = LineId;
	LastSpokenTime = World->GetTimeSeconds();
	ActiveLinePriority = Priority;
	ActiveLineEndTime = LastSpokenTime + Duration;

	// Unreliable multicast: a dropped bark is a lost flavour line, never a broken
	// mechanic, and behaviour lines must never cost bandwidth under load.
	MulticastSpeakLine(LineId, Priority, Duration);

	return true;
}

void UOneBrahamProximityVoiceComponent::MulticastSpeakLine_Implementation(FName LineId, float Priority,
	float DurationSeconds)
{
	UWorld* World = GetWorld();
	AOneBrahamCharacter* Character = GetCharacter();
	if (World == nullptr || Character == nullptr)
	{
		return;
	}

	const UOneBrahamVoiceSubsystem* VoiceSubsystem = UOneBrahamVoiceSubsystem::Get(GetOwner());
	const UOneBrahamVoiceLineSet* Lines = VoiceSubsystem != nullptr ? VoiceSubsystem->GetLineSet() : nullptr;

	// Defensive local cooldown: a modified client cannot turn barks into a stream
	// of noise for everyone else, because every listener enforces its own budget.
	const float Now = World->GetTimeSeconds();
	if ((Now - LastSpokenTime) < 1.5f && LastSpokenLineId == LineId)
	{
		return;
	}

	LastSpokenLineId = LineId;
	LastSpokenTime = Now;

	if (Lines == nullptr)
	{
		return;
	}

	TArray<const FOneBrahamVoiceLine*> Candidates;
	Lines->FindLines(LineId, Candidates);

	if (Candidates.Num() == 0)
	{
		return;
	}

	// Variant selection must be identical on every machine without replicating
	// which take was chosen, so it is derived from the line id: a stable hash
	// seeded stream picks the same variant everywhere, every time.
	FRandomStream VariantStream(static_cast<int32>(GetTypeHash(LineId)));
	const FOneBrahamVoiceLine* Line = Lines->PickLine(LineId, Character->Appearance.ArchetypeTag, VariantStream);

	if (Line == nullptr)
	{
		return;
	}

	// --- Audio ------------------------------------------------------------
	const float ProximityRange = VoiceSubsystem != nullptr ? VoiceSubsystem->GetProximityRangeCm() : 3500.f;

	if (USoundBase* LineSound = Line->Sound.LoadSynchronous())
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			// Attached to the head socket: a bark has to come from the character,
			// not from the floor, or it tells you the wrong thing about where
			// somebody is standing.
			UAudioComponent* Component = UGameplayStatics::SpawnSoundAttached(LineSound, Mesh,
				TEXT("Head"), FVector::ZeroVector, EAttachLocation::SnapToTarget,
				/*bStopWhenAttachedToDestroyed*/ true, 1.f, FMath::FRandRange(0.97f, 1.03f), 0.f,
				GetVoiceAttenuation(), nullptr, false);

			if (Component != nullptr)
			{
				Component->FadeIn(BarkFadeInSeconds, 0.05f, 1.f, EAudioFaderCurve::Linear);
			}
		}
	}

	// --- Subtitle ---------------------------------------------------------
	// Subtitles only within a generous version of the proximity range: a player
	// should never read a line they could not have heard.
	AOneBrahamPlayerState* SpeakerState = GetOwnerState();
	const AOneBrahamPlayerController* LocalPC = World->GetFirstPlayerController<AOneBrahamPlayerController>();
	const AOneBrahamCharacter* LocalCharacter = LocalPC != nullptr
		? Cast<AOneBrahamCharacter>(LocalPC->GetPawn())
		: nullptr;

	const bool bWithinSubtitleRange = LocalCharacter == nullptr
		|| FVector::DistSquared(LocalCharacter->GetActorLocation(), Character->GetActorLocation())
			<= FMath::Square(ProximityRange * 1.35f);

	const float SubtitleSeconds = DurationSeconds > 0.f ? DurationSeconds : 2.5f;

	if (bWithinSubtitleRange && VoiceSubsystem != nullptr)
	{
		// const_cast: the subsystem broadcast is non-const by design (it fires
		// Blueprint events on the HUD).
		UOneBrahamVoiceSubsystem* MutableVoice = const_cast<UOneBrahamVoiceSubsystem*>(VoiceSubsystem);
		MutableVoice->NotifyLineSpoken(SpeakerState, Character, LineId, Line->Subtitle, SubtitleSeconds);
	}
}

// ==========================================================================
//  Hosts
// ==========================================================================
bool UOneBrahamProximityVoiceComponent::IsChatHost() const
{
	return Cast<APlayerState>(GetOwner()) != nullptr;
}

UOneBrahamProximityVoiceComponent* UOneBrahamProximityVoiceComponent::GetChatHost() const
{
	if (IsChatHost())
	{
		return const_cast<UOneBrahamProximityVoiceComponent*>(this);
	}

	if (const AOneBrahamPlayerState* State = GetOwnerState())
	{
		return State->GetVoiceComponent();
	}

	return nullptr;
}

bool UOneBrahamProximityVoiceComponent::IsLocalPlayerHost() const
{
	if (!IsChatHost())
	{
		return false;
	}

	// APlayerState's owner is its controller; on a client only the local player's
	// own state has one, which is exactly the test we want.
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	const APlayerController* PC = State != nullptr ? State->GetPlayerController() : nullptr;
	return PC != nullptr && PC->IsLocalController();
}

void UOneBrahamProximityVoiceComponent::EnsureLocalSetup()
{
	if (bLocalSetupDone || !IsLocalPlayerHost())
	{
		return;
	}

	bLocalSetupDone = true;

	// Settings drive the mode, the device and the enable flag live: changing them
	// in the menu mid-match takes effect without a rejoin.
	if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Settings->OnVoiceSettingsChanged.AddUniqueDynamic(this, &UOneBrahamProximityVoiceComponent::HandleVoiceSettingsChanged);
	}

	RefreshFromSettings();

	// Ask for microphone access when the match starts rather than on the first
	// push-to-talk: a system dialog in the middle of a firefight steals focus at
	// the worst possible moment.
	if (bVoiceChatEnabled && VoiceMode != EOneBrahamVoiceChatMode::Muted)
	{
		TWeakObjectPtr<UOneBrahamProximityVoiceComponent> WeakThis(this);
		OneBrahamMicrophonePermission::Request([WeakThis](EOneBrahamMicrophonePermission Result)
		{
			if (UOneBrahamProximityVoiceComponent* Strong = WeakThis.Get())
			{
				const bool bUsable = OneBrahamMicrophonePermission::IsUsable(Result);
				Strong->bMicrophonePermissionDenied = !bUsable;
				Strong->bMicrophoneUnavailable = !bUsable;
				Strong->CaptureRetryTime = 0.f;
			}
		});
	}
}

void UOneBrahamProximityVoiceComponent::RefreshFromSettings()
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings == nullptr)
	{
		return;
	}

	const FString NewDevice = Settings->GetVoiceInputDeviceId();
	if (NewDevice != PreferredInputDevice)
	{
		// A different microphone: release the old device object so the next
		// transmission opens the new one.
		PreferredInputDevice = NewDevice;
		Capture.Shutdown();
		CaptureRetryTime = 0.f;
	}

	VoiceMode = Settings->VoiceChatMode;
	SetVoiceChatEnabled(Settings->bVoiceChatEnabled && VoiceMode != EOneBrahamVoiceChatMode::Muted);
	Gate.Reset();
}

void UOneBrahamProximityVoiceComponent::HandleVoiceSettingsChanged(bool bEnabled)
{
	RefreshFromSettings();
	SetVoiceChatEnabled(bEnabled);
}

// ==========================================================================
//  Local capture
// ==========================================================================
void UOneBrahamProximityVoiceComponent::SetLocalTransmitting(bool bTransmitting)
{
	UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	if (Host == nullptr)
	{
		return;
	}

	if (Host != this)
	{
		Host->SetLocalTransmitting(bTransmitting);
		return;
	}

	if (bTransmitting == bPushToTalkHeld || !IsLocalPlayerHost())
	{
		return;
	}

	bPushToTalkHeld = bTransmitting;

	if (!bTransmitting)
	{
		PushToTalkTailRemaining = PushToTalkTailSeconds;
	}

	// Local radio click: the transmitting player hears their own key-up, which is
	// the audio equivalent of the HUD indicator. Not when the key can do nothing.
	const AOneBrahamPlayerState* State = GetOwnerState();
	const bool bCanTransmit = bVoiceChatEnabled
		&& State != nullptr
		&& !State->bSelfMuted
		&& State->VoiceChannel != EOneBrahamVoiceChannel::None;

	if (bCanTransmit)
	{
		PlayRadioClick(bTransmitting);
	}
}

bool UOneBrahamProximityVoiceComponent::IsLocalTransmitting() const
{
	const UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	return Host != nullptr && Host->bSendingVoice;
}

bool UOneBrahamProximityVoiceComponent::HasWorkingMicrophone() const
{
	const UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	if (Host == nullptr)
	{
		return false;
	}

	if (!FVoiceModule::IsAvailable() || !FVoiceModule::Get().IsVoiceEnabled())
	{
		return false;
	}

	return !Host->bMicrophoneUnavailable && !Host->bMicrophonePermissionDenied;
}

bool UOneBrahamProximityVoiceComponent::IsMicrophonePermissionDenied() const
{
	const UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	return Host != nullptr && Host->bMicrophonePermissionDenied;
}

bool UOneBrahamProximityVoiceComponent::IsVoiceChatEnabled() const
{
	const UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	return Host != nullptr ? Host->bVoiceChatEnabled : bVoiceChatEnabled;
}

FString UOneBrahamProximityVoiceComponent::GetCaptureDeviceName() const
{
	const UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	return Host != nullptr ? Host->Capture.GetActiveDeviceName() : FString();
}

int32 UOneBrahamProximityVoiceComponent::GetActiveStreamCount() const
{
	const UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	return Host != nullptr ? Host->StreamComponents.Num() : 0;
}

void UOneBrahamProximityVoiceComponent::SetVoiceChatEnabled(bool bEnabled)
{
	UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	if (Host != nullptr && Host != this)
	{
		Host->SetVoiceChatEnabled(bEnabled);
		return;
	}

	if (bVoiceChatEnabled == bEnabled)
	{
		return;
	}

	bVoiceChatEnabled = bEnabled;

	if (!bEnabled)
	{
		Capture.Stop();
		OutgoingQueue.Reset();
		bPushToTalkHeld = false;
		PushToTalkTailRemaining = 0.f;
		SetSendingVoice(false);
		StopAllSpeakerStreams();
	}

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Voice chat %s for '%s'."),
		bEnabled ? TEXT("enabled") : TEXT("disabled"),
		GetOwner() != nullptr ? *GetOwner()->GetName() : TEXT("<none>"));
}

void UOneBrahamProximityVoiceComponent::SetSpeakerMutedLocally(int32 SpeakerPlayerId, bool bMuted)
{
	UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	if (Host != nullptr && Host != this)
	{
		Host->SetSpeakerMutedLocally(SpeakerPlayerId, bMuted);
		return;
	}

	if (bMuted)
	{
		LocallyMutedSpeakers.Add(SpeakerPlayerId);
		StopSpeakerStream(SpeakerPlayerId);
	}
	else
	{
		LocallyMutedSpeakers.Remove(SpeakerPlayerId);
	}
}

void UOneBrahamProximityVoiceComponent::TickLocalCapture(float DeltaTime)
{
	UWorld* World = GetWorld();
	const AOneBrahamPlayerState* State = GetOwnerState();
	if (World == nullptr || State == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	PushToTalkTailRemaining = FMath::Max(0.f, PushToTalkTailRemaining - DeltaTime);
	const bool bPushToTalkActive = bPushToTalkHeld || PushToTalkTailRemaining > 0.f;

	// Self-mute and the Off channel close the microphone itself, not just the
	// packets: a muted player should see the OS microphone indicator go out.
	const bool bChannelOpen = !State->bSelfMuted && State->VoiceChannel != EOneBrahamVoiceChannel::None;
	const bool bModeCaptures = VoiceMode == EOneBrahamVoiceChatMode::OpenMic
		|| VoiceMode == EOneBrahamVoiceChatMode::VoiceActivation;

	const bool bWantCapture = bVoiceChatEnabled
		&& VoiceMode != EOneBrahamVoiceChatMode::Muted
		&& bChannelOpen
		&& (bModeCaptures || bPushToTalkActive);

	if (!bWantCapture)
	{
		if (Capture.IsCapturing())
		{
			Capture.Stop();
		}

		Gate.Reset();
		bGateWasOpen = false;
		PreRollFrame.Reset();
		OutgoingQueue.Reset();
		SetSendingVoice(false);
		return;
	}

	// --- Open the device -----------------------------------------------------
	if (!Capture.IsCapturing())
	{
		if (Now < CaptureRetryTime)
		{
			SetSendingVoice(false);
			return;
		}

		const EOneBrahamMicrophonePermission Permission = OneBrahamMicrophonePermission::GetStatus();

		if (Permission == EOneBrahamMicrophonePermission::NotDetermined)
		{
			// The prompt is (or is about to be) on screen; the answer resets the
			// retry clock through the callback.
			TWeakObjectPtr<UOneBrahamProximityVoiceComponent> WeakThis(this);
			OneBrahamMicrophonePermission::Request([WeakThis](EOneBrahamMicrophonePermission Result)
			{
				if (UOneBrahamProximityVoiceComponent* Strong = WeakThis.Get())
				{
					Strong->bMicrophonePermissionDenied = !OneBrahamMicrophonePermission::IsUsable(Result);
					Strong->CaptureRetryTime = 0.f;
				}
			});

			CaptureRetryTime = Now + 1.f;
			return;
		}

		if (!OneBrahamMicrophonePermission::IsUsable(Permission))
		{
			// Denied: never open the device (it would record silence and light the
			// HUD as if we were talking). Re-checked every few seconds, so granting
			// access in System Settings recovers without a rejoin where macOS allows it.
			if (!bMicrophonePermissionDenied)
			{
				UE_LOG(LogOneBrahamVoice, Warning,
					TEXT("[OB.VOICE] Microphone permission is %s - voice chat is listen-only. Enable it in System Settings -> Privacy & Security -> Microphone."),
					OneBrahamMicrophonePermission::ToString(Permission));
			}

			bMicrophonePermissionDenied = true;
			bMicrophoneUnavailable = true;
			CaptureRetryTime = Now + 5.f;
			SetSendingVoice(false);
			return;
		}

		bMicrophonePermissionDenied = false;

		if (!Capture.Start(PreferredInputDevice))
		{
			// No device (headset unplugged, none fitted). Retry slowly rather than
			// every tick: device enumeration is not free.
			bMicrophoneUnavailable = true;
			CaptureRetryTime = Now + 3.f;
			SetSendingVoice(false);
			return;
		}

		bMicrophoneUnavailable = false;
		SendTokens = 1.f;
		FramesSentThisSecond = 0;
		SendWindowStart = Now;
	}

	// --- Pull and gate -------------------------------------------------------
	TArray<TArray<uint8>> RawFrames;
	Capture.PullFrames(RawFrames);

	const float InputGain = OneBrahamVoiceCVars::GetInputGain();
	const float NoiseGate = OneBrahamVoiceCVars::GetNoiseGate();

	if (VoiceMode == EOneBrahamVoiceChatMode::OpenMic)
	{
		// Open mic transmits whenever there is anything above the room's own
		// noise: a third of the activation threshold, with a long hang so it
		// never chops a sentence, but pure silence and hiss still go nowhere.
		Gate.OpenThreshold = NoiseGate * OpenMicGateFraction;
		Gate.HangSeconds = OpenMicHangSeconds;
	}
	else
	{
		Gate.OpenThreshold = NoiseGate;
		Gate.HangSeconds = VoiceActivationHangSeconds;
	}

	for (TArray<uint8>& RawFrame : RawFrames)
	{
		ApplyGain(RawFrame, InputGain);

		const float Rms = FOneBrahamVoiceActivityDetector::ComputeRms(
			reinterpret_cast<const int16*>(RawFrame.GetData()), RawFrame.Num() / static_cast<int32>(sizeof(int16)));

		// The Settings meter: rise instantly, fall at a steady rate.
		InputMeter = FMath::Max(FOneBrahamVoiceActivityDetector::MeterFraction(Rms),
			InputMeter - VoiceFrameSeconds / 1.5f);

		// The gate always runs, so its noise floor keeps learning the room while
		// push-to-talk is in use.
		const bool bGateOpen = Gate.ProcessFrame(Rms, VoiceFrameSeconds);
		const bool bSend = bPushToTalkActive || (bModeCaptures && bGateOpen);

		if (bSend)
		{
			if (!bGateWasOpen && PreRollFrame.Num() > 0 && !bPushToTalkActive)
			{
				EnqueueOutgoingFrame(PreRollFrame);
			}

			EnqueueOutgoingFrame(RawFrame);
			PreRollFrame.Reset();
		}
		else
		{
			PreRollFrame = MoveTemp(RawFrame);
		}

		bGateWasOpen = bSend;
	}

	// Speaking state: the key when it is held, otherwise the gate. Evaluated even
	// on ticks with no new frames, so a held key lights the icon immediately.
	SetSendingVoice(bPushToTalkActive || (bModeCaptures && Gate.bOpen));

	FlushOutgoingQueue(DeltaTime);
}

void UOneBrahamProximityVoiceComponent::EnqueueOutgoingFrame(const TArray<uint8>& RawFrame)
{
	if (!bEncoderInitialiseTried)
	{
		bEncoderInitialiseTried = true;
		Codec.InitialiseEncoder();
	}

	TArray<uint8> Encoded;
	if (!Codec.EncodeFrame(RawFrame, Encoded))
	{
		return;
	}

	if (Encoded.Num() > ONE_BRAHAM_MAX_VOICE_FRAME_BYTES)
	{
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Dropping an oversized outgoing frame (%d bytes > %d)."),
			Encoded.Num(), ONE_BRAHAM_MAX_VOICE_FRAME_BYTES);
		return;
	}

	// Bounded: a long hitch must not turn into a two-second backlog the listener
	// then hears late. Dropping the oldest keeps the voice current.
	if (OutgoingQueue.Num() >= MaxOutgoingQueueFrames)
	{
		OutgoingQueue.RemoveAt(0, 1, EAllowShrinking::No);
	}

	OutgoingQueue.Add(MoveTemp(Encoded));
}

void UOneBrahamProximityVoiceComponent::FlushOutgoingQueue(float DeltaTime)
{
	UWorld* World = GetWorld();
	const AOneBrahamPlayerState* State = GetOwnerState();
	if (World == nullptr || State == nullptr || OutgoingQueue.Num() == 0)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - SendWindowStart >= 1.f)
	{
		SendWindowStart = Now;
		FramesSentThisSecond = 0;
	}

	// Token bucket at the device's own 50 frames a second, with a small burst
	// allowance: a frame hitch flushes over a couple of ticks instead of arriving
	// at the relay as thirty frames at once (which its budget would drop).
	SendTokens = FMath::Min(SendTokens + DeltaTime / VoiceFrameSeconds, MaxSendBurstFrames);

	// Advisory only - the server routes on its own copy of the channel.
	const uint8 ChannelByte = static_cast<uint8>(State->VoiceChannel);

	while (OutgoingQueue.Num() > 0 && SendTokens >= 1.f && FramesSentThisSecond < MaxFramesPerSecondFromClient)
	{
		ServerPushVoiceFrame(OutgoingQueue[0], ChannelByte);
		OutgoingQueue.RemoveAt(0, 1, EAllowShrinking::No);
		SendTokens -= 1.f;
		++FramesSentThisSecond;
	}

	// Anything left really does stay queued for the next tick, bounded above.
}

void UOneBrahamProximityVoiceComponent::SetSendingVoice(bool bSending)
{
	if (bSending == bSendingVoice)
	{
		return;
	}

	bSendingVoice = bSending;

	// The squad panel's mic icon is the replicated PlayerState flag, which only
	// the server can set: tell it. On a listen server this runs locally.
	if (IsLocalPlayerHost())
	{
		ServerSetTransmitting(bSending);
	}
}

void UOneBrahamProximityVoiceComponent::ServerSetTransmitting_Implementation(bool bTransmitting)
{
	AOneBrahamPlayerState* State = GetOwnerState();
	if (State == nullptr)
	{
		return;
	}

	// The same conditions the relay applies: a muted or channel-off player is
	// never shown as talking, whatever the client claims.
	const bool bAllowed = bTransmitting
		&& !State->bSelfMuted
		&& State->VoiceChannel != EOneBrahamVoiceChannel::None;

	if (State->bIsTransmitting != bAllowed)
	{
		State->bIsTransmitting = bAllowed;
		State->ForceNetUpdate();
	}
}

// ==========================================================================
//  Relay
// ==========================================================================
void UOneBrahamProximityVoiceComponent::ServerPushVoiceFrame_Implementation(const TArray<uint8>& Frame,
	uint8 ChannelByte)
{
	AOneBrahamPlayerState* SpeakerState = GetOwnerState();
	if (GetWorld() == nullptr || SpeakerState == nullptr)
	{
		return;
	}

	// --- Authority and validity -------------------------------------------
	// 1. On the chat host the RPC can only have come from the connection that
	//    owns this PlayerState - the engine guarantees that. The legacy path on a
	//    character additionally checks that the pawn belongs to that player, so a
	//    puppeteered or possessed body cannot talk for somebody else.
	if (!IsChatHost())
	{
		const AOneBrahamCharacter* Character = GetCharacter();
		if (Character == nullptr || SpeakerState->GetOwningController() == nullptr
			|| Character->GetController() != SpeakerState->GetOwningController())
		{
			UE_LOG(LogOneBrahamVoice, Warning,
				TEXT("[OB.VOICE] Voice frame from '%s' rejected: pawn ownership mismatch."),
				*SpeakerState->GetPlayerName());
			return;
		}
	}

	// 2. Size sanity, before anything touches the buffer.
	if (Frame.Num() == 0 || Frame.Num() > ONE_BRAHAM_MAX_VOICE_FRAME_BYTES)
	{
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Voice frame from '%s' rejected: %d bytes (limit %d)."),
			*SpeakerState->GetPlayerName(), Frame.Num(), ONE_BRAHAM_MAX_VOICE_FRAME_BYTES);
		return;
	}

	// 3. The client's channel byte is advisory only: the server's replicated
	//    PlayerState value is the truth, so a modified client cannot broadcast to
	//    the whole team while pretending to whisper. Elimination is not a reason
	//    to drop here any more: the routing rule moves the dead to squad radio.
	if (SpeakerState->VoiceChannel == EOneBrahamVoiceChannel::None || SpeakerState->bSelfMuted)
	{
		return;
	}

	// 4. Relay budget: this is what stops one modified client from saturating the
	//    server's outbound bandwidth for everyone.
	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
	{
		if (!Voice->ConsumeRelayBudget(SpeakerState, Frame.Num()))
		{
			// Budget exceeded: drop the frame. The subsystem counts every drop and
			// the audit log reports them at match end - that count is the signal
			// for a modified client trying to flood the relay.
			return;
		}
	}

	RelayFrameToListeners(SpeakerState, Frame);
}

void UOneBrahamProximityVoiceComponent::RelayFrameToListeners(AOneBrahamPlayerState* SpeakerState,
	const TArray<uint8>& Frame)
{
	UWorld* World = GetWorld();
	UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;

	if (Voice == nullptr || GameState == nullptr || SpeakerState == nullptr)
	{
		return;
	}

	const int32 SpeakerId = SpeakerState->GetPlayerId();
	const FOneBrahamVoiceRouteRules Rules = Voice->GetRouteRules();
	const FOneBrahamVoiceRouteParty Speaker = Voice->MakeRouteParty(SpeakerState, /*bAsListener*/ false);
	const bool bLoopback = IsVoiceLoopbackEnabled();

	// Every player state, not every controller: the routing decision is about
	// players, and a player with no pawn (dead, spectating, between respawns) is
	// still a listener. Delivery goes through the listener's own PlayerState,
	// which their connection owns whether or not they have a body.
	for (APlayerState* BaseState : GameState->PlayerArray)
	{
		AOneBrahamPlayerState* ListenerState = Cast<AOneBrahamPlayerState>(BaseState);
		if (ListenerState == nullptr || ListenerState->IsABot() || ListenerState->GetPlayerController() == nullptr)
		{
			continue;
		}

		UOneBrahamProximityVoiceComponent* Target = ListenerState->GetVoiceComponent();
		if (Target == nullptr)
		{
			continue;
		}

		if (ListenerState == SpeakerState)
		{
			// QA loopback (ob.VoiceLoopback 1): hear yourself through the full
			// encode -> relay -> decode -> playback path without a second machine.
			if (bLoopback)
			{
				Target->ClientReceiveVoiceFrame(SpeakerId,
					static_cast<uint8>(OneBrahamVoiceRouting::ResolveSpeakerChannel(Speaker)), Frame);
			}
			continue;
		}

		const FOneBrahamVoiceRouteParty Listener = Voice->MakeRouteParty(ListenerState, /*bAsListener*/ true);

		// Policy lives in one place: proximity radius, squad, team, mutes, death.
		EOneBrahamVoiceChannel Channel = EOneBrahamVoiceChannel::None;
		if (!OneBrahamVoiceRouting::ResolveRoute(Listener, Speaker, Rules, Channel))
		{
			continue;
		}

		Target->ClientReceiveVoiceFrame(SpeakerId, static_cast<uint8>(Channel), Frame);
	}
}

void UOneBrahamProximityVoiceComponent::ClientReceiveVoiceFrame_Implementation(int32 SpeakerPlayerId,
	uint8 ChannelByte, const TArray<uint8>& Frame)
{
	// Frames relayed through a character's copy (an older server build) are played
	// by the chat host, so there is only ever one stream per speaker.
	UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	if (Host == nullptr)
	{
		return;
	}

	Host->HandleIncomingFrame(SpeakerPlayerId, ChannelByte, Frame);
}

void UOneBrahamProximityVoiceComponent::HandleIncomingFrame(int32 SpeakerPlayerId, uint8 ChannelByte,
	const TArray<uint8>& Frame)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Frame.Num() == 0 || Frame.Num() > ONE_BRAHAM_MAX_VOICE_FRAME_BYTES)
	{
		return;
	}

	if (ChannelByte > static_cast<uint8>(EOneBrahamVoiceChannel::None))
	{
		return;
	}

	const EOneBrahamVoiceChannel Channel = static_cast<EOneBrahamVoiceChannel>(ChannelByte);
	if (Channel == EOneBrahamVoiceChannel::None)
	{
		return;
	}

	EnsureLocalSetup();

	if (!bVoiceChatEnabled || LocallyMutedSpeakers.Contains(SpeakerPlayerId))
	{
		return;
	}

	// The server already filters mutes; this covers the frames in flight.
	if (const AOneBrahamPlayerState* LocalState = GetOwnerState())
	{
		if (LocalState->MutedPlayerIds.Contains(SpeakerPlayerId))
		{
			return;
		}
	}

	FSpeakerStream& Stream = Streams.FindOrAdd(SpeakerPlayerId);

	// --- Decode with this speaker's own decoder ----------------------------
	if (!Stream.Decoder.IsValid())
	{
		Stream.Decoder = MakeUnique<FOneBrahamVoiceCodec>();
		Stream.Decoder->InitialiseDecoder();
	}

	TArray<uint8> RawPcm;
	if (!Stream.Decoder->DecodeFrame(Frame, RawPcm) || RawPcm.Num() == 0)
	{
		return;
	}

	// --- Stream, rebuilt when the channel changes --------------------------
	// A proximity stream is a spatialised 3D sound and a radio stream is a 2D
	// filtered one; switching a live component between the two leaves the old
	// spatialisation on the active sound, so the audio side is rebuilt. The
	// decoder is kept: it is the same speaker and the same Opus stream.
	UAudioComponent* Component = StreamComponents.FindRef(SpeakerPlayerId);
	if (Component != nullptr && Stream.ChannelByte != ChannelByte)
	{
		DestroySpeakerAudio(SpeakerPlayerId);
		Component = nullptr;
	}

	Stream.ChannelByte = ChannelByte;
	Stream.LastFrameTime = World->GetTimeSeconds();

	if (Component == nullptr)
	{
		Component = CreateSpeakerAudio(SpeakerPlayerId, Channel);
		Stream.bShapingPrimed = false;
		Stream.OcclusionTimer = 0.f;
	}

	USoundWaveProcedural* Wave = StreamWaves.FindRef(SpeakerPlayerId);
	if (Component == nullptr || Wave == nullptr)
	{
		return;
	}

	// Latency cap: if the network delivered a burst after a stall, drop the
	// backlog rather than playing everybody a quarter-second late from now on.
	if (Wave->GetAvailableAudioByteCount() > MaxBufferedPlaybackFrames * FOneBrahamVoiceCodec::RawFrameBytes)
	{
		Wave->ResetAudio();
	}

	// QueueAudio appends to the wave's buffer; the audio component pulls from it
	// on the audio thread.
	Wave->QueueAudio(RawPcm.GetData(), RawPcm.Num());

	if (!Component->IsPlaying())
	{
		// Jitter buffer: hold the first few frames before starting, so ordinary
		// network jitter is absorbed by the cushion instead of heard as gaps.
		const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());
		const int32 JitterFrames = Voice != nullptr ? FMath::Clamp(Voice->GetChatConfig().JitterBufferFrames, 1, 8) : 3;

		if (Wave->GetAvailableAudioByteCount() >= JitterFrames * FOneBrahamVoiceCodec::RawFrameBytes)
		{
			UpdateStreamFiltering(SpeakerPlayerId, 0.f, /*bSnap*/ true);
			Component->Play(0.f);
		}
	}

	SetComponentTickEnabled(true);
}

// ==========================================================================
//  Streams
// ==========================================================================
UAudioComponent* UOneBrahamProximityVoiceComponent::CreateSpeakerAudio(int32 SpeakerPlayerId, EOneBrahamVoiceChannel Channel)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (World == nullptr || Owner == nullptr)
	{
		return nullptr;
	}

	const bool bSpatialised = Channel == EOneBrahamVoiceChannel::Proximity;

	// A streaming wave that stays "playing" while the buffer drains: with bLooping
	// false the component would stop between 20 ms frames and click on restart.
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(FOneBrahamVoiceCodec::SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Voice;
	Wave->bLooping = true;
	Wave->Volume = 1.f;
	Wave->bCanProcessAsync = false;

	UAudioComponent* Component = NewObject<UAudioComponent>(Owner);
	Component->bAutoActivate = false;
	Component->bAutoDestroy = false;
	Component->bStopWhenOwnerDestroyed = true;
	Component->bAllowSpatialization = bSpatialised;
	Component->bIsUISound = false;
	Component->SetSound(Wave);

	if (bSpatialised)
	{
		// Proximity streams are spatialised and follow the speaker: the world
		// location is moved to the speaker's head every tick in
		// UpdateStreamFiltering, so a talker walking away needs no positional
		// replication of their own.
		Component->AttenuationSettings = GetChatAttenuation();

		if (const AOneBrahamPlayerState* SpeakerState = FindPlayerStateById(World, SpeakerPlayerId))
		{
			if (const AOneBrahamCharacter* Speaker = Cast<AOneBrahamCharacter>(SpeakerState->GetPawn()))
			{
				Component->SetWorldLocation(GetMouthLocationOf(Speaker));
			}
		}
	}
	else
	{
		// Radio: 2D but band-passed, which is what tells the player "that came
		// down the radio" without any UI at all.
		Component->AttenuationSettings = nullptr;
		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(RadioLowPassHz);
		Component->SetHighPassFilterEnabled(true);
		Component->SetHighPassFilterFrequency(RadioHighPassHz);
	}

	Component->RegisterComponent();

	StreamComponents.Add(SpeakerPlayerId, Component);
	StreamWaves.Add(SpeakerPlayerId, Wave);

	OB_VOICE_LOG("Opened %s voice stream for player %d.",
		bSpatialised ? TEXT("proximity") : TEXT("radio"), SpeakerPlayerId);

	return Component;
}

void UOneBrahamProximityVoiceComponent::DestroySpeakerAudio(int32 SpeakerPlayerId)
{
	if (TObjectPtr<UAudioComponent>* Found = StreamComponents.Find(SpeakerPlayerId))
	{
		if (UAudioComponent* Component = Found->Get())
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}

	StreamComponents.Remove(SpeakerPlayerId);
	StreamWaves.Remove(SpeakerPlayerId);
}

bool UOneBrahamProximityVoiceComponent::GetListenerEarLocation(FVector& OutLocation) const
{
	// The camera, not the pawn: the engine's audio listener sits on the camera,
	// which follows the spectate target, so occlusion is computed from the same
	// point the spatialisation is - a spectator hears what the player they are
	// watching hears.
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	const APlayerController* PC = State != nullptr ? State->GetPlayerController() : nullptr;

	if (PC != nullptr && PC->PlayerCameraManager != nullptr)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}

	if (PC != nullptr && PC->GetPawn() != nullptr)
	{
		OutLocation = PC->GetPawn()->GetActorLocation();
		return true;
	}

	return false;
}

void UOneBrahamProximityVoiceComponent::UpdateStreamFiltering(int32 SpeakerPlayerId, float DeltaTime, bool bSnap)
{
	UAudioComponent* Component = StreamComponents.FindRef(SpeakerPlayerId);
	FSpeakerStream* Stream = Streams.Find(SpeakerPlayerId);
	UWorld* World = GetWorld();

	if (Component == nullptr || Stream == nullptr || World == nullptr)
	{
		return;
	}

	const float BaseVolume = FMath::Clamp(GetBusVolumeForVoice() * OneBrahamVoiceCVars::GetOutputGain(), 0.f, 4.f);
	const EOneBrahamVoiceChannel Channel = static_cast<EOneBrahamVoiceChannel>(Stream->ChannelByte);

	if (Channel != EOneBrahamVoiceChannel::Proximity)
	{
		// Radio keeps its fixed band; the level is the user's voice volume.
		Component->SetVolumeMultiplier(BaseVolume);
		return;
	}

	// --- Proximity: follow the speaker and shape by distance and geometry ----
	const AOneBrahamPlayerState* SpeakerState = FindPlayerStateById(World, SpeakerPlayerId);
	const AOneBrahamCharacter* Speaker = SpeakerState != nullptr ? Cast<AOneBrahamCharacter>(SpeakerState->GetPawn()) : nullptr;

	FVector Ear = FVector::ZeroVector;
	if (Speaker == nullptr || !GetListenerEarLocation(Ear))
	{
		// No body to follow (yet): hold the last position and level.
		return;
	}

	const FVector Mouth = GetMouthLocationOf(Speaker);
	Component->SetWorldLocation(Mouth);

	const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());
	const float Range = Voice != nullptr ? Voice->GetProximityRangeCm() : 3500.f;

	// Occlusion traces at 10 Hz per stream: walls do not move between frames,
	// and four multi-surface traces a tick per talker would be a real cost.
	Stream->OcclusionTimer -= DeltaTime;
	if (bSnap || Stream->OcclusionTimer <= 0.f)
	{
		Stream->OcclusionTimer = OcclusionUpdateSeconds;

		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		const bool bUserWantsMuffle = Settings == nullptr || Settings->bVoiceMuffledThroughWalls;
		const bool bOcclusion = bUserWantsMuffle && Voice != nullptr && Voice->IsVoiceOcclusionEnabled();

		if (bOcclusion)
		{
			const APlayerState* LocalState = Cast<APlayerState>(GetOwner());
			const AActor* ListenerActor = LocalState != nullptr && LocalState->GetPlayerController() != nullptr
				? LocalState->GetPlayerController()->GetViewTarget()
				: nullptr;

			Stream->BlockingSurfaces = Voice->CountVoiceOccluders(Mouth, Ear, Speaker, ListenerActor);
			Stream->FloorsApart = FMath::Abs(FloorOf(Mouth.Z) - FloorOf(Ear.Z));
		}
		else
		{
			Stream->BlockingSurfaces = 0;
			Stream->FloorsApart = 0;
		}
	}

	const FOneBrahamProximityVoiceShaping Target = OneBrahamVoiceRouting::ShapeProximityVoice(
		FVector::Dist(Mouth, Ear), Range, Stream->BlockingSurfaces, Stream->FloorsApart, Speaker->bIsCrouched);

	// Smoothed, because a raw per-update trace flickers on doorframes and reads as
	// a broken radio. The cutoff glides in log space, which is how it is heard.
	if (bSnap || !Stream->bShapingPrimed)
	{
		Stream->Gain = Target.Gain;
		Stream->LowPassHz = Target.LowPassHz;
		Stream->bShapingPrimed = true;
	}
	else
	{
		Stream->Gain = FMath::FInterpTo(Stream->Gain, Target.Gain, DeltaTime, 6.f);
		const float LogCutoff = FMath::FInterpTo(FMath::Loge(Stream->LowPassHz), FMath::Loge(Target.LowPassHz), DeltaTime, 5.f);
		Stream->LowPassHz = FMath::Exp(LogCutoff);
	}

	Component->SetLowPassFilterEnabled(Stream->LowPassHz < OneBrahamVoiceRouting::ClearLowPassHz - 500.f);
	Component->SetLowPassFilterFrequency(Stream->LowPassHz);
	Component->SetVolumeMultiplier(BaseVolume * Stream->Gain);
}

void UOneBrahamProximityVoiceComponent::StopSpeakerStream(int32 SpeakerPlayerId)
{
	UOneBrahamProximityVoiceComponent* Host = GetChatHost();
	if (Host != nullptr && Host != this)
	{
		Host->StopSpeakerStream(SpeakerPlayerId);
		return;
	}

	if (FSpeakerStream* Stream = Streams.Find(SpeakerPlayerId))
	{
		if (Stream->bHoldsDuck)
		{
			if (UOneBrahamAudioManagerComponent* Audio = DuckedAudioManager.Get())
			{
				Audio->PopVoiceDuck();
			}
		}
	}

	DestroySpeakerAudio(SpeakerPlayerId);
	Streams.Remove(SpeakerPlayerId);
}

void UOneBrahamProximityVoiceComponent::StopAllSpeakerStreams()
{
	TArray<int32> Speakers;
	Streams.GetKeys(Speakers);

	for (const TPair<int32, TObjectPtr<UAudioComponent>>& Pair : StreamComponents)
	{
		Speakers.AddUnique(Pair.Key);
	}

	for (const int32 SpeakerId : Speakers)
	{
		StopSpeakerStream(SpeakerId);
	}

	StreamComponents.Reset();
	StreamWaves.Reset();
	Streams.Reset();
}

// ==========================================================================
//  Helpers
// ==========================================================================
AOneBrahamCharacter* UOneBrahamProximityVoiceComponent::GetCharacter() const
{
	return Cast<AOneBrahamCharacter>(GetOwner());
}

AOneBrahamPlayerState* UOneBrahamProximityVoiceComponent::GetOwnerState() const
{
	if (AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(GetOwner()))
	{
		return State;
	}

	if (const AOneBrahamCharacter* Character = GetCharacter())
	{
		return Cast<AOneBrahamPlayerState>(Character->GetPlayerState());
	}

	return nullptr;
}

UOneBrahamAudioManagerComponent* UOneBrahamProximityVoiceComponent::GetLocalAudioManager() const
{
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	const AOneBrahamPlayerController* PC = State != nullptr
		? Cast<AOneBrahamPlayerController>(State->GetPlayerController())
		: nullptr;

	if (PC == nullptr)
	{
		if (const UWorld* World = GetWorld())
		{
			PC = World->GetFirstPlayerController<AOneBrahamPlayerController>();
		}
	}

	return PC != nullptr ? PC->GetAudioManager() : nullptr;
}

void UOneBrahamProximityVoiceComponent::PlayRadioClick(bool bOpen) const
{
	if (UOneBrahamAudioManagerComponent* Audio = GetLocalAudioManager())
	{
		const AOneBrahamPlayerState* State = GetOwnerState();
		const bool bSquadChannel = State != nullptr && State->VoiceChannel == EOneBrahamVoiceChannel::Squad;
		Audio->PlayRadioOpenClose(bOpen, bSquadChannel);
	}
}

USoundAttenuation* UOneBrahamProximityVoiceComponent::GetVoiceAttenuation()
{
	const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());
	const float Range = Voice != nullptr ? Voice->GetProximityRangeCm() : 3500.f;

	if (VoiceAttenuation != nullptr && FMath::IsNearlyEqual(VoiceAttenuationRangeCm, Range))
	{
		return VoiceAttenuation;
	}

	if (VoiceAttenuation == nullptr)
	{
		VoiceAttenuation = NewObject<USoundAttenuation>(this);
	}

	VoiceAttenuationRangeCm = Range;

	FSoundAttenuationSettings& Settings = VoiceAttenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;

	// Full level inside the reference distance, then the engine's natural-sound
	// curve to the proximity range: the same shape live chat follows.
	Settings.AttenuationShapeExtents = FVector(OneBrahamVoiceRouting::ReferenceDistanceCm, 0.f, 0.f);
	Settings.FalloffDistance = FMath::Max(100.f, Range - OneBrahamVoiceRouting::ReferenceDistanceCm);
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Settings.dBAttenuationAtMax = -50.f;

	// Voice is never occluded by the built-in audio occlusion: live chat does it
	// itself with the VoiceOccluder channel, which is authored per mesh.
	Settings.bEnableOcclusion = false;

	// No listener focus for voice: you must be able to hear a teammate behind you
	// exactly as well as one in front. Turning the focus filter on here is the
	// classic "why can't I hear my squad" bug.
	Settings.bEnableListenerFocus = false;

	return VoiceAttenuation;
}

USoundAttenuation* UOneBrahamProximityVoiceComponent::GetChatAttenuation()
{
	if (ChatAttenuation != nullptr)
	{
		return ChatAttenuation;
	}

	ChatAttenuation = NewObject<USoundAttenuation>(this);

	// Direction only. The level is set per tick from ShapeProximityVoice, which
	// folds walls, floors and crouching into the same curve - two attenuators in
	// series would double the falloff.
	FSoundAttenuationSettings& Settings = ChatAttenuation->Attenuation;
	Settings.bAttenuate = false;
	Settings.bSpatialize = true;
	Settings.bEnableOcclusion = false;
	Settings.bEnableListenerFocus = false;

	// A voice right beside you is not hard-panned into one ear.
	Settings.NonSpatializedRadiusStart = 120.f;
	Settings.NonSpatializedRadiusEnd = 40.f;

	return ChatAttenuation;
}

float UOneBrahamProximityVoiceComponent::GetBusVolumeForVoice() const
{
	// Local helper: the audio manager owns bus volumes, but voice playback is
	// driven from this component, so we resolve the shared value from the same
	// settings object the manager reads. Reading the settings directly avoids a
	// dependency loop between the two components.
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings == nullptr)
	{
		return 1.f;
	}

	return Settings->GetVolume(TEXT("Voice")) * Settings->GetVolume(TEXT("Master"));
}
