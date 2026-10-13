// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDTypes.h
// ---------------------------------------------------------------------------
// The HUD data model.
//
// The HUD is deliberately split in two:
//   * UOneBrahamHUDComponent owns and gathers the truth (from the pawn's health,
//     weapon, inventory, interaction and voice components, plus the game state).
//   * UOneBrahamHUDWidget renders a snapshot of it.
//
// Only one method crosses that boundary (ApplyHUDData), which means the widget
// can be replaced entirely in Blueprint by a designer without touching gameplay
// code, and the data can be unit tested headlessly.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamHUDTypes.generated.h"

/** One row of the squad panel. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamHUDSquadMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD") FString PlayerName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 PlayerId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") EOneBrahamLifeState LifeState = EOneBrahamLifeState::Alive;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float HealthFraction = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float BleedOutFraction = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bIsSquadLeader = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bIsTransmitting = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bIsSpeakingLine = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 Kills = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 DownedCount = 0;
	/** Distance in cm; INDEX_NONE when unknown (different floor, no radio). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float DistanceCm = -1.f;
	/** True when the teammate is on a different floor: shown as "FL 9" instead. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 FloorIndex = INDEX_NONE;
	/** True for the row belonging to the local player, which is styled differently. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bIsLocalPlayer = false;
};

/** One line of the kill feed. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamHUDKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD") FString KillerName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FString VictimName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FName WeaponId = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bHeadshot = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bMelee = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bTeamKill = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bKillerIsLocalPlayer = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") EOneBrahamAccessTier RequiredTier = EOneBrahamAccessTier::Public;
	/** Wall clock time of the event, for the fade. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float EventTime = 0.f;
};

/** A ping placed by a teammate. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamHUDWorldPing
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FString InstigatorName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 Team = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float SpawnTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float Lifetime = 6.f;
	/** Ping flavour: Enemy, Item, Move, Danger, Attention. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FName PingType = TEXT("Attention");
};

/** A marker on the compass/heading strip. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamHUDCompassMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD") FVector WorldLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FName MarkerType = TEXT("Objective");
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FLinearColor Colour = FLinearColor::White;
	/** Glyph key resolved by the widget ("K", "X", "OBJ", "EXTRACT"). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FString Glyph;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float DistanceCm = 0.f;
};

/** Everything the HUD renders this frame. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamHUDData
{
	GENERATED_BODY()

	// --- player ---------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float Health = 100.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float MaxHealth = 100.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float Armour = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float MaxArmour = 50.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") EOneBrahamLifeState LifeState = EOneBrahamLifeState::Alive;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float BleedOutRemaining = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 RevivesRemaining = 0;

	// --- weapon ---------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FName WeaponId = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText WeaponDisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 AmmoInMagazine = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 MagazineSize = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 AmmoReserve = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") EOneBrahamFireMode FireMode = EOneBrahamFireMode::Single;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bReloading = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float ReloadProgress01 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bMeleeAvailable = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bLaserOn = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bFlashlightOn = false;
	/** Current spread in degrees, drives the crosshair gap. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float SpreadDegrees = 0.f;
	/** 0..1 hit-marker animation timer; 0 when idle. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float HitMarkerAlpha = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bLastHitWasHeadshot = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bLastHitWasKill = false;

	// --- secondaries ----------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 FragCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 FlashCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 SmokeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 BreachCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 MedkitCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") EOneBrahamAccessTier HighestKeycardTier = EOneBrahamAccessTier::Public;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bHasOverrideTool = false;

	// --- interaction ----------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bHasInteractionTarget = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText InteractionPrompt;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText InteractionVerb;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float InteractionHoldProgress01 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bInteractionBlocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText InteractionBlockedReason;

	// --- match ----------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FName MatchPhaseTag = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float MatchTimeRemaining = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 AlivePlayersRemaining = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 SquadsRemaining = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 CurrentFloorIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText FloorDisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 HighestUnlockedFloor = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bLockdownActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float LockdownSecondsRemaining = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bExtractionActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float ExtractionHoldProgress01 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float ExtractionSecondsRemaining = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 HoldingSquadId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 ScoreHome = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 ScoreAway = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") int32 LocalTeamScore = 0;

	// --- voice ----------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") EOneBrahamVoiceChannel VoiceChannel = EOneBrahamVoiceChannel::Proximity;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText VoiceChannelName;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bSelfMuted = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bLocalTransmitting = false;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bMicrophoneAvailable = false;
	/** Subtitle text currently on screen (empty = none). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText SubtitleText;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FString SubtitleSpeaker;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float SubtitleRemaining = 0.f;

	// --- feedback -------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText BannerText;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FLinearColor BannerColour = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float BannerRemaining = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") FText SubBannerText;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float DamageDirectionDegrees = -1.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float DamageIndicatorAlpha = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float LowHealthPulseAlpha = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float FlashAlpha = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowHitMarker = false;

	// --- lists ----------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") TArray<FOneBrahamHUDSquadMember> Squad;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") TArray<FOneBrahamHUDKillFeedEntry> KillFeed;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") TArray<FOneBrahamHUDWorldPing> WorldPings;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") TArray<FOneBrahamHUDCompassMarker> CompassMarkers;

	// --- visibility switches (settings-driven) --------------------------
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowCompass = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowKillFeed = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowSquadPanel = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowAmmoCount = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowCrosshair = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowSubtitles = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowDamageDirection = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowHitMarkers = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowObjectiveMarkers = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowFloorIndicator = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowInteractionPrompt = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bShowVoiceIndicator = true;
	UPROPERTY(BlueprintReadOnly, Category = "HUD") float HUDOpacity = 1.f;
};
