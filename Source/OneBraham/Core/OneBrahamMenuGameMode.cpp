// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamMenuGameMode.h"

#include "OneBraham.h"
#include "Blueprint/UserWidget.h"
#include "Core/OneBrahamGameInstance.h"
#include "UI/OneBrahamMainMenuWidget.h"

AOneBrahamMenuGameMode::AOneBrahamMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AOneBrahamMenuPlayerController::StaticClass();
	HUDClass = nullptr;
	bStartPlayersAsSpectators = true;
}

AOneBrahamMenuPlayerController::AOneBrahamMenuPlayerController()
{
	MainMenuClass = TSoftClassPtr<UOneBrahamMainMenuWidget>(FSoftObjectPath(TEXT("/Game/OneBraham/UI/Menus/WBP_MainMenu.WBP_MainMenu_C")));
	bShowMouseCursor = true;
}

void AOneBrahamMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	UClass* WidgetClass = MainMenuClass.IsNull() ? nullptr : MainMenuClass.LoadSynchronous();
	MainMenu = CreateWidget<UOneBrahamMainMenuWidget>(this, WidgetClass != nullptr ? WidgetClass : UOneBrahamMainMenuWidget::StaticClass());
	if (MainMenu == nullptr)
	{
		OB_ERROR("The main menu could not be created.");
		return;
	}
	MainMenu->AddToViewport(0);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;

	// A match ends with a travel back here; the loading screen comes down now.
	if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
	{
		GameInstance->HideLoadingScreen();
	}
}
