// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamPauseMenuWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Containers/Ticker.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/FrontEnd/OneBrahamPaint.h"
#include "UI/FrontEnd/OneBrahamPaintedControl.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/HUD/OneBrahamTween.h"
#include "UI/OneBrahamHUDComponent.h"
#include "UI/OneBrahamSettingsWidget.h"

namespace OneBrahamPauseLayout
{
	constexpr float ColumnWidth = 600.f;
	constexpr float RowHeight = 56.f;
	constexpr float CardWidth = 420.f;
	constexpr float CardHeight = 220.f;

	/** My painter from the front end's: the same element list, layer and geometry. */
	struct FBridge
	{
		int32 Layer;
		FOneBrahamPainter P;
		explicit FBridge(const OneBrahamPaint::FPainter& C)
			: Layer(C.Layer), P(C.Out, C.Geometry, Layer, C.Opacity)
		{
		}
	};
}

UOneBrahamPauseMenuWidget::UOneBrahamPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ==========================================================================
//  Construction helpers
// ==========================================================================
UOneBrahamPaintedControl* UOneBrahamPauseMenuWidget::MakeLayer(const FVector2D& Size)
{
	UOneBrahamPaintedControl* Layer = CreateWidget<UOneBrahamPaintedControl>(this, UOneBrahamPaintedControl::StaticClass());
	Layer->Setup(Size, false);
	return Layer;
}

UOneBrahamPaintedControl* UOneBrahamPauseMenuWidget::MakeDirectoryRow(const FText& Verb, const FText& Where, TFunction<void()> OnChosen)
{
	using namespace OneBrahamPaint;

	UOneBrahamPaintedControl* Row = CreateWidget<UOneBrahamPaintedControl>(this, UOneBrahamPaintedControl::StaticClass());
	Row->Setup(FVector2D(0.f, OneBrahamPauseLayout::RowHeight), true);

	// The front end's directory row (HOME), drawn the same way: a verb, a
	// dotted leader, the where-line, and on focus the signal-blue bar growing
	// from the row's middle with the verb stepping in 12 px.
	const FString VerbText = Verb.ToString();
	const FString WhereText = Where.ToString();
	Row->PaintFn = [VerbText, WhereText](const FPainter& C, const FOneBrahamControlState& State)
	{
		const FVector2D Size = C.Size();
		const float Lit = State.bLit
			? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
			: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f);
		const float Baseline = 37.f;

		if (State.SincePress < 0.09f)
		{
			Box(C, FVector2D(-18.f, 0.f), FVector2D(Size.X + 18.f, Size.Y), Tok(EOneBrahamColour::SignalBlue, 0.22f));
		}

		if (Lit > 0.f)
		{
			Box(C, FVector2D(-18.f, 0.f), FVector2D(Size.X + 18.f, Size.Y), Tok(EOneBrahamColour::White, 0.06f * Lit));
			const float BarH = Size.Y * Lit;
			Box(C, FVector2D(-18.f, (Size.Y - BarH) * 0.5f), FVector2D(6.f, BarH), Tok(EOneBrahamColour::SignalBlue));
		}

		const FSlateFontInfo VerbFont = Type(EOneBrahamType::Verb, 28.f, -10);
		const FSlateFontInfo WhereFont = Type(EOneBrahamType::Caption, 15.f, 20);
		const float VerbX = 12.f * Lit;
		const FVector2D VerbSize = TextOnBaseline(C, VerbX, Baseline, VerbText, VerbFont, Tok(EOneBrahamColour::White, FMath::Lerp(0.8f, 1.f, Lit)));
		const FVector2D WhereSize = Measure(WhereText, WhereFont);
		TextOnBaseline(C, Size.X, Baseline, WhereText, WhereFont, Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.55f, 0.85f, Lit)), 1.f);

		const float March = UOneBrahamUIStyle::ReduceMotion() ? 0.f : static_cast<float>(FMath::Fmod(State.Now, 2.0) * 8.0) * Lit;
		DottedH(C, VerbX + VerbSize.X + 14.f, Size.X - WhereSize.X - 14.f, Baseline - 1.f,
			Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.18f, 0.35f, Lit)), 4.f, 1.f, March);

		Box(C, FVector2D(0.f, Size.Y - 1.f), FVector2D(Size.X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.08f));
	};

	// The press shows for 90 ms on the real clock (a standalone game is paused
	// under this menu, and the world's timers with it), then the choice runs.
	TWeakObjectPtr<UOneBrahamPauseMenuWidget> WeakThis(this);
	Row->OnActivate = [WeakThis, OnChosen]()
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, OnChosen](float)
		{
			if (WeakThis.IsValid())
			{
				OnChosen();
			}
			return false;
		}), 0.09f);
	};
	return Row;
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamPauseMenuWidget::BuildLayout()
{
	using namespace OneBrahamPauseLayout;
	using namespace OneBrahamPaint;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PauseRoot"));
	WidgetTree->RootWidget = Root;

	auto Fill = [](UOverlaySlot* OverlaySlot)
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	};

	// --- the frost: the meeting-room glass over the world ----------------------------
	// Blur 0 to 12 in 180 ms, a 4% white frost, the band of manifestation dots
	// at 40-46% of the height, and a navy dim so the type reads. The world is
	// still faintly there: in a networked match it is still happening.
	UOneBrahamPaintedControl* Frost = MakeLayer(FVector2D::ZeroVector);
	TWeakObjectPtr<UOneBrahamPauseMenuWidget> WeakThis(this);
	Frost->PaintFn = [WeakThis](const FPainter& C, const FOneBrahamControlState& State)
	{
		const UOneBrahamPauseMenuWidget* Menu = WeakThis.Get();
		const FVector2D Size = C.Size();
		const float T = Menu != nullptr ? static_cast<float>(State.Now - Menu->OpenedAt) : 1.f;
		const float In = UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, T / 0.18f);
		Blur(C, FVector2D::ZeroVector, Size, 12.f * In);
		Box(C, FVector2D::ZeroVector, Size, Tok(EOneBrahamColour::SignageWhite, 0.04f * In));
		Box(C, FVector2D::ZeroVector, Size, Tok(EOneBrahamColour::Navy, 0.40f * In));
		DotGrid(C, FVector2D(0.f, Size.Y * 0.40f), FVector2D(Size.X, Size.Y * 0.06f), 12.f, Tok(EOneBrahamColour::White, 0.10f * In));
		// A left-hand shade under the column, so the directory sits on something.
		GradientH(C, FVector2D::ZeroVector, FVector2D(Size.X * 0.55f, Size.Y),
			{ { 0.f, Tok(EOneBrahamColour::OLEDBlack, 0.55f * In) }, { 0.6f, Tok(EOneBrahamColour::OLEDBlack, 0.25f * In) }, { 1.f, Tok(EOneBrahamColour::OLEDBlack, 0.f) } });
	};
	Fill(Root->AddChildToOverlay(Frost));

	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("PausePages"));
	Fill(Root->AddChildToOverlay(Pages));

	// --- page 0: the directory and the status card -------------------------------------
	UCanvasPanel* Main = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PauseMain"));

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// The masthead: BACK IN 5 MINUTES, ON BREAK, the ribbon rule, the line.
	UOneBrahamPaintedControl* Masthead = MakeLayer(FVector2D(ColumnWidth, 236.f));
	Masthead->PaintFn = [WeakThis](const FPainter& C, const FOneBrahamControlState& State)
	{
		const UOneBrahamPauseMenuWidget* Menu = WeakThis.Get();
		const float T = Menu != nullptr ? static_cast<float>(State.Now - Menu->OpenedAt) : 1.f;
		const UWorld* World = Menu != nullptr ? Menu->GetWorld() : nullptr;
		const bool bStandalone = World == nullptr || World->GetNetMode() == NM_Standalone;

		Text(C, FVector2D(0.f, 0.f), TEXT("BACK  IN  5  MINUTES"), Type(EOneBrahamType::Eyebrow, 13.f, 180), Tok(EOneBrahamColour::LightGrey, 0.7f));
		const FSlateFontInfo Title = Type(EOneBrahamType::Title, 96.f, -10);
		const FVector2D TitleSize = Text(C, FVector2D(-4.f, 14.f), TEXT("ON BREAK"), Title, Tok(EOneBrahamColour::White));
		const float Rule = UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, (T - 0.08f) / 0.4f);
		Ribbon(C, FVector2D(0.f, 14.f + TitleSize.Y + 2.f), FVector2D(TitleSize.X * 0.44f * Rule, 2.f));
		const FText Line = bStandalone
			? NSLOCTEXT("OneBraham", "Pause_SubtitleStandalone", "The building is holding still until you return.")
			: NSLOCTEXT("OneBraham", "Pause_SubtitleOnline", "The match is still running. Your colleagues have been told you are in a meeting.");
		Text(C, FVector2D(0.f, 14.f + TitleSize.Y + 18.f), Line, Type(EOneBrahamType::Aside, 22.f), Tok(EOneBrahamColour::LightGrey, 0.85f));
	};
	Column->AddChildToVerticalBox(Masthead)->SetPadding(FMargin(0.f, 0.f, 0.f, 26.f));

	Row_Resume = MakeDirectoryRow(NSLOCTEXT("OneBraham", "Pause_Resume", "RESUME"), NSLOCTEXT("OneBraham", "Pause_ResumeWhere", "Back to your desk"),
		[this]() { RequestResume(); });
	Row_Settings = MakeDirectoryRow(NSLOCTEXT("OneBraham", "Pause_Settings", "FACILITIES"), NSLOCTEXT("OneBraham", "Pause_SettingsWhere", "Settings"),
		[this]() { ShowSettingsPage(); });
	Row_Leave = MakeDirectoryRow(NSLOCTEXT("OneBraham", "Pause_Leave", "LEAVE MATCH"), NSLOCTEXT("OneBraham", "Pause_LeaveWhere", "Go home early"),
		[this]() { ShowConfirm(EPendingConfirm::Leave); });
	Row_Quit = MakeDirectoryRow(NSLOCTEXT("OneBraham", "Pause_Quit", "SIGN OUT"), NSLOCTEXT("OneBraham", "Pause_QuitWhere", "Quit to desktop"),
		[this]() { ShowConfirm(EPendingConfirm::Quit); });

	// Up and down wrap, as on HOME.
	const TArray<TWeakObjectPtr<UOneBrahamPaintedControl>> Order = { Row_Resume.Get(), Row_Settings.Get(), Row_Leave.Get(), Row_Quit.Get() };
	for (int32 Index = 0; Index < Order.Num(); ++Index)
	{
		const TWeakObjectPtr<UOneBrahamPaintedControl> Up = Order[(Index + Order.Num() - 1) % Order.Num()];
		const TWeakObjectPtr<UOneBrahamPaintedControl> Down = Order[(Index + 1) % Order.Num()];
		Order[Index]->OnNavigate = [Up, Down](EUINavigation Direction) -> UWidget*
		{
			if (Direction == EUINavigation::Up) { return Up.Get(); }
			if (Direction == EUINavigation::Down) { return Down.Get(); }
			return nullptr;
		};
	}

	Column->AddChildToVerticalBox(Row_Resume);
	Column->AddChildToVerticalBox(Row_Settings);

	// A rule and a gap before the two exits, so the hand that reaches for
	// RESUME does not land on LEAVE MATCH.
	UOneBrahamPaintedControl* Rule = MakeLayer(FVector2D(ColumnWidth, 17.f));
	Rule->PaintFn = [](const FPainter& C, const FOneBrahamControlState&)
	{
		Box(C, FVector2D(0.f, 8.f), FVector2D(C.Size().X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.12f));
	};
	Column->AddChildToVerticalBox(Rule);
	Column->AddChildToVerticalBox(Row_Leave);
	Column->AddChildToVerticalBox(Row_Quit);

	// The key hints, as the front end's bottom bar sets them.
	UOneBrahamPaintedControl* Hints = MakeLayer(FVector2D(ColumnWidth, 40.f));
	Hints->PaintFn = [](const FPainter& C, const FOneBrahamControlState&)
	{
		float X = 0.f;
		auto Cap = [&](const TCHAR* Key, const TCHAR* Label)
		{
			const FSlateFontInfo KeyFont = Type(EOneBrahamType::Telemetry, 11.f);
			const FVector2D KeySize = Measure(FString(Key), KeyFont);
			const float W = FMath::Max(18.f, KeySize.X + 10.f);
			Rounded(C, FVector2D(X, 14.f), FVector2D(W, 18.f), 2.f, FLinearColor::Transparent, Tok(EOneBrahamColour::LightGrey, 0.4f), 1.f);
			Text(C, FVector2D(X + (W - KeySize.X) * 0.5f, 14.f + (18.f - KeySize.Y) * 0.5f), Key, KeyFont, Tok(EOneBrahamColour::LightGrey, 0.8f));
			X += W + 8.f;
			X += Text(C, FVector2D(X, 15.f), Label, Type(EOneBrahamType::Hint, 12.f), Tok(EOneBrahamColour::LightGrey, 0.5f)).X + 22.f;
		};
		Cap(TEXT("ENTER"), TEXT("Select"));
		Cap(TEXT("ESC"), TEXT("Back"));
	};
	Column->AddChildToVerticalBox(Hints)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));

	if (UCanvasPanelSlot* ColumnSlot = Main->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(0.08f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.f, 0.5f));
		ColumnSlot->SetAutoSize(true);
		ColumnSlot->SetPosition(FVector2D::ZeroVector);
	}

	// --- the status card: what the building is doing while you are away -------------
	UOneBrahamPaintedControl* Status = MakeLayer(FVector2D(520.f, 360.f));
	Status->PaintFn = [WeakThis](const FPainter& C, const FOneBrahamControlState& State)
	{
		using namespace OneBrahamHUDStyle;

		const UOneBrahamPauseMenuWidget* Menu = WeakThis.Get();
		const APlayerController* Owner = Menu != nullptr ? Menu->GetOwningPlayer() : nullptr;
		const UOneBrahamHUDComponent* HUD = Owner != nullptr ? Owner->FindComponentByClass<UOneBrahamHUDComponent>() : nullptr;
		if (HUD == nullptr)
		{
			return;
		}

		const FOneBrahamHUDFrame& Frame = HUD->GetHUDFrame();
		const FOneBrahamBuildingStatus& Building = Frame.Building;
		const int32 Band = StageBand(Building.Stage);
		const FLinearColor Accent = Band == 2 ? EmergencyRed() : (Band == 1 ? SafetyYellow() : SignalBlue());
		const float T = static_cast<float>(State.Now - Menu->OpenedAt);
		const float In = UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, (T - 0.04f) / 0.26f);

		OneBrahamPauseLayout::FBridge Bridge(C);
		FOneBrahamPainter P = Bridge.P.Child(FVector2D(12.f * (1.f - In), 0.f)).Faded(In);
		const FVector2D Size = C.Size();

		P.RoundRect(FVector2D::ZeroVector, Size, WithAlpha(Navy(), 0.86f), 4.f);
		P.Outline(FVector2D::ZeroVector, Size, WithAlpha(LightGrey(), 0.10f), 1.f, 4.f);
		P.Rect(FVector2D::ZeroVector, FVector2D(4.f, Size.Y), Accent);

		const float Pad = 28.f;
		P.Text(FVector2D(Pad, 24.f), TEXT("ONE BRAHAM  ·  WHILE YOU WERE OUT"), Font(EOneBrahamHUDType::Eyebrow, 11.f, 200), WithAlpha(LightGrey(), 0.6f));
		P.Text(FVector2D(Size.X - Pad, 22.f), Frame.DuskClock, Font(EOneBrahamHUDType::TelemetryLight, 13.f, 40), WithAlpha(LightGrey(), 0.7f), EOneBrahamTextAnchor::Right);

		const FString Mode = Frame.Mode == EOneBrahamMatchMode::FireDrill ? TEXT("FIRE DRILL")
			: (Frame.Mode == EOneBrahamMatchMode::NightShift ? TEXT("NIGHT SHIFT") : TEXT("ASCENT"));
		P.Text(FVector2D(Pad, 44.f), Mode, Font(EOneBrahamHUDType::Verb, 34.f, -10), White());

		// The fire panel's LED row: position is the stage.
		static const TCHAR* LEDs[4] = { TEXT("NORMAL"), TEXT("LOCKDOWN"), TEXT("PURGE"), TEXT("EVAC") };
		float LX = Pad;
		const FSlateFontInfo LEDFont = Font(EOneBrahamHUDType::Label, 10.f, 100);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const bool bLit = Index == Band;
			P.Circle(FVector2D(LX + 4.f, 108.f), 4.f, bLit ? Accent : WithAlpha(LightGrey(), 0.14f));
			LX += 12.f + P.Text(FVector2D(LX + 12.f, 101.f), LEDs[Index], LEDFont, WithAlpha(LightGrey(), bLit ? 1.f : 0.4f)).X + 14.f;
		}

		// The timer: the one number worth knowing before RESUME.
		FString What = TEXT("BUILDING SYSTEMS NORMAL");
		FString When = TEXT("--:--");
		if (Frame.Mode == EOneBrahamMatchMode::Ascent && Building.bDirectorRunning && Building.SecondsToNextStage > 0.f)
		{
			What = Building.Stage == EOneBrahamLockdownStage::Lockdown
				? FString::Printf(TEXT("PURGE L00-L%02d IN"), Building.PurgeCeiling)
				: (Building.NextSealCeiling != INDEX_NONE ? FString::Printf(TEXT("L%02d SEALS IN"), Building.NextSealCeiling) : TEXT("NEXT STAGE IN"));
			const int32 Secs = FMath::CeilToInt(Building.SecondsToNextStage);
			When = FString::Printf(TEXT("%02d:%02d"), Secs / 60, Secs % 60);
		}
		else if (Building.Stage == EOneBrahamLockdownStage::Purge)
		{
			What = FString::Printf(TEXT("PURGE FRONT L%02d"), Building.PurgeCeiling);
			When = TEXT("ACTIVE");
		}
		else if (Frame.TimeRemaining > 0.f)
		{
			What = TEXT("THE DRILL ENDS IN");
			const int32 Secs = FMath::CeilToInt(Frame.TimeRemaining);
			When = FString::Printf(TEXT("%02d:%02d"), Secs / 60, Secs % 60);
		}
		P.Rect(FVector2D(Pad, 130.f), FVector2D(Size.X - Pad * 2.f, 1.f), WithAlpha(Accent, 0.3f));
		P.Text(FVector2D(Pad, 152.f), What, Font(EOneBrahamHUDType::Label, 13.f, 60), White());
		P.Text(FVector2D(Size.X - Pad, 138.f), When, Font(EOneBrahamHUDType::TelemetryStrong, 30.f), Band > 0 ? Accent : White(), EOneBrahamTextAnchor::Right);

		// Where you left yourself: a small floor plate and the area.
		const int32 Floor = Building.LocalFloor;
		if (Floor != INDEX_NONE)
		{
			const FVector2D Plate(Pad, 196.f);
			const bool bDanger = Building.IsLocalInDanger();
			P.RoundRect(Plate, FVector2D(48.f, 48.f), bDanger ? EmergencyRed() : WithAlpha(SignageWhite(), 0.92f), 2.f);
			P.Text(Plate + FVector2D(24.f, 6.f), FString::Printf(TEXT("%02d"), Floor), Font(EOneBrahamHUDType::Title, 30.f, -20),
				bDanger ? White() : Navy(), EOneBrahamTextAnchor::Centre);
			const FOneBrahamFloorStatus* Here = Building.Find(Floor);
			P.Text(Plate + FVector2D(62.f, 4.f), Here != nullptr ? Here->ShortName.ToString().ToUpper() : FString(),
				Font(EOneBrahamHUDType::Label, 16.f, 60), White());
			P.Text(Plate + FVector2D(62.f, 26.f), bDanger ? TEXT("THIS FLOOR IS NOT SAFE") : TEXT("WHERE YOU LEFT YOURSELF"),
				Font(EOneBrahamHUDType::TelemetryLight, 11.f, 80), bDanger ? EmergencyRed() : WithAlpha(LightGrey(), 0.6f));
		}

		// The squad, in one line.
		int32 Inside = 0;
		int32 Down = 0;
		int32 Out = 0;
		for (const FOneBrahamSquadRowView& Row : Frame.Squad)
		{
			if (Row.bLocal)
			{
				continue;
			}
			if (Row.LifeState == EOneBrahamLifeState::Downed)
			{
				++Down;
			}
			else if (Row.LifeState == EOneBrahamLifeState::Dead || Row.LifeState == EOneBrahamLifeState::Spectating)
			{
				++Out;
			}
			else
			{
				++Inside;
			}
		}
		// The in/out board in one line, as tags: only the count that needs you
		// is lit, and it carries a filled tag as well as the red.
		{
			const FSlateFontInfo TagFont = Font(EOneBrahamHUDType::Telemetry, 12.f, 60);
			float TX = Pad;
			TX += P.Text(FVector2D(TX, 266.f), TEXT("SQUAD"), TagFont, WithAlpha(LightGrey(), 0.55f)).X + 14.f;
			auto Tag = [&P, &TX, &TagFont](const FString& Word, bool bLit, const FLinearColor& Lit)
			{
				const FVector2D Size = OneBrahamHUDStyle::Measure(Word, TagFont) + FVector2D(12.f, 4.f);
				if (bLit)
				{
					P.RoundRect(FVector2D(TX, 264.f), Size, Lit, 2.f);
				}
				else
				{
					P.Outline(FVector2D(TX, 264.f), Size, WithAlpha(LightGrey(), 0.35f), 1.f, 2.f);
				}
				P.Text(FVector2D(TX + 6.f, 266.f), Word, TagFont, bLit ? White() : WithAlpha(LightGrey(), 0.8f));
				TX += Size.X + 8.f;
			};
			Tag(FString::Printf(TEXT("%d IN"), Inside), false, SignalBlue());
			Tag(FString::Printf(TEXT("%d DOWN"), Down), Down > 0, EmergencyRed());
			Tag(FString::Printf(TEXT("%d OUT"), Out), false, SignalBlue());
		}

		P.Rect(FVector2D(Pad, 296.f), FVector2D(Size.X - Pad * 2.f, 1.f), WithAlpha(LightGrey(), 0.1f));
		P.Text(FVector2D(Pad, 310.f), TEXT("The building does not pause."), Font(EOneBrahamHUDType::Aside, 20.f), WithAlpha(LightGrey(), 0.75f));
	};
	if (UCanvasPanelSlot* StatusSlot = Main->AddChildToCanvas(Status))
	{
		StatusSlot->SetAnchors(FAnchors(0.62f, 0.5f));
		StatusSlot->SetAlignment(FVector2D(0.f, 0.5f));
		StatusSlot->SetAutoSize(true);
		StatusSlot->SetPosition(FVector2D::ZeroVector);
	}

	MainPage = Main;
	Pages->AddChild(Main);

	// --- page 1: FACILITIES -----------------------------------------------------------------
	// The settings screen draws its own panel (it is the same class the front
	// end shows); here it sits inside the frost at 80 x 68% of the screen.
	UOverlay* SettingsPage = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	Settings = CreateWidget<UOneBrahamSettingsWidget>(this, UOneBrahamSettingsWidget::StaticClass());
	if (Settings != nullptr)
	{
		Settings->OnBackRequested.BindUObject(this, &UOneBrahamPauseMenuWidget::ShowMainPage);

		USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Frame->SetWidthOverride(1536.f);
		Frame->SetHeightOverride(734.f);
		Frame->AddChild(Settings);

		UOverlaySlot* SettingsSlot = SettingsPage->AddChildToOverlay(Frame);
		SettingsSlot->SetHorizontalAlignment(HAlign_Center);
		SettingsSlot->SetVerticalAlignment(VAlign_Center);
	}
	Pages->AddChild(SettingsPage);

	// --- the visitor's badge: the confirmation ---------------------------------------------
	UOverlay* Confirm = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ConfirmLayer"));

	UOneBrahamPaintedControl* ConfirmDim = MakeLayer(FVector2D::ZeroVector);
	ConfirmDim->PaintFn = [](const FPainter& C, const FOneBrahamControlState&)
	{
		Box(C, FVector2D::ZeroVector, C.Size(), Tok(EOneBrahamColour::Navy, 0.55f));
	};
	Fill(Confirm->AddChildToOverlay(ConfirmDim));

	UOverlay* Badge = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOneBrahamPaintedControl* Face = MakeLayer(FVector2D(CardWidth, CardHeight));
	Face->PaintFn = [WeakThis](const FPainter& C, const FOneBrahamControlState& State)
	{
		const UOneBrahamPauseMenuWidget* Menu = WeakThis.Get();
		const bool bQuit = Menu != nullptr && Menu->PendingConfirm == EPendingConfirm::Quit;
		const UWorld* World = Menu != nullptr ? Menu->GetWorld() : nullptr;
		const bool bStandalone = World == nullptr || World->GetNetMode() == NM_Standalone;
		const FVector2D Size = C.Size();

		// A visitor's pass: the off-white face, the lanyard slot cut at the top.
		Rounded(C, FVector2D::ZeroVector, Size, 6.f, Tok(EOneBrahamColour::SignageWhite));
		Rounded(C, FVector2D(Size.X * 0.5f - 20.f, 10.f), FVector2D(40.f, 6.f), 3.f, Tok(EOneBrahamColour::OLEDBlack, 0.6f));
		Box(C, FVector2D(0.f, 0.f), FVector2D(Size.X, 4.f), Tok(EOneBrahamColour::SignalBlue));

		const FLinearColor Ink = Tok(EOneBrahamColour::Navy);
		Text(C, FVector2D(28.f, 30.f), TEXT("VISITOR"), Type(EOneBrahamType::TelemetryLight, 11.f, 200), Tok(EOneBrahamColour::Navy, 0.6f));
		Text(C, FVector2D(28.f, 46.f), bQuit ? TEXT("Sign out?") : TEXT("Go home early?"), Type(EOneBrahamType::Verb, 30.f, -10), Ink);
		const FText Body = bQuit
			? NSLOCTEXT("OneBraham", "Pause_QuitBody", "Your pass will be deactivated. The game will close.")
			: (bStandalone
				? NSLOCTEXT("OneBraham", "Pause_LeaveBodyStandalone", "You will be returned to reception. This session will not be kept.")
				: NSLOCTEXT("OneBraham", "Pause_LeaveBodyOnline", "Your squad will carry on one colleague short."));
		Text(C, FVector2D(28.f, 92.f), Body, Type(EOneBrahamType::Body, 15.f), Tok(EOneBrahamColour::Navy, 0.8f));
	};
	Badge->AddChildToOverlay(Face);

	auto MakeBadgeButton = [this](TFunction<FString()> LabelFn, bool bPrimary, TFunction<void()> OnChosen) -> UOneBrahamPaintedControl*
	{
		UOneBrahamPaintedControl* Button = CreateWidget<UOneBrahamPaintedControl>(this, UOneBrahamPaintedControl::StaticClass());
		Button->Setup(FVector2D(170.f, 44.f), true);
		Button->PaintFn = [LabelFn, bPrimary](const FPainter& C, const FOneBrahamControlState& State)
		{
			const FString LabelText = LabelFn();
			const FVector2D Size = C.Size();
			const FLinearColor Navy = Tok(EOneBrahamColour::Navy);
			if (bPrimary)
			{
				Rounded(C, FVector2D::ZeroVector, Size, 3.f, Tok(EOneBrahamColour::SignalBlue));
			}
			else
			{
				Rounded(C, FVector2D::ZeroVector, Size, 3.f, Tok(EOneBrahamColour::Navy, State.bLit ? 0.08f : 0.f), Navy, 1.f);
			}
			if (State.bLit)
			{
				// Focus: a navy ring outside the button, as a keyboard user expects.
				Rounded(C, FVector2D(-4.f, -4.f), Size + FVector2D(8.f, 8.f), 5.f, FLinearColor::Transparent, Navy, 2.f);
			}
			const FSlateFontInfo LabelFont = Type(EOneBrahamType::Verb, 15.f, 60);
			const FVector2D LabelSize = Measure(LabelText, LabelFont);
			Text(C, (Size - LabelSize) * 0.5f, LabelText, LabelFont, Navy);
		};
		TWeakObjectPtr<UOneBrahamPauseMenuWidget> Weak(this);
		Button->OnActivate = [Weak, OnChosen]()
		{
			if (Weak.IsValid())
			{
				OnChosen();
			}
		};
		return Button;
	};

	Button_ConfirmCancel = MakeBadgeButton([]() { return NSLOCTEXT("OneBraham", "Pause_ConfirmCancel", "STAY").ToString(); }, true,
		[this]() { CloseConfirm(); });
	Button_ConfirmAccept = MakeBadgeButton([WeakThis]()
		{
			const bool bQuit = WeakThis.IsValid() && WeakThis->PendingConfirm == EPendingConfirm::Quit;
			return (bQuit ? NSLOCTEXT("OneBraham", "Pause_QuitAccept", "SIGN OUT") : NSLOCTEXT("OneBraham", "Pause_LeaveAccept", "GO HOME")).ToString();
		}, false, [this]() { AcceptConfirm(); });
	{
		const TWeakObjectPtr<UOneBrahamPaintedControl> Stay = Button_ConfirmCancel.Get();
		const TWeakObjectPtr<UOneBrahamPaintedControl> Leave = Button_ConfirmAccept.Get();
		Button_ConfirmCancel->OnNavigate = [Leave](EUINavigation Direction) -> UWidget*
		{
			return Direction == EUINavigation::Right || Direction == EUINavigation::Left ? Leave.Get() : nullptr;
		};
		Button_ConfirmAccept->OnNavigate = [Stay](EUINavigation Direction) -> UWidget*
		{
			return Direction == EUINavigation::Right || Direction == EUINavigation::Left ? Stay.Get() : nullptr;
		};
	}

	UHorizontalBox* Answers = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Answers->AddChildToHorizontalBox(Button_ConfirmCancel)->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
	Answers->AddChildToHorizontalBox(Button_ConfirmAccept);
	UOverlaySlot* AnswersSlot = Badge->AddChildToOverlay(Answers);
	AnswersSlot->SetHorizontalAlignment(HAlign_Left);
	AnswersSlot->SetVerticalAlignment(VAlign_Bottom);
	AnswersSlot->SetPadding(FMargin(28.f, 0.f, 0.f, 24.f));

	UOverlaySlot* BadgeSlot = Confirm->AddChildToOverlay(Badge);
	BadgeSlot->SetHorizontalAlignment(HAlign_Center);
	BadgeSlot->SetVerticalAlignment(VAlign_Center);

	Confirm->SetVisibility(ESlateVisibility::Collapsed);
	ConfirmLayer = Confirm;
	Fill(Root->AddChildToOverlay(Confirm));

	SetVisibility(ESlateVisibility::Collapsed);
}

int32 UOneBrahamPauseMenuWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// The entrance: 180 ms in, the column sliding 12 px from the left. On the
	// real clock, because a standalone game is paused under this menu. Applied
	// as a render transform on the page, which costs nothing to set per frame.
	if (MainPage != nullptr && bOpen)
	{
		const float T = static_cast<float>(FPlatformTime::Seconds() - OpenedAt);
		const float In = UOneBrahamUIStyle::ReduceMotion() ? 1.f : UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, T / 0.18f);
		MainPage->SetRenderOpacity(In);
		MainPage->SetRenderTranslation(FVector2D(-12.f * (1.f - In), 0.f));
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

// ==========================================================================
//  Open / close
// ==========================================================================
void UOneBrahamPauseMenuWidget::OpenMenu()
{
	bOpen = true;
	OpenedAt = FPlatformTime::Seconds();
	SetVisibility(ESlateVisibility::Visible);

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

	return Row_Resume;
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

	// Shoulder buttons (and Q / E) walk the FACILITIES floors: the cheapest
	// gamepad navigation there is, and the one every console player tries first.
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

	// Back from FACILITIES lands on FACILITIES, where the player left; anything
	// else lands on RESUME.
	UWidget* Target = bFromSettings ? static_cast<UWidget*>(Row_Settings.Get()) : static_cast<UWidget*>(Row_Resume.Get());
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
	ConfirmAt = FPlatformTime::Seconds();

	if (ConfirmLayer != nullptr)
	{
		ConfirmLayer->SetVisibility(ESlateVisibility::Visible);
	}

	// The safe answer has focus: a double-tap of the confirm key stays.
	if (Button_ConfirmCancel != nullptr)
	{
		Button_ConfirmCancel->SetKeyboardFocus();
	}
}

void UOneBrahamPauseMenuWidget::CloseConfirm()
{
	const EPendingConfirm Was = PendingConfirm;
	PendingConfirm = EPendingConfirm::None;

	if (ConfirmLayer != nullptr)
	{
		ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Was != EPendingConfirm::None && bOpen)
	{
		UWidget* Back = Was == EPendingConfirm::Quit ? static_cast<UWidget*>(Row_Quit.Get()) : static_cast<UWidget*>(Row_Leave.Get());
		if (Back != nullptr)
		{
			Back->SetKeyboardFocus();
		}
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
