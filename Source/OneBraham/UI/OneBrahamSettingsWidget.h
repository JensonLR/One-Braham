// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSettingsWidget.h
// ---------------------------------------------------------------------------
// Settings: video, audio, controls, gameplay, accessibility (Docs/08 §4).
//
// Every control writes through UOneBrahamGameUserSettings as it moves, so the
// change is visible behind the menu at once; closing the screen saves. The
// controls page lists every rebindable action with its keys for the chosen
// device. Clicking a key listens for the next press and writes it through
// UOneBrahamInputBindings, which saves it and rebuilds the live bindings.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "OneBrahamSettingsWidget.generated.h"

UENUM()
enum class EOneBrahamSettingsPage : uint8
{
	Video,
	Audio,
	Controls,
	Gameplay,
	Accessibility
};

UCLASS()
class ONEBRAHAM_API UOneBrahamSettingsWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	virtual void GoBack() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

protected:
	virtual void BuildPage() override;
	virtual FText GetPageTitle() const override;
	virtual FText GetFooterText() const override;

	void BuildTabs();
	void BuildVideo();
	void BuildAudio();
	void BuildControls();
	void BuildGameplay();
	void BuildAccessibility();

	/** Starts listening for the key that replaces OldKey on Action. */
	void BeginCapture(FName Action, const FKey& OldKey);

	/** Writes a captured key, or cancels on Escape. */
	void FinishCapture(const FKey& Key);

	EOneBrahamSettingsPage Page = EOneBrahamSettingsPage::Video;

	/** The controls page shows one device at a time. */
	bool bGamepadBindings = false;

	bool bCapturing = false;
	FName CaptureAction = NAME_None;
	FKey CaptureOldKey;

	/** The last rebind's outcome, shown above the list. */
	FText RebindStatus;
};
