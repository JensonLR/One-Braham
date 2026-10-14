// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMainMenuWidget.h
// ---------------------------------------------------------------------------
// The front end (Docs/08 §4): PLAY, LOADOUT, SERVER BROWSER, WARDROBE,
// ACHIEVEMENTS, SETTINGS, FRIENDS, QUIT, as pages of one code-built menu.
//
// Every action goes through the code that already owns it: matchmaking,
// hosting, the browser and joining through UOneBrahamGameInstance; the
// wardrobe and the loadout write the profile and save it; the achievement list
// and the wardrobe locks come from UOneBrahamUILibrary. The menu holds no
// state the game does not already have, apart from which page is up.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "Core/OneBrahamGameInstance.h"
#include "OneBrahamMainMenuWidget.generated.h"

UENUM()
enum class EOneBrahamMainMenuPage : uint8
{
	Home,
	Play,
	Loadout,
	Servers,
	Wardrobe,
	Achievements
};

UCLASS()
class ONEBRAHAM_API UOneBrahamMainMenuWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void GoBack() override;

	/** Opens a page directly (the results screen sends players back to Play). */
	void OpenPage(EOneBrahamMainMenuPage NewPage);

protected:
	virtual void BuildPage() override;
	virtual FText GetPageTitle() const override;
	virtual FText GetFooterText() const override;

	void BuildHome();
	void BuildPlay();
	void BuildLoadout();
	void BuildServers();
	void BuildWardrobe();
	void BuildAchievements();

	/** The status line under the page: matchmaking, joining, a refused unlock. */
	void AddStatusLine();

	void OpenSettings();

	UFUNCTION()
	void HandleServerList(const TArray<FOneBrahamServerInfo>& Servers);

	UFUNCTION()
	void HandleMatchmakingStatus(const FText& Status, float Progress);

	UFUNCTION()
	void HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL);

	UFUNCTION()
	void HandleSettingsClosed();

	UOneBrahamGameInstance* GetOBGameInstance() const;

	EOneBrahamMainMenuPage Page = EOneBrahamMainMenuPage::Home;

	TArray<FOneBrahamServerInfo> ServerList;
	bool bSearchingServers = false;
	bool bBrowseLAN = false;

	FText StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamMenuWidget> SettingsMenu = nullptr;
};
