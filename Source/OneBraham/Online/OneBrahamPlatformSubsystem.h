// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPlatformSubsystem.h
// ---------------------------------------------------------------------------
// The things every platform holder checks before a game ships on its store,
// written against the engine's platform-neutral interfaces so they hold on
// Steam today and on Xbox (GDK) or another console later without a rewrite.
//
// DROPPED CONNECTIONS
//   When the server goes away the engine already sends the client back to the
//   default map (the main menu). What it does not do is say why. This catches
//   the network and travel failures, words them for a player, and the menu
//   shows the line the next time it opens.
//
// CONTROLLER DISCONNECTED
//   A pad that drops mid-match opens the pause menu, which swaps the gameplay
//   bindings out (the world never pauses: it is PvP), and the HUD says to
//   reconnect it. Only when the player was on the pad: a keyboard player whose
//   idle pad's batteries die is not interrupted.
//
// SUSPEND AND RESUME
//   Losing focus (alt-tab, the Xbox guide, a console suspend) opens the pause
//   menu the same way. Coming back re-checks the privileges below, because the
//   player may have changed them in the system settings in between.
//
// PRIVILEGES
//   Three answers from the platform, each defaulting to allowed until it
//   answers no: may this user play online, talk to strangers, and play with
//   people on other platforms. Steam always says yes. Xbox does not: a child
//   account, or an adult who switched voice chat off at the system level, must
//   get no online play or no voice, whatever the game's own settings say.
//   Online play gates matchmaking, the server browser and hosting; practice
//   and LAN still work. Communication gates both sending and hearing voice.
//
// CROSSPLAY
//   Every advertised session carries its platform and whether its host allows
//   crossplay. A browser entry is shown only when the two sides are on the same
//   platform or both allow it (IsCrossplayCompatible). The player's own switch
//   lives in Settings -> Gameplay; the platform privilege can turn it off.
//
// BLOCKED PLAYERS
//   The platform's block list is fetched at sign-in and on resume. A blocked
//   player's voice is dropped on arrival.
//
// None of this runs on a dedicated server.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "OneBrahamPlatformSubsystem.generated.h"

class APlayerState;
class UNetDriver;
class FUniqueNetId;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamControllerConnectionChanged, bool, bConnected);

UCLASS()
class ONEBRAHAM_API UOneBrahamPlatformSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The subsystem for a world context's game instance, or null (dedicated server, editor preview). */
	static UOneBrahamPlatformSubsystem* Get(const UObject* WorldContext);

	// ======================================================================
	//  Privileges
	// ======================================================================

	UFUNCTION(BlueprintPure, Category = "One Braham|Platform")
	bool CanPlayOnline() const { return bCanPlayOnline; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Platform")
	bool CanCommunicate() const { return bCanCommunicate; }

	/** The platform privilege and the player's own setting, both. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Platform")
	bool IsCrossplayAllowed() const;

	/** Asks the platform again for all three. Answers land asynchronously. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Platform")
	void RefreshPrivileges();

	/** The name sessions advertise as their platform: Windows, XSX, ... */
	static FString GetPlatformKey();

	/**
	 * Pure: may a player on LocalPlatform (crossplay on or off) see a session
	 * hosted on HostPlatform (crossplay on or off)? Same platform always may;
	 * different platforms only when both sides allow it.
	 */
	static bool IsCrossplayCompatible(const FString& LocalPlatform, bool bLocalAllows, const FString& HostPlatform, bool bHostAllows);

	// ======================================================================
	//  Blocked players
	// ======================================================================

	/** True when this player is on the local user's platform block list. */
	bool IsBlocked(const APlayerState* Player) const;

	/** True for a unique id string on the block list (the testable half). */
	bool IsBlockedId(const FString& UniqueId) const { return BlockedIds.Contains(UniqueId); }

	/** Replaces the block list; used by the platform callback and the tests. */
	void SetBlockedIds(const TArray<FString>& Ids);

	// ======================================================================
	//  Notices
	// ======================================================================

	/** Pure: what to tell the player about a network failure. */
	static FText DescribeNetworkFailure(ENetworkFailure::Type FailureType);

	/** Pure: what to tell the player about a travel failure. */
	static FText DescribeTravelFailure(ETravelFailure::Type FailureType);

	/** Returns the waiting notice and clears it. The main menu calls this when it opens. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Platform")
	FText ConsumeNotice();

	/** Leaves a line for the next menu; a newer one replaces an older. */
	void PostNotice(const FText& Notice) { PendingNotice = Notice; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Platform")
	bool IsControllerDisconnected() const { return bControllerDisconnected; }

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Platform")
	FOneBrahamControllerConnectionChanged OnControllerConnectionChanged;

protected:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void HandleInputDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId);
	void HandleDeactivate();
	void HandleReactivate();

	/** Opens the local player's pause menu if a match is running and it is shut. */
	void OpenPauseMenuIfPlaying();

	void QueryBlockedPlayers();
	void HandleBlockedPlayersQueried(const FUniqueNetId& UserId, bool bWasSuccessful, const FString& Error);

	bool bCanPlayOnline = true;
	bool bCanCommunicate = true;
	bool bCanCrossPlay = true;
	bool bControllerDisconnected = false;

	FText PendingNotice;
	TSet<FString> BlockedIds;

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle DeviceConnectionHandle;
	FDelegateHandle DeactivateHandle;
	FDelegateHandle BackgroundHandle;
	FDelegateHandle ReactivateHandle;
	FDelegateHandle ForegroundHandle;
	FDelegateHandle BlockedQueryHandle;
};
