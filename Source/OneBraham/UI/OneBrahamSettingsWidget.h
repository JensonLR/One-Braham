// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSettingsWidget.h
// ---------------------------------------------------------------------------
// The in-match settings screen: GRAPHICS, AUDIO, VOICE, GAMEPLAY, CONTROLS.
//
// HOW A CHANGE TRAVELS
//   Every control writes straight into UOneBrahamGameUserSettings, through the
//   settings' own setter wherever one exists (SetVolume, SetVoiceInputGain,
//   SetQualityPreset ...) and into the field where it does not. Cheap things
//   are heard or seen at once - a volume, a voice gain - because a slider that
//   does nothing until you press a button is a slider nobody trusts. Expensive
//   things (scalability, resolution) wait for ApplySettings, which runs on
//   APPLY and on BACK, followed by SaveSettings. Leaving the screen never
//   discards a change: the player moved the slider, so the player meant it.
//
//   ApplySettings broadcasts OnSettingsChanged, which the player controller
//   already listens to for FOV and sensitivity, and the HUD for its scale.
//
// CONTROLS TAB
//   Read-only. It lists what the Enhanced Input mapping contexts actually bind,
//   read from the contexts rather than from a table in this file, so it cannot
//   describe a binding the game does not have.
//
// The screen has no opinion about where it lives: the pause menu hosts it
// today, and the main menu can host the same class tomorrow.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "OneBrahamSettingsWidget.generated.h"

class APlayerController;
struct FKey;
class UOneBrahamGameUserSettings;
class UImage;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

/** One line of the Controls tab: an action and what triggers it. */
struct FOneBrahamBindingLine
{
	FText Action;
	FText KeyboardMouse;
	FText Controller;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamSettingsWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamSettingsWidget(const FObjectInitializer& ObjectInitializer);

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Raised after BACK has applied and saved. The host closes the page. */
	FSimpleDelegate OnBackRequested;

	/** Pushes the stored settings into every control. Call on open. */
	void SyncFromSettings();

	/** Focus to the active tab's button, for keyboard and gamepad. */
	void FocusDefault();

	/** The active tab's button: where focus belongs when the page opens. */
	UWidget* GetDefaultFocusTarget() const;

	void SelectTab(int32 Index);

	/** Shoulder buttons: the next or previous tab, wrapping. */
	void CycleTab(int32 Direction);

	/** ApplySettings + SaveSettings, and the confirmation in the footer. */
	void ApplyAndSave();

	/** Applies and saves if anything changed, then raises OnBackRequested. */
	void RequestBack();

	/** Applies and saves if anything changed. For a host that closes without BACK. */
	void ApplyIfDirty();

	bool HasUnappliedChanges() const { return bDirty; }

	/**
	 * The Controls tab's lines, from the mapping contexts the player actually
	 * has: Owner's gameplay and common contexts when it is a One Braham
	 * controller (the ini-built runtime context included, rebinds and all),
	 * else the contexts UOneBrahamGameSettings names.
	 */
	static TArray<FOneBrahamBindingLine> GatherBindings(const APlayerController* Owner = nullptr);

	/** A key as the Controls tab prints it: "MOUSE", "LEFT STICK", "A / CROSS". */
	static FString KeyLabel(const FKey& Key);

protected:
	virtual void BuildLayout() override;

	// --- page construction -------------------------------------------------
	UVerticalBox* AddPage(const FText& TabName);

	void AddSection(UVerticalBox* Page, const FText& Title);

	void AddNote(UVerticalBox* Page, const FText& Text, UTextBlock** OutText = nullptr);

	void AddSlider(UVerticalBox* Page, const FText& Label, float Min, float Max, float Step,
		TFunction<float(const UOneBrahamGameUserSettings&)> Get,
		TFunction<void(UOneBrahamGameUserSettings&, float)> Set,
		TFunction<FText(float)> Format);

	void AddToggle(UVerticalBox* Page, const FText& Label,
		TFunction<bool(const UOneBrahamGameUserSettings&)> Get,
		TFunction<void(UOneBrahamGameUserSettings&, bool)> Set);

	void AddChoice(UVerticalBox* Page, const FText& Label, const TArray<FText>& Options,
		TFunction<int32(const UOneBrahamGameUserSettings&)> Get,
		TFunction<void(UOneBrahamGameUserSettings&, int32)> Set);

	/** Label | control | value, the three columns every setting is set in. */
	void AddSettingRow(UVerticalBox* Page, const FText& Label, UWidget* Control, UTextBlock* Value);

	void BuildGraphicsPage();
	void BuildAudioPage();
	void BuildVoicePage();
	void BuildGameplayPage();
	void BuildControlsPage();

	/**
	 * (Re)fills the Controls tab's rows from GatherBindings. Run when the page is
	 * built and again whenever the tab is shown: the pause menu builds this
	 * widget when the HUD is created, which can be before the controller has
	 * resolved its contexts (a client given its local player late).
	 */
	void RefreshControlsRows();

	void MarkDirty();

	/** Microphone state for the Voice tab. Polled from paint while that tab is up. */
	void RefreshVoiceStatus() const;

	/** Re-reads every control's value from the settings. Built with the controls. */
	TArray<TFunction<void()>> Syncers;

	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TabUnderlines;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Status = nullptr;

	// Voice tab live readouts.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_MicPermission = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_MicDevice = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_MicActivity = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage> MicLamp = nullptr;
	UPROPERTY(Transient) TObjectPtr<class USizeBox> MicMeterFill = nullptr;
	UPROPERTY(Transient) TObjectPtr<class USpacer> MicGateOffset = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage> MicMeterBar = nullptr;

	int32 ActiveTab = 0;
	int32 VoiceTabIndex = INDEX_NONE;
	int32 ControlsTabIndex = INDEX_NONE;

	/** The Controls tab's binding rows, under its fixed headings. */
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ControlsRows = nullptr;
	bool bDirty = false;

	/** Real time of the next microphone poll: ten a second is plenty for a lamp. */
	mutable double NextVoicePollSeconds = 0.0;
};
