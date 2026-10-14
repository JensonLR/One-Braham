// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameplayStatics.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
	UWorld* WorldOf(const UObject* WorldContextObject)
	{
		return GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	}
}

AOneBrahamGameState* UOneBrahamGameplayStatics::GetOneBrahamGameState(const UObject* WorldContextObject)
{
	const UWorld* World = WorldOf(WorldContextObject);
	return World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
}

UOneBrahamGameInstance* UOneBrahamGameplayStatics::GetOneBrahamGameInstance(const UObject* WorldContextObject)
{
	const UWorld* World = WorldOf(WorldContextObject);
	return World ? World->GetGameInstance<UOneBrahamGameInstance>() : nullptr;
}

AOneBrahamPlayerController* UOneBrahamGameplayStatics::GetLocalPlayerController(const UObject* WorldContextObject)
{
	const UWorld* World = WorldOf(WorldContextObject);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? Cast<AOneBrahamPlayerController>(GameInstance->GetFirstLocalPlayerController(World)) : nullptr;
}

AOneBrahamPlayerState* UOneBrahamGameplayStatics::GetLocalPlayerState(const UObject* WorldContextObject)
{
	const AOneBrahamPlayerController* PC = GetLocalPlayerController(WorldContextObject);
	return PC ? PC->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
}

EOneBrahamMatchMode UOneBrahamGameplayStatics::GetMatchMode(const UObject* WorldContextObject)
{
	const AOneBrahamGameState* GameState = GetOneBrahamGameState(WorldContextObject);
	return GameState ? GameState->MatchMode : EOneBrahamMatchMode::Ascent;
}

EOneBrahamMatchPhase UOneBrahamGameplayStatics::GetMatchPhase(const UObject* WorldContextObject)
{
	const AOneBrahamGameState* GameState = GetOneBrahamGameState(WorldContextObject);
	return GameState ? GameState->MatchPhase : EOneBrahamMatchPhase::Warmup;
}

int32 UOneBrahamGameplayStatics::GetFloorAtLocation(const FVector& Location)
{
	return OneBrahamConstants::FloorIndexForZ(static_cast<float>(Location.Z));
}

float UOneBrahamGameplayStatics::GetFloorZ(int32 Floor)
{
	return OneBrahamConstants::FloorZ(Floor);
}

AOneBrahamPlayerState* UOneBrahamGameplayStatics::GetPlayerStateOf(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return nullptr;
	}
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		return Pawn->GetPlayerState<AOneBrahamPlayerState>();
	}
	if (const AController* Controller = Cast<AController>(Actor))
	{
		return Controller->GetPlayerState<AOneBrahamPlayerState>();
	}
	return const_cast<AOneBrahamPlayerState*>(Cast<AOneBrahamPlayerState>(Actor));
}

bool UOneBrahamGameplayStatics::AreFriendly(const AActor* A, const AActor* B)
{
	const AOneBrahamPlayerState* StateA = GetPlayerStateOf(A);
	const AOneBrahamPlayerState* StateB = GetPlayerStateOf(B);
	return StateA != nullptr && StateA->IsFriendlyWith(StateB);
}

FGameplayTag UOneBrahamGameplayStatics::RequestTag(FName TagName)
{
	// bErrorIfNotFound false: an unknown name is the empty tag, which the
	// caller checks with IsValid, not an ensure in the middle of a build.
	return FGameplayTag::RequestGameplayTag(TagName, /*bErrorIfNotFound*/ false);
}
