// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSessionSubsystem.h
// ---------------------------------------------------------------------------
// Create, find, join and destroy: the four things the front end asks of an
// online subsystem, in one place, on whichever subsystem actually started.
//
// TWO SUBSYSTEMS, ONE CODE PATH
//   Steam is configured; NULL is what a machine without a Steam client gets
//   (and what -nosteam asks for). They differ in exactly three ways that matter
//   here, and each is handled where it bites rather than by a branch per call:
//     * NULL only answers LAN queries, and only advertises LAN matches. A host
//       under NULL therefore always creates a LAN session, whatever was asked
//       for (ResolveLAN), and a search under NULL is always a LAN query.
//     * NULL ignores query settings, so the protocol filter is applied to the
//       results here (IsCompatible), not trusted to the backend.
//     * NULL builds its connect string from the game net driver's port, which
//       is 0 until the host is listening. A join that resolves port 0 uses the
//       port the host advertised instead (BuildJoinURL).
//
// THE SESSION NAME
//   NAME_GameSession, the engine's own. AGameSession registers and unregisters
//   players against that name, and the engine's disconnect path destroys it,
//   so a session under any other name is one the engine never tidies up and
//   whose player count never moves.
//
// ONE OPERATION AT A TIME
//   Each call binds its completion delegate with a handle and clears it when
//   it fires. The earlier code added a fresh UObject binding on every call and
//   never removed one: the third refresh of the server list ran its handler
//   three times.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamSessionSubsystem.generated.h"

class FOnlineSessionSearch;

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
	bool bLAN = false;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString Region;

	/** Index into the subsystem's last search: what JoinSession takes. */
	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 SearchResultIndex = INDEX_NONE;
};

/** What a host asks for. Plain data, so the settings it produces are testable. */
struct FOneBrahamHostRequest
{
	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;
	int32 MaxPlayers = 12;
	bool bLAN = true;
	FString ServerName;
	FString MapName = TEXT("L_Persistent_OneBraham");
	FString Region = TEXT("EU-West");
	/** The game port the host will listen on. Advertised for NULL's sake. */
	int32 Port = 7777;
};

/** Everything that goes on a travel URL into a match. */
struct FOneBrahamTravelRequest
{
	/** Short name ("L_Persistent_OneBraham") or a /Game/ path. */
	FString MapName = TEXT("L_Persistent_OneBraham");
	EOneBrahamMatchMode Mode = EOneBrahamMatchMode::Ascent;
	FString PlayerName;
	FString Archetype;
	FString ServerName;
	bool bListen = false;
	bool bLAN = false;
	/** INDEX_NONE leaves the bot fill to the mode; 0 or more is ?Bots=N. */
	int32 Bots = INDEX_NONE;
};

/** The pure pieces: no world, no subsystem, so the tests can hold them to account. */
namespace OneBrahamSessionRules
{
	/** Custom session keys, read back by the browser. */
	ONEBRAHAM_API extern const FName KeyMode;
	ONEBRAHAM_API extern const FName KeyServerName;
	ONEBRAHAM_API extern const FName KeyVersion;
	ONEBRAHAM_API extern const FName KeyProtocol;
	ONEBRAHAM_API extern const FName KeyMap;
	ONEBRAHAM_API extern const FName KeyRegion;
	ONEBRAHAM_API extern const FName KeyPort;

	/** The maps folder every short map name lives in. */
	ONEBRAHAM_API extern const FString MapsFolder;

	/** NULL cannot advertise beyond the LAN: a NULL host is a LAN host. */
	ONEBRAHAM_API bool ResolveLAN(bool bRequestedLAN, FName SubsystemName);

	/** The settings a host creates its session with. */
	ONEBRAHAM_API void BuildSessionSettings(const FOneBrahamHostRequest& Request, FOnlineSessionSettings& OutSettings);

	/** Reads a row back out of a session's settings. */
	ONEBRAHAM_API FOneBrahamServerInfo ServerInfoFromSettings(const FOnlineSessionSettings& Settings,
		int32 NumOpenPublicConnections, int32 PingMs, const FString& OwningUserName);

	/** Same protocol as this build (or no protocol key at all, from a build before the key). */
	ONEBRAHAM_API bool IsCompatible(const FOnlineSessionSettings& Settings);

	/** A value safe inside a URL option: no '?', '#', '=' and no control characters. */
	ONEBRAHAM_API FString SanitiseOptionValue(const FString& Value);

	/** "/Game/OneBraham/Maps/L_Persistent_OneBraham?Mode=Ascent?Name=...?listen". */
	ONEBRAHAM_API FString BuildTravelURL(const FOneBrahamTravelRequest& Request);

	/** The option string alone (no map), for OpenLevel's Options argument. */
	ONEBRAHAM_API FString MakeTravelOptions(const FOneBrahamTravelRequest& Request);

	/** A resolved connect string with the port repaired (missing or 0 becomes
	 *  FallbackPort), and Options appended. */
	ONEBRAHAM_API FString BuildJoinURL(const FString& ConnectString, int32 FallbackPort, const FString& Options);

	/** "ascent", "ASCENT", "firedrill", "fire_drill", "Fire Drill", "FireDrill" ... */
	ONEBRAHAM_API bool ParseMatchMode(const FString& Text, EOneBrahamMatchMode& OutMode);

	/** The enum's name ("Ascent", "FireDrill"): what the GameMode's ?Mode= reads. */
	ONEBRAHAM_API FString ModeKey(EOneBrahamMatchMode Mode);

	/** A match that has been decided takes nobody new: Complete and PostMatch.
	 *  A joiner let in then loads the tower only to be sent home with it. */
	ONEBRAHAM_API bool ShouldCloseToJoins(EOneBrahamMatchPhase Phase);

	/** The settings of a session closed to joins: not advertised, no join in
	 *  progress, no join through presence. Everything else as it was. */
	ONEBRAHAM_API void CloseSettingsToJoins(FOnlineSessionSettings& InOutSettings);
}

DECLARE_MULTICAST_DELEGATE_TwoParams(FOneBrahamOnSessionCreated, bool /*bSuccess*/, bool /*bLAN*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOneBrahamOnSessionsFound, bool /*bSuccess*/, const TArray<FOneBrahamServerInfo>& /*Servers*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOneBrahamOnSessionJoined, bool /*bSuccess*/, const FString& /*TravelURL*/, const FText& /*Reason*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOneBrahamOnSessionDestroyed, bool /*bSuccess*/);

UCLASS()
class ONEBRAHAM_API UOneBrahamSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/** The session every One Braham match runs under. */
	static FName SessionName() { return NAME_GameSession; }

	/** "STEAM", "NULL", or NAME_None when no online subsystem started. */
	FName GetSubsystemName() const;

	/** True under NULL (or with no subsystem): LAN and direct IP only. */
	bool IsLANOnly() const;

	/** True while a create, find, join or destroy is outstanding. */
	bool IsBusy() const { return Operation != EOperation::None; }

	bool IsSearching() const { return Operation == EOperation::Finding; }

	/** A session of ours exists (hosted or joined). */
	bool HasSession() const;

	/**
	 * Creates the session. Completes through OnSessionCreated; returns false
	 * (having broadcast nothing) only when the call could not even start, in
	 * which case the caller hosts without a session. An existing session is
	 * destroyed first, and the create waits for that to finish.
	 */
	bool HostSession(const FOneBrahamHostRequest& Request);

	/** Starts a search. bLAN is ignored under NULL, which is always LAN. */
	bool FindSessions(bool bLAN);

	void CancelFindSessions();

	/** Joins a row from the last search. Completes through OnSessionJoined -
	 *  a refusal too (a row no longer in the list, another join underway), so
	 *  a menu waiting on the answer always gets one. */
	bool JoinSession(int32 SearchResultIndex, const FString& TravelOptions);

	/** Destroys our session if there is one. Completes through OnSessionDestroyed. */
	void DestroySession();

	/** The host's match is decided: stop listing it and stop letting people
	 *  in (NULL's beacon stops answering once the session is started with
	 *  join-in-progress off; Steam hides the lobby). A no-op without a session
	 *  or when already closed. */
	void CloseToJoins();

	const TArray<FOneBrahamServerInfo>& GetLastResults() const { return LastResults; }

	FOneBrahamOnSessionCreated OnSessionCreated;
	FOneBrahamOnSessionsFound OnSessionsFound;
	FOneBrahamOnSessionJoined OnSessionJoined;
	FOneBrahamOnSessionDestroyed OnSessionDestroyed;

	/** How long a LAN search waits for answers. Long enough for a host on
	 *  this machine to reply, short enough to look like it is doing something. */
	static constexpr float LANSearchSeconds = 3.f;

private:
	enum class EOperation : uint8
	{
		None,
		Creating,
		Finding,
		Joining,
		Destroying
	};

	IOnlineSessionPtr GetSessionInterface() const;

	/** The hosting or searching local player's id, or null to use index 0. */
	FUniqueNetIdPtr GetLocalUserId() const;

	bool StartCreate();

	void HandleCreateComplete(FName InSessionName, bool bWasSuccessful);
	void HandleFindComplete(bool bWasSuccessful);
	void HandleJoinComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroyComplete(FName InSessionName, bool bWasSuccessful);

	void ClearHandles();

	EOperation Operation = EOperation::None;

	/** Set while a destroy runs only to clear the way for a create or a join. */
	enum class EAfterDestroy : uint8 { Nothing, Create, Join };
	EAfterDestroy AfterDestroy = EAfterDestroy::Nothing;

	FOneBrahamHostRequest PendingHost;
	bool bPendingHostLAN = true;
	int32 PendingJoinIndex = INDEX_NONE;
	FString PendingJoinOptions;

	/** The search in flight. */
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;

	/** The search LastResults came from. A new search does not replace it
	 *  until it lands: the rows on screen stay joinable while SEARCH AGAIN
	 *  runs, and their SearchResultIndex keeps pointing at the right result. */
	TSharedPtr<FOnlineSessionSearch> ResultSearch;
	TArray<FOneBrahamServerInfo> LastResults;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;
};
