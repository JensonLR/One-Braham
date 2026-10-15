// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPauseMenuWidget.h
// ---------------------------------------------------------------------------
// The Escape menu: RESUME, SETTINGS, LEAVE MATCH, QUIT TO DESKTOP.
//
// WHO OWNS WHAT
//   * AOneBrahamPlayerController owns whether the menu is open (bPauseMenuOpen),
//     the input mode and the cursor, and whether the world pauses (standalone
//     only: pausing a networked match pauses nobody but you, and the game
//     would merely look frozen).
//   * UOneBrahamHUDComponent creates this widget and shows or hides it.
//   * This widget owns what happens inside it: which page is up, focus, the
//     confirmation dialog, and the back key.
//
// BACK
//   Escape and the gamepad's B / Start step back one level - dialog, then
//   settings, then the menu itself - which is the one rule every menu stack
//   should have and the reason the key is handled here rather than by the
//   controller's input action.
//
// The two destructive actions ask first, with the safe answer focused, because
// a second press of the confirm key is the most common way to leave a match by
// accident.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "OneBrahamPauseMenuWidget.generated.h"

class UOneBrahamSettingsWidget;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;

UCLASS()
class ONEBRAHAM_API UOneBrahamPauseMenuWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	/** Shows the menu on its first page. Focus follows when the controller sets
	 *  the input mode with this widget as the focus target. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Pause")
	void OpenMenu();

	/** Hides the menu. Unapplied settings are applied rather than lost. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Pause")
	void CloseMenu();

	UFUNCTION(BlueprintPure, Category = "One Braham|Pause")
	bool IsMenuOpen() const { return bOpen; }

	/** The widget that should hold focus right now: the dialog's safe answer,
	 *  the settings tab, or RESUME. */
	UWidget* GetDefaultFocusTarget() const;

	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;

protected:
	virtual void BuildLayout() override;

	enum class EPendingConfirm : uint8
	{
		None,
		Leave,
		Quit
	};

	void ShowMainPage();
	void ShowSettingsPage();
	void ShowConfirm(EPendingConfirm Kind);
	void CloseConfirm();
	void AcceptConfirm();

	/** One step back: dialog, settings, then the menu. */
	void GoBack();

	/** Asks the controller to close the menu; it restores input and unpauses. */
	void RequestResume();

	void LeaveMatch();
	void QuitToDesktop();

	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget> MainPage = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamSettingsWidget> Settings = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_Resume = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_Settings = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Subtitle = nullptr;

	UPROPERTY(Transient) TObjectPtr<UWidget> ConfirmLayer = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_ConfirmTitle = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_ConfirmBody = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_ConfirmAccept = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button_ConfirmCancel = nullptr;

	EPendingConfirm PendingConfirm = EPendingConfirm::None;
	bool bOpen = false;
};
