// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameInstance.h
// ---------------------------------------------------------------------------
// Owns everything that survives a level transition:
//   * the local player profile (UOneBrahamSaveSettings)
//   * the Steam/online session used for matchmaking and the server browser
//   * the cached match settings assets for each mode
//   * the pending travel intent (which mode + which listen/dedicated host)
//
// Dedicated servers never create a profile or a client session: every entry
// point checks IsDedicatedServer() first.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamGameInstance.generated.h"

class UOneBrahamSaveSettings;
class UOneBrahamMatchSettingsAsset;

/** One row in the server browser / matchmaking list. */
USTRUCT(BlueprintType)
struct FOneBrahamServerInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString ServerName;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 MaxPlayers = 12;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 PingMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	bool bPasswordProtected = false;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString Region;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 SearchResultIndex = INDEX_NONE;

	/** A dedicated server rather than a player's listen lobby. */
	UPROPERTY(BlueprintReadOnly, Category = "Server")
	bool bDedicated = false;
};

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
	//  Sessions - listen server + matchmaking
	// ======================================================================

	/**
	 * Hosts a listen-server match on the local machine. FIRE DRILL and private
	 * ASCENT lobbies use this; ranked/automatch games use a dedicated server.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void HostListenServer(EOneBrahamMatchMode Mode, int32 MaxPlayers, bool bLAN, const FString& ServerName);

	/** Starts matchmaking: finds or creates a Steam session for the mode. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void StartMatchmaking(EOneBrahamMatchMode Mode);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void CancelMatchmaking();

	/**
	 * Offline practice: a standalone match of Mode with BotCount bots filling
	 * the roster. No session, no network: the same game mode and the same map.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void StartPracticeMatch(EOneBrahamMatchMode Mode, int32 BotCount);

	/** Fires a one-shot async Steam session search for the server browser. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void RefreshServerBrowser(bool bLANOnly = false);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void JoinServerByIndex(int32 SearchResultIndex);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void JoinServerByAddress(const FString& Address, int32 Port = 7777);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void DestroyCurrentSession();

	/** Latency budget for matchmaking: reject servers above this. */
	static constexpr int32 MaxAcceptablePingMs = 120;

	/** True while a matchmaking search is running (rich presence reads it). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Session")
	bool IsMatchmaking() const { return bMatchmakingActive; }

	/**
	 * Dedicated server: advertises this server on the online service so the
	 * browser and matchmaking can find it. Called by AOneBrahamGameSession.
	 */
	void RegisterDedicatedSession(EOneBrahamMatchMode Mode, int32 MaxPlayers, const FString& ServerName);

	/** Direct connect used by automation tests and the QA scripts. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Session")
	void TravelToAddress(const FString& Address, int32 Port, const FString& Options);

	// ======================================================================
	//  Travel / flow
	// ======================================================================

	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void TravelToMainMenu();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void TravelToMatchMap(const FString& Options = FString());

	/** Shows the branded ONE BRAHAM loading screen (uses the engine movie player). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void ShowLoadingScreen(const FText& StatusText, float Progress);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Flow")
	void HideLoadingScreen();

	UFUNCTION(BlueprintPure, Category = "One Braham|Flow")
	bool IsLoadingScreenUp() const { return bLoadingScreenActive; }

	// ======================================================================
	//  Delegates (UI binds to these)
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamSessionCreated OnSessionCreated;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamSessionJoined OnSessionJoined;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamSessionListUpdated OnServerListUpdated;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Profile") FOneBrahamProfileLoaded OnProfileLoaded;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Session") FOneBrahamMatchmakingStatus OnMatchmakingStatus;

	/** Session name used for every ONE BRAHAM game session. */
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
	// --- Online subsystem callbacks ---------------------------------------
	void HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful);

	/** Builds the option string the GameMode reads for mode/roster config. */
	FString BuildTravelOptions() const;

	/**
	 * True when the platform lets this user play online. When it does not, says
	 * so on the matchmaking status line (the menu shows it) and returns false.
	 */
	bool CheckOnlinePrivilege();

	/** Shared session settings builder so host + matchmaking agree. */
	TSharedPtr<class FOnlineSessionSettings> BuildSessionSettings(EOneBrahamMatchMode Mode, int32 MaxPlayers, bool bLAN, const FString& ServerName) const;

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

	/** The session interface, or null when there are no online services. */
	static IOnlineSessionPtr GetSessionInterface();

	/** Creates the listen-server session from the Pending* values. */
	void CreateHostedSession();

	/**
	 * Starts one search pass. The first pass finds player lobbies; when
	 * bThenDedicated is set a second pass finds dedicated servers, and the
	 * results of both land in BrowserResults.
	 */
	bool StartSessionSearch(bool bLobbies, bool bLANOnly, int32 MaxResults, bool bThenDedicated);

	/** Stops an in-flight search and forgets its results. */
	void CancelSessionSearch();

	/** Unbinds every session completion delegate this instance added. */
	void ClearSessionDelegates();

	/** Live search handle kept alive while the async search runs. */
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;

	/** Every result of the current browse, lobbies then dedicated servers. Indexed by FOneBrahamServerInfo::SearchResultIndex. */
	TArray<FOnlineSessionSearchResult> BrowserResults;

	/** The second (dedicated-server) pass is still to run. */
	bool bSearchDedicatedNext = false;

	/** The current browse is a LAN query. */
	bool bBrowseLAN = false;

	/** A re-host is waiting for the old session to finish being destroyed. */
	bool bHostAfterDestroy = false;

	FDelegateHandle CreateSessionDelegateHandle;
	FDelegateHandle FindSessionsDelegateHandle;
	FDelegateHandle JoinSessionDelegateHandle;
	FDelegateHandle DestroySessionDelegateHandle;

	bool bMatchmakingActive = false;
	bool bLoadingScreenActive = false;

	/** Matchmaking timeout in seconds before we fall back to hosting our own. */
	static constexpr float MatchmakingTimeoutSeconds = 12.f;

	FTimerHandle MatchmakingTimeoutHandle;
};
