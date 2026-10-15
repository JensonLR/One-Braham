// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamPauseMenuWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/OneBrahamSettingsWidget.h"

namespace OneBrahamPauseLayout
{
	constexpr float ColumnWidth = 440.f;
	constexpr float DialogWidth = 560.f;
	constexpr float SettingsWidth = 1020.f;

	FLinearColor NavyAlpha(float Alpha)
	{
		const FLinearColor Navy = UOneBrahamUIStyle::Navy();
		return FLinearColor(Navy.R, Navy.G, Navy.B, Alpha);
	}
}

UOneBrahamPauseMenuWidget::UOneBrahamPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamPauseMenuWidget::BuildLayout()
{
	using namespace OneBrahamPauseLayout;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PauseRoot"));
	WidgetTree->RootWidget = Root;

	auto Fill = [](UOverlaySlot* OverlaySlot)
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	};

	// The world stays faintly visible behind the menu: in a networked match it is
	// still happening, and a player should see a colleague arrive at their desk.
	UImage* Dim = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	Dim->SetBrush(UOneBrahamUIStyle::SolidBrush(NavyAlpha(0.72f)));
	Fill(Root->AddChildToOverlay(Dim));

	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("PausePages"));
	Fill(Root->AddChildToOverlay(Pages));

	// --- page 0: the menu column -----------------------------------------------
	// A full-height column on the left, like the directory panel beside a lift:
	// the eye lands top-left, and that is where RESUME is.
	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	Menu->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Pause_Kicker", "ONE BRAHAM  /  LEVEL G  /  RECEPTION"),
		EOneBrahamTextSize::Micro, InkMuted()));
	Menu->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Pause_Title", "PAUSED"),
		EOneBrahamTextSize::Display, Ink(), true))->SetPadding(FMargin(0.f, 6.f, 0.f, 4.f));

	Text_Subtitle = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Text_Subtitle->SetAutoWrapText(true);
	Menu->AddChildToVerticalBox(Text_Subtitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 36.f));

	auto AddMenuButton = [this, Menu](const FText& Label, TFunction<void()> OnClicked) -> UButton*
	{
		UButton* Button = MakeButton(Label, MoveTemp(OnClicked), EOneBrahamTextSize::Medium, true, nullptr, FMargin(18.f, 12.f));
		UVerticalBoxSlot* ButtonSlot = Menu->AddChildToVerticalBox(Button);
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		return Button;
	};

	Button_Resume = AddMenuButton(NSLOCTEXT("OneBraham", "Pause_Resume", "RESUME"), [this]() { RequestResume(); });
	Button_Settings = AddMenuButton(NSLOCTEXT("OneBraham", "Pause_Settings", "SETTINGS"), [this]() { ShowSettingsPage(); });

	// A gap before the two exits, so the hand that reaches for RESUME does not
	// land on LEAVE MATCH.
	Menu->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 14.f, 0.f, 24.f));

	AddMenuButton(NSLOCTEXT("OneBraham", "Pause_Leave", "LEAVE MATCH"), [this]() { ShowConfirm(EPendingConfirm::Leave); });
	AddMenuButton(NSLOCTEXT("OneBraham", "Pause_Quit", "QUIT TO DESKTOP"), [this]() { ShowConfirm(EPendingConfirm::Quit); });

	UVerticalBoxSlot* Gap = Menu->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));
	Gap->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Menu->AddChildToVerticalBox(MakeLabel(NSLOCTEXT("OneBraham", "Pause_Footer", "ESC  BACK          ENTER  SELECT"),
		EOneBrahamTextSize::Micro, InkFaint()));

	UBorder* Column = MakePanel(Menu, 0.96f, FMargin(44.f, 56.f, 44.f, 36.f));
	Column->SetBrush(UOneBrahamUIStyle::PanelBrush(0.96f, 0.f));

	UHorizontalBox* MainRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	MainRow->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)))->SetVerticalAlignment(VAlign_Fill);
	MainRow->AddChildToHorizontalBox(MakeCell(Column, ColumnWidth))->SetVerticalAlignment(VAlign_Fill);
	MainPage = MainRow;
	Pages->AddChild(MainRow);

	// --- page 1: settings ------------------------------------------------------
	UOverlay* SettingsPage = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

	Settings = CreateWidget<UOneBrahamSettingsWidget>(this, UOneBrahamSettingsWidget::StaticClass());
	if (Settings != nullptr)
	{
		Settings->OnBackRequested.BindUObject(this, &UOneBrahamPauseMenuWidget::ShowMainPage);

		UHorizontalBox* Framed = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Framed->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)))->SetVerticalAlignment(VAlign_Fill);
		Framed->AddChildToHorizontalBox(MakePanel(Settings, 0.97f, FMargin(32.f, 28.f)))
			->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UOverlaySlot* SettingsSlot = SettingsPage->AddChildToOverlay(MakeCell(Framed, SettingsWidth));
		SettingsSlot->SetHorizontalAlignment(HAlign_Center);
		SettingsSlot->SetVerticalAlignment(VAlign_Center);
	}

	Pages->AddChild(SettingsPage);

	// --- confirmation dialog -----------------------------------------------------
	UOverlay* Confirm = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ConfirmLayer"));

	UImage* ConfirmDim = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	ConfirmDim->SetBrush(UOneBrahamUIStyle::SolidBrush(NavyAlpha(0.6f)));
	Fill(Confirm->AddChildToOverlay(ConfirmDim));

	UVerticalBox* Dialog = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text_ConfirmTitle = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Large, Ink(), true);
	Text_ConfirmBody = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Text_ConfirmBody->SetAutoWrapText(true);
	Dialog->AddChildToVerticalBox(Text_ConfirmTitle);
	Dialog->AddChildToVerticalBox(Text_ConfirmBody)->SetPadding(FMargin(0.f, 10.f, 0.f, 28.f));

	UHorizontalBox* Answers = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UTextBlock* AcceptLabel = nullptr;
	Answers->AddChildToHorizontalBox(MakeButton(FText::GetEmpty(), [this]() { AcceptConfirm(); },
		EOneBrahamTextSize::Small, true, &AcceptLabel))->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	Text_ConfirmAccept = AcceptLabel;
	Button_ConfirmCancel = MakeButton(NSLOCTEXT("OneBraham", "Pause_ConfirmCancel", "STAY"),
		[this]() { CloseConfirm(); }, EOneBrahamTextSize::Small);
	Answers->AddChildToHorizontalBox(Button_ConfirmCancel);
	Dialog->AddChildToVerticalBox(Answers);

	UHorizontalBox* DialogFrame = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	DialogFrame->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)))->SetVerticalAlignment(VAlign_Fill);
	DialogFrame->AddChildToHorizontalBox(MakePanel(Dialog, 0.99f, FMargin(32.f, 28.f)))
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UOverlaySlot* DialogSlot = Confirm->AddChildToOverlay(MakeCell(DialogFrame, DialogWidth));
	DialogSlot->SetHorizontalAlignment(HAlign_Center);
	DialogSlot->SetVerticalAlignment(VAlign_Center);

	Confirm->SetVisibility(ESlateVisibility::Collapsed);
	ConfirmLayer = Confirm;
	Fill(Root->AddChildToOverlay(Confirm));

	SetVisibility(ESlateVisibility::Collapsed);
}

// ==========================================================================
//  Open / close
// ==========================================================================
void UOneBrahamPauseMenuWidget::OpenMenu()
{
	bOpen = true;
	SetVisibility(ESlateVisibility::Visible);

	if (Text_Subtitle != nullptr)
	{
		const UWorld* World = GetWorld();
		const bool bStandalone = World == nullptr || World->GetNetMode() == NM_Standalone;

		// Said once, plainly, because players assume Escape pauses everything.
		Text_Subtitle->SetText(bStandalone
			? NSLOCTEXT("OneBraham", "Pause_SubtitleStandalone", "The building is holding still until you return.")
			: NSLOCTEXT("OneBraham", "Pause_SubtitleOnline",
				"The match is still running. Your colleagues have been told you are in a meeting."));
	}

	CloseConfirm();
	ShowMainPage();
}

void UOneBrahamPauseMenuWidget::CloseMenu()
{
	// Closing from the settings page (Start pressed again, say) keeps whatever
	// the player changed: they moved the slider, so they meant it.
	if (Settings != nullptr && Pages != nullptr && Pages->GetActiveWidgetIndex() == 1)
	{
		Settings->ApplyIfDirty();
	}

	bOpen = false;
	PendingConfirm = EPendingConfirm::None;

	if (ConfirmLayer != nullptr)
	{
		ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

UWidget* UOneBrahamPauseMenuWidget::GetDefaultFocusTarget() const
{
	if (PendingConfirm != EPendingConfirm::None)
	{
		return Button_ConfirmCancel;
	}

	if (Pages != nullptr && Pages->GetActiveWidgetIndex() == 1 && Settings != nullptr)
	{
		return Settings->GetDefaultFocusTarget();
	}

	return Button_Resume;
}

FReply UOneBrahamPauseMenuWidget::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	// The controller focuses the menu as a whole; the menu passes it on to the
	// control that should have it, so the first press of a d-pad moves from
	// RESUME rather than from nowhere.
	if (UWidget* Target = GetDefaultFocusTarget())
	{
		return FReply::Handled().SetUserFocus(Target->TakeWidget(), EFocusCause::SetDirectly);
	}

	return Super::NativeOnFocusReceived(InGeometry, InFocusEvent);
}

FReply UOneBrahamPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		GoBack();
		return FReply::Handled();
	}

	// Shoulder buttons walk the settings tabs: the cheapest gamepad navigation
	// there is, and the one every console player tries first.
	const bool bOnSettings = Pages != nullptr && Pages->GetActiveWidgetIndex() == 1
		&& PendingConfirm == EPendingConfirm::None && Settings != nullptr;

	if (bOnSettings && (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder))
	{
		Settings->CycleTab(Key == EKeys::Gamepad_RightShoulder ? 1 : -1);
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ==========================================================================
//  Pages
// ==========================================================================
void UOneBrahamPauseMenuWidget::ShowMainPage()
{
	if (Pages == nullptr)
	{
		return;
	}

	const bool bFromSettings = Pages->GetActiveWidgetIndex() == 1;
	Pages->SetActiveWidgetIndex(0);

	// Back from settings lands on SETTINGS, where the player left; anything else
	// lands on RESUME.
	UButton* Target = bFromSettings ? Button_Settings.Get() : Button_Resume.Get();
	if (Target != nullptr && bOpen)
	{
		Target->SetKeyboardFocus();
	}
}

void UOneBrahamPauseMenuWidget::ShowSettingsPage()
{
	if (Pages == nullptr || Settings == nullptr)
	{
		return;
	}

	// Values are read on every open, not once: the console, the main menu or the
	// self-mute key may have changed them since.
	Settings->SyncFromSettings();
	Pages->SetActiveWidgetIndex(1);
	Settings->FocusDefault();
}

void UOneBrahamPauseMenuWidget::ShowConfirm(EPendingConfirm Kind)
{
	PendingConfirm = Kind;

	const UWorld* World = GetWorld();
	const bool bStandalone = World == nullptr || World->GetNetMode() == NM_Standalone;

	if (Kind == EPendingConfirm::Leave)
	{
		Text_ConfirmTitle->SetText(NSLOCTEXT("OneBraham", "Pause_LeaveTitle", "LEAVE THE MATCH?"));
		Text_ConfirmBody->SetText(bStandalone
			? NSLOCTEXT("OneBraham", "Pause_LeaveBodyStandalone", "You will be returned to the main menu. This session will not be kept.")
			: NSLOCTEXT("OneBraham", "Pause_LeaveBodyOnline",
				"You will be signed out and returned to the main menu. Your squad will carry on one colleague short."));
		Text_ConfirmAccept->SetText(NSLOCTEXT("OneBraham", "Pause_LeaveAccept", "LEAVE"));
	}
	else
	{
		Text_ConfirmTitle->SetText(NSLOCTEXT("OneBraham", "Pause_QuitTitle", "QUIT TO DESKTOP?"));
		Text_ConfirmBody->SetText(NSLOCTEXT("OneBraham", "Pause_QuitBody",
			"The game will close. Progress from this match is not kept."));
		Text_ConfirmAccept->SetText(NSLOCTEXT("OneBraham", "Pause_QuitAccept", "QUIT"));
	}

	ConfirmLayer->SetVisibility(ESlateVisibility::Visible);

	// The safe answer has focus: a double-tap of the confirm key stays.
	Button_ConfirmCancel->SetKeyboardFocus();
}

void UOneBrahamPauseMenuWidget::CloseConfirm()
{
	const EPendingConfirm Was = PendingConfirm;
	PendingConfirm = EPendingConfirm::None;

	if (ConfirmLayer != nullptr)
	{
		ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Was != EPendingConfirm::None && Button_Resume != nullptr && bOpen)
	{
		Button_Resume->SetKeyboardFocus();
	}
}

void UOneBrahamPauseMenuWidget::AcceptConfirm()
{
	const EPendingConfirm Kind = PendingConfirm;
	CloseConfirm();

	if (Kind == EPendingConfirm::Leave)
	{
		LeaveMatch();
	}
	else if (Kind == EPendingConfirm::Quit)
	{
		QuitToDesktop();
	}
}

void UOneBrahamPauseMenuWidget::GoBack()
{
	if (PendingConfirm != EPendingConfirm::None)
	{
		CloseConfirm();
		return;
	}

	if (Pages != nullptr && Pages->GetActiveWidgetIndex() == 1 && Settings != nullptr)
	{
		// Applies and saves, then calls ShowMainPage through OnBackRequested.
		Settings->RequestBack();
		return;
	}

	RequestResume();
}

// ==========================================================================
//  Actions
// ==========================================================================
void UOneBrahamPauseMenuWidget::RequestResume()
{
	if (AOneBrahamPlayerController* Controller = Cast<AOneBrahamPlayerController>(GetOwningPlayer()))
	{
		Controller->SetPauseMenuOpen(false);
	}
	else
	{
		CloseMenu();
	}
}

void UOneBrahamPauseMenuWidget::LeaveMatch()
{
	APlayerController* Controller = GetOwningPlayer();

	// Close first: the controller unpauses and gives input back to the game, so
	// the loading screen and the main menu inherit a sane input mode.
	RequestResume();

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] Leaving the match from the pause menu."));

	if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
	{
		// Destroys the session, shows the branded loading screen and travels.
		GameInstance->TravelToMainMenu();
	}
	else if (Controller != nullptr)
	{
		Controller->ClientTravel(TEXT("/Game/OneBraham/Maps/L_MainMenu"), TRAVEL_Absolute);
	}
}

void UOneBrahamPauseMenuWidget::QuitToDesktop()
{
	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] Quitting to desktop from the pause menu."));
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
