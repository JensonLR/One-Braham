// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameInstance.h
// ---------------------------------------------------------------------------
// Owns everything that survives a level transition:
//   * the local player profile (UOneBrahamSaveSettings)
//   * the cached match settings assets for each mode
//   * the pending travel intent (which mode + which listen/dedicated host)
//   * the journey between the front end and a match, both ways: solo, host,
//     join, and back to L_MainMenu when a player leaves, a match ends or a
//     connection drops (with a line for the menu saying which)
//
// The sessions themselves - create, find, join, destroy - are
// UOneBrahamSessionSubsystem's. This class decides when to ask for one and
// where to travel once it has answered.
//
// Dedicated servers never create a profile or a client session: every entry
// point checks IsDedicatedServer() first.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "Core/OneBrahamTypes.h"
#include "Core/OneBrahamSessionSubsystem.h"
#include "OneBrahamGameInstance.generated.h"

class UNetDriver;
class UOneBrahamSaveSettings;
class UOneBrahamMatchSettingsAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamSessionCreated, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamSessionJoined, bool, bWasSuccessful, const FString&, TravelURL);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamSessionListUpdated, const TArray<FOneBrahamServerInfo>&, Servers);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamProfileLoaded, UOneBrahamSaveSettings*, Profile);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamMatchmakingStatus, const FText&, Status, float, Progress);

UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UOneBrahamGameInstance();

	// --- UGameInstance ----------------------------------------------------
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual void OnStart() override;

	// ======================================================================
	//  Profile
	// ======================================================================

	/** Loads (or creates) the local profile. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Profile")
	bool LoadProfile();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Profile")
	bool SaveProfile();

	UFUNCTION(BlueprintPure, Category = "One Braham|Profile")
	UOneBrahamSaveSettings* GetProfile() const { return Profile; }

	/** Called by the online subsystem when Steam reports the persona name. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Profile")
	void ApplyPlatformDisplayName(const FString& PlatformPlayerName);

	/**
	 * The name this player goes by on LAN and direct IP (the NULL subsystem),
	 * chosen by the player and kept in the profile. Sanitised and capped as the
	 * server will (AOneBrahamGameMode::SanitisePlayerName); false when nothing
	 * usable is left. Under Steam the persona name replaces it at the next launch.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Profile")
	bool SetPlayerDisplayName(const FString& NewName);

	/** The name the HUD, scoreboard and travel URL use for this player. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Profile")
	FString GetPlayerDisplayName() const;

	/** A friendly generated office name ("Priya from Payroll"); the same seed, the same name. */
	static FString MakeOfficeDisplayName(uint32 Seed);

	/** The OS account's full name ("Jenson Raper") where the platform keeps one; empty otherwise. */
	static FString GetOSFullUserName();

	/** The NULL default: the OS full name, else an office name stable for this machine. */
	static FString MakeDefaultDisplayName();

	/** True for a profile name an earlier build filled in by itself (the login
	 *  name, the host name, the NULL nickname), which is not the player's choice. */
	static bool IsAutomaticDisplayName(const FString& Name);

	// ======================================================================
	//  Match settings
	// ======================================================================

	/** Returns the match settings asset for a mode, loading it on demand. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Match")
	UOneBrahamMatchSettingsAsset* GetMatchSettings(EOneBrahamMatchMode Mode);

	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	EOneBrahamMatchMode GetPendingMatchMode() const { return PendingMatchMode; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Match")
	void SetPendingMatchMode(EOneBrahamMatchMode NewMode);

	/** Optional override used by custom lobbies to force a specific settings asset. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Match")
	void SetPendingMatchSettingsOverride(UOneBrahamMatchSettingsAsset* Settings);

	// ======================================================================
	//  Sessions - solo, listen server, browser, matchmaking
	// ======================================================================

	/** A match on this machine alone: no session, no listen socket, bots in
	 *  every other slot. What PLAY on the main menu does. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void StartSoloMatch(EOneBrahamMatchMode Mode);

	/**
	 * Hosts a listen-server match on the local machine: creates the session
	 * (LAN, or online where the subsystem can), then travels with ?listen.
	 * A session that cannot be created still hosts - direct IP works without
	 * one - and the log says so.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void HostListenServer(EOneBrahamMatchMode Mode, int32 MaxPlayers, bool bLAN, const FString& ServerName);

	/** Starts matchmaking: finds an online session for the mode, or hosts one. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void StartMatchmaking(EOneBrahamMatchMode Mode);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void CancelMatchmaking();

	/** Searches for sessions; OnServerListUpdated reports. LAN only under NULL. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void RefreshServerBrowser(bool bLANOnly = false);

	/** Joins a row of the last search (FOneBrahamServerInfo::SearchResultIndex). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void JoinServerByIndex(int32 SearchResultIndex);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void JoinServerByAddress(const FString& Address, int32 Port = 7777);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void DestroyCurrentSession();

	/** Direct connect used by automation tests and the QA scripts. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void TravelToAddress(const FString& Address, int32 Port, const FString& Options);

	/** The session subsystem, or null on a dedicated server. */
	UOneBrahamSessionSubsystem* GetSessions() const;

	// ======================================================================
	//  Travel / flow
	// ======================================================================

	/**
	 * Back to the front end. A client disconnects; a listen host sends its
	 * guests home first ("The host has closed the floor."); a standalone game
	 * just loads the menu; a dedicated server has no menu, so it sends every
	 * client home and restarts its map for the next lobby.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void TravelToMainMenu();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void TravelToMatchMap(const FString& Options = FString());

	/** Shows the branded loading screen on the next map load (the engine movie
	 *  player: a black page, the title and one line of status). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void ShowLoadingScreen(const FText& StatusText, float Progress);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void HideLoadingScreen();

	UFUNCTION(BlueprintPure, Category = "One Braham|Flow")
	bool IsLoadingScreenUp() const { return bLoadingScreenActive; }

	/** A line for the main menu to show once, on arrival: why the player is
	 *  back ("Connection lost. The host left the building."). Empty if none. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	FText ConsumeMenuNotice();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void SetMenuNotice(const FText& Notice);

	/** The front end's map, as a travel URL. */
	static const TCHAR* MainMenuMapPath() { return TEXT("/Game/OneBraham/Maps/L_MainMenu"); }

	/** The engine's ClientReturnToMainMenu path lands here: keeps the reason. */
	virtual void ReturnToMainMenu() override;

	// ======================================================================
	//  Delegates (UI binds to these)
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamSessionCreated OnSessionCreated;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamSessionJoined OnSessionJoined;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamSessionListUpdated OnServerListUpdated;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Profile") FOneBrahamProfileLoaded OnProfileLoaded;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamMatchmakingStatus OnMatchmakingStatus;

	/** Session name used for every ONE BRAHAM game session: the engine's own
	 *  NAME_GameSession, so AGameSession registers players against it. */
	static const FName SessionName;

	// ======================================================================
	//  Pending travel intent (read by the GameMode on Init)
	// ======================================================================
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Flow")
	FString PendingServerName = TEXT("One Braham - Dedicated");

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Flow")
	bool bPendingLAN = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Flow")
	int32 PendingMaxPlayers = ONE_BRAHAM_MAX_PLAYERS;

protected:
	// --- Session subsystem callbacks ---------------------------------------
	void HandleSessionCreated(bool bWasSuccessful, bool bLAN);
	void HandleSessionsFound(bool bWasSuccessful, const TArray<FOneBrahamServerInfo>& Servers);
	void HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL, const FText& Reason);

	// --- Engine failures: recorded for the menu ------------------------------
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	/** Logs which online subsystem actually started (Steam, or the NULL fallback
	 *  a dev machine without the Steam client gets) and why it differs from the
	 *  configured default. Called once from Init. */
	void LogOnlineSubsystemInUse() const;

	/** The travel request for the pending mode, profile and server name. */
	FOneBrahamTravelRequest BuildTravelRequest(bool bListen) const;

	/** Builds the option string the GameMode reads for mode/roster config. */
	FString BuildTravelOptions() const;

	/** Options + "?OBName=<display name>" (AOneBrahamGameMode::DisplayNameOption). */
	FString AppendDisplayNameOption(const FString& Options) const;

	/** Init: -PlayerName=, else the Steam persona, else the profile's chosen
	 *  name, else MakeDefaultDisplayName. Saved to the profile. */
	void ResolveDisplayName();

	/** Empty, or the login name again. */
	static bool IsAutomaticNameCandidate(const FString& Name);

	/** On a client: asks the server once for the profile's name if the player
	 *  state arrived with another one (a login without ?OBName=). */
	void WatchClientPlayerName(UWorld* World);

	/** The name this client asks the server for: its connect URL's ?OBName= /
	 *  ?Name= (unless it is the NULL nickname), else the profile's. */
	FString GetClientRequestedName() const;
	FTimerHandle ClientNameHandle;

	/** The bindings to the session subsystem's delegates, made once in Init. */
	void BindSessionDelegates();

	FDelegateHandle SessionCreatedHandle;
	FDelegateHandle SessionsFoundHandle;
	FDelegateHandle SessionJoinedHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;

	/** Set while HostListenServer waits on the create: the travel follows it. */
	bool bHostTravelPending = false;

	/** A failure seen this session, shown by the menu on arrival. */
	FText PendingMenuNotice;

	UPROPERTY()
	TObjectPtr<UOneBrahamSaveSettings> Profile = nullptr;

	UPROPERTY()
	TMap<EOneBrahamMatchMode, TObjectPtr<UOneBrahamMatchSettingsAsset>> CachedMatchSettings;

	UPROPERTY()
	TObjectPtr<UOneBrahamMatchSettingsAsset> PendingMatchSettingsOverride = nullptr;

	UPROPERTY()
	EOneBrahamMatchMode PendingMatchMode = EOneBrahamMatchMode::Ascent;

	/** UGameInstance has no timer manager of its own; this resolves the world's. */
	FTimerManager* GetOBTimerManager() const;

	bool bMatchmakingActive = false;
	bool bLoadingScreenActive = false;

	/** Latency budget for matchmaking: reject servers above this. */
	static constexpr int32 MaxAcceptablePingMs = 120;

	/** Matchmaking timeout in seconds before we fall back to hosting our own. */
	static constexpr float MatchmakingTimeoutSeconds = 12.f;

	FTimerHandle MatchmakingTimeoutHandle;

	// --- Saved settings that live in a world -------------------------------
	/** Hooks each new world's begin play (FWorldDelegates::OnWorldInitializedActors). */
	void HandleWorldInitializedActors(const struct FActorsInitializedParams& Params);

	/** Re-applies the world-bound saved settings (colour-blind mode, bus volumes...). */
	void HandleWorldBeginPlay(TWeakObjectPtr<UWorld> World);

	/** On a listen host: closes the session to joins once the match is
	 *  decided (OneBrahamSessionRules::ShouldCloseToJoins). Polled once a
	 *  second, because the server's phase change broadcasts nothing (the
	 *  GameState's OnMatchPhaseChanged fires from OnRep, on clients only). */
	void WatchMatchForSessionClose(UWorld* World);
	FTimerHandle SessionWatchHandle;

	FDelegateHandle WorldInitializedActorsHandle;
};
