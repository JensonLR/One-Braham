// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamHUDWidget.h"
#include "UI/OneBrahamHUDStringTable.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "UI/OneBrahamCompassWidget.h"
#include "UI/OneBrahamCrosshairWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/FrontEnd/OneBrahamPaint.h"
#include "UI/FrontEnd/OneBrahamPaintedControl.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Internationalization/Regex.h"
#include "Styling/CoreStyle.h"

namespace OneBrahamHUDLayout
{
	/** The compass strip: 34% of the width, at the top of the safe area. */
	constexpr float CompassWidthFraction = 0.34f;
	constexpr float CompassTopFraction = 0.035f;
	/** Tall enough for the markers above the slats and their deltas below. */
	constexpr float CompassHeight = 64.f;

	/** How long a PA card stays after its words (the tannoy's audio + 1.5 s). */
	constexpr float AnnouncementTail = 1.5f;
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
	// canvas here and paint the rest, so a content-free checkout is complete.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}

	return Super::RebuildWidget();
}

void UOneBrahamHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// The Blueprint path binds its own widgets; the code path paints instead.
	if (Text_Ammo != nullptr)
	{
		Text_Ammo->SetFont(UOneBrahamUIStyle::TypeFont(EOneBrahamType::TelemetryStrong, 40.f, 0, true));
	}

	UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.UI] HUD widget constructed (%s layout)."),
		bBuiltLayoutInCode ? TEXT("painted") : TEXT("authored"));
}

double UOneBrahamHUDWidget::HUDNow() const
{
	// World time, the same clock the component stamps the kill feed and the
	// pings with, so "seven seconds" means the same thing on both sides.
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetTimeSeconds() : FPlatformTime::Seconds();
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

	FSlateFontInfo Font = UOneBrahamUIStyle::Font(Size, bBold);
	Font.OutlineSettings = FFontOutlineSettings(1);
	Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);
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
	using namespace OneBrahamHUDLayout;

	bBuiltLayoutInCode = true;

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDRoot"));
	WidgetTree->RootWidget = RootCanvas;

	// The underlay: the canvas's first child, so it paints beneath the compass.
	// The stage card's letterbox lives here - the compass never hides (Docs/08),
	// and painted over it the top bar took the strip to 15% for every card.
	Underlay = CreateWidget<UOneBrahamPaintedControl>(this, UOneBrahamPaintedControl::StaticClass());
	if (Underlay != nullptr)
	{
		Underlay->Setup(FVector2D::ZeroVector, false);
		Underlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		TWeakObjectPtr<UOneBrahamHUDWidget> WeakThis(this);
		Underlay->PaintFn = [WeakThis](const OneBrahamPaint::FPainter& Surface, const FOneBrahamControlState&)
		{
			if (const UOneBrahamHUDWidget* Widget = WeakThis.Get())
			{
				int32 Layer = Surface.Layer;
				FOneBrahamPainter P(Surface.Out, Surface.Geometry, Layer, FMath::Clamp(Widget->Data.HUDOpacity, 0.2f, 1.f));
				Widget->PaintUnderlay(P, Surface.Geometry);
			}
		};
		AddStretched(Underlay);
	}

	// The compass is its own widget - it repaints from the camera every frame,
	// faster than the 15 Hz the rest of the HUD's data arrives at - stretched
	// across the middle third of the top of the screen: 34% of any width, so a
	// 21:9 screen gets a wider strip rather than a stretched one.
	Compass = WidgetTree->ConstructWidget<UOneBrahamCompassWidget>(UOneBrahamCompassWidget::StaticClass(), TEXT("Compass"));
	if (UCanvasPanelSlot* CompassSlot = RootCanvas->AddChildToCanvas(Compass))
	{
		const float Half = CompassWidthFraction * 0.5f;
		CompassSlot->SetAnchors(FAnchors(0.5f - Half, CompassTopFraction, 0.5f + Half, CompassTopFraction));
		CompassSlot->SetOffsets(FMargin(0.f, -10.f, 0.f, CompassHeight));
		CompassSlot->SetAlignment(FVector2D(0.f, 0.f));
		CompassSlot->SetAutoSize(false);
	}

	// Everything else is painted (UI/HUD/OneBrahamHUDElements.cpp). The old
	// text blocks are not built: an authored Blueprint may still bind them.
	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD layout built in code: painted elements, compass widget."));
}

// ==========================================================================
//  Paint
// ==========================================================================
bool UOneBrahamHUDWidget::MakePaintContext(const FGeometry& Geometry, FPaintContext& C) const
{
	C.Now = HUDNow();
	C.Screen = Geometry.GetLocalSize();
	if (!bBuiltLayoutInCode || C.Screen.X < 64.f || C.Screen.Y < 64.f)
	{
		return false;
	}

	// The safe area: 2.5% left and right, 3.5% top and bottom (TV-safe margins
	// are the accessibility setting's business, not the layout's).
	C.SafeL = C.Screen.X * 0.025f;
	C.SafeR = C.Screen.X - C.SafeL;
	C.SafeT = C.Screen.Y * 0.035f;
	C.SafeB = C.Screen.Y - C.SafeT;
	C.Scale = FMath::Clamp(Frame.HUDScale, 0.8f, 1.2f);
	C.bReduceMotion = Frame.bReduceMotion;
	C.bRestDimming = Frame.bRestDimming;
	C.Stage = Frame.Building.Stage;
	C.Band = OneBrahamHUDStyle::StageBand(C.Stage);
	C.bHeavyOutline = C.Band == 2;
	C.bAlive = Data.LifeState == EOneBrahamLifeState::Alive || Data.LifeState == EOneBrahamLifeState::Downed;
	UpdateStageTint(C.Now, C);
	return true;
}

int32 UOneBrahamHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// --- Why there is no NativeTick here ---------------------------------
	// A C++ UUserWidget is created with EWidgetTickFrequency::Auto, and Auto
	// means "tick only if this widget has a tick event implemented in
	// Blueprint". An override of NativeTick is not one, so this widget's tick
	// would never be called. Paint is the one callback a visible widget is
	// guaranteed, so every element below is a function of the clock read here.
	FPaintContext C;
	if (!MakePaintContext(AllottedGeometry, C))
	{
		return Layer;
	}

	// The whole HUD under the accessibility opacity slider; the painter's own
	// opacity is how a painted element honours it.
	FOneBrahamPainter Painter(OutDrawElements, AllottedGeometry, Layer, FMath::Clamp(Data.HUDOpacity, 0.2f, 1.f));
	PaintHUD(Painter, C);

	return Layer + 1;
}

void UOneBrahamHUDWidget::PaintUnderlay(FOneBrahamPainter& P, const FGeometry& Geometry) const
{
	// A widget's NativePaint runs after its children have painted, so anything
	// the compass must stay on top of is painted here, by the canvas's first
	// child, instead.
	FPaintContext C;
	if (MakePaintContext(Geometry, C) && C.bAlive && !bDeathScreenVisible)
	{
		PaintStageBars(P, C);
	}
}

void UOneBrahamHUDWidget::UpdateStageTint(double Now, FPaintContext& C) const
{
	using namespace OneBrahamHUDStyle;

	// One getter drives every hairline and accent; a stage change cross-fades
	// the lot together over 400 ms, linearly, so no element changes alone.
	auto AccentFor = [](int32 Band) { return Band == 2 ? EmergencyRed() : (Band == 1 ? SafetyYellow() : SignalBlue()); };
	auto HairlineFor = [](int32 Band)
	{
		return Band == 2 ? WithAlpha(EmergencyRed(), 0.30f) : (Band == 1 ? WithAlpha(SafetyYellow(), 0.25f) : WithAlpha(LightGrey(), 0.20f));
	};

	const float T = FMath::Clamp(static_cast<float>((Now - TintChangedAt) / OneBrahamEase::StageCrossFade), 0.f, 1.f);
	C.Accent = FMath::Lerp(AccentFor(TintFromBand), AccentFor(TintBand), T);
	C.Hairline = FMath::Lerp(HairlineFor(TintFromBand), HairlineFor(TintBand), T);
}

// ==========================================================================
//  Data in
// ==========================================================================
void UOneBrahamHUDWidget::ApplyHUDData(const FOneBrahamHUDData& NewData)
{
	const double Now = HUDNow();
	const FOneBrahamHUDData Previous = Data;
	Data = NewData;

	// --- the staff pass: a hit leaves a ghost, a heal refills ---------------------
	const float MaxHealth = FMath::Max(1.f, Data.MaxHealth);
	if (Data.Health < Previous.Health - 0.5f && Previous.LifeState == EOneBrahamLifeState::Alive)
	{
		const float Lost = Previous.Health - Data.Health;
		// A ghost that is still draining keeps its top: three quick hits read as
		// one long red section, which is what they were.
		GhostFrom = (Now - GhostAt) < 0.65 ? FMath::Max(GhostFrom, Previous.Health) : Previous.Health;
		GhostTo = Data.Health;
		GhostAt = Now;
		CountFrom = ShownHealth;
		CountAt = Now;
		if (Lost > 20.f)
		{
			NudgeAt = Now;
		}
	}
	else if (Data.Health > Previous.Health + 0.5f)
	{
		HealFrom = Previous.Health / MaxHealth;
		HealAt = Now;
		CountFrom = ShownHealth;
		CountAt = Now;
	}
	ShownHealth = Data.Health;

	if (LastArmour > 0.5f && Data.Armour <= 0.5f)
	{
		ArmourBreakAt = Now;
	}
	LastArmour = Data.Armour;

	// --- the magazine: each shot fades the top tick -------------------------------
	if (LastAmmo >= 0 && Data.AmmoInMagazine < LastAmmo && Data.WeaponId == Previous.WeaponId)
	{
		ShotAt = Now;
		NotifyFireInput();
	}
	LastAmmo = Data.AmmoInMagazine;

	if (Data.FireMode != LastFireMode)
	{
		PrevFireMode = LastFireMode;
		LastFireMode = Data.FireMode;
		FireModeAt = Now;
	}

	if (Data.bReloading && !bWasReloading)
	{
		ReloadStartAt = Now;
	}
	bWasReloading = Data.bReloading;

	// --- hit markers: the snapshot's alpha jumps to 1 on a confirmed hit -----------
	if (Data.HitMarkerAlpha > LastHitMarkerAlpha + 0.05f && Data.HitMarkerAlpha > 0.5f)
	{
		HitAt = Now;
		bHitHeadshot = Data.bLastHitWasHeadshot;
		bHitKill = Data.bLastHitWasKill;
	}
	LastHitMarkerAlpha = Data.HitMarkerAlpha;

	// --- the card reader: a hold that completed --------------------------------------
	if (LastHoldProgress >= 0.9f && Data.InteractionHoldProgress01 < 0.05f && !Data.bInteractionBlocked)
	{
		ReaderAcceptedAt = Now;
	}
	LastHoldProgress = Data.InteractionHoldProgress01;
	if (Data.bHasInteractionTarget)
	{
		LastPromptText = (Data.bInteractionBlocked && !Data.InteractionBlockedReason.IsEmpty()
			? Data.InteractionBlockedReason : Data.InteractionPrompt).ToString();
	}

	// --- downed ------------------------------------------------------------------------------
	const bool bDowned = Data.LifeState == EOneBrahamLifeState::Downed;
	if (bDowned && !bWasDowned)
	{
		DownedAt = Now;
	}
	bWasDowned = bDowned;

	// --- subtitles from the snapshot (the tannoy's words arrive this way) --------------
	if (!Data.SubtitleText.IsEmpty() && Data.SubtitleRemaining > 0.f && Data.SubtitleText.ToString() != SubtitleShown)
	{
		SubtitleShown = Data.SubtitleText.ToString();
		SubtitleSpeaker = Data.SubtitleSpeaker;
		SubtitleAt = Now;
		SubtitleSeconds = Data.SubtitleRemaining;
	}

	if (!Data.BannerText.IsEmpty() && Data.BannerRemaining > 0.f && Data.BannerText.ToString() != BannerShown)
	{
		ShowBanner(Data.BannerText, Data.BannerRemaining, Data.BannerColour);
	}

	// --- the authored path ------------------------------------------------------------------
	if (!bBuiltLayoutInCode)
	{
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
		UpdateOverlays(Data);
	}

	UpdateCompassData(Data);
}

void UOneBrahamHUDWidget::ApplyHUDFrame(const FOneBrahamHUDFrame& NewFrame)
{
	const double Now = HUDNow();
	const FOneBrahamBuildingStatus& Building = NewFrame.Building;

	// --- the floor plate's split-flap ----------------------------------------------
	if (Building.LocalFloor != ShownFloor)
	{
		const bool bFirst = ShownFloor == INDEX_NONE;
		PreviousFloor = ShownFloor;
		ShownFloor = Building.LocalFloor;
		FloorChangedAt = bFirst ? -100.0 : Now;

		if (const FOneBrahamFloorStatus* Here = Building.Find(ShownFloor))
		{
			PlateArea = Here->ShortName.ToString().ToUpper();
			PlateAreaAt = bFirst ? -100.0 : Now;
		}

		// The lift indicator appears on the first move: before that the
		// player is standing on the plaza and a column of eighteen empty cells
		// is noise.
		if (!bFirst && !bStackRevealed)
		{
			bStackRevealed = true;
			StackChangedAt = Now;
		}
	}

	// --- the stage: tint, the fire panel's clock, the OVERRIDE -----------------------
	const int32 Band = OneBrahamHUDStyle::StageBand(Building.Stage);
	if (!bStagePrimed)
	{
		bStagePrimed = true;
		LastStage = Building.Stage;
		TintBand = TintFromBand = Band;
		StageStartedAt = Band > 0 ? -100.0 : Now;
		LastSealCeiling = Building.SealCeiling;
	}
	else if (Building.Stage != LastStage)
	{
		LastStage = Building.Stage;
		StageStartedAt = Now;

		if (Band != TintBand)
		{
			// The colour the tint was showing, frozen, becomes the start of the fade.
			TintFromBand = TintBand;
			TintBand = Band;
			TintChangedAt = Now;
		}

		// The building overrides the objective: the old line stays, struck
		// through in the stage colour, with the new instruction under it.
		if (Band > 0 && !ObjectiveShown.IsEmpty())
		{
			OverriddenLine = ObjectiveShown;
			OverriddenDetail = ObjectiveDetailShown;
			OverrideAt = Now;
			OverrideBand = Band;
		}

		bStackRevealed = bStackRevealed || Band > 0;
		StackChangedAt = Now;
	}

	// --- the LCD strip: one line per change in the sealed zones ------------------------
	if (Building.SealCeiling != LastSealCeiling || (Building.Stage == EOneBrahamLockdownStage::Purge && LCDLines.Num() == 0))
	{
		FString Line;
		if (Building.Stage == EOneBrahamLockdownStage::Purge)
		{
			Line = FString::Printf(TEXT("PURGE ZONES 00-%02d ACTIVE"), Building.PurgeCeiling);
		}
		else if (Building.SealCeiling != INDEX_NONE)
		{
			Line = FString::Printf(TEXT("ZONES 00-%02d SEALED"), Building.SealCeiling);
		}

		if (!Line.IsEmpty() && (LCDLines.Num() == 0 || LCDLines.Last() != Line))
		{
			LCDLines.Add(Line);
			while (LCDLines.Num() > 4)
			{
				LCDLines.RemoveAt(0);
			}
			LCDScrolledAt = Now;
		}
		LastSealCeiling = Building.SealCeiling;
	}

	// --- the objective line ---------------------------------------------------------------
	const FString NewObjective = NewFrame.Objective.ToString();
	if (NewObjective != ObjectiveShown || NewFrame.ObjectiveDetail != ObjectiveDetailShown)
	{
		// A detail tick ("8 FLOORS" to "7 FLOORS") is not a new objective: only a
		// new sentence gets the wipe and the ribbon.
		if (NewObjective != ObjectiveShown)
		{
			ObjectiveChangedAt = Now;
		}
		ObjectiveShown = NewObjective;
		ObjectiveDetailShown = NewFrame.ObjectiveDetail;
	}

	// --- rest dimming: what changed, and when --------------------------------------------
	FString Stack;
	for (const FOneBrahamFloorStatus& Floor : Building.Floors)
	{
		Stack.AppendChar(TEXT('0') + static_cast<TCHAR>(Floor.Condition));
		Stack.AppendChar(Floor.bLocal ? TEXT('*') : TEXT('.'));
		Stack.AppendInt(Floor.Mates * 10 + Floor.MatesDowned);
	}
	if (Stack != StackSignature)
	{
		StackSignature = Stack;
		StackChangedAt = Now;
	}

	FString Squad;
	for (const FOneBrahamSquadRowView& Row : NewFrame.Squad)
	{
		Squad += FString::Printf(TEXT("%d:%d:%d:%d:%d;"), Row.PlayerId, Row.Floor, static_cast<int32>(Row.LifeState),
			FMath::RoundToInt(Row.HealthFraction * 4.f), Row.bTransmitting ? 1 : 0);
	}
	if (Squad != SquadSignature)
	{
		SquadSignature = Squad;
		SquadChangedAt = Now;
	}

	Frame = NewFrame;

	// --- the PA card queue: one at a time, the next when the last has gone -------------
	const bool bCurrentDone = CurrentAnnouncement.At < 0.0
		|| Now > CurrentAnnouncement.At + CurrentAnnouncement.Hold + 0.3;
	if (bCurrentDone && AnnouncementQueue.Num() > 0 && !IsStageCardUp(Now))
	{
		CurrentAnnouncement = AnnouncementQueue[0];
		CurrentAnnouncement.At = Now;
		AnnouncementQueue.RemoveAt(0);
	}

	// --- the compass's markers and the stage it draws its tags in ------------------------
	if (Compass != nullptr)
	{
		Compass->SetFrame(Frame.Compass, Frame.Building.Stage, Frame.bReduceMotion, Frame.HUDScale);

		// The strip goes with the rest of the HUD when the player is out: the
		// accident book and the spectator's lower third are all that is left.
		const bool bOut = Data.LifeState == EOneBrahamLifeState::Dead || Data.LifeState == EOneBrahamLifeState::Spectating;
		Compass->SetCompassVisible(Data.bShowCompass && !bOut && !bDeathScreenVisible);
	}
}

void UOneBrahamHUDWidget::PushAnnouncement(const FText& Headline, const FText& Subline, EOneBrahamLockdownStage Stage, float HoldSeconds)
{
	if (Headline.IsEmpty())
	{
		return;
	}

	FAnnouncement Entry;
	Entry.Headline = Headline.ToString();
	Entry.Subline = Subline.ToString();
	Entry.Stage = Stage;
	Entry.Hold = FMath::Clamp(HoldSeconds, 2.f, 14.f) + OneBrahamHUDLayout::AnnouncementTail;
	Entry.Eyebrow = FString::Printf(TEXT("ONE BRAHAM  ·  ALL FLOORS  ·  %s"),
		Frame.DuskClock.IsEmpty() ? *OneBrahamDuskClock::Now(GetWorld()).ToString() : *Frame.DuskClock);

	// The same sentence twice in a row is one announcement repeated by the
	// server, not two: the card would flash off and on for nothing.
	if ((CurrentAnnouncement.At >= 0.0 && CurrentAnnouncement.Headline == Entry.Headline
		&& HUDNow() < CurrentAnnouncement.At + CurrentAnnouncement.Hold)
		|| AnnouncementQueue.ContainsByPredicate([&Entry](const FAnnouncement& A) { return A.Headline == Entry.Headline; }))
	{
		return;
	}

	AnnouncementQueue.Add(Entry);
	while (AnnouncementQueue.Num() > 3)
	{
		AnnouncementQueue.RemoveAt(0);
	}

	if ((CurrentAnnouncement.At < 0.0 || HUDNow() > CurrentAnnouncement.At + CurrentAnnouncement.Hold + 0.3) && !IsStageCardUp(HUDNow()))
	{
		CurrentAnnouncement = AnnouncementQueue[0];
		CurrentAnnouncement.At = HUDNow();
		AnnouncementQueue.RemoveAt(0);
	}
}

void UOneBrahamHUDWidget::ShowStageCard(EOneBrahamStageCard Card)
{
	if (Card == EOneBrahamStageCard::None)
	{
		return;
	}

	StageCard = Card;
	StageCardAt = HUDNow();

	// The card is the building's loudest voice: a PA line already up leaves
	// now (its 300 ms exit), and anything queued waits until the card has gone.
	if (CurrentAnnouncement.At >= 0.0 && StageCardAt < CurrentAnnouncement.At + CurrentAnnouncement.Hold)
	{
		CurrentAnnouncement.Hold = FMath::Max(0.f, static_cast<float>(StageCardAt - CurrentAnnouncement.At));
	}
	UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.UI] Stage card %d at %.2f"), static_cast<int32>(Card), StageCardAt);
	StageCardHold = 2.f;
	StageCardEyebrow = FString::Printf(TEXT("%s  ·  ALL FLOORS"),
		Frame.DuskClock.IsEmpty() ? *OneBrahamDuskClock::Now(GetWorld()).ToString() : *Frame.DuskClock);
}

void UOneBrahamHUDWidget::NotifyFireInput()
{
	// A player who fires is busy: the card gets out of the way at twice the pace.
	if (StageCard == EOneBrahamStageCard::None)
	{
		return;
	}

	const double Now = HUDNow();
	const double HoldStart = StageCardAt + 0.35;
	const double HoldEnd = HoldStart + StageCardHold;
	if (Now > HoldStart && Now < HoldEnd)
	{
		const float Left = static_cast<float>(HoldEnd - Now);
		StageCardHold -= Left * 0.5f;
	}
}

void UOneBrahamHUDWidget::PushDamage(float Damage, const FVector& FromLocation, bool bHasSource, int32 FloorDelta)
{
	const double Now = HUDNow();

	if (bHasSource)
	{
		FDamageArc& Arc = DamageArcs.AddDefaulted_GetRef();
		Arc.From = FromLocation;
		Arc.Damage = Damage;
		Arc.At = Now;
		Arc.FloorDelta = FloorDelta;
		while (DamageArcs.Num() > 6)
		{
			DamageArcs.RemoveAt(0);
		}
	}

	if (Damage > 40.f)
	{
		HeavyHitAt = Now;
	}
}

// ==========================================================================
//  The authored path: bound widgets, kept for a designer's Blueprint HUD
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
		Text_Access->SetText(BuildAccessRow(InData));
		Text_Access->SetVisibility(InData.bShowFloorIndicator ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

FText UOneBrahamHUDWidget::BuildAccessRow(const FOneBrahamHUDData& InData) const
{
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
		Text_Ammo->SetVisibility(InData.bShowAmmoCount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Text_Ammo->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), InData.AmmoInMagazine, InData.AmmoReserve)));
		const bool bLow = InData.MagazineSize > 0 && InData.AmmoInMagazine <= FMath::Max(2, InData.MagazineSize / 5);
		Text_Ammo->SetColorAndOpacity(FSlateColor(bLow ? UOneBrahamUIStyle::SafetyYellow() : UOneBrahamUIStyle::LightGrey()));
	}

	if (Text_WeaponName != nullptr)
	{
		Text_WeaponName->SetText(InData.WeaponDisplayName);
	}

	if (Text_FireMode != nullptr)
	{
		Text_FireMode->SetText(UEnum::GetDisplayValueAsText(InData.FireMode));
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

	if (Text_Objective != nullptr)
	{
		Text_Objective->SetText(Frame.Objective);
	}

	if (Bar_Objective != nullptr)
	{
		Bar_Objective->SetVisibility(InData.bExtractionActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Bar_Objective->SetPercent(InData.ExtractionHoldProgress01);
	}

	if (Text_Clock != nullptr)
	{
		Text_Clock->SetText(UOneBrahamUIStyle::ClockLabel(InData.MatchTimeRemaining));
		Text_Clock->SetVisibility(InData.MatchTimeRemaining > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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

	// One line per member, joined by line breaks: a list of names concatenated
	// without them is how "BOT BrennanBOT Patel" reached a screen.
	TArray<FString> Lines;
	for (const FOneBrahamHUDSquadMember& Member : InData.Squad)
	{
		const FString Floor = Member.FloorIndex != INDEX_NONE
			? FText::Format(OneBrahamHUDStrings::Get(TEXT("HUD_Floor_Format")), FText::AsNumber(Member.FloorIndex)).ToString()
			: FString();
		Lines.Add(FString::Printf(TEXT("%s  %s"), *Member.PlayerName, *Floor));
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
	const int32 StartIndex = FMath::Max(0, InData.KillFeed.Num() - 5);
	for (int32 Index = InData.KillFeed.Num() - 1; Index >= StartIndex; --Index)
	{
		const FOneBrahamHUDKillFeedEntry& Entry = InData.KillFeed[Index];
		// Always a separator between the two names: the verb used to be the
		// only thing between them, and an ordinary kill has no verb.
		Lines.Add(FString::Printf(TEXT("%s  →  %s"), *Entry.KillerName, *Entry.VictimName));
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
	Text_Interaction->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Bar_Interaction->SetVisibility(bVisible && InData.InteractionHoldProgress01 > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (bVisible)
	{
		Text_Interaction->SetText(InData.bInteractionBlocked && !InData.InteractionBlockedReason.IsEmpty()
			? InData.InteractionBlockedReason : InData.InteractionPrompt);
		Bar_Interaction->SetPercent(InData.InteractionHoldProgress01);
	}
}

void UOneBrahamHUDWidget::UpdateVoicePanel(const FOneBrahamHUDData& InData)
{
	if (Text_Voice == nullptr)
	{
		return;
	}

	const bool bShow = InData.bShowVoiceIndicator && InData.bLocalTransmitting;
	Text_Voice->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Text_Voice->SetText(InData.VoiceChannelName);
}

void UOneBrahamHUDWidget::UpdateCompassData(const FOneBrahamHUDData& InData)
{
	if (Compass == nullptr)
	{
		return;
	}

	// The compass never hides while the setting has it on and the player is in
	// the fight (Docs/08): it is the one navigation aid in a game with no minimap.
	const bool bOut = InData.LifeState == EOneBrahamLifeState::Dead || InData.LifeState == EOneBrahamLifeState::Spectating;
	Compass->SetCompassVisible(InData.bShowCompass && !bOut && !bDeathScreenVisible);
	Compass->SetMarkers(InData.CompassMarkers);
}

void UOneBrahamHUDWidget::UpdateOverlays(const FOneBrahamHUDData& InData)
{
	if (Panel_Downed != nullptr)
	{
		const bool bDowned = InData.LifeState == EOneBrahamLifeState::Downed;
		Panel_Downed->SetVisibility(bDowned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bDowned && Text_BleedOut != nullptr)
		{
			Text_BleedOut->SetText(FText::Format(
				NSLOCTEXT("OneBraham", "HUD_BigValue", "{0}  {1}s"),
				OneBrahamHUDStrings::Get(TEXT("HUD_Downed_BleedOut")),
				FText::AsNumber(FMath::CeilToInt(InData.BleedOutRemaining))));
		}
	}

	if (Text_Subtitle != nullptr)
	{
		const bool bHasSubtitle = InData.bShowSubtitles && !InData.SubtitleText.IsEmpty() && InData.SubtitleRemaining > 0.f;
		Text_Subtitle->SetVisibility(bHasSubtitle ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasSubtitle)
		{
			Text_Subtitle->SetText(InData.SubtitleText);
		}
	}
}

// ==========================================================================
//  Overlay commands
// ==========================================================================
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

	if (bVisible && !bDeathScreenVisible)
	{
		// The book is stamped with the moment and the place: the clock the
		// building keeps, and the floor the player fell on.
		DeathAt = HUDNow();
		DeathTime = Frame.DuskClock;
		++DeathEntryNumber;
		DeathLocation.Reset();
		if (const FOneBrahamFloorStatus* Floor = Frame.Building.Find(ShownFloor))
		{
			DeathLocation = FString::Printf(TEXT("Level %02d · %s"), ShownFloor, *Floor->AreaName.ToString());
		}
	}

	bDeathScreenVisible = bVisible;
	if (bVisible)
	{
		DeathText = CauseText;
	}
}

void UOneBrahamHUDWidget::TogglePauseMenu(bool bVisible)
{
	if (Panel_PauseMenu != nullptr)
	{
		Panel_PauseMenu->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UOneBrahamHUDWidget::SetDownedVisible(bool bDowned)
{
	if (Panel_Downed != nullptr)
	{
		Panel_Downed->SetVisibility(bDowned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bDowned && !bWasDowned)
	{
		DownedAt = HUDNow();
	}
	bWasDowned = bDowned;
}

void UOneBrahamHUDWidget::ShowBanner(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	BannerShown = Text.ToString();
	BannerColour = Colour;
	BannerAt = HUDNow();
	BannerSeconds = FMath::Max(0.6f, DurationSeconds);

	if (Text_Banner != nullptr)
	{
		Text_Banner->SetText(Text);
		Text_Banner->SetColorAndOpacity(FSlateColor(Colour));
		Text_Banner->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UOneBrahamHUDWidget::ShowSubBanner(const FText& Text, float DurationSeconds)
{
	SubBannerShown = Text.ToString();
	SubBannerAt = HUDNow();
	SubBannerSeconds = FMath::Max(0.6f, DurationSeconds);

	if (Text_SubBanner != nullptr)
	{
		Text_SubBanner->SetText(Text);
		Text_SubBanner->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UOneBrahamHUDWidget::ShowSubtitle(const FText& Text, const FString& SpeakerName, float DurationSeconds)
{
	SubtitleShown = Text.ToString();
	SubtitleSpeaker = SpeakerName;
	SubtitleAt = HUDNow();
	SubtitleSeconds = FMath::Max(0.75f, DurationSeconds);

	if (Text_Subtitle != nullptr)
	{
		Text_Subtitle->SetText(Text);
		Text_Subtitle->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UOneBrahamHUDWidget::AddWorldPing(const FOneBrahamHUDWorldPing& Ping)
{
	// Pings are drawn in the world by the marker component; the HUD keeps the
	// list so the compass can hang a diamond on the strip for their six seconds.
	RecentPings.Add(Ping);

	static constexpr int32 MaxPings = 8;
	while (RecentPings.Num() > MaxPings)
	{
		RecentPings.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

// ==========================================================================
//  The accident book's reading of the death text
// ==========================================================================
void UOneBrahamHUDWidget::ParseDeathText(const FText& Text, FString& OutCause, TArray<FRecapRow>& OutRows, FString& OutRemaining)
{
	OutCause.Reset();
	OutRows.Reset();
	OutRemaining.Reset();

	// The controller composes "cause\n\nrecap line\nrecap line": the cause is
	// the first paragraph, the recap the second (Core/OneBrahamDeathRecap.h).
	TArray<FString> Lines;
	Text.ToString().ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
	if (Lines.Num() == 0)
	{
		return;
	}

	OutCause = Lines[0];

	static const FRegexPattern RecapLine(TEXT("^(.+?) - (\\d+) hits?, (\\d+) damage \\((.*)\\) at (\\d+) m$"));
	static const FRegexPattern RemainingLine(TEXT("^(.+?) had (\\d+) health(?: and (\\d+) armour)? left$"));

	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];

		FRegexMatcher Recap(RecapLine, Line);
		if (Recap.FindNext())
		{
			FRecapRow& Row = OutRows.AddDefaulted_GetRef();
			Row.Attacker = Recap.GetCaptureGroup(1);
			Row.Hits = Recap.GetCaptureGroup(2);
			Row.Damage = Recap.GetCaptureGroup(3);
			Row.Locations = Recap.GetCaptureGroup(4);
			Row.Distance = Recap.GetCaptureGroup(5) + TEXT(" m");
			Row.bParsed = true;
			Row.Raw = Line;
			continue;
		}

		FRegexMatcher Remaining(RemainingLine, Line);
		if (Remaining.FindNext())
		{
			const FString Armour = Remaining.GetCaptureGroup(3);
			OutRemaining = Armour.IsEmpty()
				? FString::Printf(TEXT("%s %s HP"), *Remaining.GetCaptureGroup(1), *Remaining.GetCaptureGroup(2))
				: FString::Printf(TEXT("%s %s HP · %s ARMOUR"), *Remaining.GetCaptureGroup(1), *Remaining.GetCaptureGroup(2), *Armour);
			continue;
		}

		FRecapRow& Row = OutRows.AddDefaulted_GetRef();
		Row.Raw = Line;
	}
}
