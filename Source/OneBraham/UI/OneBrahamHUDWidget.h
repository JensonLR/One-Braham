// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDWidget.h
// ---------------------------------------------------------------------------
// The combat HUD.
//
// TWO WAYS TO GET A HUD
//   1. The shipped build uses WBP_OneBrahamHUD, a Blueprint subclass authored by
//      a designer. Its widget tree wins: this class only fills in the values.
//   2. With no content at all, RebuildWidget() constructs the entire layout in
//      C++ - compass, crosshair, panels, bars, overlays - so a clean checkout is
//      fully playable and every readout exists.
//
// The layout rules that matter (see Docs/07_UI.md for the full spec):
//   * Ammo bottom-right, health bottom-left: the two things a player checks
//     without thinking are the two things in the corners they already look at.
//   * Squad top-right, kill feed below it: information you glance at.
//   * Objective state (floor, lockdown, extraction) top-left, always visible.
//   * Nothing in the centre of the screen except the crosshair, the hit marker
//     and a two-line banner: the middle of the screen is where the game happens.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamHUDTypes.h"
#include "OneBrahamHUDWidget.generated.h"

class UOneBrahamCrosshairWidget;
class UOneBrahamCompassWidget;
class UTextBlock;
class UProgressBar;
class UImage;
class UCanvasPanel;
class UWrapBox;
class UVerticalBox;
class UWidget;
class UButton;

UCLASS()
class ONEBRAHAM_API UOneBrahamHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamHUDWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// ======================================================================
	//  Data in
	// ======================================================================
	/** The single entry point from UOneBrahamHUDComponent. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ApplyHUDData(const FOneBrahamHUDData& NewData);

	// ======================================================================
	//  Overlays (driven by the controller)
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetScoreboardVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetDeathScreenVisible(bool bVisible, const FText& CauseText = FText::GetEmpty());

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void TogglePauseMenu(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetDownedVisible(bool bDowned);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ShowBanner(const FText& Text, float DurationSeconds, FLinearColor Colour);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ShowSubBanner(const FText& Text, float DurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ShowSubtitle(const FText& Text, const FString& SpeakerName, float DurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void AddWorldPing(const FOneBrahamHUDWorldPing& Ping);

	/** True when this instance built its own layout (no authored Blueprint). */
	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	bool IsCodelessLayout() const { return bBuiltLayoutInCode; }

protected:
	/** World time of the last paint. Overlay timers advance by the difference:
	 *  paint is the only callback this widget is guaranteed every frame, and a
	 *  widget that expects NativeTick to be called has a bug it cannot see. */
	mutable double LastPaintTimeSeconds = -1.0;

	// --- layout construction ---------------------------------------------
	/** Builds the whole tree in code when the Blueprint has none. */
	void BuildDefaultLayout();

	UTextBlock* MakeText(FName Name, EOneBrahamTextSize Size, const FLinearColor& Colour,
		ETextJustify::Type Justify = ETextJustify::Left, bool bBold = false);

	UProgressBar* MakeBar(FName Name, const FLinearColor& FillColour, float Thickness);

	/** Adds a widget to the root canvas at a screen-relative position. */
	void AddToCanvas(UWidget* Widget, const FVector2D& Position, const FVector2D& Size,
		const FAnchors& Anchor, const FVector2D& Alignment);

	/** Adds a widget filling the whole canvas (anchors 0,0 - 1,1, zero offsets). */
	void AddStretched(UWidget* Widget);

	// --- pause menu ---------------------------------------------------------
	/** Resume, settings, leave: the code-built buttons on Panel_PauseMenu. */
	void BuildPauseMenuButtons();

	/** One outlined, signal-blue-on-hover menu button with an upper-case label. */
	UButton* MakeMenuButton(FName Name, const FText& Label, float OffsetY);

	/** Binds the three buttons, authored or code-built; safe to call repeatedly. */
	void BindPauseMenuButtons();

	UFUNCTION()
	void HandlePauseResume();

	UFUNCTION()
	void HandlePauseSettings();

	UFUNCTION()
	void HandlePauseLeave();

	// --- time-based overlays ---------------------------------------------
	/** Decays the banner, sub-banner and subtitle timers and eases the damage
	 *  flash. Called from NativePaint with the real elapsed time since the last
	 *  paint, because this widget does not tick - see NativePaint. */
	void AdvanceOverlayTimers(float DeltaSeconds) const;

	// --- panel update helpers --------------------------------------------
	void UpdateHealthPanel(const FOneBrahamHUDData& InData);
	void UpdateWeaponPanel(const FOneBrahamHUDData& InData);
	void UpdateObjectivePanel(const FOneBrahamHUDData& InData);
	void UpdateSquadPanel(const FOneBrahamHUDData& InData);
	void UpdateKillFeed(const FOneBrahamHUDData& InData);
	void UpdateInteractionPanel(const FOneBrahamHUDData& InData);
	void UpdateVoicePanel(const FOneBrahamHUDData& InData);
	void UpdateCompassData(const FOneBrahamHUDData& InData);
	void UpdateOverlays(const FOneBrahamHUDData& InData);

	FText BuildUtilityRow(const FOneBrahamHUDData& InData) const;
	FText BuildAccessRow(const FOneBrahamHUDData& InData) const;

	// --- cached bindings (either the Blueprint's or ours) ----------------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UOneBrahamCrosshairWidget> Crosshair = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UOneBrahamCompassWidget> Compass = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Ammo = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_WeaponName = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_FireMode = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Utilities = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Access = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Health = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UProgressBar> Bar_Health = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UProgressBar> Bar_Armour = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UProgressBar> Bar_Reload = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Floor = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Objective = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UProgressBar> Bar_Objective = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Clock = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Squad = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_KillFeed = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Interaction = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UProgressBar> Bar_Interaction = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Voice = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Banner = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_SubBanner = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_Subtitle = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_DeathCause = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UTextBlock> Text_BleedOut = nullptr;

	/** Fills while a colleague is pulling this player up. Hidden when nobody is. */
	TObjectPtr<UProgressBar> Bar_Revive = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UImage> Image_DamageFlash = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UCanvasPanel> Panel_Scoreboard = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UCanvasPanel> Panel_DeathScreen = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UCanvasPanel> Panel_PauseMenu = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UButton> Button_PauseResume = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UButton> Button_PauseSettings = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UButton> Button_PauseLeave = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UCanvasPanel> Panel_Downed = nullptr;

	/** The canvas everything else is added to. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|HUD")
	TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

	// --- state ------------------------------------------------------------
	FOneBrahamHUDData Data;

	mutable float BannerRemaining = 0.f;
	mutable float SubBannerRemaining = 0.f;
	mutable float SubtitleRemaining = 0.f;

	/** Ping actors are drawn in world space by the map marker component; the HUD
	 *  keeps the list only so the compass can use it as a fallback. */
	TArray<FOneBrahamHUDWorldPing> RecentPings;

	bool bBuiltLayoutInCode = false;
};
