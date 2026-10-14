// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameInstance.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Online/OneBrahamPlatformSubsystem.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Online.h"
#include "MoviePlayer.h"
#include "UI/OneBrahamLoadingScreen.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"

const FName UOneBrahamGameInstance::SessionName(TEXT("OneBrahamMatch"));

UOneBrahamGameInstance::UOneBrahamGameInstance()
{
}

void UOneBrahamGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogOneBraham, Log, TEXT("[OB] GameInstance::Init on %s"),
		IsDedicatedServerInstance() ? TEXT("dedicated server") : TEXT("client"));

	if (!IsDedicatedServerInstance())
	{
		LoadProfile();

		// Steam persona name lands asynchronously; seed from the command line so
		// a launch with -PlayerName=Alex still shows the right name immediately.
		FString CommandLineName;
		if (FParse::Value(FCommandLine::Get(), TEXT("PlayerName="), CommandLineName) && !CommandLineName.IsEmpty())
		{
			ApplyPlatformDisplayName(CommandLineName);
		}
		else
		{
			const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
			const IOnlineIdentityPtr Identity = OnlineSubsystem ? OnlineSubsystem->GetIdentityInterface() : nullptr;
			if (Identity.IsValid())
			{
				ApplyPlatformDisplayName(Identity->GetPlayerNickname(0));
			}
		}
	}

	// Match settings are resolved lazily by GetMatchSettings(), but pre-loading
	// the default mode avoids a hitch on the first mode-select click.
	GetMatchSettings(UOneBrahamGameSettings::Get().DefaultMode);
}

void UOneBrahamGameInstance::OnStart()
{
	Super::OnStart();
	HideLoadingScreen();
}

void UOneBrahamGameInstance::Shutdown()
{
	if (!IsDedicatedServerInstance())
	{
		SaveProfile();
	}

	// Nothing may call back into an instance that is going away: stop the
	// search, unbind every completion delegate, then leave the session without
	// listening for the answer. A dedicated server's advert goes too, so the
	// browser stops listing a server that has shut down.
	CancelSessionSearch();
	ClearSessionDelegates();
	bHostAfterDestroy = false;
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (Sessions->GetNamedSession(SessionName) != nullptr)
		{
			Sessions->DestroySession(SessionName);
		}
	}

	Super::Shutdown();
}

// ==========================================================================
//  Internal helper: the game instance is not an actor, so timers come from
//  whichever world is current. Returns null during very early startup.
// ==========================================================================
FTimerManager* UOneBrahamGameInstance::GetOBTimerManager() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? &World->GetTimerManager() : nullptr;
}

// ==========================================================================
//  Profile
// ==========================================================================
bool UOneBrahamGameInstance::LoadProfile()
{
	const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineIdentityPtr Identity = OnlineSubsystem ? OnlineSubsystem->GetIdentityInterface() : nullptr;
	const FString UniqueId = Identity.IsValid() && Identity->GetUniquePlayerId(0).IsValid()
		? Identity->GetUniquePlayerId(0)->ToString()
		: TEXT("Local");

	const FString SlotName = FString::Printf(TEXT("%s%s"), UOneBrahamSaveSettings::SlotNamePrefix(), *UniqueId);

	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		Profile = Cast<UOneBrahamSaveSettings>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
		if (Profile == nullptr)
		{
			OB_WARN("Profile slot '%s' exists but failed to load (corrupt or version mismatch). Creating a fresh profile.", *SlotName);
		}
	}

	if (Profile == nullptr)
	{
		Profile = Cast<UOneBrahamSaveSettings>(UGameplayStatics::CreateSaveGameObject(UOneBrahamSaveSettings::StaticClass()));
		if (Profile != nullptr)
		{
			// First run: give the player a complete, playable starting loadout.
			FOneBrahamSavedLoadout Default;
			Default.PrimaryWeaponId = TEXT("W_Carbine556");
			Default.SidearmWeaponId = TEXT("W_Pistol");
			Default.UtilityIds = { TEXT("Utility_Frag"), TEXT("Utility_Flashbang"), TEXT("Utility_Smoke"), TEXT("Utility_BreachCharge") };
			Default.ArchetypeId = TEXT("BusinessAnalyst");
			Profile->Loadouts.Add(Default);
			Profile->SelectedLoadoutIndex = 0;
			Profile->bHasSeenFirstRunSetup = false;

			// Every player starts with the standard-issue cosmetics so the locker
			// is never empty on a fresh install.
			//
			// These seven ids are the wardrobe in build_characters.py, which is
			// the same table the registry asset is generated from and the same
			// one Docs/13 documents. They were not: this list used a naming
			// scheme (Head_*, Torso_*, Legs_*, Feet_*, Outerwear_*) that no
			// wardrobe has ever contained, so a fresh profile unlocked seven
			// cosmetics that do not exist and a player's first outfit was bare.
			// The lanyard and the badge are in the set deliberately: the badge
			// carries the access tier and the lobby reads it at a glance.
			const TArray<FName> StarterCosmetics = {
				TEXT("Jacket_Chino"),
				TEXT("Fleece_Grey"),
				TEXT("Trousers_Chino"),
				TEXT("Shoes_Loafer"),
				TEXT("Bag_None"),
				TEXT("Lanyard_Visitor"),
				TEXT("Badge_Public"),
				TEXT("Hat_None"),
				TEXT("Gloves_None"),
				TEXT("Watch_None"),
				TEXT("BagTag_Staff")
			};
			for (const FName& CosmeticId : StarterCosmetics)
			{
				FOneBrahamCosmeticProgress Progress;
				Progress.CosmeticId = CosmeticId;
				Progress.bUnlocked = true;
				Progress.UnlockedUtc = FDateTime::UtcNow();
				Profile->CosmeticProgress.Add(Progress);
			}
		}
	}

	if (Profile != nullptr)
	{
		Profile->MigrateIfNeeded();
		OnProfileLoaded.Broadcast(Profile);
		OB_LOG("Profile loaded for '%s' (%d matches played, %d extractions).",
			*Profile->PlayerName, Profile->TotalMatchesPlayed, Profile->TotalExtractions);
		return true;
	}

	OB_ERROR("Failed to create a One Braham profile - settings and progression will not persist this session.");
	return false;
}

bool UOneBrahamGameInstance::SaveProfile()
{
	if (Profile == nullptr || IsDedicatedServerInstance())
	{
		return false;
	}

	const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineIdentityPtr Identity = OnlineSubsystem ? OnlineSubsystem->GetIdentityInterface() : nullptr;
	const FString UniqueId = Identity.IsValid() && Identity->GetUniquePlayerId(0).IsValid()
		? Identity->GetUniquePlayerId(0)->ToString()
		: TEXT("Local");

	const FString SlotName = FString::Printf(TEXT("%s%s"), UOneBrahamSaveSettings::SlotNamePrefix(), *UniqueId);
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Profile, SlotName, 0);
	if (!bSaved)
	{
		OB_ERROR("Failed to save profile to slot '%s' (disk full or read-only Saved/ folder).", *SlotName);
	}
	return bSaved;
}

void UOneBrahamGameInstance::ApplyPlatformDisplayName(const FString& PlatformPlayerName)
{
	if (Profile == nullptr)
	{
		return;
	}

	if (!PlatformPlayerName.IsEmpty())
	{
		Profile->PlayerName = PlatformPlayerName;
		SaveProfile();
	}
}

// ==========================================================================
//  Match settings
// ==========================================================================
UOneBrahamMatchSettingsAsset* UOneBrahamGameInstance::GetMatchSettings(EOneBrahamMatchMode Mode)
{
	if (PendingMatchSettingsOverride != nullptr && PendingMatchSettingsOverride->Mode == Mode)
	{
		return PendingMatchSettingsOverride;
	}

	if (TObjectPtr<UOneBrahamMatchSettingsAsset>* Cached = CachedMatchSettings.Find(Mode))
	{
		if (*Cached != nullptr)
		{
			return *Cached;
		}
	}

	// Project Settings -> One Braham -> DefaultMatchSettings is an override for
	// whichever mode it names: somebody tuning one mode does not have to keep the
	// asset at the conventional path, and the field is not decoration.
	if (UOneBrahamMatchSettingsAsset* Configured = UOneBrahamGameSettings::Get().DefaultMatchSettings.LoadSynchronous())
	{
		if (Configured->Mode == Mode)
		{
			Configured->EnsureDefaults();
			CachedMatchSettings.Add(Mode, Configured);

			return Configured;
		}
	}

	// Path convention: /Game/OneBraham/Data/DA_MatchSettings_<Mode>
	const TCHAR* ModeName = TEXT("Ascent");
	switch (Mode)
	{
	case EOneBrahamMatchMode::FireDrill: ModeName = TEXT("FireDrill"); break;
	case EOneBrahamMatchMode::NightShift: ModeName = TEXT("NightShift"); break;
	default: break;
	}

	const FString AssetPath = FString::Printf(TEXT("/Game/OneBraham/Data/DA_MatchSettings_%s.DA_MatchSettings_%s"), ModeName, ModeName);
	UOneBrahamMatchSettingsAsset* Settings = LoadObject<UOneBrahamMatchSettingsAsset>(nullptr, *AssetPath);

	if (Settings == nullptr)
	{
		// Fall back to a transient, sensible default rather than failing: the
		// game must remain playable from a code-only checkout with no content.
		Settings = NewObject<UOneBrahamMatchSettingsAsset>(this, UOneBrahamMatchSettingsAsset::StaticClass(),
			*FString::Printf(TEXT("DA_MatchSettings_Runtime_%s"), ModeName));
		Settings->Mode = Mode;
		Settings->ModeDisplayName = FText::FromString(ModeName);
		Settings->EnsureDefaults();

		OB_WARN("Match settings asset '%s' missing - generated a runtime default. "
			"Run Tools/Python/create_data_assets.py to author the real asset.", *AssetPath);
	}

	// An authored asset fills what it leaves empty the same way the game mode
	// does, so the menu and the travel agree with the match about the map.
	Settings->EnsureDefaults();
	CachedMatchSettings.Add(Mode, Settings);
	return Settings;
}

void UOneBrahamGameInstance::SetPendingMatchMode(EOneBrahamMatchMode NewMode)
{
	PendingMatchMode = NewMode;
	GetMatchSettings(NewMode);

	// Keep the roster sane for the mode so the lobby UI shows the right slots
	// before a server exists.
	UOneBrahamMatchSettingsAsset* Settings = GetMatchSettings(NewMode);
	if (Settings != nullptr)
	{
		PendingMaxPlayers = Settings->GetExpectedPlayerCount();
	}
}

void UOneBrahamGameInstance::SetPendingMatchSettingsOverride(UOneBrahamMatchSettingsAsset* Settings)
{
	PendingMatchSettingsOverride = Settings;
	if (Settings != nullptr)
	{
		PendingMatchMode = Settings->Mode;
		CachedMatchSettings.Add(Settings->Mode, Settings);
	}
}

// ==========================================================================
//  Sessions
// ==========================================================================
TSharedPtr<FOnlineSessionSettings> UOneBrahamGameInstance::BuildSessionSettings(EOneBrahamMatchMode Mode, int32 MaxPlayers, bool bLAN, const FString& ServerName) const
{
	TSharedPtr<FOnlineSessionSettings> Settings = MakeShared<FOnlineSessionSettings>();

	Settings->bIsLANMatch = bLAN;
	Settings->bUsesPresence = !bLAN;
	Settings->bAllowJoinViaPresence = !bLAN;
	Settings->bAllowJoinViaPresenceFriendsOnly = false;
	Settings->bAllowInvites = true;
	Settings->bAllowJoinInProgress = false;	// ASCENT is one life; joining mid-match is off.
	Settings->bShouldAdvertise = true;
	Settings->bUseLobbiesIfAvailable = !bLAN;
	Settings->bUseLobbiesVoiceChatIfAvailable = false;	// ONE BRAHAM owns its own voice path
	Settings->NumPublicConnections = FMath::Clamp(MaxPlayers, 2, ONE_BRAHAM_MAX_PLAYERS);
	Settings->NumPrivateConnections = 0;
	Settings->bUsesStats = false;
	Settings->bAntiCheatProtected = true;

	// Custom keys read back by the server browser.
	const FString ModeKey = StaticEnum<EOneBrahamMatchMode>()->GetNameStringByValue(static_cast<int64>(Mode));
	Settings->Set(FName(TEXT("OB_MODE")), ModeKey, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName(TEXT("OB_SERVERNAME")), ServerName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName(TEXT("OB_VERSION")), FString(ONE_BRAHAM_DISPLAY_NAME), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName(TEXT("OB_PROTOCOL")), ONE_BRAHAM_NET_PROTOCOL_VERSION, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	// The map the mode plays on: FIRE DRILL has its own arena.
	const UOneBrahamMatchSettingsAsset* ModeSettings = const_cast<UOneBrahamGameInstance*>(this)->GetMatchSettings(Mode);
	const FString MapName = ModeSettings != nullptr && !ModeSettings->MapName.IsNone()
		? ModeSettings->MapName.ToString() : FString(TEXT("L_Persistent_OneBraham"));
	Settings->Set(FName(TEXT("OB_MAP")), MapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName(TEXT("OB_REGION")), Profile ? Profile->PreferredRegion : FString(TEXT("EU-West")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Crossplay: a player's lobby carries their platform and whether they
	// allow crossplay. A dedicated server has no platform of its own (it is a
	// Linux box serving everyone), so it advertises none and suits every client.
	const UOneBrahamPlatformSubsystem* Platform = GetSubsystem<UOneBrahamPlatformSubsystem>();
	Settings->Set(FName(TEXT("OB_PLATFORM")), Platform != nullptr ? UOneBrahamPlatformSubsystem::GetPlatformKey() : FString(),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName(TEXT("OB_CROSSPLAY")), Platform == nullptr || Platform->IsCrossplayAllowed(),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	return Settings;
}

void UOneBrahamGameInstance::HostListenServer(EOneBrahamMatchMode Mode, int32 MaxPlayers, bool bLAN, const FString& ServerName)
{
	SetPendingMatchMode(Mode);
	PendingMaxPlayers = MaxPlayers;
	bPendingLAN = bLAN;
	PendingServerName = ServerName.IsEmpty() ? TEXT("One Braham - Private") : ServerName;

	if (IsDedicatedServerInstance())
	{
		// Already a server: just travel into the map.
		TravelToMatchMap(BuildTravelOptions());
		return;
	}

	if (!bLAN && !CheckOnlinePrivilege())
	{
		return;
	}

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	if (OnlineSubsystem == nullptr || bLAN)
	{
		// LAN / offline: skip session services entirely and listen directly.
		TravelToMatchMap(FString::Printf(TEXT("%s?listen"), *BuildTravelOptions()));
		return;
	}

	const IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OB_WARN("No online session interface - hosting an offline listen server instead.");
		TravelToMatchMap(FString::Printf(TEXT("%s?listen"), *BuildTravelOptions()));
		return;
	}

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "CreatingSession", "Creating session..."), 0.1f);

	// A previous session must be gone before a new one of the same name can
	// exist. DestroySession is asynchronous, so the create waits for it.
	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		bHostAfterDestroy = true;
		DestroyCurrentSession();
		return;
	}

	CreateHostedSession();
}

void UOneBrahamGameInstance::CreateHostedSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		TravelToMatchMap(FString::Printf(TEXT("%s?listen"), *BuildTravelOptions()));
		return;
	}

	const TSharedPtr<FOnlineSessionSettings> Settings = BuildSessionSettings(PendingMatchMode, PendingMaxPlayers, bPendingLAN, PendingServerName);
	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
	CreateSessionDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UOneBrahamGameInstance::HandleCreateSessionComplete));
	if (!Sessions->CreateSession(0, SessionName, *Settings))
	{
		// A synchronous refusal still fires the delegate on most subsystems, but
		// not all: never leave the player on a loading screen waiting for it.
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
		HandleCreateSessionComplete(SessionName, false);
	}
}

void UOneBrahamGameInstance::RegisterDedicatedSession(EOneBrahamMatchMode Mode, int32 MaxPlayers, const FString& ServerName)
{
	if (!IsDedicatedServerInstance())
	{
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OB_LOG("Dedicated server has no online session interface: reachable by address only.");
		return;
	}
	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		// Seamless travel back into the map keeps the session; nothing to do.
		return;
	}

	SetPendingMatchMode(Mode);
	PendingMaxPlayers = MaxPlayers;
	PendingServerName = ServerName.IsEmpty() ? FString(TEXT("One Braham - Dedicated")) : ServerName;

	// A dedicated server is a game server, not a player: no presence, no lobby,
	// and joined by address from the advert rather than through a friend.
	const TSharedPtr<FOnlineSessionSettings> Settings = BuildSessionSettings(Mode, MaxPlayers, false, PendingServerName);
	Settings->bIsDedicated = true;
	Settings->bUsesPresence = false;
	Settings->bAllowJoinViaPresence = false;
	Settings->bUseLobbiesIfAvailable = false;
	Settings->bAllowInvites = false;
	Settings->Set(FName(TEXT("OB_DEDICATED")), true, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
	CreateSessionDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateWeakLambda(this, [this](FName InSessionName, bool bWasSuccessful)
		{
			if (const IOnlineSessionPtr CompletedSessions = GetSessionInterface())
			{
				CompletedSessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
			}
			if (bWasSuccessful)
			{
				OB_LOG("Dedicated server advertised as '%s' (%s).", *PendingServerName, *InSessionName.ToString());
			}
			else
			{
				OB_WARN("Dedicated server could not advertise itself: reachable by address only.");
			}
		}));

	// Dedicated servers create with the platform's server user (0 is fine on
	// Steam: the game-server API ignores the local user index).
	if (!Sessions->CreateSession(0, SessionName, *Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
		OB_WARN("CreateSession refused for the dedicated server advert.");
	}
}

void UOneBrahamGameInstance::StartMatchmaking(EOneBrahamMatchMode Mode)
{
	if (!CheckOnlinePrivilege())
	{
		return;
	}
	SetPendingMatchMode(Mode);
	bMatchmakingActive = true;

	OnMatchmakingStatus.Broadcast(
		NSLOCTEXT("OneBraham", "MatchmakingSearching", "Searching for available matches..."), 0.15f);
	ShowLoadingScreen(NSLOCTEXT("OneBraham", "MatchmakingSearching", "Searching for available matches..."), 0.15f);

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineSessionPtr Sessions = OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;

	if (OnlineSubsystem == nullptr || !Sessions.IsValid())
	{
		// No online services (offline Steam, no network): host our own. The player
		// still gets a game, and bot practice fills the empty roster.
		OB_WARN("Matchmaking called with no online session interface - hosting a local match.");
		bMatchmakingActive = false;
		HostListenServer(Mode, GetMatchSettings(Mode) ? GetMatchSettings(Mode)->GetExpectedPlayerCount() : 12, true, TEXT("One Braham - Offline"));
		return;
	}

	// Player lobbies first, then dedicated servers; the best of both wins.
	if (!StartSessionSearch(true, false, 64, true))
	{
		bMatchmakingActive = false;
		HostListenServer(Mode, GetMatchSettings(Mode) ? GetMatchSettings(Mode)->GetExpectedPlayerCount() : 12, false, TEXT("One Braham - Auto"));
		return;
	}

	// If nothing suitable shows up in time we host, so matchmaking never hangs.
	if (FTimerManager* TimerManager = GetOBTimerManager())
	{
	TimerManager->SetTimer(MatchmakingTimeoutHandle, FTimerDelegate::CreateWeakLambda(this, [this, Mode]()
	{
		if (!bMatchmakingActive)
		{
			return;
		}
		OB_LOG("Matchmaking timed out after %.0fs - hosting a fresh match.", MatchmakingTimeoutSeconds);
		bMatchmakingActive = false;
		CancelSessionSearch();
		HostListenServer(Mode, GetMatchSettings(Mode) ? GetMatchSettings(Mode)->GetExpectedPlayerCount() : 12, false, TEXT("One Braham - Auto"));
	}), MatchmakingTimeoutSeconds, false);
	}
}

bool UOneBrahamGameInstance::CheckOnlinePrivilege()
{
	const UOneBrahamPlatformSubsystem* Platform = GetSubsystem<UOneBrahamPlatformSubsystem>();
	if (Platform == nullptr || Platform->CanPlayOnline())
	{
		return true;
	}
	OnMatchmakingStatus.Broadcast(NSLOCTEXT("OneBraham", "NoOnlinePrivilege",
		"Your account's settings do not allow online play. Practice and LAN games are still available."), 0.f);
	return false;
}

void UOneBrahamGameInstance::CancelMatchmaking()
{
	if (!bMatchmakingActive && ActiveSearch == nullptr)
	{
		return;
	}

	bMatchmakingActive = false;
	CancelSessionSearch();
	if (FTimerManager* TimerManager = GetOBTimerManager())
	{
		TimerManager->ClearTimer(MatchmakingTimeoutHandle);
	}
	HideLoadingScreen();
	OnMatchmakingStatus.Broadcast(NSLOCTEXT("OneBraham", "MatchmakingCancelled", "Matchmaking cancelled."), 0.f);
}

void UOneBrahamGameInstance::StartPracticeMatch(EOneBrahamMatchMode Mode, int32 BotCount)
{
	CancelMatchmaking();
	SetPendingMatchMode(Mode);
	bPendingLAN = true;
	PendingServerName = TEXT("One Braham - Practice");

	// The game mode reads ?Bots= (AOneBrahamGameMode::InitGame) and fills the
	// roster; the cap is the match's own player count less the player.
	const int32 Cap = FMath::Max(1, (GetMatchSettings(Mode) ? GetMatchSettings(Mode)->GetExpectedPlayerCount() : 12) - 1);
	TravelToMatchMap(FString::Printf(TEXT("%s?Bots=%d"), *BuildTravelOptions(), FMath::Clamp(BotCount, 1, Cap)));
}

void UOneBrahamGameInstance::RefreshServerBrowser(bool bLANOnly)
{
	if (!bLANOnly && !CheckOnlinePrivilege())
	{
		OnServerListUpdated.Broadcast(TArray<FOneBrahamServerInfo>());
		return;
	}

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineSessionPtr Sessions = OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		// Offline: report the built-in "host a LAN game" entry so the browser is
		// never an empty dead end for the player.
		TArray<FOneBrahamServerInfo> Fallback;
		FOneBrahamServerInfo Local;
		Local.ServerName = TEXT("Host a local match");
		Local.MapName = TEXT("L_Persistent_OneBraham");
		Local.Mode = PendingMatchMode;
		Local.MaxPlayers = 12;
		Local.PingMs = 0;
		Local.Region = TEXT("Local");
		Fallback.Add(Local);
		OnServerListUpdated.Broadcast(Fallback);
		return;
	}

	// A LAN query has no lobbies: one pass finds every server on the subnet.
	// Online, lobbies and dedicated servers are separate searches.
	if (!StartSessionSearch(!bLANOnly, bLANOnly, 200, !bLANOnly))
	{
		OnServerListUpdated.Broadcast(TArray<FOneBrahamServerInfo>());
	}
}

IOnlineSessionPtr UOneBrahamGameInstance::GetSessionInterface()
{
	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	return OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
}

bool UOneBrahamGameInstance::StartSessionSearch(bool bLobbies, bool bLANOnly, int32 MaxResults, bool bThenDedicated)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return false;
	}

	// A new browse replaces the last: stop it so its answer cannot land in
	// this one's list.
	const bool bFirstPass = bLobbies || !bThenDedicated;
	if (bFirstPass)
	{
		CancelSessionSearch();
		BrowserResults.Reset();
	}
	bSearchDedicatedNext = bThenDedicated && bLobbies;
	bBrowseLAN = bLANOnly;

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = MaxResults;
	ActiveSearch->bIsLanQuery = bLANOnly;
	if (bLobbies)
	{
		ActiveSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}
	else if (!bLANOnly)
	{
		ActiveSearch->QuerySettings.Set(SEARCH_DEDICATED_ONLY, true, EOnlineComparisonOp::Equals);
	}
	ActiveSearch->QuerySettings.Set(FName(TEXT("OB_PROTOCOL")), ONE_BRAHAM_NET_PROTOCOL_VERSION, EOnlineComparisonOp::Equals);

	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
	FindSessionsDelegateHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UOneBrahamGameInstance::HandleFindSessionsComplete));
	if (!Sessions->FindSessions(0, ActiveSearch.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
		ActiveSearch.Reset();
		bSearchDedicatedNext = false;
		return false;
	}
	return true;
}

void UOneBrahamGameInstance::CancelSessionSearch()
{
	const bool bWasSearching = ActiveSearch.IsValid() && ActiveSearch->SearchState == EOnlineAsyncTaskState::InProgress;
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		// Unbind first: a cancel can complete the search with a failure, and
		// that answer belongs to nobody now.
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
		if (bWasSearching)
		{
			Sessions->CancelFindSessions();
		}
	}
	ActiveSearch.Reset();
	bSearchDedicatedNext = false;
}

void UOneBrahamGameInstance::ClearSessionDelegates()
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
	}
	CreateSessionDelegateHandle.Reset();
	FindSessionsDelegateHandle.Reset();
	JoinSessionDelegateHandle.Reset();
	DestroySessionDelegateHandle.Reset();
}

void UOneBrahamGameInstance::JoinServerByIndex(int32 SearchResultIndex)
{
	if (!BrowserResults.IsValidIndex(SearchResultIndex))
	{
		OB_WARN("JoinServerByIndex: index %d is not in the current search results.", SearchResultIndex);
		OnSessionJoined.Broadcast(false, FString());
		return;
	}

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineSessionPtr Sessions = OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		OnSessionJoined.Broadcast(false, FString());
		return;
	}

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "Joining", "Joining match..."), 0.3f);

	// Remember the address so the LAN rejoin path and QA scripts can reuse it.
	const FOnlineSessionSearchResult Result = BrowserResults[SearchResultIndex];
	PendingMaxPlayers = Result.Session.SessionSettings.NumPublicConnections;

	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
	JoinSessionDelegateHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UOneBrahamGameInstance::HandleJoinSessionComplete));
	if (!Sessions->JoinSession(0, SessionName, Result))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
		HandleJoinSessionComplete(SessionName, EOnJoinSessionCompleteResult::UnknownError);
	}
}

void UOneBrahamGameInstance::JoinServerByAddress(const FString& Address, int32 Port)
{
	if (Profile != nullptr)
	{
		Profile->LastUsedServerAddress = Address;
		Profile->LastUsedServerPort = Port;
		SaveProfile();
	}

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "Joining", "Joining match..."), 0.3f);
	TravelToAddress(Address, Port, FString());
}

void UOneBrahamGameInstance::TravelToAddress(const FString& Address, int32 Port, const FString& Options)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Strip the port from the address if the player pasted "ip:port".
	FString Host = Address;
	FString PortPart;
	if (Host.Split(TEXT(":"), &Host, &PortPart) && !PortPart.IsEmpty())
	{
		Port = FCString::Atoi(*PortPart);
	}

	const FString URL = FString::Printf(TEXT("%s:%d%s%s"),
		*Host.TrimStartAndEnd(),
		Port,
		Options.IsEmpty() ? TEXT("") : TEXT("?"),
		*Options);

	OB_LOG("Client travel to %s", *URL);
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC != nullptr)
	{
		PC->ClientTravel(URL, TRAVEL_Absolute);
	}
}

void UOneBrahamGameInstance::DestroyCurrentSession()
{
	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineSessionPtr Sessions = OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
	if (Sessions.IsValid() && Sessions->GetNamedSession(SessionName) != nullptr)
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
		DestroySessionDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UOneBrahamGameInstance::HandleDestroySessionComplete));
		if (!Sessions->DestroySession(SessionName))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
			HandleDestroySessionComplete(SessionName, false);
		}
	}
	else if (bHostAfterDestroy)
	{
		bHostAfterDestroy = false;
		CreateHostedSession();
	}
}

// --- Online callbacks -----------------------------------------------------
void UOneBrahamGameInstance::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
	}

	if (bWasSuccessful)
	{
		OB_LOG("Created session '%s'. Travelling into the match map with 'listen'.",
			*InSessionName.ToString());
		TravelToMatchMap(FString::Printf(TEXT("%s?listen"), *BuildTravelOptions()));
	}
	else
	{
		OB_WARN("CreateSession failed - hosting an offline listen server as a fallback.");
		TravelToMatchMap(FString::Printf(TEXT("%s?listen"), *BuildTravelOptions()));
	}

	OnSessionCreated.Broadcast(bWasSuccessful);
}

void UOneBrahamGameInstance::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
	}

	if (!ActiveSearch.IsValid())
	{
		return;
	}

	if (bWasSuccessful)
	{
		BrowserResults.Append(ActiveSearch->SearchResults);
	}

	// Lobbies are in; now the dedicated servers. A failed lobby pass still
	// runs it: a player behind a lobby outage can still find a server.
	if (bSearchDedicatedNext)
	{
		bSearchDedicatedNext = false;
		if (StartSessionSearch(false, bBrowseLAN, ActiveSearch->MaxSearchResults, true))
		{
			return;
		}
	}

	TArray<FOneBrahamServerInfo> Servers;

	if (BrowserResults.Num() > 0)
	{
		const UOneBrahamPlatformSubsystem* Platform = GetSubsystem<UOneBrahamPlatformSubsystem>();
		const bool bLocalCrossplay = Platform == nullptr || Platform->IsCrossplayAllowed();
		for (int32 Index = 0; Index < BrowserResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = BrowserResults[Index];
			if (!Result.IsValid())
			{
				continue;
			}

			// Crossplay: another platform's lobby shows only when both sides allow it.
			FString HostPlatform;
			bool bHostCrossplay = true;
			Result.Session.SessionSettings.Get(FName(TEXT("OB_PLATFORM")), HostPlatform);
			Result.Session.SessionSettings.Get(FName(TEXT("OB_CROSSPLAY")), bHostCrossplay);
			if (!UOneBrahamPlatformSubsystem::IsCrossplayCompatible(UOneBrahamPlatformSubsystem::GetPlatformKey(), bLocalCrossplay, HostPlatform, bHostCrossplay))
			{
				continue;
			}

			FOneBrahamServerInfo Info;
			Info.SearchResultIndex = Index;
			Info.CurrentPlayers = Result.Session.SessionSettings.NumPublicConnections - Result.Session.NumOpenPublicConnections;
			Info.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Info.PingMs = Result.PingInMs;
			Info.bDedicated = Result.Session.SessionSettings.bIsDedicated;
			Info.bPasswordProtected = false;	// ONE BRAHAM has no passworded servers; private games are invite-only lobbies.

			FString ModeString;
			if (Result.Session.SessionSettings.Get(FName(TEXT("OB_MODE")), ModeString))
			{
				if (const UEnum* ModeEnum = StaticEnum<EOneBrahamMatchMode>())
				{
					const int64 Value = ModeEnum->GetValueByNameString(ModeString);
					Info.Mode = Value != INDEX_NONE ? static_cast<EOneBrahamMatchMode>(Value) : PendingMatchMode;
				}
			}

			Result.Session.SessionSettings.Get(FName(TEXT("OB_SERVERNAME")), Info.ServerName);
			Result.Session.SessionSettings.Get(FName(TEXT("OB_MAP")), Info.MapName);
			Result.Session.SessionSettings.Get(FName(TEXT("OB_REGION")), Info.Region);

			if (Info.ServerName.IsEmpty())
			{
				Info.ServerName = Result.Session.OwningUserName + TEXT("'s One Braham");
			}

			Servers.Add(Info);
		}

		// Lowest ping first: the whole point of a matchmaker in a 12-player
		// shooter is that the fastest server wins.
		Servers.Sort([](const FOneBrahamServerInfo& A, const FOneBrahamServerInfo& B)
		{
			return A.PingMs < B.PingMs;
		});
	}

	OnServerListUpdated.Broadcast(Servers);

	// Matchmaking path: join the best result immediately.
	if (bMatchmakingActive)
	{
		bMatchmakingActive = false;
		if (FTimerManager* TimerManager = GetOBTimerManager())
		{
			TimerManager->ClearTimer(MatchmakingTimeoutHandle);
		}

		if (Servers.Num() > 0 && Servers[0].PingMs <= MaxAcceptablePingMs)
		{
			OnMatchmakingStatus.Broadcast(
				FText::Format(NSLOCTEXT("OneBraham", "MatchmakingFound", "Joining {0} ({1}ms)..."),
					FText::FromString(Servers[0].ServerName), FText::AsNumber(Servers[0].PingMs)), 0.5f);
			JoinServerByIndex(Servers[0].SearchResultIndex);
			return;
		}

		// Nothing suitable: host.
		OB_LOG("Matchmaking found no suitable server (%d results) - hosting.", Servers.Num());
		HostListenServer(PendingMatchMode, GetMatchSettings(PendingMatchMode) ? GetMatchSettings(PendingMatchMode)->GetExpectedPlayerCount() : 12, false, TEXT("One Braham - Auto"));
	}
}

void UOneBrahamGameInstance::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		const TCHAR* Reason = TEXT("Unknown");
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull: Reason = TEXT("the match is full"); break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist: Reason = TEXT("the match no longer exists"); break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress: Reason = TEXT("the host address could not be retrieved"); break;
		case EOnJoinSessionCompleteResult::AlreadyInSession: Reason = TEXT("already in that session"); break;
		case EOnJoinSessionCompleteResult::UnknownError: Reason = TEXT("an unknown session error"); break;
		default: break;
		}

		OB_WARN("JoinSession failed: %s.", Reason);
		HideLoadingScreen();
		OnSessionJoined.Broadcast(false, FString());
		return;
	}

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineSessionPtr Sessions = OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(InSessionName, ConnectString))
	{
		OB_ERROR("JoinSession reported success but no connect string was resolved.");
		HideLoadingScreen();
		OnSessionJoined.Broadcast(false, FString());
		return;
	}

	// Append our travel options so the receiving GameMode knows the mode and the
	// player's chosen archetype straight away.
	const FString FullURL = FString::Printf(TEXT("%s?%s"), *ConnectString, *BuildTravelOptions());

	OB_LOG("Joining match at %s", *ConnectString);
	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(FullURL, TRAVEL_Absolute);
	}

	OnSessionJoined.Broadcast(true, FullURL);
}

void UOneBrahamGameInstance::HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
	}

	OB_LOG("DestroySession('%s') %s.", *InSessionName.ToString(), bWasSuccessful ? TEXT("succeeded") : TEXT("failed"));

	// A re-host was waiting for the old session to go.
	if (bHostAfterDestroy)
	{
		bHostAfterDestroy = false;
		CreateHostedSession();
	}
}

// ==========================================================================
//  Travel / flow
// ==========================================================================
FString UOneBrahamGameInstance::BuildTravelOptions() const
{
	const FString ModeKey = StaticEnum<EOneBrahamMatchMode>()->GetNameStringByValue(static_cast<int64>(PendingMatchMode));
	const FString PlayerName = Profile != nullptr ? Profile->PlayerName : FString();
	const FString LoadoutArchetype = (Profile != nullptr && Profile->Loadouts.IsValidIndex(Profile->SelectedLoadoutIndex))
		? Profile->Loadouts[Profile->SelectedLoadoutIndex].ArchetypeId.ToString()
		: FString(TEXT("BusinessAnalyst"));

	return FString::Printf(TEXT("Mode=%s?Name=%s?Archetype=%s?ServerName=%s?LAN=%d"),
		*ModeKey,
		*PlayerName,
		*LoadoutArchetype,
		*PendingServerName,
		bPendingLAN ? 1 : 0);
}

void UOneBrahamGameInstance::TravelToMainMenu()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	DestroyCurrentSession();

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "ReturningToMenu", "Returning to One Braham..."), 0.6f);

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->ClientTravel(TEXT("/Game/OneBraham/Maps/L_MainMenu"), TRAVEL_Absolute);
	}
	else
	{
		UGameplayStatics::OpenLevel(World, TEXT("L_MainMenu"));
	}
}

void UOneBrahamGameInstance::TravelToMatchMap(const FString& Options)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UOneBrahamMatchSettingsAsset* Settings = GetMatchSettings(PendingMatchMode);
	const FString MapName = Settings != nullptr && !Settings->MapName.IsNone()
		? Settings->MapName.ToString()
		: FString(TEXT("L_Persistent_OneBraham"));

	const FString URL = Options.IsEmpty()
		? FString::Printf(TEXT("/Game/OneBraham/Maps/%s"), *MapName)
		: FString::Printf(TEXT("/Game/OneBraham/Maps/%s?%s"), *MapName, *Options);

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "LoadingBuilding", "Entering One Braham..."), 0.75f);

	OB_LOG("Server travel to %s", *URL);

	if (World->GetNetMode() == NM_DedicatedServer || World->GetNetMode() == NM_ListenServer)
	{
		World->ServerTravel(URL, /*bAbsolute*/ true);
	}
	else
	{
		// Client: OpenLevel with the same option string keeps the flow identical
		// between a listen host and a player who started from the main menu.
		UGameplayStatics::OpenLevel(World, FName(*MapName), true, Options);
	}
}

// ==========================================================================
//  Loading screen
// ==========================================================================
void UOneBrahamGameInstance::ShowLoadingScreen(const FText& StatusText, float Progress)
{
	if (IsDedicatedServerInstance())
	{
		return;
	}

	bLoadingScreenActive = true;

	// A Slate screen on the movie player: it draws on the loading thread while
	// the map streams in, and comes down by itself when the load completes.
	FLoadingScreenAttributes LoadingScreen;
	LoadingScreen.MinimumLoadingScreenDisplayTime = 1.0f;
	LoadingScreen.bAutoCompleteWhenLoadingCompletes = true;
	LoadingScreen.bMoviesAreSkippable = false;
	LoadingScreen.bWaitForManualStop = false;

	const UOneBrahamMatchSettingsAsset* Settings = GetMatchSettings(PendingMatchMode);
	const UEnum* ModeEnum = StaticEnum<EOneBrahamMatchMode>();
	LoadingScreen.WidgetLoadingScreen = SNew(SOneBrahamLoadingScreen)
		.ModeName(Settings != nullptr && !Settings->ModeDisplayName.IsEmpty()
			? Settings->ModeDisplayName
			: (ModeEnum ? ModeEnum->GetDisplayNameTextByValue(static_cast<int64>(PendingMatchMode)) : FText::GetEmpty()))
		.Briefing(Settings != nullptr ? Settings->ModeBriefing : FText::GetEmpty())
		.Status(StatusText)
		.HighestFloor(Profile != nullptr ? Profile->LongestAscentFloorReached : 0);

	GetMoviePlayer()->SetupLoadingScreen(LoadingScreen);
	GetMoviePlayer()->PlayMovie();
}

void UOneBrahamGameInstance::HideLoadingScreen()
{
	if (!bLoadingScreenActive)
	{
		return;
	}

	bLoadingScreenActive = false;
	if (GetMoviePlayer() != nullptr && GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		GetMoviePlayer()->StopMovie();
	}
}
