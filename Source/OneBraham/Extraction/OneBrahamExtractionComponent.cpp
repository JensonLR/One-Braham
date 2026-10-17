// Copyright (c) One Braham. All Rights Reserved.

#include "Extraction/OneBrahamExtractionComponent.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

namespace
{
	/** Fallback copy so the component works before the settings asset is cooked. */
	constexpr float OBFallbackBroadcastSeconds = 10.f;
	constexpr float OBFallbackArrivalSeconds = OneBrahamConstants::HelicopterArrivalSeconds;
	constexpr float OBFallbackHoldSeconds = OneBrahamConstants::ExtractionHoldSeconds;
	constexpr float OBFallbackLoiterSeconds = 75.f;

	/** How fast the hold bar drains when the claiming squad has nobody on the pad. */
	constexpr float OBHoldDecayRate = 0.5f;

	/** How long the "FAILED" banner sits before the terminals accept a new call. */
	constexpr float OBFailedResetSeconds = 5.f;

	/** Replicated countdown is written at 4 Hz; clients interpolate between packets. */
	constexpr float OBReplicationInterval = 0.25f;

	/** ob.Extraction.Fast: the arrival and the hold, in seconds. */
	constexpr float OBFastSeconds = 3.f;

#if !UE_BUILD_SHIPPING
	// Dev only: compress the winch for a full-match test run. -1 = the match
	// settings' value. Read when used, so -ExecCmds after the map loads applies.
	TAutoConsoleVariable<float> CVarBroadcastSeconds(TEXT("ob.Extraction.BroadcastSeconds"), -1.f,
		TEXT("Dev: seconds of head start after a winch call (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarArrivalSeconds(TEXT("ob.Extraction.ArrivalSeconds"), -1.f,
		TEXT("Dev: seconds of helicopter arrival (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarHoldSeconds(TEXT("ob.Extraction.HoldSeconds"), -1.f,
		TEXT("Dev: seconds of uncontested hold to extract (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarLoiterSeconds(TEXT("ob.Extraction.LoiterSeconds"), -1.f,
		TEXT("Dev: seconds the helicopter waits over the pad (-1 = match settings)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarBotCalls(TEXT("ob.Extraction.BotCalls"), 1,
		TEXT("Dev: 0 stops bots calling the winch from the pad (to see the other endings)."), ECVF_Cheat);

	/** A dev override when one is set, else the value given. */
	float OBTiming(const TAutoConsoleVariable<float>& Override, float Value)
	{
		const float Forced = Override.GetValueOnGameThread();
		return Forced >= 0.f ? Forced : Value;
	}

	/** ob.Extraction.Fast, registered by the module (OneBraham.cpp). */
	bool OBFastExtraction()
	{
		static const IConsoleVariable* const Fast = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Extraction.Fast"));
		return Fast != nullptr && Fast->GetInt() != 0;
	}
#endif
}

UOneBrahamExtractionComponent::UOneBrahamExtractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UOneBrahamExtractionComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedGameState = Cast<AOneBrahamGameState>(GetOwner());

	if (const AOneBrahamGameState* GS = CachedGameState.Get())
	{
		if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
		{
			BroadcastDurationSeconds = FMath::RoundToInt(Settings->ExtractionBroadcastSeconds > 0.f ? Settings->ExtractionBroadcastSeconds : OBFallbackBroadcastSeconds);
			HoldRadiusCm = Settings->ExtractionZoneRadiusCm > 0.f ? Settings->ExtractionZoneRadiusCm : HoldRadiusCm;
		}
	}
}

void UOneBrahamExtractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamExtractionComponent, ExtractionState);
	DOREPLIFETIME(UOneBrahamExtractionComponent, ClaimingSquadId);
	DOREPLIFETIME(UOneBrahamExtractionComponent, HoldingSquadId);
	DOREPLIFETIME(UOneBrahamExtractionComponent, HoldProgress01);
	DOREPLIFETIME(UOneBrahamExtractionComponent, SecondsRemainingInState);
	DOREPLIFETIME(UOneBrahamExtractionComponent, bContested);
	DOREPLIFETIME(UOneBrahamExtractionComponent, ArrivalAlpha01);
	DOREPLIFETIME(UOneBrahamExtractionComponent, HelipadLocation);
	DOREPLIFETIME(UOneBrahamExtractionComponent, HoldRadiusCm);
	DOREPLIFETIME(UOneBrahamExtractionComponent, BroadcastDurationSeconds);
	DOREPLIFETIME(UOneBrahamExtractionComponent, MatchEnding);
	DOREPLIFETIME(UOneBrahamExtractionComponent, EndingWinningSquadId);
	DOREPLIFETIME(UOneBrahamExtractionComponent, LeaveServerTime);
	DOREPLIFETIME(UOneBrahamExtractionComponent, PostMatchAction);
}

// ===========================================================================
//  Timings
// ===========================================================================

float UOneBrahamExtractionComponent::GetBroadcastSeconds() const
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;
	float Seconds = Settings != nullptr && Settings->ExtractionBroadcastSeconds > 0.f ? Settings->ExtractionBroadcastSeconds : OBFallbackBroadcastSeconds;
#if !UE_BUILD_SHIPPING
	Seconds = OBTiming(CVarBroadcastSeconds, Seconds);
#endif
	return FMath::Max(0.f, Seconds);
}

float UOneBrahamExtractionComponent::GetArrivalSeconds() const
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;
	float Seconds = Settings != nullptr ? Settings->HelicopterArrivalSeconds : OBFallbackArrivalSeconds;
#if !UE_BUILD_SHIPPING
	Seconds = OBFastExtraction() ? OBFastSeconds : OBTiming(CVarArrivalSeconds, Seconds);
#endif
	return FMath::Max(0.f, Seconds);
}

float UOneBrahamExtractionComponent::GetHoldSeconds() const
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;
	float Seconds = Settings != nullptr ? Settings->ExtractionHoldSeconds : OBFallbackHoldSeconds;
#if !UE_BUILD_SHIPPING
	Seconds = OBFastExtraction() ? OBFastSeconds : OBTiming(CVarHoldSeconds, Seconds);
#endif
	return FMath::Max(1.f, Seconds);
}

float UOneBrahamExtractionComponent::GetLoiterSeconds() const
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;
	float Seconds = Settings != nullptr ? Settings->HelicopterLoiterSeconds : OBFallbackLoiterSeconds;
#if !UE_BUILD_SHIPPING
	Seconds = OBTiming(CVarLoiterSeconds, Seconds);
#endif
	// The pilot always waits at least as long as a clean hold takes.
	return FMath::Max(GetHoldSeconds() + 1.f, Seconds);
}

// ===========================================================================
//  Tick
// ===========================================================================

void UOneBrahamExtractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr || GS->MatchMode != EOneBrahamMatchMode::Ascent)
	{
		// FIRE DRILL and NIGHT SHIFT have their own win conditions.
		return;
	}

	if (GetOwnerRole() != ROLE_Authority)
	{
		// Clients advance the countdown locally so the HUD reads as a smooth clock,
		// and snap back to the authoritative value every time a packet lands.
		if (ExtractionState != EOneBrahamExtractionState::Inactive && ExtractionState != EOneBrahamExtractionState::Failed)
		{
			SecondsRemainingInState = FMath::Max(0.f, SecondsRemainingInState - DeltaTime);

			const float ArrivalWindow = static_cast<float>(BroadcastDurationSeconds) + GetArrivalSeconds();
			if (ExtractionState == EOneBrahamExtractionState::Broadcast || ExtractionState == EOneBrahamExtractionState::Arriving)
			{
				ArrivalAlpha01 = FMath::Clamp(ArrivalAlpha01 + DeltaTime / FMath::Max(1.f, ArrivalWindow), 0.f, 1.f);
			}
		}

		// After the interpolation, so the rotor's volume is driven by the same
		// number the floodlight on the pad is using.
		ApplyLocalExtractionAudio();
		return;
	}

	ApplyLocalExtractionAudio();

	// The match is over: the winch stays where the ending left it.
	if (MatchEnding != EOneBrahamMatchEnding::None)
	{
		return;
	}

	TimeInState += DeltaTime;
	ReplicationAccumulator += DeltaTime;

	const float ArrivalSeconds = GetArrivalSeconds();
	const float LoiterSeconds = GetLoiterSeconds();

	// The squad that called it is gone, in whichever live state: during the
	// broadcast and the arrival too, not only on the pad. An attempt nobody can
	// finish otherwise held the match open until the helicopter loitered out.
	if (IsExtractionActive() && !IsClaimingSquadAlive())
	{
		ServerAbortExtraction(EOneBrahamExtractionFailReason::CallersEliminated);
		return;
	}

	switch (ExtractionState)
	{
	case EOneBrahamExtractionState::Inactive:
		ServerConsiderBotCalls(DeltaTime);
		return;

	case EOneBrahamExtractionState::Complete:
		return;

	case EOneBrahamExtractionState::Broadcast:
	{
		const float Remaining = static_cast<float>(BroadcastDurationSeconds) - TimeInState;
		if (Remaining <= 0.f)
		{
			ServerSetState(EOneBrahamExtractionState::Arriving);
			break;
		}
		break;
	}

	case EOneBrahamExtractionState::Arriving:
	{
		const float ArrivalWindow = FMath::Max(1.f, static_cast<float>(BroadcastDurationSeconds) + ArrivalSeconds);
		const float Elapsed = static_cast<float>(BroadcastDurationSeconds) + TimeInState;
		const float Remaining = ArrivalSeconds - TimeInState;

		if (Remaining <= 0.f)
		{
			ServerSetState(EOneBrahamExtractionState::Holding);
			break;
		}

		// The beacon sweeps faster as the helicopter closes in.
		ArrivalAlpha01 = FMath::Clamp(Elapsed / ArrivalWindow, 0.f, 1.f);
		break;
	}

	case EOneBrahamExtractionState::Holding:
	{
		ServerEvaluateHold(DeltaTime);

		// A hold can still end after the component moved on to Complete/Failed.
		if (ExtractionState != EOneBrahamExtractionState::Holding)
		{
			return;
		}

		// Bought time ran out: the pilot is not hanging around for a losing fight.
		if (TimeInState >= LoiterSeconds)
		{
			ServerAbortExtraction(EOneBrahamExtractionFailReason::LoiterExpired);
			return;
		}

		if (HoldProgress01 >= 1.f)
		{
			HoldProgress01 = 1.f;
			ServerSetState(EOneBrahamExtractionState::Complete);
		}
		break;
	}

	case EOneBrahamExtractionState::Failed:
	{
		// Give the lobby five seconds of "WINCH ABORTED" before the pad re-arms.
		if (TimeInState >= OBFailedResetSeconds)
		{
			ServerSetState(EOneBrahamExtractionState::Inactive);
		}
		break;
	}

	default:
		break;
	}

	// Publish the clock to clients at 4 Hz rather than every tick.
	if (ReplicationAccumulator >= OBReplicationInterval && ExtractionState != EOneBrahamExtractionState::Inactive)
	{
		float Published = 0.f;
		switch (ExtractionState)
		{
		case EOneBrahamExtractionState::Broadcast:
			Published = FMath::Max(0.f, static_cast<float>(BroadcastDurationSeconds) - TimeInState);
			break;
		case EOneBrahamExtractionState::Arriving:
			Published = FMath::Max(0.f, ArrivalSeconds - TimeInState);
			break;
		case EOneBrahamExtractionState::Holding:
			Published = FMath::Max(0.f, LoiterSeconds - TimeInState);
			break;
		default:
			break;
		}

		SecondsRemainingInState = Published;
		ReplicationAccumulator = 0.f;
	}
}

// ===========================================================================
//  Calling
// ===========================================================================

bool UOneBrahamExtractionComponent::ServerCallExtraction(int32 SquadId, AOneBrahamPlayerState* CallingPlayer, FText& OutRefusal)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_ServerOnly", "Only the host can authorise a winch.");
		return false;
	}

	AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_NoState", "The building is not responding.");
		return false;
	}

	if (GS->MatchMode != EOneBrahamMatchMode::Ascent)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_WrongMode", "No winch is assigned to this exercise.");
		return false;
	}

	const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings();
	if (Settings != nullptr && !Settings->bExtractionEnabled)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_Disabled", "The winch has been taken out of service.");
		return false;
	}

	if (ExtractionState != EOneBrahamExtractionState::Inactive)
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_AlreadyLive", "A winch request is already active. Listen for the helicopter.");
		return false;
	}

	if (SquadId < 0 || SquadId >= GS->Squads.Num())
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_BadSquad", "You are not on a floor with a winch allowance.");
		return false;
	}

	if (GS->IsSquadEliminated(SquadId))
	{
		OutRefusal = NSLOCTEXT("OneBraham", "Extraction_SquadDead", "Your team has been stood down.");
		return false;
	}

	// The terminal is on floor 15; nobody calls the winch from the canteen.
	if (CallingPlayer != nullptr)
	{
		const APawn* Pawn = CallingPlayer->GetPawn();
		// One storey below the Skydeck, from the building's own storey height
		// rather than a literal 400: this was the second place the number was
		// written down, and the constant exists so there is only one.
		const float SkydeckMinZ = static_cast<float>(OneBrahamConstants::FloorSkydeck - 1)
			* OneBrahamConstants::FloorHeightCm;
		if (Pawn == nullptr || Pawn->GetActorLocation().Z < SkydeckMinZ)
		{
			OutRefusal = NSLOCTEXT("OneBraham", "Extraction_TooLow", "Winch terminals are on the Skydeck, fifteenth floor.");
			return false;
		}
	}

	// The head start is fixed at the call, so the countdown every machine draws
	// is the one this call was given.
	BroadcastDurationSeconds = FMath::Max(1, FMath::RoundToInt(GetBroadcastSeconds()));

	ClaimingSquadId = SquadId;
	HoldingSquadId = INDEX_NONE;
	BotPadSeconds.Reset();
	HoldProgress01 = 0.f;
	ArrivalAlpha01 = 0.f;
	UncontestedHoldTime = 0.f;
	LastFailReason = EOneBrahamExtractionFailReason::None;

	for (FOneBrahamSquadInfo& Squad : GS->Squads)
	{
		Squad.bHasExtractionClaim = (Squad.SquadId == SquadId);
	}
	GS->ForceNetUpdate();

	const int32 CallerPlayerId = CallingPlayer ? CallingPlayer->GetPlayerId() : INDEX_NONE;
	ServerSetState(EOneBrahamExtractionState::Broadcast);

	MulticastExtractionStateChanged(EOneBrahamExtractionState::Broadcast, SquadId,
		static_cast<float>(BroadcastDurationSeconds), CallerPlayerId);

	OB_MATCH_LOG("Squad %d called the winch (caller=%d, broadcast=%ds at pad %s)",
		SquadId, CallerPlayerId, BroadcastDurationSeconds, *HelipadLocation.ToCompactString());

	return true;
}

void UOneBrahamExtractionComponent::ServerSetHelipadLocation(const FVector& Location, float InHoldRadiusCm)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	HelipadLocation = Location;
	HoldRadiusCm = FMath::Max(200.f, InHoldRadiusCm);
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	OB_MATCH_LOG("Winch pad set to %s (radius %.0f cm)", *Location.ToCompactString(), HoldRadiusCm);
}

void UOneBrahamExtractionComponent::ServerSkipToHolding()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	if (ClaimingSquadId == INDEX_NONE)
	{
		return;
	}

	TimeInState = 0.f;
	ArrivalAlpha01 = 1.f;
	HoldProgress01 = 0.f;
	ServerSetState(EOneBrahamExtractionState::Holding);
	MulticastExtractionStateChanged(EOneBrahamExtractionState::Holding, ClaimingSquadId, -1.f, INDEX_NONE);
}

void UOneBrahamExtractionComponent::ServerAbortExtraction(EOneBrahamExtractionFailReason Reason)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	if (ExtractionState == EOneBrahamExtractionState::Inactive || ExtractionState == EOneBrahamExtractionState::Failed)
	{
		return;
	}

	const int32 FailedSquad = ClaimingSquadId;
	LastFailReason = Reason;

	if (AOneBrahamGameState* GS = CachedGameState.Get())
	{
		for (FOneBrahamSquadInfo& Squad : GS->Squads)
		{
			Squad.bHasExtractionClaim = false;
		}
		GS->ForceNetUpdate();
	}

	HoldingSquadId = INDEX_NONE;
	bContested = false;
	UncontestedHoldTime = 0.f;

	ServerSetState(EOneBrahamExtractionState::Failed);
	MulticastExtractionStateChanged(EOneBrahamExtractionState::Failed, FailedSquad, -1.f, INDEX_NONE);

	if (OnExtractionFailed.IsBound())
	{
		OnExtractionFailed.Broadcast(FailedSquad, Reason);
	}

	OB_MATCH_LOG("Winch attempt by squad %d FAILED (%s)",
		FailedSquad, *UEnum::GetValueAsString(Reason));
}

// ===========================================================================
//  State machine
// ===========================================================================

void UOneBrahamExtractionComponent::ServerSetState(EOneBrahamExtractionState NewState)
{
	if (ExtractionState == NewState)
	{
		return;
	}

	ExtractionState = NewState;
	TimeInState = 0.f;
	ReplicationAccumulator = OBReplicationInterval;

	switch (NewState)
	{
	case EOneBrahamExtractionState::Broadcast:
		SecondsRemainingInState = static_cast<float>(BroadcastDurationSeconds);
		break;
	case EOneBrahamExtractionState::Arriving:
		SecondsRemainingInState = GetArrivalSeconds();
		break;
	case EOneBrahamExtractionState::Holding:
		SecondsRemainingInState = GetLoiterSeconds();
		HoldingSquadId = ClaimingSquadId;
		break;
	case EOneBrahamExtractionState::Complete:
		SecondsRemainingInState = 0.f;
		ArrivalAlpha01 = 1.f;
		break;
	case EOneBrahamExtractionState::Inactive:
		ClaimingSquadId = INDEX_NONE;
		HoldingSquadId = INDEX_NONE;
		HoldProgress01 = 0.f;
		ArrivalAlpha01 = 0.f;
		bContested = false;
		SecondsRemainingInState = 0.f;
		break;
	default:
		break;
	}

	OnRep_ExtractionState();

	if (NewState == EOneBrahamExtractionState::Complete)
	{
		const AOneBrahamGameState* GS = CachedGameState.Get();
		const EOneBrahamTeam WinningTeam = (GS && GS->Squads.IsValidIndex(ClaimingSquadId))
			? GS->Squads[ClaimingSquadId].Team
			: EOneBrahamTeam::None;

		OB_MATCH_LOG("Squad %d is on the wire. Match won by %s.", ClaimingSquadId, *UEnum::GetValueAsString(WinningTeam));

		if (OnExtractionComplete.IsBound())
		{
			OnExtractionComplete.Broadcast(ClaimingSquadId, WinningTeam);
		}
	}
}

void UOneBrahamExtractionComponent::ServerEvaluateHold(float DeltaTime)
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr)
	{
		return;
	}

	const float HoldSeconds = GetHoldSeconds();

	CountPlayersInZone(ClaimantsInZone, ContestersInZone);

	const bool bHadContest = bContested;
	bContested = (ContestersInZone > 0);

	if (ClaimantsInZone > 0 && !bContested)
	{
		HoldProgress01 = FMath::Clamp(HoldProgress01 + DeltaTime / HoldSeconds, 0.f, 1.f);
		UncontestedHoldTime += DeltaTime;
		HoldingSquadId = ClaimingSquadId;
	}
	else if (ClaimantsInZone > 0 && bContested)
	{
		// Contested time is bought time, not lost time: progress freezes.
		if (!bHadContest)
		{
			OB_MATCH_LOG("Winch CONTESTED: %d attackers on the pad (%d defenders holding)",
				ContestersInZone, ClaimantsInZone);
		}
		HoldingSquadId = ClaimingSquadId;
	}
	else
	{
		// Nobody on the wire: the winch line slips back at half rate.
		HoldProgress01 = FMath::Clamp(HoldProgress01 - (DeltaTime * OBHoldDecayRate) / HoldSeconds, 0.f, 1.f);
		HoldingSquadId = INDEX_NONE;
	}
}

/** True while the deck has gone loud and red: contest must persist a moment. */
bool UOneBrahamExtractionComponent::IsDeckAlarmActive() const
{
	return bContested || ExtractionState == EOneBrahamExtractionState::Holding;
}

bool UOneBrahamExtractionComponent::IsClaimingSquadAlive() const
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr || ClaimingSquadId == INDEX_NONE)
	{
		return false;
	}

	return !GS->IsSquadEliminated(ClaimingSquadId);
}

void UOneBrahamExtractionComponent::ServerConsiderBotCalls(float DeltaTime)
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr || GS->MatchPhase != EOneBrahamMatchPhase::Active)
	{
		BotPadSeconds.Reset();
		return;
	}

#if !UE_BUILD_SHIPPING
	if (CVarBotCalls.GetValueOnGameThread() == 0)
	{
		return;
	}
#endif

	const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings();
	if (Settings != nullptr && !Settings->bExtractionEnabled)
	{
		return;
	}

	// One bot per squad on the pad is enough to call it; the first squad to have
	// stood there long enough calls it, and only one squad can.
	TMap<int32, AOneBrahamPlayerState*> OnPad;
	const float RadiusSquared = HoldRadiusCm * HoldRadiusCm;

	for (APlayerState* BaseState : GS->PlayerArray)
	{
		AOneBrahamPlayerState* PS = Cast<AOneBrahamPlayerState>(BaseState);
		const AController* Owner = PS != nullptr ? Cast<AController>(PS->GetOwner()) : nullptr;
		const bool bBot = PS != nullptr && (PS->IsABot() || (Owner != nullptr && Cast<APlayerController>(Owner) == nullptr));
		if (!bBot || !PS->IsAlive())
		{
			continue;
		}

		const APawn* Pawn = PS->GetPawn();
		if (Pawn == nullptr)
		{
			continue;
		}

		const FVector Delta = Pawn->GetActorLocation() - HelipadLocation;
		if (FVector::DistSquared2D(Delta, FVector::ZeroVector) > RadiusSquared
			|| FMath::Abs(Delta.Z) > OneBrahamConstants::FloorHeightCm)
		{
			continue;
		}

		const int32 SquadId = GS->GetSquadIdForPlayer(PS->GetPlayerId());
		if (SquadId != INDEX_NONE && !GS->IsSquadEliminated(SquadId))
		{
			OnPad.Add(SquadId, PS);
		}
	}

	for (auto It = BotPadSeconds.CreateIterator(); It; ++It)
	{
		if (!OnPad.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	for (const TPair<int32, AOneBrahamPlayerState*>& Pair : OnPad)
	{
		float& Seconds = BotPadSeconds.FindOrAdd(Pair.Key);
		Seconds += DeltaTime;
		if (Seconds < BotCallSeconds)
		{
			continue;
		}

		FText Refusal;
		if (ServerCallExtraction(Pair.Key, Pair.Value, Refusal))
		{
			OB_EXTRACT_LOG("Bot '%s' keyed the winch from the pad for squad %d.", *Pair.Value->GetPlayerName(), Pair.Key);
			return;
		}

		// Refused (a lockout, a squad the server no longer has): try again later
		// rather than every tick.
		Seconds = 0.f;
		OB_EXTRACT_LOG("Bot '%s' could not call the winch: %s", *Pair.Value->GetPlayerName(), *Refusal.ToString());
	}
}

void UOneBrahamExtractionComponent::ServerRecordMatchEnding(EOneBrahamMatchEnding Ending, int32 WinningSquadId, double InLeaveServerTime,
	EOneBrahamPostMatchAction InPostMatchAction)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	MatchEnding = Ending;
	EndingWinningSquadId = WinningSquadId;
	LeaveServerTime = InLeaveServerTime;
	PostMatchAction = static_cast<uint8>(InPostMatchAction);
	BotPadSeconds.Reset();

	// An attempt still in the air ends with the match, quietly: the ending has
	// been announced, and a "winch withdrawn" after it would contradict it.
	if (IsExtractionActive() || ExtractionState == EOneBrahamExtractionState::Failed)
	{
		bContested = false;
		ServerSetState(EOneBrahamExtractionState::Inactive);
	}

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	OB_EXTRACT_LOG("Match ending recorded: %s (winning squad %d).", *OneBrahamMatchEnd::LogNameFor(Ending), WinningSquadId);
}

void UOneBrahamExtractionComponent::CountPlayersInZone(int32& OutClaimants, int32& OutContesters) const
{
	OutClaimants = 0;
	OutContesters = 0;

	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr)
	{
		return;
	}

	const float RadiusSquared = HoldRadiusCm * HoldRadiusCm;

	for (APlayerState* BaseState : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* PS = Cast<AOneBrahamPlayerState>(BaseState);
		if (PS == nullptr || !PS->IsAlive())
		{
			// Only players on their feet hold the wire or break a hold. Downed
			// players are a body on the pad that a squadmate has to stand over.
			continue;
		}

		const APawn* Pawn = PS->GetPawn();
		if (Pawn == nullptr)
		{
			continue;
		}

		const FVector Delta = Pawn->GetActorLocation() - HelipadLocation;
		if (FVector::DistSquared2D(Delta, FVector::ZeroVector) > RadiusSquared)
		{
			continue;
		}

		// Vertical check: the deck below the pad does not count. One storey, from
		// the building's storey height.
		if (FMath::Abs(Delta.Z) > OneBrahamConstants::FloorHeightCm)
		{
			continue;
		}

		if (GS->GetSquadIdForPlayer(PS->GetPlayerId()) == ClaimingSquadId)
		{
			++OutClaimants;
		}
		else
		{
			++OutContesters;
		}
	}
}

void UOneBrahamExtractionComponent::ApplyLocalExtractionAudio()
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Active means the helicopter is on its way or on the wire. COMPLETE drops the
	// rotor (it is climbing out, and `SFX_Helicopter_Leave` covers it); FAILED and
	// INACTIVE drop it too, because an aborted run has to sound like a lift home.
	const bool bActive = IsExtractionActive();

	// Half a percent of the flight is the smallest change anyone could hear, and
	// this runs at 10 Hz on every machine in the match.
	const bool bApproachMoved = FMath::Abs(ArrivalAlpha01 - PushedApproach01) >= 0.005f;
	if (bActive == bPushedExtractionAudio && !bApproachMoved)
	{
		return;
	}

	AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>();
	UOneBrahamAudioManagerComponent* Audio = PC ? PC->GetAudioManager() : nullptr;
	if (Audio == nullptr)
	{
		return;
	}

	Audio->SetExtractionAudio(bActive, HelipadLocation, ArrivalAlpha01);
	bPushedExtractionAudio = bActive;
	PushedApproach01 = ArrivalAlpha01;
}

bool UOneBrahamExtractionComponent::IsExtractionActive() const
{
	return ExtractionState == EOneBrahamExtractionState::Broadcast
		|| ExtractionState == EOneBrahamExtractionState::Arriving
		|| ExtractionState == EOneBrahamExtractionState::Holding;
}

bool UOneBrahamExtractionComponent::IsSquadHoldingExtraction(int32 SquadId) const
{
	return SquadId != INDEX_NONE && SquadId == HoldingSquadId;
}

// ===========================================================================
//  Replication + feedback
// ===========================================================================

void UOneBrahamExtractionComponent::OnRep_ExtractionState()
{
	// Snap the interpolated clock so a client cannot read a stale countdown.
	TimeInState = 0.f;

	if (OnExtractionStateChanged.IsBound())
	{
		OnExtractionStateChanged.Broadcast(ExtractionState, HoldProgress01);
	}
}

void UOneBrahamExtractionComponent::OnRep_HoldProgress()
{
	// Progress is polled by the HUD at 15 Hz, so there is nothing to push here.
	// The function exists because the property replicates with OnRep binding.
}

void UOneBrahamExtractionComponent::MulticastExtractionStateChanged_Implementation(EOneBrahamExtractionState NewState, int32 SquadId, float DurationSeconds, int32 CallerPlayerId)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner());

	const int32 LocalPlayerCount = UGameplayStatics::GetNumLocalPlayerControllers(World);
	for (int32 Index = 0; Index < LocalPlayerCount; ++Index)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, Index);
		if (PC == nullptr || !PC->IsLocalController())
		{
			continue;
		}

		const AOneBrahamPlayerState* LocalPS = PC->GetPlayerState<AOneBrahamPlayerState>();
		if (LocalPS == nullptr || Voice == nullptr)
		{
			continue;
		}

		// Squad voice line: the caller calls it in, the rest of the squad is told.
		if (LocalPS->SquadId == SquadId && NewState == EOneBrahamExtractionState::Broadcast)
		{
			AActor* Speaker = LocalPS->GetPawn();
			if (Speaker != nullptr)
			{
				const bool bIsCaller = (CallerPlayerId != INDEX_NONE && LocalPS->GetPlayerId() == CallerPlayerId);
				Voice->SpeakContextualLine(Speaker, bIsCaller ? FName(TEXT("V_EXTRACTION_CALLED")) : FName(TEXT("V_EXTRACTION_HOLD")), 1.f);
			}
		}
		else if (NewState == EOneBrahamExtractionState::Failed && LocalPS->SquadId == SquadId)
		{
			if (AActor* Speaker = LocalPS->GetPawn())
			{
				Voice->SpeakContextualLine(Speaker, FName(TEXT("V_EXTRACTION_LOST")), 0.9f);
			}
		}
		else if (NewState == EOneBrahamExtractionState::Complete && LocalPS->SquadId == SquadId)
		{
			if (AActor* Speaker = LocalPS->GetPawn())
			{
				Voice->SpeakContextualLine(Speaker, FName(TEXT("V_EXTRACTION_LEAVING")), 1.f);
			}
		}
	}

	UE_LOG(LogOneBraham, Verbose, TEXT("[OB] Extraction multicast: state=%s squad=%d duration=%.1f"),
		*UEnum::GetValueAsString(NewState), SquadId, DurationSeconds);
}
