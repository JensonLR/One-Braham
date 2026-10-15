// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDComponent.h
// ---------------------------------------------------------------------------
// Gathers everything the HUD needs and pushes it to the widget.
//
// RULES
//   * The component never draws. It owns data; the widget owns pixels.
//   * Only one method crosses the boundary: the widget's ApplyHUDData(snapshot).
//   * Gathering ticks at 15 Hz, not 60: health, ammo and objective state do not
//     change faster than that, and a HUD that costs 0.2 ms a frame at 60 Hz is a
//     HUD that a 240 Hz player never notices.
//   * Transient feedback (hit markers, damage flashes, banners) is event driven
//     and driven by delegates, so nothing is missed between gather ticks.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UI/OneBrahamHUDTypes.h"
#include "OneBrahamHUDComponent.generated.h"

class AOneBrahamPlayerController;
class AOneBrahamCharacter;
class UOneBrahamHUDWidget;
class UOneBrahamHealthComponent;
class UOneBrahamWeaponComponent;
class UOneBrahamInventoryComponent;
class UOneBrahamInteractComponent;
class UOneBrahamVoiceLineSet;
class UOneBrahamPauseMenuWidget;
class UOneBrahamScoreboardWidget;

/** Kill feed lines arrive from the game state; this is the HUD's own copy. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamHUDKillFeedUpdated, const TArray<FOneBrahamHUDKillFeedEntry>&, Entries);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamHUDComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamHUDComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ======================================================================
	//  Lifecycle (called by AOneBrahamPlayerController)
	// ======================================================================
	/** Creates the root widget. Safe to call on a dedicated server (does nothing). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void CreateHUD(AOneBrahamPlayerController* InOwningController);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void DestroyHUD();

	/** Subscribes to the new pawn's components. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void BindToPawn(APawn* InPawn);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void UnbindFromPawn();

	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	UUserWidget* GetRootWidget() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	UOneBrahamHUDWidget* GetHUDWidget() const { return HUDWidget; }

	// ======================================================================
	//  Widget commands
	// ======================================================================
	/**
	 * Re-reads the user's HUD settings and pushes the parts that are not per-frame
	 * data (HUD scale, opacity). Called when the HUD is created and by the player
	 * controller whenever the settings change, so an options menu change is
	 * visible immediately rather than after the next level load.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ApplyAccessibilitySettings();

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetScoreboardVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetDeathScreenVisible(bool bVisible, const FText& CauseText = FText::GetEmpty());

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetPauseMenuVisible(bool bVisible);

	/** The in-match menus, created on first use (see EnsureMenuWidgets). Null on a
	 *  dedicated server and before the first open. */
	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	UOneBrahamPauseMenuWidget* GetPauseMenuWidget() const { return PauseMenuWidget; }

	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	UOneBrahamScoreboardWidget* GetScoreboardWidget() const { return ScoreboardWidget; }

	// -----------------------------------------------------------------------
	//  Overlays the player asks for (the IA_ToggleHUD, IA_Map and
	//  IA_VoiceLineMenu actions). Each is owned here rather than by the widget
	//  so that a Blueprint-authored HUD and the C++ fallback behave identically,
	//  and so a headless test can drive them.
	// -----------------------------------------------------------------------

	/** Shows or hides the whole HUD. Session-only: the player's saved setting is
	 *  a scale and a set of element switches, not "hide everything". */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetHUDVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	bool IsHUDVisible() const { return bHUDVisible; }

	/** Opens or closes the floor plan, gathering its data on open. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetFloorPlanVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	bool IsFloorPlanVisible() const { return bFloorPlanVisible; }

	/** Opens or closes the comms wheel, gathering the line list on open. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetQuickLineMenuVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	bool IsQuickLineMenuVisible() const { return bQuickLineMenuVisible; }

	/** Rebuilds the plan or the wheel while it is open. Called when the lockdown
	 *  stage changes, a squadmate goes down, or a line's cooldown lapses - and
	 *  by the controller when the wheel opens, so the cooldowns it shows are the
	 *  ones the server will enforce a moment later. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void RefreshOverlays();

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ShowBanner(const FText& Text, float DurationSeconds, FLinearColor Colour);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void ShowSubBanner(const FText& Text, float DurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void AddWorldPing(const FVector& Location, const FString& InstigatorName, int32 Team, FName PingType = TEXT("Attention"));

	// ======================================================================
	//  Transient feedback
	// ======================================================================
	/** From the weapon component when the server confirms a hit. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void NotifyHitConfirmed(bool bHeadshot, bool bKill);

	/**
	 * From `AOneBrahamPlayerController::ClientDamageTaken`, on the machine that
	 * was hit: direction + intensity for the screen-edge arc.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void NotifyDamageTaken(float Damage, const FVector& InstigatorLocation);

	/** From the downed component: the bleed-out bar and the revive prompt. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void NotifyDownedStateChanged(bool bDowned, float BleedOutRemaining);

	/** Last hit-marker state, exposed for tests. */
	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	bool WasLastHitHeadshot() const { return bLastHitHeadshot; }

	// ======================================================================
	//  Data
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	const FOneBrahamHUDData& GetHUDData() const { return Data; }

	/** Forces a gather right now (used after a respawn so the HUD is never stale). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void RefreshNow();

	UPROPERTY(BlueprintAssignable, Category = "One Braham|HUD")
	FOneBrahamHUDKillFeedUpdated OnKillFeedUpdated;

	/** Debug: writes the whole HUD snapshot to LogOneBrahamUI.Needed by QA. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD|Debug")
	void DumpHUDState() const;

protected:
	/** Pulls continuous state from the pawn's components and the game state. */
	void GatherHUDData();

	// --- delegate handlers -------------------------------------------------
	UFUNCTION() void HandleHealthChanged(float NewHealth, float MaxHealthValue, float Delta);
	UFUNCTION() void HandleArmourChanged(float NewArmour, float MaxArmourValue);
	UFUNCTION() void HandleInventoryChanged();
	UFUNCTION() void HandleVoiceLineSpoken(APlayerState* Speaker, FName LineId, const FText& Subtitle, float DurationSeconds);
	UFUNCTION() void HandleHitConfirmed(bool bHeadshot, bool bKill, float DistanceCm);

	/** Resolves the pawn's components once, on bind. */
	void CacheComponents();

	/**
	 * Announces what just changed: the winch, the shutters, and a squadmate
	 * going down.
	 *
	 * The winch is here because nothing else tells the player: the extraction
	 * component moves through five states and the HUD only ever drew a bar. The
	 * shutters are not, because the lockdown component already reads the
	 * building out over the tannoy, in the building's own voice, and two
	 * announcements of the same event is worse than one.
	 *
	 * This is also the caller RefreshOverlays() was documented to have - "called
	 * when the lockdown stage changes, a squadmate goes down" - and did not.
	 */
	void AnnounceStateChanges();

	/** The copy table's text for a key, shown on the banner. */
	void Announce(FName Key, float DurationSeconds, FLinearColor Colour);

	/** The copy table's text for a key, shown on the sub-banner. */
	void AnnounceSub(FName Key, float DurationSeconds);

	/** Binds to the game state's announcement delegates once, lazily. */
	void BindToGameState();

	/** The tannoy's words, as a subtitle, while the tannoy audio plays. */
	UFUNCTION() void HandleServerAnnouncement(const FText& Announcement);

	/** A short imperative banner from the server. */
	UFUNCTION() void HandleWarningBanner(const FText& Text, float DurationSeconds, FLinearColor Colour);

	// --- cached components -------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "One Braham|HUD")
	AOneBrahamCharacter* GetBoundCharacter() const { return BoundCharacter.Get(); }

	// Weak refs on purpose: the HUD must never keep a dead pawn's components
	// alive. (UPROPERTY on a TWeakObjectPtr is rejected by UHT.)
	TWeakObjectPtr<AOneBrahamPlayerController> OwningController;
	TWeakObjectPtr<AOneBrahamCharacter> BoundCharacter;
	TWeakObjectPtr<UOneBrahamHealthComponent> CachedHealth;
	TWeakObjectPtr<UOneBrahamWeaponComponent> CachedWeapons;
	TWeakObjectPtr<UOneBrahamInventoryComponent> CachedInventory;
	TWeakObjectPtr<UOneBrahamInteractComponent> CachedInteraction;

	/** Widget class: the Blueprint subclass when authored, the C++ default otherwise. */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|HUD")
	TSubclassOf<UOneBrahamHUDWidget> HUDWidgetClass;

	UPROPERTY(Transient) TObjectPtr<UOneBrahamHUDWidget> HUDWidget = nullptr;

	/** The snapshot being filled and handed to the widget. */
	FOneBrahamHUDData Data;

	// --- change detectors for the announcements ---------------------------
	/** Extraction state as of the last tick. */
	EOneBrahamExtractionState LastExtractionState = EOneBrahamExtractionState::Inactive;
	/** Lockdown stage as of the last tick, so the overlays rebuild when it moves. */
	EOneBrahamLockdownStage LastLockdownStage = EOneBrahamLockdownStage::Normal;
	/** Squadmates in Downed last tick; a rise is the event worth a sub-banner. */
	int32 LastDownedSquadmates = 0;
	/** Set once the game state's delegates are bound. */
	bool bBoundToGameState = false;

	/**
	 * How long a tannoy announcement stays on screen as a subtitle.
	 *
	 * The lockdown paragraph is around twenty words: at a comfortable reading
	 * speed that is five seconds, and reading it twice is the point of a
	 * subtitle. The audio runs about the same length.
	 */
	static constexpr float TannoySubtitleSeconds = 6.f;
	/** Set after the first AnnounceStateChanges, so a spawn is not a change. */
	bool bAnnouncementsPrimed = false;

	/** Transient feedback timers, seconds remaining. */
	float HitMarkerRemaining = 0.f;
	float DamageIndicatorRemaining = 0.f;

	/** Strength of the last hit's indicator (a graze whispers), 0.25..1. */
	float DamageIndicatorStrength = 1.f;

	/** Where the last hit came from, in the world. The arc's bearing is recomputed
	 *  from it against the current view every frame, so it keeps pointing at the
	 *  shooter while the player turns to face them. */
	FVector DamageInstigatorLocation = FVector::ZeroVector;
	bool bHasDamageInstigator = false;

	/** Bearing of DamageInstigatorLocation against the local view, degrees clockwise from straight ahead. */
	void UpdateDamageDirection();
	float BannerRemaining = 0.f;
	float SubBannerRemaining = 0.f;
	float SubtitleRemaining = 0.f;
	float KillFeedPruneTimer = 0.f;

	bool bLastHitHeadshot = false;
	bool bLastHitWasKill = false;
	bool bIsBound = false;

	/** Cached user settings switches. */
	void ReadUserSettings();

	/** The HUD scale currently applied to the widget, so it is only pushed on change. */
	float CachedHUDScale = 1.f;

	/** Set to false on a dedicated server: no widget, no gathering. */
	bool bHUDEnabled = true;

	// --- overlay state -----------------------------------------------------
	/** True unless the player has hidden the HUD with IA_ToggleHUD. */
	bool bHUDVisible = true;

	bool bFloorPlanVisible = false;
	bool bQuickLineMenuVisible = false;

	/** The floor plan widget. Created on first open, kept afterwards: it is one
	 *  widget with no children, and recreating it on every press would cost more
	 *  than it saves. */
	UPROPERTY(Transient)
	TObjectPtr<class UOneBrahamFloorPlanWidget> FloorPlanWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<class UOneBrahamQuickLineWidget> QuickLineWidget = nullptr;

	/** Fills PlanData from the game state, the squad and the extraction. */
	void GatherFloorPlanData(FOneBrahamFloorPlanData& OutData) const;

	/** Fills QuickLineData: the eight lines, with live cooldown state. */
	void GatherQuickLineData(FOneBrahamQuickLineData& OutData) const;

	/** Creates the two overlay widgets on demand, on the same viewport layer
	 *  ordering as the HUD: below the HUD's own panels, above the world. */
	void EnsureOverlayWidgets();

	/**
	 * The scoreboard and the pause menu. Separate viewport widgets rather than
	 * panels inside the HUD, because the HUD is hit-test invisible by design (it
	 * must never take a click from the player) and both of these are clicked.
	 * Above the HUD and the overlays; the pause menu above the scoreboard.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamScoreboardWidget> ScoreboardWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamPauseMenuWidget> PauseMenuWidget = nullptr;

	/** Viewport layers. The overlays use 1 and 2; the HUD 0. */
	static constexpr int32 ScoreboardZOrder = 20;
	static constexpr int32 PauseMenuZOrder = 30;

	/** Creates the scoreboard and pause menu on demand. */
	void EnsureMenuWidgets();

	/** Forwarded from the wheel: the player chose a line. */
	UFUNCTION()
	void HandleQuickLineChosen(FName LineId);
};
