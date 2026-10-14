// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamProximityVoiceComponent.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "Voice/OneBrahamVoiceTypes.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

namespace
{
	/** 20 ms frames: the sweet spot between latency and packet overhead. */
	constexpr float VoiceFrameSeconds = 1.f / 50.f;

	/** A speaker stream with no frames for this long is torn down. */
	constexpr float StreamIdleTimeoutSeconds = 1.25f;

	/** Barks fade in properly: a line starting on a hard cut sounds like a bug. */
	constexpr float BarkFadeInSeconds = 0.04f;

	/** Radio channels are filtered to a bandwidth that reads as "over the radio". */
	constexpr float RadioLowPassHz = 3200.f;

	/** A talker is only allowed a little over the nominal cadence. */
	constexpr int32 MaxFramesPerSecondFromClient = 60;
}

UOneBrahamProximityVoiceComponent::UOneBrahamProximityVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Voice is latency sensitive but not tick-critical: 60 Hz keeps the 20 ms
	// cadence honest without burning frame time on 16 idle players.
	PrimaryComponentTick.TickInterval = 0.f;
	SetIsReplicatedByDefault(true);

	bVoiceChatEnabled = true;
}

void UOneBrahamProximityVoiceComponent::BeginPlay()
{
	Super::BeginPlay();

	const AOneBrahamCharacter* Character = GetCharacter();
	const bool bLocallyControlled = Character != nullptr && Character->IsLocallyControlled();
	const bool bServer = GetOwner() != nullptr && GetOwner()->HasAuthority();

	// The codec is only needed where audio is actually processed. Creating it on a
	// headless dedicated server would allocate an encoder for nothing.
	if (!bServer || bLocallyControlled)
	{
		Codec.Initialise();
	}

	// Prime the per-speaker streams table so the first received frame does not
	// stall on object creation.
	StreamComponents.Reset();
	StreamWaves.Reset();

	UE_LOG(LogOneBrahamVoice, Verbose,
		TEXT("[OB.VOICE] Proximity voice component ready on '%s' (%s%s)."),
		GetOwner() != nullptr ? *GetOwner()->GetName() : TEXT("<none>"),
		bServer ? TEXT("server") : TEXT("client"),
		bLocallyControlled ? TEXT(", locally controlled") : TEXT(""));
}

void UOneBrahamProximityVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Leaving the match: stop the microphone first so we do not keep a device open
	// across a level travel, then tear down every stream.
	bLocalTransmitting = false;
	Capture.Stop();
	StopAllSpeakerStreams();

	Super::EndPlay(EndPlayReason);
}

void UOneBrahamProximityVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// --- Local capture ----------------------------------------------------
	AOneBrahamCharacter* Character = GetCharacter();
	if (bLocalTransmitting && Character != nullptr && Character->IsLocallyControlled())
	{
		TickLocalCapture(DeltaTime);
	}
	else if (!bLocalTransmitting && Capture.IsCapturing())
	{
		Capture.Stop();
	}

	// --- Playback streams -------------------------------------------------
	// Filtering tracks the listener's ear, so it updates per frame, not per frame
	// of voice data: occlusion behind a closing lift door should glide, not click.
	for (const TPair<int32, TObjectPtr<UAudioComponent>>& Pair : StreamComponents)
	{
		UpdateStreamFiltering(Pair.Key, DeltaTime);
	}

	// Tear down speakers who have stopped talking.
	const float Now = World->GetTimeSeconds();
	TArray<int32> StaleStreams;

	for (const TPair<int32, float>& Pair : StreamLastFrameTime)
	{
		if (StreamComponents.Contains(Pair.Key) && (Now - Pair.Value) > StreamIdleTimeoutSeconds)
		{
			StaleStreams.Add(Pair.Key);
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
//  Local capture
// ==========================================================================
void UOneBrahamProximityVoiceComponent::SetLocalTransmitting(bool bTransmitting)
{
	if (bTransmitting == bLocalTransmitting)
	{
		return;
	}

	AOneBrahamCharacter* Character = GetCharacter();
	const bool bLocallyControlled = Character != nullptr && Character->IsLocallyControlled();

	if (bTransmitting && bLocallyControlled && bVoiceChatEnabled)
	{
		const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
		const FString PreferredDevice = Settings != nullptr ? Settings->AudioInputDeviceId : FString();

		if (!Capture.Start(PreferredDevice))
		{
			OB_VOICE_LOG("Push-to-talk requested but no microphone is available.");
			bLocalTransmitting = false;
			return;
		}

		FrameTimer = 0.f;
		FramesSentThisSecond = 0;
		SendWindowStart = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.f;
	}
	else
	{
		Capture.Stop();
	}

	bLocalTransmitting = bTransmitting;

	// Local radio click: the transmitting player hears their own key-up, which is
	// the audio equivalent of the HUD indicator.
	if (UWorld* World = GetWorld())
	{
		if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
		{
			if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
			{
				const AOneBrahamPlayerState* State = GetOwnerState();
				const bool bSquadChannel = State != nullptr && State->VoiceChannel == EOneBrahamVoiceChannel::Squad;
				Audio->PlayRadioOpenClose(bLocalTransmitting, bSquadChannel);
			}
		}
	}
}

void UOneBrahamProximityVoiceComponent::SetVoiceChatEnabled(bool bEnabled)
{
	bVoiceChatEnabled = bEnabled;

	if (!bEnabled)
	{
		Capture.Stop();
		bLocalTransmitting = false;
	}

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Voice chat %s for '%s'."),
		bEnabled ? TEXT("enabled") : TEXT("disabled"),
		GetOwner() != nullptr ? *GetOwner()->GetName() : TEXT("<none>"));
}

void UOneBrahamProximityVoiceComponent::TickLocalCapture(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !Capture.IsCapturing())
	{
		return;
	}

	// Self-limit before the server has to: a 24 fps frame hitch must not turn into
	// a burst of thirty frames that all get dropped by the relay budget.
	const float Now = World->GetTimeSeconds();
	if (Now - SendWindowStart >= 1.f)
	{
		SendWindowStart = Now;
		FramesSentThisSecond = 0;
	}

	FrameTimer += DeltaTime;

	// Send at most one frame per tick interval worth of time, keeping the cadence
	// at 20 ms plus a 20% allowance for jitter, and never more than the client cap.
	const int32 AllowedFrames = FMath::Min(
		FMath::Max(1, FMath::FloorToInt(FrameTimer / VoiceFrameSeconds)),
		FMath::Max(1, MaxFramesPerSecondFromClient - FramesSentThisSecond));

	if (AllowedFrames <= 0)
	{
		return;
	}

	TArray<TArray<uint8>> RawFrames;
	if (!Capture.PullFrames(RawFrames))
	{
		return;
	}

	const int32 FramesToSend = FMath::Min(RawFrames.Num(), AllowedFrames);

	for (int32 Index = 0; Index < FramesToSend; ++Index)
	{
		TArray<uint8> Encoded;
		if (!Codec.EncodeFrame(RawFrames[Index], Encoded))
		{
			continue;
		}

		if (Encoded.Num() > ONE_BRAHAM_MAX_VOICE_FRAME_BYTES)
		{
			UE_LOG(LogOneBrahamVoice, Warning,
				TEXT("[OB.VOICE] Dropping an oversized outgoing frame (%d bytes > %d)."),
				Encoded.Num(), ONE_BRAHAM_MAX_VOICE_FRAME_BYTES);
			continue;
		}

		const uint8 ChannelByte = static_cast<uint8>(EOneBrahamVoiceChannel::Proximity);
		ServerPushVoiceFrame(Encoded, ChannelByte);
		++FramesSentThisSecond;
	}

	// Consume only what we sent; anything left stays buffered for the next tick.
	FrameTimer = FMath::Max(0.f, FrameTimer - (FramesToSend * VoiceFrameSeconds));
}

// ==========================================================================
//  Relay
// ==========================================================================
void UOneBrahamProximityVoiceComponent::ServerPushVoiceFrame_Implementation(const TArray<uint8>& Frame,
	uint8 ChannelByte)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	AOneBrahamPlayerState* SpeakerState = GetOwnerState();
	AOneBrahamCharacter* Character = GetCharacter();

	if (SpeakerState == nullptr || Character == nullptr)
	{
		return;
	}

	// --- Authority and validity -------------------------------------------
	// 1. The frame must come from the player who owns this pawn, not from a
	//    puppeteered or possessed character.
	if (SpeakerState->GetOwningController() == nullptr
		|| Character->GetController() != SpeakerState->GetOwningController())
	{
		UE_LOG(LogOneBrahamVoice, Warning,
			TEXT("[OB.VOICE] Voice frame from '%s' rejected: pawn ownership mismatch."),
			*SpeakerState->GetPlayerName());
		return;
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
	//    the whole team while pretending to whisper.
	const EOneBrahamVoiceChannel Channel = SpeakerState->VoiceChannel;

	if (Channel == EOneBrahamVoiceChannel::None || SpeakerState->bSelfMuted || SpeakerState->IsEliminated())
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

	RelayFrameToListeners(SpeakerState, static_cast<uint8>(Channel), Frame);
}

void UOneBrahamProximityVoiceComponent::RelayFrameToListeners(AOneBrahamPlayerState* SpeakerState,
	uint8 ChannelByte, const TArray<uint8>& Frame)
{
	UWorld* World = GetWorld();
	UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());
	if (World == nullptr || Voice == nullptr || SpeakerState == nullptr)
	{
		return;
	}

	const int32 SpeakerId = SpeakerState->GetPlayerId();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* ListenerPC = Cast<AOneBrahamPlayerController>(It->Get());
		if (ListenerPC == nullptr)
		{
			continue;
		}

		AOneBrahamPlayerState* ListenerState = ListenerPC->GetPlayerState<AOneBrahamPlayerState>();
		if (ListenerState == nullptr || ListenerState == SpeakerState)
		{
			continue;
		}

		// Policy lives in one place: proximity radius, squad, team, mutes.
		if (!Voice->ShouldHear(ListenerState, SpeakerState))
		{
			continue;
		}

		AOneBrahamCharacter* ListenerCharacter = Cast<AOneBrahamCharacter>(ListenerPC->GetPawn());
		if (ListenerCharacter == nullptr)
		{
			continue;
		}

		if (UOneBrahamProximityVoiceComponent* ListenerVoice = ListenerCharacter->FindComponentByClass<UOneBrahamProximityVoiceComponent>())
		{
			if (ListenerVoice->IsVoiceChatEnabled())
			{
				ListenerVoice->ClientReceiveVoiceFrame(SpeakerId, ChannelByte, Frame);
			}
		}
	}
}

void UOneBrahamProximityVoiceComponent::ClientReceiveVoiceFrame_Implementation(int32 SpeakerPlayerId,
	uint8 ChannelByte, const TArray<uint8>& Frame)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Frame.Num() == 0 || Frame.Num() > ONE_BRAHAM_MAX_VOICE_FRAME_BYTES)
	{
		return;
	}

	const EOneBrahamVoiceChannel Channel = static_cast<EOneBrahamVoiceChannel>(ChannelByte);
	const bool bSpatialised = Channel == EOneBrahamVoiceChannel::Proximity;

	// --- Decode -----------------------------------------------------------
	TArray<uint8> RawPcm;
	if (!Codec.DecodeFrame(Frame, RawPcm) || RawPcm.Num() == 0)
	{
		return;
	}

	// --- Stream -----------------------------------------------------------
	UAudioComponent* Stream = EnsureSpeakerStream(SpeakerPlayerId, bSpatialised);
	if (Stream == nullptr)
	{
		return;
	}

	USoundWaveProcedural* Wave = StreamWaves.FindRef(SpeakerPlayerId);
	if (Wave == nullptr)
	{
		return;
	}

	// QueueAudio appends to the wave's ring buffer; the audio component pulls from
	// it on the audio thread, so no locking or allocation happens on the hot path.
	Wave->QueueAudio(RawPcm.GetData(), RawPcm.Num());

	if (!Stream->IsPlaying())
	{
		Stream->Play(0.f);
	}

	StreamChannels.Add(SpeakerPlayerId, ChannelByte);
	StreamLastFrameTime.Add(SpeakerPlayerId, World->GetTimeSeconds());

	// --- Ducking ----------------------------------------------------------
	// Whoever is talking, everyone else's beds get out of the way. Ref-counted so
	// two simultaneous talkers do not both try to restore the mix.
	UOneBrahamAudioManagerComponent* Audio = nullptr;
	if (AOneBrahamPlayerController* LocalPC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		Audio = LocalPC->GetAudioManager();
	}

	if (Audio != nullptr)
	{
		Audio->PushVoiceDuck();

		// Release the duck shortly after this frame's audio would have finished, so
		// the mix comes back within the same breath as the voice.
		const float ReleaseAfter = (RawPcm.Num() / 2.0f) / static_cast<float>(FOneBrahamVoiceCodec::SampleRate) + 0.45f;

		FTimerHandle Handle;
		TWeakObjectPtr<UOneBrahamAudioManagerComponent> WeakAudio(Audio);

		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[WeakAudio]()
			{
				if (UOneBrahamAudioManagerComponent* StrongAudio = WeakAudio.Get())
				{
					StrongAudio->PopVoiceDuck();
				}
			}), ReleaseAfter, false);
	}
}

// ==========================================================================
//  Streams
// ==========================================================================
UAudioComponent* UOneBrahamProximityVoiceComponent::EnsureSpeakerStream(int32 SpeakerPlayerId, bool bSpatialised)
{
	if (TObjectPtr<UAudioComponent>* Existing = StreamComponents.Find(SpeakerPlayerId))
	{
		if (UAudioComponent* Component = Existing->Get())
		{
			return Component;
		}
	}

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (World == nullptr || Owner == nullptr)
	{
		return nullptr;
	}

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
	Component->bIsUISound = !bSpatialised;
	Component->SetSound(Wave);
	Component->RegisterComponent();

	if (bSpatialised)
	{
		// Proximity streams are spatialised and follow the speaker: the component's
		// world location is updated in UpdateStreamFiltering to the speaker's head
		// socket every frame, so a talker walking away does not need any positional
		// replication of their own.
		Component->AttenuationSettings = GetVoiceAttenuation();
		Component->SetWorldLocation(GetMouthLocation());
	}
	else
	{
		// Radio: 2D but filtered, which is what tells the player "that came down
		// the radio" without any UI at all.
		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(FMath::Min(RadioLowPassHz,
			FMath::Max(400.f, Codec.SampleRate * 0.2f)));
	}

	StreamComponents.Add(SpeakerPlayerId, Component);
	StreamWaves.Add(SpeakerPlayerId, Wave);
	StreamChannels.Add(SpeakerPlayerId, static_cast<uint8>(bSpatialised
		? EOneBrahamVoiceChannel::Proximity : EOneBrahamVoiceChannel::Squad));
	StreamOcclusion.Add(SpeakerPlayerId, 0.f);

	OB_VOICE_LOG("Opened %s voice stream for player %d.",
		bSpatialised ? TEXT("proximity") : TEXT("radio"), SpeakerPlayerId);

	return Component;
}

void UOneBrahamProximityVoiceComponent::UpdateStreamFiltering(int32 SpeakerPlayerId, float DeltaTime)
{
	UAudioComponent* Component = StreamComponents.FindRef(SpeakerPlayerId);
	if (Component == nullptr)
	{
		return;
	}

	const uint8 ChannelByte = StreamChannels.FindRef(SpeakerPlayerId);
	const EOneBrahamVoiceChannel Channel = static_cast<EOneBrahamVoiceChannel>(ChannelByte);

	if (Channel != EOneBrahamVoiceChannel::Proximity)
	{
		// Radio keeps its fixed filter; volume is already governed by the mixer.
		Component->SetLowPassFilterFrequency(RadioLowPassHz);
		Component->SetVolumeMultiplier(FMath::Clamp(GetBusVolumeForVoice(), 0.f, 2.f));
		return;
	}

	// --- Proximity: follow the speaker and muffle through geometry ---------
	if (AOneBrahamPlayerState* SpeakerState = [this, SpeakerPlayerId]() -> AOneBrahamPlayerState*
		{
			if (UWorld* World = GetWorld())
			{
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get()))
					{
						if (AOneBrahamPlayerState* State = PC->GetPlayerState<AOneBrahamPlayerState>())
						{
							if (State->GetPlayerId() == SpeakerPlayerId)
							{
								return State;
							}
						}
					}
				}
			}
			return nullptr;
		}())
	{
		if (AOneBrahamCharacter* SpeakerCharacter = Cast<AOneBrahamCharacter>(SpeakerState->GetPawn()))
		{
			if (const USkeletalMeshComponent* Mesh = SpeakerCharacter->GetMesh())
			{
				Component->SetWorldLocation(Mesh->GetSocketLocation(TEXT("Head")));
			}
		}

		// Occlusion: recomputed continuously but smoothed, because a raw per-frame
		// trace flickers on doorframes and reads as a broken radio.
		if (const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
		{
			const AOneBrahamCharacter* Listener = GetCharacter();
			const AOneBrahamCharacter* Speaker = Cast<AOneBrahamCharacter>(SpeakerState->GetPawn());

			if (Listener != nullptr && Speaker != nullptr)
			{
				const float Target = Voice->ComputeOcclusionAlpha(
					Speaker->GetEyeLocation(), Listener->GetEyeLocation());

				const float Current = StreamOcclusion.FindRef(SpeakerPlayerId);
				const float Smoothed = FMath::FInterpTo(Current, Target, DeltaTime, 3.f);
				StreamOcclusion.Add(SpeakerPlayerId, Smoothed);

				// Occlusion is expressed as a low-pass sweep: 20 kHz clear, 900 Hz
				// fully behind a shutter. It is remarkably convincing.
				const float Cutoff = FMath::Lerp(20000.f, 900.f, FMath::Clamp(Smoothed, 0.f, 1.f));
				Component->SetLowPassFilterEnabled(Cutoff < 19500.f);
				Component->SetLowPassFilterFrequency(Cutoff);
			}
		}

		Component->SetVolumeMultiplier(FMath::Clamp(GetBusVolumeForVoice(), 0.f, 2.f));
	}
}

void UOneBrahamProximityVoiceComponent::StopSpeakerStream(int32 SpeakerPlayerId)
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
	StreamChannels.Remove(SpeakerPlayerId);
	StreamOcclusion.Remove(SpeakerPlayerId);
	StreamLastFrameTime.Remove(SpeakerPlayerId);
}

void UOneBrahamProximityVoiceComponent::StopAllSpeakerStreams()
{
	TArray<int32> Speakers;
	StreamComponents.GetKeys(Speakers);

	for (const int32 SpeakerId : Speakers)
	{
		StopSpeakerStream(SpeakerId);
	}

	StreamComponents.Reset();
	StreamWaves.Reset();
	StreamChannels.Reset();
	StreamOcclusion.Reset();
	StreamLastFrameTime.Reset();
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
	if (const AOneBrahamCharacter* Character = GetCharacter())
	{
		return Cast<AOneBrahamPlayerState>(Character->GetPlayerState());
	}
	return nullptr;
}

FVector UOneBrahamProximityVoiceComponent::GetMouthLocation() const
{
	if (const AOneBrahamCharacter* Character = GetCharacter())
	{
		return Character->GetEyeLocation() + Character->GetActorForwardVector() * 12.f;
	}

	return GetOwner() != nullptr ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

USoundAttenuation* UOneBrahamProximityVoiceComponent::GetVoiceAttenuation()
{
	if (VoiceAttenuation != nullptr)
	{
		return VoiceAttenuation;
	}

	const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());
	const float Range = Voice != nullptr ? Voice->GetProximityRangeCm() : 3500.f;

	VoiceAttenuation = NewObject<USoundAttenuation>(this);

	FSoundAttenuationSettings& Settings = VoiceAttenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(Range * 0.18f);
	Settings.FalloffDistance = Range;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;

	// Voice is never occluded by the built-in audio occlusion: we do it ourselves
	// with a channel that is authored per mesh and a smoothing filter, which gives
	// far better results through an open lift lobby than a binary trace would.
	Settings.bEnableOcclusion = false;

	// No listener focus for voice: you must be able to hear a teammate behind you
	// exactly as well as one in front. Turning the focus filter on here is the
	// classic "why can't I hear my squad" bug.
	Settings.bEnableListenerFocus = false;

	return VoiceAttenuation;
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
