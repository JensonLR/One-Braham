// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPauseMenuWidget.h
// ---------------------------------------------------------------------------
// The Escape menu: ON BREAK.
//
// THE LOOK
//   The world behind frosts over like the meeting-room glass (a blur, a 4%
//   white frost, the band of manifestation dots at eye height) under a navy
//   dim. On the left, the masthead - "BACK IN 5 MINUTES" and ON BREAK in the
//   title face - and the same directory rows as the front end's HOME: RESUME,
//   FACILITIES, then after a rule the two ways out. On the right, a status card
//   says what the building is doing while the player is away, because in a
//   networked match it is still doing it: the stage, the lockdown clock, the
//   floor, the squad. "The building does not pause."
//
// WHO OWNS WHAT
//   * AOneBrahamPlayerController owns whether the menu is open (bPauseMenuOpen),
//     the input mode and the cursor, and whether the world pauses (standalone
//     only: pausing a networked match pauses nobody but you, and the game
//     would merely look frozen).
//   * UOneBrahamHUDComponent creates this widget and shows or hides it (and
//     steps the HUD out while it is up).
//   * This widget owns what happens inside it: which page is up, focus, the
//     confirmation card, and the back key.
//
// BACK
//   Escape and the gamepad's B / Start step back one level - card, then
//   FACILITIES, then the menu itself - which is the one rule every menu stack
//   should have and the reason the key is handled here rather than by the
//   controller's input action.
//
// The two exits ask first, on a visitor's badge, with STAY focused, because a
// second press of the confirm key is the most common way to leave a match by
// accident.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "OneBrahamPauseMenuWidget.generated.h"

class UOneBrahamPaintedControl;
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

	/** The widget that should hold focus right now: the card's safe answer,
	 *  the settings tab, or RESUME. */
	UWidget* GetDefaultFocusTarget() const;

	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

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

	/** One step back: card, settings, then the menu. */
	void GoBack();

	/** Asks the controller to close the menu; it restores input and unpauses. */
	void RequestResume();

	void LeaveMatch();
	void QuitToDesktop();

	/** A directory row, as the front end's HOME draws them. */
	UOneBrahamPaintedControl* MakeDirectoryRow(const FText& Verb, const FText& Where, TFunction<void()> OnChosen);

	/** A painted layer that takes no focus: the masthead, the frost, the card. */
	UOneBrahamPaintedControl* MakeLayer(const FVector2D& Size);

	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget> MainPage = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamSettingsWidget> Settings = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Row_Resume = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Row_Settings = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Row_Leave = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Row_Quit = nullptr;

	UPROPERTY(Transient) TObjectPtr<UWidget> ConfirmLayer = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_ConfirmCancel = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_ConfirmAccept = nullptr;

	EPendingConfirm PendingConfirm = EPendingConfirm::None;
	bool bOpen = false;

	/** Real time the menu opened and the card came up: the frost and the
	 *  entrance are a function of these (the world may be paused). */
	double OpenedAt = 0.0;
	double ConfirmAt = 0.0;
};
