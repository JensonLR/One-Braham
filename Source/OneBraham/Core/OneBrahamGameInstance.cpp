// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameInstance.h"
#include "UI/FrontEnd/SOneBrahamLiftLoading.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameMode.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "UI/OneBrahamUIStyle.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"
#include "Online.h"
#include "MoviePlayer.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <pwd.h>
#include <unistd.h>
#endif

const FName UOneBrahamGameInstance::SessionName(NAME_GameSession);

UOneBrahamGameInstance::UOneBrahamGameInstance()
{
}

void UOneBrahamGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogOneBraham, Log, TEXT("[OB] GameInstance::Init on %s"),
		IsDedicatedServerInstance() ? TEXT("dedicated server") : TEXT("client"));

	LogOnlineSubsystemInUse();

	if (!IsDedicatedServerInstance())
	{
		// First run: pick a preset this machine can hold before the first map
		// renders. Once per install (the settings object persists that it ran);
		// a no-op on every later launch. Not in the editor, whose scalability is
		// its own.
		if (!GIsEditor)
		{
			if (UOneBrahamGameUserSettings* UserSettings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
			{
				UserSettings->ApplyHardwareDefaultPreset();
			}
		}

		// Saved settings that live in a world (the colour-blind material
		// parameter, sound-mix volumes) start at their defaults in every new
		// world, so they are re-applied whenever one of ours begins play.
		WorldInitializedActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(
			this, &UOneBrahamGameInstance::HandleWorldInitializedActors);

		LoadProfile();

		BindSessionDelegates();

		// A dropped connection or a failed join ends with the engine loading
		// GameDefaultMap - the menu - and the menu says why.
		if (GEngine != nullptr)
		{
			NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UOneBrahamGameInstance::HandleNetworkFailure);
			TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UOneBrahamGameInstance::HandleTravelFailure);
		}

		ResolveDisplayName();
	}

	// Match settings are resolved lazily by GetMatchSettings(), but pre-loading
	// the default mode avoids a hitch on the first mode-select click.
	GetMatchSettings(UOneBrahamGameSettings::Get().DefaultMode);
}

void UOneBrahamGameInstance::LogOnlineSubsystemInUse() const
{
	// Steam is configured as the default, and the online subsystem module drops
	// to NULL on its own when Steam cannot start (no client running, no
	// steamclient library, -nosteam). Nothing else says so in our log, and a
	// NULL subsystem changes what works: LAN sessions and direct IP only, no
	// server browser beyond the LAN, no persona name. One line, so a "can't
	// find any servers" report shows which one it was.
	const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const FName InUse = OnlineSubsystem ? OnlineSubsystem->GetSubsystemName() : NAME_None;

	FString Configured;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), Configured, GEngineIni);

	const bool bSteamDisabled = FParse::Param(FCommandLine::Get(), TEXT("nosteam"));

	if (OnlineSubsystem == nullptr)
	{
		UE_LOG(LogOneBraham, Warning, TEXT("[OB] Online subsystem: none (configured %s) - offline only; direct IP still works"),
			*Configured);
	}
	else if (!Configured.IsEmpty() && InUse != FName(*Configured))
	{
		UE_LOG(LogOneBraham, Log, TEXT("[OB] Online subsystem: %s (configured %s %s) - LAN sessions and direct IP only"),
			*InUse.ToString(), *Configured,
			bSteamDisabled ? TEXT("disabled by -nosteam") : TEXT("could not start"));
	}
	else
	{
		UE_LOG(LogOneBraham, Log, TEXT("[OB] Online subsystem: %s"), *InUse.ToString());
	}
}

void UOneBrahamGameInstance::OnStart()
{
	Super::OnStart();
	HideLoadingScreen();
}

void UOneBrahamGameInstance::Shutdown()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedActorsHandle);
	WorldInitializedActorsHandle.Reset();

	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
	{
		Sessions->OnSessionCreated.Remove(SessionCreatedHandle);
		Sessions->OnSessionsFound.Remove(SessionsFoundHandle);
		Sessions->OnSessionJoined.Remove(SessionJoinedHandle);
	}

	if (!IsDedicatedServerInstance())
	{
		SaveProfile();
		DestroyCurrentSession();
	}

	Super::Shutdown();
}

// ==========================================================================
//  World-bound settings
// ==========================================================================
void UOneBrahamGameInstance::HandleWorldInitializedActors(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (World == nullptr || !World->IsGameWorld() || World->GetGameInstance() != this)
	{
		return;
	}

	// Begin play rather than now: the local player's controller exists by then,
	// and anything a level's own BeginPlay writes to the collection is in first.
	World->OnWorldBeginPlay.AddUObject(this, &UOneBrahamGameInstance::HandleWorldBeginPlay, TWeakObjectPtr<UWorld>(World));
}

void UOneBrahamGameInstance::HandleWorldBeginPlay(TWeakObjectPtr<UWorld> World)
{
	UWorld* BeganWorld = World.Get();
	if (BeganWorld == nullptr || BeganWorld->GetNetMode() == NM_DedicatedServer || GetFirstGamePlayer() == nullptr)
	{
		return;
	}

	if (UOneBrahamGameUserSettings* UserSettings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		UserSettings->ApplyWorldBoundSettings(BeganWorld);
	}

	if (BeganWorld->GetNetMode() == NM_ListenServer)
	{
		WatchMatchForSessionClose(BeganWorld);
	}

	if (BeganWorld->GetNetMode() == NM_Client)
	{
		WatchClientPlayerName(BeganWorld);
	}
}

void UOneBrahamGameInstance::WatchMatchForSessionClose(UWorld* World)
{
	// Without this, the listing outlives the match: a colleague who joins in
	// the end-of-match screen loads the whole tower and is sent straight home
	// with the host. The timer is the world's, so it dies with the match.
	TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(SessionWatchHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakWorld]()
	{
		UWorld* MatchWorld = WeakWorld.Get();
		const AOneBrahamGameState* GameState = MatchWorld != nullptr ? MatchWorld->GetGameState<AOneBrahamGameState>() : nullptr;
		if (GameState == nullptr || !OneBrahamSessionRules::ShouldCloseToJoins(GameState->MatchPhase))
		{
			return;
		}

		if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
		{
			Sessions->CloseToJoins();
		}
		MatchWorld->GetTimerManager().ClearTimer(SessionWatchHandle);
	}), 1.f, true);
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

	const FString Clean = AOneBrahamGameMode::SanitisePlayerName(PlatformPlayerName);
	if (!Clean.IsEmpty() && Clean != Profile->PlayerName)
	{
		Profile->PlayerName = Clean;
		SaveProfile();
	}
}

bool UOneBrahamGameInstance::SetPlayerDisplayName(const FString& NewName)
{
	const FString Clean = AOneBrahamGameMode::SanitisePlayerName(NewName);
	if (Profile == nullptr || Clean.IsEmpty())
	{
		return false;
	}

	Profile->PlayerName = Clean;
	SaveProfile();
	OB_LOG("Display name set to '%s'.", *Clean);
	return true;
}

FString UOneBrahamGameInstance::GetPlayerDisplayName() const
{
	return Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName : MakeDefaultDisplayName();
}

void UOneBrahamGameInstance::ResolveDisplayName()
{
	if (Profile == nullptr)
	{
		return;
	}

	// A launch with -PlayerName=Alex is Alex, under any subsystem.
	FString CommandLineName;
	if (FParse::Value(FCommandLine::Get(), TEXT("PlayerName="), CommandLineName)
		&& !AOneBrahamGameMode::SanitisePlayerName(CommandLineName).IsEmpty())
	{
		ApplyPlatformDisplayName(CommandLineName);
		OB_LOG("Display name '%s' (from -PlayerName=).", *Profile->PlayerName);
		return;
	}

	// Steam (any real platform): the persona name is who the player is to
	// everybody they play with, and the subsystem's nickname is it.
	const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineIdentityPtr Identity = OnlineSubsystem ? OnlineSubsystem->GetIdentityInterface() : nullptr;
	static const FName NullName(TEXT("NULL"));
	const bool bNullSubsystem = OnlineSubsystem == nullptr || OnlineSubsystem->GetSubsystemName() == NullName;
	if (!bNullSubsystem && Identity.IsValid())
	{
		const FString Persona = AOneBrahamGameMode::SanitisePlayerName(Identity->GetPlayerNickname(0));
		const FUniqueNetIdPtr Id = Identity->GetUniquePlayerId(0);
		if (!AOneBrahamGameMode::IsPlaceholderPlayerName(Persona, Id.IsValid() ? Id->ToString() : FString()))
		{
			ApplyPlatformDisplayName(Persona);
			OB_LOG("Display name '%s' (the %s nickname).", *Profile->PlayerName, *OnlineSubsystem->GetSubsystemName().ToString());
			return;
		}
	}

	// NULL (LAN, direct IP): the name the player chose, kept in the profile.
	// What earlier builds wrote there by themselves - the account's login name,
	// the machine's host name - is not a choice, and gives way to the default.
	if (!IsAutomaticDisplayName(Profile->PlayerName))
	{
		OB_LOG("Display name '%s' (the profile's).", *Profile->PlayerName);
		return;
	}

	const FString Previous = Profile->PlayerName;
	Profile->PlayerName = MakeDefaultDisplayName();
	SaveProfile();
	OB_LOG("Display name '%s' (default; the profile had '%s').", *Profile->PlayerName, *Previous);
}

bool UOneBrahamGameInstance::IsAutomaticDisplayName(const FString& Name)
{
	const FString Clean = AOneBrahamGameMode::SanitisePlayerName(Name);
	if (Clean.IsEmpty())
	{
		return true;
	}

	const FString Automatic[] =
	{
		FPlatformProcess::UserName(true),
		FPlatformProcess::UserName(false),
		FString(FPlatformProcess::ComputerName()),
		FPlatformMisc::GetLoginId()
	};
	for (const FString& Candidate : Automatic)
	{
		const FString CleanCandidate = AOneBrahamGameMode::SanitisePlayerName(Candidate);
		if (!CleanCandidate.IsEmpty() && Clean.Equals(CleanCandidate, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	// The NULL nickname: "<host name>-<login id>", whole or cut to the cap. A
	// host name has no spaces; a chosen name that merely opens with the same
	// eight letters ("Jenson R..." on "Jenson Raper's Mac") usually does.
	const FString HostName = AOneBrahamGameMode::SanitisePlayerName(FPlatformProcess::ComputerName());
	if (HostName.Len() >= 8 && !Clean.Contains(TEXT(" ")) && Clean.StartsWith(HostName.Left(8), ESearchCase::IgnoreCase))
	{
		return true;
	}

	return AOneBrahamGameMode::IsPlaceholderPlayerName(Clean, FString());
}

FString UOneBrahamGameInstance::GetOSFullUserName()
{
	FString FullName;
#if PLATFORM_MAC || PLATFORM_LINUX
	// The account's "real name" (System Settings > Users & Groups on a Mac).
	if (const passwd* Entry = getpwuid(getuid()))
	{
		if (Entry->pw_gecos != nullptr)
		{
			FullName = UTF8_TO_TCHAR(Entry->pw_gecos);
			FString Before;
			if (FullName.Split(TEXT(","), &Before, nullptr))
			{
				FullName = Before;
			}
		}
	}
#endif
	FullName = AOneBrahamGameMode::SanitisePlayerName(FullName);

	// A real name and not the login name again (Linux often has no gecos, or the login in it).
	return IsAutomaticNameCandidate(FullName) ? FString() : FullName;
}

bool UOneBrahamGameInstance::IsAutomaticNameCandidate(const FString& Name)
{
	return Name.IsEmpty() || Name.Equals(FPlatformProcess::UserName(true), ESearchCase::IgnoreCase)
		|| Name.Equals(FPlatformProcess::UserName(false), ESearchCase::IgnoreCase);
}

FString UOneBrahamGameInstance::MakeDefaultDisplayName()
{
	const FString FullName = GetOSFullUserName();
	if (!FullName.IsEmpty())
	{
		return FullName;
	}

	// Stable per machine and account, so the same desk gets the same name.
	const uint32 Seed = GetTypeHash(FString(FPlatformProcess::ComputerName()) + FPlatformProcess::UserName(false));
	return MakeOfficeDisplayName(Seed);
}

FString UOneBrahamGameInstance::MakeOfficeDisplayName(uint32 Seed)
{
	// Six letters at most and eight: "<name> from <department>" always fits
	// the twenty-character cap, so it is never cut mid-word on the HUD.
	static const TCHAR* FirstNames[] =
	{
		TEXT("Priya"), TEXT("Tom"), TEXT("Aisha"), TEXT("Owen"), TEXT("Mei"), TEXT("Femi"),
		TEXT("Rosa"), TEXT("Dev"), TEXT("Hana"), TEXT("Callum"), TEXT("Nadia"), TEXT("Jonah"),
		TEXT("Ruth"), TEXT("Kofi"), TEXT("Lena"), TEXT("Sami")
	};
	static const TCHAR* Departments[] =
	{
		TEXT("Payroll"), TEXT("Finance"), TEXT("Legal"), TEXT("IT"), TEXT("HR"), TEXT("Comms"),
		TEXT("Audit"), TEXT("Sales"), TEXT("Estates"), TEXT("Security"), TEXT("Strategy")
	};

	const uint32 Mixed = Seed * 2654435761u;
	const TCHAR* First = FirstNames[Mixed % UE_ARRAY_COUNT(FirstNames)];
	const TCHAR* Department = Departments[(Mixed >> 8) % UE_ARRAY_COUNT(Departments)];
	return FString::Printf(TEXT("%s from %s"), First, Department);
}

FString UOneBrahamGameInstance::AppendDisplayNameOption(const FString& Options) const
{
	const FString Name = AOneBrahamGameMode::SanitisePlayerName(GetPlayerDisplayName());
	if (Name.IsEmpty())
	{
		return Options;
	}

	const FString NameOption = FString::Printf(TEXT("%s=%s"), AOneBrahamGameMode::DisplayNameOption, *Name);
	return Options.IsEmpty() ? NameOption : FString::Printf(TEXT("%s?%s"), *Options, *NameOption);
}

FString UOneBrahamGameInstance::GetClientRequestedName() const
{
	// The URL this client connected with: ?OBName= when this class built it,
	// ?Name= when a person or a script typed it ("127.0.0.1?Name=OBClient").
	// The engine overwrote the latter in the copy it sent, not in this one.
	if (const FWorldContext* Context = GetWorldContext())
	{
		const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
		const IOnlineIdentityPtr Identity = OnlineSubsystem ? OnlineSubsystem->GetIdentityInterface() : nullptr;
		const FUniqueNetIdPtr Id = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
		const FString IdString = Id.IsValid() ? Id->ToString() : FString();

		const FString Options = FString::Join(Context->LastURL.Op, TEXT("?"));
		const FString FromURL = AOneBrahamGameMode::ChooseLoginPlayerName(TEXT("?") + Options, IdString, FString());
		if (!FromURL.IsEmpty())
		{
			return FromURL;
		}
	}

	return AOneBrahamGameMode::SanitisePlayerName(GetPlayerDisplayName());
}

void UOneBrahamGameInstance::WatchClientPlayerName(UWorld* World)
{
	// A client that reached the server some way other than a URL this class
	// built (a direct `open`, the LAN smoke script) logged in without ?OBName=,
	// and the server only had its machine's nickname to go on. Once its player
	// state arrives, it asks for its own name, once: the server sanitises it
	// and makes it unique ("Jenson Raper 2"), and that answer stands.
	TWeakObjectPtr<UWorld> WeakWorld(World);
	TSharedRef<int32> Attempts = MakeShared<int32>(0);
	World->GetTimerManager().SetTimer(ClientNameHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakWorld, Attempts]()
	{
		UWorld* ClientWorld = WeakWorld.Get();
		APlayerController* PC = ClientWorld != nullptr ? GetFirstLocalPlayerController(ClientWorld) : nullptr;
		const FString Desired = GetClientRequestedName();
		if (ClientWorld == nullptr || Desired.IsEmpty() || ++(*Attempts) > 60)
		{
			if (ClientWorld != nullptr)
			{
				ClientWorld->GetTimerManager().ClearTimer(ClientNameHandle);
			}
			return;
		}

		if (PC == nullptr || PC->PlayerState == nullptr)
		{
			return;
		}

		const FString Current = PC->PlayerState->GetPlayerName();
		if (!AOneBrahamGameMode::IsUniqueVariantOfName(Current, Desired))
		{
			OB_LOG("The server calls this player '%s'; asking for '%s'.", *Current, *Desired);
			PC->SetName(Desired);
		}
		ClientWorld->GetTimerManager().ClearTimer(ClientNameHandle);
	}), 1.f, true);
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
			"Run Scripts/BuildContent.sh --only match_settings to author the real asset.", *AssetPath);
	}

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
UOneBrahamSessionSubsystem* UOneBrahamGameInstance::GetSessions() const
{
	return GetSubsystem<UOneBrahamSessionSubsystem>();
}

void UOneBrahamGameInstance::BindSessionDelegates()
{
	UOneBrahamSessionSubsystem* Sessions = GetSessions();
	if (Sessions == nullptr || SessionCreatedHandle.IsValid())
	{
		return;
	}

	SessionCreatedHandle = Sessions->OnSessionCreated.AddUObject(this, &UOneBrahamGameInstance::HandleSessionCreated);
	SessionsFoundHandle = Sessions->OnSessionsFound.AddUObject(this, &UOneBrahamGameInstance::HandleSessionsFound);
	SessionJoinedHandle = Sessions->OnSessionJoined.AddUObject(this, &UOneBrahamGameInstance::HandleSessionJoined);
}

void UOneBrahamGameInstance::StartSoloMatch(EOneBrahamMatchMode Mode)
{
	SetPendingMatchMode(Mode);
	bPendingLAN = false;
	PendingServerName = TEXT("Solo");

	// Nothing to advertise and nobody to let in: the match runs standalone and
	// the bots take every other desk (the mode's own fill rule).
	DestroyCurrentSession();
	OB_LOG("Solo %s from the front end.", *OneBrahamSessionRules::ModeKey(Mode));
	TravelToMatchMap(BuildTravelOptions());
}

void UOneBrahamGameInstance::HostListenServer(EOneBrahamMatchMode Mode, int32 MaxPlayers, bool bLAN, const FString& ServerName)
{
	SetPendingMatchMode(Mode);
	PendingMaxPlayers = MaxPlayers;
	PendingServerName = ServerName.IsEmpty() ? TEXT("One Braham - Private") : ServerName;

	if (IsDedicatedServerInstance())
	{
		// Already a server: just travel into the map.
		TravelToMatchMap(BuildTravelOptions());
		return;
	}

	UOneBrahamSessionSubsystem* Sessions = GetSessions();
	const FName SubsystemName = Sessions != nullptr ? Sessions->GetSubsystemName() : NAME_None;
	bPendingLAN = OneBrahamSessionRules::ResolveLAN(bLAN, SubsystemName);

	FOneBrahamHostRequest Request;
	Request.Mode = Mode;
	Request.MaxPlayers = MaxPlayers;
	Request.bLAN = bPendingLAN;
	Request.ServerName = PendingServerName;
	Request.Port = FURL().Port;
	if (const UOneBrahamMatchSettingsAsset* Settings = GetMatchSettings(Mode))
	{
		if (!Settings->MapName.IsNone())
		{
			Request.MapName = Settings->MapName.ToString();
		}
	}
	if (Profile != nullptr && !Profile->PreferredRegion.IsEmpty())
	{
		Request.Region = Profile->PreferredRegion;
	}

	bHostTravelPending = true;
	if (Sessions == nullptr || !Sessions->HostSession(Request))
	{
		// No session interface at all: a listen server still takes direct IP.
		OB_WARN("No session could be created - hosting a listen server anyone can reach by address.");
		bHostTravelPending = false;
		TravelToMatchMap(BuildTravelOptions() + TEXT("?listen"));
		OnSessionCreated.Broadcast(false);
	}
}

void UOneBrahamGameInstance::HandleSessionCreated(bool bWasSuccessful, bool bLAN)
{
	OnSessionCreated.Broadcast(bWasSuccessful);

	if (!bHostTravelPending)
	{
		// A session created for matchmaking's host fallback goes the same way.
		return;
	}
	bHostTravelPending = false;
	bPendingLAN = bLAN;

	if (!bWasSuccessful)
	{
		OB_WARN("CreateSession failed - hosting a listen server reachable by address only.");
	}

	TravelToMatchMap(BuildTravelOptions() + TEXT("?listen"));
}

void UOneBrahamGameInstance::StartMatchmaking(EOneBrahamMatchMode Mode)
{
	SetPendingMatchMode(Mode);

	UOneBrahamSessionSubsystem* Sessions = GetSessions();
	const int32 Expected = GetMatchSettings(Mode) ? GetMatchSettings(Mode)->GetExpectedPlayerCount() : 12;

	if (Sessions == nullptr || Sessions->IsLANOnly())
	{
		// No online services (no Steam, no network): host our own. The player
		// still gets a game, and the bots fill the empty roster.
		OB_WARN("Matchmaking with no online sessions - hosting a local match.");
		HostListenServer(Mode, Expected, true, TEXT("One Braham - Offline"));
		return;
	}

	bMatchmakingActive = true;
	OnMatchmakingStatus.Broadcast(NSLOCTEXT("OneBraham", "MatchmakingSearching", "Looking for a match..."), 0.15f);

	if (!Sessions->FindSessions(false))
	{
		bMatchmakingActive = false;
		HostListenServer(Mode, Expected, false, TEXT("One Braham - Auto"));
		return;
	}

	// If nothing suitable shows up in time we host, so matchmaking never hangs.
	if (FTimerManager* TimerManager = GetOBTimerManager())
	{
		TimerManager->SetTimer(MatchmakingTimeoutHandle, FTimerDelegate::CreateWeakLambda(this, [this, Mode, Expected]()
		{
			if (!bMatchmakingActive)
			{
				return;
			}
			OB_LOG("Matchmaking timed out after %.0fs - hosting a fresh match.", MatchmakingTimeoutSeconds);
			bMatchmakingActive = false;
			if (UOneBrahamSessionSubsystem* Subsystem = GetSessions())
			{
				Subsystem->CancelFindSessions();
			}
			HostListenServer(Mode, Expected, false, TEXT("One Braham - Auto"));
		}), MatchmakingTimeoutSeconds, false);
	}
}

void UOneBrahamGameInstance::CancelMatchmaking()
{
	if (!bMatchmakingActive)
	{
		return;
	}

	bMatchmakingActive = false;
	if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
	{
		Sessions->CancelFindSessions();
	}
	if (FTimerManager* TimerManager = GetOBTimerManager())
	{
		TimerManager->ClearTimer(MatchmakingTimeoutHandle);
	}
	OnMatchmakingStatus.Broadcast(NSLOCTEXT("OneBraham", "MatchmakingCancelled", "Matchmaking cancelled."), 0.f);
}

void UOneBrahamGameInstance::RefreshServerBrowser(bool bLANOnly)
{
	UOneBrahamSessionSubsystem* Sessions = GetSessions();
	if (Sessions == nullptr || !Sessions->FindSessions(bLANOnly))
	{
		// Offline: an empty list, said as such, rather than a spinner forever.
		OnServerListUpdated.Broadcast(TArray<FOneBrahamServerInfo>());
	}
}

void UOneBrahamGameInstance::HandleSessionsFound(bool bWasSuccessful, const TArray<FOneBrahamServerInfo>& Servers)
{
	OnServerListUpdated.Broadcast(Servers);

	if (!bMatchmakingActive)
	{
		return;
	}

	// Matchmaking path: join the best result immediately.
	bMatchmakingActive = false;
	if (FTimerManager* TimerManager = GetOBTimerManager())
	{
		TimerManager->ClearTimer(MatchmakingTimeoutHandle);
	}

	if (Servers.Num() > 0 && Servers[0].PingMs <= MaxAcceptablePingMs)
	{
		OnMatchmakingStatus.Broadcast(
			FText::Format(NSLOCTEXT("OneBraham", "MatchmakingFound", "Joining {0} ({1} ms)..."),
				FText::FromString(Servers[0].ServerName), FText::AsNumber(Servers[0].PingMs)), 0.5f);
		JoinServerByIndex(Servers[0].SearchResultIndex);
		return;
	}

	OB_LOG("Matchmaking found no suitable match (%d results) - hosting.", Servers.Num());
	HostListenServer(PendingMatchMode, GetMatchSettings(PendingMatchMode) ? GetMatchSettings(PendingMatchMode)->GetExpectedPlayerCount() : 12,
		false, TEXT("One Braham - Auto"));
}

void UOneBrahamGameInstance::JoinServerByIndex(int32 SearchResultIndex)
{
	UOneBrahamSessionSubsystem* Sessions = GetSessions();

	if (Sessions != nullptr)
	{
		for (const FOneBrahamServerInfo& Row : Sessions->GetLastResults())
		{
			if (Row.SearchResultIndex == SearchResultIndex)
			{
				SetPendingMatchMode(Row.Mode);
				PendingMaxPlayers = Row.MaxPlayers;
				break;
			}
		}
	}

	// The joiner's own options: the host's GameMode reads Name and Archetype
	// from the URL the client arrives with.
	FOneBrahamTravelRequest Request = BuildTravelRequest(false);
	Request.ServerName.Reset();
	const FString Options = AppendDisplayNameOption(OneBrahamSessionRules::MakeTravelOptions(Request));

	// A refusal by the subsystem comes back through HandleSessionJoined like
	// any other answer; only "no subsystem at all" is said here.
	if (Sessions == nullptr)
	{
		OnSessionJoined.Broadcast(false, FString());
		return;
	}
	Sessions->JoinSession(SearchResultIndex, Options);
}

void UOneBrahamGameInstance::HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL, const FText& Reason)
{
	OnSessionJoined.Broadcast(bWasSuccessful, TravelURL);

	if (!bWasSuccessful)
	{
		// Not a menu notice: the player is on the JOIN page, which says why
		// itself. A notice kept now would surface after some later match, and
		// (first reason wins) hide the reason that match really ended.
		OB_WARN("Join failed: %s", *Reason.ToString());
		return;
	}

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "Joining", "Joining the match..."), 0.3f);

	OB_LOG("Client travel to %s", *TravelURL);
	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(TravelURL, TRAVEL_Absolute);
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

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "Joining", "Joining the match..."), 0.3f);
	TravelToAddress(Address, Port, AppendDisplayNameOption(OneBrahamSessionRules::MakeTravelOptions(BuildTravelRequest(false))));
}

void UOneBrahamGameInstance::TravelToAddress(const FString& Address, int32 Port, const FString& Options)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// "ip:port" pasted whole wins over the port argument.
	const FString URL = OneBrahamSessionRules::BuildJoinURL(Address, Port, Options);

	OB_LOG("Client travel to %s", *URL);
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC != nullptr)
	{
		PC->ClientTravel(URL, TRAVEL_Absolute);
	}
}

void UOneBrahamGameInstance::DestroyCurrentSession()
{
	if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
	{
		if (Sessions->HasSession())
		{
			Sessions->DestroySession();
		}
	}
}

// --- Failures, kept for the menu -------------------------------------------
void UOneBrahamGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (World != nullptr && World->GetGameInstance() != this)
	{
		return;
	}

	// The engine reports a guest's dropped connection to the host too (the
	// same ConnectionTimeout a client gets). For a host that is one empty
	// desk, not a reason to tear down its own session - which would unlist the
	// match mid-game - or to greet it in the menu later with "connection
	// lost". The engine's own rule (UEngine::HandleNetworkFailure): hosts do
	// not travel for a client's failures, so neither does anything here.
	const bool bHostSide = NetDriver != nullptr && NetDriver->GetNetMode() != NM_Client;
	if (bHostSide)
	{
		switch (FailureType)
		{
		case ENetworkFailure::ConnectionLost:
		case ENetworkFailure::ConnectionTimeout:
		case ENetworkFailure::NetGuidMismatch:
		case ENetworkFailure::NetChecksumMismatch:
		case ENetworkFailure::OutdatedClient:
		case ENetworkFailure::OutdatedServer:
			OB_LOG("A guest's connection failed (%s): %s", ENetworkFailure::ToString(FailureType), *ErrorString);
			return;
		default:
			break;
		}
	}

	OB_WARN("Network failure (%s): %s", ENetworkFailure::ToString(FailureType), *ErrorString);

	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::ConnectionTimeout:
		SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_ConnectionLost", "Connection lost. The host may have left the building."));
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_Version", "That match is running a different version of the game."));
		break;
	case ENetworkFailure::PendingConnectionFailure:
		SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_CouldNotConnect", "Could not reach the host."));
		break;
	case ENetworkFailure::NetDriverListenFailure:
		SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_ListenFailed", "Could not open the floor. Another game may be using the port."));
		break;
	default:
		SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_NetFailure", "Disconnected from the match."));
		break;
	}

	DestroyCurrentSession();
}

void UOneBrahamGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (World != nullptr && World->GetGameInstance() != this)
	{
		return;
	}

	OB_WARN("Travel failure (%s): %s", ETravelFailure::ToString(FailureType), *ErrorString);
	SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_TravelFailed", "The match could not be reached."));
	DestroyCurrentSession();
}

FText UOneBrahamGameInstance::ConsumeMenuNotice()
{
	FText Notice = PendingMenuNotice;
	PendingMenuNotice = FText::GetEmpty();
	return Notice;
}

void UOneBrahamGameInstance::SetMenuNotice(const FText& Notice)
{
	// The first reason wins: a lost connection is followed by a travel failure
	// back to the default map, and the first one is the one the player needs.
	if (PendingMenuNotice.IsEmpty())
	{
		PendingMenuNotice = Notice;
	}
}

// ==========================================================================
//  Travel / flow
// ==========================================================================
FOneBrahamTravelRequest UOneBrahamGameInstance::BuildTravelRequest(bool bListen) const
{
	FOneBrahamTravelRequest Request;
	Request.Mode = PendingMatchMode;
	Request.PlayerName = GetPlayerDisplayName();
	Request.Archetype = (Profile != nullptr && Profile->Loadouts.IsValidIndex(Profile->SelectedLoadoutIndex))
		? Profile->Loadouts[Profile->SelectedLoadoutIndex].ArchetypeId.ToString()
		: FString(TEXT("BusinessAnalyst"));
	Request.ServerName = PendingServerName;
	Request.bLAN = bPendingLAN;
	Request.bListen = bListen;
	return Request;
}

FString UOneBrahamGameInstance::BuildTravelOptions() const
{
	// ?Name= is the engine's to overwrite at login (with the online nickname),
	// so the chosen name also travels as ?OBName=, which it leaves alone.
	return AppendDisplayNameOption(OneBrahamSessionRules::MakeTravelOptions(BuildTravelRequest(false)));
}

void UOneBrahamGameInstance::ReturnToMainMenu()
{
	// ClientReturnToMainMenuWithTextReason ends here, and so does a kick. The
	// engine drops the reason on the floor; the menu at least says why.
	SetMenuNotice(NSLOCTEXT("OneBraham", "Notice_Returned", "The match has ended for you."));
	DestroyCurrentSession();
	Super::ReturnToMainMenu();
}

void UOneBrahamGameInstance::TravelToMainMenu()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const ENetMode NetMode = World->GetNetMode();

	// Everyone connected to this machine goes home first, with a reason, while
	// their connections are still open to carry it.
	if (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer)
	{
		const FText Reason = NetMode == NM_DedicatedServer
			? NSLOCTEXT("OneBraham", "Notice_MatchOver", "The match is over.")
			: NSLOCTEXT("OneBraham", "Notice_HostClosed", "The host has closed the floor.");

		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller != nullptr && !Controller->IsLocalController() && Controller->GetNetConnection() != nullptr)
			{
				Controller->ClientReturnToMainMenuWithTextReason(Reason);
			}
		}
	}

	if (NetMode == NM_DedicatedServer)
	{
		// No menu on a server: the map starts over for the next lobby. A short
		// wait lets the return-to-menu RPCs leave before the connections close.
		OB_LOG("Match over on the dedicated server - restarting the map.");
		if (FTimerManager* TimerManager = GetOBTimerManager())
		{
			FTimerHandle Restart;
			TimerManager->SetTimer(Restart, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UWorld* ServerWorld = GetWorld())
				{
					ServerWorld->ServerTravel(TEXT("?restart"), false);
				}
			}), 2.f, false);
		}
		return;
	}

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "ReturningToMenu", "Signing out..."), 0.6f);

	if (NetMode == NM_Client)
	{
		// The engine's disconnect: destroys NAME_GameSession, closes the
		// connection and loads GameDefaultMap, which is the menu.
		DestroyCurrentSession();
		GetEngine()->HandleDisconnect(World, World->GetNetDriver());
		return;
	}

	DestroyCurrentSession();

	// Standalone or the listen host: a fresh load of the menu, without ?listen,
	// which also closes the host's net driver.
	OB_LOG("Travelling to the main menu.");
	UGameplayStatics::OpenLevel(World, FName(MainMenuMapPath()), true);
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
	const FString MapPath = FString::Printf(TEXT("%s/%s"), *OneBrahamSessionRules::MapsFolder, *MapName);

	ShowLoadingScreen(NSLOCTEXT("OneBraham", "LoadingBuilding", "Entering One Braham..."), 0.75f);

	OB_LOG("Travel to %s%s%s", *MapPath, Options.IsEmpty() ? TEXT("") : TEXT("?"), *Options);

	if (World->GetNetMode() == NM_DedicatedServer || World->GetNetMode() == NM_ListenServer)
	{
		const FString URL = Options.IsEmpty() ? MapPath : FString::Printf(TEXT("%s?%s"), *MapPath, *Options);
		World->ServerTravel(URL, /*bAbsolute*/ true);
	}
	else
	{
		// From the front end (standalone): a hard load with the same options,
		// "?listen" among them when hosting, so the flow is the same for a
		// listen host and a solo player.
		UGameplayStatics::OpenLevel(World, FName(*MapPath), true, Options);
	}
}

// ==========================================================================
//  Loading screen
// ==========================================================================
void UOneBrahamGameInstance::ShowLoadingScreen(const FText& StatusText, float Progress)
{
	if (IsDedicatedServerInstance() || !IsMoviePlayerEnabled() || GetMoviePlayer() == nullptr)
	{
		return;
	}

	bLoadingScreenActive = true;

	// The engine movie player puts this on screen at the next map load
	// (PreLoadMap) and takes it down when the map has loaded: THE LIFT, a floor
	// counter going up and the tower lighting floor by floor with the real
	// load, drawn by Slate on the movie player's own thread so it keeps up
	// while the game thread is busy loading (UI/FrontEnd/SOneBrahamLiftLoading).
	FText ModeName;
	switch (PendingMatchMode)
	{
	case EOneBrahamMatchMode::FireDrill:	ModeName = NSLOCTEXT("OneBraham", "Menu_ModeFireDrill", "FIRE DRILL"); break;
	case EOneBrahamMatchMode::NightShift:	ModeName = NSLOCTEXT("OneBraham", "Menu_ModeNightShift", "NIGHT SHIFT"); break;
	default:								ModeName = NSLOCTEXT("OneBraham", "Menu_ModeAscent", "ASCENT"); break;
	}
	TArray<FString> Squad;
	Squad.Add(Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName : FString(TEXT("You")));

	TSharedRef<SWidget> Page = SNew(SOneBrahamLiftLoading)
		.ModeName(ModeName)
		.Status(StatusText)
		.Squad(Squad)
		.TargetFloor(PendingMatchMode == EOneBrahamMatchMode::FireDrill ? 15 : 18);

	FLoadingScreenAttributes LoadingScreen;
	LoadingScreen.WidgetLoadingScreen = Page;
	LoadingScreen.MinimumLoadingScreenDisplayTime = 0.5f;
	LoadingScreen.bAutoCompleteWhenLoadingCompletes = true;
	LoadingScreen.bMoviesAreSkippable = false;
	LoadingScreen.bWaitForManualStop = false;
	LoadingScreen.PlaybackType = MT_Normal;

	GetMoviePlayer()->SetupLoadingScreen(LoadingScreen);
}

void UOneBrahamGameInstance::HideLoadingScreen()
{
	if (!bLoadingScreenActive)
	{
		return;
	}

	bLoadingScreenActive = false;
	if (IsMoviePlayerEnabled() && GetMoviePlayer() != nullptr && GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		GetMoviePlayer()->StopMovie();
	}
}
