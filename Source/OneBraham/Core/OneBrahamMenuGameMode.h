// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMenuGameMode.h
// ---------------------------------------------------------------------------
// The front end's game mode and controller: L_MainMenu runs these (its world
// settings name the game mode; build_environment.py sets it).
//
// No pawn, no HUD, no match. The controller puts the main menu on screen in
// UI-only input and gives the cursor to the player. The menu is WBP_MainMenu
// when the UI build has made it, which subclasses UOneBrahamMainMenuWidget and
// is empty by default, so the C++ menu is what a player sees either way.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "OneBrahamMenuGameMode.generated.h"

class UOneBrahamMainMenuWidget;

UCLASS()
class ONEBRAHAM_API AOneBrahamMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOneBrahamMenuGameMode();
};

UCLASS()
class ONEBRAHAM_API AOneBrahamMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AOneBrahamMenuPlayerController();

	virtual void BeginPlay() override;

	/** WBP_MainMenu; the C++ menu when the asset does not exist. */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Menu")
	TSoftClassPtr<UOneBrahamMainMenuWidget> MainMenuClass;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamMainMenuWidget> MainMenu = nullptr;
};
