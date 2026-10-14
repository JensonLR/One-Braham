// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamUILibrary.h"

#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameplayStatics.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Data/OneBrahamCosmeticDataAsset.h"
#include "Online/OneBrahamSteamSubsystem.h"

const UOneBrahamSaveSettings* UOneBrahamUILibrary::LocalProfile(const UObject* WorldContextObject)
{
	const UOneBrahamGameInstance* GameInstance = UOneBrahamGameplayStatics::GetOneBrahamGameInstance(WorldContextObject);
	return GameInstance ? GameInstance->GetProfile() : nullptr;
}

TArray<FOneBrahamAchievementView> UOneBrahamUILibrary::GetAchievements(const UObject* WorldContextObject)
{
	const UOneBrahamSaveSettings* Profile = LocalProfile(WorldContextObject);
	TArray<FOneBrahamAchievementView> Views;
	for (const FOneBrahamAchievementDefinition& Entry : UOneBrahamSteamSubsystem::GetAchievementDefinitions())
	{
		FOneBrahamAchievementView View;
		View.AchievementId = FName(Entry.Id);
		View.Title = FText::FromString(Entry.Title);
		View.Description = FText::FromString(Entry.Description);
		View.bUnlocked = Profile != nullptr && Profile->UnlockedAchievements.Contains(View.AchievementId);
		Views.Add(View);
	}
	return Views;
}

bool UOneBrahamUILibrary::IsCosmeticUnlocked(const UObject* WorldContextObject, FName CosmeticId)
{
	return UOneBrahamCosmeticDataAsset::LoadCatalogue()->IsUnlockedFor(LocalProfile(WorldContextObject), CosmeticId);
}

FText UOneBrahamUILibrary::GetCosmeticLockText(const UObject* WorldContextObject, FName CosmeticId)
{
	const UOneBrahamCosmeticDataAsset* Catalogue = UOneBrahamCosmeticDataAsset::LoadCatalogue();
	if (Catalogue->IsUnlockedFor(LocalProfile(WorldContextObject), CosmeticId))
	{
		return FText::GetEmpty();
	}
	const FOneBrahamCosmeticUnlock* Row = Catalogue->FindUnlock(CosmeticId);
	return Row ? Row->HowToUnlock : FText::GetEmpty();
}

EOneBrahamPingBand UOneBrahamUILibrary::GetPingBand(int32 PingMs)
{
	if (PingMs < GoodPingMs)
	{
		return EOneBrahamPingBand::Good;
	}
	return PingMs <= UOneBrahamGameInstance::MaxAcceptablePingMs ? EOneBrahamPingBand::Playable : EOneBrahamPingBand::Poor;
}

bool UOneBrahamUILibrary::OpenSteamOverlay(const UObject* WorldContextObject, int32 Page)
{
	const UOneBrahamGameInstance* GameInstance = UOneBrahamGameplayStatics::GetOneBrahamGameInstance(WorldContextObject);
	UOneBrahamSteamSubsystem* Steam = GameInstance ? GameInstance->GetSubsystem<UOneBrahamSteamSubsystem>() : nullptr;
	if (Steam == nullptr)
	{
		return false;
	}
	switch (Page)
	{
	case 1: return Steam->ShowInviteOverlay();
	case 2: return Steam->ShowStoreOverlay();
	default: return Steam->ShowFriendsOverlay();
	}
}
