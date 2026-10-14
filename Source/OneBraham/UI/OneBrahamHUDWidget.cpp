// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamHUDWidget.h"
#include "UI/OneBrahamHUDStringTable.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "UI/OneBrahamCompassWidget.h"
#include "UI/OneBrahamCrosshairWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Core/OneBrahamPlayerController.h"
#include "Styling/SlateTypes.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** Screen-space design constants. Positions are relative - see AddToCanvas. */
	constexpr float AmmoFontScale = 1.f;
	constexpr float PanelPadding = 14.f;

}

UOneBrahamHUDWidget::UOneBrahamHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The HUD never takes focus: every key press belongs to the player, and a HUD
	// that swallows input is the most common way a shooter feels broken.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
}

TSharedRef<SWidget> UOneBrahamHUDWidget::RebuildWidget()
{
	// A designer's Blueprint supplies its own tree. If there is none, we build the
	// entire HUD here so a content-free checkout still has a complete interface.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}

	return Super::RebuildWidget();
}

void UOneBrahamHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// The Blueprint path binds its own widgets; the code path already has them.
	if (Text_Ammo != nullptr)
	{
		Text_Ammo->SetFont(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Large, true));
	}

	UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.UI] HUD widget constructed (%s layout)."),
		bBuiltLayoutInCode ? TEXT("code") : TEXT("authored"));
}

int32 UOneBrahamHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 CurrentLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// --- Why there is no NativeTick here ---------------------------------
	// A C++ UUserWidget is created with EWidgetTickFrequency::Auto, and Auto
	// means "tick only if this widget has a tick event implemented in
	// Blueprint". An override of NativeTick is not one, so this widget's tick
	// was never going to be called: the banner, the subtitle and the damage
	// flash would each have frozen at whatever the last component snapshot set
	// them to. Paint is the one callback a visible widget is guaranteed, so the
	// same timers advance from the clock that the painter already has.
	if (const UWorld* World = GetWorld())
	{
		const double Now = World->GetTimeSeconds();
		const double Delta = LastPaintTimeSeconds < 0.0 ? 0.0 : Now - LastPaintTimeSeconds;
		LastPaintTimeSeconds = Now;

		if (Delta > 0.0)
		{
			AdvanceOverlayTimers(static_cast<float>(Delta));
		}
	}

	return CurrentLayer;
}

void UOneBrahamHUDWidget::AdvanceOverlayTimers(float InDeltaTime) const
{
	// Overlay fades are widget-side because they are purely cosmetic: the
	// component's snapshot stays authoritative for gameplay-visible values.
	if (BannerRemaining > 0.f)
	{
		BannerRemaining = FMath::Max(0.f, BannerRemaining - InDeltaTime);

		if (BannerRemaining <= 0.f && Text_Banner != nullptr)
		{
			Text_Banner->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (SubBannerRemaining > 0.f)
	{
		SubBannerRemaining = FMath::Max(0.f, SubBannerRemaining - InDeltaTime);

		if (SubBannerRemaining <= 0.f && Text_SubBanner != nullptr)
		{
			Text_SubBanner->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (SubtitleRemaining > 0.f)
	{
		SubtitleRemaining = FMath::Max(0.f, SubtitleRemaining - InDeltaTime);

		if (SubtitleRemaining <= 0.f && Text_Subtitle != nullptr)
		{
			Text_Subtitle->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Damage flash fades smoothly even between the component's 15 Hz updates.
	if (Image_DamageFlash != nullptr)
	{
		const float TargetAlpha = Data.DamageIndicatorAlpha * 0.35f;
		const FLinearColor Current = Image_DamageFlash->GetColorAndOpacity();

		if (!FMath::IsNearlyEqual(Current.A, TargetAlpha, 0.01f))
		{
			Image_DamageFlash->SetColorAndOpacity(FLinearColor(Current.R, Current.G, Current.B,
				FMath::FInterpTo(Current.A, TargetAlpha, InDeltaTime, 6.f)));
		}
	}
}

// ==========================================================================
//  Layout construction
// ==========================================================================
UTextBlock* UOneBrahamHUDWidget::MakeText(FName Name, EOneBrahamTextSize Size, const FLinearColor& Colour,
	ETextJustify::Type Justify, bool bBold)
{
	if (WidgetTree == nullptr)
	{
		return nullptr;
	}

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	if (Text == nullptr)
	{
		return nullptr;
	}

	// Text must stay readable over a bright office window and over a dark stairwell,
	// so everything carries a 1 px dark outline in the brand navy. This is the
	// single most important legibility decision on the HUD: a white number on a
	// sunlit Skydeck is invisible without it.
	FSlateFontInfo Font = UOneBrahamUIStyle::Font(Size, bBold);
	Font.OutlineSettings = FFontOutlineSettings(1);
	Font.OutlineSettings.OutlineColor = UOneBrahamUIStyle::Navy();
	Text->SetFont(Font);

	Text->SetColorAndOpacity(FSlateColor(Colour));
	Text->SetJustification(Justify);
	Text->SetVisibility(ESlateVisibility::HitTestInvisible);

	return Text;
}

UProgressBar* UOneBrahamHUDWidget::MakeBar(FName Name, const FLinearColor& FillColour, float Thickness)
{
	if (WidgetTree == nullptr)
	{
		return nullptr;
	}

	UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
	if (Bar == nullptr)
	{
		return nullptr;
	}

	Bar->SetBarFillType(EProgressBarFillType::LeftToRight);
	Bar->SetFillColorAndOpacity(FillColour);
	Bar->SetPercent(1.f);
	Bar->SetVisibility(ESlateVisibility::HitTestInvisible);

	return Bar;
}

void UOneBrahamHUDWidget::AddToCanvas(UWidget* Widget, const FVector2D& Position, const FVector2D& Size,
	const FAnchors& Anchor, const FVector2D& Alignment)
{
	if (Widget == nullptr || RootCanvas == nullptr)
	{
		return;
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(Widget);
	if (Slot == nullptr)
	{
		return;
	}

	Slot->SetAnchors(Anchor);
	Slot->SetAlignment(Alignment);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
	Slot->SetAutoSize(false);
}

void UOneBrahamHUDWidget::AddStretched(UWidget* Widget)
{
	if (Widget == nullptr || RootCanvas == nullptr)
	{
		return;
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(Widget);
	if (Slot == nullptr)
	{
		return;
	}

	// Anchored to all four corners with zero offsets: this is how a full-screen
	// overlay must be built, and it survives any resolution or aspect ratio.
	Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	Slot->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
	Slot->SetAlignment(FVector2D(0.f, 0.f));
	Slot->SetAutoSize(false);
}

void UOneBrahamHUDWidget::BuildDefaultLayout()
{
	bBuiltLayoutInCode = true;

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDRoot"));
	WidgetTree->RootWidget = RootCanvas;

	const FAnchors TopLeft(0.f, 0.f);
	const FAnchors TopCentre(0.5f, 0.f);
	const FAnchors TopRight(1.f, 0.f);
	const FAnchors Centre(0.5f, 0.5f);
	const FAnchors BottomLeft(0.f, 1.f);
	const FAnchors BottomRight(1.f, 1.f);
	const FAnchors BottomCentre(0.5f, 1.f);

	// --- Crosshair (screen centre) ---------------------------------------
	Crosshair = WidgetTree->ConstructWidget<UOneBrahamCrosshairWidget>(
		UOneBrahamCrosshairWidget::StaticClass(), TEXT("Crosshair"));
	AddToCanvas(Crosshair, FVector2D::ZeroVector, FVector2D(600.f, 600.f), Centre, FVector2D(0.5f, 0.5f));

	// --- Compass (top centre) --------------------------------------------
	Compass = WidgetTree->ConstructWidget<UOneBrahamCompassWidget>(
		UOneBrahamCompassWidget::StaticClass(), TEXT("Compass"));
	AddToCanvas(Compass, FVector2D(0.f, 18.f), FVector2D(680.f, 44.f), TopCentre, FVector2D(0.5f, 0.f));

	// --- Match clock (top centre, beneath the compass) -------------------
	Text_Clock = MakeText(TEXT("Text_Clock"), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Center, true);
	AddToCanvas(Text_Clock, FVector2D(0.f, 62.f), FVector2D(240.f, 22.f), TopCentre, FVector2D(0.5f, 0.f));

	// --- Objective banner (top centre, under the clock) ------------------
	Text_Banner = MakeText(TEXT("Text_Banner"), EOneBrahamTextSize::Header, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Center, true);
	AddToCanvas(Text_Banner, FVector2D(0.f, 96.f), FVector2D(900.f, 40.f), TopCentre, FVector2D(0.5f, 0.f));

	Text_SubBanner = MakeText(TEXT("Text_SubBanner"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow(),
		ETextJustify::Center);
	AddToCanvas(Text_SubBanner, FVector2D(0.f, 138.f), FVector2D(900.f, 22.f), TopCentre, FVector2D(0.5f, 0.f));

	// --- Spectating (bottom centre, above where the ammo row would be) ------
	Text_Spectating = MakeText(TEXT("Text_Spectating"), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Center, true);
	AddToCanvas(Text_Spectating, FVector2D(0.f, -96.f), FVector2D(900.f, 56.f), FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f));
	Text_Spectating->SetVisibility(ESlateVisibility::Collapsed);

	// --- Objective state (top left) --------------------------------------
	Text_Floor = MakeText(TEXT("Text_Floor"), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::LightGrey(), ETextJustify::Left, true);
	AddToCanvas(Text_Floor, FVector2D(PanelPadding, 18.f), FVector2D(320.f, 24.f), TopLeft, FVector2D(0.f, 0.f));

	Text_Objective = MakeText(TEXT("Text_Objective"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
	AddToCanvas(Text_Objective, FVector2D(PanelPadding, 44.f), FVector2D(360.f, 20.f), TopLeft, FVector2D(0.f, 0.f));

	Bar_Objective = MakeBar(TEXT("Bar_Objective"), UOneBrahamUIStyle::SafetyYellow(), 6.f);
	AddToCanvas(Bar_Objective, FVector2D(PanelPadding, 68.f), FVector2D(220.f, 6.f), TopLeft, FVector2D(0.f, 0.f));

	// --- Squad (top right) -----------------------------------------------
	Text_Squad = MakeText(TEXT("Text_Squad"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Right);
	AddToCanvas(Text_Squad, FVector2D(-PanelPadding, 18.f), FVector2D(360.f, 108.f), TopRight, FVector2D(1.f, 0.f));

	// --- Kill feed (right, below the squad) ------------------------------
	Text_KillFeed = MakeText(TEXT("Text_KillFeed"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Right);
	AddToCanvas(Text_KillFeed, FVector2D(-PanelPadding, 134.f), FVector2D(420.f, 120.f), TopRight, FVector2D(1.f, 0.f));

	// --- Health (bottom left) --------------------------------------------
	Bar_Health = MakeBar(TEXT("Bar_Health"), UOneBrahamUIStyle::LightGrey(), 8.f);
	AddToCanvas(Bar_Health, FVector2D(PanelPadding, -46.f), FVector2D(300.f, 8.f), BottomLeft, FVector2D(0.f, 1.f));

	Bar_Armour = MakeBar(TEXT("Bar_Armour"), UOneBrahamUIStyle::SignalBlue(), 5.f);
	AddToCanvas(Bar_Armour, FVector2D(PanelPadding, -32.f), FVector2D(300.f, 5.f), BottomLeft, FVector2D(0.f, 1.f));

	Text_Health = MakeText(TEXT("Text_Health"), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::LightGrey(), ETextJustify::Left, true);
	AddToCanvas(Text_Health, FVector2D(PanelPadding, -26.f), FVector2D(300.f, 22.f), BottomLeft, FVector2D(0.f, 1.f));

	Text_Access = MakeText(TEXT("Text_Access"), EOneBrahamTextSize::Micro, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.4f));
	AddToCanvas(Text_Access, FVector2D(PanelPadding, -8.f), FVector2D(320.f, 16.f), BottomLeft, FVector2D(0.f, 1.f));

	// --- Weapon (bottom right) -------------------------------------------
	Text_Ammo = MakeText(TEXT("Text_Ammo"), EOneBrahamTextSize::Large, UOneBrahamUIStyle::LightGrey(), ETextJustify::Right, true);
	AddToCanvas(Text_Ammo, FVector2D(-PanelPadding, -58.f), FVector2D(260.f, 34.f), BottomRight, FVector2D(1.f, 1.f));

	Text_WeaponName = MakeText(TEXT("Text_WeaponName"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.3f), ETextJustify::Right);
	AddToCanvas(Text_WeaponName, FVector2D(-PanelPadding, -30.f), FVector2D(260.f, 20.f), BottomRight, FVector2D(1.f, 1.f));

	Text_FireMode = MakeText(TEXT("Text_FireMode"), EOneBrahamTextSize::Micro, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.45f), ETextJustify::Right);
	AddToCanvas(Text_FireMode, FVector2D(-PanelPadding, -14.f), FVector2D(260.f, 16.f), BottomRight, FVector2D(1.f, 1.f));

	Text_Utilities = MakeText(TEXT("Text_Utilities"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::LightGrey(), ETextJustify::Right);
	AddToCanvas(Text_Utilities, FVector2D(-PanelPadding, -86.f), FVector2D(300.f, 20.f), BottomRight, FVector2D(1.f, 1.f));

	Bar_Reload = MakeBar(TEXT("Bar_Reload"), UOneBrahamUIStyle::SafetyYellow(), 4.f);
	AddToCanvas(Bar_Reload, FVector2D(-PanelPadding, -66.f), FVector2D(180.f, 4.f), BottomRight, FVector2D(1.f, 1.f));

	// --- Interaction prompt (centre bottom) -------------------------------
	Text_Interaction = MakeText(TEXT("Text_Interaction"), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Center, true);
	AddToCanvas(Text_Interaction, FVector2D(0.f, -150.f), FVector2D(700.f, 26.f), BottomCentre, FVector2D(0.5f, 1.f));

	Bar_Interaction = MakeBar(TEXT("Bar_Interaction"), UOneBrahamUIStyle::SignalBlue(), 4.f);
	AddToCanvas(Bar_Interaction, FVector2D(0.f, -122.f), FVector2D(240.f, 4.f), BottomCentre, FVector2D(0.5f, 1.f));

	// --- Subtitle (bottom centre) ----------------------------------------
	Text_Subtitle = MakeText(TEXT("Text_Subtitle"), EOneBrahamTextSize::Small, UOneBrahamUIStyle::LightGrey(),
		ETextJustify::Center);
	AddToCanvas(Text_Subtitle, FVector2D(0.f, -84.f), FVector2D(900.f, 22.f), BottomCentre, FVector2D(0.5f, 1.f));

	// --- Voice indicator (bottom centre, above the subtitle) -------------
	Text_Voice = MakeText(TEXT("Text_Voice"), EOneBrahamTextSize::Micro, UOneBrahamUIStyle::SignalBlue(),
		ETextJustify::Center);
	AddToCanvas(Text_Voice, FVector2D(0.f, -64.f), FVector2D(400.f, 16.f), BottomCentre, FVector2D(0.5f, 1.f));

	// --- Damage flash (full screen) --------------------------------------
	Image_DamageFlash = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Image_DamageFlash"));
	if (Image_DamageFlash != nullptr)
	{
		Image_DamageFlash->SetBrush(UOneBrahamUIStyle::SolidBrush(FLinearColor(0.7f, 0.05f, 0.05f, 0.f)));
		Image_DamageFlash->SetVisibility(ESlateVisibility::HitTestInvisible);
		AddStretched(Image_DamageFlash);
	}

	// --- Downed overlay ---------------------------------------------------
	Panel_Downed = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Panel_Downed"));
	if (Panel_Downed != nullptr)
	{
		Panel_Downed->SetVisibility(ESlateVisibility::Collapsed);

		Text_BleedOut = MakeText(TEXT("Text_BleedOut"), EOneBrahamTextSize::Header, UOneBrahamUIStyle::EmergencyRed(),
			ETextJustify::Center, true);
		UCanvasPanelSlot* BleedSlot = Panel_Downed->AddChildToCanvas(Text_BleedOut);
		if (BleedSlot != nullptr)
		{
			BleedSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			BleedSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			BleedSlot->SetPosition(FVector2D(0.f, -190.f));
			BleedSlot->SetSize(FVector2D(600.f, 40.f));
			BleedSlot->SetAutoSize(false);
		}

		// The revive bar, under the bleed-out timer.
		//
		// The person doing the reviving watches the interaction ring above the
		// crosshair; the person being revived watches this one. Same colour
		// (signal blue) because it is the same interaction seen from the two ends
		// of it, and a player who has been on both ends should not have to learn
		// two colours for one verb. Wider than the interaction ring and across the
		// middle of the screen: while you are on the carpet, this is the only
		// thing worth reading.
		Bar_Revive = MakeBar(TEXT("Bar_Revive"), UOneBrahamUIStyle::SignalBlue(), 8.f);
		UCanvasPanelSlot* ReviveSlot = Bar_Revive != nullptr ? Panel_Downed->AddChildToCanvas(Bar_Revive) : nullptr;
		if (ReviveSlot != nullptr)
		{
			ReviveSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			ReviveSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			ReviveSlot->SetPosition(FVector2D(0.f, -160.f));
			ReviveSlot->SetSize(FVector2D(420.f, 8.f));
			ReviveSlot->SetAutoSize(false);
		}

		if (Bar_Revive != nullptr)
		{
			Bar_Revive->SetPercent(0.f);
			Bar_Revive->SetVisibility(ESlateVisibility::Collapsed);
		}

		AddStretched(Panel_Downed);
	}

	// --- Overlays ---------------------------------------------------------
	auto BuildOverlayPanel = [this](FName Name, UTextBlock*& OutTitle) -> UCanvasPanel*
	{
		UCanvasPanel* Panel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), Name);
		if (Panel == nullptr)
		{
			return nullptr;
		}

		Panel->SetVisibility(ESlateVisibility::Collapsed);

		UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*(Name.ToString() + TEXT("_Backdrop"))));
		if (Backdrop != nullptr)
		{
			Backdrop->SetBrush(UOneBrahamUIStyle::PanelBrush(0.88f, 0.f));

			UCanvasPanelSlot* BackdropSlot = Panel->AddChildToCanvas(Backdrop);
			if (BackdropSlot != nullptr)
			{
				BackdropSlot->SetAnchors(FAnchors(0.f, 0.f));
				BackdropSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
				BackdropSlot->SetAutoSize(false);
			}
		}

		OutTitle = MakeText(FName(*(Name.ToString() + TEXT("_Title"))), EOneBrahamTextSize::Display,
			UOneBrahamUIStyle::LightGrey(), ETextJustify::Center, true);

		UCanvasPanelSlot* TitleSlot = Panel->AddChildToCanvas(OutTitle);
		if (TitleSlot != nullptr)
		{
			TitleSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			TitleSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			TitleSlot->SetPosition(FVector2D(0.f, -120.f));
			TitleSlot->SetSize(FVector2D(1200.f, 60.f));
			TitleSlot->SetAutoSize(false);
		}

		AddStretched(Panel);

		return Panel;
	};

	UTextBlock* DeathTitle = nullptr;
	Panel_DeathScreen = BuildOverlayPanel(TEXT("Panel_DeathScreen"), DeathTitle);

	Text_DeathCause = MakeText(TEXT("Text_DeathCause"), EOneBrahamTextSize::Medium,
		UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.25f), ETextJustify::Center);
	if (Panel_DeathScreen != nullptr && Text_DeathCause != nullptr)
	{
		UCanvasPanelSlot* CauseSlot = Panel_DeathScreen->AddChildToCanvas(Text_DeathCause);
		if (CauseSlot != nullptr)
		{
			CauseSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CauseSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CauseSlot->SetPosition(FVector2D(0.f, -50.f));
			CauseSlot->SetSize(FVector2D(1200.f, 30.f));
			CauseSlot->SetAutoSize(false);
		}
	}

	// The scoreboard can be an authored screen (WBP_OneBrahamScoreboard) and the
	// pause menu can be one (PauseMenuWidgetClass on the controller); without
	// them, these panels are the screens - Docs/08 section 4.
	UTextBlock* ScoreboardTitle = nullptr;
	Panel_Scoreboard = BuildOverlayPanel(TEXT("Panel_Scoreboard"), ScoreboardTitle);

	UTextBlock* PauseTitle = nullptr;
	Panel_PauseMenu = BuildOverlayPanel(TEXT("Panel_PauseMenu"), PauseTitle);

	if (Panel_Scoreboard != nullptr && ScoreboardTitle != nullptr)
	{
		ScoreboardTitle->SetText(NSLOCTEXT("OneBraham", "HUD_ScoreboardPlaceholder",
			"SCOREBOARD"));
	}
	if (Panel_PauseMenu != nullptr && PauseTitle != nullptr)
	{
		// Not "PAUSED": nothing is. The other eleven are still climbing.
		PauseTitle->SetText(UOneBrahamUIStyle::HeaderLabel(
			NSLOCTEXT("OneBraham", "HUD_PauseTitle", "Match in progress")));
	}
	BuildPauseMenuButtons();
	if (Panel_DeathScreen != nullptr && DeathTitle != nullptr)
	{
		DeathTitle->SetText(NSLOCTEXT("OneBraham", "HUD_DeathTitle", "ELIMINATED"));
	}

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD layout built in code (no authored Blueprint found)."));
}

// ==========================================================================
//  Data in
// ==========================================================================
void UOneBrahamHUDWidget::ApplyHUDData(const FOneBrahamHUDData& NewData)
{
	Data = NewData;

	// Accessibility: a single opacity slider for the whole HUD. Applied to the
	// root so a sub-panel can never be missed by the setting.
	if (RootCanvas != nullptr)
	{
		RootCanvas->SetRenderOpacity(FMath::Clamp(Data.HUDOpacity, 0.2f, 1.f));
	}

	// --- Crosshair --------------------------------------------------------
	if (Crosshair != nullptr)
	{
		Crosshair->SetCrosshairVisible(Data.bShowCrosshair);
		Crosshair->SetSpreadDegrees(Data.SpreadDegrees);
		Crosshair->SetSpreadAlpha(FMath::Clamp(Data.SpreadDegrees / 8.f, 0.f, 1.f));
		Crosshair->SetHitMarker(Data.bShowHitMarkers ? Data.HitMarkerAlpha : 0.f,
			Data.bLastHitWasHeadshot, Data.bLastHitWasKill);
		Crosshair->SetDamageDirection(Data.bShowDamageDirection ? Data.DamageDirectionDegrees : -1000.f);
		Crosshair->SetDamageAlpha(Data.DamageIndicatorAlpha);
		Crosshair->SetReloadProgress(Data.bReloading ? Data.ReloadProgress01 : 0.f);
		Crosshair->SetInteractionProgress(Data.bHasInteractionTarget ? Data.InteractionHoldProgress01 : 0.f);
		Crosshair->SetBlocked(Data.bInteractionBlocked);
		Crosshair->SetLowHealthPulse(Data.LowHealthPulseAlpha);
	}

	UpdateHealthPanel(Data);
	UpdateWeaponPanel(Data);
	UpdateObjectivePanel(Data);
	UpdateSquadPanel(Data);
	UpdateKillFeed(Data);
	UpdateInteractionPanel(Data);
	UpdateVoicePanel(Data);
	UpdateCompassData(Data);
	UpdateOverlays(Data);
}

// ==========================================================================
//  Panels
// ==========================================================================
void UOneBrahamHUDWidget::UpdateHealthPanel(const FOneBrahamHUDData& InData)
{
	const float HealthFraction = InData.MaxHealth > 0.f ? FMath::Clamp(InData.Health / InData.MaxHealth, 0.f, 1.f) : 0.f;
	const FLinearColor HealthColour = UOneBrahamUIStyle::HealthColour(HealthFraction);

	if (Bar_Health != nullptr)
	{
		Bar_Health->SetPercent(HealthFraction);
		Bar_Health->SetFillColorAndOpacity(HealthColour);
	}

	if (Bar_Armour != nullptr)
	{
		const float ArmourFraction = InData.MaxArmour > 0.f
			? FMath::Clamp(InData.Armour / InData.MaxArmour, 0.f, 1.f) : 0.f;
		Bar_Armour->SetPercent(ArmourFraction);
		Bar_Armour->SetVisibility(ArmourFraction > 0.001f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (Text_Health != nullptr)
	{
		// Downed shows the bleed-out countdown instead of a number: the player needs
		// to know how long they have, not how much health is left.
		if (InData.LifeState == EOneBrahamLifeState::Downed)
		{
			Text_Health->SetText(FText::Format(NSLOCTEXT("OneBraham", "HUD_BleedOut", "BLEED OUT IN {0}s"),
				FText::AsNumber(FMath::CeilToInt(InData.BleedOutRemaining))));
			Text_Health->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::EmergencyRed()));
		}
		else
		{
			Text_Health->SetText(FText::Format(NSLOCTEXT("OneBraham", "HUD_Health", "{0}"),
				FText::AsNumber(FMath::CeilToInt(InData.Health))));
			Text_Health->SetColorAndOpacity(FSlateColor(HealthColour));
		}
	}

	if (Text_Access != nullptr)
	{
		Text_Access->SetText(FText::FromString(BuildAccessRow(InData)));
		Text_Access->SetVisibility(InData.bShowFloorIndicator ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

FText UOneBrahamHUDWidget::BuildAccessRow(const FOneBrahamHUDData& InData) const
{
	// Access is deliberately text rather than icons: a keycard tier is a colour
	// and a word, and the player learns the colour code from the doors themselves.
	FString Row;

	switch (InData.HighestKeycardTier)
	{
	case EOneBrahamAccessTier::Public:		Row = TEXT("PUBLIC"); break;
	case EOneBrahamAccessTier::Staff:		Row = TEXT("STAFF"); break;
	case EOneBrahamAccessTier::Secure:		Row = TEXT("SECURE"); break;
	case EOneBrahamAccessTier::Executive:	Row = TEXT("EXECUTIVE"); break;
	case EOneBrahamAccessTier::Skydeck:		Row = TEXT("FACILITIES MASTER"); break;
	default:								Row = TEXT("PUBLIC"); break;
	}

	if (InData.bHasOverrideTool)
	{
		Row += TEXT("  +  OVERRIDE TOOL");
	}

	return FText::FromString(Row);
}

FText UOneBrahamHUDWidget::BuildUtilityRow(const FOneBrahamHUDData& InData) const
{
	// Row of glyph + count. Empty utilities are dimmed out of the row entirely so
	// the remainder is readable at a glance mid-fight.
	TArray<FString> Parts;

	if (InData.FragCount > 0)		{ Parts.Add(FString::Printf(TEXT("F %d"), InData.FragCount)); }
	if (InData.FlashCount > 0)		{ Parts.Add(FString::Printf(TEXT("X %d"), InData.FlashCount)); }
	if (InData.SmokeCount > 0)		{ Parts.Add(FString::Printf(TEXT("S %d"), InData.SmokeCount)); }
	if (InData.BreachCount > 0)		{ Parts.Add(FString::Printf(TEXT("B %d"), InData.BreachCount)); }
	if (InData.MedkitCount > 0)		{ Parts.Add(FString::Printf(TEXT("+ %d"), InData.MedkitCount)); }

	return FText::FromString(FString::Join(Parts, TEXT("   ")));
}

void UOneBrahamHUDWidget::UpdateWeaponPanel(const FOneBrahamHUDData& InData)
{
	if (Text_Ammo != nullptr)
	{
		if (!InData.bShowAmmoCount)
		{
			Text_Ammo->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			Text_Ammo->SetVisibility(ESlateVisibility::HitTestInvisible);

			// "12 / 90" - magazine then reserve. No icons, no shells: this is a
			// corporate building, the number is the number.
			Text_Ammo->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"),
				InData.AmmoInMagazine, InData.AmmoReserve)));

			// Low ammo is the only colour the ammo counter ever takes.
			const bool bLow = InData.MagazineSize > 0 && InData.AmmoInMagazine <= FMath::Max(2, InData.MagazineSize / 5);
			Text_Ammo->SetColorAndOpacity(FSlateColor(bLow
				? UOneBrahamUIStyle::SafetyYellow()
				: UOneBrahamUIStyle::LightGrey()));
		}
	}

	if (Text_WeaponName != nullptr)
	{
		Text_WeaponName->SetText(InData.WeaponDisplayName);
	}

	if (Text_FireMode != nullptr)
	{
		FText ModeText;
		switch (InData.FireMode)
		{
		case EOneBrahamFireMode::Single:	ModeText = NSLOCTEXT("OneBraham", "FireMode_Single", "SINGLE"); break;
		case EOneBrahamFireMode::Burst:		ModeText = NSLOCTEXT("OneBraham", "FireMode_Burst", "BURST"); break;
		case EOneBrahamFireMode::Auto:		ModeText = NSLOCTEXT("OneBraham", "FireMode_Auto", "AUTO"); break;
		case EOneBrahamFireMode::Pump:		ModeText = NSLOCTEXT("OneBraham", "FireMode_Pump", "PUMP"); break;
		case EOneBrahamFireMode::Bolt:		ModeText = NSLOCTEXT("OneBraham", "FireMode_Bolt", "BOLT"); break;
		default:							ModeText = FText::GetEmpty(); break;
		}

		FString Suffix;
		if (InData.bLaserOn)		{ Suffix += TEXT("  LASER"); }
		if (InData.bFlashlightOn)	{ Suffix += TEXT("  LIGHT"); }

		Text_FireMode->SetText(Suffix.IsEmpty()
			? ModeText
			: FText::FromString(ModeText.ToString() + Suffix));
	}

	if (Text_Utilities != nullptr)
	{
		Text_Utilities->SetText(BuildUtilityRow(InData));
	}

	if (Bar_Reload != nullptr)
	{
		Bar_Reload->SetPercent(InData.ReloadProgress01);
		Bar_Reload->SetVisibility(InData.bReloading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UOneBrahamHUDWidget::UpdateObjectivePanel(const FOneBrahamHUDData& InData)
{
	if (Text_Floor != nullptr)
	{
		Text_Floor->SetText(InData.FloorDisplayName);
		Text_Floor->SetVisibility(InData.bShowFloorIndicator ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (Text_Objective == nullptr && Bar_Objective == nullptr)
	{
		return;
	}

	// Priority order: extraction beats lockdown beats generic phase text. The
	// player only ever needs the single most urgent thing.
	if (InData.bExtractionActive)
	{
		const FText Text = InData.HoldingSquadId >= 0
			? FText::Format(NSLOCTEXT("OneBraham", "HUD_ExtractionHeld", "EXTRACTION - {0}s"),
				FText::AsNumber(FMath::CeilToInt(InData.ExtractionSecondsRemaining)))
			: NSLOCTEXT("OneBraham", "HUD_ExtractionOpen", "EXTRACTION LIVE ON THE SKYDECK");

		if (Text_Objective != nullptr)
		{
			Text_Objective->SetText(Text);
			Text_Objective->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::SuccessGreen()));
		}

		if (Bar_Objective != nullptr)
		{
			Bar_Objective->SetPercent(InData.ExtractionHoldProgress01);
			Bar_Objective->SetFillColorAndOpacity(UOneBrahamUIStyle::SuccessGreen());
			Bar_Objective->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	else if (InData.bLockdownActive)
	{
		if (Text_Objective != nullptr)
		{
			Text_Objective->SetText(FText::Format(NSLOCTEXT("OneBraham", "HUD_Lockdown", "LOCKDOWN - {0}s TO NEXT STAGE"),
				FText::AsNumber(FMath::CeilToInt(InData.LockdownSecondsRemaining))));
			Text_Objective->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::EmergencyRed()));
		}

		if (Bar_Objective != nullptr)
		{
			Bar_Objective->SetFillColorAndOpacity(UOneBrahamUIStyle::EmergencyRed());
			Bar_Objective->SetPercent(InData.LockdownSecondsRemaining > 0.f
				? FMath::Clamp(InData.LockdownSecondsRemaining / 90.f, 0.f, 1.f) : 0.f);
			Bar_Objective->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	else
	{
		if (Text_Objective != nullptr && !InData.ControlPointSummary.IsEmpty())
		{
			// FIRE DRILL: the points and the score, the whole objective in a line.
			Text_Objective->SetText(FText::Format(NSLOCTEXT("OneBraham", "HUD_ControlPoints", "{0}   ({1} - {2})"),
				InData.ControlPointSummary, FText::AsNumber(InData.ScoreHome), FText::AsNumber(InData.ScoreAway)));
			Text_Objective->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::SafetyYellow()));
		}
		else if (Text_Objective != nullptr)
		{
			Text_Objective->SetText(FText::Format(NSLOCTEXT("OneBraham", "HUD_AliveCount", "{0} ALIVE  -  {1} SQUADS"),
				FText::AsNumber(InData.AlivePlayersRemaining), FText::AsNumber(InData.SquadsRemaining)));
			Text_Objective->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.3f)));
		}

		if (Bar_Objective != nullptr)
		{
			Bar_Objective->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (Text_Clock != nullptr)
	{
		Text_Clock->SetText(UOneBrahamUIStyle::ClockLabel(InData.MatchTimeRemaining));
		Text_Clock->SetVisibility(InData.MatchTimeRemaining > 0.f
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UOneBrahamHUDWidget::UpdateSquadPanel(const FOneBrahamHUDData& InData)
{
	if (Text_Squad == nullptr)
	{
		return;
	}

	if (!InData.bShowSquadPanel || InData.Squad.Num() == 0)
	{
		Text_Squad->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	Text_Squad->SetVisibility(ESlateVisibility::HitTestInvisible);

	TArray<FString> Lines;
	Lines.Add(FString::Join(TArray<FString>{ TEXT("SQUAD") }, TEXT("")));

	for (const FOneBrahamHUDSquadMember& Member : InData.Squad)
	{
		FString Status;
		switch (Member.LifeState)
		{
		case EOneBrahamLifeState::Alive:
			Status = TEXT("");
			break;
		case EOneBrahamLifeState::Downed:
			Status = FString::Printf(TEXT("  DOWN %ds"), FMath::CeilToInt(Member.BleedOutFraction * 60.f));
			break;
		case EOneBrahamLifeState::Extracted:
			Status = TEXT("  EXTRACTED");
			break;
		default:
			Status = TEXT("  OUT");
			break;
		}

		FString Range;
		if (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed)
		{
			Range = Member.DistanceCm >= 0.f
				? TEXT("  ") + UOneBrahamUIStyle::DistanceLabel(Member.DistanceCm).ToString()
				: (Member.FloorIndex != INDEX_NONE
					? TEXT("  ") + FText::Format(
						OneBrahamHUDStrings::Get(TEXT("HUD_Floor_Format")),
						FText::AsNumber(Member.FloorIndex)).ToString()
					: FString());
		}

		const FString Mic = Member.bIsTransmitting ? TEXT("[mic] ") : TEXT("");

		Lines.Add(FString::Printf(TEXT("%s%s%s%s"), *Mic, *Member.PlayerName, *Status, *Range));
	}

	Text_Squad->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
}

void UOneBrahamHUDWidget::UpdateKillFeed(const FOneBrahamHUDData& InData)
{
	if (Text_KillFeed == nullptr)
	{
		return;
	}

	if (!InData.bShowKillFeed || InData.KillFeed.Num() == 0)
	{
		Text_KillFeed->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	Text_KillFeed->SetVisibility(ESlateVisibility::HitTestInvisible);

	TArray<FString> Lines;

	// Newest first, and never more than five: a feed longer than that is a log,
	// not a feed.
	const int32 StartIndex = FMath::Max(0, InData.KillFeed.Num() - 5);

	for (int32 Index = InData.KillFeed.Num() - 1; Index >= StartIndex; --Index)
	{
		const FOneBrahamHUDKillFeedEntry& Entry = InData.KillFeed[Index];

		FString Verb = Entry.bHeadshot ? TEXT("[HS] ") : (Entry.bMelee ? TEXT("[M] ") : TEXT(""));
		if (Entry.WeaponId == TEXT("Building"))	{ Verb = TEXT("[B] "); }
		if (Entry.WeaponId == TEXT("Utility"))	{ Verb = TEXT("[U] "); }

		const FString Self = Entry.bKillerIsLocalPlayer ? TEXT("* ") : TEXT("");

		Lines.Add(FString::Printf(TEXT("%s%s%s%s"), *Self, *Entry.KillerName, *Verb,
			*Entry.VictimName));
	}

	Text_KillFeed->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
}

void UOneBrahamHUDWidget::UpdateInteractionPanel(const FOneBrahamHUDData& InData)
{
	if (Text_Interaction == nullptr || Bar_Interaction == nullptr)
	{
		return;
	}

	const bool bVisible = InData.bShowInteractionPrompt && InData.bHasInteractionTarget;

	if (!bVisible)
	{
		Text_Interaction->SetVisibility(ESlateVisibility::Collapsed);
		Bar_Interaction->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	Text_Interaction->SetVisibility(ESlateVisibility::HitTestInvisible);
	Text_Interaction->SetText(InData.bInteractionBlocked && !InData.InteractionBlockedReason.IsEmpty()
		? InData.InteractionBlockedReason
		: InData.InteractionPrompt);
	Text_Interaction->SetColorAndOpacity(FSlateColor(InData.bInteractionBlocked
		? UOneBrahamUIStyle::EmergencyRed()
		: UOneBrahamUIStyle::LightGrey()));

	const bool bHold = InData.InteractionHoldProgress01 > 0.f && InData.InteractionHoldProgress01 < 1.f;
	Bar_Interaction->SetPercent(InData.InteractionHoldProgress01);
	Bar_Interaction->SetVisibility(bHold ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UOneBrahamHUDWidget::UpdateVoicePanel(const FOneBrahamHUDData& InData)
{
	if (Text_Voice == nullptr)
	{
		return;
	}

	if (!InData.bShowVoiceIndicator)
	{
		Text_Voice->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Only show the indicator when transmitting, muted or there is a problem: a
	// permanent voice widget is clutter, and this is not a streaming overlay.
	FString VoiceText;

	if (InData.bSelfMuted)
	{
		VoiceText = TEXT("VOICE MUTED");
	}
	else if (InData.bLocalTransmitting)
	{
		VoiceText = FString::Printf(TEXT("TRANSMITTING - %s"), *InData.VoiceChannelName.ToString());
	}
	else if (!InData.bMicrophoneAvailable)
	{
		VoiceText = TEXT("NO MICROPHONE DETECTED");
	}

	Text_Voice->SetVisibility(VoiceText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	Text_Voice->SetText(FText::FromString(VoiceText));
	Text_Voice->SetColorAndOpacity(FSlateColor(InData.bLocalTransmitting
		? UOneBrahamUIStyle::SuccessGreen()
		: UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.4f)));
}

void UOneBrahamHUDWidget::UpdateCompassData(const FOneBrahamHUDData& InData)
{
	if (Compass == nullptr)
	{
		return;
	}

	Compass->SetCompassVisible(InData.bShowCompass && InData.bShowObjectiveMarkers);
	Compass->SetMarkers(InData.CompassMarkers);
}

// ==========================================================================
//  Overlays
// ==========================================================================
void UOneBrahamHUDWidget::UpdateOverlays(const FOneBrahamHUDData& InData)
{
	if (Panel_Downed != nullptr)
	{
		const bool bDowned = InData.LifeState == EOneBrahamLifeState::Downed;
		Panel_Downed->SetVisibility(bDowned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bDowned && Text_BleedOut != nullptr)
		{
			// "BLEEDING OUT  12s": the words come from the copy table (which a
			// writer can edit in DT_HUDStrings), the number from the state.
			Text_BleedOut->SetText(FText::Format(
				NSLOCTEXT("OneBraham", "HUD_BigValue", "{0}  {1}s"),
				OneBrahamHUDStrings::Get(TEXT("HUD_Downed_BleedOut")),
				FText::AsNumber(FMath::CeilToInt(InData.BleedOutRemaining))));
		}

		if (bDowned && Bar_Revive != nullptr)
		{
			// Zero means nobody is helping. The bar appears the moment a hand
			// touches the player and fills at the server's rate, so a revive that
			// gets interrupted visibly snaps back to nothing - which is the whole
			// drama of the moment and worth one element on the screen.
			const bool bBeingRevived = InData.ReviveProgress01 > 0.f;

			Bar_Revive->SetPercent(FMath::Clamp(InData.ReviveProgress01, 0.f, 1.f));
			Bar_Revive->SetVisibility(bBeingRevived ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	// The damage flash is the one full-screen element in the game, and it is kept
	// deliberately faint: at 35% maximum it reads as pain without hiding a threat.
	if (Image_DamageFlash != nullptr)
	{
		Image_DamageFlash->SetVisibility(InData.DamageIndicatorAlpha > 0.01f
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	// Banner text: driven by the component's snapshot, so a replicate-order change
	// cannot leave a stale banner on screen.
	if (Text_Banner != nullptr)
	{
		const bool bHasBanner = !InData.BannerText.IsEmpty() && InData.BannerRemaining > 0.f;
		Text_Banner->SetVisibility(bHasBanner ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bHasBanner)
		{
			Text_Banner->SetText(InData.BannerText);
			Text_Banner->SetColorAndOpacity(FSlateColor(InData.BannerColour));
		}
	}

	if (Text_Spectating != nullptr)
	{
		Text_Spectating->SetVisibility(InData.bSpectating ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (InData.bSpectating)
		{
			const FText Who = InData.bSpectatorFreeCamera || InData.SpectatedPlayerName.IsEmpty()
				? NSLOCTEXT("OneBraham", "HUD_SpectatingFree", "SPECTATING - FREE CAMERA")
				: FText::Format(NSLOCTEXT("OneBraham", "HUD_SpectatingWho", "SPECTATING {0} - FLOOR {1}"),
					FText::FromString(InData.SpectatedPlayerName.ToUpper()), FText::AsNumber(InData.SpectatedFloor));
			Text_Spectating->SetText(FText::Format(NSLOCTEXT("OneBraham", "HUD_SpectatingHelp", "{0}\nPrevious / next player  -  free camera"), Who));
		}
	}

	if (Text_SubBanner != nullptr)
	{
		const bool bHasSub = !InData.SubBannerText.IsEmpty();
		Text_SubBanner->SetVisibility(bHasSub ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bHasSub)
		{
			Text_SubBanner->SetText(InData.SubBannerText);
		}
	}

	if (Text_Subtitle != nullptr)
	{
		const bool bHasSubtitle = InData.bShowSubtitles
			&& !InData.SubtitleText.IsEmpty()
			&& InData.SubtitleRemaining > 0.f;

		Text_Subtitle->SetVisibility(bHasSubtitle ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bHasSubtitle)
		{
			const FText Speaker = InData.SubtitleSpeaker.IsEmpty()
				? FText::GetEmpty()
				: FText::FromString(InData.SubtitleSpeaker + TEXT(": "));
			Text_Subtitle->SetText(FText::Join(Speaker, InData.SubtitleText));
		}
	}
}

void UOneBrahamHUDWidget::SetScoreboardVisible(bool bVisible)
{
	if (Panel_Scoreboard != nullptr)
	{
		Panel_Scoreboard->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UOneBrahamHUDWidget::SetDeathScreenVisible(bool bVisible, const FText& CauseText)
{
	if (Panel_DeathScreen != nullptr)
	{
		Panel_DeathScreen->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (Text_DeathCause != nullptr)
	{
		// The cause line is deliberately plain: "h.ferris - MP5", or the head
		// shot / execution variant of it, built by
		// AOneBrahamCharacter::BuildDeathCauseText from the same kill-feed entry
		// the feed prints. The game does not editorialise and it never blames
		// the player.
		Text_DeathCause->SetText(CauseText);
	}
}

void UOneBrahamHUDWidget::TogglePauseMenu(bool bVisible)
{
	// The HUD is hit-test invisible so it never steals a click from the game.
	// The pause menu is the one time it must take one: the widget lets clicks
	// through to its children while the menu is up, and goes back after.
	SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);

	if (Panel_PauseMenu != nullptr)
	{
		Panel_PauseMenu->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (bVisible)
	{
		BindPauseMenuButtons();

		if (Button_PauseResume != nullptr)
		{
			// A pad has no cursor: focus the first button so the d-pad and A work.
			Button_PauseResume->SetKeyboardFocus();
		}
	}
}

void UOneBrahamHUDWidget::BuildPauseMenuButtons()
{
	if (Panel_PauseMenu == nullptr)
	{
		return;
	}

	Button_PauseResume = MakeMenuButton(TEXT("Button_PauseResume"),
		NSLOCTEXT("OneBraham", "HUD_Pause_Resume", "Resume"), -20.f);
	Button_PauseSettings = MakeMenuButton(TEXT("Button_PauseSettings"),
		NSLOCTEXT("OneBraham", "HUD_Pause_Settings", "Settings"), 50.f);
	Button_PauseLeave = MakeMenuButton(TEXT("Button_PauseLeave"),
		NSLOCTEXT("OneBraham", "HUD_Pause_Leave", "Leave match"), 120.f);
}

UButton* UOneBrahamHUDWidget::MakeMenuButton(FName Name, const FText& Label, float OffsetY)
{
	if (WidgetTree == nullptr || Panel_PauseMenu == nullptr)
	{
		return nullptr;
	}

	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	if (Button == nullptr)
	{
		return nullptr;
	}

	// Outline at rest, signal blue under the cursor or the pad's focus, navy while
	// held: the brand palette, and the same three states on both devices.
	FButtonStyle Style;
	Style.SetNormal(UOneBrahamUIStyle::OutlineBrush(UOneBrahamUIStyle::LightGrey(), 1.f, 4.f));
	Style.SetHovered(UOneBrahamUIStyle::SolidBrush(UOneBrahamUIStyle::SignalBlue()));
	Style.SetPressed(UOneBrahamUIStyle::SolidBrush(UOneBrahamUIStyle::Navy()));
	Button->SetStyle(Style);

	if (UTextBlock* Text = MakeText(FName(*(Name.ToString() + TEXT("_Label"))), EOneBrahamTextSize::Medium,
		UOneBrahamUIStyle::LightGrey(), ETextJustify::Center, true))
	{
		Text->SetText(UOneBrahamUIStyle::HeaderLabel(Label));
		Button->AddChild(Text);
	}

	if (UCanvasPanelSlot* ButtonSlot = Panel_PauseMenu->AddChildToCanvas(Button))
	{
		ButtonSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ButtonSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ButtonSlot->SetPosition(FVector2D(0.f, OffsetY));
		ButtonSlot->SetSize(FVector2D(360.f, 56.f));
		ButtonSlot->SetAutoSize(false);
	}

	return Button;
}

void UOneBrahamHUDWidget::BindPauseMenuButtons()
{
	if (Button_PauseResume != nullptr)
	{
		Button_PauseResume->OnClicked.AddUniqueDynamic(this, &UOneBrahamHUDWidget::HandlePauseResume);
	}
	if (Button_PauseSettings != nullptr)
	{
		Button_PauseSettings->OnClicked.AddUniqueDynamic(this, &UOneBrahamHUDWidget::HandlePauseSettings);
	}
	if (Button_PauseLeave != nullptr)
	{
		Button_PauseLeave->OnClicked.AddUniqueDynamic(this, &UOneBrahamHUDWidget::HandlePauseLeave);
	}
}

void UOneBrahamHUDWidget::HandlePauseResume()
{
	if (AOneBrahamPlayerController* Controller = Cast<AOneBrahamPlayerController>(GetOwningPlayer()))
	{
		Controller->TogglePauseMenu();
	}
}

void UOneBrahamHUDWidget::HandlePauseSettings()
{
	if (AOneBrahamPlayerController* Controller = Cast<AOneBrahamPlayerController>(GetOwningPlayer()))
	{
		Controller->OpenSettingsMenu();
	}
}

void UOneBrahamHUDWidget::HandlePauseLeave()
{
	if (AOneBrahamPlayerController* Controller = Cast<AOneBrahamPlayerController>(GetOwningPlayer()))
	{
		Controller->LeaveMatch();
	}
}

void UOneBrahamHUDWidget::SetDownedVisible(bool bDowned)
{
	if (Panel_Downed != nullptr)
	{
		Panel_Downed->SetVisibility(bDowned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UOneBrahamHUDWidget::ShowBanner(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	BannerRemaining = FMath::Max(0.2f, DurationSeconds);

	if (Text_Banner != nullptr)
	{
		Text_Banner->SetText(Text);
		Text_Banner->SetColorAndOpacity(FSlateColor(Colour));
		Text_Banner->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UOneBrahamHUDWidget::ShowSubBanner(const FText& Text, float DurationSeconds)
{
	SubBannerRemaining = FMath::Max(0.2f, DurationSeconds);

	if (Text_SubBanner != nullptr)
	{
		Text_SubBanner->SetText(Text);
		Text_SubBanner->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UOneBrahamHUDWidget::ShowSubtitle(const FText& Text, const FString& SpeakerName, float DurationSeconds)
{
	SubtitleRemaining = FMath::Max(0.5f, DurationSeconds);

	if (Text_Subtitle == nullptr)
	{
		return;
	}

	const FText Speaker = SpeakerName.IsEmpty()
		? FText::GetEmpty()
		: FText::FromString(SpeakerName + TEXT(": "));

	Text_Subtitle->SetText(FText::Join(Speaker, Text));
	Text_Subtitle->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UOneBrahamHUDWidget::AddWorldPing(const FOneBrahamHUDWorldPing& Ping)
{
	// Screened pings are drawn in the world by the marker component; the HUD keeps
	// the list so the compass can fall back to it when the marker is off screen.
	RecentPings.Add(Ping);

	static constexpr int32 MaxPings = 8;
	while (RecentPings.Num() > MaxPings)
	{
		RecentPings.RemoveAt(0, 1, EAllowShrinking::No);
	}
}
