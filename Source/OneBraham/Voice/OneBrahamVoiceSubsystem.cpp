// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceSubsystem.h"
#include "UI/OneBrahamHUDStringTable.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/GameStateBase.h"
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

	/**
	 * True when the character took damage or fired their held weapon within the
	 * last WindowSeconds. Server-side: both timestamps are authoritative there.
	 */
	bool HasRecentCombatActivity(const AOneBrahamCharacter* Character, float WindowSeconds)
	{
		const UWorld* World = Character != nullptr ? Character->GetWorld() : nullptr;
		if (World == nullptr)
		{
			return false;
		}

		const float Now = World->GetTimeSeconds();

		if (const UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			if (Now - Health->GetLastDamageTime() < WindowSeconds)
			{
				return true;
			}
		}

		if (const UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			if (const AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon())
			{
				if (Now - Weapon->GetLastShotTime() < WindowSeconds)
				{
					return true;
				}
			}
		}

		return false;
	}
}

// ==========================================================================
//  Lifecycle
// ==========================================================================
void UOneBrahamVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));

	// --- Line set ---------------------------------------------------------
	// Three ways to get one, in the order a designer expects: the asset named in
	// Project Settings -> One Braham -> Content, then the conventional path, then
	// the compiled-in table.
	LineSet = UOneBrahamGameSettings::Get().VoiceLineRegistry.LoadSynchronous();

	if (LineSet == nullptr)
	{
		LineSet = LoadObject<UOneBrahamVoiceLineSet>(nullptr,
			TEXT("/Game/OneBraham/Voice/DA_VoiceLines.DA_VoiceLines"));
	}

	if (LineSet == nullptr)
	{
		// Code-only fallback. Same ids, same priorities, no recorded audio: the
		// game still subtitles every line, so no mechanic is ever silent.
		LineSet = NewObject<UOneBrahamVoiceLineSet>(this, TEXT("RuntimeVoiceLineSet"));
	}

	LineSet->EnsureCoreLines();

	// --- Transport config -------------------------------------------------
	// The match-dependent parts (range, relay budget) cannot be read here: a
	// world subsystem initialises before the game state that carries the match
	// settings exists. They are resolved on first use instead - see
	// GetChatConfig and GetProximityRangeCm.
	ChatConfig = FOneBrahamVoiceChatConfig();
	bChatConfigResolved = false;
	LocalMuteIntent.Reset();

	UE_LOG(LogOneBrahamVoice, Log,
		TEXT("[OB.VOICE] Voice subsystem ready - %d line(s); range and relay budget follow the match settings."),
		LineSet->Lines.Num());

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
		? LineSet->PickLine(LineId, Character->Appearance.ArchetypeTag, Random)
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
	UOneBrahamProximityVoiceComponent* VoiceComponent = Character->FindComponentByClass<UOneBrahamProximityVoiceComponent>();
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
		if (Character == nullptr || Character->LifeState != EOneBrahamLifeState::Alive)
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

		if (HasRecentCombatActivity(Character, 20.f))
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
	if (GetWorld() == nullptr || Context == nullptr)
	{
		return;
	}

	// Resolve the player state from whatever we were handed.
	const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Context);

	if (const AController* Controller = Cast<AController>(Context))
	{
		State = Controller->GetPlayerState<AOneBrahamPlayerState>();
	}
	else if (const APawn* Pawn = Cast<APawn>(Context))
	{
		State = Pawn->GetPlayerState<AOneBrahamPlayerState>();
	}

	if (State == nullptr)
	{
		return;
	}

	// The PlayerState's copy owns capture, so push-to-talk works with or without a
	// body. Self-mute and the Off channel are applied there (the device stays
	// closed), and the server applies them again when it sets bIsTransmitting -
	// this used to set the flag only when called with authority, so a remote
	// client's mic icon never lit for anybody.
	if (UOneBrahamProximityVoiceComponent* VoiceComponent = State->GetVoiceComponent())
	{
		VoiceComponent->SetLocalTransmitting(bTransmitting);
	}
}

bool UOneBrahamVoiceSubsystem::IsTransmitting(const AOneBrahamPlayerState* PlayerState) const
{
	return PlayerState != nullptr && PlayerState->bIsTransmitting;
}

FOneBrahamVoiceRouteParty UOneBrahamVoiceSubsystem::MakeRouteParty(const AOneBrahamPlayerState* State, bool bAsListener) const
{
	FOneBrahamVoiceRouteParty Party;
	if (State == nullptr)
	{
		return Party;
	}

	Party.PlayerId = State->GetPlayerId();
	Party.Team = State->Team;
	Party.SquadId = State->SquadId;
	Party.bEliminated = State->IsEliminated();
	Party.bSelfMuted = State->bSelfMuted;
	Party.Channel = State->VoiceChannel;
	Party.MutedPlayerIds = &State->MutedPlayerIds;

	const APawn* LocationPawn = nullptr;

	if (!Party.bEliminated)
	{
		// Alive (or downed): their own body. A corpse never counts - after death
		// the pawn may still be possessed, lying on the carpet.
		LocationPawn = State->GetPawn();
	}
	else if (bAsListener)
	{
		// Eliminated listener: hear from the player being spectated. The server
		// owns SpectatedPlayerId (set in ServerBeginSpectating).
		const AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(State->GetPlayerController());
		if (PC != nullptr && PC->bIsSpectating && PC->SpectatedPlayerId >= 0)
		{
			if (const AGameStateBase* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState() : nullptr)
			{
				for (const APlayerState* Other : GameState->PlayerArray)
				{
					const AOneBrahamPlayerState* OtherState = Cast<AOneBrahamPlayerState>(Other);
					if (OtherState != nullptr && OtherState->GetPlayerId() == PC->SpectatedPlayerId && !OtherState->IsEliminated())
					{
						LocationPawn = OtherState->GetPawn();
						break;
					}
				}
			}
		}
	}

	if (LocationPawn != nullptr)
	{
		Party.bHasLocation = true;
		Party.Location = LocationPawn->GetActorLocation();
	}

	return Party;
}

FOneBrahamVoiceRouteRules UOneBrahamVoiceSubsystem::GetRouteRules() const
{
	FOneBrahamVoiceRouteRules Rules;
	Rules.ProximityRangeCm = GetProximityRangeCm();

	if (const AOneBrahamGameState* GS = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr)
	{
		if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
		{
			Rules.bProximityEnabled = Settings->bProximityVoiceEnabled;
			Rules.bTeamEnabled = Settings->bTeamVoiceEnabled;
		}
	}

	return Rules;
}

bool UOneBrahamVoiceSubsystem::IsVoiceOcclusionEnabled() const
{
	if (const AOneBrahamGameState* GS = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr)
	{
		if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
		{
			return Settings->bVoiceOcclusionEnabled;
		}
	}

	return true;
}

bool UOneBrahamVoiceSubsystem::ShouldHear(const AOneBrahamPlayerState* Listener,
	const AOneBrahamPlayerState* Speaker) const
{
	if (Listener == nullptr || Speaker == nullptr || Listener == Speaker)
	{
		return false;
	}

	EOneBrahamVoiceChannel Channel = EOneBrahamVoiceChannel::None;
	return OneBrahamVoiceRouting::ResolveRoute(MakeRouteParty(Listener, true), MakeRouteParty(Speaker, false),
		GetRouteRules(), Channel);
}

int32 UOneBrahamVoiceSubsystem::CountVoiceOccluders(const FVector& From, const FVector& To, const AActor* IgnoreA,
	const AActor* IgnoreB, int32 MaxSurfaces) const
{
	UWorld* World = GetWorld();
	if (World == nullptr || MaxSurfaces <= 0)
	{
		return 0;
	}

	const FVector Delta = To - From;
	const float Distance = Delta.Size();
	if (Distance <= 1.f)
	{
		return 0;
	}

	const FVector Direction = Delta / Distance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OneBrahamVoiceOcclusion), /*bTraceComplex*/ false);
	Params.bReturnPhysicalMaterial = false;
	Params.AddIgnoredActor(IgnoreA);
	Params.AddIgnoredActor(IgnoreB);

	// The VoiceOccluder channel is authored per mesh: desks and glass do not block
	// voice, but walls, shutters, slabs and lift doors do. That distinction is
	// what makes proximity chat feel physical rather than psychic.
	//
	// A blocking trace stops at its first hit, so surfaces are counted by walking:
	// trace, step a wall's thickness past the hit, trace again. A trace that
	// starts inside the same wall reports a start-penetrating hit, which is the
	// far side of the surface already counted, so it is stepped over uncounted.
	constexpr float StepCm = 25.f;
	FVector Start = From;
	int32 Surfaces = 0;

	for (int32 Iteration = 0; Iteration < MaxSurfaces * 3 && Surfaces < MaxSurfaces; ++Iteration)
	{
		if (((To - Start) | Direction) <= 0.f)
		{
			break;
		}

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, To, ECC_GameTraceChannel7, Params))
		{
			break;
		}

		if (!Hit.bStartPenetrating)
		{
			++Surfaces;
		}

		Start = (Hit.bStartPenetrating ? Start : Hit.ImpactPoint) + Direction * StepCm;
	}

	return Surfaces;
}

float UOneBrahamVoiceSubsystem::ComputeOcclusionAlpha(const FVector& SpeakerLocation,
	const FVector& ListenerLocation) const
{
	const float Distance = FVector::Dist(SpeakerLocation, ListenerLocation);
	if (Distance <= 250.f)
	{
		return 0.f; // same room, always clear
	}

	const int32 Surfaces = CountVoiceOccluders(SpeakerLocation, ListenerLocation);
	if (Surfaces == 0)
	{
		// Long-distance falloff even with a clear line: a shout does not carry
		// across an entire open-plan floor unchanged.
		const float Range = FMath::Max(500.f, GetProximityRangeCm());
		return FMath::Clamp(Distance / Range, 0.f, 1.f) * 0.5f;
	}

	// Each blocking surface adds more muffle, up to fully unintelligible.
	return FMath::Clamp(0.55f + 0.2f * static_cast<float>(Surfaces - 1), 0.f, 1.f);
}

float UOneBrahamVoiceSubsystem::GetProximityRangeCm() const
{
	// The match settings asset is authoritative whenever it names a range. It is
	// read live rather than cached at Initialize, because the game state that
	// carries it does not exist yet when this subsystem starts - which is why the
	// match override used to never apply.
	float MatchRange = 0.f;

	if (const UWorld* World = GetWorld())
	{
		if (const AOneBrahamGameState* GS = World->GetGameState<AOneBrahamGameState>())
		{
			if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
			{
				MatchRange = Settings->ProximityVoiceRangeCm;
			}
		}
	}

	return OneBrahamVoiceRouting::ResolveProximityRangeCm(MatchRange, ChatConfig.ProximityRangeCm,
		UOneBrahamGameSettings::Get().VoiceMaxDistanceCm);
}

const FOneBrahamVoiceChatConfig& UOneBrahamVoiceSubsystem::GetChatConfig() const
{
	ResolveChatConfigFromMatch();
	return ChatConfig;
}

void UOneBrahamVoiceSubsystem::ResolveChatConfigFromMatch() const
{
	if (bChatConfigResolved)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	const UOneBrahamMatchSettingsAsset* Settings = GS != nullptr ? GS->GetMatchSettings() : nullptr;

	if (Settings == nullptr)
	{
		return; // try again on the next use
	}

	// Bigger rooms get a slightly wider relay budget: a 12-player TDM uses a lot
	// more talking than three squads creeping up a stairwell.
	const int32 ExtraFrames = Settings->GetExpectedPlayerCount() > 12 ? 20 : 10;
	ChatConfig.MaxFramesPerSecond = FMath::Clamp(ChatConfig.SendFramesPerSecond + ExtraFrames, 40, 120);
	bChatConfigResolved = true;

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Match voice: proximity %.0f cm, relay budget %d frames/s."),
		GetProximityRangeCm(), ChatConfig.MaxFramesPerSecond);
}

bool UOneBrahamVoiceSubsystem::ConsumeRelayBudget(APlayerState* Talker, int32 FrameBytes)
{
	if (Talker == nullptr)
	{
		return false;
	}

	// Clamp defensively: a modified client can send any size, and the budget is
	// the last line of defence before the server relays it to five other players.
	const int32 ClampedBytes = FMath::Clamp(FrameBytes, 0, ONE_BRAHAM_MAX_VOICE_FRAME_BYTES);

	UWorld* World = GetWorld();
	const float Now = World != nullptr ? World->GetTimeSeconds() : 0.f;

	ResolveChatConfigFromMatch();

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
	// The three names the HUD shows come from the copy table, so the wheel and
	// the scoreboard cannot disagree with it about what a channel is called. The
	// building's own channel and the off state have no key: nothing but the
	// voice wheel ever displays them.
	case EOneBrahamVoiceChannel::Proximity:		return OneBrahamHUDStrings::Get(TEXT("HUD_Voice_Proximity"));
	case EOneBrahamVoiceChannel::Squad:			return OneBrahamHUDStrings::Get(TEXT("HUD_Voice_Squad"));
	case EOneBrahamVoiceChannel::Team:			return OneBrahamHUDStrings::Get(TEXT("HUD_Voice_Team"));
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
	// Eight segments, clockwise from the top, every one of them a line the script
	// already had. The wheel was built by reading Docs/14 rather than by inventing
	// ids and hoping: seven of the eight existed before the wheel did, and the
	// eighth - a bystander calling out a downed squadmate - was the one genuine
	// hole in the script, so it was written rather than worked around.
	//
	// A segment whose line is missing is dropped from the wheel rather than drawn
	// empty, so a content fault is visible instead of decorative.
	static const FName WheelIds[] =
	{
		TEXT("V_CONTACT_ENEMY"),     // "Contact. One."
		TEXT("V_PUSH_NOW"),          // "Now. Go now."
		TEXT("V_HOLD_POSITION"),     // "Hold here. Let them come to us."
		TEXT("V_RELOADING"),         // "Reloading - cover me."
		TEXT("V_PING_MOVE"),         // "Move up."
		TEXT("V_MAN_DOWN"),          // "Somebody's down - nearest one, get to him."
		TEXT("V_QUIET"),             // "Quiet - I heard something in the ceiling."
		TEXT("V_ACK_NEGATIVE")       // "No. Not that way."
	};

	static const FText WheelLabels[] =
	{
		NSLOCTEXT("OneBraham", "Wheel_Contact", "CONTACT"),
		NSLOCTEXT("OneBraham", "Wheel_Push", "PUSH"),
		NSLOCTEXT("OneBraham", "Wheel_Hold", "HOLD"),
		NSLOCTEXT("OneBraham", "Wheel_Reloading", "RELOADING"),
		NSLOCTEXT("OneBraham", "Wheel_MovingUp", "MOVING UP"),
		NSLOCTEXT("OneBraham", "Wheel_ManDown", "MAN DOWN"),
		NSLOCTEXT("OneBraham", "Wheel_Quiet", "QUIET"),
		NSLOCTEXT("OneBraham", "Wheel_Negative", "NEGATIVE")
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

// ==========================================================================
//  Per-player mute
// ==========================================================================
AOneBrahamPlayerState* UOneBrahamVoiceSubsystem::GetLocalPlayerState() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	return PC != nullptr && PC->IsLocalController() ? PC->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
}

bool UOneBrahamVoiceSubsystem::SetPlayerMuted(APlayerState* Target, bool bMuted)
{
	AOneBrahamPlayerState* LocalState = GetLocalPlayerState();
	if (LocalState == nullptr || Target == nullptr || Target == LocalState)
	{
		return false;
	}

	const int32 TargetId = Target->GetPlayerId();
	LocalMuteIntent.Add(TargetId, bMuted);

	// Silence them here at once - the frames already in flight would otherwise
	// play for a round trip after the click - and ask the server to stop relaying.
	if (UOneBrahamProximityVoiceComponent* VoiceComponent = LocalState->GetVoiceComponent())
	{
		VoiceComponent->SetSpeakerMutedLocally(TargetId, bMuted);
	}

	LocalState->ServerSetPlayerMuted(TargetId, bMuted);

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] %s '%s'."),
		bMuted ? TEXT("Muted") : TEXT("Unmuted"), *Target->GetPlayerName());

	return true;
}

bool UOneBrahamVoiceSubsystem::IsPlayerMuted(const APlayerState* Target) const
{
	if (Target == nullptr)
	{
		return false;
	}

	if (const bool* Intent = LocalMuteIntent.Find(Target->GetPlayerId()))
	{
		return *Intent;
	}

	const AOneBrahamPlayerState* LocalState = GetLocalPlayerState();
	return LocalState != nullptr && LocalState->MutedPlayerIds.Contains(Target->GetPlayerId());
}

APlayerState* UOneBrahamVoiceSubsystem::FindPlayerByName(const FString& Name) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr || Name.IsEmpty())
	{
		return nullptr;
	}

	APlayerState* PrefixMatch = nullptr;
	int32 PrefixMatches = 0;

	for (APlayerState* State : GameState->PlayerArray)
	{
		if (State == nullptr)
		{
			continue;
		}

		const FString PlayerName = State->GetPlayerName();
		if (PlayerName.Equals(Name, ESearchCase::IgnoreCase))
		{
			return State;
		}

		if (PlayerName.StartsWith(Name, ESearchCase::IgnoreCase))
		{
			PrefixMatch = State;
			++PrefixMatches;
		}
	}

	// A prefix that fits two players is refused rather than guessed: muting the
	// wrong person is worse than asking for the full name.
	return PrefixMatches == 1 ? PrefixMatch : nullptr;
}

namespace
{
	void HandleMuteCommand(const TArray<FString>& Args, UWorld* World, bool bMute)
	{
		UOneBrahamVoiceSubsystem* Voice = World != nullptr ? World->GetSubsystem<UOneBrahamVoiceSubsystem>() : nullptr;
		if (Voice == nullptr)
		{
			UE_LOG(LogOneBrahamVoice, Warning, TEXT("[OB.VOICE] No voice subsystem in this world."));
			return;
		}

		// Names may contain spaces; the console splits on them, so rejoin.
		const FString Name = FString::Join(Args, TEXT(" ")).TrimStartAndEnd().TrimQuotes();
		if (Name.IsEmpty())
		{
			UE_LOG(LogOneBrahamVoice, Display, TEXT("[OB.VOICE] Usage: ob.Voice.%s <PlayerName>"),
				bMute ? TEXT("Mute") : TEXT("Unmute"));
			return;
		}

		APlayerState* Target = Voice->FindPlayerByName(Name);
		if (Target == nullptr)
		{
			UE_LOG(LogOneBrahamVoice, Display, TEXT("[OB.VOICE] No single player matches '%s'."), *Name);
			return;
		}

		if (!Voice->SetPlayerMuted(Target, bMute))
		{
			UE_LOG(LogOneBrahamVoice, Display, TEXT("[OB.VOICE] Cannot %s '%s' (no local player, or that is you)."),
				bMute ? TEXT("mute") : TEXT("unmute"), *Target->GetPlayerName());
		}
	}

	// Minimal player-facing mute until the scoreboard has a button: the console
	// is available in every build that ships with one, and the command goes
	// through the same SetPlayerMuted path a button would.
	FAutoConsoleCommandWithWorldAndArgs GVoiceMuteCommand(
		TEXT("ob.Voice.Mute"),
		TEXT("Mutes a player's voice chat for you. Usage: ob.Voice.Mute <PlayerName> (exact name, or a unique prefix)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			HandleMuteCommand(Args, World, true);
		}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceUnmuteCommand(
		TEXT("ob.Voice.Unmute"),
		TEXT("Unmutes a player's voice chat for you. Usage: ob.Voice.Unmute <PlayerName>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			HandleMuteCommand(Args, World, false);
		}));
}
