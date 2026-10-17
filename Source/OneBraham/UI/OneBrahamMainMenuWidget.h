// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMainMenuWidget.h
// ---------------------------------------------------------------------------
// The front end a Steam player meets: PLAY, HOST, JOIN, SETTINGS,
// CREDITS & LICENCES, QUIT. Built in C++ on UOneBrahamMenuWidget, like the
// pause menu and the settings, for the same reason (Docs/08 section 1): a
// clean checkout can start, host and join a match before any widget asset
// exists, and it looks like the rest of the building's signage.
//
// THE LAYOUT
//   The directory column on the left - the title, the six signs, a line of
//   status - over the backdrop of the tower at dusk; the chosen page in a
//   panel beside it. HOME is no panel at all: the building is the picture.
//
//     PLAY      solo, with bots in every empty slot: ASCENT or FIRE DRILL
//     HOST      mode, LAN or online, then a session and ?listen
//     JOIN      a search (LAN under NULL, online under Steam), one row per
//               match: name, mode, players, ping. Select a row to join it
//     SETTINGS  the settings screen the pause menu uses
//     CREDITS & LICENCES  the third-party credits from Docs/LICENCES.md,
//               the OpenStreetMap attribution the ODbL asks for first
//
// INPUT
//   Mouse, keyboard and pad all work through Slate's own navigation: every
//   sign and row is a focusable button, the focused one drawn as a hovered
//   one (UOneBrahamMenuWidget), Enter / A selects, Escape / B steps back a
//   page, the shoulders walk the settings tabs.
//
// CREDITS
//   The list is compiled in (Credits()), the same rule as the HUD strings: a
//   cooked build has no Docs folder to read. build_frontend.check_credits
//   holds it to Docs/LICENCES.md on every RunAll, so a credit added there and
//   missing here fails the build rather than the legal review.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "Core/OneBrahamSessionSubsystem.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamMainMenuWidget.generated.h"

class UOneBrahamSettingsWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetSwitcher;

/** One block of the credits page. */
struct FOneBrahamCreditBlock
{
	FText Heading;
	FText Body;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamMainMenuWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	enum class EPage : uint8
	{
		Home,
		Play,
		Host,
		Join,
		Settings,
		Credits
	};

	/** "home", "play", "host", "join", "settings", "credits" (any case). */
	static bool ParsePage(const FString& Text, EPage& OutPage);

	/** The credits, in the order they are shown. The OpenStreetMap block first. */
	static const TArray<FOneBrahamCreditBlock>& Credits();

	/** "ASCENT", "FIRE DRILL", "NIGHT SHIFT". */
	static FText ModeLabel(EOneBrahamMatchMode Mode);

	/** One row of the JOIN list as text: "12 / 16", "38 ms". */
	static FText PlayersLabel(const FOneBrahamServerInfo& Info);
	static FText PingLabel(const FOneBrahamServerInfo& Info);

	/** Every public slot taken (an unknown capacity is never "full"). */
	static bool IsFull(const FOneBrahamServerInfo& Info);

	void ShowPage(EPage Page);
	EPage GetPage() const { return Page; }

	// --- the actions every sign ends in (also what ob.menu.* calls) ----------
	void PlaySolo(EOneBrahamMatchMode Mode);
	void Host(EOneBrahamMatchMode Mode, bool bLAN);
	void RefreshJoinList();
	void JoinRow(int32 RowIndex);

	/** Shows JOIN, searches, and joins row RowIndex when it appears; searches
	 *  again (up to Attempts times) while the list is shorter than that. */
	void JoinWhenFound(int32 RowIndex, int32 Attempts);

	/** A line under the signs: why the player is back, or what went wrong. */
	void ShowNotice(const FText& Notice);

	UWidget* GetDefaultFocusTarget() const;

	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;

protected:
	virtual void BuildLayout() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UWidget* BuildHomePage();
	UWidget* BuildPlayPage();
	UWidget* BuildHostPage();
	UWidget* BuildJoinPage();
	UWidget* BuildSettingsPage();
	UWidget* BuildCreditsPage();

	/** A page's heading and its one line of explanation. */
	void AddPageHeading(UVerticalBox* Page, const FText& Title, const FText& Body, UTextBlock** OutBody = nullptr);

	/** Fills the JOIN list from the session subsystem's last search. */
	void RebuildJoinRows(const TArray<FOneBrahamServerInfo>& Servers);

	void HandleSessionsFound(bool bWasSuccessful, const TArray<FOneBrahamServerInfo>& Servers);
	void HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL, const FText& Reason);
	void HandleSessionCreated(bool bWasSuccessful, bool bLAN);

	void SetStatus(UTextBlock* Line, const FText& Text);

	/** One step back: settings' own back first, then HOME. */
	void GoBack();

	void Quit();

	UOneBrahamSessionSubsystem* GetSessions() const;

	/** The sign that opens a page: where focus returns when it closes. */
	UButton* SignFor(EPage InPage) const;

	EPage Page = EPage::Home;

	EOneBrahamMatchMode HostMode = EOneBrahamMatchMode::Ascent;
	bool bHostLAN = true;

	/** Set while a host or a join is underway: a second press does nothing. */
	bool bBusy = false;

	int32 AutoJoinRow = INDEX_NONE;
	int32 AutoJoinAttemptsLeft = 0;
	FTimerHandle AutoJoinRetry;

	FDelegateHandle FoundHandle;
	FDelegateHandle JoinedHandle;
	FDelegateHandle CreatedHandle;

	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget> PagePanel = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamSettingsWidget> Settings = nullptr;

	UPROPERTY(Transient) TObjectPtr<UButton> Sign_Play = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Sign_Host = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Sign_Join = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Sign_Settings = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Sign_Credits = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Sign_Quit = nullptr;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Notice = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Build = nullptr;

	UPROPERTY(Transient) TObjectPtr<UButton> Button_PlayAscent = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_PlayStatus = nullptr;

	UPROPERTY(Transient) TObjectPtr<UButton> Button_HostMode = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_HostMode = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_HostNetwork = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_HostNetwork = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_HostGo = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_HostNote = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_HostStatus = nullptr;

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> JoinRows = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_JoinRefresh = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_JoinBody = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_JoinStatus = nullptr;

	UPROPERTY(Transient) TObjectPtr<UScrollBox> CreditsScroll = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_CreditsBack = nullptr;

	/** One button per row of the JOIN list, in list order. */
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> JoinRowButtons;
};
