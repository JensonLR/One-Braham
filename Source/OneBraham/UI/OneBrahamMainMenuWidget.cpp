// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMainMenuWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "UI/OneBrahamSettingsWidget.h"

namespace OneBrahamMainMenuLayout
{
	constexpr float ColumnWidth = 440.f;
	constexpr float PageWidth = 780.f;
	constexpr float SettingsWidth = 1020.f;
	constexpr float Gap = 40.f;

	// The JOIN list's columns. They line up because every cell in a column
	// is the same width (UOneBrahamMenuWidget::MakeCell), not because of a table.
	constexpr float NameWidth = 340.f;
	constexpr float ModeWidth = 150.f;
	constexpr float PlayersWidth = 110.f;
	constexpr float PingWidth = 90.f;

	/** Seconds between searches while ob.menu.join waits for its row. */
	constexpr float AutoJoinRetrySeconds = 2.f;
}

UOneBrahamMainMenuWidget::UOneBrahamMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ==========================================================================
//  Pure pieces
// ==========================================================================
bool UOneBrahamMainMenuWidget::ParsePage(const FString& Text, EPage& OutPage)
{
	static const TPair<const TCHAR*, EPage> Names[] = {
		{ TEXT("home"), EPage::Home },
		{ TEXT("play"), EPage::Play },
		{ TEXT("host"), EPage::Host },
		{ TEXT("join"), EPage::Join },
		{ TEXT("settings"), EPage::Settings },
		{ TEXT("credits"), EPage::Credits },
	};

	const FString Key = Text.TrimStartAndEnd();
	for (const TPair<const TCHAR*, EPage>& Name : Names)
	{
		if (Key.Equals(Name.Key, ESearchCase::IgnoreCase))
		{
			OutPage = Name.Value;
			return true;
		}
	}
	return false;
}

FText UOneBrahamMainMenuWidget::ModeLabel(EOneBrahamMatchMode Mode)
{
	// Not UEnum display names: those are editor metadata, and a cooked build
	// has none.
	switch (Mode)
	{
	case EOneBrahamMatchMode::FireDrill:	return NSLOCTEXT("OneBraham", "Menu_ModeFireDrill", "FIRE DRILL");
	case EOneBrahamMatchMode::NightShift:	return NSLOCTEXT("OneBraham", "Menu_ModeNightShift", "NIGHT SHIFT");
	default:								return NSLOCTEXT("OneBraham", "Menu_ModeAscent", "ASCENT");
	}
}

FText UOneBrahamMainMenuWidget::PlayersLabel(const FOneBrahamServerInfo& Info)
{
	return FText::Format(NSLOCTEXT("OneBraham", "Menu_Players", "{0} / {1}"),
		FText::AsNumber(FMath::Max(0, Info.CurrentPlayers)), FText::AsNumber(FMath::Max(0, Info.MaxPlayers)));
}

FText UOneBrahamMainMenuWidget::PingLabel(const FOneBrahamServerInfo& Info)
{
	// MAX_QUERY_PING (9999) is "did not answer the ping", not a number to print.
	if (Info.PingMs <= 0 || Info.PingMs >= 9999)
	{
		return NSLOCTEXT("OneBraham", "Menu_PingUnknown", "-");
	}
	return FText::Format(NSLOCTEXT("OneBraham", "Menu_Ping", "{0} ms"), FText::AsNumber(Info.PingMs));
}

bool UOneBrahamMainMenuWidget::IsFull(const FOneBrahamServerInfo& Info)
{
	return Info.MaxPlayers > 0 && Info.CurrentPlayers >= Info.MaxPlayers;
}

const TArray<FOneBrahamCreditBlock>& UOneBrahamMainMenuWidget::Credits()
{
	// From Docs/LICENCES.md. Every entry there that asks for a credit is here,
	// and build_frontend.check_credits fails the build when one is not. The
	// OpenStreetMap notice is the one a licence requires, in its own words,
	// so it goes first.
	static const TArray<FOneBrahamCreditBlock> Blocks = {
		{
			NSLOCTEXT("OneBraham", "Credits_MapHead", "MAP DATA"),
			NSLOCTEXT("OneBraham", "Credits_MapBody",
				"Map data \u00A9 OpenStreetMap contributors, available under the Open Database Licence (ODbL 1.0): "
				"openstreetmap.org/copyright\n"
				"The streets, buildings, parks and trees of Aldgate East around the tower are derived from it.")
		},
		{
			NSLOCTEXT("OneBraham", "Credits_SkyHead", "SKIES"),
			NSLOCTEXT("OneBraham", "Credits_SkyBody",
				"Poly Haven, CC0: kloofendal_48d_partly_cloudy_puresky (the day), belfast_sunset_puresky (the dusk).")
		},
		{
			NSLOCTEXT("OneBraham", "Credits_TexHead", "SURFACES"),
			NSLOCTEXT("OneBraham", "Credits_TexBody",
				"Poly Haven, CC0: terrazzo_tiles, concrete_floor_02, oak_wood_planks, white_plaster_02, "
				"concrete_pavement, asphalt_02, rubber_tiles.\n"
				"ambientCG, CC0: Carpet001, Metal011, Planks037A, Fabric030, Fabric031, Fingerprints002, "
				"SurfaceImperfections015, Smear001.")
		},
		{
			NSLOCTEXT("OneBraham", "Credits_StandInHead", "STAND-INS"),
			NSLOCTEXT("OneBraham", "Credits_StandInBody",
				"The UE5 mannequins, Manny and Quinn, their animation, and the template rifle and pistol: Epic Games, "
				"under the Unreal Engine EULA. They hold the cast's and the armoury's places until those are made.")
		},
		{
			NSLOCTEXT("OneBraham", "Credits_EngineHead", "ENGINE AND SERVICES"),
			NSLOCTEXT("OneBraham", "Credits_EngineBody",
				"Made with Unreal Engine. Unreal Engine \u00A9 Epic Games, Inc.\n"
				"Steamworks \u00A9 Valve Corporation.")
		},
		{
			NSLOCTEXT("OneBraham", "Credits_BuildingHead", "THE BUILDING"),
			NSLOCTEXT("OneBraham", "Credits_BuildingBody",
				"One Braham is inspired by an address in Aldgate, not drawn from its plans. "
				"No real company, tenant or colleague works in it.")
		},
		{
			NSLOCTEXT("OneBraham", "Credits_GameHead", "ON BREAK"),
			NSLOCTEXT("OneBraham", "Credits_GameBody", "\u00A9 2026 One Braham Interactive. All rights reserved.")
		},
	};

	return Blocks;
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildLayout()
{
	using namespace OneBrahamMainMenuLayout;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MainMenuRoot"));
	WidgetTree->RootWidget = Root;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UOverlaySlot* RowSlot = Root->AddChildToOverlay(Row);
	RowSlot->SetHorizontalAlignment(HAlign_Fill);
	RowSlot->SetVerticalAlignment(VAlign_Fill);

	// --- the directory column -------------------------------------------------
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	Column->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Menu_Kicker", "ONE BRAHAM  /  ALDGATE  /  LONDON E1"),
		EOneBrahamTextSize::Micro, InkMuted()));
	Column->AddChildToVerticalBox(MakeLabel(FText::FromString(ONE_BRAHAM_DISPLAY_NAME),
		EOneBrahamTextSize::Display, Ink(), true))->SetPadding(FMargin(0.f, 6.f, 0.f, 4.f));

	UTextBlock* Strapline = MakeLabel(NSLOCTEXT("OneBraham", "Menu_Strapline", "Eighteen floors. Twelve colleagues. One way out, from the roof."),
		EOneBrahamTextSize::Small, InkMuted());
	Strapline->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(Strapline)->SetPadding(FMargin(0.f, 0.f, 0.f, 36.f));

	auto AddSign = [this, Column](const FText& Label, TFunction<void()> OnClicked) -> UButton*
	{
		UButton* Button = MakeButton(Label, MoveTemp(OnClicked), EOneBrahamTextSize::Medium, true, nullptr, FMargin(18.f, 12.f));
		UVerticalBoxSlot* SignSlot = Column->AddChildToVerticalBox(Button);
		SignSlot->SetHorizontalAlignment(HAlign_Fill);
		SignSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		return Button;
	};

	Sign_Play = AddSign(NSLOCTEXT("OneBraham", "Menu_Play", "PLAY"), [this]() { ShowPage(EPage::Play); });
	Sign_Host = AddSign(NSLOCTEXT("OneBraham", "Menu_Host", "HOST"), [this]() { ShowPage(EPage::Host); });
	Sign_Join = AddSign(NSLOCTEXT("OneBraham", "Menu_Join", "JOIN"), [this]() { ShowPage(EPage::Join); });
	Sign_Settings = AddSign(NSLOCTEXT("OneBraham", "Menu_Settings", "SETTINGS"), [this]() { ShowPage(EPage::Settings); });
	Sign_Credits = AddSign(NSLOCTEXT("OneBraham", "Menu_Credits", "CREDITS & LICENCES"), [this]() { ShowPage(EPage::Credits); });

	// A gap before the way out, as on the pause menu.
	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 14.f, 0.f, 24.f));
	Sign_Quit = AddSign(NSLOCTEXT("OneBraham", "Menu_Quit", "QUIT"), [this]() { Quit(); });

	UVerticalBoxSlot* Filler = Column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));
	Filler->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Text_Notice = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), true);
	Text_Notice->SetAutoWrapText(true);
	Text_Notice->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(Text_Notice)->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));

	Column->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Menu_Footer", "ENTER  SELECT          ESC  BACK"),
		EOneBrahamTextSize::Micro, InkFaint()));
	Text_Build = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Micro, InkFaint());
	Column->AddChildToVerticalBox(Text_Build)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	UBorder* ColumnPanel = MakePanel(Column, 0.94f, FMargin(44.f, 56.f, 44.f, 36.f));
	ColumnPanel->SetBrush(UOneBrahamUIStyle::PanelBrush(0.94f, 0.f));

	Row->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)))->SetVerticalAlignment(VAlign_Fill);
	Row->AddChildToHorizontalBox(MakeCell(ColumnPanel, ColumnWidth))->SetVerticalAlignment(VAlign_Fill);

	// --- the page beside it ------------------------------------------------------
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("MainMenuPages"));
	Pages->AddChild(BuildHomePage());
	Pages->AddChild(BuildPlayPage());
	Pages->AddChild(BuildHostPage());
	Pages->AddChild(BuildJoinPage());
	Pages->AddChild(BuildSettingsPage());
	Pages->AddChild(BuildCreditsPage());

	UHorizontalBox* Framed = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Framed->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)))->SetVerticalAlignment(VAlign_Fill);
	Framed->AddChildToHorizontalBox(MakePanel(Pages, 0.95f, FMargin(36.f, 32.f)))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PagePanel = Framed;

	UHorizontalBoxSlot* PageSlot = Row->AddChildToHorizontalBox(Framed);
	PageSlot->SetPadding(FMargin(Gap, 0.f, Gap, 0.f));
	PageSlot->SetVerticalAlignment(VAlign_Center);
	PageSlot->SetHorizontalAlignment(HAlign_Left);
	PageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// --- the attribution the backdrop needs --------------------------------------
	// The streets and buildings behind the menu are OpenStreetMap's: a produced
	// work, which the ODbL asks to carry the credit where it is seen.
	UTextBlock* Attribution = MakeLabel(NSLOCTEXT("OneBraham", "Menu_OSM", "Map data \u00A9 OpenStreetMap contributors"),
		EOneBrahamTextSize::Micro, InkMuted(), false, ETextJustify::Right);
	UOverlaySlot* AttributionSlot = Root->AddChildToOverlay(Attribution);
	AttributionSlot->SetHorizontalAlignment(HAlign_Right);
	AttributionSlot->SetVerticalAlignment(VAlign_Bottom);
	AttributionSlot->SetPadding(FMargin(0.f, 0.f, 28.f, 20.f));

	ShowPage(EPage::Home);
}

void UOneBrahamMainMenuWidget::AddPageHeading(UVerticalBox* InPage, const FText& Title, const FText& Body, UTextBlock** OutBody)
{
	InPage->AddChildToVerticalBox(MakeLabel(Title, EOneBrahamTextSize::Large, Ink(), true));
	UTextBlock* BodyText = MakeLabel(Body, EOneBrahamTextSize::Small, InkMuted());
	BodyText->SetAutoWrapText(true);
	InPage->AddChildToVerticalBox(BodyText)->SetPadding(FMargin(0.f, 8.f, 0.f, 26.f));

	if (OutBody != nullptr)
	{
		*OutBody = BodyText;
	}
}

UWidget* UOneBrahamMainMenuWidget::BuildHomePage()
{
	// Nothing: the building is the picture.
	return WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
}

UWidget* UOneBrahamMainMenuWidget::BuildPlayPage()
{
	using namespace OneBrahamMainMenuLayout;

	UVerticalBox* PageBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	AddPageHeading(PageBox, NSLOCTEXT("OneBraham", "Play_Title", "PLAY"),
		NSLOCTEXT("OneBraham", "Play_Body", "On your own. Every empty desk is taken by a bot, and the bots climb too."));

	auto AddMode = [this, PageBox](EOneBrahamMatchMode Mode, const FText& Description) -> UButton*
	{
		UButton* Button = MakeButton(ModeLabel(Mode), [this, Mode]() { PlaySolo(Mode); }, EOneBrahamTextSize::Medium,
			true, nullptr, FMargin(18.f, 14.f));
		UVerticalBoxSlot* ButtonSlot = PageBox->AddChildToVerticalBox(Button);
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);

		UTextBlock* Text = MakeLabel(Description, EOneBrahamTextSize::Small, InkMuted());
		Text->SetAutoWrapText(true);
		PageBox->AddChildToVerticalBox(Text)->SetPadding(FMargin(2.f, 8.f, 0.f, 22.f));
		return Button;
	};

	Button_PlayAscent = AddMode(EOneBrahamMatchMode::Ascent,
		NSLOCTEXT("OneBraham", "Play_Ascent", "Three squads of four, ground floor to the Skydeck. The building seals behind you. No respawns. One squad walks out."));
	AddMode(EOneBrahamMatchMode::FireDrill,
		NSLOCTEXT("OneBraham", "Play_FireDrill", "Six against six on floors 1, 9 and 15. Hold the points, come back when you fall. First to 200, or ten minutes."));

	Text_PlayStatus = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink());
	Text_PlayStatus->SetAutoWrapText(true);
	PageBox->AddChildToVerticalBox(Text_PlayStatus);

	return MakeCell(PageBox, PageWidth);
}

UWidget* UOneBrahamMainMenuWidget::BuildHostPage()
{
	using namespace OneBrahamMainMenuLayout;

	UVerticalBox* PageBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	AddPageHeading(PageBox, NSLOCTEXT("OneBraham", "Host_Title", "HOST"),
		NSLOCTEXT("OneBraham", "Host_Body", "Open the building to colleagues. Bots hold the desks until people arrive."));

	auto AddChoice = [this, PageBox](const FText& Label, TFunction<void()> OnClicked, UTextBlock*& OutValue) -> UButton*
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Line->AddChildToHorizontalBox(MakeCell(MakeLabel(Label, EOneBrahamTextSize::Small, InkMuted(), true), 160.f))
			->SetVerticalAlignment(VAlign_Center);

		UTextBlock* Value = nullptr;
		UButton* Button = MakeButton(FText::GetEmpty(), MoveTemp(OnClicked), EOneBrahamTextSize::Medium, true, &Value,
			FMargin(18.f, 10.f));
		UHorizontalBoxSlot* ButtonSlot = Line->AddChildToHorizontalBox(Button);
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		PageBox->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		OutValue = Value;
		return Button;
	};

	UTextBlock* ModeValue = nullptr;
	Button_HostMode = AddChoice(NSLOCTEXT("OneBraham", "Host_Mode", "MODE"), [this]()
	{
		HostMode = HostMode == EOneBrahamMatchMode::Ascent ? EOneBrahamMatchMode::FireDrill : EOneBrahamMatchMode::Ascent;
		Text_HostMode->SetText(ModeLabel(HostMode));
	}, ModeValue);
	Text_HostMode = ModeValue;

	UTextBlock* NetworkValue = nullptr;
	Button_HostNetwork = AddChoice(NSLOCTEXT("OneBraham", "Host_Network", "NETWORK"), [this]()
	{
		const UOneBrahamSessionSubsystem* Sessions = GetSessions();
		if (Sessions != nullptr && !Sessions->IsLANOnly())
		{
			bHostLAN = !bHostLAN;
		}
		Text_HostNetwork->SetText(bHostLAN
			? NSLOCTEXT("OneBraham", "Host_LAN", "LAN")
			: NSLOCTEXT("OneBraham", "Host_Online", "ONLINE"));
	}, NetworkValue);
	Text_HostNetwork = NetworkValue;

	Text_HostNote = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Text_HostNote->SetAutoWrapText(true);
	PageBox->AddChildToVerticalBox(Text_HostNote)->SetPadding(FMargin(0.f, 2.f, 0.f, 26.f));

	Button_HostGo = MakeButton(NSLOCTEXT("OneBraham", "Host_Go", "OPEN THE FLOOR"), [this]() { Host(HostMode, bHostLAN); },
		EOneBrahamTextSize::Medium, true, nullptr, FMargin(18.f, 14.f));
	PageBox->AddChildToVerticalBox(Button_HostGo)->SetHorizontalAlignment(HAlign_Left);

	Text_HostStatus = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink());
	Text_HostStatus->SetAutoWrapText(true);
	PageBox->AddChildToVerticalBox(Text_HostStatus)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));

	return MakeCell(PageBox, PageWidth);
}

UWidget* UOneBrahamMainMenuWidget::BuildJoinPage()
{
	using namespace OneBrahamMainMenuLayout;

	UVerticalBox* PageBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UTextBlock* Body = nullptr;
	AddPageHeading(PageBox, NSLOCTEXT("OneBraham", "Join_Title", "JOIN"), FText::GetEmpty(), &Body);
	Text_JoinBody = Body;

	// Column headings, set in the same cells as the rows below them.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Header->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Join_ColName", "NAME"), EOneBrahamTextSize::Micro, InkMuted(), true), NameWidth));
	Header->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Join_ColMode", "MODE"), EOneBrahamTextSize::Micro, InkMuted(), true), ModeWidth));
	Header->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Join_ColPlayers", "PLAYERS"), EOneBrahamTextSize::Micro, InkMuted(), true, ETextJustify::Right), PlayersWidth));
	Header->AddChildToHorizontalBox(MakeCell(MakeLabel(NSLOCTEXT("OneBraham", "Join_ColPing", "PING"), EOneBrahamTextSize::Micro, InkMuted(), true, ETextJustify::Right), PingWidth));
	PageBox->AddChildToVerticalBox(Header)->SetPadding(FMargin(18.f, 0.f, 18.f, 6.f));
	PageBox->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	JoinRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	List->AddChild(JoinRows);
	USizeBox* ListBox = MakeCell(List, PageWidth, 300.f);
	PageBox->AddChildToVerticalBox(ListBox);

	Text_JoinStatus = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Text_JoinStatus->SetAutoWrapText(true);
	PageBox->AddChildToVerticalBox(Text_JoinStatus)->SetPadding(FMargin(0.f, 12.f, 0.f, 18.f));

	Button_JoinRefresh = MakeButton(NSLOCTEXT("OneBraham", "Join_Refresh", "SEARCH AGAIN"), [this]() { RefreshJoinList(); },
		EOneBrahamTextSize::Small);
	PageBox->AddChildToVerticalBox(Button_JoinRefresh)->SetHorizontalAlignment(HAlign_Left);

	return MakeCell(PageBox, PageWidth);
}

UWidget* UOneBrahamMainMenuWidget::BuildSettingsPage()
{
	using namespace OneBrahamMainMenuLayout;

	Settings = CreateWidget<UOneBrahamSettingsWidget>(this, UOneBrahamSettingsWidget::StaticClass());
	if (Settings == nullptr)
	{
		return WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	}

	Settings->OnBackRequested.BindWeakLambda(this, [this]() { ShowPage(EPage::Home); });
	return MakeCell(Settings, SettingsWidth);
}

UWidget* UOneBrahamMainMenuWidget::BuildCreditsPage()
{
	using namespace OneBrahamMainMenuLayout;

	UVerticalBox* PageBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	AddPageHeading(PageBox, NSLOCTEXT("OneBraham", "Credits_Title", "CREDITS & LICENCES"),
		NSLOCTEXT("OneBraham", "Credits_Body", "Everything in the building that somebody else made, and the terms it came on."));

	CreditsScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	for (const FOneBrahamCreditBlock& Block : Credits())
	{
		CreditsScroll->AddChild(MakeLabel(Block.Heading, EOneBrahamTextSize::Micro, InkMuted(), true));
		UTextBlock* Body = MakeLabel(Block.Body, EOneBrahamTextSize::Small, Ink());
		Body->SetAutoWrapText(true);
		if (UPanelSlot* BodySlot = CreditsScroll->AddChild(Body))
		{
			if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(BodySlot))
			{
				ScrollSlot->SetPadding(FMargin(0.f, 4.f, 12.f, 20.f));
			}
		}
	}
	PageBox->AddChildToVerticalBox(MakeCell(CreditsScroll, PageWidth, 440.f));

	Button_CreditsBack = MakeButton(NSLOCTEXT("OneBraham", "Credits_Back", "BACK"), [this]() { GoBack(); }, EOneBrahamTextSize::Small);
	UVerticalBoxSlot* BackSlot = PageBox->AddChildToVerticalBox(Button_CreditsBack);
	BackSlot->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
	BackSlot->SetHorizontalAlignment(HAlign_Left);

	return MakeCell(PageBox, PageWidth);
}

// ==========================================================================
//  Construct / destruct
// ==========================================================================
void UOneBrahamMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
	{
		if (!FoundHandle.IsValid())
		{
			FoundHandle = Sessions->OnSessionsFound.AddUObject(this, &UOneBrahamMainMenuWidget::HandleSessionsFound);
			JoinedHandle = Sessions->OnSessionJoined.AddUObject(this, &UOneBrahamMainMenuWidget::HandleSessionJoined);
			CreatedHandle = Sessions->OnSessionCreated.AddUObject(this, &UOneBrahamMainMenuWidget::HandleSessionCreated);
		}

		// Under NULL there is one network: say so where the choice would be.
		bHostLAN = Sessions->IsLANOnly() || bHostLAN;
		const FName Subsystem = Sessions->GetSubsystemName();
		if (Text_Build != nullptr)
		{
			Text_Build->SetText(FText::Format(NSLOCTEXT("OneBraham", "Menu_Build", "BUILD {0}.{1}.{2}  /  {3}"),
				FText::AsNumber(ONE_BRAHAM_VERSION_MAJOR), FText::AsNumber(ONE_BRAHAM_VERSION_MINOR),
				FText::AsNumber(ONE_BRAHAM_VERSION_PATCH),
				Sessions->IsLANOnly()
					? NSLOCTEXT("OneBraham", "Menu_BuildLAN", "LAN ONLY")
					: FText::FromName(Subsystem)));
		}
	}

	if (Text_HostMode != nullptr)
	{
		Text_HostMode->SetText(ModeLabel(HostMode));
	}
	if (Text_HostNetwork != nullptr)
	{
		Text_HostNetwork->SetText(bHostLAN ? NSLOCTEXT("OneBraham", "Host_LAN", "LAN") : NSLOCTEXT("OneBraham", "Host_Online", "ONLINE"));
	}
	if (Text_HostNote != nullptr)
	{
		const UOneBrahamSessionSubsystem* Sessions = GetSessions();
		Text_HostNote->SetText(Sessions == nullptr || Sessions->IsLANOnly()
			? NSLOCTEXT("OneBraham", "Host_NoteLAN", "Steam is not running, so this is a LAN game: colleagues on this network can find it under JOIN.")
			: NSLOCTEXT("OneBraham", "Host_NoteOnline", "ONLINE lists the match on Steam. LAN keeps it to this network."));
	}
}

void UOneBrahamMainMenuWidget::NativeDestruct()
{
	if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
	{
		Sessions->OnSessionsFound.Remove(FoundHandle);
		Sessions->OnSessionJoined.Remove(JoinedHandle);
		Sessions->OnSessionCreated.Remove(CreatedHandle);
	}
	FoundHandle.Reset();
	JoinedHandle.Reset();
	CreatedHandle.Reset();

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoJoinRetry);
	}

	Super::NativeDestruct();
}

UOneBrahamSessionSubsystem* UOneBrahamMainMenuWidget::GetSessions() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UOneBrahamSessionSubsystem>() : nullptr;
}

// ==========================================================================
//  Pages and focus
// ==========================================================================
void UOneBrahamMainMenuWidget::ShowPage(EPage InPage)
{
	if (Pages == nullptr)
	{
		return;
	}

	const EPage Previous = Page;
	Page = InPage;

	if (Previous == EPage::Settings && InPage != EPage::Settings && Settings != nullptr)
	{
		// Left without BACK (a sign clicked with the mouse): keep the changes.
		Settings->ApplyIfDirty();
	}

	Pages->SetActiveWidgetIndex(static_cast<int32>(InPage));
	if (PagePanel != nullptr)
	{
		PagePanel->SetVisibility(InPage == EPage::Home ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	switch (InPage)
	{
	case EPage::Join:
		if (Text_JoinBody != nullptr)
		{
			const UOneBrahamSessionSubsystem* Sessions = GetSessions();
			Text_JoinBody->SetText(Sessions == nullptr || Sessions->IsLANOnly()
				? NSLOCTEXT("OneBraham", "Join_BodyLAN", "Matches hosted on this network. Steam is not running, so the search stays on the LAN.")
				: NSLOCTEXT("OneBraham", "Join_BodyOnline", "Matches open on Steam, nearest first."));
		}
		RefreshJoinList();
		break;

	case EPage::Settings:
		if (Settings != nullptr)
		{
			// Read on every open: the pause menu may have changed them.
			Settings->SyncFromSettings();
		}
		break;

	case EPage::Credits:
		if (CreditsScroll != nullptr)
		{
			CreditsScroll->ScrollToStart();
		}
		break;

	default:
		break;
	}

	if (UWidget* Target = InPage == EPage::Home ? static_cast<UWidget*>(SignFor(Previous)) : GetDefaultFocusTarget())
	{
		Target->SetKeyboardFocus();
	}

	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Main menu page: %d"), static_cast<int32>(InPage));
}

UButton* UOneBrahamMainMenuWidget::SignFor(EPage InPage) const
{
	switch (InPage)
	{
	case EPage::Host:		return Sign_Host;
	case EPage::Join:		return Sign_Join;
	case EPage::Settings:	return Sign_Settings;
	case EPage::Credits:	return Sign_Credits;
	default:				return Sign_Play;
	}
}

UWidget* UOneBrahamMainMenuWidget::GetDefaultFocusTarget() const
{
	switch (Page)
	{
	case EPage::Play:
		return Button_PlayAscent;
	case EPage::Host:
		return Button_HostGo;
	case EPage::Join:
		return JoinRowButtons.Num() > 0 ? static_cast<UWidget*>(JoinRowButtons[0].Get()) : static_cast<UWidget*>(Button_JoinRefresh.Get());
	case EPage::Settings:
		return Settings != nullptr ? Settings->GetDefaultFocusTarget() : nullptr;
	case EPage::Credits:
		return Button_CreditsBack;
	default:
		return Sign_Play;
	}
}

FReply UOneBrahamMainMenuWidget::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	// The controller focuses the menu as a whole; the first press of a d-pad
	// should move from PLAY, not from nowhere.
	if (UWidget* Target = GetDefaultFocusTarget())
	{
		return FReply::Handled().SetUserFocus(Target->TakeWidget(), EFocusCause::SetDirectly);
	}
	return Super::NativeOnFocusReceived(InGeometry, InFocusEvent);
}

FReply UOneBrahamMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::BackSpace)
	{
		GoBack();
		return FReply::Handled();
	}

	if (Page == EPage::Settings && Settings != nullptr
		&& (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder))
	{
		Settings->CycleTab(Key == EKeys::Gamepad_RightShoulder ? 1 : -1);
		return FReply::Handled();
	}

	// The credits are text, which nothing focuses: the stick and the arrows
	// scroll them instead.
	if (Page == EPage::Credits && CreditsScroll != nullptr)
	{
		const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_RightStick_Up;
		const bool bDown = Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_RightStick_Down;
		if (bUp || bDown)
		{
			CreditsScroll->SetScrollOffset(FMath::Max(0.f, CreditsScroll->GetScrollOffset() + (bDown ? 90.f : -90.f)));
			return FReply::Handled();
		}
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UOneBrahamMainMenuWidget::GoBack()
{
	if (Page == EPage::Home)
	{
		return;
	}

	if (Page == EPage::Settings && Settings != nullptr)
	{
		// Applies and saves, then comes back through OnBackRequested.
		Settings->RequestBack();
		return;
	}

	if (Page == EPage::Join)
	{
		AutoJoinRow = INDEX_NONE;
		if (UOneBrahamSessionSubsystem* Sessions = GetSessions())
		{
			Sessions->CancelFindSessions();
		}
	}

	ShowPage(EPage::Home);
}

void UOneBrahamMainMenuWidget::ShowNotice(const FText& Notice)
{
	if (Text_Notice == nullptr)
	{
		return;
	}

	Text_Notice->SetText(Notice);
	Text_Notice->SetVisibility(Notice.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (!Notice.IsEmpty())
	{
		UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Main menu notice: %s"), *Notice.ToString());
	}
}

void UOneBrahamMainMenuWidget::SetStatus(UTextBlock* Line, const FText& Text)
{
	if (Line != nullptr)
	{
		Line->SetText(Text);
	}
}

// ==========================================================================
//  Actions
// ==========================================================================
void UOneBrahamMainMenuWidget::PlaySolo(EOneBrahamMatchMode Mode)
{
	if (bBusy)
	{
		return;
	}

	UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
	if (GameInstance == nullptr)
	{
		return;
	}

	bBusy = true;
	ShowPage(EPage::Play);
	SetStatus(Text_PlayStatus, FText::Format(NSLOCTEXT("OneBraham", "Play_Status", "Signing in for {0}..."), ModeLabel(Mode)));
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] PLAY %s (solo)."), *OneBrahamSessionRules::ModeKey(Mode));

	GameInstance->StartSoloMatch(Mode);
}

void UOneBrahamMainMenuWidget::Host(EOneBrahamMatchMode Mode, bool bLAN)
{
	if (bBusy)
	{
		return;
	}

	UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
	if (GameInstance == nullptr)
	{
		return;
	}

	HostMode = Mode;
	const UOneBrahamSessionSubsystem* Sessions = GetSessions();
	bHostLAN = bLAN || Sessions == nullptr || Sessions->IsLANOnly();

	ShowPage(EPage::Host);
	if (Text_HostMode != nullptr)
	{
		Text_HostMode->SetText(ModeLabel(HostMode));
	}
	if (Text_HostNetwork != nullptr)
	{
		Text_HostNetwork->SetText(bHostLAN ? NSLOCTEXT("OneBraham", "Host_LAN", "LAN") : NSLOCTEXT("OneBraham", "Host_Online", "ONLINE"));
	}

	bBusy = true;
	SetStatus(Text_HostStatus, NSLOCTEXT("OneBraham", "Host_Status", "Opening the floor..."));

	// "<Name>'s floor": what the JOIN list on the other machines shows.
	const UOneBrahamSaveSettings* Profile = GameInstance->GetProfile();
	const FString Name = Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName : FString(TEXT("A colleague"));
	const FString ServerName = FString::Printf(TEXT("%s's floor"), *Name);

	const UOneBrahamMatchSettingsAsset* MatchSettings = GameInstance->GetMatchSettings(Mode);
	const int32 MaxPlayers = MatchSettings != nullptr ? MatchSettings->GetExpectedPlayerCount() : 12;

	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] HOST %s %s as '%s' (%d slots)."), *OneBrahamSessionRules::ModeKey(Mode),
		bHostLAN ? TEXT("LAN") : TEXT("online"), *ServerName, MaxPlayers);

	GameInstance->HostListenServer(Mode, MaxPlayers, bHostLAN, ServerName);
}

void UOneBrahamMainMenuWidget::HandleSessionCreated(bool bWasSuccessful, bool bLAN)
{
	SetStatus(Text_HostStatus, bWasSuccessful
		? NSLOCTEXT("OneBraham", "Host_Created", "Floor open. Going up...")
		: NSLOCTEXT("OneBraham", "Host_NoSession", "The match could not be listed. Hosting anyway: colleagues can join by address."));
}

void UOneBrahamMainMenuWidget::RefreshJoinList()
{
	UOneBrahamSessionSubsystem* Sessions = GetSessions();
	if (Sessions == nullptr)
	{
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_NoOnline", "No network services. Matches can only be joined by address."));
		return;
	}

	if (Sessions->IsSearching())
	{
		return;
	}

	SetStatus(Text_JoinStatus, Sessions->IsLANOnly()
		? NSLOCTEXT("OneBraham", "Join_SearchingLAN", "Searching the network...")
		: NSLOCTEXT("OneBraham", "Join_SearchingOnline", "Searching Steam..."));

	if (!Sessions->FindSessions(Sessions->IsLANOnly()))
	{
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_SearchFailed", "The search could not start. Try again in a moment."));
	}
}

void UOneBrahamMainMenuWidget::HandleSessionsFound(bool bWasSuccessful, const TArray<FOneBrahamServerInfo>& Servers)
{
	RebuildJoinRows(Servers);

	if (!bWasSuccessful)
	{
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_SearchError", "The search failed. Try again in a moment."));
	}
	else if (Servers.Num() == 0)
	{
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_Empty", "Nobody is hosting. HOST a match, or search again."));
	}
	else
	{
		SetStatus(Text_JoinStatus, FText::Format(NSLOCTEXT("OneBraham", "Join_Found", "{0} {0}|plural(one=match,other=matches) found. Select one to join."),
			FText::AsNumber(Servers.Num())));
	}

	if (AutoJoinRow == INDEX_NONE)
	{
		return;
	}

	if (Servers.IsValidIndex(AutoJoinRow))
	{
		const int32 Row = AutoJoinRow;
		AutoJoinRow = INDEX_NONE;
		JoinRow(Row);
		return;
	}

	if (--AutoJoinAttemptsLeft <= 0)
	{
		UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] ob.menu.join: row %d never appeared."), AutoJoinRow);
		AutoJoinRow = INDEX_NONE;
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(AutoJoinRetry, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			RefreshJoinList();
		}), OneBrahamMainMenuLayout::AutoJoinRetrySeconds, false);
	}
}

void UOneBrahamMainMenuWidget::RebuildJoinRows(const TArray<FOneBrahamServerInfo>& Servers)
{
	using namespace OneBrahamMainMenuLayout;

	if (JoinRows == nullptr)
	{
		return;
	}

	// Rebuilt whole: the list changes only when a search lands.
	for (const TObjectPtr<UButton>& Old : JoinRowButtons)
	{
		FocusStyledButtons.Remove(Old.Get());
	}
	JoinRows->ClearChildren();
	JoinRowButtons.Reset();

	for (int32 Index = 0; Index < Servers.Num(); ++Index)
	{
		const FOneBrahamServerInfo& Info = Servers[Index];

		UOneBrahamButton* Button = WidgetTree->ConstructWidget<UOneBrahamButton>(UOneBrahamButton::StaticClass());
		Button->SetStyle(BrandButtonStyle(false));
		Button->InitBrandFocusable(true);

		UHorizontalBox* Cells = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		auto Cell = [this, Cells](const FText& Text, float Width, bool bBold, ETextJustify::Type Justify)
		{
			UTextBlock* Label = MakeLabel(Text, EOneBrahamTextSize::Small, Ink(), bBold, Justify);
			// Hover and focus recolour the row through the button's foreground.
			Label->SetColorAndOpacity(FSlateColor::UseForeground());
			Cells->AddChildToHorizontalBox(MakeCell(Label, Width));
		};
		Cell(FText::FromString(Info.ServerName), NameWidth, true, ETextJustify::Left);
		Cell(ModeLabel(Info.Mode), ModeWidth, false, ETextJustify::Left);
		Cell(PlayersLabel(Info), PlayersWidth, false, ETextJustify::Right);
		Cell(PingLabel(Info), PingWidth, false, ETextJustify::Right);

		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(Cells)))
		{
			ButtonSlot->SetPadding(FMargin(18.f, 10.f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Left);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}

		UOneBrahamMenuBinder* Binder = NewBinder();
		Binder->Clicked = [this, Index]() { JoinRow(Index); };
		Button->OnClicked.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleClicked);
		FocusStyledButtons.Add(Button);

		JoinRows->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		JoinRowButtons.Add(Button);
	}

	// Focus follows the list onto its first row for a pad, if it was on the
	// list or the refresh button when the search landed.
	if (Page == EPage::Join && JoinRowButtons.Num() > 0 && Button_JoinRefresh != nullptr && Button_JoinRefresh->HasKeyboardFocus())
	{
		JoinRowButtons[0]->SetKeyboardFocus();
	}
}

void UOneBrahamMainMenuWidget::JoinRow(int32 RowIndex)
{
	if (bBusy)
	{
		return;
	}

	UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
	const UOneBrahamSessionSubsystem* Sessions = GetSessions();
	if (GameInstance == nullptr || Sessions == nullptr || !Sessions->GetLastResults().IsValidIndex(RowIndex))
	{
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_Gone", "That match has closed."));
		return;
	}

	const FOneBrahamServerInfo& Info = Sessions->GetLastResults()[RowIndex];
	if (IsFull(Info))
	{
		// Said here because nothing else will: NULL joins a full session
		// without a word, and the host would seat a thirteenth player.
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_Full", "That floor is full."));
		UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] JOIN row %d refused: full (%d/%d)."), RowIndex, Info.CurrentPlayers, Info.MaxPlayers);
		return;
	}

	bBusy = true;
	SetStatus(Text_JoinStatus, FText::Format(NSLOCTEXT("OneBraham", "Join_Joining", "Joining {0}..."), FText::FromString(Info.ServerName)));
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] JOIN row %d: '%s' (%s, %d/%d, %d ms)."), RowIndex, *Info.ServerName,
		*OneBrahamSessionRules::ModeKey(Info.Mode), Info.CurrentPlayers, Info.MaxPlayers, Info.PingMs);

	GameInstance->JoinServerByIndex(Info.SearchResultIndex);
}

void UOneBrahamMainMenuWidget::JoinWhenFound(int32 RowIndex, int32 Attempts)
{
	AutoJoinRow = FMath::Max(0, RowIndex);
	AutoJoinAttemptsLeft = FMath::Max(1, Attempts);

	if (Page != EPage::Join)
	{
		ShowPage(EPage::Join);		// searches on the way in
	}
	else
	{
		RefreshJoinList();
	}
}

void UOneBrahamMainMenuWidget::HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL, const FText& Reason)
{
	if (bWasSuccessful)
	{
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_Travelling", "Signing in..."));
		return;
	}

	bBusy = false;
	SetStatus(Text_JoinStatus, Reason.IsEmpty() ? NSLOCTEXT("OneBraham", "Join_Failed", "Could not join. The host did not answer.") : Reason);
}

void UOneBrahamMainMenuWidget::Quit()
{
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] QUIT from the main menu."));
	if (Settings != nullptr)
	{
		Settings->ApplyIfDirty();
	}
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
