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
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

namespace OneBrahamScoreboard
{
	// Column widths, in the order the cells are added. The headings and every
	// row read from this one table, which is the whole of the column alignment.
	constexpr float Lamp		= 22.f;
	constexpr float Name		= 236.f;
	constexpr float Archetype	= 190.f;
	constexpr float Status		= 126.f;
	constexpr float Stat		= 46.f;
	constexpr float Objective	= 62.f;
	constexpr float Ping		= 58.f;
	constexpr float Mute		= 92.f;

	constexpr float PanelWidth	= 1040.f;
	constexpr float RowHeight	= 30.f;

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
			NSLOCTEXT("OneBraham", "Scoreboard_ServerLine", "DIRECTORY  /  {0}  /  {1} PERSONNEL"),
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

	// A timed mode counts down to its deadline, which is what a player wants to
	// know; ASCENT has no deadline, so it shows how long this phase has run.
	const bool bTimed = GameState->MatchTimeLimitSeconds > 0.f;

	if (Text_Clock != nullptr)
	{
		Text_Clock->SetText(UOneBrahamUIStyle::ClockLabel(bTimed
			? FMath::Max(0.f, GameState->MatchTimeLimitSeconds - GameState->PhaseElapsedSeconds)
			: GameState->PhaseElapsedSeconds));
	}

	if (Text_ClockCaption != nullptr)
	{
		Text_ClockCaption->SetText(bTimed
			? NSLOCTEXT("OneBraham", "Scoreboard_ClockRemaining", "REMAINING")
			: NSLOCTEXT("OneBraham", "Scoreboard_ClockElapsed", "ELAPSED"));
	}

	if (Text_Score != nullptr)
	{
		const bool bFireDrill = GameState->MatchMode == EOneBrahamMatchMode::FireDrill;
		Text_Score->SetVisibility(bFireDrill ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bFireDrill)
		{
			Text_Score->SetText(FText::Format(
				NSLOCTEXT("OneBraham", "Scoreboard_FireDrillScore", "INTERNAL STAFF   {0}   :   {1}   EXTERNAL CONTRACTORS"),
				FText::AsNumber(TeamScore(GameState, 0)), FText::AsNumber(TeamScore(GameState, 1))));
		}
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
	Dim->SetBrush(UOneBrahamUIStyle::SolidBrush(WithAlpha(UOneBrahamUIStyle::Navy(), 0.35f)));
	if (UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim))
	{
		DimSlot->SetHorizontalAlignment(HAlign_Fill);
		DimSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// --- header ------------------------------------------------------------
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	UVerticalBox* TitleStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text_ServerLine = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Micro, InkMuted());
	Text_Mode = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Header, Ink(), true);
	Text_Phase = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	TitleStack->AddChildToVerticalBox(Text_ServerLine)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	TitleStack->AddChildToVerticalBox(Text_Mode);
	TitleStack->AddChildToVerticalBox(Text_Phase)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleStack);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Bottom);

	UVerticalBox* ClockStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text_Clock = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Large, Ink(), true, ETextJustify::Right);
	Text_ClockCaption = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Micro, InkMuted(), false, ETextJustify::Right);
	ClockStack->AddChildToVerticalBox(Text_Clock)->SetHorizontalAlignment(HAlign_Right);
	ClockStack->AddChildToVerticalBox(Text_ClockCaption)->SetHorizontalAlignment(HAlign_Right);
	Header->AddChildToHorizontalBox(ClockStack)->SetVerticalAlignment(VAlign_Bottom);

	Column->AddChildToVerticalBox(Header);

	Text_Score = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Medium, Ink(), true);
	Text_Score->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(Text_Score)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));

	// --- table ---------------------------------------------------------------
	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 18.f, 0.f, 8.f));
	Column->AddChildToVerticalBox(MakeColumnHeadings());
	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));

	USizeBox* ListFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	// Sixteen rows and four headers fit without scrolling at 1080p; a custom
	// lobby larger than that scrolls rather than pushing the footer off screen.
	ListFrame->SetMaxDesiredHeight(640.f);

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Scroll->SetScrollbarThickness(FVector2D(2.f, 2.f));
	RowList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowList"));
	Scroll->AddChild(RowList);
	ListFrame->AddChild(Scroll);
	Column->AddChildToVerticalBox(ListFrame);

	// --- footer ------------------------------------------------------------
	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 6.f, 0.f, 10.f));
	Column->AddChildToVerticalBox(MakeLabel(
		NSLOCTEXT("OneBraham", "Scoreboard_Footer",
			"Muting applies to you alone and lasts until the end of the match. The lamp beside a name is lit while they transmit."),
		EOneBrahamTextSize::Micro, InkFaint()));

	UBorder* Panel = MakePanel(Column, 0.94f, FMargin(32.f, 26.f, 32.f, 22.f));

	// The left edge carries the one accent: a bar, as on a floor directory.
	UHorizontalBox* Framed = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UHorizontalBoxSlot* BarSlot = Framed->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)));
	BarSlot->SetVerticalAlignment(VAlign_Fill);
	Framed->AddChildToHorizontalBox(Panel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	USizeBox* PanelFrame = MakeCell(Framed, PanelWidth);

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
		Headings->AddChildToHorizontalBox(MakeCell(
			MakeLabel(Text, EOneBrahamTextSize::Micro, InkMuted(), true, Justify), Width));
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
		UHorizontalBoxSlot* CellSlot = Cells->AddChildToHorizontalBox(MakeCell(Content, Width, RowHeight));
		CellSlot->SetVerticalAlignment(VAlign_Center);
	};

	// The lamp: a small square, lit in the accent while the player transmits.
	Row.MicLamp = MakeSwatch(InkFaint(), FVector2D(8.f, 8.f));
	USizeBox* LampBox = MakeCell(Row.MicLamp, 8.f, 8.f);
	UHorizontalBoxSlot* LampSlot = Cells->AddChildToHorizontalBox(MakeCell(LampBox, Lamp, RowHeight));
	LampSlot->SetVerticalAlignment(VAlign_Center);
	LampBox->SetVisibility(ESlateVisibility::HitTestInvisible);

	Row.Name = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), true);
	Row.Archetype = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Row.Status = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink());
	Row.Kills = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), false, ETextJustify::Right);
	Row.Deaths = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), false, ETextJustify::Right);
	Row.Assists = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), false, ETextJustify::Right);
	Row.Objective = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, Ink(), true, ETextJustify::Right);
	Row.Ping = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted(), false, ETextJustify::Right);

	// Long names are clipped at the column rather than wrapping a row to two
	// lines: a directory board has one line per name.
	Row.Name->SetClipping(EWidgetClipping::ClipToBounds);
	Row.Archetype->SetClipping(EWidgetClipping::ClipToBounds);

	AddCell(Row.Name, Name);
	AddCell(Row.Archetype, Archetype);
	AddCell(Row.Status, Status);
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

	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		const TPair<int32, TArray<AOneBrahamPlayerState*>>& Group = Groups[GroupIndex];
		const bool bLocalGroup = LocalState != nullptr && Group.Value.Contains(LocalState);

		// --- group header: the squad's sign -----------------------------------
		FOneBrahamScoreboardGroupWidgets& HeaderWidgets = GroupHeaders.AddDefaulted_GetRef();
		HeaderWidgets.GroupKey = Group.Key;

		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HeaderWidgets.Title = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small,
			bLocalGroup ? Accent() : Ink(), true);
		HeaderWidgets.Detail = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Micro, InkMuted(), false, ETextJustify::Right);

		UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(HeaderWidgets.Title);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Bottom);
		HeaderRow->AddChildToHorizontalBox(HeaderWidgets.Detail)->SetVerticalAlignment(VAlign_Bottom);

		RowList->AddChildToVerticalBox(HeaderRow)->SetPadding(FMargin(0.f, GroupIndex == 0 ? 6.f : 18.f, 0.f, 4.f));
		RowList->AddChildToVerticalBox(MakeRule(bLocalGroup ? Accent() : InkFaint()))->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		// --- members -------------------------------------------------------
		for (AOneBrahamPlayerState* Member : Group.Value)
		{
			FOneBrahamScoreboardRowWidgets& Row = Rows.AddDefaulted_GetRef();
			Row.Player = Member;

			Row.Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Row.Root->SetPadding(FMargin(0.f));

			// The local player's row is the only tinted one. It is the row they
			// look for first, and the tint finds it before the name does.
			const bool bLocal = Member == LocalState;
			Row.Root->SetBrush(UOneBrahamUIStyle::SolidBrush(bLocal
				? WithAlpha(Accent(), 0.16f)
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
		Group.Title->SetText(Group.GroupKey == 0
			? NSLOCTEXT("OneBraham", "Scoreboard_TeamInternal", "INTERNAL STAFF")
			: NSLOCTEXT("OneBraham", "Scoreboard_TeamExternal", "EXTERNAL CONTRACTORS"));
		Group.Detail->SetText(FText::Format(NSLOCTEXT("OneBraham", "Scoreboard_TeamScore", "SCORE  {0}"),
			FText::AsNumber(TeamScore(GameState, Group.GroupKey))));
		return;
	}

	const FOneBrahamSquadInfo Squad = GameState->GetSquadInfo(Group.GroupKey);
	Group.Title->SetText(SquadName(Squad.Team, Group.GroupKey));

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

	// Signed-out players fade rather than change colour: monochrome, one accent.
	const FLinearColor Body = bSignedOut ? InkFaint() : Ink();
	const FLinearColor Secondary = bSignedOut ? InkFaint() : InkMuted();

	Row.Name->SetText(FText::FromString(State->GetPlayerName()));
	Row.Name->SetColorAndOpacity(FSlateColor(bLocal ? Accent() : Body));

	Row.Archetype->SetText(ArchetypeLabel(State->ArchetypeTag));
	Row.Archetype->SetColorAndOpacity(FSlateColor(Secondary));

	// Downed is the one state that changes what a colleague should do next, so
	// it is the one set in bold white rather than body grey.
	const bool bDowned = State->LifeState == EOneBrahamLifeState::Downed;
	Row.Status->SetText(LifeStateLabel(State->LifeState));
	Row.Status->SetFont(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small, bDowned));
	Row.Status->SetColorAndOpacity(FSlateColor(bDowned ? FLinearColor::White : Secondary));

	Row.Kills->SetText(FText::AsNumber(State->Kills));
	Row.Deaths->SetText(FText::AsNumber(State->Deaths));
	Row.Assists->SetText(FText::AsNumber(State->Assists));
	Row.Objective->SetText(FText::AsNumber(State->ObjectiveScore));

	for (UTextBlock* Number : { Row.Kills.Get(), Row.Deaths.Get(), Row.Assists.Get(), Row.Objective.Get() })
	{
		Number->SetColorAndOpacity(FSlateColor(Body));
	}

	// Bots have no connection and no ping worth showing. "BOT" is said plainly:
	// a player deciding whether to trust a squadmate deserves to know.
	Row.Ping->SetText(State->IsABot()
		? NSLOCTEXT("OneBraham", "Scoreboard_PingBot", "BOT")
		: FText::AsNumber(FMath::RoundToInt(State->GetPingInMilliseconds())));

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
	Row.MicLamp->SetColorAndOpacity(bTransmitting && !bMuted ? Accent() : WithAlpha(Ink(), 0.12f));

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
