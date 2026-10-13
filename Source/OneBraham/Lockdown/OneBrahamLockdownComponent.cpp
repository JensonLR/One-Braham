// Copyright (c) One Braham. All Rights Reserved.

#include "Lockdown/OneBrahamLockdownComponent.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameState.h"
#include "UI/OneBrahamHUDStringTable.h"
#include "UI/OneBrahamUIStyle.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

namespace
{
	/** Fallback timings when the settings asset is missing. */
	constexpr float OBFallbackStageSeconds = OneBrahamConstants::LockdownStageSeconds;
	constexpr float OBFallbackWarningSeconds = 15.f;
	constexpr float OBFallbackPurgeDps = 18.f;
	constexpr int32 OBFallbackPurgeCeiling = 13;

	/** Height of one floor in cm. The map is 15 x 400 cm with the lobby at Z=0. */
	/** One number, defined once: see OneBrahamConstants in OneBraham.h. */
	constexpr float OBFloorHeightCm = OneBrahamConstants::FloorHeightCm;

	/** Shutter travel time: steel fire shutters are heavy and slow. */
	constexpr float OBShutterDurationSeconds = 2.75f;

	/** Purge tick is fed in 5 Hz slices so the damage-over-time is smooth. */
	constexpr float OBPurgeTickInterval = 0.2f;
}

UOneBrahamLockdownComponent::UOneBrahamLockdownComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
	SetIsReplicatedByDefault(true);
}

void UOneBrahamLockdownComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedGameState = Cast<AOneBrahamGameState>(GetOwner());

	if (const AOneBrahamGameState* GS = CachedGameState.Get())
	{
		if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
		{
			SecondsToNextStage = Settings->LockdownStageSeconds;
		}
		else
		{
			SecondsToNextStage = OBFallbackStageSeconds;
		}
	}
}

void UOneBrahamLockdownComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamLockdownComponent, Stage);
	DOREPLIFETIME(UOneBrahamLockdownComponent, SecondsToNextStage);
	DOREPLIFETIME(UOneBrahamLockdownComponent, bWarningActive);
	DOREPLIFETIME(UOneBrahamLockdownComponent, HighestOccupiedFloor);
	DOREPLIFETIME(UOneBrahamLockdownComponent, bDirectorRunning);
}

void UOneBrahamLockdownComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr)
	{
		return;
	}

	// Only ASCENT escalates. FIRE DRILL and NIGHT SHIFT keep the building open.
	if (GS->MatchMode != EOneBrahamMatchMode::Ascent)
	{
		return;
	}

	if (!HasAuthority())
	{
		// Clients count the clock down between the 2 Hz replication beats.
		if (bDirectorRunning && Stage != EOneBrahamLockdownStage::Purge)
		{
			SecondsToNextStage = FMath::Max(0.f, SecondsToNextStage - DeltaTime);
		}
		return;
	}

	if (!bDirectorRunning)
	{
		return;
	}

	const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings();
	const float StageSeconds = Settings ? FMath::Max(5.f, Settings->LockdownStageSeconds) : OBFallbackStageSeconds;
	const float WarningSeconds = Settings ? FMath::Max(1.f, Settings->LockdownWarningSeconds) : OBFallbackWarningSeconds;

	TimeInStage += DeltaTime;
	ReplicationAccumulator += DeltaTime;

	// Rescan occupancy four times a second: it changes when someone takes a lift.
	OccupancyAccumulator += DeltaTime;
	if (OccupancyAccumulator >= 0.25f)
	{
		OccupancyAccumulator = 0.f;
		UpdateHighestOccupiedFloor();
	}

	const float Remaining = FMath::Max(0.f, StageSeconds - TimeInStage);

	const bool bShouldWarn = Stage != EOneBrahamLockdownStage::Purge
		&& Remaining <= WarningSeconds
		&& !bWarningFiredForStage;

	if (bShouldWarn)
	{
		bWarningFiredForStage = true;
		bWarningActive = true;

		// The warning is the single most important audio cue in the match: it is
		// the building telling the players to move. It is voiced and it is loud.
		if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
		{
			Voice->SpeakAnnouncement(FName(TEXT("V_LOCKDOWN_WARNING")));
		}
	}

	if (Remaining > WarningSeconds)
	{
		bWarningActive = false;
	}

	// Advance the stage when the clock runs out.
	if (Remaining <= 0.f && Stage != EOneBrahamLockdownStage::Purge)
	{
		const EOneBrahamLockdownStage Next = static_cast<EOneBrahamLockdownStage>(static_cast<uint8>(Stage) + 1);
		ServerApplyStage(Next, /*bAnnounce*/ true);
	}

	// Purge damage on the staged floors.
	if (Stage == EOneBrahamLockdownStage::Purge)
	{
		ServerApplyPurgeDamage(DeltaTime);
	}

	if (ReplicationAccumulator >= 0.5f)
	{
		ReplicationAccumulator = 0.f;
		SecondsToNextStage = Remaining;
		ForceNetUpdate();
	}
}

// ===========================================================================
//  Director
// ===========================================================================

void UOneBrahamLockdownComponent::StartDirector()
{
	if (!HasAuthority() || bDirectorRunning)
	{
		return;
	}

	bDirectorRunning = true;
	TimeInStage = 0.f;
	bWarningFiredForStage = false;

	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;
	SecondsToNextStage = Settings ? Settings->LockdownStageSeconds : OBFallbackStageSeconds;

	OB_MATCH_LOG("Lockdown director started (first escalation in %.0f s)", SecondsToNextStage);
}

void UOneBrahamLockdownComponent::StopDirector()
{
	if (!HasAuthority())
	{
		return;
	}

	bDirectorRunning = false;
	bWarningActive = false;

	// Unseal everything: used when a match ends and the building resets.
	ServerSealFloorsBelow(OneBrahamConstants::FloorSkydeck + 1, /*bClose*/ false);
}

void UOneBrahamLockdownComponent::ServerForceStage(EOneBrahamLockdownStage NewStage)
{
	if (!HasAuthority())
	{
		return;
	}

	// Forcing a stage should be able to move the building backwards for testing.
	if (static_cast<uint8>(NewStage) < static_cast<uint8>(Stage))
	{
		Stage = NewStage;
		TimeInStage = 0.f;
		bWarningFiredForStage = false;
		bWarningActive = false;

		ServerSealFloorsBelow(OneBrahamConstants::FloorSkydeck + 1, false);
		SealedFloors.Reset();

		if (NewStage != EOneBrahamLockdownStage::Purge)
		{
			OnRep_Stage();
			ForceNetUpdate();
		}
		return;
	}

	ServerApplyStage(NewStage, /*bAnnounce*/ true);
}

void UOneBrahamLockdownComponent::ServerApplyStage(EOneBrahamLockdownStage NewStage, bool bAnnounce)
{
	if (!HasAuthority() || NewStage == Stage)
	{
		return;
	}

	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;

	Stage = NewStage;
	TimeInStage = 0.f;
	bWarningFiredForStage = false;
	bWarningActive = false;
	SecondsToNextStage = NewStage == EOneBrahamLockdownStage::Purge
		? 0.f
		: (Settings ? Settings->LockdownStageSeconds : OBFallbackStageSeconds);

	UpdateHighestOccupiedFloor();

	switch (NewStage)
	{
	case EOneBrahamLockdownStage::PreLockdown:
		// Everything below the highest occupied floor starts refusing cards: the
		// sealed zone is what makes the lower loot run expensive rather than free.
		ServerSealFloorsBelow(HighestOccupiedFloor - 1, /*bClose*/ true);
		break;

	case EOneBrahamLockdownStage::Lockdown:
		ServerSealFloorsBelow(HighestOccupiedFloor - 1, true);
		break;

	case EOneBrahamLockdownStage::Purge:
		ServerSealFloorsBelow(OneBrahamConstants::FloorSkydeck + 1, true);
		break;

	default:
		ServerSealFloorsBelow(OneBrahamConstants::FloorSkydeck + 1, false);
		SealedFloors.Reset();
		break;
	}

	OnRep_Stage();
	ForceNetUpdate();

	if (bAnnounce)
	{
		BroadcastStageAnnouncement(NewStage);
	}

	MulticastStageChanged(NewStage, HighestOccupiedFloor);

	OB_MATCH_LOG("Lockdown stage -> %s (highest occupied floor %d, %d shutters registered)",
		*UEnum::GetValueAsString(NewStage), HighestOccupiedFloor, RegisteredShutters.Num());
}

void UOneBrahamLockdownComponent::ServerSealFloorsBelow(int32 FloorIndex, bool bClose)
{
	const int32 Ceiling = FMath::Clamp(FloorIndex, 0, OneBrahamConstants::FloorSkydeck);

	for (int32 Floor = 0; Floor <= Ceiling; ++Floor)
	{
		if (bClose)
		{
			if (SealedFloors.Contains(Floor))
			{
				continue;
			}
			SealedFloors.Add(Floor);
		}
		else
		{
			if (SealedFloors.Num() > 0)
			{
				SealedFloors.Remove(Floor);
			}
		}

		if (HasAuthority() && bClose)
		{
			// Report the higher of the shutter's own floor and the group we are
			// sealing: a single mesh can serve a whole stair core.
			OnShutterRequested.Broadcast(Floor, bClose, OBShutterDurationSeconds);
		}

		for (int32 Index = RegisteredShutters.Num() - 1; Index >= 0; --Index)
		{
			AActor* Shutter = RegisteredShutters[Index].Get();
			if (Shutter == nullptr)
			{
				RegisteredShutters.RemoveAtSwap(Index, 1, EAllowShrinking::No);
				continue;
			}

			if (!Shutter->GetClass()->ImplementsInterface(UOneBrahamShutterInterface::StaticClass()))
			{
				continue;
			}

			const int32 ShutterFloor = IOneBrahamShutterInterface::Execute_GetShutterFloor(Shutter);
			if (ShutterFloor != Floor)
			{
				continue;
			}

			IOneBrahamShutterInterface::Execute_SetShutterClosed(Shutter, bClose, OBShutterDurationSeconds);
		}
	}

	if (!bClose)
	{
		// Opening is broadcast for the whole building in one event (floor INDEX_NONE):
		// the map's Blueprint simply opens everything it owns, and every shutter
		// actor that implements the interface is opened directly.
		OnShutterRequested.Broadcast(INDEX_NONE, false, OBShutterDurationSeconds);

		for (const TWeakObjectPtr<AActor>& WeakShutter : RegisteredShutters)
		{
			AActor* Shutter = WeakShutter.Get();
			if (Shutter != nullptr && Shutter->GetClass()->ImplementsInterface(UOneBrahamShutterInterface::StaticClass()))
			{
				IOneBrahamShutterInterface::Execute_SetShutterClosed(Shutter, false, OBShutterDurationSeconds);
			}
		}
	}
}

void UOneBrahamLockdownComponent::RegisterShutter(AActor* ShutterActor)
{
	if (ShutterActor == nullptr)
	{
		return;
	}

	RegisteredShutters.AddUnique(ShutterActor);

	// A shutter registered after its floor has already been sealed must close now,
	// otherwise a streamed-in sub-level would open a hole in the lockdown.
	if (HasAuthority() && ShutterActor->GetClass()->ImplementsInterface(UOneBrahamShutterInterface::StaticClass()))
	{
		const int32 Floor = IOneBrahamShutterInterface::Execute_GetShutterFloor(ShutterActor);
		if (SealedFloors.Contains(Floor) ||
			(Stage == EOneBrahamLockdownStage::Purge && Floor <= OBFallbackPurgeCeiling))
		{
			IOneBrahamShutterInterface::Execute_SetShutterClosed(ShutterActor, true, 0.35f);
		}
	}
}

// ===========================================================================
//  Occupancy and purge
// ===========================================================================

void UOneBrahamLockdownComponent::UpdateHighestOccupiedFloor()
{
	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr)
	{
		return;
	}

	int32 Highest = 0;
	int32 Occupants = 0;

	for (APlayerState* BaseState : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* PS = Cast<AOneBrahamPlayerState>(BaseState);
		if (PS == nullptr || PS->IsEliminated())
		{
			continue;
		}

		const APawn* Pawn = PS->GetPawn();
		if (Pawn == nullptr)
		{
			continue;
		}

		const float Z = Pawn->GetActorLocation().Z;
		const int32 Floor = FMath::Clamp(FMath::FloorToInt(Z / OBFloorHeightCm), 0, OneBrahamConstants::FloorSkydeck);

		Highest = FMath::Max(Highest, Floor);
		++Occupants;
	}

	// Nobody alive (last squad just died): hold the highest floor rather than
	// unsealing the building under the end-of-match screen.
	if (Occupants > 0)
	{
		HighestOccupiedFloor = Highest;
	}
}

bool UOneBrahamLockdownComponent::IsFloorBeingPurged(int32 FloorIndex) const
{
	if (Stage != EOneBrahamLockdownStage::Purge)
	{
		return false;
	}

	const AOneBrahamGameState* GS = CachedGameState.Get();
	const UOneBrahamMatchSettingsAsset* Settings = GS ? GS->GetMatchSettings() : nullptr;
	const int32 Ceiling = Settings ? Settings->PurgeFloorCeiling : OBFallbackPurgeCeiling;

	// The Skydeck is never purged: the last fight is a fight, not a timer.
	return FloorIndex >= 0 && FloorIndex <= Ceiling && FloorIndex < OneBrahamConstants::FloorSkydeck;
}

void UOneBrahamLockdownComponent::ServerApplyPurgeDamage(float DeltaTime)
{
	PurgeDamageAccumulator += DeltaTime;
	if (PurgeDamageAccumulator < OBPurgeTickInterval)
	{
		return;
	}

	const float Slice = PurgeDamageAccumulator;
	PurgeDamageAccumulator = 0.f;

	const AOneBrahamGameState* GS = CachedGameState.Get();
	if (GS == nullptr)
	{
		return;
	}

	const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings();
	const float DamagePerSecond = Settings ? FMath::Max(0.f, Settings->PurgeDamagePerSecond) : OBFallbackPurgeDps;

	if (DamagePerSecond <= 0.f)
	{
		return;
	}

	for (APlayerState* BaseState : GS->PlayerArray)
	{
		AOneBrahamPlayerState* PS = Cast<AOneBrahamPlayerState>(BaseState);
		if (PS == nullptr || PS->IsEliminated())
		{
			continue;
		}

		AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(PS->GetPawn());
		if (Character == nullptr)
		{
			continue;
		}

		const int32 Floor = FMath::Clamp(FMath::FloorToInt(Character->GetActorLocation().Z / OBFloorHeightCm),
			0, OneBrahamConstants::FloorSkydeck);

		if (!IsFloorBeingPurged(Floor))
		{
			continue;
		}

		if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
		{
			// The purge is a game-mode hazard, not a weapon: no instigator, no
			// headshot multipliers, and it never counts as a player kill.
			FOneBrahamDamageReport Report;
			Report.BaseDamage = DamagePerSecond * Slice;
			Report.HitZone = EOneBrahamHitZone::Torso;
			Report.Distance = 0.f;

			const float Applied = Health->ApplyDamage(Report.BaseDamage, GetOwner(), Report, nullptr);
			if (Applied > 0.f && !Health->IsAlive())
			{
				++PurgeKillCount;
				OB_MATCH_LOG("Security purge killed %s on floor %d.", *PS->GetPlayerName(), Floor);
			}
		}
	}
}

// ===========================================================================
//  Announcements + replication
// ===========================================================================

FText UOneBrahamLockdownComponent::GetStageAnnouncement(EOneBrahamLockdownStage InStage, int32 HighestFloor)
{
	// Dry, procedural, and exactly what a building management system would say.
	// No jokes: the humour in ONE BRAHAM comes from the gap between this language
	// and what is actually happening on floor 11.
	switch (InStage)
	{
	case EOneBrahamLockdownStage::PreLockdown:
		return FText::Format(NSLOCTEXT("OneBraham", "Lockdown_Pre",
			"Attention all staff. The building is now in lockdown. Access below floor {0} has been suspended. Please make your way to your floor of occupancy."),
			FText::AsNumber(FMath::Max(1, HighestFloor - 1)));

	case EOneBrahamLockdownStage::Lockdown:
		return FText::Format(NSLOCTEXT("OneBraham", "Lockdown_Lockdown",
			"Fire shutters are closing on all floors below {0}. Lifts are out of service. Please use the stairwell nearest to you."),
			FText::AsNumber(FMath::Max(1, HighestFloor - 1)));

	case EOneBrahamLockdownStage::Purge:
		return NSLOCTEXT("OneBraham", "Lockdown_Purge",
			"Security purge in progress on lower floors. This is not a drill. Evacuate the affected floors immediately.");

	case EOneBrahamLockdownStage::Normal:
	default:
		return NSLOCTEXT("OneBraham", "Lockdown_Normal",
			"Building systems normal. Thank you for your patience.");
	}
}

void UOneBrahamLockdownComponent::BroadcastStageAnnouncement(EOneBrahamLockdownStage NewStage)
{
	const FText Announcement = GetStageAnnouncement(NewStage, HighestOccupiedFloor);

	if (AOneBrahamGameState* GS = CachedGameState.Get())
	{
		GS->MulticastAnnounce(Announcement, /*bPlayTannoySound*/ true);

		// The same event in two lengths, which is how a building actually tells
		// you something: the paragraph over the speakers, and two words on the
		// screen of everyone who has taken their headphones off. The short form
		// comes from the copy table, so it is editable without a recompile.
		switch (NewStage)
		{
		case EOneBrahamLockdownStage::PreLockdown:
		case EOneBrahamLockdownStage::Lockdown:
			GS->MulticastWarningBanner(OneBrahamHUDStrings::Get(TEXT("HUD_Objective_Lockdown")),
				4.f, UOneBrahamUIStyle::SafetyYellow());
			break;

		case EOneBrahamLockdownStage::Purge:
			GS->MulticastWarningBanner(OneBrahamHUDStrings::Get(TEXT("HUD_Objective_Purge")),
				5.f, UOneBrahamUIStyle::EmergencyRed());
			break;

		case EOneBrahamLockdownStage::Normal:
		default:
			break;
		}
	}

	// The Tannoy voice is a building asset, not a character, so it goes through
	// the announcement path rather than SpeakContextualLine.
	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
	{
		switch (NewStage)
		{
		case EOneBrahamLockdownStage::PreLockdown:
		case EOneBrahamLockdownStage::Lockdown:
			Voice->SpeakAnnouncement(FName(TEXT("V_LOCKDOWN_START")));
			break;
		case EOneBrahamLockdownStage::Purge:
			// The purge is the building's own security response; the line is the
			// same tired staff voice that has been on the Tannoy all match.
			Voice->SpeakAnnouncement(FName(TEXT("V_SECURITY_PURGE")));
			break;
		default:
			break;
		}
	}
}

FText UOneBrahamLockdownComponent::GetStageDisplayText() const
{
	switch (Stage)
	{
	case EOneBrahamLockdownStage::PreLockdown:
		return NSLOCTEXT("OneBraham", "LockdownHUD_Pre", "LOCKDOWN: ACCESS RESTRICTED");
	case EOneBrahamLockdownStage::Lockdown:
		return NSLOCTEXT("OneBraham", "LockdownHUD_Lockdown", "LOCKDOWN: SHUTTERS DOWN");
	case EOneBrahamLockdownStage::Purge:
		return NSLOCTEXT("OneBraham", "LockdownHUD_Purge", "SECURITY PURGE: GET UP");
	case EOneBrahamLockdownStage::Normal:
	default:
		return NSLOCTEXT("OneBraham", "LockdownHUD_Normal", "BUILDING SYSTEMS NORMAL");
	}
}

void UOneBrahamLockdownComponent::OnRep_Stage()
{
	ApplyLocalStageEffects(Stage);

	if (OnStageChanged.IsBound())
	{
		OnStageChanged.Broadcast(Stage);
	}
}

void UOneBrahamLockdownComponent::ApplyLocalStageEffects(EOneBrahamLockdownStage NewStage)
{
	// Every machine runs the same local effects: the audio director's alarm bed,
	// the map's lighting (via OnStageChanged), and the HUD objective line (which
	// polls this component). One state, three consumers, no RPCs.
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->SetLockdownAudio(NewStage != EOneBrahamLockdownStage::Normal);
		}
	}
}

void UOneBrahamLockdownComponent::MulticastStageChanged_Implementation(EOneBrahamLockdownStage NewStage, int32 InHighestFloor)
{
	HighestOccupiedFloor = InHighestFloor;
	ApplyLocalStageEffects(NewStage);

	// The HUD reads the stage from this component every gather, so there is no
	// per-client banner to push here - the audio bed above *is* the feedback, and
	// the Tannoy announcement carries the words.
	UE_LOG(LogOneBrahamMatch, Verbose, TEXT("[OB.MATCH] Lockdown %s at highest floor %d"),
		*UEnum::GetValueAsString(NewStage), InHighestFloor);
}
