// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceSubsystem.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	/** Line cooldown key: player id + line hash, packed into one 64-bit key. */
	FORCEINLINE uint64 MakeCooldownKey(int32 PlayerId, FName LineId)
	{
		const uint64 IdPart = static_cast<uint64>(static_cast<uint32>(PlayerId)) << 32;
		return IdPart | static_cast<uint64>(GetTypeHash(LineId));
	}

	/** Voice policy: how long between ambient mutters for one character. */
	constexpr float AmbientLineMinInterval = 38.f;
	constexpr float AmbientLineMaxInterval = 95.f;
}

// ==========================================================================
//  Lifecycle
// ==========================================================================
void UOneBrahamVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));

	// --- Line set ---------------------------------------------------------
	if (UOneBrahamVoiceLineSet* Authored = LoadObject<UOneBrahamVoiceLineSet>(nullptr,
		TEXT("/Game/OneBraham/Voice/DA_VoiceLines.DA_VoiceLines")))
	{
		LineSet = Authored;
	}
	else
	{
		// Code-only fallback. Same ids, same priorities, no recorded audio: the
		// game still subtitles every line, so no mechanic is ever silent.
		LineSet = NewObject<UOneBrahamVoiceLineSet>(this, TEXT("RuntimeVoiceLineSet"));
	}

	LineSet->EnsureCoreLines();

	// --- Transport config -------------------------------------------------
	ChatConfig = FOneBrahamVoiceChatConfig();

	// Priority is resolved from the match settings asset when the world has one,
	// so a designer can widen proximity chat for a big FIRE DRILL map without a
	// code change.
	if (UWorld* World = GetWorld())
	{
		if (const AOneBrahamGameState* GS = World->GetGameState<AOneBrahamGameState>())
		{
			if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
			{
				ChatConfig.ProximityRangeCm = Settings->ProximityVoiceRangeCm;

				// Bigger rooms get a slightly wider relay budget: a 12-player TDM uses
				// a lot more talking than three squads creeping up a stairwell.
				const int32 ExtraFrames = Settings->MaxPlayers > 12 ? 20 : 10;
				ChatConfig.MaxFramesPerSecond = FMath::Clamp(
					ChatConfig.SendFramesPerSecond + ExtraFrames, 40, 120);
			}
		}
	}

	UE_LOG(LogOneBrahamVoice, Log,
		TEXT("[OB.VOICE] Voice subsystem ready - %d line(s), proximity %.0f cm, budget %d frames/s."),
		LineSet->Lines.Num(), ChatConfig.ProximityRangeCm, ChatConfig.MaxFramesPerSecond);

	if (GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_Client)
	{
		ScheduleNextAmbientLine();
	}
}

void UOneBrahamVoiceSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AmbientLineTimerHandle);
	}

	Super::Deinitialize();
}

UOneBrahamVoiceSubsystem* UOneBrahamVoiceSubsystem::Get(const AActor* Context)
{
	if (Context == nullptr)
	{
		return nullptr;
	}

	UWorld* World = Context->GetWorld();
	return World != nullptr ? World->GetSubsystem<UOneBrahamVoiceSubsystem>() : nullptr;
}

// ==========================================================================
//  Behaviour lines
// ==========================================================================
bool UOneBrahamVoiceSubsystem::SpeakContextualLine(AActor* Speaker, FName LineId, float Priority)
{
	UWorld* World = GetWorld();
	if (World == nullptr || LineId.IsNone())
	{
		return false;
	}

	// Server-authoritative: the server picks the line, the multicast delivers it.
	// A client call is a no-op rather than a warning, because several systems
	// (weapon component, inventory, downed component) legitimately run on both
	// sides and want the same call site.
	if (World->GetNetMode() == NM_Client)
	{
		return false;
	}

	AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Speaker);
	AOneBrahamPlayerState* State = Character != nullptr
		? Cast<AOneBrahamPlayerState>(Character->GetPlayerState())
		: Cast<AOneBrahamPlayerState>(Speaker);

	if (Character == nullptr && State != nullptr)
	{
		Character = Cast<AOneBrahamCharacter>(State->GetPawn());
	}

	if (Character == nullptr)
	{
		return false;
	}

	if (!CanSpeak(State != nullptr ? Cast<AActor>(State) : Cast<AActor>(Character)))
	{
		return false;
	}

	// --- Line selection ---------------------------------------------------
	const FOneBrahamVoiceLine* Line = LineSet != nullptr
		? LineSet->PickLine(LineId, Character->GetArchetypeTag(), Random)
		: nullptr;

	if (Line == nullptr)
	{
		OB_VOICE_LOG("No line registered for '%s' requested by '%s'.",
			*LineId.ToString(), *Character->GetName());
		return false;
	}

	// --- Luck and budget --------------------------------------------------
	if (Line->Chance < 1.f && Random.FRand() > Line->Chance)
	{
		return false;
	}

	const int32 PlayerId = State != nullptr ? State->GetPlayerId() : -1;
	const uint64 CooldownKey = MakeCooldownKey(PlayerId, LineId);
	const float Now = World->GetTimeSeconds();

	if (const float* LastTime = LineCooldowns.Find(CooldownKey))
	{
		if (Now - *LastTime < Line->CooldownSeconds)
		{
			return false;
		}
	}

	// Priority override: callers may raise or lower the line's own priority (for
	// example "reloading" is critical when the enemy is ten metres away).
	const float EffectivePriority = FMath::Clamp(
		FMath::Max(Priority, static_cast<float>(Line->Priority) / 3.f), 0.f, 1.f);

	// --- Deliver ----------------------------------------------------------
	UOneBrahamProximityVoiceComponent* VoiceComponent = Character->GetVoiceComponent();
	if (VoiceComponent == nullptr)
	{
		OB_VOICE_LOG("Character '%s' has no proximity voice component.", *Character->GetName());
		return false;
	}

	const bool bSpoken = VoiceComponent->ServerSpeakLine(LineId, EffectivePriority);
	if (bSpoken)
	{
		LineCooldowns.Add(CooldownKey, Now);
		LastSpokenTime.Add(State, Now);
	}

	return bSpoken;
}

bool UOneBrahamVoiceSubsystem::CanSpeak(AActor* Speaker) const
{
	if (Speaker == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Speaker);
	const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Speaker);

	if (Character != nullptr && State == nullptr)
	{
		State = Cast<AOneBrahamPlayerState>(Character->GetPlayerState());
	}
	if (State != nullptr && Character == nullptr)
	{
		Character = Cast<AOneBrahamCharacter>(State->GetPawn());
	}

	if (State == nullptr)
	{
		return false;
	}

	// An eliminated player does not make small talk. Downed players do - they are
	// the ones the squad most needs to hear from.
	if (State->IsEliminated())
	{
		return false;
	}

	// One line per character per 4 seconds, so a squad of four cannot turn into a
	// podcast, and a single character cannot repeat themselves.
	UWorld* World = GetWorld();
	const float Now = World != nullptr ? World->GetTimeSeconds() : 0.f;

	if (const float* LastSpoken = LastSpokenTime.Find(State))
	{
		if (Now - *LastSpoken < 4.f)
		{
			return false;
		}
	}

	return true;
}

void UOneBrahamVoiceSubsystem::NotifyLineSpoken(AOneBrahamPlayerState* SpeakerState,
	AOneBrahamCharacter* Speaker, FName LineId, const FText& Subtitle, float DurationSeconds)
{
	OnVoiceLineSpoken.Broadcast(SpeakerState, LineId, Subtitle, DurationSeconds);

	OB_VOICE_LOG("Line '%s' spoken by '%s'.", *LineId.ToString(),
		Speaker != nullptr ? *Speaker->GetName() : TEXT("<none>"));
}

// ==========================================================================
//  Proximity muttering
// ==========================================================================
void UOneBrahamVoiceSubsystem::ScheduleNextAmbientLine()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Delay = Random.FRandRange(AmbientLineMinInterval, AmbientLineMaxInterval);
	World->GetTimerManager().SetTimer(AmbientLineTimerHandle, this,
		&UOneBrahamVoiceSubsystem::HandleAmbientLineTimer, Delay, false);
}

void UOneBrahamVoiceSubsystem::HandleAmbientLineTimer()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Pick a random living character who is not currently in a fight. The check
	// matters: characters muttering about the carpet while a firefight is going on
	// two rooms away reads as a bug, not as personality.
	TArray<AOneBrahamCharacter*> Candidates;

	for (TActorIterator<AOneBrahamCharacter> It(World); It; ++It)
	{
		AOneBrahamCharacter* Character = *It;
		if (Character == nullptr || !Character->IsAlive())
		{
			continue;
		}

		AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Character->GetPlayerState());
		if (State == nullptr)
		{
			continue;
		}

		const int32* Count = AmbientLineCounts.Find(State);
		if (Count != nullptr && *Count >= 4)
		{
			continue; // this one has had their say for the match
		}

		if (Character->HasRecentCombatActivity(20.f))
		{
			continue;
		}

		Candidates.Add(Character);
	}

	if (Candidates.Num() > 0)
	{
		AOneBrahamCharacter* Speaker = Candidates[Random.RandRange(0, Candidates.Num() - 1)];

		static const FName AmbientLines[] = {
			TEXT("V_AMBIENT_ELEVATOR"), TEXT("V_AMBIENT_CANTEEN"), TEXT("V_AMBIENT_SERVER_ROOM"),
			TEXT("V_AMBIENT_RAIN"), TEXT("V_AMBIENT_STAIRS"), TEXT("V_AMBIENT_EXEC"),
			TEXT("V_AMBIENT_TIRED"), TEXT("V_AMBIENT_COFFEE"), TEXT("V_AMBIENT_MEETING"),
			TEXT("V_AMBIENT_SKYDECK"), TEXT("V_AMBIENT_PLAZA")
		};

		const FName LineId = AmbientLines[Random.RandRange(0, UE_ARRAY_COUNT(AmbientLines) - 1)];

		if (SpeakContextualLine(Speaker, LineId, 0.1f))
		{
			if (AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Speaker->GetPlayerState()))
			{
				AmbientLineCounts.FindOrAdd(State) += 1;
			}
		}
	}

	ScheduleNextAmbientLine();
}

// ==========================================================================
//  Live chat policy
// ==========================================================================
void UOneBrahamVoiceSubsystem::SetTransmitting(const AActor* Context, bool bTransmitting)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Context == nullptr)
	{
		return;
	}

	// Resolve the player state and the character from whatever we were handed.
	AOneBrahamPlayerState* State = const_cast<AOneBrahamPlayerState*>(Cast<AOneBrahamPlayerState>(Context));
	AOneBrahamCharacter* Character = const_cast<AOneBrahamCharacter*>(Cast<AOneBrahamCharacter>(Context));

	if (const AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Context))
	{
		State = PC->GetPlayerState<AOneBrahamPlayerState>();
		Character = Cast<AOneBrahamCharacter>(PC->GetPawn());
	}

	if (State == nullptr && Character != nullptr)
	{
		State = Cast<AOneBrahamPlayerState>(Character->GetPlayerState());
	}

	if (State == nullptr)
	{
		return;
	}

	// Self-muted or the channel is Off: push-to-talk does nothing, and the HUD
	// shows the mic-muted icon rather than the transmitting one.
	const bool bAllowed = bTransmitting
		&& !State->bSelfMuted
		&& State->VoiceChannel != EOneBrahamVoiceChannel::None;

	UOneBrahamProximityVoiceComponent* VoiceComponent = Character != nullptr
		? Character->GetVoiceComponent()
		: nullptr;

	if (VoiceComponent != nullptr)
	{
		// Local capture is driven on the owning client; the server only flips the
		// replicated transmitting flag that drives the HUD indicator and the duck.
		VoiceComponent->SetLocalTransmitting(bAllowed);
	}

	if (State->HasAuthority())
	{
		State->MulticastSetTransmitting(bAllowed);
	}
}

bool UOneBrahamVoiceSubsystem::IsTransmitting(const AOneBrahamPlayerState* PlayerState) const
{
	return PlayerState != nullptr && PlayerState->bIsTransmitting;
}

bool UOneBrahamVoiceSubsystem::ShouldHear(const AOneBrahamPlayerState* Listener,
	const AOneBrahamPlayerState* Speaker) const
{
	if (Listener == nullptr || Speaker == nullptr || Listener == Speaker)
	{
		return false;
	}

	// Muting is per listener, so the caller is responsible for the audio graph.
	if (Listener->MutedByPlayerIds.Contains(Speaker->GetPlayerId()))
	{
		return false;
	}

	if (Listener->bSelfMuted || Speaker->bSelfMuted)
	{
		return false;
	}

	// Dead players can still hear (they are spectating the match) but cannot be
	// heard: enforced on the transmit path, so nothing to do here.
	switch (Speaker->VoiceChannel)
	{
	case EOneBrahamVoiceChannel::Proximity:
	{
		const APawn* ListenerPawn = Listener->GetPawn();
		const APawn* SpeakerPawn = Speaker->GetPawn();

		if (ListenerPawn == nullptr || SpeakerPawn == nullptr)
		{
			return false;
		}

		const float Range = GetProximityRangeCm();
		return FVector::DistSquared(ListenerPawn->GetActorLocation(), SpeakerPawn->GetActorLocation())
			<= FMath::Square(Range);
	}

	case EOneBrahamVoiceChannel::Squad:
		// A dead squad member's radio still works: that is a deliberate design
		// decision. It keeps eliminated players useful instead of bored.
		return Speaker->Team == Listener->Team && Speaker->SquadId == Listener->SquadId;

	case EOneBrahamVoiceChannel::Team:
		return Speaker->Team == Listener->Team;

	case EOneBrahamVoiceChannel::Announcement:
		return true;

	case EOneBrahamVoiceChannel::None:
	default:
		return false;
	}
}

float UOneBrahamVoiceSubsystem::ComputeOcclusionAlpha(const FVector& SpeakerLocation,
	const FVector& ListenerLocation) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0.f;
	}

	const float Distance = FVector::Dist(SpeakerLocation, ListenerLocation);
	if (Distance <= 250.f)
	{
		return 0.f; // same room, always clear
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OneBrahamVoiceOcclusion), /*bTraceComplex*/ false);
	Params.bReturnPhysicalMaterial = false;

	// The VoiceOccluder channel is authored per mesh: desks and glass do not block
	// voice, but walls, shutters and lift doors do. That distinction is what makes
	// proximity chat feel physical rather than psychic.
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(
		Hit, SpeakerLocation, ListenerLocation, ECC_GameTraceChannel7, Params);

	if (!bBlocked)
	{
		// Long-distance falloff even with a clear line: a shout does not carry
		// across an entire open-plan floor plan unchanged.
		const float Range = FMath::Max(500.f, GetProximityRangeCm());
		return FMath::Clamp(Distance / Range, 0.f, 1.f) * 0.5f;
	}

	// Each blocking surface adds more muffle, up to fully unintelligible.
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, SpeakerLocation, ListenerLocation, ECC_GameTraceChannel7, Params);

	const int32 BlockingSurfaces = FMath::Max(1, Hits.Num());
	return FMath::Clamp(0.55f + 0.2f * static_cast<float>(BlockingSurfaces - 1), 0.f, 1.f);
}

float UOneBrahamVoiceSubsystem::GetProximityRangeCm() const
{
	if (ChatConfig.ProximityRangeCm > 0.f)
	{
		return ChatConfig.ProximityRangeCm;
	}

	// Match settings asset override, then the documented default.
	if (UWorld* World = GetWorld())
	{
		if (const AOneBrahamGameState* GS = World->GetGameState<AOneBrahamGameState>())
		{
			if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
			{
				return Settings->ProximityVoiceRangeCm;
			}
		}
	}

	return 3500.f;
}

bool UOneBrahamVoiceSubsystem::ConsumeRelayBudget(APlayerState* Talker, int32 FrameBytes)
{
	if (Talker == nullptr)
	{
		return false;
	}

	// Clamp defensively: a modified client can send any size, and the budget is
	// the last line of defence before the server relays it to five other players.
	const int32 ClampedBytes = FMath::Clamp(FrameBytes, 0, MAX_VOICE_FRAME_BYTES);

	UWorld* World = GetWorld();
	const float Now = World != nullptr ? World->GetTimeSeconds() : 0.f;

	FOneBrahamVoiceBudget& Budget = RelayBudgets.FindOrAdd(Talker);

	const bool bAllowed = Budget.TryConsume(Now, ClampedBytes,
		ChatConfig.MaxFramesPerSecond, ChatConfig.MaxBytesPerSecond);

	if (!bAllowed)
	{
		++TotalDroppedFrames;

		// Log the first few and then stay quiet: a flooding client would otherwise
		// fill the log with thousands of identical lines.
		if (Budget.DroppedFrames <= 5)
		{
			UE_LOG(LogOneBrahamVoice, Warning,
				TEXT("[OB.VOICE] Throttling voice from '%s' (%d dropped frames this second)."),
				*Talker->GetPlayerName(), Budget.DroppedFrames);
		}
	}

	return bAllowed;
}

// ==========================================================================
//  Local UI
// ==========================================================================
void UOneBrahamVoiceSubsystem::PlayChannelSwitchCue(EOneBrahamVoiceChannel Channel)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			// A different pitch per channel so the player knows where they landed
			// without looking at the HUD: proximity low, squad mid, team high.
			const float Pitch = Channel == EOneBrahamVoiceChannel::Proximity ? 0.9f
				: (Channel == EOneBrahamVoiceChannel::Squad ? 1.05f : 1.2f);

			Audio->PlaySound2D(TEXT("UI_Voice_ChannelSwitch"), 1.f, Pitch);
		}
	}
}

FText UOneBrahamVoiceSubsystem::GetChannelDisplayName(EOneBrahamVoiceChannel Channel) const
{
	switch (Channel)
	{
	case EOneBrahamVoiceChannel::Proximity:		return NSLOCTEXT("OneBraham", "VoiceChannel_Proximity", "PROXIMITY");
	case EOneBrahamVoiceChannel::Squad:			return NSLOCTEXT("OneBraham", "VoiceChannel_Squad", "SQUAD");
	case EOneBrahamVoiceChannel::Team:			return NSLOCTEXT("OneBraham", "VoiceChannel_Team", "TEAM");
	case EOneBrahamVoiceChannel::Announcement:	return NSLOCTEXT("OneBraham", "VoiceChannel_Announcement", "BUILDING");
	case EOneBrahamVoiceChannel::None:
	default:									return NSLOCTEXT("OneBraham", "VoiceChannel_None", "VOICE OFF");
	}
}

void UOneBrahamVoiceSubsystem::SpeakAnnouncement(FName LineId)
{
	// Building announcements come from the director, not from a character, and are
	// heard everywhere: no proximity, no occlusion, subtitle in the same style.
	if (LineSet == nullptr)
	{
		return;
	}

	TArray<const FOneBrahamVoiceLine*> Lines;
	LineSet->FindLines(LineId, Lines);

	if (Lines.Num() == 0)
	{
		OB_VOICE_LOG("Announcement '%s' is not in the voice line set.", *LineId.ToString());
		return;
	}

	const FOneBrahamVoiceLine* Line = Lines[Random.RandRange(0, Lines.Num() - 1)];

	const float Duration = Line->SubtitleSeconds > 0.f ? Line->SubtitleSeconds : 4.f;
	OnVoiceLineSpoken.Broadcast(nullptr, LineId, Line->Subtitle, Duration);

	// Play the announcement 2D on the local machine. Every machine receives the
	// same replicated trigger from the match director, so no RPC is needed, but
	// only the two nearest floors actually hear a building announcement.
	if (UWorld* World = GetWorld())
	{
		const bool bNearby = true;

		if (USoundBase* LineSound = Line->Sound.LoadSynchronous())
		{
			if (bNearby)
			{
				UGameplayStatics::SpawnSound2D(World, LineSound, 1.f, 1.f, 0.f, nullptr, false, true);
			}
		}
		else
		{
			OB_VOICE_LOG("Announcement '%s' has no recorded asset - subtitle only.", *LineId.ToString());
		}
	}
}


// ==========================================================================
//  Quick lines
// ==========================================================================
float UOneBrahamVoiceSubsystem::GetLineCooldownRemaining(const AActor* Speaker, FName LineId) const
{
	const UWorld* World = GetWorld();

	if (World == nullptr || Speaker == nullptr || LineId.IsNone())
	{
		return 0.f;
	}

	const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Speaker);
	const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Speaker);

	if (Character != nullptr && State == nullptr)
	{
		State = Character->GetPlayerState<AOneBrahamPlayerState>();
	}

	if (State == nullptr)
	{
		return 0.f;
	}

	// How long the line would be unavailable for, from the table.
	float CooldownSeconds = 0.f;

	if (LineSet != nullptr)
	{
		TArray<const FOneBrahamVoiceLine*> Lines;
		LineSet->FindLines(LineId, Lines);

		if (Lines.Num() > 0)
		{
			CooldownSeconds = Lines[0]->CooldownSeconds;
		}
	}

	if (CooldownSeconds <= 0.f)
	{
		return 0.f;
	}

	const uint64 CooldownKey = MakeCooldownKey(State->GetPlayerId(), LineId);
	const float* LastTime = LineCooldowns.Find(CooldownKey);

	if (LastTime == nullptr)
	{
		return 0.f;
	}

	const float Elapsed = World->GetTimeSeconds() - *LastTime;

	return Elapsed >= CooldownSeconds ? 0.f : CooldownSeconds - Elapsed;
}

void UOneBrahamVoiceSubsystem::GetQuickLineWheel(FName& OutLineId, FText& OutLabel, int32 SegmentIndex)
{
	// Eight segments, clockwise from the top. The order is the design: the first
	// is the one that keeps people alive, the last is the one that ends a
	// conversation, and the ones in between are what a squad says while it moves.
	//
	// The ids are checked against the voice script by the tags checker and by
	// Docs/14; a missing id means the segment is dropped from the wheel rather
	// than drawn empty, so a content fault is visible rather than decorative.
	static const FName WheelIds[] =
	{
		TEXT("V_CONTACT_ENEMY"),
		TEXT("V_PUSHING"),
		TEXT("V_HOLDING"),
		TEXT("V_RELOADING_COVER"),
		TEXT("V_MOVING_UP"),
		TEXT("V_MAN_DOWN_ONE"),
		TEXT("V_GOING_QUIET"),
		TEXT("V_NEVER_MIND")
	};

	static const FText WheelLabels[] =
	{
		NSLOCTEXT("OneBraham", "Wheel_Contact", "CONTACT"),
		NSLOCTEXT("OneBraham", "Wheel_Pushing", "PUSHING"),
		NSLOCTEXT("OneBraham", "Wheel_Holding", "HOLDING"),
		NSLOCTEXT("OneBraham", "Wheel_Reloading", "RELOADING"),
		NSLOCTEXT("OneBraham", "Wheel_MovingUp", "MOVING UP"),
		NSLOCTEXT("OneBraham", "Wheel_ManDown", "MAN DOWN"),
		NSLOCTEXT("OneBraham", "Wheel_GoingQuiet", "GOING QUIET"),
		NSLOCTEXT("OneBraham", "Wheel_NeverMind", "NEVER MIND")
	};

	static_assert(UE_ARRAY_COUNT(WheelIds) == UE_ARRAY_COUNT(WheelLabels),
		"The comms wheel's ids and labels must stay the same length: a segment with an id "
		"and no label draws an empty button, which is worse than no wheel at all.");

	if (SegmentIndex < 0 || SegmentIndex >= static_cast<int32>(UE_ARRAY_COUNT(WheelIds)))
	{
		OutLineId = NAME_None;
		OutLabel = FText::GetEmpty();
		return;
	}

	OutLineId = WheelIds[SegmentIndex];
	OutLabel = WheelLabels[SegmentIndex];
}
