// Copyright (c) One Braham. All Rights Reserved.

#include "Online/OneBrahamPlatformSubsystem.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "UI/OneBrahamHUDComponent.h"
#include "UI/OneBrahamUIStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformProperties.h"
#include "Misc/CoreDelegates.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"

namespace
{
	IOnlineIdentityPtr GetIdentity()
	{
		const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
		return OnlineSubsystem != nullptr ? OnlineSubsystem->GetIdentityInterface() : nullptr;
	}

	FUniqueNetIdPtr GetLocalUserId()
	{
		const IOnlineIdentityPtr Identity = GetIdentity();
		return Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	}
}

bool UOneBrahamPlatformSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UOneBrahamPlatformSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UOneBrahamPlatformSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UOneBrahamPlatformSubsystem::HandleTravelFailure);
	}

	DeviceConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(
		this, &UOneBrahamPlatformSubsystem::HandleInputDeviceConnectionChange);

	// Deactivate is focus loss on a desktop; background is a console suspend
	// or a phone-style lifecycle. Either one means the player is not looking.
	DeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UOneBrahamPlatformSubsystem::HandleDeactivate);
	BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UOneBrahamPlatformSubsystem::HandleDeactivate);
	ReactivateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &UOneBrahamPlatformSubsystem::HandleReactivate);
	ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UOneBrahamPlatformSubsystem::HandleReactivate);

	RefreshPrivileges();
	QueryBlockedPlayers();
}

void UOneBrahamPlatformSubsystem::Deinitialize()
{
	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(DeviceConnectionHandle);
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
	FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivateHandle);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);

	if (const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get())
	{
		if (const IOnlineFriendsPtr Friends = OnlineSubsystem->GetFriendsInterface())
		{
			Friends->ClearOnQueryBlockedPlayersCompleteDelegate_Handle(BlockedQueryHandle);
		}
	}

	Super::Deinitialize();
}

UOneBrahamPlatformSubsystem* UOneBrahamPlatformSubsystem::Get(const UObject* WorldContext)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UOneBrahamPlatformSubsystem>() : nullptr;
}

// ==========================================================================
//  Privileges and crossplay
// ==========================================================================
bool UOneBrahamPlatformSubsystem::IsCrossplayAllowed() const
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	return bCanCrossPlay && (Settings == nullptr || Settings->bAllowCrossplay);
}

void UOneBrahamPlatformSubsystem::RefreshPrivileges()
{
	const IOnlineIdentityPtr Identity = GetIdentity();
	const FUniqueNetIdPtr UserId = GetLocalUserId();
	if (!Identity.IsValid() || !UserId.IsValid())
	{
		// No platform, or nobody signed in yet: nothing has said no.
		return;
	}

	const TWeakObjectPtr<UOneBrahamPlatformSubsystem> WeakThis(this);
	auto Ask = [&Identity, &UserId, WeakThis](EUserPrivileges::Type Privilege, bool UOneBrahamPlatformSubsystem::* Field)
	{
		Identity->GetUserPrivilege(*UserId, Privilege, IOnlineIdentity::FOnGetUserPrivilegeCompleteDelegate::CreateLambda(
			[WeakThis, Field](const FUniqueNetId&, EUserPrivileges::Type Asked, uint32 PrivilegeResult)
			{
				if (UOneBrahamPlatformSubsystem* Self = WeakThis.Get())
				{
					const bool bAllowed = PrivilegeResult == static_cast<uint32>(IOnlineIdentity::EPrivilegeResults::NoFailures);
					if (Self->*Field != bAllowed)
					{
						OB_LOG("Platform privilege %d is now %s.", static_cast<int32>(Asked), bAllowed ? TEXT("allowed") : TEXT("refused"));
					}
					Self->*Field = bAllowed;
				}
			}));
	};

	Ask(EUserPrivileges::CanPlayOnline, &UOneBrahamPlatformSubsystem::bCanPlayOnline);
	Ask(EUserPrivileges::CanCommunicateOnline, &UOneBrahamPlatformSubsystem::bCanCommunicate);
	Ask(EUserPrivileges::CanUserCrossPlay, &UOneBrahamPlatformSubsystem::bCanCrossPlay);
}

FString UOneBrahamPlatformSubsystem::GetPlatformKey()
{
	return FString(FPlatformProperties::IniPlatformName());
}

bool UOneBrahamPlatformSubsystem::IsCrossplayCompatible(const FString& LocalPlatform, bool bLocalAllows, const FString& HostPlatform, bool bHostAllows)
{
	// A session that does not say (an older build) counts as the local platform:
	// hiding every old server from everyone is worse than the rare mismatch.
	if (HostPlatform.IsEmpty() || HostPlatform.Equals(LocalPlatform, ESearchCase::IgnoreCase))
	{
		return true;
	}
	return bLocalAllows && bHostAllows;
}

// ==========================================================================
//  Blocked players
// ==========================================================================
void UOneBrahamPlatformSubsystem::QueryBlockedPlayers()
{
	const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineFriendsPtr Friends = OnlineSubsystem != nullptr ? OnlineSubsystem->GetFriendsInterface() : nullptr;
	const FUniqueNetIdPtr UserId = GetLocalUserId();
	if (!Friends.IsValid() || !UserId.IsValid())
	{
		return;
	}

	Friends->ClearOnQueryBlockedPlayersCompleteDelegate_Handle(BlockedQueryHandle);
	BlockedQueryHandle = Friends->AddOnQueryBlockedPlayersCompleteDelegate_Handle(
		FOnQueryBlockedPlayersCompleteDelegate::CreateUObject(this, &UOneBrahamPlatformSubsystem::HandleBlockedPlayersQueried));

	// Not every platform keeps a block list here (Steam's lives in its own
	// client); false means nothing will answer, so stop listening.
	if (!Friends->QueryBlockedPlayers(*UserId))
	{
		Friends->ClearOnQueryBlockedPlayersCompleteDelegate_Handle(BlockedQueryHandle);
	}
}

void UOneBrahamPlatformSubsystem::HandleBlockedPlayersQueried(const FUniqueNetId& UserId, bool bWasSuccessful, const FString& Error)
{
	const IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	const IOnlineFriendsPtr Friends = OnlineSubsystem != nullptr ? OnlineSubsystem->GetFriendsInterface() : nullptr;
	if (!Friends.IsValid())
	{
		return;
	}
	Friends->ClearOnQueryBlockedPlayersCompleteDelegate_Handle(BlockedQueryHandle);

	if (!bWasSuccessful)
	{
		OB_WARN("Block list query failed: %s", *Error);
		return;
	}

	TArray<TSharedRef<FOnlineBlockedPlayer>> Blocked;
	Friends->GetBlockedPlayers(UserId, Blocked);

	TArray<FString> Ids;
	for (const TSharedRef<FOnlineBlockedPlayer>& Player : Blocked)
	{
		Ids.Add(Player->GetUserId()->ToString());
	}
	SetBlockedIds(Ids);
}

void UOneBrahamPlatformSubsystem::SetBlockedIds(const TArray<FString>& Ids)
{
	BlockedIds = TSet<FString>(Ids);
}

bool UOneBrahamPlatformSubsystem::IsBlocked(const APlayerState* Player) const
{
	if (Player == nullptr || BlockedIds.Num() == 0)
	{
		return false;
	}
	const FUniqueNetIdRepl& Id = Player->GetUniqueId();
	return Id.IsValid() && BlockedIds.Contains(Id->ToString());
}

// ==========================================================================
//  Notices
// ==========================================================================
FText UOneBrahamPlatformSubsystem::DescribeNetworkFailure(ENetworkFailure::Type FailureType)
{
	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
		return NSLOCTEXT("OneBraham", "Net_Lost", "Connection to the server was lost.");
	case ENetworkFailure::ConnectionTimeout:
		return NSLOCTEXT("OneBraham", "Net_Timeout", "The server stopped responding.");
	case ENetworkFailure::OutdatedClient:
		return NSLOCTEXT("OneBraham", "Net_OldClient", "That server runs a newer version. Update the game to join it.");
	case ENetworkFailure::OutdatedServer:
		return NSLOCTEXT("OneBraham", "Net_OldServer", "That server runs an older version of the game.");
	case ENetworkFailure::PendingConnectionFailure:
		return NSLOCTEXT("OneBraham", "Net_Refused", "The server refused the connection. It may be full or mid-match.");
	case ENetworkFailure::NetGuidMismatch:
	case ENetworkFailure::NetChecksumMismatch:
		return NSLOCTEXT("OneBraham", "Net_Mismatch", "Your game files do not match the server's. Verify the game files and try again.");
	case ENetworkFailure::FailureReceived:
		return NSLOCTEXT("OneBraham", "Net_Closed", "The server closed the connection.");
	default:
		return NSLOCTEXT("OneBraham", "Net_Other", "A network error ended the match.");
	}
}

FText UOneBrahamPlatformSubsystem::DescribeTravelFailure(ETravelFailure::Type FailureType)
{
	switch (FailureType)
	{
	case ETravelFailure::PackageMissing:
	case ETravelFailure::NoLevel:
	case ETravelFailure::LoadMapFailure:
		return NSLOCTEXT("OneBraham", "Travel_Missing", "The match map could not be loaded. Verify the game files and try again.");
	case ETravelFailure::PackageVersion:
		return NSLOCTEXT("OneBraham", "Travel_Version", "The server's map is from a different version of the game.");
	case ETravelFailure::PendingNetGameCreateFailure:
		return NSLOCTEXT("OneBraham", "Travel_Connect", "Could not reach the server.");
	default:
		return NSLOCTEXT("OneBraham", "Travel_Other", "Could not join the match.");
	}
}

FText UOneBrahamPlatformSubsystem::ConsumeNotice()
{
	FText Notice = PendingNotice;
	PendingNotice = FText::GetEmpty();
	return Notice;
}

void UOneBrahamPlatformSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// A listen host's failures are its clients' problem; this is for the client
	// whose server just went away.
	if (World == nullptr || World->GetNetMode() != NM_Client)
	{
		return;
	}
	OB_WARN("Network failure %s: %s", ENetworkFailure::ToString(FailureType), *ErrorString);
	PostNotice(DescribeNetworkFailure(FailureType));
}

void UOneBrahamPlatformSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	OB_WARN("Travel failure %s: %s", ETravelFailure::ToString(FailureType), *ErrorString);
	PostNotice(DescribeTravelFailure(FailureType));
}

// ==========================================================================
//  Controller and lifecycle
// ==========================================================================
void UOneBrahamPlatformSubsystem::OpenPauseMenuIfPlaying()
{
	const UGameInstance* GameInstance = GetGameInstance();
	AOneBrahamPlayerController* PC = GameInstance != nullptr
		? Cast<AOneBrahamPlayerController>(GameInstance->GetFirstLocalPlayerController())
		: nullptr;
	if (PC != nullptr && PC->GetPawn() != nullptr && !PC->IsPauseMenuOpen())
	{
		PC->TogglePauseMenu();
	}
}

void UOneBrahamPlatformSubsystem::HandleInputDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId)
{
	const UGameInstance* GameInstance = GetGameInstance();
	AOneBrahamPlayerController* PC = GameInstance != nullptr
		? Cast<AOneBrahamPlayerController>(GameInstance->GetFirstLocalPlayerController())
		: nullptr;

	if (NewState == EInputDeviceConnectionState::Disconnected)
	{
		// Only the device the player was using: a keyboard player's idle pad
		// going flat is not a reason to interrupt them.
		if (PC == nullptr || !PC->IsUsingGamepad())
		{
			return;
		}
		bControllerDisconnected = true;
		OpenPauseMenuIfPlaying();
		if (UOneBrahamHUDComponent* HUD = PC->GetHUDComponent())
		{
			HUD->ShowBanner(NSLOCTEXT("OneBraham", "Pad_Lost", "CONTROLLER DISCONNECTED - RECONNECT TO CONTINUE"), 6.f, UOneBrahamUIStyle::SafetyYellow());
		}
		OnControllerConnectionChanged.Broadcast(false);
	}
	else if (NewState == EInputDeviceConnectionState::Connected && bControllerDisconnected)
	{
		bControllerDisconnected = false;
		if (PC != nullptr)
		{
			if (UOneBrahamHUDComponent* HUD = PC->GetHUDComponent())
			{
				HUD->ShowBanner(NSLOCTEXT("OneBraham", "Pad_Back", "CONTROLLER CONNECTED"), 2.f, UOneBrahamUIStyle::SuccessGreen());
			}
		}
		OnControllerConnectionChanged.Broadcast(true);
	}
}

void UOneBrahamPlatformSubsystem::HandleDeactivate()
{
	// In the editor, clicking from the game into the editor deactivates the
	// game; the menu popping up every time would be unbearable.
	if (GIsEditor)
	{
		return;
	}
	OpenPauseMenuIfPlaying();
}

void UOneBrahamPlatformSubsystem::HandleReactivate()
{
	// The player may have changed privacy settings, or blocked someone, while
	// the game was suspended.
	RefreshPrivileges();
	QueryBlockedPlayers();
}
