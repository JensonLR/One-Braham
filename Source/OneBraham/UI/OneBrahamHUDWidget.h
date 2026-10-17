// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDWidget.h
// ---------------------------------------------------------------------------
// The combat HUD: "the building speaks".
//
// THE IDEA
//   Every device on this HUD is something a player can walk up to in One
//   Braham. The floor you are on is the stair core's level sign - a white plate
//   with navy numerals. The floors above and below are the lift indicator. The
//   lockdown is the fire-alarm panel by the lifts, LEDs and an LCD strip. Your
//   squad is the in/out board, the kill feed is the sign-out log stamped with
//   the building's own clock, your health is your staff pass, and the building
//   talks through the same announcement cards its OLED screens show. The tone
//   is the building's: dry, procedural, and never wrong about what it is doing.
//
// CLARITY IS SACRED
//   Health, ammo, the objective, the lockdown timer, the compass, the crosshair
//   and the damage arcs are always drawn at full strength while they matter.
//   Only the floor stack (55%) and the squad board (70%) rest-dim, and the
//   "HUD rest dimming" setting turns even that off.
//
// TWO WAYS TO GET A HUD
//   1. A Blueprint subclass (WBP_OneBrahamHUD) with its own widget tree: this
//      class fills in the bound widgets and draws nothing of its own.
//   2. With no authored tree, RebuildWidget() builds a canvas holding the
//      compass, and everything else is painted in NativePaint - the floor
//      plate, the fire panel, the staff pass, the stage cards - because paint
//      is the one callback a visible widget is guaranteed every frame
//      (Docs/08), and because a split-flap or a strikethrough is a function of
//      time, not a stack of widgets animated from outside.
//
// DATA
//   UOneBrahamHUDComponent gathers; this widget draws. ApplyHUDData (the
//   long-standing snapshot) and ApplyHUDFrame (the building, the clock, the
//   objective, the markers) arrive together at 15 Hz. Events that must not be
//   missed between gathers - a hit taken, an announcement, a stage change -
//   arrive as calls. Everything that moves is a tween against the world clock.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Text/TextLayout.h"
#include "UI/OneBrahamHUDTypes.h"
#include "UI/OneBrahamUIStyle.h"
#include "UI/HUD/OneBrahamHUDFrame.h"
#include "UI/HUD/OneBrahamTween.h"
#include "OneBrahamHUDWidget.generated.h"

class UOneBrahamCrosshairWidget;
class UOneBrahamCompassWidget;
class UOneBrahamPaintedControl;
class UTextBlock;
class UProgressBar;
class UImage;
class UCanvasPanel;
class UWrapBox;
class UVerticalBox;
class UWidget;
struct FOneBrahamPainter;

/** The stage cards: the three moments the whole screen is allowed to speak. */
enum class EOneBrahamStageCard : uint8
{
	None,
	Lockdown,
	Purge,
	Roof
};

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

	/** The building, the clock, the objective and the markers; same gather. */
	void ApplyHUDFrame(const FOneBrahamHUDFrame& NewFrame);

	/** A PA line: the card at the top of the screen, while the tannoy speaks. */
	void PushAnnouncement(const FText& Headline, const FText& Subline, EOneBrahamLockdownStage Stage, float HoldSeconds);

	/** One of the three full-width moments: LOCKDOWN, PURGE, THE ROOF. */
	void ShowStageCard(EOneBrahamStageCard Card);

	/** A hit taken: the arc, the ghost on the pass, the heavy-hit split. */
	void PushDamage(float Damage, const FVector& FromLocation, bool bHasSource, int32 FloorDelta);

	/** Any fire input: halves what is left of a stage card's hold. */
	void NotifyFireInput();

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

	/** The floor plan is open: it replaces the floor stack and the squad board. */
	void SetPlanOpen(bool bOpen) { bPlanOpen = bOpen; }

	/** The tenant directory is up over the middle of the screen: the PA card and
	 *  the reader prompt hold back rather than paint through it. */
	void SetBoardOpen(bool bOpen) { bBoardOpen = bOpen; }

	/** True while a stage card holds the screen (announcements wait for it). */
	bool IsStageCardUp(double Now) const { return StageCard != EOneBrahamStageCard::None && Now < StageCardAt + 0.35 + StageCardHold + 0.3; }

	/** True while the accident book (the death screen) is up. */
	bool IsDeathScreenVisible() const { return bDeathScreenVisible; }

	/**
	 * Splits a death-screen text into its cause line and the recap's lines and
	 * reads each recap line into columns: "Alex - 4 hits, 112 damage (1 head,
	 * 3 body) at 18 m" -> {Alex, 4, 112, "1 head, 3 body", 18 m}. Lines that do
	 * not parse are kept whole. Pure, for the tests.
	 */
	struct FRecapRow
	{
		FString Attacker;
		FString Hits;
		FString Damage;
		FString Locations;
		FString Distance;
		bool bParsed = false;
		FString Raw;
	};
	static void ParseDeathText(const FText& Text, FString& OutCause, TArray<FRecapRow>& OutRows, FString& OutRemaining);

protected:
	// --- layout construction ---------------------------------------------
	/** Builds the canvas and the compass when the Blueprint has no tree. */
	void BuildDefaultLayout();

	UTextBlock* MakeText(FName Name, EOneBrahamTextSize Size, const FLinearColor& Colour,
		ETextJustify::Type Justify = ETextJustify::Left, bool bBold = false);

	UProgressBar* MakeBar(FName Name, const FLinearColor& FillColour, float Thickness);

	/** Adds a widget to the root canvas at a screen-relative position. */
	void AddToCanvas(UWidget* Widget, const FVector2D& Position, const FVector2D& Size,
		const FAnchors& Anchor, const FVector2D& Alignment);

	/** Adds a widget filling the whole canvas (anchors 0,0 - 1,1, zero offsets). */
	void AddStretched(UWidget* Widget);

	// --- the authored path's bindings --------------------------------------
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

	// ======================================================================
	//  The painted HUD (UI/HUD/OneBrahamHUDElements.cpp)
	// ======================================================================
	/** Everything a painter needs to know about this frame. */
	struct FPaintContext
	{
		double Now = 0.0;
		FVector2D Screen = FVector2D(1920.f, 1080.f);
		float SafeL = 48.f;
		float SafeR = 1872.f;
		float SafeT = 38.f;
		float SafeB = 1042.f;
		float Scale = 1.f;
		EOneBrahamLockdownStage Stage = EOneBrahamLockdownStage::Normal;
		/** 0 calm, 1 lockdown, 2 purge. */
		int32 Band = 0;
		/** The stage accent and hairline, cross-faded over 400 ms. */
		FLinearColor Accent;
		FLinearColor Hairline;
		bool bHeavyOutline = false;
		bool bReduceMotion = false;
		bool bRestDimming = true;
		/** 0..1: secondary elements fade to 25% under a stage card. */
		float SecondaryFade = 1.f;
		bool bAlive = true;
	};

	void PaintHUD(FOneBrahamPainter& P, const FPaintContext& C) const;

	/** Fills the context for one paint; false when there is nothing to paint. */
	bool MakePaintContext(const FGeometry& Geometry, FPaintContext& C) const;

	/** What is painted beneath the child widgets (the stage card's letterbox). */
	void PaintUnderlay(FOneBrahamPainter& P, const FGeometry& Geometry) const;

	void PaintFloorPlate(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintFloorStack(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintObjective(FOneBrahamPainter& P, const FPaintContext& C, float& OutBottom) const;
	void PaintFireDrillPoints(FOneBrahamPainter& P, const FPaintContext& C, float Top, float& OutBottom) const;
	void PaintExtraction(FOneBrahamPainter& P, const FPaintContext& C, float Top, float& OutBottom) const;
	void PaintLockdownModule(FOneBrahamPainter& P, const FPaintContext& C, float& OutBottom) const;
	void PaintKillFeed(FOneBrahamPainter& P, const FPaintContext& C, float Top) const;
	void PaintSquadBoard(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintMicPill(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintHealthCard(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintAmmo(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintCrosshair(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintDamageArcs(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintAnnouncement(FOneBrahamPainter& P, const FPaintContext& C, float Top) const;
	void PaintStageCard(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintStageBars(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintPrompt(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintDowned(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintSubtitles(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintBanner(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintStageVignette(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintAccidentBook(FOneBrahamPainter& P, const FPaintContext& C) const;
	void PaintSpectating(FOneBrahamPainter& P, const FPaintContext& C) const;

	/** The world clock the HUD animates against. */
	double HUDNow() const;

	/** The stage the tint is fading towards, and its 400 ms cross-fade. */
	void UpdateStageTint(double Now, FPaintContext& C) const;

	// --- cached bindings (either the Blueprint's or ours) ----------------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UOneBrahamCrosshairWidget> Crosshair = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "One Braham|HUD")
	TObjectPtr<UOneBrahamCompassWidget> Compass = nullptr;

	/** The canvas's first child: paints beneath the compass. */
	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamPaintedControl> Underlay = nullptr;

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

	/** Fills while a colleague is pulling this player up (authored HUDs). */
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
	TObjectPtr<UCanvasPanel> Panel_Downed = nullptr;

	/** The canvas everything else is added to. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|HUD")
	TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

	// --- state ------------------------------------------------------------
	FOneBrahamHUDData Data;
	FOneBrahamHUDFrame Frame;

	/** Ping actors are drawn in world space by the map marker component; the HUD
	 *  keeps the list so the compass can show the six-second diamonds. */
	TArray<FOneBrahamHUDWorldPing> RecentPings;

	bool bBuiltLayoutInCode = false;
	bool bPlanOpen = false;
	bool bBoardOpen = false;

	// ======================================================================
	//  Animation state. Every value is a start time and a target, evaluated in
	//  paint against HUDNow(): nothing here ticks.
	// ======================================================================
	/** The floor plate's split-flap. */
	int32 ShownFloor = INDEX_NONE;
	int32 PreviousFloor = INDEX_NONE;
	double FloorChangedAt = -100.0;
	/** The line under the plate retypes on a floor change. */
	FString PlateArea;
	double PlateAreaAt = -100.0;

	/** Rest-dimming clocks: when each secondary element last changed. */
	FString StackSignature;
	double StackChangedAt = -100.0;
	bool bStackRevealed = false;
	FString SquadSignature;
	double SquadChangedAt = -100.0;

	/** The objective line and its OVERRIDE. */
	FString ObjectiveShown;
	FString ObjectiveDetailShown;
	double ObjectiveChangedAt = -100.0;
	FString OverriddenLine;
	FString OverriddenDetail;
	double OverrideAt = -100.0;
	int32 OverrideBand = 0;

	/** Stage tint cross-fade. */
	int32 TintBand = 0;
	int32 TintFromBand = 0;
	double TintChangedAt = -100.0;
	EOneBrahamLockdownStage LastStage = EOneBrahamLockdownStage::Normal;
	bool bStagePrimed = false;
	/** When the fire panel's stage began: the LED blinks for six seconds. */
	double StageStartedAt = -100.0;
	/** The panel's LCD log, newest last, and when the last line arrived. */
	TArray<FString> LCDLines;
	double LCDScrolledAt = -100.0;
	int32 LastSealCeiling = INDEX_NONE;

	/** The staff pass. */
	float ShownHealth = 100.f;
	float GhostFrom = 0.f;
	float GhostTo = 0.f;
	double GhostAt = -100.0;
	double HealAt = -100.0;
	float HealFrom = 0.f;
	double NudgeAt = -100.0;
	float LastArmour = 0.f;
	double ArmourBreakAt = -100.0;
	float CountFrom = 100.f;
	double CountAt = -100.0;

	/** The magazine. */
	int32 LastAmmo = -1;
	double ShotAt = -100.0;
	EOneBrahamFireMode LastFireMode = EOneBrahamFireMode::Auto;
	EOneBrahamFireMode PrevFireMode = EOneBrahamFireMode::Auto;
	double FireModeAt = -100.0;
	bool bWasReloading = false;
	double ReloadStartAt = -100.0;

	/** Hit markers: the last confirmed hit, read from the snapshot's rising edge. */
	float LastHitMarkerAlpha = 0.f;
	double HitAt = -100.0;
	bool bHitHeadshot = false;
	bool bHitKill = false;

	/** Damage arcs, newest last. */
	struct FDamageArc
	{
		FVector From = FVector::ZeroVector;
		float Damage = 0.f;
		double At = 0.0;
		int32 FloorDelta = 0;
	};
	TArray<FDamageArc> DamageArcs;
	double HeavyHitAt = -100.0;

	/** PA lines, one at a time. */
	struct FAnnouncement
	{
		FString Headline;
		FString Subline;
		FString Eyebrow;
		EOneBrahamLockdownStage Stage = EOneBrahamLockdownStage::Normal;
		float Hold = 4.f;
		double At = -1.0;
	};
	TArray<FAnnouncement> AnnouncementQueue;
	mutable FAnnouncement CurrentAnnouncement;

	/** The stage card. */
	EOneBrahamStageCard StageCard = EOneBrahamStageCard::None;
	double StageCardAt = -100.0;
	mutable float StageCardHold = 2.f;
	FString StageCardEyebrow;

	/** The prompt's reader: the success flash when a hold completes. */
	float LastHoldProgress = 0.f;
	double ReaderAcceptedAt = -100.0;
	FString LastPromptText;

	/** Banner and sub-banner (kept from the old HUD's API: warnings, induction). */
	FString BannerShown;
	FLinearColor BannerColour = FLinearColor::White;
	double BannerAt = -100.0;
	float BannerSeconds = 0.f;
	FString SubBannerShown;
	double SubBannerAt = -100.0;
	float SubBannerSeconds = 0.f;

	/** Subtitles. */
	FString SubtitleShown;
	FString SubtitleSpeaker;
	double SubtitleAt = -100.0;
	float SubtitleSeconds = 0.f;

	/** Downed, and the accident book. */
	double DownedAt = -100.0;
	bool bWasDowned = false;
	bool bDeathScreenVisible = false;
	double DeathAt = -100.0;
	FText DeathText;
	int32 DeathEntryNumber = 47;
	FString DeathTime;
	FString DeathLocation;
};
