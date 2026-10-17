// Copyright (c) One Braham. All Rights Reserved.

#include "UI/HUD/OneBrahamBuildingStatus.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Lockdown/OneBrahamLockdownComponent.h"

namespace
{
	/** The stage length as the director uses it: the dev override first. */
	float StageSecondsFor(const AOneBrahamGameState* GS)
	{
		static const IConsoleVariable* const Override = IConsoleManager::Get().FindConsoleVariable(TEXT("ob.Lockdown.StageSeconds"));
		if (Override != nullptr && Override->GetFloat() > 0.f)
		{
			return Override->GetFloat();
		}

		const UOneBrahamMatchSettingsAsset* Settings = GS != nullptr ? GS->GetMatchSettings() : nullptr;
		return Settings != nullptr ? FMath::Max(5.f, Settings->LockdownStageSeconds) : OneBrahamConstants::LockdownStageSeconds;
	}

	/** Same squad in ASCENT and NIGHT SHIFT, same team in FIRE DRILL. */
	bool IsColleague(const AOneBrahamPlayerState* Local, const AOneBrahamPlayerState* Other, EOneBrahamMatchMode Mode)
	{
		if (Local == nullptr || Other == nullptr)
		{
			return false;
		}
		if (Mode == EOneBrahamMatchMode::FireDrill)
		{
			return Local->Team == Other->Team && Local->Team != EOneBrahamTeam::None;
		}
		return Local->SquadId != INDEX_NONE && Local->SquadId == Other->SquadId;
	}
}

EOneBrahamFloorCondition FOneBrahamBuildingStatus::ConditionFor(EOneBrahamLockdownStage Stage, int32 FloorIndex,
	int32 SealCeiling, int32 NextSealCeiling, int32 PurgeCeiling)
{
	// The Skydeck and above are never purged: the last fight is a fight.
	if (Stage == EOneBrahamLockdownStage::Purge)
	{
		return FloorIndex <= PurgeCeiling && FloorIndex < OneBrahamConstants::FloorSkydeck
			? EOneBrahamFloorCondition::Purged : EOneBrahamFloorCondition::Open;
	}

	if (SealCeiling != INDEX_NONE && FloorIndex <= SealCeiling)
	{
		return EOneBrahamFloorCondition::Sealed;
	}

	if (NextSealCeiling != INDEX_NONE && FloorIndex <= NextSealCeiling)
	{
		return EOneBrahamFloorCondition::NextToSeal;
	}

	return EOneBrahamFloorCondition::Open;
}

void FOneBrahamBuildingStatus::Gather(const UWorld* World, const APlayerController* LocalController, int32 SealCeilingHint,
	FOneBrahamBuildingStatus& Out)
{
	Out = FOneBrahamBuildingStatus();

	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	const UOneBrahamBuildingLayoutAsset* Layout = UOneBrahamBuildingLayoutAsset::Get(World);
	if (GS == nullptr || Layout == nullptr)
	{
		return;
	}

	Out.bValid = true;
	Out.Mode = GS->MatchMode;
	Out.ExtractionFloor = Layout->ExtractionFloorIndex;
	Out.StageSeconds = StageSecondsFor(GS);

	if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
	{
		Out.PurgeCeiling = Settings->PurgeFloorCeiling;
	}

	const UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent();
	const bool bLockdownMode = Out.Mode == EOneBrahamMatchMode::Ascent;

	if (Lockdown != nullptr && bLockdownMode)
	{
		Out.Stage = Lockdown->GetStage();
		Out.SecondsToNextStage = Lockdown->GetSecondsToNextStage();
		Out.bDirectorRunning = Lockdown->IsDirectorRunning();
		Out.bWarningActive = Lockdown->IsWarningActive();
		Out.HighestOccupiedFloor = Lockdown->GetHighestOccupiedFloor();

		// The server knows its sealed set; a client has the hint the HUD
		// recorded when the stage last changed. Both are "the highest sealed
		// floor", because sealing is always everything below a line.
		if (World->GetNetMode() != NM_Client)
		{
			for (int32 Floor = OneBrahamConstants::FloorSkydeck; Floor >= 0; --Floor)
			{
				if (Lockdown->IsFloorSealed(Floor))
				{
					Out.SealCeiling = Floor;
					break;
				}
			}
		}
		else if (Out.Stage == EOneBrahamLockdownStage::PreLockdown || Out.Stage == EOneBrahamLockdownStage::Lockdown)
		{
			Out.SealCeiling = SealCeilingHint != INDEX_NONE
				? SealCeilingHint : FMath::Max(static_cast<int32>(INDEX_NONE), Out.HighestOccupiedFloor - 1);
		}

		// What the next stage takes: below the highest occupied floor for the
		// two sealing stages, everything up to the purge ceiling for the Purge.
		if (Out.bDirectorRunning)
		{
			switch (Out.Stage)
			{
			case EOneBrahamLockdownStage::Normal:
			case EOneBrahamLockdownStage::PreLockdown:
				Out.NextSealCeiling = Out.HighestOccupiedFloor - 1 > Out.SealCeiling ? Out.HighestOccupiedFloor - 1 : INDEX_NONE;
				break;
			case EOneBrahamLockdownStage::Lockdown:
				Out.NextSealCeiling = FMath::Min(Out.PurgeCeiling, OneBrahamConstants::FloorSkydeck - 1);
				break;
			default:
				break;
			}
		}
	}

	// --- the local player and their colleagues ---------------------------------
	const AOneBrahamPlayerState* LocalState = LocalController != nullptr
		? LocalController->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

	if (LocalController != nullptr)
	{
		if (const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(LocalController->GetPawn()))
		{
			Out.LocalFloor = Character->GetCurrentFloorIndex();
		}
	}

	for (const APlayerState* Base : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* Other = Cast<AOneBrahamPlayerState>(Base);
		if (Other == nullptr || !(Other == LocalState || IsColleague(LocalState, Other, Out.Mode)))
		{
			continue;
		}

		FOneBrahamSquadMarker Marker;
		Marker.PlayerId = Other->GetPlayerId();
		Marker.Name = Other->GetPlayerName();
		Marker.bLocal = Other == LocalState;
		Marker.bDowned = Other->IsDowned();
		Marker.bTransmitting = Other->bIsTransmitting;

		if (const AOneBrahamCharacter* Pawn = Cast<AOneBrahamCharacter>(Other->GetPawn()))
		{
			Marker.Floor = Marker.bLocal && Out.LocalFloor != INDEX_NONE ? Out.LocalFloor : Pawn->GetCurrentFloorIndex();
			Marker.Location = Pawn->GetActorLocation();
			Marker.bHasLocation = true;
		}

		if (!Other->IsEliminated())
		{
			Out.Squad.Add(Marker);
		}
	}

	// --- floors --------------------------------------------------------------------
	TArray<FOneBrahamFloorDescription> Ascending;
	Layout->GetFloorsAscending(Ascending);
	Out.Floors.Reserve(Ascending.Num());

	for (const FOneBrahamFloorDescription& Description : Ascending)
	{
		FOneBrahamFloorStatus& Floor = Out.Floors.AddDefaulted_GetRef();
		Floor.FloorIndex = Description.FloorIndex;
		Floor.ShortName = Description.ShortName;
		Floor.AreaName = Description.AreaName;
		Floor.bEndgame = Description.bEndgameFloor;
		Floor.bExtraction = bLockdownMode && Description.FloorIndex == Out.ExtractionFloor;
		Floor.bLocal = Description.FloorIndex == Out.LocalFloor;
		Floor.Condition = bLockdownMode
			? ConditionFor(Out.Stage, Description.FloorIndex, Out.SealCeiling, Out.NextSealCeiling, Out.PurgeCeiling)
			: EOneBrahamFloorCondition::Open;

		for (const FOneBrahamSquadMarker& Marker : Out.Squad)
		{
			if (!Marker.bLocal && Marker.Floor == Description.FloorIndex)
			{
				++Floor.Mates;
				Floor.MatesDowned += Marker.bDowned ? 1 : 0;
			}
		}
	}
}

const FOneBrahamFloorStatus* FOneBrahamBuildingStatus::Find(int32 FloorIndex) const
{
	for (const FOneBrahamFloorStatus& Floor : Floors)
	{
		if (Floor.FloorIndex == FloorIndex)
		{
			return &Floor;
		}
	}
	return nullptr;
}

bool FOneBrahamBuildingStatus::IsLocalSealingWithin(float WindowSeconds) const
{
	return LocalFloor != INDEX_NONE
		&& ConditionOf(LocalFloor) == EOneBrahamFloorCondition::NextToSeal
		&& bDirectorRunning
		&& SecondsToNextStage > 0.f
		&& SecondsToNextStage <= WindowSeconds;
}

bool FOneBrahamBuildingStatus::IsLocalInDanger() const
{
	if (LocalFloor == INDEX_NONE)
	{
		return false;
	}

	const EOneBrahamFloorCondition Here = ConditionOf(LocalFloor);
	if (Here == EOneBrahamFloorCondition::Sealed || Here == EOneBrahamFloorCondition::Purged)
	{
		return true;
	}

	// The purge front: one floor above the purge is where the gas is heard.
	return Stage == EOneBrahamLockdownStage::Purge && LocalFloor <= PurgeCeiling + 1
		&& LocalFloor < OneBrahamConstants::FloorSkydeck;
}

// ==========================================================================
//  The dusk clock
// ==========================================================================
int32 OneBrahamDuskClock::MatchSecondsToMinutes(float MatchSeconds, float PurgeAtSeconds)
{
	const float Span = static_cast<float>(PurgeMinutes - StartMinutes);
	const float Rate = Span / FMath::Max(30.f, PurgeAtSeconds);
	const int32 Minutes = StartMinutes + FMath::FloorToInt(FMath::Max(0.f, MatchSeconds) * Rate);
	return Minutes % (24 * 60);
}

FText OneBrahamDuskClock::MatchSecondsToClock(float MatchSeconds, float PurgeAtSeconds)
{
	const int32 Minutes = MatchSecondsToMinutes(MatchSeconds, PurgeAtSeconds);
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Minutes / 60, Minutes % 60));
}

float OneBrahamDuskClock::MatchSeconds(const UWorld* World)
{
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	return GS != nullptr ? GS->PhaseElapsedSeconds : 0.f;
}

float OneBrahamDuskClock::PurgeAtSeconds(const UWorld* World)
{
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	return StageSecondsFor(GS) * 3.f;
}

FText OneBrahamDuskClock::Now(const UWorld* World)
{
	return MatchSecondsToClock(MatchSeconds(World), PurgeAtSeconds(World));
}
