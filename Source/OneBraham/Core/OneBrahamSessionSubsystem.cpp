// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamSessionSubsystem.h"

#include "OneBraham.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Misc/PackageName.h"

// ==========================================================================
//  The pure pieces
// ==========================================================================
namespace OneBrahamSessionRules
{
	const FName KeyMode(TEXT("OB_MODE"));
	const FName KeyServerName(TEXT("OB_SERVERNAME"));
	const FName KeyVersion(TEXT("OB_VERSION"));
	const FName KeyProtocol(TEXT("OB_PROTOCOL"));
	const FName KeyMap(TEXT("OB_MAP"));
	const FName KeyRegion(TEXT("OB_REGION"));
	const FName KeyPort(TEXT("OB_PORT"));

	// The folder of the map every mode plays on: named by a real asset, so the
	// soft-path check holds it to the pipeline like any other load.
	const FString MapsFolder = FPackageName::GetLongPackagePath(TEXT("/Game/OneBraham/Maps/L_Persistent_OneBraham"));

	bool ResolveLAN(bool bRequestedLAN, FName SubsystemName)
	{
		// NULL's sessions are LAN beacons and nothing else: its host only answers
		// a query for a LAN match (FOnlineSessionNull::NeedsToAdvertise), so an
		// "online" session under NULL is one nobody can ever find.
		static const FName NullName(TEXT("NULL"));
		return bRequestedLAN || SubsystemName == NullName || SubsystemName.IsNone();
	}

	void BuildSessionSettings(const FOneBrahamHostRequest& Request, FOnlineSessionSettings& OutSettings)
	{
		OutSettings = FOnlineSessionSettings();

		OutSettings.bIsLANMatch = Request.bLAN;
		// Presence and lobbies are how Steam lists and joins a session; on a LAN
		// they mean nothing and NULL ignores them.
		OutSettings.bUsesPresence = !Request.bLAN;
		OutSettings.bAllowJoinViaPresence = !Request.bLAN;
		OutSettings.bAllowJoinViaPresenceFriendsOnly = false;
		OutSettings.bUseLobbiesIfAvailable = !Request.bLAN;
		OutSettings.bUseLobbiesVoiceChatIfAvailable = false;	// the game carries its own voice
		OutSettings.bAllowInvites = true;
		// ASCENT is one life; nobody walks in half-way up the building.
		OutSettings.bAllowJoinInProgress = false;
		OutSettings.bShouldAdvertise = true;
		OutSettings.bUsesStats = false;
		OutSettings.bAntiCheatProtected = false;
		OutSettings.NumPublicConnections = FMath::Clamp(Request.MaxPlayers, 2, ONE_BRAHAM_MAX_PLAYERS);
		OutSettings.NumPrivateConnections = 0;

		const EOnlineDataAdvertisementType::Type Advertise = EOnlineDataAdvertisementType::ViaOnlineServiceAndPing;
		OutSettings.Set(KeyMode, ModeKey(Request.Mode), Advertise);
		OutSettings.Set(KeyServerName, Request.ServerName, Advertise);
		OutSettings.Set(KeyVersion, FString(ONE_BRAHAM_DISPLAY_NAME), Advertise);
		OutSettings.Set(KeyProtocol, ONE_BRAHAM_NET_PROTOCOL_VERSION, Advertise);
		OutSettings.Set(KeyMap, Request.MapName, Advertise);
		OutSettings.Set(KeyRegion, Request.Region, Advertise);
		OutSettings.Set(KeyPort, Request.Port, Advertise);
	}

	FOneBrahamServerInfo ServerInfoFromSettings(const FOnlineSessionSettings& Settings,
		int32 NumOpenPublicConnections, int32 PingMs, const FString& OwningUserName)
	{
		FOneBrahamServerInfo Info;
		Info.MaxPlayers = Settings.NumPublicConnections;
		Info.CurrentPlayers = FMath::Clamp(Settings.NumPublicConnections - NumOpenPublicConnections, 0, Settings.NumPublicConnections);
		Info.PingMs = FMath::Max(0, PingMs);
		Info.bLAN = Settings.bIsLANMatch;
		Info.bPasswordProtected = false;

		FString ModeString;
		if (Settings.Get(KeyMode, ModeString))
		{
			ParseMatchMode(ModeString, Info.Mode);
		}

		Settings.Get(KeyServerName, Info.ServerName);
		Settings.Get(KeyMap, Info.MapName);
		Settings.Get(KeyRegion, Info.Region);

		if (Info.ServerName.IsEmpty())
		{
			Info.ServerName = OwningUserName.IsEmpty()
				? FString(TEXT("Unnamed floor"))
				: FString::Printf(TEXT("%s's floor"), *OwningUserName);
		}

		return Info;
	}

	bool IsCompatible(const FOnlineSessionSettings& Settings)
	{
		int32 Protocol = INDEX_NONE;
		if (!Settings.Get(KeyProtocol, Protocol))
		{
			// No key: a session from before the key, or somebody else's game on the
			// same LAN beacon port. Only ours carries OB_MODE as well.
			FString Mode;
			return Settings.Get(KeyMode, Mode);
		}

		return Protocol == ONE_BRAHAM_NET_PROTOCOL_VERSION;
	}

	FString SanitiseOptionValue(const FString& Value)
	{
		FString Clean;
		Clean.Reserve(Value.Len());

		for (const TCHAR Char : Value)
		{
			// '?' starts the next option, '#' a portal, '=' splits key from value:
			// any of them in a player's name would rewrite the URL.
			if (Char == TEXT('?') || Char == TEXT('#') || Char == TEXT('=') || FChar::IsLinebreak(Char) || Char < 32)
			{
				continue;
			}
			Clean.AppendChar(Char);
		}

		Clean.TrimStartAndEndInline();
		return Clean.Left(48);
	}

	FString MakeTravelOptions(const FOneBrahamTravelRequest& Request)
	{
		TArray<FString> Options;
		Options.Add(FString::Printf(TEXT("Mode=%s"), *ModeKey(Request.Mode)));

		const FString Name = SanitiseOptionValue(Request.PlayerName);
		if (!Name.IsEmpty())
		{
			Options.Add(FString::Printf(TEXT("Name=%s"), *Name));
		}

		const FString Archetype = SanitiseOptionValue(Request.Archetype);
		if (!Archetype.IsEmpty())
		{
			Options.Add(FString::Printf(TEXT("Archetype=%s"), *Archetype));
		}

		const FString Server = SanitiseOptionValue(Request.ServerName);
		if (!Server.IsEmpty())
		{
			Options.Add(FString::Printf(TEXT("ServerName=%s"), *Server));
		}

		if (Request.bLAN)
		{
			Options.Add(TEXT("LAN=1"));
		}

		if (Request.Bots >= 0)
		{
			Options.Add(FString::Printf(TEXT("Bots=%d"), Request.Bots));
		}

		if (Request.bListen)
		{
			Options.Add(TEXT("listen"));
		}

		return FString::Join(Options, TEXT("?"));
	}

	FString BuildTravelURL(const FOneBrahamTravelRequest& Request)
	{
		FString Map = Request.MapName.IsEmpty() ? FString(TEXT("L_Persistent_OneBraham")) : Request.MapName;
		if (!Map.StartsWith(TEXT("/")))
		{
			Map = FString::Printf(TEXT("%s/%s"), *MapsFolder, *Map);
		}

		return FString::Printf(TEXT("%s?%s"), *Map, *OneBrahamSessionRules::MakeTravelOptions(Request));
	}

	FString BuildJoinURL(const FString& ConnectString, int32 FallbackPort, const FString& Options)
	{
		FString Address = ConnectString.TrimStartAndEnd();

		// The port is whatever follows the last ':' when it is all digits; an
		// IPv6 literal in brackets keeps its own colons.
		FString Host = Address;
		int32 Port = 0;
		int32 Colon = INDEX_NONE;
		if (Address.FindLastChar(TEXT(':'), Colon))
		{
			const FString PortText = Address.Mid(Colon + 1);
			// A colon inside "[...]", or one of an unbracketed IPv6 address's
			// many, is not a port separator.
			const bool bInsideAddress = (Address.StartsWith(TEXT("[")) && !Address.Left(Colon).EndsWith(TEXT("]")))
				|| (!Address.StartsWith(TEXT("[")) && Address.Left(Colon).Contains(TEXT(":")));
			if (!bInsideAddress && !PortText.IsEmpty() && PortText.IsNumeric())
			{
				Host = Address.Left(Colon);
				Port = FCString::Atoi(*PortText);
			}
		}

		if (Port <= 0)
		{
			Port = FallbackPort > 0 ? FallbackPort : 7777;
		}

		FString URL = FString::Printf(TEXT("%s:%d"), *Host, Port);
		if (!Options.IsEmpty())
		{
			URL += Options.StartsWith(TEXT("?")) ? Options : TEXT("?") + Options;
		}
		return URL;
	}

	bool ParseMatchMode(const FString& Text, EOneBrahamMatchMode& OutMode)
	{
		FString Key = Text.TrimStartAndEnd();
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));

		if (Key.Equals(TEXT("Ascent"), ESearchCase::IgnoreCase))
		{
			OutMode = EOneBrahamMatchMode::Ascent;
			return true;
		}
		if (Key.Equals(TEXT("FireDrill"), ESearchCase::IgnoreCase))
		{
			OutMode = EOneBrahamMatchMode::FireDrill;
			return true;
		}
		if (Key.Equals(TEXT("NightShift"), ESearchCase::IgnoreCase))
		{
			OutMode = EOneBrahamMatchMode::NightShift;
			return true;
		}
		return false;
	}

	FString ModeKey(EOneBrahamMatchMode Mode)
	{
		return StaticEnum<EOneBrahamMatchMode>()->GetNameStringByValue(static_cast<int64>(Mode));
	}

	bool ShouldCloseToJoins(EOneBrahamMatchPhase Phase)
	{
		return Phase == EOneBrahamMatchPhase::Complete || Phase == EOneBrahamMatchPhase::PostMatch;
	}

	void CloseSettingsToJoins(FOnlineSessionSettings& InOutSettings)
	{
		InOutSettings.bShouldAdvertise = false;
		InOutSettings.bAllowJoinInProgress = false;
		InOutSettings.bAllowJoinViaPresence = false;
		InOutSettings.bAllowJoinViaPresenceFriendsOnly = false;
	}
}

// ==========================================================================
//  Subsystem
// ==========================================================================
bool UOneBrahamSessionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// A dedicated server advertises through the GameSession and its command
	// line; it never browses, so it has no front end to serve.
	const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
	return GameInstance == nullptr || !GameInstance->IsDedicatedServerInstance();
}

void UOneBrahamSessionSubsystem::Deinitialize()
{
	ClearHandles();
	ActiveSearch.Reset();
	ResultSearch.Reset();
	Super::Deinitialize();
}

IOnlineSessionPtr UOneBrahamSessionSubsystem::GetSessionInterface() const
{
	// Online::GetSubsystem resolves the world's own subsystem instance (PIE
	// gets one per client); outside a world, the default.
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSubsystem* OnlineSubsystem = World != nullptr ? Online::GetSubsystem(World) : IOnlineSubsystem::Get();
	return OnlineSubsystem != nullptr ? OnlineSubsystem->GetSessionInterface() : nullptr;
}

FName UOneBrahamSessionSubsystem::GetSubsystemName() const
{
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const IOnlineSubsystem* OnlineSubsystem = World != nullptr ? Online::GetSubsystem(World) : IOnlineSubsystem::Get();
	return OnlineSubsystem != nullptr ? OnlineSubsystem->GetSubsystemName() : NAME_None;
}

bool UOneBrahamSessionSubsystem::IsLANOnly() const
{
	return OneBrahamSessionRules::ResolveLAN(false, GetSubsystemName());
}

bool UOneBrahamSessionSubsystem::HasSession() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(SessionName()) != nullptr;
}

FUniqueNetIdPtr UOneBrahamSessionSubsystem::GetLocalUserId() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* LocalPlayer = GameInstance != nullptr ? GameInstance->GetFirstGamePlayer() : nullptr;
	if (LocalPlayer == nullptr)
	{
		return nullptr;
	}

	const FUniqueNetIdRepl Id = LocalPlayer->GetPreferredUniqueNetId();
	return Id.IsValid() ? Id.GetUniqueNetId() : nullptr;
}

void UOneBrahamSessionSubsystem::ClearHandles()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
}

// --- Create ---------------------------------------------------------------
bool UOneBrahamSessionSubsystem::HostSession(const FOneBrahamHostRequest& Request)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OB_WARN("Session: no online session interface - hosting without a session (direct IP only).");
		return false;
	}

	if (Operation == EOperation::Finding)
	{
		CancelFindSessions();
	}
	else if (Operation == EOperation::Destroying)
	{
		// The last match's session is still going (Steam's destroy is a round
		// trip, and a player back in the menu can press HOST before it lands):
		// the create follows it rather than being refused.
		PendingHost = Request;
		PendingHost.bLAN = OneBrahamSessionRules::ResolveLAN(Request.bLAN, GetSubsystemName());
		AfterDestroy = EAfterDestroy::Create;
		return true;
	}
	else if (IsBusy())
	{
		OB_WARN("Session: host asked for while another session operation is running - ignored.");
		return false;
	}

	PendingHost = Request;
	PendingHost.bLAN = OneBrahamSessionRules::ResolveLAN(Request.bLAN, GetSubsystemName());

	if (Sessions->GetNamedSession(SessionName()) != nullptr)
	{
		// A session from the last match. Creating over it fails with "already
		// exists", and on Steam the destroy is asynchronous, so the create waits.
		AfterDestroy = EAfterDestroy::Create;
		DestroySession();
		return true;
	}

	return StartCreate();
}

bool UOneBrahamSessionSubsystem::StartCreate()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return false;
	}

	FOnlineSessionSettings Settings;
	OneBrahamSessionRules::BuildSessionSettings(PendingHost, Settings);

	Operation = EOperation::Creating;
	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UOneBrahamSessionSubsystem::HandleCreateComplete));

	OB_LOG("Session: creating '%s' on %s (%s, %d slots, %s).", *PendingHost.ServerName,
		*GetSubsystemName().ToString(), PendingHost.bLAN ? TEXT("LAN") : TEXT("online"),
		Settings.NumPublicConnections, *OneBrahamSessionRules::ModeKey(PendingHost.Mode));

	const FUniqueNetIdPtr UserId = GetLocalUserId();
	const bool bStarted = UserId.IsValid()
		? Sessions->CreateSession(*UserId, SessionName(), Settings)
		: Sessions->CreateSession(0, SessionName(), Settings);

	if (!bStarted)
	{
		// The interface refused synchronously; some implementations have already
		// fired the delegate, some have not. Either way the operation is over.
		if (Operation == EOperation::Creating)
		{
			HandleCreateComplete(SessionName(), false);
		}
	}

	return true;
}

void UOneBrahamSessionSubsystem::HandleCreateComplete(FName InSessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}

	Operation = EOperation::None;

	OB_LOG("Session: create '%s' %s.", *InSessionName.ToString(), bWasSuccessful ? TEXT("succeeded") : TEXT("FAILED"));
	OnSessionCreated.Broadcast(bWasSuccessful, PendingHost.bLAN);
}

// --- Find -----------------------------------------------------------------
bool UOneBrahamSessionSubsystem::FindSessions(bool bLAN)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		LastResults.Reset();
		OnSessionsFound.Broadcast(false, LastResults);
		return false;
	}

	if (IsBusy())
	{
		OB_WARN("Session: search asked for while another session operation is running - ignored.");
		return false;
	}

	const bool bLANQuery = OneBrahamSessionRules::ResolveLAN(bLAN, GetSubsystemName());

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = 100;
	ActiveSearch->bIsLanQuery = bLANQuery;
	ActiveSearch->TimeoutInSeconds = LANSearchSeconds;
	if (!bLANQuery)
	{
		ActiveSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}
	// A filter Steam applies server-side; NULL ignores it, which is why the
	// results are filtered again in HandleFindComplete.
	ActiveSearch->QuerySettings.Set(OneBrahamSessionRules::KeyProtocol, ONE_BRAHAM_NET_PROTOCOL_VERSION, EOnlineComparisonOp::Equals);

	Operation = EOperation::Finding;
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UOneBrahamSessionSubsystem::HandleFindComplete));

	OB_LOG("Session: searching %s on %s.", bLANQuery ? TEXT("the LAN") : TEXT("online"), *GetSubsystemName().ToString());

	const FUniqueNetIdPtr UserId = GetLocalUserId();
	const bool bStarted = UserId.IsValid()
		? Sessions->FindSessions(*UserId, ActiveSearch.ToSharedRef())
		: Sessions->FindSessions(0, ActiveSearch.ToSharedRef());

	if (!bStarted && Operation == EOperation::Finding)
	{
		HandleFindComplete(false);
	}

	return true;
}

void UOneBrahamSessionSubsystem::CancelFindSessions()
{
	if (Operation != EOperation::Finding)
	{
		return;
	}

	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->CancelFindSessions();
	}

	Operation = EOperation::None;
}

void UOneBrahamSessionSubsystem::HandleFindComplete(bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}

	Operation = EOperation::None;
	LastResults.Reset();

	// The new results replace the old ones only now, together: the list on
	// screen and the search its rows index into never disagree.
	ResultSearch = ActiveSearch;

	if (bWasSuccessful && ResultSearch.IsValid())
	{
		for (int32 Index = 0; Index < ResultSearch->SearchResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = ResultSearch->SearchResults[Index];
			if (!Result.IsValid() || !OneBrahamSessionRules::IsCompatible(Result.Session.SessionSettings))
			{
				continue;
			}

			FOneBrahamServerInfo Info = OneBrahamSessionRules::ServerInfoFromSettings(Result.Session.SessionSettings,
				Result.Session.NumOpenPublicConnections, Result.PingInMs, Result.Session.OwningUserName);
			Info.SearchResultIndex = Index;
			LastResults.Add(Info);
		}

		// Nearest first: in a twelve-player shooter the fastest floor wins.
		LastResults.StableSort([](const FOneBrahamServerInfo& A, const FOneBrahamServerInfo& B)
		{
			return A.PingMs < B.PingMs;
		});
	}

	OB_LOG("Session: search %s, %d match(es) found.", bWasSuccessful ? TEXT("finished") : TEXT("FAILED"), LastResults.Num());
	OnSessionsFound.Broadcast(bWasSuccessful, LastResults);
}

// --- Join -----------------------------------------------------------------
bool UOneBrahamSessionSubsystem::JoinSession(int32 SearchResultIndex, const FString& TravelOptions)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !ResultSearch.IsValid() || !ResultSearch->SearchResults.IsValidIndex(SearchResultIndex))
	{
		OB_WARN("Session: join %d is not in the current search results.", SearchResultIndex);
		OnSessionJoined.Broadcast(false, FString(), NSLOCTEXT("OneBraham", "Join_Gone", "That match has closed."));
		return false;
	}

	if (Operation == EOperation::Finding)
	{
		// SEARCH AGAIN was running: the row chosen is from the results on
		// screen (ResultSearch), which the search in flight has not touched.
		CancelFindSessions();
	}
	else if (Operation == EOperation::Destroying)
	{
		PendingJoinIndex = SearchResultIndex;
		PendingJoinOptions = TravelOptions;
		AfterDestroy = EAfterDestroy::Join;
		return true;
	}
	else if (IsBusy())
	{
		OB_WARN("Session: join asked for while another session operation is running - ignored.");
		OnSessionJoined.Broadcast(false, FString(), NSLOCTEXT("OneBraham", "Join_Busy", "Still busy with the last request. Try again in a moment."));
		return false;
	}

	PendingJoinIndex = SearchResultIndex;
	PendingJoinOptions = TravelOptions;

	if (Sessions->GetNamedSession(SessionName()) != nullptr)
	{
		AfterDestroy = EAfterDestroy::Join;
		DestroySession();
		return true;
	}

	Operation = EOperation::Joining;
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UOneBrahamSessionSubsystem::HandleJoinComplete));

	const FOnlineSessionSearchResult& Result = ResultSearch->SearchResults[SearchResultIndex];
	OB_LOG("Session: joining result %d (%s).", SearchResultIndex, *Result.Session.OwningUserName);

	const FUniqueNetIdPtr UserId = GetLocalUserId();
	const bool bStarted = UserId.IsValid()
		? Sessions->JoinSession(*UserId, SessionName(), Result)
		: Sessions->JoinSession(0, SessionName(), Result);

	if (!bStarted && Operation == EOperation::Joining)
	{
		HandleJoinComplete(SessionName(), EOnJoinSessionCompleteResult::UnknownError);
	}

	return true;
}

void UOneBrahamSessionSubsystem::HandleJoinComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}

	Operation = EOperation::None;

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		FText Reason = NSLOCTEXT("OneBraham", "Join_Failed", "Could not join. The host did not answer.");
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			Reason = NSLOCTEXT("OneBraham", "Join_Full", "That floor is full.");
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			Reason = NSLOCTEXT("OneBraham", "Join_Gone", "That match has closed.");
			break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			Reason = NSLOCTEXT("OneBraham", "Join_NoAddress", "The host's address could not be found.");
			break;
		case EOnJoinSessionCompleteResult::AlreadyInSession:
			Reason = NSLOCTEXT("OneBraham", "Join_Already", "You are already in that match.");
			break;
		default:
			break;
		}

		OB_WARN("Session: join failed (%s).", LexToString(Result));
		OnSessionJoined.Broadcast(false, FString(), Reason);
		return;
	}

	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(InSessionName, ConnectString))
	{
		OB_ERROR("Session: join succeeded but no connect string resolved.");
		OnSessionJoined.Broadcast(false, FString(), NSLOCTEXT("OneBraham", "Join_NoAddress", "The host's address could not be found."));
		return;
	}

	// NULL builds the address from the host's net driver port at the moment
	// it answered; OB_PORT is what the host said it would listen on.
	int32 AdvertisedPort = 0;
	if (ResultSearch.IsValid() && ResultSearch->SearchResults.IsValidIndex(PendingJoinIndex))
	{
		ResultSearch->SearchResults[PendingJoinIndex].Session.SessionSettings.Get(OneBrahamSessionRules::KeyPort, AdvertisedPort);
	}

	const FString URL = OneBrahamSessionRules::BuildJoinURL(ConnectString, AdvertisedPort, PendingJoinOptions);
	OB_LOG("Session: joined; travelling to %s", *URL);
	OnSessionJoined.Broadcast(true, URL, FText::GetEmpty());
}

// --- Close ----------------------------------------------------------------
void UOneBrahamSessionSubsystem::CloseToJoins()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName()) : nullptr;
	if (Session == nullptr)
	{
		return;
	}

	const bool bAlreadyClosed = !Session->SessionSettings.bShouldAdvertise
		&& Session->SessionState == EOnlineSessionState::InProgress;
	if (bAlreadyClosed)
	{
		return;
	}

	FOnlineSessionSettings Settings = Session->SessionSettings;
	OneBrahamSessionRules::CloseSettingsToJoins(Settings);
	Sessions->UpdateSession(SessionName(), Settings, true);

	// The game mode is an AGameModeBase, so AGameSession never starts the
	// session: it sits "pending", which NULL treats as open to anyone. Started
	// with join-in-progress off, NULL's beacon stops answering.
	Session = Sessions->GetNamedSession(SessionName());
	if (Session != nullptr && (Session->SessionState == EOnlineSessionState::Pending || Session->SessionState == EOnlineSessionState::Ended))
	{
		Sessions->StartSession(SessionName());
	}

	OB_LOG("Session: the match is decided - closed to joins and unlisted.");
}

// --- Destroy --------------------------------------------------------------
void UOneBrahamSessionSubsystem::DestroySession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(SessionName()) == nullptr)
	{
		AfterDestroy = EAfterDestroy::Nothing;
		OnSessionDestroyed.Broadcast(true);
		return;
	}

	if (Operation == EOperation::Finding)
	{
		CancelFindSessions();
	}

	Operation = EOperation::Destroying;
	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UOneBrahamSessionSubsystem::HandleDestroyComplete));

	if (!Sessions->DestroySession(SessionName()) && Operation == EOperation::Destroying)
	{
		HandleDestroyComplete(SessionName(), false);
	}
}

void UOneBrahamSessionSubsystem::HandleDestroyComplete(FName InSessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}

	Operation = EOperation::None;
	OB_LOG("Session: destroy '%s' %s.", *InSessionName.ToString(), bWasSuccessful ? TEXT("succeeded") : TEXT("failed"));

	const EAfterDestroy Next = AfterDestroy;
	AfterDestroy = EAfterDestroy::Nothing;

	OnSessionDestroyed.Broadcast(bWasSuccessful);

	if (Next == EAfterDestroy::Create)
	{
		if (!StartCreate())
		{
			OnSessionCreated.Broadcast(false, PendingHost.bLAN);
		}
	}
	else if (Next == EAfterDestroy::Join)
	{
		// A refusal broadcasts its own answer.
		JoinSession(PendingJoinIndex, PendingJoinOptions);
	}
}
