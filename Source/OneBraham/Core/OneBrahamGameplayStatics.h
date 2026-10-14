// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameplayStatics.h
// ---------------------------------------------------------------------------
// The project's own gameplay helpers for Blueprint: the typed game state,
// game instance and local player; the building's floor datum; and whether two
// actors are on the same side. Each one is a thin, null-safe wrapper over the
// C++ that already owns the answer, so a Blueprint never re-derives a floor
// from a Z height or a friendship from two team enums.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamGameplayStatics.generated.h"

class AOneBrahamGameState;
class AOneBrahamPlayerController;
class AOneBrahamPlayerState;
class UOneBrahamGameInstance;

UCLASS()
class ONEBRAHAM_API UOneBrahamGameplayStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics", meta = (WorldContext = "WorldContextObject"))
	static AOneBrahamGameState* GetOneBrahamGameState(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "One Braham|Statics", meta = (WorldContext = "WorldContextObject"))
	static UOneBrahamGameInstance* GetOneBrahamGameInstance(const UObject* WorldContextObject);

	/** The first local player's controller: null on a dedicated server. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics", meta = (WorldContext = "WorldContextObject"))
	static AOneBrahamPlayerController* GetLocalPlayerController(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "One Braham|Statics", meta = (WorldContext = "WorldContextObject"))
	static AOneBrahamPlayerState* GetLocalPlayerState(const UObject* WorldContextObject);

	/** The match mode, or ASCENT before a game state exists. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics", meta = (WorldContext = "WorldContextObject"))
	static EOneBrahamMatchMode GetMatchMode(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "One Braham|Statics", meta = (WorldContext = "WorldContextObject"))
	static EOneBrahamMatchPhase GetMatchPhase(const UObject* WorldContextObject);

	/** The building floor at a world location (OneBrahamConstants::FloorIndexForZ). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics")
	static int32 GetFloorAtLocation(const FVector& Location);

	/** The world Z of a floor's slab top (OneBrahamConstants::FloorZ). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics")
	static float GetFloorZ(int32 Floor);

	/** The player state behind an actor: a pawn's, a controller's, or the actor itself. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics")
	static AOneBrahamPlayerState* GetPlayerStateOf(const AActor* Actor);

	/** True when two actors' players are on the same side under the current mode's rules. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Statics")
	static bool AreFriendly(const AActor* A, const AActor* B);
};
