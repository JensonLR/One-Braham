// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameSettings.h
// ---------------------------------------------------------------------------
// Project-wide, code-readable settings. Exposed in Project Settings >
// ONE BRAHAM, and mirrored in Config/DefaultEngine.ini + DefaultGame.ini so
// that a fresh clone is playable before anyone opens the editor.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataAsset.h"
#include "OneBrahamGameSettings.generated.h"

class UInputMappingContext;
class UInputAction;
class UOneBrahamWeaponDataAsset;
class UOneBrahamCharacterDataAsset;
class UOneBrahamMatchSettingsAsset;
class UOneBrahamVoiceLineDataAsset;
class USoundBase;

// ==========================================================================
//  UOneBrahamGameSettings - brand, match + security tuning
// ==========================================================================
UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "ONE BRAHAM"))
class ONEBRAHAM_API UOneBrahamGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UOneBrahamGameSettings();

	/** Convenience accessor; never returns null in a packaged build. */
	static const UOneBrahamGameSettings& Get();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// --- Brand ------------------------------------------------------------
	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FLinearColor BrandPrimary = FLinearColor(0.000f, 0.439f, 0.753f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FLinearColor BrandNavy = FLinearColor(0.016f, 0.063f, 0.145f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FLinearColor BrandSignalBlue = FLinearColor(0.078f, 0.522f, 1.000f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FLinearColor BrandSafetyYellow = FLinearColor(1.000f, 0.804f, 0.000f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FLinearColor BrandEmergencyRed = FLinearColor(0.867f, 0.118f, 0.122f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FLinearColor BrandLightGrey = FLinearColor(0.878f, 0.898f, 0.922f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FString BuildingName = TEXT("ONE BRAHAM");

	UPROPERTY(config, EditAnywhere, Category = "Brand")
	FString BuildingAddress = TEXT("1 Braham Street, Aldgate, London E1");

	/** Number of occupied floors above the plaza. */
	UPROPERTY(config, EditAnywhere, Category = "Brand", meta = (ClampMin = "1", ClampMax = "40"))
	int32 Floors = 15;

	/** Height of the Skydeck above street level, metres (used by fall damage + skybox). */
	UPROPERTY(config, EditAnywhere, Category = "Brand")
	float RooftopHeightMeters = 78.f;

	// --- Match ------------------------------------------------------------
	/** Authoritative match settings asset. If unset, compiled defaults are used. */
	UPROPERTY(config, EditAnywhere, Category = "Match")
	TSoftObjectPtr<UOneBrahamMatchSettingsAsset> DefaultMatchSettings;

	UPROPERTY(config, EditAnywhere, Category = "Match")
	EOneBrahamMatchMode DefaultMode = EOneBrahamMatchMode::Ascent;

	/** Team sizes the matchmaker will fill for FIRE DRILL. */
	UPROPERTY(config, EditAnywhere, Category = "Match")
	TArray<int32> FireDrillTeamSizes = { 6, 8 };

	// --- Security / anti-cheat -------------------------------------------
	UPROPERTY(config, EditAnywhere, Category = "Security")
	float MaxFireRateTolerance = 0.08f;

	UPROPERTY(config, EditAnywhere, Category = "Security")
	float MaxMovementSpeedTolerance = 1.35f;

	UPROPERTY(config, EditAnywhere, Category = "Security")
	/**
	 * Server-side slack on the interaction distance, in centimetres.
	 *
	 * NOT the player's reach - that is OneBrahamConstants::InteractRangeCm. This
	 * is what the server adds before it refuses a request, because the client's
	 * position when it asked is not the position the server has: a player who
	 * pressed interact at 210 cm while backing away at 400 cm/s is 220 cm away
	 * by the time the RPC lands, and refusing that interaction reads as lag.
	 *
	 * Wide enough to cover a bad connection, narrow enough that it is not a
	 * teleport-to-open: 220 cm is half a second of sprinting.
	 */
	float MaxInteractDistanceTolerance = 220.f;

	UPROPERTY(config, EditAnywhere, Category = "Security")
	int32 RpcBudgetPerSecond = 90;

	UPROPERTY(config, EditAnywhere, Category = "Security")
	bool bLogSuspiciousActions = true;

	UPROPERTY(config, EditAnywhere, Category = "Security")
	bool bKickOnRepeatedViolations = false;

	UPROPERTY(config, EditAnywhere, Category = "Security")
	int32 ViolationsBeforeKick = 25;

	// --- Voice ------------------------------------------------------------
	UPROPERTY(config, EditAnywhere, Category = "Voice")
	int32 VoiceSampleRate = ONE_BRAHAM_VOICE_SAMPLE_RATE;

	UPROPERTY(config, EditAnywhere, Category = "Voice")
	int32 VoiceNumChannels = 1;

	/** Capture buffer length in samples. 320 samples @16kHz = 20 ms frames. */
	UPROPERTY(config, EditAnywhere, Category = "Voice")
	int32 VoiceFrameSamples = 320;

	UPROPERTY(config, EditAnywhere, Category = "Voice")
	float VoiceMaxDistanceCm = 3500.f;

	/** How far voice carries through a closed fire shutter (multiplier on the falloff). */
	UPROPERTY(config, EditAnywhere, Category = "Voice")
	float VoiceOccludedFalloffMultiplier = 2.4f;

	// --- Content registries ----------------------------------------------
	UPROPERTY(config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UOneBrahamWeaponDataAsset> WeaponRegistry;

	UPROPERTY(config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UOneBrahamCharacterDataAsset> CharacterRegistry;

	UPROPERTY(config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UOneBrahamVoiceLineDataAsset> VoiceLineRegistry;

	UPROPERTY(config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UInputMappingContext> CommonMappingContext;
};

// ==========================================================================
//  UOneBrahamInputConfig - every Input Action the game binds, in one asset.
//  Blueprint assigns the actual IA_* assets; C++ only ever reads by name, so
//  a designer can rebind without touching code.
// ==========================================================================
UCLASS(BlueprintType, Const)
class ONEBRAHAM_API UOneBrahamInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Movement ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Move;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Look;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Jump;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Sprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Walk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Crouch;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Lean;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Mantle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement") TObjectPtr<UInputAction> Slide;

	// --- Combat -----------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat") TObjectPtr<UInputAction> Fire;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat") TObjectPtr<UInputAction> Aim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat") TObjectPtr<UInputAction> Reload;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat") TObjectPtr<UInputAction> FireModeSwitch;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat") TObjectPtr<UInputAction> Melee;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat") TObjectPtr<UInputAction> Inspect;

	// --- Inventory --------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> WeaponSlotPrimary;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> WeaponSlotSidearm;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> WeaponSlotMelee;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> WeaponCycleNext;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> WeaponCyclePrev;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> ThrowGrenade;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> UseUtility;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> UseMedkit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") TObjectPtr<UInputAction> DropWeapon;

	// --- Interaction ------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction") TObjectPtr<UInputAction> Interact;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction") TObjectPtr<UInputAction> InteractHold;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction") TObjectPtr<UInputAction> ToggleFlashlight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction") TObjectPtr<UInputAction> ToggleLaser;

	// --- Voice ------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice") TObjectPtr<UInputAction> PushToTalk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice") TObjectPtr<UInputAction> VoiceChannelCycle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice") TObjectPtr<UInputAction> VoiceMuteSelf;

	// --- UI ---------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> Scoreboard;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> PauseMenu;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> Ping;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> SpectateNextPlayer;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> SpectatePrevPlayer;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> SpectateToggleFreeCam;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") TObjectPtr<UInputAction> ClipMarker;

	// --- Overlays ---------------------------------------------------------
	// The three the player asks for rather than the HUD drawing unasked. They
	// were generated as assets by the content pipeline for a while with no field
	// here to point at them and no handler anywhere: three keys that did nothing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlays") TObjectPtr<UInputAction> ToggleHUD;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlays") TObjectPtr<UInputAction> Map;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlays") TObjectPtr<UInputAction> VoiceLineMenu;

	/** Throws a clear log line when any action is missing - called by the controller on possess. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ValidateAllActionsSet(TArray<FName>& OutMissing) const;
};
