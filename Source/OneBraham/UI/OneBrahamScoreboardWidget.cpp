// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamScoreboardWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "UI/HUD/OneBrahamBuildingStatus.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

namespace OneBrahamScoreboard
{
	// Column widths, in the order the cells are added. The headings and every
	// row read from this one table, which is the whole of the column alignment.
	constexpr float Lamp		= 26.f;
	constexpr float Name		= 300.f;
	constexpr float Archetype	= 220.f;
	constexpr float Status		= 150.f;
	constexpr float Stat		= 58.f;
	constexpr float Objective	= 72.f;
	constexpr float Ping		= 72.f;
	constexpr float Mute		= 98.f;

	constexpr float PanelWidth	= 1228.f;
	constexpr float RowHeight	= 34.f;

	/** The squads' signage names. Team enum, not squad id, because the squad's
	 *  letter is what is painted on its spawn entrance. */
	FText SquadName(EOneBrahamTeam Team, int32 SquadId)
	{
		switch (Team)
		{
		case EOneBrahamTeam::Alpha:		return NSLOCTEXT("OneBraham", "Scoreboard_SquadAlpha", "SQUAD ALPHA");
		case EOneBrahamTeam::Bravo:		return NSLOCTEXT("OneBraham", "Scoreboard_SquadBravo", "SQUAD BRAVO");
		case EOneBrahamTeam::Charlie:	return NSLOCTEXT("OneBraham", "Scoreboard_SquadCharlie", "SQUAD CHARLIE");
		case EOneBrahamTeam::Delta:		return NSLOCTEXT("OneBraham", "Scoreboard_SquadDelta", "SQUAD DELTA");
		default:
			return FText::Format(NSLOCTEXT("OneBraham", "Scoreboard_SquadNumber", "SQUAD {0}"),
				FText::AsNumber(SquadId + 1));
		}
	}

	FText ModeLabel(EOneBrahamMatchMode Mode)
	{
		switch (Mode)
		{
		case EOneBrahamMatchMode::FireDrill:	return NSLOCTEXT("OneBraham", "Scoreboard_ModeFireDrill", "FIRE DRILL");
		case EOneBrahamMatchMode::NightShift:	return NSLOCTEXT("OneBraham", "Scoreboard_ModeNightShift", "NIGHT SHIFT");
		default:								return NSLOCTEXT("OneBraham", "Scoreboard_ModeAscent", "ASCENT");
		}
	}

	FText PhaseLabel(EOneBrahamMatchPhase Phase)
	{
		switch (Phase)
		{
		case EOneBrahamMatchPhase::Warmup:		return NSLOCTEXT("OneBraham", "Scoreboard_PhaseWarmup", "WARM-UP. NOTHING COUNTS YET");
		case EOneBrahamMatchPhase::Active:		return NSLOCTEXT("OneBraham", "Scoreboard_PhaseActive", "IN PROGRESS");
		case EOneBrahamMatchPhase::Extraction:	return NSLOCTEXT("OneBraham", "Scoreboard_PhaseExtraction", "EXTRACTION CALLED");
		case EOneBrahamMatchPhase::Complete:	return NSLOCTEXT("OneBraham", "Scoreboard_PhaseComplete", "CONCLUDED");
		case EOneBrahamMatchPhase::PostMatch:	return NSLOCTEXT("OneBraham", "Scoreboard_PhasePostMatch", "CLOSING");
		default:								return FText::GetEmpty();
		}
	}

	/** FIRE DRILL's TeamScores index: 0 = Internal Staff, 1 = External Contractors
	 *  (AOneBrahamGameMode::AddTeamScoreByIndex). */
	int32 TeamScore(const AOneBrahamGameState* GameState, int32 TeamIndex)
	{
		return GameState != nullptr && GameState->TeamScores.IsValidIndex(TeamIndex)
			? GameState->TeamScores[TeamIndex] : 0;
	}

	FLinearColor WithAlpha(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Alpha);
	}

	FSlateFontInfo Type(EOneBrahamType Token, float Px, int32 Tracking = 0)
	{
		return UOneBrahamUIStyle::TypeFont(Token, Px, Tracking);
	}

	/** A tag's frame: outlined, or filled. Rounded 2 px, as the HUD's tags. */
	FSlateBrush TagBrush(const FLinearColor& Colour, bool bFilled, float Radius = 2.f)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(bFilled ? Colour : FLinearColor(Colour.R, Colour.G, Colour.B, 0.f));
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius));
		Brush.OutlineSettings.Color = FSlateColor(Colour);
		Brush.OutlineSettings.Width = bFilled ? 0.f : 1.f;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}
}

UTextBlock* UOneBrahamScoreboardWidget::Label(const FText& Text, const FSlateFontInfo& Font, const FLinearColor& Colour)
{
	UTextBlock* Block = MakeLabel(Text, EOneBrahamTextSize::Small, Colour);
	Block->SetFont(Font);
	return Block;
}

UOneBrahamScoreboardWidget::UOneBrahamScoreboardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The board is read while the game keeps the keyboard: Tab is held, and its
	// release has to reach the player's input to close the board again. A board
	// that took focus would eat that release and stay open.
	SetIsFocusable(false);
}

void UOneBrahamScoreboardWidget::NativeDestruct()
{
	if (RefreshHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RefreshHandle);
		RefreshHandle.Reset();
	}

	Super::NativeDestruct();
}

// ==========================================================================
//  Pure helpers
// ==========================================================================
FText UOneBrahamScoreboardWidget::ArchetypeLabel(const FGameplayTag& ArchetypeTag)
{
	if (!ArchetypeTag.IsValid())
	{
		return NSLOCTEXT("OneBraham", "Scoreboard_ArchetypeUnknown", "-");
	}

	FString Leaf = ArchetypeTag.GetTagName().ToString();
	int32 Dot = INDEX_NONE;
	if (Leaf.FindLastChar(TEXT('.'), Dot))
	{
		Leaf.RightChopInline(Dot + 1);
	}

	return SignageCase(Leaf);
}

FText UOneBrahamScoreboardWidget::LifeStateLabel(EOneBrahamLifeState State)
{
	// The building's words, not the game's: a directory board does not say "dead".
	switch (State)
	{
	case EOneBrahamLifeState::Alive:		return NSLOCTEXT("OneBraham", "Scoreboard_StateAlive", "ON SITE");
	case EOneBrahamLifeState::Downed:		return NSLOCTEXT("OneBraham", "Scoreboard_StateDowned", "DOWNED");
	case EOneBrahamLifeState::Dead:
	case EOneBrahamLifeState::Spectating:	return NSLOCTEXT("OneBraham", "Scoreboard_StateEliminated", "SIGNED OUT");
	case EOneBrahamLifeState::Extracted:	return NSLOCTEXT("OneBraham", "Scoreboard_StateExtracted", "EXTRACTED");
	default:								return FText::GetEmpty();
	}
}

int32 UOneBrahamScoreboardWidget::GroupKeyFor(EOneBrahamMatchMode Mode, EOneBrahamTeam Team, int32 SquadId)
{
	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		if (Team == EOneBrahamTeam::InternalStaff)			{ return 0; }
		if (Team == EOneBrahamTeam::ExternalContractors)	{ return 1; }
		return INDEX_NONE;
	}

	return SquadId >= 0 ? SquadId : INDEX_NONE;
}

// ==========================================================================
//  Open / close / refresh
// ==========================================================================
void UOneBrahamScoreboardWidget::SetScoreboardOpen(bool bInOpen)
{
	bOpen = bInOpen;

	// Visible, not SelfHitTestInvisible: the board is a solid object while it is
	// up, so a click on its background lands on it and not on the trigger.
	SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (bOpen)
	{
		OpenedAt = FPlatformTime::Seconds();
		RefreshNow();

		if (!RefreshHandle.IsValid())
		{
			RefreshHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateUObject(this, &UOneBrahamScoreboardWidget::HandleRefreshTick),
				RefreshIntervalSeconds);
		}
	}
	else if (RefreshHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RefreshHandle);
		RefreshHandle.Reset();
	}
}

bool UOneBrahamScoreboardWidget::HandleRefreshTick(float DeltaTime)
{
	if (!bOpen)
	{
		RefreshHandle.Reset();
		return false;
	}

	RefreshNow();
	return true;
}

void UOneBrahamScoreboardWidget::RefreshNow()
{
	if (!bBuiltLayoutInCode || RowList == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GameState = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GameState == nullptr)
	{
		return;
	}

	RefreshHeader(GameState);

	const AOneBrahamPlayerState* LocalState = GetOwningPlayerState<AOneBrahamPlayerState>();
	const int32 LocalGroup = LocalState != nullptr
		? GroupKeyFor(GameState->MatchMode, LocalState->Team, LocalState->SquadId) : INDEX_NONE;

	// --- group and order the roster ----------------------------------------
	TMap<int32, TArray<AOneBrahamPlayerState*>> ByGroup;

	for (APlayerState* Player : GameState->PlayerArray)
	{
		AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Player);

		// Inactive states are players who disconnected and are being kept for a
		// reconnect; a pure spectator is a QA camera. Neither is on the board.
		if (State == nullptr || State->IsInactive() || State->IsOnlyASpectator())
		{
			continue;
		}

		ByGroup.FindOrAdd(GroupKeyFor(GameState->MatchMode, State->Team, State->SquadId)).Add(State);
	}

	TArray<int32> Keys;
	ByGroup.GetKeys(Keys);

	// The local player's group first - it is the one the board was opened for -
	// then the others in squad order, with the unplaced last.
	Keys.Sort([LocalGroup](int32 A, int32 B)
	{
		if (A == LocalGroup) { return B != LocalGroup; }
		if (B == LocalGroup) { return false; }
		if (A == INDEX_NONE) { return false; }
		if (B == INDEX_NONE) { return true; }
		return A < B;
	});

	TArray<TPair<int32, TArray<AOneBrahamPlayerState*>>> Groups;
	FString Signature;

	for (const int32 Key : Keys)
	{
		TArray<AOneBrahamPlayerState*>& Members = ByGroup[Key];

		// Within a group: objective work first, because that is what wins a
		// match here, then kills, then fewest deaths, then name for stability.
		Members.Sort([](const AOneBrahamPlayerState& A, const AOneBrahamPlayerState& B)
		{
			if (A.ObjectiveScore != B.ObjectiveScore) { return A.ObjectiveScore > B.ObjectiveScore; }
			if (A.Kills != B.Kills) { return A.Kills > B.Kills; }
			if (A.Deaths != B.Deaths) { return A.Deaths < B.Deaths; }
			return A.GetPlayerName() < B.GetPlayerName();
		});

		Signature += FString::Printf(TEXT("G%d:"), Key);
		for (const AOneBrahamPlayerState* Member : Members)
		{
			Signature += FString::Printf(TEXT("%d,"), Member->GetPlayerId());
		}

		Groups.Emplace(Key, Members);
	}

	if (Signature != RosterSignature)
	{
		RosterSignature = Signature;
		RebuildRows(Groups, GameState);
	}

	for (FOneBrahamScoreboardRowWidgets& Row : Rows)
	{
		UpdateRow(Row, Cast<AOneBrahamPlayerState>(Row.Player.Get()));
	}

	for (FOneBrahamScoreboardGroupWidgets& Group : GroupHeaders)
	{
		UpdateGroupHeader(Group, GameState);
	}
}

void UOneBrahamScoreboardWidget::RefreshHeader(const AOneBrahamGameState* GameState)
{
	using namespace OneBrahamScoreboard;

	int32 Personnel = 0;
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		if (Player != nullptr && !Player->IsInactive() && !Player->IsOnlyASpectator())
		{
			++Personnel;
		}
	}

	if (Text_ServerLine != nullptr)
	{
		Text_ServerLine->SetText(FText::Format(
			NSLOCTEXT("OneBraham", "Scoreboard_ServerLine", "ONE BRAHAM  ·  TODAY  ·  {0}  ·  {1} ON SITE"),
			FText::FromString(GameState->ServerName.ToUpper()), FText::AsNumber(Personnel)));
	}

	if (Text_Mode != nullptr)
	{
		Text_Mode->SetText(ModeLabel(GameState->MatchMode));
	}

	if (Text_Phase != nullptr)
	{
		Text_Phase->SetText(PhaseLabel(GameState->MatchPhase));
	}

	// The stage chip: the building's state in the board's corner, in its colour.
	if (StageChip != nullptr && Text_Stage != nullptr)
	{
		const UOneBrahamLockdownComponent* Lockdown = GameState->GetLockdownComponent();
		const EOneBrahamLockdownStage Stage = Lockdown != nullptr && GameState->MatchMode == EOneBrahamMatchMode::Ascent
			? Lockdown->GetStage() : EOneBrahamLockdownStage::Normal;
		const int32 Band = OneBrahamHUDStyle::StageBand(Stage);
		const FLinearColor Colour = Band == 2 ? OneBrahamHUDStyle::EmergencyRed() : (Band == 1 ? OneBrahamHUDStyle::SafetyYellow() : OneBrahamHUDStyle::LightGrey());
		Text_Stage->SetText(Band == 2 ? NSLOCTEXT("OneBraham", "Scoreboard_StagePurge", "PURGE")
			: (Band == 1 ? NSLOCTEXT("OneBraham", "Scoreboard_StageLockdown", "LOCKDOWN") : NSLOCTEXT("OneBraham", "Scoreboard_StageNormal", "SYSTEM NORMAL")));
		Text_Stage->SetColorAndOpacity(FSlateColor(Band == 1 ? OneBrahamHUDStyle::Navy() : (Band == 2 ? OneBrahamHUDStyle::White() : Colour)));
		StageChip->SetBrush(TagBrush(Colour, Band > 0));
	}

	// A timed mode counts down to its deadline, which is what a player wants to
	// know; ASCENT has no deadline, so it shows the building's own clock.
	const bool bTimed = GameState->MatchTimeLimitSeconds > 0.f;

	if (Text_Clock != nullptr)
	{
		Text_Clock->SetText(bTimed
			? UOneBrahamUIStyle::ClockLabel(FMath::Max(0.f, GameState->MatchTimeLimitSeconds - GameState->PhaseElapsedSeconds))
			: OneBrahamDuskClock::Now(GetWorld()));
	}

	if (Text_ClockCaption != nullptr)
	{
		Text_ClockCaption->SetText(bTimed
			? NSLOCTEXT("OneBraham", "Scoreboard_ClockRemaining", "REMAINING")
			: FText::Format(NSLOCTEXT("OneBraham", "Scoreboard_ClockElapsed", "ON SITE {0}"), UOneBrahamUIStyle::ClockLabel(GameState->PhaseElapsedSeconds)));
	}

	const bool bFireDrill = GameState->MatchMode == EOneBrahamMatchMode::FireDrill;
	if (ScoreRow != nullptr)
	{
		ScoreRow->SetVisibility(bFireDrill ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (bFireDrill && Text_ScoreHome != nullptr && Text_ScoreAway != nullptr)
	{
		Text_ScoreHome->SetText(FText::AsNumber(TeamScore(GameState, 0)));
		Text_ScoreAway->SetText(FText::AsNumber(TeamScore(GameState, 1)));
	}
	if (Text_Score != nullptr)
	{
		Text_Score->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamScoreboardWidget::BuildLayout()
{
	using namespace OneBrahamScoreboard;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ScoreboardRoot"));
	WidgetTree->RootWidget = Root;

	// A light dim over the world: the match is still running behind the board,
	// and a player should be able to see a muzzle flash through it.
	UImage* Dim = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	Dim->SetBrush(UOneBrahamUIStyle::SolidBrush(WithAlpha(OneBrahamHUDStyle::Navy(), 0.35f)));
	if (UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim))
	{
		DimSlot->SetHorizontalAlignment(HAlign_Fill);
		DimSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// --- header: the board's sign ------------------------------------------------------
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	UVerticalBox* TitleStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text_ServerLine = Label(FText::GetEmpty(), Type(EOneBrahamType::Eyebrow, 12.f, 180), InkMuted());
	UTextBlock* Title = Label(NSLOCTEXT("OneBraham", "Scoreboard_Title", "TENANT DIRECTORY"), Type(EOneBrahamType::Title, 40.f, -10), Ink());
	TitleStack->AddChildToVerticalBox(Text_ServerLine)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	TitleStack->AddChildToVerticalBox(Title);

	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleStack);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Bottom);

	// Right: mode · stage chip · clock.
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UHorizontalBox* ModeLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Text_Mode = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 13.f, 120), Ink());
	ModeLine->AddChildToHorizontalBox(Text_Mode)->SetVerticalAlignment(VAlign_Center);
	StageChip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	StageChip->SetPadding(FMargin(8.f, 2.f));
	Text_Stage = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 11.f, 120), Ink());
	StageChip->SetContent(Text_Stage);
	ModeLine->AddChildToHorizontalBox(StageChip)->SetPadding(FMargin(12.f, 0.f, 12.f, 0.f));
	Text_Clock = Label(FText::GetEmpty(), Type(EOneBrahamType::TelemetryStrong, 22.f), Ink());
	ModeLine->AddChildToHorizontalBox(Text_Clock)->SetVerticalAlignment(VAlign_Center);
	Right->AddChildToVerticalBox(ModeLine)->SetHorizontalAlignment(HAlign_Right);
	UHorizontalBox* PhaseLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Text_Phase = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 11.f, 120), InkMuted());
	Text_ClockCaption = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 11.f, 120), InkMuted());
	PhaseLine->AddChildToHorizontalBox(Text_Phase)->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
	PhaseLine->AddChildToHorizontalBox(Text_ClockCaption);
	Right->AddChildToVerticalBox(PhaseLine)->SetHorizontalAlignment(HAlign_Right);
	Header->AddChildToHorizontalBox(Right)->SetVerticalAlignment(VAlign_Bottom);

	Column->AddChildToVerticalBox(Header);

	// FIRE DRILL: the two scores as LED numerals, the way the building shows a
	// count, with the departments named under them.
	UHorizontalBox* Scores = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	auto ScoreBlock = [this](const FText& Team, UTextBlock*& OutNumber, const FLinearColor& Colour, EHorizontalAlignment Align)
	{
		UVerticalBox* Block = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		OutNumber = Label(FText::AsNumber(0), Type(EOneBrahamType::LED, 72.f), Colour);
		Block->AddChildToVerticalBox(OutNumber)->SetHorizontalAlignment(Align);
		Block->AddChildToVerticalBox(Label(Team, Type(EOneBrahamType::Eyebrow, 12.f, 180), InkMuted()))->SetHorizontalAlignment(Align);
		return Block;
	};
	UTextBlock* Home = nullptr;
	UTextBlock* Away = nullptr;
	Scores->AddChildToHorizontalBox(ScoreBlock(NSLOCTEXT("OneBraham", "Scoreboard_TeamInternalShort", "INTERNAL STAFF"), Home, Accent(), HAlign_Left));
	UHorizontalBoxSlot* Gap = Scores->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));
	Gap->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Scores->AddChildToHorizontalBox(ScoreBlock(NSLOCTEXT("OneBraham", "Scoreboard_TeamExternalShort", "EXTERNAL CONTRACTORS"), Away, Ink(), HAlign_Right));
	Text_ScoreHome = Home;
	Text_ScoreAway = Away;
	ScoreRow = Scores;
	Scores->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(Scores)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));

	Text_Score = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Medium, Ink(), true);
	Text_Score->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(Text_Score);

	// --- table ---------------------------------------------------------------
	Column->AddChildToVerticalBox(MakeRule(WithAlpha(Ink(), 0.12f)))->SetPadding(FMargin(0.f, 20.f, 0.f, 8.f));
	Column->AddChildToVerticalBox(MakeColumnHeadings());
	Column->AddChildToVerticalBox(MakeRule(WithAlpha(Ink(), 0.12f)))->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));

	USizeBox* ListFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	// Sixteen rows and four headers fit without scrolling at 1080p; a custom
	// lobby larger than that scrolls rather than pushing the footer off screen.
	ListFrame->SetMaxDesiredHeight(560.f);

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Scroll->SetScrollbarThickness(FVector2D(2.f, 2.f));
	RowList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowList"));
	Scroll->AddChild(RowList);
	ListFrame->AddChild(Scroll);
	Column->AddChildToVerticalBox(ListFrame);

	// --- footer ------------------------------------------------------------
	Column->AddChildToVerticalBox(MakeRule(WithAlpha(Ink(), 0.12f)))->SetPadding(FMargin(0.f, 6.f, 0.f, 10.f));
	Column->AddChildToVerticalBox(Label(
		NSLOCTEXT("OneBraham", "Scoreboard_Footer",
			"Muting applies to you alone, until the end of the match. A lamp beside a name is lit while they transmit."),
		Type(EOneBrahamType::Aside, 16.f), InkMuted()));

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	// Near-opaque: the board is read while the HUD's own cards keep painting
	// underneath it, and an announcement ghosting through the rows is noise.
	Panel->SetBrush(TagBrush(WithAlpha(OneBrahamHUDStyle::BaffleBlack(), 0.96f), true, 4.f));
	Panel->SetPadding(FMargin(36.f, 28.f, 36.f, 22.f));
	Panel->SetContent(Column);

	// The left edge carries the one accent: a Primary-blue bar, as on a floor
	// directory.
	UHorizontalBox* Framed = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UHorizontalBoxSlot* BarSlot = Framed->AddChildToHorizontalBox(MakeSwatch(OneBrahamHUDStyle::Primary(), FVector2D(6.f, 6.f)));
	BarSlot->SetVerticalAlignment(VAlign_Fill);
	Framed->AddChildToHorizontalBox(Panel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	USizeBox* PanelFrame = MakeCell(Framed, PanelWidth);
	Board = PanelFrame;

	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelFrame))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	SetVisibility(ESlateVisibility::Collapsed);
	RowBinderStart = Binders.Num();
}

UWidget* UOneBrahamScoreboardWidget::MakeColumnHeadings()
{
	using namespace OneBrahamScoreboard;

	UHorizontalBox* Headings = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	auto Heading = [this, Headings](const FText& Text, float Width, ETextJustify::Type Justify)
	{
		UTextBlock* Cell = Label(Text, Type(EOneBrahamType::Telemetry, 10.f, 160), InkMuted());
		Cell->SetJustification(Justify);
		Headings->AddChildToHorizontalBox(MakeCell(Cell, Width));
	};

	Heading(FText::GetEmpty(), Lamp, ETextJustify::Left);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColName", "NAME"), Name, ETextJustify::Left);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColArchetype", "ROLE"), Archetype, ETextJustify::Left);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColStatus", "STATUS"), Status, ETextJustify::Left);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColKills", "K"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColDeaths", "D"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColAssists", "A"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColObjective", "OBJ"), Objective, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColPing", "PING"), Ping, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "Scoreboard_ColVoice", "VOICE"), Mute, ETextJustify::Right);

	return Headings;
}

UWidget* UOneBrahamScoreboardWidget::MakeRowCells(FOneBrahamScoreboardRowWidgets& Row)
{
	using namespace OneBrahamScoreboard;

	UHorizontalBox* Cells = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	// Number columns are right-justified inside a fixed-width cell, the way a
	// ledger is set, so the digits line up down the column.
	auto AddCell = [this, Cells](UWidget* Content, float Width)
	{
		// A size box fills its child to the row's height and sets it at the top:
		// centre it instead, so a name, a tag and a number share one middle line.
		USizeBox* Cell = MakeCell(Content, Width, RowHeight);
		if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(Cell->GetContentSlot()))
		{
			Inner->SetVerticalAlignment(VAlign_Center);
		}
		UHorizontalBoxSlot* CellSlot = Cells->AddChildToHorizontalBox(Cell);
		CellSlot->SetVerticalAlignment(VAlign_Center);
	};

	// The lamp: a small round light, lit in the accent while the player transmits.
	Row.MicLamp = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	FSlateBrush LampBrush = TagBrush(FLinearColor::White, true, 4.f);
	LampBrush.ImageSize = FVector2D(8.f, 8.f);
	Row.MicLamp->SetBrush(LampBrush);
	Row.MicLamp->SetVisibility(ESlateVisibility::HitTestInvisible);
	UHorizontalBox* LampAlign = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UHorizontalBoxSlot* LampInner = LampAlign->AddChildToHorizontalBox(Row.MicLamp);
	LampInner->SetVerticalAlignment(VAlign_Center);
	LampInner->SetHorizontalAlignment(HAlign_Left);
	LampInner->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	UHorizontalBoxSlot* LampSlot = Cells->AddChildToHorizontalBox(MakeCell(LampAlign, Lamp, RowHeight));
	LampSlot->SetVerticalAlignment(VAlign_Center);

	Row.Name = Label(FText::GetEmpty(), Type(EOneBrahamType::Label, 15.f, 20), Ink());
	Row.Archetype = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 10.f, 120), InkMuted());
	Row.Status = Label(FText::GetEmpty(), Type(EOneBrahamType::Label, 10.f, 120), Ink());
	auto Number = [this]()
	{
		UTextBlock* Cell = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 15.f), Ink());
		Cell->SetJustification(ETextJustify::Right);
		return Cell;
	};
	Row.Kills = Number();
	Row.Deaths = Number();
	Row.Assists = Number();
	Row.Objective = Number();
	Row.Ping = Number();

	// Long names are clipped at the column rather than wrapping a row to two
	// lines: a directory board has one line per name.
	Row.Name->SetClipping(EWidgetClipping::ClipToBounds);
	Row.Archetype->SetClipping(EWidgetClipping::ClipToBounds);

	AddCell(Row.Name, Name);
	AddCell(Row.Archetype, Archetype);

	// The status, as the in/out board's tags: a word in a frame.
	Row.StatusTag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Row.StatusTag->SetPadding(FMargin(6.f, 1.f));
	Row.StatusTag->SetContent(Row.Status);
	UHorizontalBox* TagAlign = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	TagAlign->AddChildToHorizontalBox(Row.StatusTag)->SetVerticalAlignment(VAlign_Center);
	AddCell(TagAlign, Status);

	AddCell(Row.Kills, Stat);
	AddCell(Row.Deaths, Stat);
	AddCell(Row.Assists, Stat);
	AddCell(Row.Objective, Objective);
	AddCell(Row.Ping, Ping);

	// The mute button. Not focusable: it is clicked with Tab still held, and a
	// button that took keyboard focus would swallow the Tab release.
	const TWeakObjectPtr<APlayerState> Target = Row.Player;
	UTextBlock* MuteText = nullptr;
	Row.MuteButton = MakeButton(NSLOCTEXT("OneBraham", "Scoreboard_Mute", "MUTE"),
		[this, Target]() { ToggleMute(Target); },
		EOneBrahamTextSize::Micro, false, &MuteText, FMargin(10.f, 3.f));
	Row.MuteLabel = MuteText;
	if (Row.MuteLabel != nullptr)
	{
		Row.MuteLabel->SetFont(Type(EOneBrahamType::Telemetry, 10.f, 120));
	}

	USizeBox* MuteCell = MakeCell(nullptr, Mute, RowHeight);
	UHorizontalBox* MuteAlign = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	MuteAlign->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()))
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	MuteAlign->AddChildToHorizontalBox(Row.MuteButton)->SetVerticalAlignment(VAlign_Center);
	MuteCell->AddChild(MuteAlign);
	Cells->AddChildToHorizontalBox(MuteCell)->SetVerticalAlignment(VAlign_Center);

	return Cells;
}

void UOneBrahamScoreboardWidget::RebuildRows(const TArray<TPair<int32, TArray<AOneBrahamPlayerState*>>>& Groups,
	const AOneBrahamGameState* GameState)
{
	using namespace OneBrahamScoreboard;

	RowList->ClearChildren();
	Rows.Reset();
	GroupHeaders.Reset();

	// Release the previous rows' binders. Their buttons are gone with the rows;
	// keeping the binders would only grow the array on every reorder.
	if (Binders.Num() > RowBinderStart)
	{
		Binders.SetNum(RowBinderStart);
	}

	const AOneBrahamPlayerState* LocalState = GetOwningPlayerState<AOneBrahamPlayerState>();
	int32 RowIndex = 0;

	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		const TPair<int32, TArray<AOneBrahamPlayerState*>>& Group = Groups[GroupIndex];
		const bool bLocalGroup = LocalState != nullptr && Group.Value.Contains(LocalState);

		// --- group header: the tenant's sign -------------------------------------
		FOneBrahamScoreboardGroupWidgets& HeaderWidgets = GroupHeaders.AddDefaulted_GetRef();
		HeaderWidgets.GroupKey = Group.Key;

		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HeaderWidgets.Title = Label(FText::GetEmpty(), Type(EOneBrahamType::Label, 14.f, 120), bLocalGroup ? Accent() : Ink());
		HeaderWidgets.Detail = Label(FText::GetEmpty(), Type(EOneBrahamType::Telemetry, 11.f, 120), InkMuted());
		HeaderWidgets.Detail->SetJustification(ETextJustify::Right);

		UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(HeaderWidgets.Title);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Bottom);
		HeaderRow->AddChildToHorizontalBox(HeaderWidgets.Detail)->SetVerticalAlignment(VAlign_Bottom);

		RowList->AddChildToVerticalBox(HeaderRow)->SetPadding(FMargin(0.f, GroupIndex == 0 ? 8.f : 22.f, 0.f, 6.f));
		RowList->AddChildToVerticalBox(MakeRule(bLocalGroup ? Accent() : WithAlpha(Ink(), 0.12f)))->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		// --- members -------------------------------------------------------
		for (AOneBrahamPlayerState* Member : Group.Value)
		{
			FOneBrahamScoreboardRowWidgets& Row = Rows.AddDefaulted_GetRef();
			Row.Player = Member;
			Row.Index = RowIndex++;

			Row.Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Row.Root->SetPadding(FMargin(0.f));

			// The local player's row is the only tinted one. It is the row they
			// look for first, and the tint finds it before the name does.
			const bool bLocal = Member == LocalState;
			Row.Root->SetBrush(UOneBrahamUIStyle::SolidBrush(bLocal
				? WithAlpha(Accent(), 0.14f)
				: FLinearColor(0.f, 0.f, 0.f, 0.f)));

			Row.Root->SetContent(MakeRowCells(Row));
			RowList->AddChildToVerticalBox(Row.Root);

			// Hairline between rows, faint enough to guide the eye along a line
			// without turning the board into a spreadsheet.
			RowList->AddChildToVerticalBox(MakeRule(WithAlpha(Ink(), 0.07f)));
		}
	}

	(void)GameState;
}

void UOneBrahamScoreboardWidget::UpdateGroupHeader(FOneBrahamScoreboardGroupWidgets& Group,
	const AOneBrahamGameState* GameState) const
{
	using namespace OneBrahamScoreboard;

	if (Group.Title == nullptr || Group.Detail == nullptr || GameState == nullptr)
	{
		return;
	}

	if (Group.GroupKey == INDEX_NONE)
	{
		Group.Title->SetText(NSLOCTEXT("OneBraham", "Scoreboard_GroupUnassigned", "VISITORS"));
		Group.Detail->SetText(NSLOCTEXT("OneBraham", "Scoreboard_GroupUnassignedDetail", "AWAITING ALLOCATION"));
		return;
	}

	if (GameState->MatchMode == EOneBrahamMatchMode::FireDrill)
	{
		// The two departments are the drill's zones: RED ZONE and BLUE ZONE on
		// the assembly-point signs, which is how a player finds their side.
		Group.Title->SetText(Group.GroupKey == 0
			? NSLOCTEXT("OneBraham", "Scoreboard_TeamInternal", "BLUE ZONE  ·  INTERNAL STAFF")
			: NSLOCTEXT("OneBraham", "Scoreboard_TeamExternal", "RED ZONE  ·  EXTERNAL CONTRACTORS"));
		Group.Detail->SetText(FText::Format(NSLOCTEXT("OneBraham", "Scoreboard_TeamScore", "SCORE  {0}"),
			FText::AsNumber(TeamScore(GameState, Group.GroupKey))));
		return;
	}

	// ASCENT: the squad as a tenant - "SQUAD A  ·  L07  ·  3 IN". The floor is
	// only shown for the player's own squad: the board is not a radar.
	const FOneBrahamSquadInfo Squad = GameState->GetSquadInfo(Group.GroupKey);
	int32 Inside = 0;
	for (const FOneBrahamSquadMemberInfo& Member : Squad.Members)
	{
		Inside += Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed ? 1 : 0;
	}

	const AOneBrahamPlayerState* LocalState = GetOwningPlayerState<AOneBrahamPlayerState>();
	const bool bOwn = LocalState != nullptr && LocalState->SquadId == Group.GroupKey;
	FString Floor;
	if (bOwn)
	{
		if (const AOneBrahamCharacter* Pawn = Cast<AOneBrahamCharacter>(LocalState->GetPawn()))
		{
			Floor = FString::Printf(TEXT("  ·  L%02d"), Pawn->GetCurrentFloorIndex());
		}
	}

	const FString Letter = FString::Chr(TEXT('A') + FMath::Clamp(Group.GroupKey, 0, 25));
	Group.Title->SetText(FText::FromString(FString::Printf(TEXT("SQUAD %s%s  ·  %d IN"), *Letter, *Floor, Inside)));

	FText Detail = Squad.SquadName.ToUpper();
	if (Squad.bEliminated)
	{
		Detail = NSLOCTEXT("OneBraham", "Scoreboard_SquadEliminated", "ALL SIGNED OUT");
	}
	else if (Squad.bHasExtractionClaim)
	{
		Detail = NSLOCTEXT("OneBraham", "Scoreboard_SquadExtracting", "HOLDING THE EXTRACTION");
	}

	Group.Detail->SetText(Detail);
}

void UOneBrahamScoreboardWidget::UpdateRow(FOneBrahamScoreboardRowWidgets& Row, const AOneBrahamPlayerState* State) const
{
	using namespace OneBrahamScoreboard;

	if (State == nullptr || Row.Name == nullptr)
	{
		return;
	}

	const AOneBrahamPlayerState* LocalState = GetOwningPlayerState<AOneBrahamPlayerState>();
	const bool bLocal = State == LocalState;
	const bool bSignedOut = State->IsEliminated();

	// Signed-out rows fade to 35% rather than changing colour: the strike is
	// the word OUT and the faded line, not a hue.
	if (Row.Root != nullptr)
	{
		Row.Root->SetContentColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, bSignedOut ? 0.35f : 1.f));
	}

	Row.Name->SetText(FText::FromString(State->GetPlayerName()));
	Row.Name->SetColorAndOpacity(FSlateColor(bLocal ? Accent() : Ink()));

	Row.Archetype->SetText(ArchetypeLabel(State->ArchetypeTag));

	// The status tag: IN outlined, DOWN filled red (the one state that changes
	// what a colleague does next), OUT plain, EXTRACTED outlined yellow.
	const bool bDowned = State->LifeState == EOneBrahamLifeState::Downed;
	const bool bExtracted = State->LifeState == EOneBrahamLifeState::Extracted;
	Row.Status->SetText(LifeStateLabel(State->LifeState));
	const FLinearColor TagColour = bDowned ? OneBrahamHUDStyle::EmergencyRed()
		: (bExtracted ? OneBrahamHUDStyle::SafetyYellow() : WithAlpha(Ink(), 0.7f));
	Row.Status->SetColorAndOpacity(FSlateColor(bDowned ? OneBrahamHUDStyle::White() : TagColour));
	if (Row.StatusTag != nullptr)
	{
		Row.StatusTag->SetBrush(TagBrush(TagColour, bDowned));
	}

	Row.Kills->SetText(FText::AsNumber(State->Kills));
	Row.Deaths->SetText(FText::AsNumber(State->Deaths));
	Row.Assists->SetText(FText::AsNumber(State->Assists));
	Row.Objective->SetText(FText::AsNumber(State->ObjectiveScore));

	// Bots have no connection and no ping worth showing. "BOT" is said plainly:
	// a player deciding whether to trust a squadmate deserves to know.
	Row.Ping->SetText(State->IsABot()
		? NSLOCTEXT("OneBraham", "Scoreboard_PingBot", "BOT")
		: FText::AsNumber(FMath::RoundToInt(State->GetPingInMilliseconds())));
	Row.Ping->SetColorAndOpacity(FSlateColor(InkMuted()));

	// --- voice -------------------------------------------------------------
	UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwningPlayer());
	const bool bMuted = !bLocal && Voice != nullptr && Voice->IsPlayerMuted(State);

	bool bTransmitting = State->bIsTransmitting;
	if (bLocal)
	{
		// The replicated flag arrives a round trip late; for your own lamp the
		// capture state on this machine is the truth.
		if (const UOneBrahamProximityVoiceComponent* VoiceComponent = State->GetVoiceComponent())
		{
			bTransmitting = bTransmitting || VoiceComponent->IsLocalTransmitting();
		}
	}

	// A muted player's lamp stays dark: you have asked not to hear them, and a
	// light flickering beside their name is them being heard by other means.
	Row.MicLamp->SetColorAndOpacity(bTransmitting && !bMuted ? Accent() : WithAlpha(Ink(), 0.14f));

	// --- mute button -------------------------------------------------------
	// Nobody mutes themselves from here (that is the self-mute key), and a bot
	// has no voice to mute.
	const bool bCanMute = !bLocal && !State->IsABot() && Voice != nullptr;
	Row.MuteButton->SetVisibility(bCanMute ? ESlateVisibility::Visible : ESlateVisibility::Hidden);

	if (bCanMute && Row.MuteLabel != nullptr)
	{
		Row.MuteLabel->SetText(bMuted
			? NSLOCTEXT("OneBraham", "Scoreboard_Muted", "MUTED")
			: NSLOCTEXT("OneBraham", "Scoreboard_Mute", "MUTE"));

		// Muted is drawn in the lit style, so a row you have silenced is visible
		// at a glance down the column.
		Row.MuteButton->SetStyle(BrandButtonStyle(bMuted));
	}
}

int32 UOneBrahamScoreboardWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// The board drops 8 px into place and fades in over 140 ms; the rows
	// follow 12 ms apart. On the real clock: Tab can be held over a pause.
	if (bOpen && Board != nullptr)
	{
		const double Now = FPlatformTime::Seconds();
		const bool bFlat = UOneBrahamUIStyle::ReduceMotion();
		const float In = bFlat ? 1.f : UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, static_cast<float>(Now - OpenedAt) / 0.14f);
		Board->SetRenderOpacity(In);
		Board->SetRenderTranslation(FVector2D(0.f, -8.f * (1.f - In)));

		for (const FOneBrahamScoreboardRowWidgets& Row : Rows)
		{
			if (Row.Root != nullptr)
			{
				const float RowIn = bFlat ? 1.f : UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon,
					static_cast<float>(Now - OpenedAt - 0.06 - Row.Index * 0.012) / 0.14f);
				Row.Root->SetRenderOpacity(RowIn);
			}
		}
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void UOneBrahamScoreboardWidget::ToggleMute(TWeakObjectPtr<APlayerState> Target)
{
	APlayerState* Player = Target.Get();
	UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwningPlayer());

	if (Player == nullptr || Voice == nullptr)
	{
		return;
	}

	const bool bNowMuted = !Voice->IsPlayerMuted(Player);

	if (!Voice->SetPlayerMuted(Player, bNowMuted))
	{
		UE_LOG(LogOneBrahamUI, Warning, TEXT("[OB.UI] Scoreboard: could not %s '%s'."),
			bNowMuted ? TEXT("mute") : TEXT("unmute"), *Player->GetPlayerName());
	}

	// Redraw now rather than on the next quarter-second: the button is what the
	// player is looking at, and it should answer the click.
	RefreshNow();
}
