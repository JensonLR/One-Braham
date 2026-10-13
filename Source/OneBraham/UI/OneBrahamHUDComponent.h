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
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetScoreboardVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetDeathScreenVisible(bool bVisible, const FText& CauseText = FText::GetEmpty());

	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void SetPauseMenuVisible(bool bVisible);

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

	/** From the health component when damage lands: direction + intensity. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void NotifyDamageTaken(float Damage, const FVector& InstigatorLocation);

	/** From the downed component: the bleed-out bar and the revive prompt. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void NotifyDownedStateChanged(bool bDowned, float BleedOutRemaining);

	/** From the interaction component: the hold-to-interact progress. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|HUD")
	void NotifyInteractionProgress(const FText& Prompt, float Progress01);

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

	/** Reads the visibility switches from the user's settings. */
	void ApplyAccessibilitySettings();

	// --- delegate handlers -------------------------------------------------
	UFUNCTION() void HandleHealthChanged(float NewHealth, float MaxHealthValue, float Delta);
	UFUNCTION() void HandleArmourChanged(float NewArmour, float MaxArmourValue);
	UFUNCTION() void HandleInventoryChanged();
	UFUNCTION() void HandleVoiceLineSpoken(APlayerState* Speaker, FName LineId, const FText& Subtitle, float DurationSeconds);
	UFUNCTION() void HandleHitConfirmed(bool bHeadshot, bool bKill, float DistanceCm);

	/** Resolves the pawn's components once, on bind. */
	void CacheComponents();

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

	/** Transient feedback timers, seconds remaining. */
	float HitMarkerRemaining = 0.f;
	float DamageIndicatorRemaining = 0.f;
	float BannerRemaining = 0.f;
	float SubBannerRemaining = 0.f;
	float SubtitleRemaining = 0.f;
	float KillFeedPruneTimer = 0.f;

	bool bLastHitHeadshot = false;
	bool bLastHitWasKill = false;
	bool bIsBound = false;

	/** Cached user settings switches. */
	void ReadUserSettings();

	/** Set to false on a dedicated server: no widget, no gathering. */
	bool bHUDEnabled = true;
};
