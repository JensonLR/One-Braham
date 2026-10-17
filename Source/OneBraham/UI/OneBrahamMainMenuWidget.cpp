// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMainMenuWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Spacer.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/OneBrahamFrontEndGameMode.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "UI/FrontEnd/OneBrahamCinemaOverlay.h"
#include "UI/FrontEnd/OneBrahamMenuSection.h"
#include "UI/FrontEnd/OneBrahamPaintedControl.h"
#include "UI/OneBrahamSettingsWidget.h"

using namespace OneBrahamPaint;

namespace OneBrahamMainMenuLayout
{
	/** Seconds between searches while ob.menu.join waits for its row. */
	constexpr float AutoJoinRetrySeconds = 2.f;

	// The reference frame is 1920 x 1080; positions are fractions of the
	// screen, sizes are 1080p pixels (the DPI curve scales them).
	constexpr float Letterbox = 0.128f;			// 2.39:1 on 16:9: each bar
	constexpr float RowHeight = 56.f;			// a directory row
	constexpr float CardHeight = 132.f;			// a mode card
	constexpr float CardGap = 12.f;
	constexpr float LedgerRow = 52.f;
	constexpr float BoardRow = 48.f;
	constexpr float IdleSeconds = 90.f;
	constexpr float ReelSeconds = 14.f;

	/** The HOME reel, in order. */
	const FName ReelShots[] = { TEXT("crown"), TEXT("reception"), TEXT("plaza_up"), TEXT("skydeck") };
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
		{ TEXT("signout"), EPage::SignOut },
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

int32 UOneBrahamMainMenuWidget::PingBars(int32 PingMs)
{
	// The shape carries the reading as well as the colour: never hue alone.
	if (PingMs <= 0 || PingMs >= 9999)
	{
		return 0;
	}
	return PingMs < 60 ? 3 : PingMs <= 120 ? 2 : 1;
}

FName UOneBrahamMainMenuWidget::ShotForPage(EPage InPage)
{
	switch (InPage)
	{
	case EPage::Play:		return TEXT("plaza_up");
	case EPage::Host:
	case EPage::Join:		return TEXT("reception_desk");
	case EPage::Credits:	return TEXT("skydeck");
	case EPage::SignOut:	return TEXT("doors_out");		// the wall glowing behind the badge
	case EPage::Settings:	return NAME_None;		// the frost, over whatever is on screen
	default:				return TEXT("crown");
	}
}

FText UOneBrahamMainMenuWidget::BreadcrumbForPage(EPage InPage)
{
	switch (InPage)
	{
	case EPage::Play:		return NSLOCTEXT("OneBraham", "Crumb_Play", "RECEPTION / LIFT LOBBY");
	case EPage::Host:		return NSLOCTEXT("OneBraham", "Crumb_Host", "RECEPTION / MEETING ROOMS / BOOK");
	case EPage::Join:		return NSLOCTEXT("OneBraham", "Crumb_Join", "RECEPTION / TODAY'S MEETINGS");
	case EPage::Settings:	return NSLOCTEXT("OneBraham", "Crumb_Settings", "RECEPTION / FACILITIES");
	case EPage::Credits:	return NSLOCTEXT("OneBraham", "Crumb_Credits", "RECEPTION / VISITORS' BOOK");
	case EPage::SignOut:	return NSLOCTEXT("OneBraham", "Crumb_SignOut", "RECEPTION / SIGN OUT");
	default:				return NSLOCTEXT("OneBraham", "Crumb_Home", "ONE BRAHAM / ALDGATE EAST / 18 FLOORS");
	}
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
			NSLOCTEXT("OneBraham", "Credits_TypeHead", "TYPE"),
			NSLOCTEXT("OneBraham", "Credits_TypeBody",
				"Archivo \u00B7 SIL Open Font License 1.1 \u00B7 The Archivo Project Authors\n"
				"IBM Plex Mono \u00B7 SIL Open Font License 1.1 \u00B7 IBM Corp.\n"
				"Doto \u00B7 SIL Open Font License 1.1 \u00B7 The Doto Project Authors\n"
				"Instrument Serif \u00B7 SIL Open Font License 1.1 \u00B7 The Instrument Serif Project Authors")
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
//  Construction helpers
// ==========================================================================
UOneBrahamPaintedControl* UOneBrahamMainMenuWidget::NewControl(const FVector2D& Size, bool bFocusable,
	const TSharedPtr<FOneBrahamReveal>& Reveal, int32 Slot)
{
	UOneBrahamPaintedControl* Control = WidgetTree->ConstructWidget<UOneBrahamPaintedControl>(UOneBrahamPaintedControl::StaticClass());
	Control->Setup(Size, bFocusable);
	Control->Reveal = Reveal;
	Control->RevealSlot = Slot;
	return Control;
}

void UOneBrahamMainMenuWidget::LinkVertical(const TArray<UOneBrahamPaintedControl*>& Controls, bool bWrap)
{
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		TWeakObjectPtr<UOneBrahamPaintedControl> Up = Controls[(Index + Controls.Num() - 1) % Controls.Num()];
		TWeakObjectPtr<UOneBrahamPaintedControl> Down = Controls[(Index + 1) % Controls.Num()];
		const bool bFirst = Index == 0;
		const bool bLast = Index == Controls.Num() - 1;
		Controls[Index]->OnNavigate = [Up, Down, bWrap, bFirst, bLast](EUINavigation Nav) -> UWidget*
		{
			if (Nav == EUINavigation::Up && (bWrap || !bFirst))
			{
				return Up.Get();
			}
			if (Nav == EUINavigation::Down && (bWrap || !bLast))
			{
				return Down.Get();
			}
			return nullptr;
		};
	}
}

void UOneBrahamMainMenuWidget::Place(UCanvasPanel* Canvas, UWidget* Widget, const FVector2D& Anchor01, const FVector2D& PixelOffset,
	const FVector2D& Size, float StretchToX01)
{
	UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Widget);
	if (StretchToX01 > Anchor01.X)
	{
		// Stretched across a span of the screen: offsets are margins on X.
		CanvasSlot->SetAnchors(FAnchors(Anchor01.X, Anchor01.Y, StretchToX01, Anchor01.Y));
		CanvasSlot->SetOffsets(FMargin(PixelOffset.X, PixelOffset.Y, 0.f, Size.Y));
	}
	else
	{
		CanvasSlot->SetAnchors(FAnchors(Anchor01.X, Anchor01.Y));
		CanvasSlot->SetOffsets(FMargin(PixelOffset.X, PixelOffset.Y, Size.X, Size.Y));
	}
}

UOneBrahamPaintedControl* UOneBrahamMainMenuWidget::MakeDirectoryRow(const FText& Verb, const FText& Where, TFunction<void()> OnChosen,
	int32 Slot, const TSharedPtr<FOneBrahamReveal>& Reveal)
{
	UOneBrahamPaintedControl* Row = NewControl(FVector2D(0.f, OneBrahamMainMenuLayout::RowHeight), true, Reveal, Slot);

	const FString VerbText = Verb.ToString();
	const FString WhereText = Where.ToString();
	Row->PaintFn = [VerbText, WhereText](const FPainter& C, const FOneBrahamControlState& State)
	{
		const FVector2D Size = C.Size();
		const float Lit = State.bLit
			? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
			: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f);
		const float Baseline = 37.f;

		// The press: the bar fills the row for 90 ms.
		if (State.SincePress < 0.09f)
		{
			Box(C, FVector2D(-18.f, 0.f), FVector2D(Size.X + 18.f, Size.Y), Tok(EOneBrahamColour::SignalBlue, 0.22f));
		}

		// Focus: a field, and the bar at the row's left growing from its centre.
		if (Lit > 0.f)
		{
			Box(C, FVector2D(-18.f, 0.f), FVector2D(Size.X + 18.f, Size.Y), Tok(EOneBrahamColour::White, 0.06f * Lit));
			const float BarH = Size.Y * Lit;
			Box(C, FVector2D(-18.f, (Size.Y - BarH) * 0.5f), FVector2D(6.f, BarH), Tok(EOneBrahamColour::SignalBlue));
		}

		const FSlateFontInfo VerbFont = Type(EOneBrahamType::Verb, 28.f, -10);
		const FSlateFontInfo WhereFont = Type(EOneBrahamType::Caption, 15.f);
		const float VerbX = 12.f * Lit;
		const FVector2D VerbSize = TextOnBaseline(C, VerbX, Baseline, VerbText, VerbFont, Tok(EOneBrahamColour::White, FMath::Lerp(0.8f, 1.f, Lit)));
		const FVector2D WhereSize = Measure(WhereText, WhereFont);
		TextOnBaseline(C, Size.X, Baseline, WhereText, WhereFont, Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.55f, 0.85f, Lit)), 1.f);

		// The leader: dots every 4 px, marching toward the where-line when lit.
		const float March = UOneBrahamUIStyle::ReduceMotion() ? 0.f : static_cast<float>(FMath::Fmod(State.Now, 2.0) * 8.0) * Lit;
		DottedH(C, VerbX + VerbSize.X + 14.f, Size.X - WhereSize.X - 14.f, Baseline - 1.f,
			Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.18f, 0.35f, Lit)), 4.f, 1.f, March);

		// The hairline between rows.
		Box(C, FVector2D(0.f, Size.Y - 1.f), FVector2D(Size.X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.08f));
	};

	TWeakObjectPtr<UOneBrahamMainMenuWidget> WeakThis(this);
	Row->OnActivate = [WeakThis, OnChosen]()
	{
		// The press shows for 90 ms, then the page changes.
		UOneBrahamMainMenuWidget* Menu = WeakThis.Get();
		if (Menu == nullptr || Menu->GetWorld() == nullptr)
		{
			return;
		}
		Menu->GetWorld()->GetTimerManager().SetTimer(Menu->ActionTimer, FTimerDelegate::CreateWeakLambda(Menu, [OnChosen]()
		{
			OnChosen();
		}), 0.09f, false);
	};
	return Row;
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamMainMenuWidget::BuildLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MainMenuRoot"));
	WidgetTree->RootWidget = Root;

	bFlat = UOneBrahamUIStyle::ReduceMotion();

	for (int32 Index = 0; Index <= static_cast<int32>(EPage::SignOut); ++Index)
	{
		TSharedPtr<FOneBrahamReveal> Reveal = MakeShared<FOneBrahamReveal>();
		Reveal->bFlat = bFlat;
		PageReveals.Add(Reveal);
	}

	// The picture's furniture: scrims, frost, the drawings that are not controls.
	Backdrop = NewControl(FVector2D::ZeroVector, false, nullptr, 0);
	Backdrop->PaintFn = [this](const FPainter& C, const FOneBrahamControlState& State) { PaintBackdrop(C, State.Now); };
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// One canvas per page, in EPage order; only the current one is visible.
	UWidget* (UOneBrahamMainMenuWidget::*Builders[])() = {
		&UOneBrahamMainMenuWidget::BuildHomePage,
		&UOneBrahamMainMenuWidget::BuildPlayPage,
		&UOneBrahamMainMenuWidget::BuildHostPage,
		&UOneBrahamMainMenuWidget::BuildJoinPage,
		&UOneBrahamMainMenuWidget::BuildSettingsPage,
		&UOneBrahamMainMenuWidget::BuildCreditsPage,
		&UOneBrahamMainMenuWidget::BuildSignOutPage,
	};
	for (auto Builder : Builders)
	{
		UWidget* PageWidget = (this->*Builder)();
		PageCanvases.Add(Cast<UCanvasPanel>(PageWidget));
		if (UOverlaySlot* PageSlot = Root->AddChildToOverlay(PageWidget))
		{
			PageSlot->SetHorizontalAlignment(HAlign_Fill);
			PageSlot->SetVerticalAlignment(VAlign_Fill);
		}
		PageWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// The camera grammar's screen half, over everything.
	Cinema = WidgetTree->ConstructWidget<UOneBrahamCinemaOverlay>(UOneBrahamCinemaOverlay::StaticClass(), TEXT("Cinema"));
	if (UOverlaySlot* CinemaSlot = Root->AddChildToOverlay(Cinema))
	{
		CinemaSlot->SetHorizontalAlignment(HAlign_Fill);
		CinemaSlot->SetVerticalAlignment(VAlign_Fill);
	}
	Cinema->SetLetterbox(OneBrahamMainMenuLayout::Letterbox, 0.f);

	// Until a camera arrives (it always does in L_MainMenu), HOME is simply up.
	Page = EPage::Home;
	if (PageCanvases.IsValidIndex(0) && PageCanvases[0] != nullptr)
	{
		PageCanvases[0]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	LastInputAt = FPlatformTime::Seconds();
}

UWidget* UOneBrahamMainMenuWidget::BuildHomePage()
{
	using namespace OneBrahamMainMenuLayout;

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_Home"));
	const TSharedPtr<FOneBrahamReveal>& Reveal = PageReveals[static_cast<int32>(EPage::Home)];

	UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	int32 Slot = 3;
	auto AddRow = [&](const FText& Verb, const FText& Where, TFunction<void()> OnChosen) -> UOneBrahamPaintedControl*
	{
		UOneBrahamPaintedControl* Row = MakeDirectoryRow(Verb, Where, MoveTemp(OnChosen), Slot++, Reveal);
		Rows->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Fill);
		return Row;
	};

	Sign_Play = AddRow(NSLOCTEXT("OneBraham", "Menu_Play", "PLAY"), NSLOCTEXT("OneBraham", "Menu_PlayWhere", "Lift lobby \u00B7 Ascend"),
		[this]() { ShowPage(EPage::Play); });
	Sign_Host = AddRow(NSLOCTEXT("OneBraham", "Menu_Host", "HOST"), NSLOCTEXT("OneBraham", "Menu_HostWhere", "Book a meeting room"),
		[this]() { ShowPage(EPage::Host); });
	Sign_Join = AddRow(NSLOCTEXT("OneBraham", "Menu_Join", "JOIN"), NSLOCTEXT("OneBraham", "Menu_JoinWhere", "Today's meetings"),
		[this]() { ShowPage(EPage::Join); });
	Sign_Settings = AddRow(NSLOCTEXT("OneBraham", "Menu_Settings", "FACILITIES"), NSLOCTEXT("OneBraham", "Menu_SettingsWhere", "Settings"),
		[this]() { ShowPage(EPage::Settings); });
	Sign_Credits = AddRow(NSLOCTEXT("OneBraham", "Menu_Credits", "VISITORS' BOOK"), NSLOCTEXT("OneBraham", "Menu_CreditsWhere", "Credits & licences"),
		[this]() { ShowPage(EPage::Credits); });

	// A rule and a gap before the way out, as on the pause menu.
	UOneBrahamPaintedControl* Rule = NewControl(FVector2D(0.f, 17.f), false, Reveal, Slot++);
	Rule->PaintFn = [](const FPainter& C, const FOneBrahamControlState&)
	{
		Box(C, FVector2D(0.f, 8.f), FVector2D(C.Size().X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.12f));
	};
	Rows->AddChildToVerticalBox(Rule)->SetHorizontalAlignment(HAlign_Fill);

	Sign_Quit = AddRow(NSLOCTEXT("OneBraham", "Menu_Quit", "SIGN OUT"), NSLOCTEXT("OneBraham", "Menu_QuitWhere", "Quit to desktop"),
		[this]() { ShowPage(EPage::SignOut); });

	LinkVertical({ Sign_Play, Sign_Host, Sign_Join, Sign_Settings, Sign_Credits, Sign_Quit }, true);

	Place(Canvas, Rows, FVector2D(0.06f, 0.45f), FVector2D::ZeroVector, FVector2D(0.f, RowHeight * 6.f + 17.f), 0.40f);
	return Canvas;
}

UWidget* UOneBrahamMainMenuWidget::BuildPlayPage()
{
	using namespace OneBrahamMainMenuLayout;

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_Play"));
	const TSharedPtr<FOneBrahamReveal>& Reveal = PageReveals[static_cast<int32>(EPage::Play)];

	struct FModeCopy { EOneBrahamMatchMode Mode; FText Destination; FText Line; uint32 Floors; int32 Target; };
	const FModeCopy Modes[] = {
		{ EOneBrahamMatchMode::Ascent, NSLOCTEXT("OneBraham", "Play_AscentTo", "\u2192 15 \u00B7 SKYDECK"),
			NSLOCTEXT("OneBraham", "Play_AscentLine", "Three squads. One extraction. The building is locking down."), 0x3FFFF, 15 },
		{ EOneBrahamMatchMode::FireDrill, NSLOCTEXT("OneBraham", "Play_FireDrillTo", "\u2192 ASSEMBLY POINT"),
			NSLOCTEXT("OneBraham", "Play_FireDrillLine", "6 v 6. Hold the points. This is not a drill."), (1u << 1) | (1u << 9) | (1u << 15), INDEX_NONE },
	};

	UVerticalBox* Cards = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	TArray<UOneBrahamPaintedControl*> Column;
	int32 Slot = 3;
	for (const FModeCopy& Copy : Modes)
	{
		UOneBrahamPaintedControl* Card = NewControl(FVector2D(0.f, CardHeight), true, Reveal, Slot++);
		const FString Name = ModeLabel(Copy.Mode).ToString();
		const FString Destination = Copy.Destination.ToString();
		const FString Line = Copy.Line.ToString();
		const uint32 Floors = Copy.Floors;
		const int32 Target = Copy.Target;
		const EOneBrahamMatchMode Mode = Copy.Mode;
		Card->PaintFn = [this, Name, Destination, Line, Floors, Target, Mode](const FPainter& C, const FOneBrahamControlState& State)
		{
			const FVector2D Size = C.Size();
			const float Lit = State.bLit ? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
				: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f);
			const bool bChosen = FocusedMode == Mode;

			Rounded(C, FVector2D::ZeroVector, Size, 4.f, Tok(EOneBrahamColour::Navy, FMath::Lerp(0.78f, 0.9f, Lit)),
				FMath::Lerp(Tok(EOneBrahamColour::LightGrey, 0.12f), Tok(EOneBrahamColour::SignalBlue, 0.6f), Lit), 1.f);
			if (Lit > 0.f || bChosen)
			{
				const float BarH = Size.Y * FMath::Max(Lit, bChosen ? 1.f : 0.f);
				Box(C, FVector2D(0.f, (Size.Y - BarH) * 0.5f), FVector2D(6.f, BarH), Tok(EOneBrahamColour::SignalBlue, bChosen && Lit <= 0.f ? 0.5f : 1.f));
			}

			TextOnBaseline(C, 28.f, 50.f, Name, Type(EOneBrahamType::Verb, 34.f, -10), Tok(EOneBrahamColour::White));
			TextOnBaseline(C, 28.f, 80.f, Destination, Type(EOneBrahamType::Telemetry, 16.f, 40), Tok(EOneBrahamColour::SafetyYellow));
			TextOnBaseline(C, 28.f, 108.f, Line, Type(EOneBrahamType::Body, 15.f), Tok(EOneBrahamColour::LightGrey, 0.75f));

			// The mini section: this mode's floors lit, the target in yellow,
			// stroking in bottom-up when the card takes focus.
			FOneBrahamMenuSectionParams Params;
			Params.Style = FOneBrahamMenuSectionParams::EStyle::Mini;
			Params.LitFloors = Floors;
			Params.TargetFloor = Target;
			Params.RevealAt = State.bLit && State.SinceLit < 1.f ? State.Now - State.SinceLit : 0.0;
			FOneBrahamMenuSection::Paint(C, FVector2D(Size.X - 24.f - 40.f, (Size.Y - 112.f) * 0.5f), FVector2D(40.f, 112.f), Params, State.Now);
		};
		Card->OnLitChanged = [this, Mode](bool bNowLit)
		{
			if (bNowLit && FocusedMode != Mode)
			{
				FocusedMode = Mode;
				ModeFocusedAt = FPlatformTime::Seconds();
			}
		};
		Card->OnActivate = [this]()
		{
			// The card picks the destination; the lift buttons go there.
			if (LiftButtons.IsValidIndex(0) && LiftButtons[0] != nullptr)
			{
				LiftButtons[0]->SetKeyboardFocus();
			}
		};
		Cards->AddChildToVerticalBox(Card)->SetPadding(FMargin(0.f, 0.f, 0.f, CardGap));
		ModeCards.Add(Card);
		CardModes.Add(Mode);
		Column.Add(Card);
	}
	Place(Canvas, Cards, FVector2D(0.06f, 0.30f), FVector2D::ZeroVector, FVector2D(0.f, (CardHeight + CardGap) * Column.Num()), 0.40f);

	// The lift buttons: SOLO WITH BOTS, HOST, JOIN.
	struct FLift { FText Label; int32 Glyph; };
	const FLift Lifts[] = {
		{ NSLOCTEXT("OneBraham", "Play_Solo", "SOLO WITH BOTS"), 0 },
		{ NSLOCTEXT("OneBraham", "Play_Host", "HOST"), 1 },
		{ NSLOCTEXT("OneBraham", "Play_Join", "JOIN"), 2 },
	};
	const float LiftTop = (CardHeight + CardGap) * Column.Num() + 22.f;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Lifts); ++Index)
	{
		UOneBrahamPaintedControl* Lift = NewControl(FVector2D(132.f, 108.f), true, Reveal, Slot++);
		const FString Label = Lifts[Index].Label.ToString();
		const int32 Glyph = Lifts[Index].Glyph;
		Lift->PaintFn = [this, Label, Glyph, Index](const FPainter& C, const FOneBrahamControlState& State)
		{
			const FVector2D Centre(C.Size().X * 0.5f, 34.f);
			const bool bPressed = PressedLift == Index;
			const float Lit = FMath::Max(bPressed ? 1.f : 0.f, State.bLit ? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
				: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f));
			const FLinearColor Ring = FMath::Lerp(Tok(EOneBrahamColour::LightGrey, 0.4f), Tok(EOneBrahamColour::SignalBlue), Lit);

			PenCircle(C, Centre, 31.f, Tok(EOneBrahamColour::Navy, 0.55f), 0.f);
			if (bPressed)
			{
				// A real lift button: the ring stays lit until the doors close.
				PenCircle(C, Centre, 31.f, Tok(EOneBrahamColour::SignalBlue, 0.28f), 0.f);
			}
			PenCircle(C, Centre, 31.f, Ring, 2.f);

			const FLinearColor Ink = Tok(EOneBrahamColour::White, FMath::Lerp(0.8f, 1.f, Lit));
			if (Glyph == 0)
			{
				// Up: a triangle.
				PenPath(C, { Centre + FVector2D(-9.f, 6.f), Centre + FVector2D(0.f, -8.f), Centre + FVector2D(9.f, 6.f), Centre + FVector2D(-9.f, 6.f) }, Ink, 2.f);
			}
			else if (Glyph == 1)
			{
				// Book: a plus.
				PenLine(C, Centre + FVector2D(-9.f, 0.f), Centre + FVector2D(9.f, 0.f), Ink, 2.f);
				PenLine(C, Centre + FVector2D(0.f, -9.f), Centre + FVector2D(0.f, 9.f), Ink, 2.f);
			}
			else
			{
				// Join: doors opening.
				PenLine(C, Centre + FVector2D(-3.f, -9.f), Centre + FVector2D(-3.f, 9.f), Ink, 2.f);
				PenLine(C, Centre + FVector2D(3.f, -9.f), Centre + FVector2D(3.f, 9.f), Ink, 2.f);
				PenPath(C, { Centre + FVector2D(-7.f, -4.f), Centre + FVector2D(-11.f, 0.f), Centre + FVector2D(-7.f, 4.f) }, Ink, 2.f);
				PenPath(C, { Centre + FVector2D(7.f, -4.f), Centre + FVector2D(11.f, 0.f), Centre + FVector2D(7.f, 4.f) }, Ink, 2.f);
			}
			TextOnBaseline(C, Centre.X, 92.f, Label, Type(EOneBrahamType::Label, 12.f, 140), Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.7f, 1.f, Lit)), 0.5f);
		};
		LiftButtons.Add(Lift);
		Place(Canvas, Lift, FVector2D(0.06f, 0.30f), FVector2D(-34.f + Index * 150.f, LiftTop), FVector2D(132.f, 108.f));
	}
	LiftButtons[0]->OnActivate = [this]() { RideLift(FocusedMode); };
	LiftButtons[1]->OnActivate = [this]()
	{
		PressedLift = 1;
		HostMode = FocusedMode;
		ShowPage(EPage::Host);
	};
	LiftButtons[2]->OnActivate = [this]()
	{
		PressedLift = 2;
		ShowPage(EPage::Join);
	};

	// Cards: up and down; the last card's down is the lift row. The lift row
	// walks left and right and goes back up to the cards.
	for (int32 Index = 0; Index < ModeCards.Num(); ++Index)
	{
		TWeakObjectPtr<UOneBrahamPaintedControl> Up = ModeCards[(Index + ModeCards.Num() - 1) % ModeCards.Num()];
		TWeakObjectPtr<UOneBrahamPaintedControl> Down = Index + 1 < ModeCards.Num() ? ModeCards[Index + 1].Get() : LiftButtons[0].Get();
		const bool bFirst = Index == 0;
		ModeCards[Index]->OnNavigate = [Up, Down, bFirst, this](EUINavigation Nav) -> UWidget*
		{
			if (Nav == EUINavigation::Up)
			{
				return bFirst ? static_cast<UWidget*>(LiftButtons[0].Get()) : Up.Get();
			}
			if (Nav == EUINavigation::Down)
			{
				return Down.Get();
			}
			return nullptr;
		};
	}
	for (int32 Index = 0; Index < LiftButtons.Num(); ++Index)
	{
		TWeakObjectPtr<UOneBrahamPaintedControl> Left = LiftButtons[(Index + LiftButtons.Num() - 1) % LiftButtons.Num()];
		TWeakObjectPtr<UOneBrahamPaintedControl> Right = LiftButtons[(Index + 1) % LiftButtons.Num()];
		LiftButtons[Index]->OnNavigate = [Left, Right, this](EUINavigation Nav) -> UWidget*
		{
			if (Nav == EUINavigation::Left)
			{
				return Left.Get();
			}
			if (Nav == EUINavigation::Right)
			{
				return Right.Get();
			}
			if (Nav == EUINavigation::Up || Nav == EUINavigation::Down)
			{
				const int32 Card = CardModes.IndexOfByKey(FocusedMode);
				return ModeCards.IsValidIndex(Card) ? ModeCards[Card].Get() : nullptr;
			}
			return nullptr;
		};
	}
	return Canvas;
}

UWidget* UOneBrahamMainMenuWidget::BuildHostPage()
{
	using namespace OneBrahamMainMenuLayout;

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_Host"));
	const TSharedPtr<FOneBrahamReveal>& Reveal = PageReveals[static_cast<int32>(EPage::Host)];

	// One ledger row: a label over a value; Left/Right (or Enter) changes it.
	auto MakeLedgerRow = [this, &Reveal](int32 Slot, const FText& Label, TFunction<FString()> ValueOf, TFunction<void(int32)> StepBy,
		bool bStepper) -> UOneBrahamPaintedControl*
	{
		const bool bFocusable = StepBy != nullptr;
		UOneBrahamPaintedControl* Row = NewControl(FVector2D(0.f, LedgerRow), bFocusable, Reveal, Slot);
		const FString LabelText = Label.ToString();
		Row->PaintFn = [LabelText, ValueOf, bStepper, bFocusable](const FPainter& C, const FOneBrahamControlState& State)
		{
			const FVector2D Size = C.Size();
			const float Lit = State.bLit ? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
				: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f);
			if (Lit > 0.f)
			{
				Box(C, FVector2D::ZeroVector, Size, Tok(EOneBrahamColour::White, 0.05f * Lit));
				Box(C, FVector2D(0.f, 0.f), FVector2D(2.f, Size.Y), Tok(EOneBrahamColour::SignalBlue, Lit));
			}
			TextOnBaseline(C, 16.f, 18.f, LabelText, Type(EOneBrahamType::Eyebrow, 11.f, 180), Tok(EOneBrahamColour::LightGrey, 0.55f));
			const FString Text = ValueOf();
			const FSlateFontInfo Font = bStepper ? Type(EOneBrahamType::Telemetry, 18.f) : Type(EOneBrahamType::Body, 18.f);
			const float ValueAlpha = bFocusable ? FMath::Lerp(0.85f, 1.f, Lit) : 0.7f;
			TextOnBaseline(C, 16.f, 42.f, Text, Font, Tok(EOneBrahamColour::White, ValueAlpha));
			if (bFocusable)
			{
				// The arrows that say it can be changed.
				const FSlateFontInfo Arrow = Type(EOneBrahamType::Telemetry, 20.f);
				TextOnBaseline(C, Size.X - 16.f, 42.f, TEXT("\u2039   \u203A"), Arrow, FMath::Lerp(Tok(EOneBrahamColour::LightGrey, 0.5f),
					Tok(EOneBrahamColour::SignalBlue), Lit), 1.f);
			}
			Box(C, FVector2D(0.f, Size.Y - 1.f), FVector2D(Size.X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.12f));
		};
		if (bFocusable)
		{
			Row->OnKey = [StepBy](const FKey& Key)
			{
				if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::A)
				{
					StepBy(-1);
					return true;
				}
				if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right || Key == EKeys::D)
				{
					StepBy(1);
					return true;
				}
				return false;
			};
			Row->OnActivate = [StepBy]() { StepBy(1); };
		}
		return Row;
	};

	UVerticalBox* Ledger = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	TArray<UOneBrahamPaintedControl*> Column;

	UOneBrahamPaintedControl* TypeRow = MakeLedgerRow(3, NSLOCTEXT("OneBraham", "Host_Type", "MEETING TYPE"),
		[this]() { return FString::Printf(TEXT("%s"), *ModeLabel(HostMode).ToString()); },
		[this](int32) { HostMode = HostMode == EOneBrahamMatchMode::Ascent ? EOneBrahamMatchMode::FireDrill : EOneBrahamMatchMode::Ascent; },
		false);
	UOneBrahamPaintedControl* AttendeesRow = MakeLedgerRow(4, NSLOCTEXT("OneBraham", "Host_Attendees", "ATTENDEES"),
		[this]() { return FString::Printf(TEXT("%02d"), HostAttendees); },
		[this](int32 Delta) { HostAttendees = FMath::Clamp(HostAttendees + Delta, 2, 12); },
		true);
	UOneBrahamPaintedControl* TempsRow = MakeLedgerRow(5, NSLOCTEXT("OneBraham", "Host_Temps", "FILL EMPTY SEATS WITH TEMPS"),
		[]() { return NSLOCTEXT("OneBraham", "Host_TempsValue", "Always \u00B7 bots hold every empty desk").ToString(); },
		nullptr, false);
	UOneBrahamPaintedControl* NetworkRow = MakeLedgerRow(6, NSLOCTEXT("OneBraham", "Host_Network", "NETWORK"),
		[this]()
		{
			return bHostLAN ? NSLOCTEXT("OneBraham", "Host_LANValue", "LAN \u00B7 this building only").ToString()
				: NSLOCTEXT("OneBraham", "Host_OnlineValue", "ONLINE \u00B7 listed on Steam").ToString();
		},
		[this](int32)
		{
			const UOneBrahamSessionSubsystem* Sessions = GetSessions();
			if (Sessions != nullptr && !Sessions->IsLANOnly())
			{
				bHostLAN = !bHostLAN;
			}
		},
		false);
	UOneBrahamPaintedControl* NameRow = MakeLedgerRow(7, NSLOCTEXT("OneBraham", "Host_RoomName", "ROOM NAME"),
		[this]()
		{
			const UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
			const UOneBrahamSaveSettings* Profile = GameInstance != nullptr ? GameInstance->GetProfile() : nullptr;
			const FString Name = Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName : FString(TEXT("A colleague"));
			return FString::Printf(TEXT("%s's floor"), *Name);
		},
		nullptr, false);

	for (UOneBrahamPaintedControl* Row : { TypeRow, AttendeesRow, TempsRow, NetworkRow, NameRow })
	{
		Ledger->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Fill);
		HostRows.Add(Row);
	}
	Place(Canvas, Ledger, FVector2D(0.56f, 0.30f), FVector2D(0.f, 0.f), FVector2D(0.f, LedgerRow * 5.f), 0.92f);

	// BOOK ROOM: a full-width button filled with the accent.
	Button_HostGo = NewControl(FVector2D(0.f, 60.f), true, Reveal, 9);
	Button_HostGo->PaintFn = [this](const FPainter& C, const FOneBrahamControlState& State)
	{
		const FVector2D Size = C.Size();
		const float Lit = State.bLit ? 1.f : 0.f;
		const bool bBooked = Booking != EBooking::Available;
		const FLinearColor Fill = bBooked ? Tok(EOneBrahamColour::LightGrey, 0.12f)
			: State.SincePress < 0.09f ? Tok(EOneBrahamColour::White) : Tok(EOneBrahamColour::SignalBlue);
		Rounded(C, FVector2D::ZeroVector, Size, 2.f, Fill, Lit > 0.f ? Tok(EOneBrahamColour::White, 0.9f) : FLinearColor::Transparent, 2.f);
		const FString Label = bBooked ? NSLOCTEXT("OneBraham", "Host_Booked", "BOOKED").ToString() : NSLOCTEXT("OneBraham", "Host_Go", "BOOK ROOM").ToString();
		TextOnBaseline(C, Size.X * 0.5f, 38.f, Label, Type(EOneBrahamType::Verb, 20.f, 40),
			bBooked ? Tok(EOneBrahamColour::LightGrey, 0.6f) : Tok(EOneBrahamColour::Navy), 0.5f);
		if (!bBooked)
		{
			TextOnBaseline(C, Size.X - 22.f, 37.f, TEXT("\u2192"), Type(EOneBrahamType::Telemetry, 20.f), Tok(EOneBrahamColour::Navy), 1.f);
		}
	};
	Button_HostGo->OnActivate = [this]() { Host(HostMode, bHostLAN); };
	Place(Canvas, Button_HostGo, FVector2D(0.56f, 0.30f), FVector2D(0.f, LedgerRow * 5.f + 36.f), FVector2D(0.f, 60.f), 0.92f);

	LinkVertical({ TypeRow, AttendeesRow, NetworkRow, Button_HostGo }, true);
	return Canvas;
}

UWidget* UOneBrahamMainMenuWidget::BuildJoinPage()
{
	using namespace OneBrahamMainMenuLayout;

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_Join"));
	const TSharedPtr<FOneBrahamReveal>& Reveal = PageReveals[static_cast<int32>(EPage::Join)];

	JoinRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("JoinRows"));
	Place(Canvas, JoinRows, FVector2D(0.46f, 0.30f), FVector2D(0.f, 0.f), FVector2D(0.f, BoardRow * 7.f), 0.92f);

	// "Check again": a text link under the board.
	auto Link = [this, &Reveal](int32 Slot, const FText& Label, TFunction<void()> OnChosen) -> UOneBrahamPaintedControl*
	{
		UOneBrahamPaintedControl* Control = NewControl(FVector2D(260.f, 34.f), true, Reveal, Slot);
		const FString Text = Label.ToString();
		Control->PaintFn = [Text](const FPainter& C, const FOneBrahamControlState& State)
		{
			const float Lit = State.bLit ? 1.f : 0.f;
			const FSlateFontInfo Font = Type(EOneBrahamType::Label, 13.f, 160);
			const FVector2D Size = TextOnBaseline(C, 0.f, 22.f, Text, Font, Tok(EOneBrahamColour::White, FMath::Lerp(0.7f, 1.f, Lit)));
			Box(C, FVector2D(0.f, 27.f), FVector2D(Size.X * (State.bLit ? 1.f : 0.25f), 2.f),
				State.bLit ? Tok(EOneBrahamColour::SignalBlue) : Tok(EOneBrahamColour::LightGrey, 0.3f));
		};
		Control->OnActivate = MoveTemp(OnChosen);
		return Control;
	};

	Button_JoinRefresh = Link(12, NSLOCTEXT("OneBraham", "Join_Refresh", "CHECK AGAIN"), [this]() { RefreshJoinList(); });
	Place(Canvas, Button_JoinRefresh, FVector2D(0.46f, 0.30f), FVector2D(16.f, BoardRow * 7.f + 24.f), FVector2D(260.f, 34.f));

	Button_JoinBook = Link(13, NSLOCTEXT("OneBraham", "Join_Book", "BOOK A ROOM \u2192"), [this]() { ShowPage(EPage::Host); });
	Place(Canvas, Button_JoinBook, FVector2D(0.46f, 0.30f), FVector2D(16.f, 96.f), FVector2D(260.f, 34.f));
	Button_JoinBook->SetVisibility(ESlateVisibility::Collapsed);

	// DIAL EXTENSION: a direct address, for a host the search cannot see.
	DialFrame = NewControl(FVector2D(420.f, 40.f), false, Reveal, 14);
	DialFrame->PaintFn = [this](const FPainter& C, const FOneBrahamControlState&)
	{
		const bool bFocused = DialText != nullptr && DialText->HasKeyboardFocus();
		TextOnBaseline(C, 0.f, -8.f, NSLOCTEXT("OneBraham", "Join_Dial", "DIAL EXTENSION").ToString(), Type(EOneBrahamType::Eyebrow, 11.f, 180),
			Tok(EOneBrahamColour::LightGrey, 0.55f));
		Box(C, FVector2D(0.f, C.Size().Y - 1.f), FVector2D(C.Size().X, bFocused ? 2.f : 1.f),
			bFocused ? Tok(EOneBrahamColour::SignalBlue) : Tok(EOneBrahamColour::LightGrey, 0.3f));
		if (DialText != nullptr && DialText->GetText().IsEmpty() && !bFocused)
		{
			TextOnBaseline(C, 0.f, 27.f, TEXT("192.168.0.12"), Type(EOneBrahamType::TelemetryLight, 16.f), Tok(EOneBrahamColour::LightGrey, 0.25f));
		}
	};
	Place(Canvas, DialFrame, FVector2D(0.46f, 0.30f), FVector2D(380.f, BoardRow * 7.f + 20.f), FVector2D(420.f, 40.f));

	DialText = WidgetTree->ConstructWidget<UEditableText>(UEditableText::StaticClass(), TEXT("DialText"));
	FEditableTextStyle DialStyle;
	DialStyle.SetFont(UOneBrahamUIStyle::TypeFont(EOneBrahamType::TelemetryLight, 16.f));
	DialStyle.SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::Colour(EOneBrahamColour::White)));
	DialText->SetWidgetStyle(DialStyle);
	DialText->OnTextCommitted.AddDynamic(this, &UOneBrahamMainMenuWidget::HandleDialCommitted);
	Place(Canvas, DialText, FVector2D(0.46f, 0.30f), FVector2D(380.f, BoardRow * 7.f + 26.f), FVector2D(420.f, 30.f));

	return Canvas;
}

UWidget* UOneBrahamMainMenuWidget::BuildSettingsPage()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_Settings"));

	Settings = CreateWidget<UOneBrahamSettingsWidget>(this, UOneBrahamSettingsWidget::StaticClass());
	if (Settings == nullptr)
	{
		return Canvas;
	}
	Settings->OnBackRequested.BindWeakLambda(this, [this]() { ShowPage(EPage::Home); });

	// The panel: x 10-90%, y 16-84%, inside the letterbox.
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Settings);
	PanelSlot->SetAnchors(FAnchors(0.10f, 0.16f, 0.90f, 0.84f));
	PanelSlot->SetOffsets(FMargin(0.f));
	return Canvas;
}

UWidget* UOneBrahamMainMenuWidget::BuildCreditsPage()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_Credits"));

	// The roll is drawn by the backdrop; this holds focus, so the keys reach
	// the page (hold Enter to hurry it, anything else to leave).
	CreditsFocus = NewControl(FVector2D(10.f, 10.f), true, PageReveals[static_cast<int32>(EPage::Credits)], 0);
	CreditsFocus->OnActivate = [this]() { bCreditsFast = true; };
	Place(Canvas, CreditsFocus, FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, FVector2D(10.f, 10.f));
	return Canvas;
}

UWidget* UOneBrahamMainMenuWidget::BuildSignOutPage()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Page_SignOut"));
	const TSharedPtr<FOneBrahamReveal>& Reveal = PageReveals[static_cast<int32>(EPage::SignOut)];

	auto BadgeButton = [this, &Reveal](int32 Slot, const FText& Label, bool bPrimary) -> UOneBrahamPaintedControl*
	{
		UOneBrahamPaintedControl* Button = NewControl(FVector2D(176.f, 44.f), true, Reveal, Slot);
		const FString Text = Label.ToString();
		Button->PaintFn = [Text, bPrimary](const FPainter& C, const FOneBrahamControlState& State)
		{
			const FVector2D Size = C.Size();
			const bool bLit = State.bLit;
			const FLinearColor Navy = Tok(EOneBrahamColour::Navy);
			if (bLit)
			{
				Rounded(C, FVector2D::ZeroVector, Size, 3.f, Tok(EOneBrahamColour::SignalBlue));
			}
			else
			{
				Rounded(C, FVector2D::ZeroVector, Size, 3.f, FLinearColor::Transparent, Fade(Navy, bPrimary ? 1.f : 0.6f), 1.f);
			}
			TextOnBaseline(C, Size.X * 0.5f, 28.f, Text, Type(EOneBrahamType::Label, 15.f, 120), Navy, 0.5f);
		};
		return Button;
	};

	Button_Stay = BadgeButton(3, NSLOCTEXT("OneBraham", "SignOut_Stay", "STAY"), true);
	Button_Stay->OnActivate = [this]() { ShowPage(EPage::Home); };
	Button_SignOut = BadgeButton(4, NSLOCTEXT("OneBraham", "SignOut_Confirm", "SIGN OUT"), false);
	Button_SignOut->OnActivate = [this]()
	{
		if (bSigningOut)
		{
			return;
		}
		// The pass is deactivated: half a second to black, then out.
		bSigningOut = true;
		if (Cinema != nullptr)
		{
			Cinema->Dip(0.5f, 30.f, 0.2f);
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(ActionTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { Quit(); }), 0.6f, false);
		}
	};

	Place(Canvas, Button_Stay, FVector2D(0.5f, 0.5f), FVector2D(-188.f, 46.f), FVector2D(176.f, 44.f));
	Place(Canvas, Button_SignOut, FVector2D(0.5f, 0.5f), FVector2D(12.f, 46.f), FVector2D(176.f, 44.f));

	auto Across = [](UOneBrahamPaintedControl* Other)
	{
		TWeakObjectPtr<UOneBrahamPaintedControl> Weak = Other;
		return [Weak](EUINavigation Nav) -> UWidget*
		{
			return Nav == EUINavigation::Left || Nav == EUINavigation::Right ? Weak.Get() : nullptr;
		};
	};
	Button_Stay->OnNavigate = Across(Button_SignOut);
	Button_SignOut->OnNavigate = Across(Button_Stay);
	return Canvas;
}

// ==========================================================================
//  Painting: the picture layer
// ==========================================================================
float UOneBrahamMainMenuWidget::PageAlpha(EPage InPage, double Now) const
{
	const int32 Index = static_cast<int32>(InPage);
	if (!PageCanvases.IsValidIndex(Index) || PageCanvases[Index] == nullptr || !PageCanvases[Index]->IsVisible()
		|| !PageReveals.IsValidIndex(Index))
	{
		return 0.f;
	}
	float Alpha = 1.f;
	float Offset = 0.f;
	PageReveals[Index]->Evaluate(Now, 0, Alpha, Offset);
	return Alpha;
}

bool UOneBrahamMainMenuWidget::IsIdle(double Now) const
{
	return Page == EPage::Home && !bColdOpen && Now - LastInputAt > OneBrahamMainMenuLayout::IdleSeconds;
}

void UOneBrahamMainMenuWidget::PaintBackdrop(const FPainter& C, double Now) const
{
	if (bColdOpen)
	{
		return;
	}

	const FVector2D S = C.Size();
	const float Home = PageAlpha(EPage::Home, Now);
	const float Play = PageAlpha(EPage::Play, Now);
	const float Host = PageAlpha(EPage::Host, Now);
	const float Join = PageAlpha(EPage::Join, Now);
	const float Facilities = PageAlpha(EPage::Settings, Now);
	const float Credits = PageAlpha(EPage::Credits, Now);
	const float SignOut = PageAlpha(EPage::SignOut, Now);

	// The directory's scrim: black from the left edge, gone by half way. No
	// panel and no border; the picture stays the picture.
	const float LeftScrim = FMath::Max(Home * HomeIdleAlpha(Now), Play);
	if (LeftScrim > 0.f)
	{
		GradientH(C, FVector2D::ZeroVector, S, {
			{ 0.f, Tok(EOneBrahamColour::OLEDBlack, 0.78f * LeftScrim) },
			{ 0.30f, Tok(EOneBrahamColour::OLEDBlack, 0.72f * LeftScrim) },
			{ 0.42f, Tok(EOneBrahamColour::OLEDBlack, 0.38f * LeftScrim) },
			{ 0.56f, Tok(EOneBrahamColour::OLEDBlack, 0.f) },
			{ 1.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) } });
	}

	// The mirrored scrim of the right-hand columns.
	const float RightScrim = FMath::Max3(Host, Join, Play * 0.8f);
	if (RightScrim > 0.f)
	{
		const float From = Join > Host ? 0.36f : 0.44f;
		GradientH(C, FVector2D::ZeroVector, S, {
			{ 0.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) },
			{ From, Tok(EOneBrahamColour::OLEDBlack, 0.f) },
			{ From + 0.18f, Tok(EOneBrahamColour::OLEDBlack, 0.75f * RightScrim) },
			{ 1.f, Tok(EOneBrahamColour::OLEDBlack, 0.78f * RightScrim) } });
	}

	if (Home > 0.f)
	{
		PaintHomeHeader(C, Now, Home * HomeIdleAlpha(Now));
	}
	if (Play > 0.f)
	{
		PaintPlayPage(C, Now, Play);
	}
	if (Host > 0.f)
	{
		PaintHostPage(C, Now, Host);
	}
	if (Join > 0.f)
	{
		PaintJoinPage(C, Now, Join);
	}

	// FACILITIES: the meeting-room glass. The picture blurs, takes a 4% frost
	// and the band of dots the real glass carries at eye height.
	if (Facilities > 0.f)
	{
		const bool bLow = bFlat || !FPlatformMisc::SupportsBackbufferSampling();
		if (!bLow)
		{
			Blur(C, FVector2D::ZeroVector, S, 12.f * Facilities);
			Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::SignageWhite, 0.04f * Facilities));
		}
		else
		{
			Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::Navy, 0.7f * Facilities));
		}
		DotGrid(C, FVector2D(0.f, S.Y * 0.40f), FVector2D(S.X, S.Y * 0.06f), 12.f, Tok(EOneBrahamColour::White, 0.10f * Facilities));
		Rounded(C, FVector2D(S.X * 0.10f, S.Y * 0.16f), FVector2D(S.X * 0.80f, S.Y * 0.68f), 4.f,
			Tok(EOneBrahamColour::OLEDBlack, 0.55f * Facilities), Tok(EOneBrahamColour::LightGrey, 0.10f * Facilities), 1.f);
	}

	if (Credits > 0.f)
	{
		PaintCredits(C, Now, Credits);
	}
	if (SignOut > 0.f)
	{
		PaintSignOut(C, Now, SignOut);
	}
}

float UOneBrahamMainMenuWidget::HomeIdleAlpha(double Now) const
{
	using namespace OneBrahamMainMenuLayout;
	if (Page != EPage::Home || bColdOpen)
	{
		return 1.f;
	}
	const double Quiet = Now - LastInputAt;
	if (Quiet > IdleSeconds)
	{
		// After ninety seconds the directory steps aside for the reel.
		return 1.f - FMath::Clamp(static_cast<float>((Quiet - IdleSeconds) / 0.6), 0.f, 1.f);
	}
	// Woken: back over 250 ms.
	return FMath::Clamp(static_cast<float>((Now - IdleWakeAt) / 0.25), 0.f, 1.f);
}

void UOneBrahamMainMenuWidget::PaintHomeHeader(const FPainter& Canvas, double Now, float Alpha) const
{
	const FPainter C(Canvas, Alpha);
	const FVector2D S = C.Size();
	const float X = S.X * 0.06f;

	TextOnBaseline(C, X, S.Y * 0.22f, NSLOCTEXT("OneBraham", "Menu_Kicker", "WELCOME TO ONE BRAHAM").ToString(),
		Type(EOneBrahamType::Eyebrow, 13.f, 180), Tok(EOneBrahamColour::LightGrey, 0.7f));

	FSlateFontInfo TitleFont = Type(EOneBrahamType::Title, 104.f, -10);
	TitleFont.OutlineSettings = FFontOutlineSettings(1, Tok(EOneBrahamColour::OLEDBlack, 0.4f));
	const float TitleBaseline = S.Y * 0.33f;
	const FVector2D Title = TextOnBaseline(C, X - 4.f, TitleBaseline, FString(ONE_BRAHAM_DISPLAY_NAME), TitleFont, Tok(EOneBrahamColour::White));

	// The ribbon rule: drawn left to right when HOME appears.
	const float Draw = HomeBuiltAt > 0.0 ? UOneBrahamUIStyle::Tween(Now, HomeBuiltAt + 0.15, 0.4f, EOneBrahamEase::Ribbon) : 1.f;
	Ribbon(C, FVector2D(X, TitleBaseline + 20.f), FVector2D(Title.X * 0.44f * Draw, 2.f));

	TextOnBaseline(C, X, S.Y * 0.375f + 18.f,
		NSLOCTEXT("OneBraham", "Menu_Strapline", "Eighteen floors. Twelve colleagues. One way out, from the roof.").ToString(),
		Type(EOneBrahamType::Aside, 24.f), Tok(EOneBrahamColour::LightGrey, 0.85f));

	// Why the player is back, said once, in the building's voice.
	if (!Text_Notice.IsEmpty())
	{
		const float Y = S.Y * 0.375f + 54.f;
		Box(C, FVector2D(X, Y - 10.f), FVector2D(6.f, 6.f), Tok(EOneBrahamColour::SafetyYellow));
		TextOnBaseline(C, X + 16.f, Y, Text_Notice.ToString(), Type(EOneBrahamType::Aside, 18.f), Tok(EOneBrahamColour::SignageWhite, 0.9f));
	}
}

void UOneBrahamMainMenuWidget::PaintPlayPage(const FPainter& Canvas, double Now, float Alpha) const
{
	using namespace OneBrahamMainMenuLayout;
	const FPainter C(Canvas, Alpha);
	const FVector2D S = C.Size();
	const float X = S.X * 0.06f;

	TextOnBaseline(C, X, S.Y * 0.20f, NSLOCTEXT("OneBraham", "Play_Eyebrow", "THE LIFT LOBBY IS OPEN").ToString(),
		Type(EOneBrahamType::Eyebrow, 13.f, 180), Tok(EOneBrahamColour::LightGrey, 0.7f));
	TextOnBaseline(C, X, S.Y * 0.265f, NSLOCTEXT("OneBraham", "Play_Heading", "Where to?").ToString(),
		Type(EOneBrahamType::Aside, 40.f), Tok(EOneBrahamColour::White));

	// The section of the focused mode, restroked whenever the mode changes.
	const FVector2D At(S.X * 0.60f, S.Y * 0.18f);
	const FVector2D Size(S.X * 0.34f, S.Y * 0.64f);
	TextOnBaseline(C, At.X + Size.X - 18.f, At.Y - 4.f,
		FString::Printf(TEXT("SECTION A\u2013A  \u00B7  %s"), *ModeLabel(FocusedMode).ToString()),
		Type(EOneBrahamType::TelemetryLight, 11.f, 160), Tok(EOneBrahamColour::LightGrey, 0.5f), 1.f);

	FOneBrahamMenuSectionParams Params;
	Params.RevealAt = ModeFocusedAt;
	if (FocusedMode == EOneBrahamMatchMode::FireDrill)
	{
		Params.LitFloors = (1u << 1) | (1u << 9) | (1u << 15);
		Params.ControlPoints = { 1, 9, 15 };
	}
	else
	{
		Params.LitFloors = 0x3FFFF;
		Params.TargetFloor = FOneBrahamMenuSection::NumFloors;
		Params.bRoute = true;
	}
	FOneBrahamMenuSection::Paint(C, At, Size, Params, Now);

	// What happens next, in the building's voice.
	if (!Text_PlayStatus.IsEmpty())
	{
		const float Y = S.Y * 0.30f + (CardHeight + CardGap) * ModeCards.Num() + 22.f + 108.f + 36.f;
		TextOnBaseline(C, X, Y, Text_PlayStatus.ToString(), Type(EOneBrahamType::Aside, 20.f), Tok(EOneBrahamColour::SignageWhite, 0.9f));
	}
}

void UOneBrahamMainMenuWidget::PaintHostPage(const FPainter& Canvas, double Now, float Alpha) const
{
	using namespace OneBrahamMainMenuLayout;
	const FPainter C(Canvas, Alpha);
	const FVector2D S = C.Size();
	const float X = S.X * 0.56f;
	const float Top = S.Y * 0.30f;
	const float Bottom = Top + LedgerRow * 5.f + 36.f + 60.f;

	// The LED edge: blue while the room is free, a sweep as it books, yellow
	// once it is in use, red when it cannot be had.
	FLinearColor Edge = Tok(EOneBrahamColour::SignalBlue);
	FString Chip = NSLOCTEXT("OneBraham", "Host_Available", "AVAILABLE").ToString();
	if (Booking == EBooking::InUse)
	{
		Edge = Tok(EOneBrahamColour::SafetyYellow);
		Chip = NSLOCTEXT("OneBraham", "Host_InUse", "IN USE \u00B7 Waiting for attendees").ToString();
	}
	else if (Booking == EBooking::Booking)
	{
		Chip = NSLOCTEXT("OneBraham", "Host_Booking", "BOOKING\u2026").ToString();
	}
	else if (Booking == EBooking::Failed)
	{
		Edge = Tok(EOneBrahamColour::EmergencyRed);
		Chip = NSLOCTEXT("OneBraham", "Host_Unavailable", "ROOM UNAVAILABLE").ToString();
	}
	const float EdgeTop = S.Y * 0.19f;
	Box(C, FVector2D(X - 22.f, EdgeTop), FVector2D(2.f, Bottom - EdgeTop), Edge);
	if (Booking != EBooking::Available)
	{
		const float T = static_cast<float>(FMath::Fmod(Now - BookingAt, 1.6) / 0.6);
		if (T < 1.f)
		{
			const float Y = FMath::Lerp(EdgeTop, Bottom - 60.f, UOneBrahamUIStyle::Ease(EOneBrahamEase::Lift, T));
			GradientV(C, FVector2D(X - 23.f, Y), FVector2D(4.f, 60.f), {
				{ 0.f, Tok(EOneBrahamColour::White, 0.f) }, { 0.5f, Tok(EOneBrahamColour::White, 0.95f) }, { 1.f, Tok(EOneBrahamColour::White, 0.f) } });
		}
	}

	TextOnBaseline(C, X + 16.f, S.Y * 0.21f, NSLOCTEXT("OneBraham", "Host_Eyebrow", "MEETING ROOMS \u00B7 GROUND FLOOR").ToString(),
		Type(EOneBrahamType::Eyebrow, 12.f, 180), Tok(EOneBrahamColour::LightGrey, 0.6f));
	const FVector2D Room = TextOnBaseline(C, X + 16.f, S.Y * 0.255f, NSLOCTEXT("OneBraham", "Host_Room", "ROOM G.07").ToString(),
		Type(EOneBrahamType::Verb, 32.f, -10), Tok(EOneBrahamColour::White));

	// The status chip, in a pill.
	const FSlateFontInfo ChipFont = Type(EOneBrahamType::Telemetry, 12.f, 80);
	const FVector2D ChipSize = Measure(Chip, ChipFont) + FVector2D(24.f, 10.f);
	const FVector2D ChipAt(X + 16.f + Room.X + 18.f, S.Y * 0.255f - 24.f);
	Rounded(C, ChipAt, ChipSize, ChipSize.Y * 0.5f, Fade(Edge, 0.12f), Edge, 1.f);
	TextOnBaseline(C, ChipAt.X + 12.f, ChipAt.Y + ChipSize.Y * 0.5f + 4.5f, Chip, ChipFont, Booking == EBooking::Available ? Tok(EOneBrahamColour::White) : Edge);

	if (!Text_HostNote.IsEmpty())
	{
		TextOnBaseline(C, X + 16.f, S.Y * 0.285f, Text_HostNote.ToString(), Type(EOneBrahamType::Aside, 16.f), Tok(EOneBrahamColour::LightGrey, 0.65f));
	}

	// The booking confirmation: the attendees arrive through the turnstiles.
	float Y = Bottom + 34.f;
	if (Booking == EBooking::InUse || Booking == EBooking::Booking)
	{
		const UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
		const UOneBrahamSaveSettings* Profile = GameInstance != nullptr ? GameInstance->GetProfile() : nullptr;
		const FString Name = Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName.ToUpper() : FString(TEXT("YOU"));
		TextOnBaseline(C, X + 16.f, Y, TypeOn(TEXT("HOST: YOU"), Now - BookingAt - 0.6, 0.02f), Type(EOneBrahamType::Telemetry, 12.f, 120),
			Tok(EOneBrahamColour::SafetyYellow));
		Y += 22.f;
		TextOnBaseline(C, X + 16.f, Y, TypeOn(FString::Printf(TEXT("TURNSTILE 2 \u00B7 BADGE ACCEPTED \u00B7 %s"), *Name), Now - BookingAt - 0.9, 0.02f),
			Type(EOneBrahamType::TelemetryLight, 12.f, 60), Tok(EOneBrahamColour::LightGrey, 0.8f));
		Y += 30.f;
	}
	if (!Text_HostStatus.IsEmpty())
	{
		TextOnBaseline(C, X + 16.f, Y, Text_HostStatus.ToString(), Type(EOneBrahamType::Aside, 16.f),
			Booking == EBooking::Failed ? Tok(EOneBrahamColour::EmergencyRed) : Tok(EOneBrahamColour::SignageWhite, 0.85f));
	}
}

void UOneBrahamMainMenuWidget::PaintJoinPage(const FPainter& Canvas, double Now, float Alpha) const
{
	using namespace OneBrahamMainMenuLayout;
	const FPainter C(Canvas, Alpha);
	const FVector2D S = C.Size();
	const float X = S.X * 0.46f;
	const float W = S.X * 0.46f;
	const float Top = S.Y * 0.30f;

	const UOneBrahamSessionSubsystem* Sessions = GetSessions();
	const bool bSearching = Sessions != nullptr && Sessions->IsSearching();

	TextOnBaseline(C, X + 16.f, S.Y * 0.205f, NSLOCTEXT("OneBraham", "Join_Eyebrow", "RECEPTION \u00B7 DEPARTURES").ToString(),
		Type(EOneBrahamType::Eyebrow, 12.f, 180), Tok(EOneBrahamColour::LightGrey, 0.6f));
	const FVector2D Heading = TextOnBaseline(C, X + 16.f, S.Y * 0.25f, NSLOCTEXT("OneBraham", "Join_Heading", "Today's meetings.").ToString(),
		Type(EOneBrahamType::Aside, 32.f), Tok(EOneBrahamColour::White));
	const FString Sub = bSearching ? NSLOCTEXT("OneBraham", "Join_Checking", "Checking the screens\u2026").ToString() : Text_JoinBody.ToString();
	TextOnBaseline(C, X + 16.f + Heading.X + 18.f, S.Y * 0.25f, Sub, Type(EOneBrahamType::Aside, 16.f), Tok(EOneBrahamColour::LightGrey, 0.6f));

	// The board's column heads, in the same cells as the rows.
	const FSlateFontInfo HeadFont = Type(EOneBrahamType::TelemetryLight, 10.f, 160);
	const float HeadY = Top - 10.f;
	const FLinearColor HeadInk = Tok(EOneBrahamColour::LightGrey, 0.45f);
	TextOnBaseline(C, X + 16.f, HeadY, TEXT("TIME"), HeadFont, HeadInk);
	TextOnBaseline(C, X + W * 0.13f, HeadY, TEXT("MEETING"), HeadFont, HeadInk);
	TextOnBaseline(C, X + W * 0.52f, HeadY, TEXT("TYPE"), HeadFont, HeadInk);
	TextOnBaseline(C, X + W * 0.68f, HeadY, TEXT("ATTENDEES"), HeadFont, HeadInk);
	TextOnBaseline(C, X + W * 0.82f, HeadY, TEXT("PING"), HeadFont, HeadInk);
	Box(C, FVector2D(X, Top - 2.f), FVector2D(W, 1.f), Tok(EOneBrahamColour::LightGrey, 0.2f));

	// While searching: a ribbon line scanning the board, top to bottom.
	if (bSearching)
	{
		const float T = static_cast<float>(FMath::Fmod(Now, 0.8) / 0.8);
		const float Y = Top + BoardRow * 7.f * T;
		Ribbon(C, FVector2D(X, Y), FVector2D(W, 1.f), 0.9f);
		GradientV(C, FVector2D(X, Y - 24.f), FVector2D(W, 24.f), {
			{ 0.f, Tok(EOneBrahamColour::RibbonB, 0.f) }, { 1.f, Tok(EOneBrahamColour::RibbonB, 0.08f) } });
	}
	else if (bSearchedOnce && BoardServers.Num() == 0)
	{
		TextOnBaseline(C, X + 16.f, Top + 44.f, NSLOCTEXT("OneBraham", "Join_EmptyLine", "No meetings today. Why not book one?").ToString(),
			Type(EOneBrahamType::Aside, 22.f), Tok(EOneBrahamColour::SignageWhite, 0.85f));
	}

	// Empty slots on the board: faint hairlines where rows would be.
	for (int32 Row = BoardServers.Num(); Row < 7; ++Row)
	{
		Box(C, FVector2D(X, Top + BoardRow * (Row + 1) - 1.f), FVector2D(W, 1.f), Tok(EOneBrahamColour::LightGrey, 0.06f));
	}

	// While searching the heading already says so; the line under the board
	// is for what the search found, or why a join failed.
	const bool bFailed = FailedRow != INDEX_NONE && Now - FailedAt < 6.0;
	const bool bEmptyBoard = bSearchedOnce && BoardServers.Num() == 0;
	if (!Text_JoinStatus.IsEmpty() && !bSearching && (!bEmptyBoard || bFailed))
	{
		TextOnBaseline(C, X + 16.f, Top + BoardRow * 7.f + 104.f, Text_JoinStatus.ToString(), Type(EOneBrahamType::Aside, 16.f),
			bFailed ? Tok(EOneBrahamColour::EmergencyRed) : Tok(EOneBrahamColour::LightGrey, 0.7f));
	}
}

void UOneBrahamMainMenuWidget::PaintCredits(const FPainter& Canvas, double Now, float Alpha) const
{
	const FPainter C(Canvas, Alpha);
	const FVector2D S = C.Size();
	const float CX = S.X * 0.5f;

	// The City goes down a stop under the roll, and a soft column of dark
	// down the middle carries it: the names read over rooftops, not into them.
	Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::OLEDBlack, 0.30f));
	GradientH(C, FVector2D::ZeroVector, S, {
		{ 0.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) }, { 0.28f, Tok(EOneBrahamColour::OLEDBlack, 0.50f) },
		{ 0.72f, Tok(EOneBrahamColour::OLEDBlack, 0.50f) }, { 1.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) } });

	// The roll: advanced here, from the real clock, at 40 px/s (160 held).
	const double Dt = CreditsLastPaint > 0.0 ? FMath::Clamp(Now - CreditsLastPaint, 0.0, 0.1) : 0.0;
	CreditsLastPaint = Now;

	struct FItem { int32 Kind; FString Text; };		// 0 role, 1 name, 2 body, 3 gap, 4 title, 5 final
	TArray<FItem> Items;
	Items.Add({ 4, FString(ONE_BRAHAM_DISPLAY_NAME) });
	Items.Add({ 2, NSLOCTEXT("OneBraham", "Credits_Sub", "A game in eighteen floors").ToString() });
	Items.Add({ 3, FString() });

	const UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
	const UOneBrahamSaveSettings* Profile = GameInstance != nullptr ? GameInstance->GetProfile() : nullptr;
	Items.Add({ 0, TEXT("STARRING") });
	Items.Add({ 1, Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName : FString(TEXT("You")) });
	Items.Add({ 2, TEXT("and eleven colleagues") });
	Items.Add({ 3, FString() });
	Items.Add({ 0, TEXT("THE BUILDING") });
	Items.Add({ 1, TEXT("One Braham, Aldgate East") });
	Items.Add({ 3, FString() });
	Items.Add({ 0, TEXT("ENGINEERING") });
	Items.Add({ 1, TEXT("One Braham Interactive") });
	Items.Add({ 3, FString() });
	Items.Add({ 0, TEXT("LICENCES") });
	Items.Add({ 3, FString() });

	const FSlateFontInfo BodyFont = Type(EOneBrahamType::Body, 18.f);
	const float WrapWidth = FMath::Min(980.f, S.X * 0.6f);
	for (const FOneBrahamCreditBlock& Block : Credits())
	{
		Items.Add({ 0, Block.Heading.ToString() });
		TArray<FString> Paragraphs;
		Block.Body.ToString().ParseIntoArray(Paragraphs, TEXT("\n"));
		for (const FString& Paragraph : Paragraphs)
		{
			// Word-wrap by measure.
			TArray<FString> Words;
			Paragraph.ParseIntoArrayWS(Words);
			FString Line;
			for (const FString& Word : Words)
			{
				const FString Trial = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
				if (!Line.IsEmpty() && Measure(Trial, BodyFont).X > WrapWidth)
				{
					Items.Add({ 2, Line });
					Line = Word;
				}
				else
				{
					Line = Trial;
				}
			}
			if (!Line.IsEmpty())
			{
				Items.Add({ 2, Line });
			}
		}
		Items.Add({ 3, FString() });
	}
	Items.Add({ 3, FString() });
	Items.Add({ 5, NSLOCTEXT("OneBraham", "Credits_Final", "Thank you for visiting One Braham. Please return your pass.").ToString() });

	auto Height = [](int32 Kind) { return Kind == 0 ? 26.f : Kind == 1 ? 44.f : Kind == 2 ? 28.f : Kind == 3 ? 56.f : Kind == 4 ? 90.f : 60.f; };
	float Total = 0.f;
	for (const FItem& Item : Items)
	{
		Total += Height(Item.Kind);
	}

	// The last card stops in the middle of the screen, held four seconds.
	const float Start = S.Y * 0.84f;
	const float Stop = Start + Total - S.Y * 0.5f - 30.f;
	if (CreditsRolled < Stop)
	{
		CreditsRolled = FMath::Min(Stop, CreditsRolled + static_cast<float>(Dt) * 40.f * (bCreditsFast ? 4.f : 1.f));
		CreditsStoppedAt = 0.0;
	}
	else if (CreditsStoppedAt <= 0.0)
	{
		CreditsStoppedAt = Now;
	}
	else if (Now - CreditsStoppedAt > 4.0 && Page == EPage::Credits && !bTransitioning)
	{
		// Back to reception: the visit is over.
		const_cast<UOneBrahamMainMenuWidget*>(this)->ShowPage(EPage::Home);
	}

	const float Top = S.Y * 0.13f;
	const float BottomEdge = S.Y * 0.87f;
	float Y = Start - CreditsRolled;
	for (const FItem& Item : Items)
	{
		const float H = Height(Item.Kind);
		const float Mid = Y + H * 0.5f;
		// Fade at the bars' edges.
		const float Edge = FMath::Clamp(FMath::Min(Mid - Top, BottomEdge - Mid) / 60.f, 0.f, 1.f);
		if (Edge > 0.f && !Item.Text.IsEmpty())
		{
			switch (Item.Kind)
			{
			case 0:
				TextOnBaseline(C, CX, Y + 18.f, Item.Text, Type(EOneBrahamType::TelemetryLight, 11.f, 220), Tok(EOneBrahamColour::LightGrey, 0.5f * Edge), 0.5f);
				break;
			case 1:
				TextOnBaseline(C, CX, Y + 36.f, Item.Text, Type(EOneBrahamType::Verb, 32.f, -10), Tok(EOneBrahamColour::White, Edge), 0.5f);
				break;
			case 2:
				TextOnBaseline(C, CX, Y + 20.f, Item.Text, BodyFont, Tok(EOneBrahamColour::LightGrey, 0.85f * Edge), 0.5f);
				break;
			case 4:
				TextOnBaseline(C, CX, Y + 76.f, Item.Text, Type(EOneBrahamType::Title, 72.f, -10), Tok(EOneBrahamColour::White, Edge), 0.5f);
				break;
			case 5:
				TextOnBaseline(C, CX, Y + 40.f, Item.Text, Type(EOneBrahamType::Aside, 30.f), Tok(EOneBrahamColour::SignageWhite, Edge), 0.5f);
				break;
			default:
				break;
			}
		}
		Y += H;
	}
}

void UOneBrahamMainMenuWidget::PaintSignOut(const FPainter& Canvas, double Now, float Alpha) const
{
	const FPainter C(Canvas, Alpha);
	const FVector2D S = C.Size();
	Box(C, FVector2D::ZeroVector, S, Tok(EOneBrahamColour::OLEDBlack, 0.45f));

	// The visitor badge, rising a few pixels as it arrives.
	const float Rise = 10.f * (1.f - Alpha);
	const FVector2D Card(420.f, 220.f);
	const FVector2D At(S.X * 0.5f - Card.X * 0.5f, S.Y * 0.5f - Card.Y * 0.5f + Rise);
	Rounded(C, At + FVector2D(0.f, 6.f), Card, 6.f, Tok(EOneBrahamColour::OLEDBlack, 0.35f));
	Rounded(C, At, Card, 6.f, Tok(EOneBrahamColour::SignageWhite));
	Rounded(C, At + FVector2D(Card.X * 0.5f - 20.f, 12.f), FVector2D(40.f, 6.f), 3.f, Tok(EOneBrahamColour::OLEDBlack, 0.6f));

	const FLinearColor Navy = Tok(EOneBrahamColour::Navy);
	TextOnBaseline(C, At.X + 28.f, At.Y + 46.f, TEXT("VISITOR"), Type(EOneBrahamType::TelemetryLight, 11.f, 200), Fade(Navy, 0.7f));
	TextOnBaseline(C, At.X + 28.f, At.Y + 88.f, NSLOCTEXT("OneBraham", "SignOut_Title", "Sign out?").ToString(), Type(EOneBrahamType::Verb, 30.f, -10), Navy);
	TextOnBaseline(C, At.X + 28.f, At.Y + 118.f, NSLOCTEXT("OneBraham", "SignOut_Body", "Your pass will be deactivated.").ToString(),
		Type(EOneBrahamType::Body, 16.f), Fade(Navy, 0.75f));

	// The photo square, top right, and the badge's stripe.
	Rounded(C, At + FVector2D(Card.X - 28.f - 52.f, 26.f), FVector2D(52.f, 60.f), 2.f, Tok(EOneBrahamColour::SignalBlue, 0.85f));
	TextOnBaseline(C, At.X + Card.X - 28.f - 26.f, At.Y + 64.f, TEXT("OB"), Type(EOneBrahamType::Verb, 18.f), Navy, 0.5f);
	Ribbon(C, At + FVector2D(0.f, Card.Y - 6.f), FVector2D(Card.X, 6.f));
}

// ==========================================================================
//  Painting: the bars, over everything
// ==========================================================================
void UOneBrahamMainMenuWidget::PaintBars(const FPainter& C, double Now) const
{
	const FVector2D S = C.Size();
	if (Cinema == nullptr)
	{
		return;
	}

	float BarH = S.Y * Cinema->CurrentLetterbox(Now);
	if (UOneBrahamCinemaOverlay::IsUltrawide(S))
	{
		// No bars on an ultrawide: their furniture sits on a gradient band.
		BarH = S.Y * 0.06f;
		GradientV(C, FVector2D::ZeroVector, FVector2D(S.X, BarH), { { 0.f, Tok(EOneBrahamColour::OLEDBlack, 0.7f) }, { 1.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) } });
		GradientV(C, FVector2D(0.f, S.Y - BarH), FVector2D(S.X, BarH), { { 0.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) }, { 1.f, Tok(EOneBrahamColour::OLEDBlack, 0.7f) } });
	}
	if (BarH < 30.f || bColdOpen)
	{
		return;
	}

	// A size up and a shade brighter than the spec's 11 px at 45%: at 720p
	// that was seven pixels of grey on black, there but not readable.
	const FSlateFontInfo Mono = Type(EOneBrahamType::TelemetryLight, 12.f, 180);
	const FLinearColor Faint = Tok(EOneBrahamColour::LightGrey, 0.62f);
	const float TopY = BarH * 0.5f + 4.f;

	// Top left: where you are, typed on at 14 ms a character on each change.
	const FString Crumb = BreadcrumbForPage(Page).ToString();
	TextOnBaseline(C, S.X * 0.03f, TopY, TypeOn(Crumb, Now - BreadcrumbAt, 0.014f), Mono, Faint);

	// Top right: the live clock and the meetings on this network.
	const UOneBrahamSessionSubsystem* Sessions = GetSessions();
	const int32 Meetings = Sessions != nullptr ? Sessions->GetLastResults().Num() : 0;
	const FString Network = Sessions != nullptr && !Sessions->IsLANOnly() ? TEXT("ONLINE") : TEXT("LAN");
	const FString Status = !bSearchedOnce ? Network
		: Meetings == 0 ? FString::Printf(TEXT("%s \u00B7 NO MEETINGS"), *Network)
		: FString::Printf(TEXT("%s \u00B7 %d %s NEARBY"), *Network, Meetings, Meetings == 1 ? TEXT("MEETING") : TEXT("MEETINGS"));
	TextOnBaseline(C, S.X * 0.97f, TopY, FString::Printf(TEXT("%s  \u00B7  %s"), *UOneBrahamUIStyle::LocalClock(true).ToString(), *Status),
		Mono, Faint, 1.f);

	// Bottom left: the keys, drawn as caps.
	const float BottomY = S.Y - BarH * 0.5f;
	struct FHint { const TCHAR* Key; const TCHAR* Pad; FText Label; };
	TArray<FHint> Hints;
	if (Page == EPage::Credits)
	{
		Hints.Add({ TEXT("ENTER"), TEXT("A"), NSLOCTEXT("OneBraham", "Hint_Hurry", "Hold to hurry") });
		Hints.Add({ TEXT("ESC"), TEXT("B"), NSLOCTEXT("OneBraham", "Hint_Back", "Back") });
	}
	else
	{
		Hints.Add({ TEXT("ENTER"), TEXT("A"), Page == EPage::Settings ? NSLOCTEXT("OneBraham", "Hint_Apply", "Apply") : NSLOCTEXT("OneBraham", "Hint_Select", "Select") });
		Hints.Add({ TEXT("ESC"), TEXT("B"), NSLOCTEXT("OneBraham", "Hint_Back", "Back") });
		if (Page == EPage::Settings)
		{
			Hints.Add({ TEXT("Q  E"), TEXT("LB  RB"), NSLOCTEXT("OneBraham", "Hint_Floors", "Floors") });
		}
		if (Page == EPage::Join)
		{
			Hints.Add({ TEXT("R"), TEXT("Y"), NSLOCTEXT("OneBraham", "Hint_Refresh", "Check again") });
		}
		if (Page == EPage::Host)
		{
			Hints.Add({ TEXT("\u2190 \u2192"), TEXT("\u2190 \u2192"), NSLOCTEXT("OneBraham", "Hint_Change", "Change") });
		}
	}
	const FSlateFontInfo CapFont = Type(EOneBrahamType::Telemetry, 11.f, 60);
	const FSlateFontInfo HintFont = Type(EOneBrahamType::Hint, 12.f, 40);
	float X = S.X * 0.03f;
	for (const FHint& Hint : Hints)
	{
		const FString Key = bGamepadHints ? Hint.Pad : Hint.Key;
		const FVector2D KeySize = Measure(Key, CapFont);
		const FVector2D Cap(FMath::Max(18.f, KeySize.X + 10.f), 18.f);
		Rounded(C, FVector2D(X, BottomY - 9.f), Cap, 2.f, FLinearColor::Transparent, Tok(EOneBrahamColour::LightGrey, 0.4f), 1.f);
		TextOnBaseline(C, X + Cap.X * 0.5f, BottomY + 4.f, Key, CapFont, Tok(EOneBrahamColour::LightGrey, 0.8f), 0.5f);
		X += Cap.X + 8.f;
		const FVector2D Label = TextOnBaseline(C, X, BottomY + 4.5f, Hint.Label.ToString(), HintFont, Tok(EOneBrahamColour::LightGrey, 0.7f));
		X += Label.X + 26.f;
	}

	// Bottom right: the build and the credit the backdrop needs. The streets
	// and buildings behind the menu are OpenStreetMap's: a produced work,
	// which the ODbL asks to carry the credit where it is seen.
	const FText Attribution = NSLOCTEXT("OneBraham", "Menu_OSM", "Map data \u00A9 OpenStreetMap contributors");
	TextOnBaseline(C, S.X * 0.97f, BottomY + 4.f, FString::Printf(TEXT("%s  \u00B7  %s"), *Text_Build.ToString(), *Attribution.ToString()),
		Type(EOneBrahamType::TelemetryLight, 11.f, 120), Tok(EOneBrahamColour::LightGrey, 0.5f), 1.f);
}

int32 UOneBrahamMainMenuWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const double Now = FPlatformTime::Seconds();

	// The director's clock: the cold open's beats and the HOME reel are
	// timelines read from paint, the one callback a visible widget gets.
	UOneBrahamMainMenuWidget* Mutable = const_cast<UOneBrahamMainMenuWidget*>(this);
	Mutable->AdvanceColdOpen(Now);
	Mutable->AdvanceReel(Now);

	// HOME steps aside when idle (the controls follow the canvas's opacity).
	const int32 HomeIndex = static_cast<int32>(EPage::Home);
	if (PageCanvases.IsValidIndex(HomeIndex) && PageCanvases[HomeIndex] != nullptr)
	{
		const float Idle = HomeIdleAlpha(Now);
		if (!FMath::IsNearlyEqual(PageCanvases[HomeIndex]->GetRenderOpacity(), Idle))
		{
			PageCanvases[HomeIndex]->SetRenderOpacity(Idle);
		}
	}

	++Layer;
	const FPainter C(OutDrawElements, AllottedGeometry, Layer);
	PaintBars(C, Now);

	// The cold open's first words: where, and when.
	if (bColdOpen)
	{
		const float T = static_cast<float>(Now - ColdOpenAt);
		if (T > 1.f && T < 3.3f)
		{
			const float Alpha = FMath::Min(FMath::Clamp((T - 1.f) / 0.3f, 0.f, 1.f), FMath::Clamp((3.3f - T) / 0.3f, 0.f, 1.f));
			const FVector2D S = C.Size();
			TextOnBaseline(C, S.X * 0.5f, S.Y * 0.5f + 4.f,
				FString::Printf(TEXT("ALDGATE EAST  \u00B7  LONDON E1  \u00B7  %s"), *UOneBrahamUIStyle::LocalClock(false).ToString()),
				Type(EOneBrahamType::TelemetryLight, 13.f, 180), Tok(EOneBrahamColour::LightGrey, 0.7f * Alpha), 0.5f);
		}
	}
	return C.TopLayer();
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
		Text_Build = FText::Format(NSLOCTEXT("OneBraham", "Menu_Build", "BUILD {0}.{1}.{2}  \u00B7  {3}"),
			FText::AsNumber(ONE_BRAHAM_VERSION_MAJOR), FText::AsNumber(ONE_BRAHAM_VERSION_MINOR),
			FText::AsNumber(ONE_BRAHAM_VERSION_PATCH),
			Sessions->IsLANOnly()
				? NSLOCTEXT("OneBraham", "Menu_BuildLAN", "LAN ONLY")
				: FText::FromName(Subsystem));
	}

	const UOneBrahamSessionSubsystem* Sessions = GetSessions();
	Text_HostNote = Sessions == nullptr || Sessions->IsLANOnly()
		? NSLOCTEXT("OneBraham", "Host_NoteLAN", "Steam is not running, so this is a LAN game: colleagues on this network can find it under JOIN.")
		: NSLOCTEXT("OneBraham", "Host_NoteOnline", "ONLINE lists the match on Steam. LAN keeps it to this network.");

	if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
	{
		if (const UOneBrahamMatchSettingsAsset* MatchSettings = GameInstance->GetMatchSettings(HostMode))
		{
			HostAttendees = FMath::Clamp(MatchSettings->GetExpectedPlayerCount(), 2, 12);
		}
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
		World->GetTimerManager().ClearTimer(TransitionTimer);
		World->GetTimerManager().ClearTimer(ActionTimer);
	}

	Super::NativeDestruct();
}

UOneBrahamSessionSubsystem* UOneBrahamMainMenuWidget::GetSessions() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UOneBrahamSessionSubsystem>() : nullptr;
}

// ==========================================================================
//  The director: camera, cold open, reel
// ==========================================================================
void UOneBrahamMainMenuWidget::AttachCamera(AOneBrahamFrontEndCamera* InCamera)
{
	Camera = InCamera;
	if (InCamera == nullptr)
	{
		return;
	}

	// Flat mode (Reduce Motion, or the Low preset): one held shot, no drift,
	// every transition a short cross-fade.
	const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	bFlat = UOneBrahamUIStyle::ReduceMotion() || (User != nullptr && User->GetQualityPreset() == EOneBrahamQualityPreset::Low);
	InCamera->SetFlat(bFlat);
	for (const TSharedPtr<FOneBrahamReveal>& Reveal : PageReveals)
	{
		Reveal->bFlat = bFlat;
	}

	// First launch: the cold open. Back from a match (or second time round):
	// HOME builds in on the crown.
	if (!AOneBrahamFrontEndGameMode::ColdOpenPlayed() && !AOneBrahamFrontEndGameMode::IsReturningFromMatch() && !bFlat)
	{
		AOneBrahamFrontEndGameMode::ColdOpenPlayed() = true;
		BeginColdOpen();
	}
	else
	{
		AOneBrahamFrontEndGameMode::ColdOpenPlayed() = true;
		InCamera->PlayShot(TEXT("crown"), EOneBrahamShotTransition::Cut);
		if (Cinema != nullptr)
		{
			Cinema->HoldBlack();
			Cinema->FadeFromBlack(0.6f);
		}
		BuildInHome(false);
	}
}

void UOneBrahamMainMenuWidget::DipToBlack(float OutS, float HoldS, float InS)
{
	if (Cinema != nullptr)
	{
		if (bFlat)
		{
			// The flat front end cross-fades instead: a short, shallow dip.
			Cinema->Dip(0.1f, 0.f, 0.15f);
			return;
		}
		Cinema->Dip(OutS, HoldS, InS);
	}
}

void UOneBrahamMainMenuWidget::HoldShot(FName Shot, EOneBrahamShotTransition Transition)
{
	if (bColdOpen)
	{
		SkipColdOpen();
	}
	bHoldShot = true;
	AOneBrahamFrontEndCamera* Cam = Camera.Get();
	if (Cam != nullptr && !Shot.IsNone())
	{
		Cam->PlayShot(Shot, Transition);
	}
}

void UOneBrahamMainMenuWidget::BeginColdOpen()
{
	bColdOpen = true;
	ColdOpenAt = FPlatformTime::Seconds();
	ColdOpenStage = 0;
	if (Cinema != nullptr)
	{
		Cinema->HoldBlack();
		Cinema->SetLetterbox(0.f, 0.f);
		Cinema->SuperStyle = EOneBrahamSuperStyle::ColdOpen;
	}
	for (UCanvasPanel* Canvas : PageCanvases)
	{
		if (Canvas != nullptr)
		{
			Canvas->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (AOneBrahamFrontEndCamera* Cam = Camera.Get())
	{
		Cam->PlayShot(TEXT("fins"), EOneBrahamShotTransition::Cut);
	}
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Cold open."));
}

void UOneBrahamMainMenuWidget::AdvanceColdOpen(double Now)
{
	if (!bColdOpen)
	{
		return;
	}
	const double T = Now - ColdOpenAt;
	AOneBrahamFrontEndCamera* Cam = Camera.Get();

	// 3.3 s: the fins, up out of black; the bars ease in; the first super.
	if (ColdOpenStage == 0 && T >= 3.3)
	{
		ColdOpenStage = 1;
		if (Cam != nullptr)
		{
			Cam->PlayShot(TEXT("fins"), EOneBrahamShotTransition::Cut);
		}
		if (Cinema != nullptr)
		{
			Cinema->FadeFromBlack(0.8f);
			Cinema->SetLetterbox(OneBrahamMainMenuLayout::Letterbox, 0.9f);
			Cinema->ShowSuper(FText::GetEmpty(), NSLOCTEXT("OneBraham", "Cold_Floors", "Eighteen floors."), FText::GetEmpty(), 3.6f);
		}
	}
	// 8.3 s: through black to the reception wall, which says ON BREAK.
	else if (ColdOpenStage == 1 && T >= 8.3)
	{
		ColdOpenStage = 2;
		if (Cam != nullptr)
		{
			Cam->PlayShot(TEXT("reception"), EOneBrahamShotTransition::Dip);
		}
	}
	else if (ColdOpenStage == 2 && T >= 8.7)
	{
		ColdOpenStage = 3;
		if (Cinema != nullptr)
		{
			Cinema->ShowSuper(FText::GetEmpty(), NSLOCTEXT("OneBraham", "Cold_Colleagues", "Twelve colleagues."), FText::GetEmpty(), 4.2f);
		}
	}
	// 14.3 s: cut to the crown; the last super; HOME builds in under it.
	else if (ColdOpenStage == 3 && T >= 14.3)
	{
		ColdOpenStage = 4;
		if (Cam != nullptr)
		{
			Cam->PlayShot(TEXT("crown"), EOneBrahamShotTransition::Cut);
		}
		if (Cinema != nullptr)
		{
			Cinema->ShowSuper(FText::GetEmpty(), NSLOCTEXT("OneBraham", "Cold_Roof", "One way out, from the roof."), FText::GetEmpty(), 2.0f);
		}
	}
	else if (ColdOpenStage == 4 && T >= 17.0)
	{
		ColdOpenStage = 5;
		bColdOpen = false;
		BuildInHome(true);
	}
}

void UOneBrahamMainMenuWidget::SkipColdOpen()
{
	if (!bColdOpen)
	{
		return;
	}
	bColdOpen = false;
	ColdOpenStage = 5;
	if (AOneBrahamFrontEndCamera* Cam = Camera.Get())
	{
		Cam->PlayShot(TEXT("crown"), EOneBrahamShotTransition::Cut);
	}
	if (Cinema != nullptr)
	{
		Cinema->ClearSuper();
		Cinema->HoldBlack();
		Cinema->FadeFromBlack(0.3f);
		Cinema->SetLetterbox(OneBrahamMainMenuLayout::Letterbox, 0.3f);
	}
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Cold open skipped."));
	BuildInHome(false);
}

void UOneBrahamMainMenuWidget::BuildInHome(bool bFromColdOpen)
{
	const double Now = FPlatformTime::Seconds();
	if (Cinema != nullptr)
	{
		Cinema->SetLetterbox(OneBrahamMainMenuLayout::Letterbox, bFromColdOpen ? 0.f : 0.6f);
	}

	PreviousPage = EPage::Home;
	Page = EPage::Home;
	for (int32 Index = 0; Index < PageCanvases.Num(); ++Index)
	{
		if (PageCanvases[Index] != nullptr)
		{
			PageCanvases[Index]->SetVisibility(Index == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	PageReveals[0]->Enter(Now + 0.1);
	HomeBuiltAt = Now + 0.1;
	BreadcrumbAt = Now;
	LastInputAt = Now;
	IdleWakeAt = Now - 1.0;
	NextReelAt = Now + OneBrahamMainMenuLayout::ReelSeconds;
	ReelIndex = 0;
	if (Sign_Play != nullptr)
	{
		Sign_Play->SetKeyboardFocus();
	}

	// One quiet look at the LAN, for the top bar's count of meetings nearby.
	if (!bSearchedOnce)
	{
		UOneBrahamSessionSubsystem* Sessions = GetSessions();
		if (Sessions != nullptr && Sessions->IsLANOnly() && !Sessions->IsSearching())
		{
			Sessions->FindSessions(true);
		}
	}
}

void UOneBrahamMainMenuWidget::AdvanceReel(double Now)
{
	using namespace OneBrahamMainMenuLayout;
	AOneBrahamFrontEndCamera* Cam = Camera.Get();
	if (Cam == nullptr || bColdOpen || bHoldShot || bFlat || Page != EPage::Home || bTransitioning || NextReelAt <= 0.0 || Now < NextReelAt)
	{
		return;
	}
	NextReelAt = Now + ReelSeconds;
	ReelIndex = (ReelIndex + 1) % UE_ARRAY_COUNT(ReelShots);
	Cam->PlayShot(ReelShots[ReelIndex], EOneBrahamShotTransition::Dip);
}

// ==========================================================================
//  Pages and focus
// ==========================================================================
void UOneBrahamMainMenuWidget::ShowPage(EPage InPage)
{
	if (PageCanvases.IsEmpty())
	{
		return;
	}
	TransitionTo(InPage);
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Main menu page: %d"), static_cast<int32>(InPage));
}

void UOneBrahamMainMenuWidget::TransitionTo(EPage InPage)
{
	if (bColdOpen)
	{
		SkipColdOpen();
	}

	const double Now = FPlatformTime::Seconds();
	const EPage Previous = Page;
	if (InPage == Previous && !bTransitioning)
	{
		if (UWidget* Target = GetDefaultFocusTarget())
		{
			Target->SetKeyboardFocus();
		}
		return;
	}

	if (Previous == EPage::Settings && InPage != EPage::Settings && Settings != nullptr)
	{
		// Left without BACK (a sign clicked with the mouse): keep the changes.
		Settings->ApplyIfDirty();
	}

	PreviousPage = Previous;
	Page = InPage;
	bTransitioning = true;
	BreadcrumbAt = Now + 0.14;
	PageReveals[static_cast<int32>(Previous)]->Leave(Now);

	// The camera dips to the page's shot when it is a different one.
	bool bDip = false;
	const FName Shot = ShotForPage(InPage);
	AOneBrahamFrontEndCamera* Cam = Camera.Get();
	if (Cam != nullptr && !Shot.IsNone() && !bHoldShot)
	{
		bDip = Cam->ResolveShot(Shot) != Cam->GetCurrentShot();
		if (bDip)
		{
			Cam->PlayShot(Shot, EOneBrahamShotTransition::Dip);
		}
	}
	if (Cinema != nullptr && Previous == EPage::Play && PressedLift == INDEX_NONE)
	{
		Cinema->SetLetterbox(OneBrahamMainMenuLayout::Letterbox, 0.3f);
	}

	const double InAt = Now + (bDip ? 0.2 : 0.14);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(TransitionTimer, FTimerDelegate::CreateWeakLambda(this, [this, InPage, InAt]()
		{
			CompleteTransition(InPage, InAt);
		}), bFlat ? 0.07f : 0.14f, false);
	}
	else
	{
		CompleteTransition(InPage, InAt);
	}
}

void UOneBrahamMainMenuWidget::CompleteTransition(EPage InPage, double InAt)
{
	bTransitioning = false;
	for (int32 Index = 0; Index < PageCanvases.Num(); ++Index)
	{
		if (PageCanvases[Index] != nullptr)
		{
			PageCanvases[Index]->SetVisibility(Index == static_cast<int32>(InPage) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	PageReveals[static_cast<int32>(InPage)]->Enter(InAt);
	const double Now = FPlatformTime::Seconds();

	switch (InPage)
	{
	case EPage::Home:
		NextReelAt = Now + OneBrahamMainMenuLayout::ReelSeconds;
		HomeBuiltAt = InAt;
		break;

	case EPage::Play:
		ModeFocusedAt = InAt;
		PressedLift = INDEX_NONE;
		break;

	case EPage::Join:
	{
		const UOneBrahamSessionSubsystem* Sessions = GetSessions();
		Text_JoinBody = Sessions == nullptr || Sessions->IsLANOnly()
			? NSLOCTEXT("OneBraham", "Join_BodyLAN", "On this network.")
			: NSLOCTEXT("OneBraham", "Join_BodyOnline", "On Steam, nearest first.");
		RefreshJoinList();
		break;
	}

	case EPage::Settings:
		if (Settings != nullptr)
		{
			// Read on every open: the pause menu may have changed them.
			Settings->SyncFromSettings();
		}
		break;

	case EPage::Credits:
		CreditsRolled = 0.f;
		CreditsLastPaint = 0.0;
		CreditsStoppedAt = 0.0;
		bCreditsFast = false;
		break;

	case EPage::SignOut:
		bSigningOut = false;
		break;

	default:
		break;
	}

	UWidget* Target = InPage == EPage::Home ? static_cast<UWidget*>(SignFor(PreviousPage)) : GetDefaultFocusTarget();
	if (Target != nullptr)
	{
		Target->SetKeyboardFocus();
	}
}

UOneBrahamPaintedControl* UOneBrahamMainMenuWidget::SignFor(EPage InPage) const
{
	switch (InPage)
	{
	case EPage::Host:		return Sign_Host;
	case EPage::Join:		return Sign_Join;
	case EPage::Settings:	return Sign_Settings;
	case EPage::Credits:	return Sign_Credits;
	case EPage::SignOut:	return Sign_Quit;
	default:				return Sign_Play;
	}
}

UWidget* UOneBrahamMainMenuWidget::GetDefaultFocusTarget() const
{
	switch (Page)
	{
	case EPage::Play:
	{
		const int32 Card = CardModes.IndexOfByKey(FocusedMode);
		return ModeCards.IsValidIndex(Card) ? ModeCards[Card].Get() : nullptr;
	}
	case EPage::Host:
		return Button_HostGo;
	case EPage::Join:
		return JoinRowButtons.Num() > 0 ? static_cast<UWidget*>(JoinRowButtons[0].Get()) : static_cast<UWidget*>(Button_JoinRefresh.Get());
	case EPage::Settings:
		return Settings != nullptr ? Settings->GetDefaultFocusTarget() : nullptr;
	case EPage::Credits:
		return CreditsFocus;
	case EPage::SignOut:
		return Button_Stay;
	default:
		return Sign_Play;
	}
}

FReply UOneBrahamMainMenuWidget::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	// The controller focuses the menu as a whole; the first press of a d-pad
	// should move from PLAY, not from nowhere.
	if (!bColdOpen)
	{
		if (UWidget* Target = GetDefaultFocusTarget())
		{
			return FReply::Handled().SetUserFocus(Target->TakeWidget(), EFocusCause::SetDirectly);
		}
	}
	return Super::NativeOnFocusReceived(InGeometry, InFocusEvent);
}

void UOneBrahamMainMenuWidget::NoteInput(bool bGamepad)
{
	const double Now = FPlatformTime::Seconds();
	if (IsIdle(Now))
	{
		IdleWakeAt = Now;
	}
	LastInputAt = Now;
	bGamepadHints = bGamepad;
}

FReply UOneBrahamMainMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const double Now = FPlatformTime::Seconds();
	const FKey Key = InKeyEvent.GetKey();
	const bool bWasIdle = IsIdle(Now);
	NoteInput(Key.IsGamepadKey());

	// The cold open: any key after the first beat skips it; before, nothing.
	if (bColdOpen)
	{
		if (Now - ColdOpenAt >= 1.2)
		{
			SkipColdOpen();
		}
		return FReply::Handled();
	}

	// Waking from idle: the key is consumed, not acted on.
	if (bWasIdle)
	{
		return FReply::Handled();
	}

	if (Page == EPage::Credits && !bTransitioning)
	{
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Virtual_Accept)
		{
			bCreditsFast = true;
			return FReply::Handled();
		}
		ShowPage(EPage::Home);
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UOneBrahamMainMenuWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Virtual_Accept)
	{
		bCreditsFast = false;
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

FReply UOneBrahamMainMenuWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const double Now = FPlatformTime::Seconds();
	const bool bWasIdle = IsIdle(Now);
	NoteInput(false);
	if (bColdOpen)
	{
		if (Now - ColdOpenAt >= 1.2)
		{
			SkipColdOpen();
		}
		return FReply::Handled();
	}
	if (bWasIdle)
	{
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UOneBrahamMainMenuWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		NoteInput(false);
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UOneBrahamMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::BackSpace)
	{
		if (Page == EPage::Home)
		{
			// Escape at reception does not leave the building: it points at the door.
			if (Sign_Quit != nullptr)
			{
				Sign_Quit->SetKeyboardFocus();
			}
			return FReply::Handled();
		}
		if (Page == EPage::SignOut)
		{
			ShowPage(EPage::Home);
			return FReply::Handled();
		}
		GoBack();
		return FReply::Handled();
	}

	if (Page == EPage::Settings && Settings != nullptr
		&& (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder || Key == EKeys::Q || Key == EKeys::E))
	{
		Settings->CycleTab(Key == EKeys::Gamepad_RightShoulder || Key == EKeys::E ? 1 : -1);
		return FReply::Handled();
	}

	if (Page == EPage::Join && (Key == EKeys::R || Key == EKeys::Gamepad_FaceButton_Top))
	{
		RefreshJoinList();
		return FReply::Handled();
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
	Text_Notice = Notice;
	if (!Notice.IsEmpty())
	{
		UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Main menu notice: %s"), *Notice.ToString());
	}
}

void UOneBrahamMainMenuWidget::SetStatus(FText& Line, const FText& Text)
{
	Line = Text;
}

void UOneBrahamMainMenuWidget::FocusMode(EOneBrahamMatchMode Mode)
{
	const int32 Card = CardModes.IndexOfByKey(Mode);
	if (ModeCards.IsValidIndex(Card) && ModeCards[Card] != nullptr)
	{
		ModeCards[Card]->SetKeyboardFocus();
	}
}

// ==========================================================================
//  Actions
// ==========================================================================
void UOneBrahamMainMenuWidget::RideLift(EOneBrahamMatchMode Mode)
{
	if (bBusy || PressedLift == 0)
	{
		return;
	}

	// The lift: the up-chime, the bars close in, the camera steps forward and
	// the lens opens, then black, then the lift card (the loading page).
	PressedLift = 0;
	if (Cinema != nullptr)
	{
		Cinema->SetLetterbox(0.18f, 0.6f);
	}
	if (AOneBrahamFrontEndCamera* Cam = Camera.Get())
	{
		Cam->PushIn(200.f, 36.f, 1.1f);
	}
	UWorld* World = GetWorld();
	if (World == nullptr || bFlat)
	{
		PlaySolo(Mode);
		return;
	}
	World->GetTimerManager().SetTimer(ActionTimer, FTimerDelegate::CreateWeakLambda(this, [this, Mode]()
	{
		if (Cinema != nullptr)
		{
			Cinema->Dip(0.3f, 30.f, 0.2f);
		}
		if (UWorld* Inner = GetWorld())
		{
			Inner->GetTimerManager().SetTimer(ActionTimer, FTimerDelegate::CreateWeakLambda(this, [this, Mode]()
			{
				PlaySolo(Mode);
			}), 0.32f, false);
		}
	}), 0.8f, false);
}

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

	AOneBrahamFrontEndGameMode::NoteLeavingForMatch();
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

	bBusy = true;
	Booking = EBooking::Booking;
	BookingAt = FPlatformTime::Seconds();
	SetStatus(Text_HostStatus, NSLOCTEXT("OneBraham", "Host_Status", "Opening the floor..."));

	// "<Name>'s floor": what the JOIN list on the other machines shows.
	const UOneBrahamSaveSettings* Profile = GameInstance->GetProfile();
	const FString Name = Profile != nullptr && !Profile->PlayerName.IsEmpty() ? Profile->PlayerName : FString(TEXT("A colleague"));
	const FString ServerName = FString::Printf(TEXT("%s's floor"), *Name);

	const UOneBrahamMatchSettingsAsset* MatchSettings = GameInstance->GetMatchSettings(Mode);
	const int32 Expected = MatchSettings != nullptr ? MatchSettings->GetExpectedPlayerCount() : 12;
	// The ledger's ATTENDEES, when the host changed it from the mode's own count.
	const int32 MaxPlayers = HostAttendees >= 2 ? FMath::Min(HostAttendees, FMath::Max(Expected, 2)) : Expected;

	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] HOST %s %s as '%s' (%d slots)."), *OneBrahamSessionRules::ModeKey(Mode),
		bHostLAN ? TEXT("LAN") : TEXT("online"), *ServerName, MaxPlayers);

	AOneBrahamFrontEndGameMode::NoteLeavingForMatch();
	GameInstance->HostListenServer(Mode, MaxPlayers, bHostLAN, ServerName);
}

void UOneBrahamMainMenuWidget::HandleSessionCreated(bool bWasSuccessful, bool bLAN)
{
	Booking = EBooking::InUse;
	BookingAt = FPlatformTime::Seconds();
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
	bSearchedOnce = true;
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

	const bool bHadFocus = Page == EPage::Join && Button_JoinRefresh != nullptr && Button_JoinRefresh->HasKeyboardFocus();

	// Rebuilt whole: the board changes only when a search lands.
	JoinRows->ClearChildren();
	JoinRowButtons.Reset();
	BoardServers = Servers;
	BoardTimes.Reset();
	BoardArrivedAt = FPlatformTime::Seconds();
	JoiningRow = INDEX_NONE;
	const FString Clock = UOneBrahamUIStyle::LocalClock(false).ToString();

	const TSharedPtr<FOneBrahamReveal>& Reveal = PageReveals[static_cast<int32>(EPage::Join)];
	for (int32 Index = 0; Index < Servers.Num() && Index < 7; ++Index)
	{
		const FOneBrahamServerInfo& Info = Servers[Index];
		BoardTimes.Add(Clock);

		UOneBrahamPaintedControl* Row = NewControl(FVector2D(0.f, BoardRow), true, Reveal, 3 + Index);
		const FString Time = Clock;
		const FString Name = Info.ServerName;
		const FString Mode = ModeLabel(Info.Mode).ToString();
		const FString Attendees = FString::Printf(TEXT("%d/%d"), FMath::Max(0, Info.CurrentPlayers), FMath::Max(0, Info.MaxPlayers));
		const FString Ping = PingLabel(Info).ToString();
		const int32 Bars = PingBars(Info.PingMs);
		const bool bFull = IsFull(Info);
		Row->PaintFn = [this, Index, Time, Name, Mode, Attendees, Ping, Bars, bFull](const FPainter& C, const FOneBrahamControlState& State)
		{
			const FVector2D Size = C.Size();
			const float W = Size.X;
			const bool bJoining = JoiningRow == Index;
			const bool bFailed = FailedRow == Index && State.Now - FailedAt < 1.2;
			const bool bInverted = State.bLit || bJoining;

			if (bInverted)
			{
				Box(C, FVector2D::ZeroVector, Size, Tok(EOneBrahamColour::SignageWhite));
			}
			Box(C, FVector2D(0.f, Size.Y - 1.f), FVector2D(W, 1.f), Tok(EOneBrahamColour::LightGrey, 0.10f));

			const FLinearColor Ink = bFailed ? Tok(EOneBrahamColour::EmergencyRed)
				: bInverted ? Tok(EOneBrahamColour::Navy) : Tok(EOneBrahamColour::White, bFull ? 0.45f : 1.f);
			const FLinearColor Soft = bInverted ? Tok(EOneBrahamColour::Navy, 0.75f) : Tok(EOneBrahamColour::LightGrey, bFull ? 0.4f : 0.75f);

			// Split-flap: each mono character cycles three glyphs, 30 ms each,
			// left to right, before it settles. Archivo simply fades in.
			const double Since = State.Now - BoardArrivedAt - 0.05 * Index;
			const bool bFlap = !UOneBrahamUIStyle::ReduceMotion();
			auto Flap = [Since, bFlap, Index](const FString& Text) -> FString
			{
				if (!bFlap || Since > 0.09 + 0.03 * Text.Len())
				{
					return Text;
				}
				static const TCHAR Glyphs[] = TEXT("0123456789ABCDEFHJKLMNPRSTUVWXYZ:/");
				FString Out = Text;
				for (int32 Char = 0; Char < Text.Len(); ++Char)
				{
					if (Since < 0.09 + 0.03 * Char && Text[Char] != TEXT(' '))
					{
						const int32 Seed = FMath::FloorToInt(FMath::Max(0.0, Since) / 0.03) * 31 + Char * 7 + Index * 13;
						Out[Char] = Glyphs[Seed % (UE_ARRAY_COUNT(Glyphs) - 1)];
					}
				}
				return Out;
			};
			const float NameAlpha = FMath::Clamp(static_cast<float>(Since / 0.2), 0.f, 1.f);

			const float Baseline = 30.f;
			TextOnBaseline(C, 16.f, Baseline, Flap(Time), Type(EOneBrahamType::TelemetryLight, 13.f), Soft);
			TextOnBaseline(C, W * 0.13f, Baseline, Name, Type(EOneBrahamType::Eyebrow, 17.f), Fade(Ink, NameAlpha));
			TextOnBaseline(C, W * 0.52f, Baseline, Flap(Mode), Type(EOneBrahamType::TelemetryLight, 11.f, 120), Soft);
			TextOnBaseline(C, W * 0.68f, Baseline, Flap(bFull ? Attendees + TEXT(" FULL") : Attendees), Type(EOneBrahamType::Telemetry, 14.f), Ink);

			if (bJoining)
			{
				TextOnBaseline(C, W * 0.82f, Baseline, TEXT("SIGNING IN"), Type(EOneBrahamType::Telemetry, 12.f, 80), Ink);
			}
			else
			{
				// Ping: bars as well as colour (3 good, 2 fair, 1 poor).
				const FLinearColor BarColour = bInverted ? Tok(EOneBrahamColour::Navy)
					: Bars >= 3 ? Tok(EOneBrahamColour::LightGrey) : Bars == 2 ? Tok(EOneBrahamColour::SafetyYellow) : Tok(EOneBrahamColour::EmergencyRed);
				for (int32 Bar = 0; Bar < 3; ++Bar)
				{
					const float H = 4.f + Bar * 4.f;
					Box(C, FVector2D(W * 0.82f + Bar * 5.f, Baseline - H), FVector2D(3.f, H), Bar < Bars ? BarColour : Fade(BarColour, 0.2f));
				}
				TextOnBaseline(C, W * 0.82f + 22.f, Baseline, Flap(Ping), Type(EOneBrahamType::Telemetry, 14.f), Ink);
			}

			if (State.bLit && !bJoining)
			{
				TextOnBaseline(C, W - 16.f, Baseline, TEXT("JOIN \u2192"), Type(EOneBrahamType::Verb, 14.f, 40), Tok(EOneBrahamColour::Navy), 1.f);
			}
		};
		Row->OnActivate = [this, Index]() { JoinRow(Index); };
		JoinRows->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Fill);
		JoinRowButtons.Add(Row);
	}

	TArray<UOneBrahamPaintedControl*> Column;
	for (const TObjectPtr<UOneBrahamPaintedControl>& Row : JoinRowButtons)
	{
		Column.Add(Row);
	}
	if (Button_JoinRefresh != nullptr)
	{
		Column.Add(Button_JoinRefresh);
	}
	if (Button_JoinBook != nullptr)
	{
		const bool bEmpty = Servers.Num() == 0;
		Button_JoinBook->SetVisibility(bEmpty ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bEmpty)
		{
			Column.Insert(Button_JoinBook, 0);
		}
	}
	LinkVertical(Column, true);

	// Focus follows the board onto its first row for a pad, if it was on the
	// refresh link when the search landed.
	if (bHadFocus && JoinRowButtons.Num() > 0)
	{
		JoinRowButtons[0]->SetKeyboardFocus();
	}
}

void UOneBrahamMainMenuWidget::RefreshJoinRowsFocus()
{
	if (Page == EPage::Join && JoinRowButtons.Num() > 0)
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
		FailedRow = RowIndex;
		FailedAt = FPlatformTime::Seconds();
		return;
	}

	const FOneBrahamServerInfo& Info = Sessions->GetLastResults()[RowIndex];
	if (IsFull(Info))
	{
		// Said here because nothing else will: NULL joins a full session
		// without a word, and the host would seat a thirteenth player.
		SetStatus(Text_JoinStatus, NSLOCTEXT("OneBraham", "Join_Full", "That floor is full."));
		FailedRow = RowIndex;
		FailedAt = FPlatformTime::Seconds();
		UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] JOIN row %d refused: full (%d/%d)."), RowIndex, Info.CurrentPlayers, Info.MaxPlayers);
		return;
	}

	bBusy = true;
	JoiningRow = RowIndex;
	SetStatus(Text_JoinStatus, FText::Format(NSLOCTEXT("OneBraham", "Join_Joining", "Joining {0}..."), FText::FromString(Info.ServerName)));
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] JOIN row %d: '%s' (%s, %d/%d, %d ms)."), RowIndex, *Info.ServerName,
		*OneBrahamSessionRules::ModeKey(Info.Mode), Info.CurrentPlayers, Info.MaxPlayers, Info.PingMs);

	AOneBrahamFrontEndGameMode::NoteLeavingForMatch();
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
	FailedRow = JoiningRow;
	FailedAt = FPlatformTime::Seconds();
	JoiningRow = INDEX_NONE;
	SetStatus(Text_JoinStatus, Reason.IsEmpty() ? NSLOCTEXT("OneBraham", "Join_Failed", "Could not join. The host did not answer.") : Reason);
}

void UOneBrahamMainMenuWidget::HandleDialCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		DialExtension(Text.ToString());
	}
}

void UOneBrahamMainMenuWidget::DialExtension(const FString& Address)
{
	FString Host = Address.TrimStartAndEnd();
	if (Host.IsEmpty() || bBusy)
	{
		return;
	}
	int32 Port = 7777;
	FString PortText;
	if (Host.Split(TEXT(":"), &Host, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		Port = FMath::Clamp(FCString::Atoi(*PortText), 1, 65535);
	}
	UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
	if (GameInstance == nullptr)
	{
		return;
	}
	bBusy = true;
	SetStatus(Text_JoinStatus, FText::Format(NSLOCTEXT("OneBraham", "Join_Dialling", "Dialling {0}..."), FText::FromString(Host)));
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] JOIN by address %s:%d."), *Host, Port);
	AOneBrahamFrontEndGameMode::NoteLeavingForMatch();
	GameInstance->JoinServerByAddress(Host, Port);
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
