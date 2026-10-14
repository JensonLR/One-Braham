// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMainMenuWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Data/OneBrahamCharacterDataAsset.h"
#include "Data/OneBrahamCosmeticDataAsset.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/OneBrahamSettingsWidget.h"
#include "UI/OneBrahamUILibrary.h"

namespace
{
	/** "Jacket_Bomber" -> "Bomber": the family is the slot's name already. */
	FText CosmeticLabel(FName CosmeticId)
	{
		FString Family;
		FString Item;
		const FString Id = CosmeticId.ToString();
		return FText::FromString(Id.Split(TEXT("_"), &Family, &Item) ? Item.Replace(TEXT("_"), TEXT(" ")) : Id);
	}

	/** OB.Character.Archetype.BusinessAnalyst -> BusinessAnalyst, the profile's id. */
	FName ArchetypeIdOf(const FGameplayTag& Tag)
	{
		FString Left;
		FString Right;
		return Tag.ToString().Split(TEXT("."), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd) ? FName(*Right) : NAME_None;
	}

	FLinearColor PingColour(int32 PingMs)
	{
		switch (UOneBrahamUILibrary::GetPingBand(PingMs))
		{
		case EOneBrahamPingBand::Good: return UOneBrahamUIStyle::SuccessGreen();
		case EOneBrahamPingBand::Playable: return UOneBrahamUIStyle::SafetyYellow();
		default: return UOneBrahamUIStyle::EmergencyRed();
		}
	}
}

UOneBrahamMainMenuWidget::UOneBrahamMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BackdropOpacity = 1.f;
}

UOneBrahamGameInstance* UOneBrahamMainMenuWidget::GetOBGameInstance() const
{
	return GetGameInstance<UOneBrahamGameInstance>();
}

void UOneBrahamMainMenuWidget::NativeConstruct()
{
	if (UOneBrahamGameInstance* GameInstance = GetOBGameInstance())
	{
		GameInstance->OnServerListUpdated.AddUniqueDynamic(this, &UOneBrahamMainMenuWidget::HandleServerList);
		GameInstance->OnMatchmakingStatus.AddUniqueDynamic(this, &UOneBrahamMainMenuWidget::HandleMatchmakingStatus);
		GameInstance->OnSessionJoined.AddUniqueDynamic(this, &UOneBrahamMainMenuWidget::HandleSessionJoined);
		GameInstance->HideLoadingScreen();
	}
	Super::NativeConstruct();
}

void UOneBrahamMainMenuWidget::NativeDestruct()
{
	if (UOneBrahamGameInstance* GameInstance = GetOBGameInstance())
	{
		GameInstance->OnServerListUpdated.RemoveDynamic(this, &UOneBrahamMainMenuWidget::HandleServerList);
		GameInstance->OnMatchmakingStatus.RemoveDynamic(this, &UOneBrahamMainMenuWidget::HandleMatchmakingStatus);
		GameInstance->OnSessionJoined.RemoveDynamic(this, &UOneBrahamMainMenuWidget::HandleSessionJoined);
	}
	Super::NativeDestruct();
}

void UOneBrahamMainMenuWidget::GoBack()
{
	// The front end has nowhere to close to: back goes home, and home stays.
	if (Page != EOneBrahamMainMenuPage::Home)
	{
		OpenPage(EOneBrahamMainMenuPage::Home);
	}
}

void UOneBrahamMainMenuWidget::OpenPage(EOneBrahamMainMenuPage NewPage)
{
	Page = NewPage;
	StatusText = FText::GetEmpty();
	ShowPage();
}

FText UOneBrahamMainMenuWidget::GetPageTitle() const
{
	switch (Page)
	{
	case EOneBrahamMainMenuPage::Play: return NSLOCTEXT("OneBraham", "Menu_Play", "Play");
	case EOneBrahamMainMenuPage::Loadout: return NSLOCTEXT("OneBraham", "Menu_Loadout", "Loadout");
	case EOneBrahamMainMenuPage::Servers: return NSLOCTEXT("OneBraham", "Menu_Servers", "Server browser");
	case EOneBrahamMainMenuPage::Wardrobe: return NSLOCTEXT("OneBraham", "Menu_Wardrobe", "Wardrobe");
	case EOneBrahamMainMenuPage::Achievements: return NSLOCTEXT("OneBraham", "Menu_Achievements", "Achievements");
	default: return FText::FromString(ONE_BRAHAM_DISPLAY_NAME);
	}
}

FText UOneBrahamMainMenuWidget::GetFooterText() const
{
	if (Page == EOneBrahamMainMenuPage::Wardrobe)
	{
		return NSLOCTEXT("OneBraham", "Menu_WardrobeFooter", "Cosmetics are cosmetic. Nothing here changes how you play.");
	}
	return Page == EOneBrahamMainMenuPage::Home
		? FText::FromString(FString::Printf(TEXT("%s  -  protocol %d"), ONE_BRAHAM_DISPLAY_NAME, ONE_BRAHAM_NET_PROTOCOL_VERSION))
		: Super::GetFooterText();
}

void UOneBrahamMainMenuWidget::BuildPage()
{
	switch (Page)
	{
	case EOneBrahamMainMenuPage::Play: BuildPlay(); break;
	case EOneBrahamMainMenuPage::Loadout: BuildLoadout(); break;
	case EOneBrahamMainMenuPage::Servers: BuildServers(); break;
	case EOneBrahamMainMenuPage::Wardrobe: BuildWardrobe(); break;
	case EOneBrahamMainMenuPage::Achievements: BuildAchievements(); break;
	default: BuildHome(); break;
	}

	if (Page != EOneBrahamMainMenuPage::Home)
	{
		AddButton(NSLOCTEXT("OneBraham", "Menu_Back", "Back"), [this]() { OpenPage(EOneBrahamMainMenuPage::Home); });
	}
}

void UOneBrahamMainMenuWidget::AddStatusLine()
{
	if (!StatusText.IsEmpty())
	{
		AddText(StatusText, EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
	}
}

// ==========================================================================
//  Home
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildHome()
{
	const UOneBrahamGameInstance* GameInstance = GetOBGameInstance();
	const UOneBrahamSaveSettings* Profile = GameInstance ? GameInstance->GetProfile() : nullptr;
	if (Profile != nullptr && !Profile->PlayerName.IsEmpty())
	{
		AddText(FText::Format(NSLOCTEXT("OneBraham", "Menu_SignedIn", "Signed in as {0}  -  {1} matches, {2} extractions"),
			FText::FromString(Profile->PlayerName), FText::AsNumber(Profile->TotalMatchesPlayed), FText::AsNumber(Profile->TotalExtractions)),
			EOneBrahamTextSize::Small, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()));
	}

	AddButton(NSLOCTEXT("OneBraham", "Menu_Play", "Play"), [this]() { OpenPage(EOneBrahamMainMenuPage::Play); });
	AddButton(NSLOCTEXT("OneBraham", "Menu_Loadout", "Loadout"), [this]() { OpenPage(EOneBrahamMainMenuPage::Loadout); });
	AddButton(NSLOCTEXT("OneBraham", "Menu_Servers", "Server browser"), [this]()
	{
		OpenPage(EOneBrahamMainMenuPage::Servers);
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			bSearchingServers = true;
			Instance->RefreshServerBrowser(bBrowseLAN);
			ShowPage();
		}
	});
	AddButton(NSLOCTEXT("OneBraham", "Menu_Wardrobe", "Wardrobe"), [this]() { OpenPage(EOneBrahamMainMenuPage::Wardrobe); });
	AddButton(NSLOCTEXT("OneBraham", "Menu_Achievements", "Achievements"), [this]() { OpenPage(EOneBrahamMainMenuPage::Achievements); });
	AddButton(NSLOCTEXT("OneBraham", "Menu_Settings", "Settings"), [this]() { OpenSettings(); });
	AddButton(NSLOCTEXT("OneBraham", "Menu_Friends", "Friends"), [this]()
	{
		if (!UOneBrahamUILibrary::OpenSteamOverlay(this, 0))
		{
			StatusText = NSLOCTEXT("OneBraham", "Menu_NoOverlay", "The Steam overlay is not available.");
			ShowPage();
		}
	});
	AddButton(NSLOCTEXT("OneBraham", "Menu_Quit", "Quit"), [this]()
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
	});

	AddStatusLine();
}

// ==========================================================================
//  Play
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildPlay()
{
	UOneBrahamGameInstance* GameInstance = GetOBGameInstance();
	if (GameInstance == nullptr)
	{
		return;
	}

	if (GameInstance->IsMatchmaking())
	{
		AddText(StatusText.IsEmpty() ? NSLOCTEXT("OneBraham", "Menu_Searching", "Searching for a match...") : StatusText,
			EOneBrahamTextSize::Medium, UOneBrahamUIStyle::SafetyYellow());
		AddButton(NSLOCTEXT("OneBraham", "Menu_Cancel", "Cancel search"), [this]()
		{
			if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
			{
				Instance->CancelMatchmaking();
			}
			OpenPage(EOneBrahamMainMenuPage::Play);
		});
		return;
	}

	AddHeading(NSLOCTEXT("OneBraham", "Menu_FindMatch", "Find a match"));
	AddText(NSLOCTEXT("OneBraham", "Menu_AscentBrief", "ASCENT. Twelve people, three squads, fifteen floors, one helicopter."));
	AddButton(NSLOCTEXT("OneBraham", "Menu_Ascent", "Ascent"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			Instance->StartMatchmaking(EOneBrahamMatchMode::Ascent);
		}
		ShowPage();
	});
	AddText(NSLOCTEXT("OneBraham", "Menu_FireDrillBrief", "FIRE DRILL. Internal Staff against External Contractors, with respawns."));
	AddButton(NSLOCTEXT("OneBraham", "Menu_FireDrill", "Fire Drill"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			Instance->StartMatchmaking(EOneBrahamMatchMode::FireDrill);
		}
		ShowPage();
	});

	AddHeading(NSLOCTEXT("OneBraham", "Menu_Private", "Private and practice"));
	AddButton(NSLOCTEXT("OneBraham", "Menu_HostAscent", "Host a private Ascent"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			const UOneBrahamSaveSettings* Profile = Instance->GetProfile();
			const FString Name = Profile && !Profile->PlayerName.IsEmpty() ? FString::Printf(TEXT("%s's One Braham"), *Profile->PlayerName) : FString();
			Instance->HostListenServer(EOneBrahamMatchMode::Ascent, 12, false, Name);
		}
	});
	AddButton(NSLOCTEXT("OneBraham", "Menu_HostLAN", "Host on the local network"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			Instance->HostListenServer(EOneBrahamMatchMode::Ascent, 12, true, FString());
		}
	});
	AddButton(NSLOCTEXT("OneBraham", "Menu_Practice", "Practice against bots"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			Instance->StartPracticeMatch(EOneBrahamMatchMode::Ascent, 11);
		}
	});
	AddButton(NSLOCTEXT("OneBraham", "Menu_NightShift", "Night Shift (beta, with bots)"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			Instance->StartPracticeMatch(EOneBrahamMatchMode::NightShift, 3);
		}
	});

	AddStatusLine();
}

// ==========================================================================
//  Loadout
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildLoadout()
{
	UOneBrahamGameInstance* GameInstance = GetOBGameInstance();
	UOneBrahamSaveSettings* Profile = GameInstance ? GameInstance->GetProfile() : nullptr;
	if (Profile == nullptr)
	{
		return;
	}
	if (!Profile->Loadouts.IsValidIndex(Profile->SelectedLoadoutIndex))
	{
		Profile->Loadouts.SetNum(FMath::Max(1, Profile->SelectedLoadoutIndex + 1));
	}
	const int32 LoadoutIndex = Profile->SelectedLoadoutIndex;

	AddText(NSLOCTEXT("OneBraham", "Menu_LoadoutBrief",
		"Who you are is cosmetic. What you carry is not. The server issues what it recognises and the standard kit for anything it does not."));

	// Who: the six archetypes, from the cast registry.
	if (const UOneBrahamCharacterDataAsset* CastRegistry = UOneBrahamCharacterDataAsset::LoadRegistry())
	{
		TArray<FText> Labels;
		TArray<FName> Ids;
		int32 Current = 0;
		for (const FOneBrahamArchetypeDefinition& Entry : CastRegistry->Archetypes)
		{
			const FName Id = ArchetypeIdOf(Entry.ArchetypeTag);
			if (Id == Profile->Loadouts[LoadoutIndex].ArchetypeId)
			{
				Current = Ids.Num();
			}
			Ids.Add(Id);
			Labels.Add(Entry.DisplayName.IsEmpty() ? FText::FromName(Id) : Entry.DisplayName);
		}
		if (Ids.Num() > 0)
		{
			AddCycle(NSLOCTEXT("OneBraham", "Menu_Archetype", "Archetype"), Labels, Current, [GameInstance, Profile, LoadoutIndex, Ids](int32 Index)
			{
				Profile->Loadouts[LoadoutIndex].ArchetypeId = Ids[Index];
				GameInstance->SaveProfile();
			});
		}
	}

	// What: primary and sidearm, from the weapon registry.
	const UOneBrahamWeaponDataAsset* Arsenal = UOneBrahamGameSettings::Get().WeaponRegistry.IsNull()
		? nullptr : UOneBrahamGameSettings::Get().WeaponRegistry.LoadSynchronous();
	if (Arsenal == nullptr)
	{
		AddText(NSLOCTEXT("OneBraham", "Menu_NoWeapons", "The weapon list comes from the weapon build (Scripts\\BuildContent.bat --only weapons)."),
			EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
	}
	else
	{
		const auto AddWeaponCycle = [this, Arsenal, GameInstance, Profile, LoadoutIndex](const FText& Label, EOneBrahamWeaponSlot WeaponSlot, bool bPrimary)
		{
			TArray<FText> Labels;
			TArray<FName> Ids;
			const FName Current = bPrimary ? Profile->Loadouts[LoadoutIndex].PrimaryWeaponId : Profile->Loadouts[LoadoutIndex].SidearmWeaponId;
			int32 CurrentIndex = 0;
			for (const UOneBrahamWeaponDefinition* Weapon : Arsenal->Weapons)
			{
				if (Weapon == nullptr || Weapon->DefaultSlot != WeaponSlot || Weapon->Category == EOneBrahamWeaponCategory::Utility)
				{
					continue;
				}
				if (Weapon->WeaponId == Current)
				{
					CurrentIndex = Ids.Num();
				}
				Ids.Add(Weapon->WeaponId);
				Labels.Add(Weapon->DisplayName.IsEmpty() ? FText::FromName(Weapon->WeaponId) : Weapon->DisplayName);
			}
			if (Ids.Num() == 0)
			{
				return;
			}
			AddCycle(Label, Labels, CurrentIndex, [GameInstance, Profile, LoadoutIndex, Ids, bPrimary](int32 Index)
			{
				FOneBrahamSavedLoadout& Loadout = Profile->Loadouts[LoadoutIndex];
				(bPrimary ? Loadout.PrimaryWeaponId : Loadout.SidearmWeaponId) = Ids[Index];
				GameInstance->SaveProfile();
			});
		};
		AddWeaponCycle(NSLOCTEXT("OneBraham", "Menu_Primary", "Primary"), EOneBrahamWeaponSlot::Primary, true);
		AddWeaponCycle(NSLOCTEXT("OneBraham", "Menu_Sidearm", "Sidearm"), EOneBrahamWeaponSlot::Sidearm, false);
	}
}

// ==========================================================================
//  Server browser
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildServers()
{
	UHorizontalBox* Controls = AddRow();
	AddButton(NSLOCTEXT("OneBraham", "Menu_Refresh", "Refresh"), [this]()
	{
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			bSearchingServers = true;
			Instance->RefreshServerBrowser(bBrowseLAN);
			ShowPage();
		}
	}, Controls, !bSearchingServers);
	AddButton(bBrowseLAN ? NSLOCTEXT("OneBraham", "Menu_ShowOnline", "Show online") : NSLOCTEXT("OneBraham", "Menu_ShowLAN", "Show LAN"), [this]()
	{
		bBrowseLAN = !bBrowseLAN;
		if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
		{
			bSearchingServers = true;
			Instance->RefreshServerBrowser(bBrowseLAN);
		}
		ShowPage();
	}, Controls);

	if (bSearchingServers)
	{
		AddText(NSLOCTEXT("OneBraham", "Menu_SearchingServers", "Searching..."), EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
	}
	else if (ServerList.Num() == 0)
	{
		AddText(NSLOCTEXT("OneBraham", "Menu_NoServers", "No servers found. Refresh, host your own, or connect by address."));
	}

	for (const FOneBrahamServerInfo& Server : ServerList)
	{
		const UEnum* ModeEnum = StaticEnum<EOneBrahamMatchMode>();
		const FText Line = FText::Format(NSLOCTEXT("OneBraham", "Menu_ServerRow", "{0}  -  {1}  -  {2}/{3}  -  {4} ms{5}"),
			FText::FromString(Server.ServerName),
			ModeEnum ? ModeEnum->GetDisplayNameTextByValue(static_cast<int64>(Server.Mode)) : FText::GetEmpty(),
			FText::AsNumber(Server.CurrentPlayers), FText::AsNumber(Server.MaxPlayers), FText::AsNumber(Server.PingMs),
			Server.bDedicated ? NSLOCTEXT("OneBraham", "Menu_Dedicated", "  -  dedicated") : FText::GetEmpty());

		// A full server is listed and greyed: it is information, not a door.
		const int32 ResultIndex = Server.SearchResultIndex;
		const bool bJoinable = Server.CurrentPlayers < Server.MaxPlayers;
		UButton* Row = AddButton(Line, [this, ResultIndex]()
		{
			if (UOneBrahamGameInstance* Instance = GetOBGameInstance())
			{
				StatusText = NSLOCTEXT("OneBraham", "Menu_Joining", "Joining...");
				ShowPage();
				Instance->JoinServerByIndex(ResultIndex);
			}
		}, nullptr, bJoinable);
		if (Row != nullptr && Row->GetChildAt(0) != nullptr)
		{
			if (UTextBlock* RowText = Cast<UTextBlock>(Row->GetChildAt(0)))
			{
				RowText->SetJustification(ETextJustify::Left);
				RowText->SetColorAndOpacity(FSlateColor(bJoinable ? PingColour(Server.PingMs) : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey())));
			}
		}
	}

	AddHeading(NSLOCTEXT("OneBraham", "Menu_Direct", "Connect by address"));
	UEditableTextBox* Address = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	const UOneBrahamGameInstance* GameInstance = GetOBGameInstance();
	const UOneBrahamSaveSettings* Profile = GameInstance ? GameInstance->GetProfile() : nullptr;
	Address->SetHintText(NSLOCTEXT("OneBraham", "Menu_AddressHint", "host:port"));
	if (Profile != nullptr && !Profile->LastUsedServerAddress.IsEmpty())
	{
		Address->SetText(FText::FromString(FString::Printf(TEXT("%s:%d"), *Profile->LastUsedServerAddress, Profile->LastUsedServerPort)));
	}
	UHorizontalBox* DirectRow = AddRow();
	if (UHorizontalBoxSlot* AddressSlot = DirectRow->AddChildToHorizontalBox(Address))
	{
		AddressSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AddressSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	}
	TWeakObjectPtr<UEditableTextBox> WeakAddress = Address;
	AddButton(NSLOCTEXT("OneBraham", "Menu_Connect", "Connect"), [this, WeakAddress]()
	{
		UOneBrahamGameInstance* Instance = GetOBGameInstance();
		const UEditableTextBox* Box = WeakAddress.Get();
		const FString Text = Box ? Box->GetText().ToString().TrimStartAndEnd() : FString();
		if (Instance == nullptr || Text.IsEmpty())
		{
			return;
		}
		FString Host = Text;
		FString PortText;
		int32 Port = 7777;
		if (Text.Split(TEXT(":"), &Host, &PortText))
		{
			Port = FMath::Clamp(FCString::Atoi(*PortText), 1, 65535);
		}
		Instance->JoinServerByAddress(Host, Port);
	}, DirectRow);

	AddStatusLine();
}

void UOneBrahamMainMenuWidget::HandleServerList(const TArray<FOneBrahamServerInfo>& Servers)
{
	ServerList = Servers;
	bSearchingServers = false;
	if (Page == EOneBrahamMainMenuPage::Servers)
	{
		ShowPage();
	}
}

void UOneBrahamMainMenuWidget::HandleMatchmakingStatus(const FText& Status, float Progress)
{
	StatusText = Status;
	if (Page == EOneBrahamMainMenuPage::Play || Page == EOneBrahamMainMenuPage::Home)
	{
		ShowPage();
	}
}

void UOneBrahamMainMenuWidget::HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL)
{
	if (!bWasSuccessful)
	{
		StatusText = NSLOCTEXT("OneBraham", "Menu_JoinFailed", "Could not join that match. It may have filled or ended.");
		ShowPage();
	}
}

// ==========================================================================
//  Wardrobe and achievements
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildWardrobe()
{
	UOneBrahamGameInstance* GameInstance = GetOBGameInstance();
	UOneBrahamSaveSettings* Profile = GameInstance ? GameInstance->GetProfile() : nullptr;
	const UOneBrahamCharacterDataAsset* CastRegistry = UOneBrahamCharacterDataAsset::LoadRegistry();
	if (Profile == nullptr || CastRegistry == nullptr || CastRegistry->CosmeticSlots.Num() == 0)
	{
		AddText(NSLOCTEXT("OneBraham", "Menu_NoWardrobe", "The wardrobe comes from the character build (Scripts\\BuildContent.bat --only characters)."),
			EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
		return;
	}

	const UOneBrahamCosmeticDataAsset* Catalogue = UOneBrahamCosmeticDataAsset::LoadCatalogue();
	for (const FOneBrahamCosmeticSlot& WardrobeSlot : CastRegistry->CosmeticSlots)
	{
		TArray<FName> Ids;
		WardrobeSlot.Options.GetKeys(Ids);
		Ids.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		if (Ids.Num() == 0)
		{
			continue;
		}

		const FName Worn = Profile->EquippedCosmetics.FindRef(WardrobeSlot.SlotName);
		int32 Current = FMath::Max(0, Ids.IndexOfByKey(Worn));
		TArray<FText> Labels;
		for (const FName& Id : Ids)
		{
			Labels.Add(Catalogue->IsUnlockedFor(Profile, Id)
				? CosmeticLabel(Id)
				: FText::Format(NSLOCTEXT("OneBraham", "Menu_LockedItem", "{0} (locked)"), CosmeticLabel(Id)));
		}

		const FName SlotName = WardrobeSlot.SlotName;
		AddCycle(WardrobeSlot.DisplayName.IsEmpty() ? FText::FromName(SlotName) : WardrobeSlot.DisplayName, Labels, Current,
			[this, GameInstance, Profile, Catalogue, Ids, SlotName](int32 Index)
			{
				const FName Choice = Ids[Index];
				if (!Catalogue->IsUnlockedFor(Profile, Choice))
				{
					// Shown so the player knows what it takes; not worn.
					StatusText = UOneBrahamUILibrary::GetCosmeticLockText(this, Choice);
					return;
				}
				StatusText = FText::GetEmpty();
				Profile->EquippedCosmetics.Add(SlotName, Choice);
				GameInstance->SaveProfile();
			});
	}

	AddStatusLine();
}

void UOneBrahamMainMenuWidget::BuildAchievements()
{
	int32 Earned = 0;
	const TArray<FOneBrahamAchievementView> Achievements = UOneBrahamUILibrary::GetAchievements(this);
	for (const FOneBrahamAchievementView& View : Achievements)
	{
		Earned += View.bUnlocked ? 1 : 0;
	}
	AddText(FText::Format(NSLOCTEXT("OneBraham", "Menu_AchievementCount", "{0} of {1} earned."),
		FText::AsNumber(Earned), FText::AsNumber(Achievements.Num())), EOneBrahamTextSize::Small, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()));

	for (const FOneBrahamAchievementView& View : Achievements)
	{
		AddText(UOneBrahamUIStyle::HeaderLabel(View.Title), EOneBrahamTextSize::Medium,
			View.bUnlocked ? UOneBrahamUIStyle::SuccessGreen() : UOneBrahamUIStyle::LightGrey());
		AddText(View.Description, EOneBrahamTextSize::Small, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()));
	}
}

// ==========================================================================
//  Settings
// ==========================================================================
void UOneBrahamMainMenuWidget::OpenSettings()
{
	if (SettingsMenu == nullptr)
	{
		SettingsMenu = CreateWidget<UOneBrahamMenuWidget>(GetOwningPlayer(), UOneBrahamSettingsWidget::StaticClass());
		if (SettingsMenu != nullptr)
		{
			SettingsMenu->OnMenuClosed.AddUniqueDynamic(this, &UOneBrahamMainMenuWidget::HandleSettingsClosed);
		}
	}
	if (SettingsMenu != nullptr && !SettingsMenu->IsInViewport())
	{
		SettingsMenu->AddToViewport(10);
	}
}

void UOneBrahamMainMenuWidget::HandleSettingsClosed()
{
	ShowPage();	// focus back on the menu
}
