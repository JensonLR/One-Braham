// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMatchEndWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "UI/OneBrahamScoreboardWidget.h"

namespace OneBrahamMatchEnd_UI
{
	// Column widths for the member rows, shared by the headings and every row;
	// the name fills what is left.
	constexpr float Stat	= 70.f;
	constexpr float Status	= 150.f;

	constexpr float PanelWidth = 1000.f;

	FText ModeLabel(EOneBrahamMatchMode Mode)
	{
		switch (Mode)
		{
		case EOneBrahamMatchMode::FireDrill:	return NSLOCTEXT("OneBraham", "MatchEnd_ModeFireDrill", "FIRE DRILL");
		case EOneBrahamMatchMode::NightShift:	return NSLOCTEXT("OneBraham", "MatchEnd_ModeNightShift", "NIGHT SHIFT");
		default:								return NSLOCTEXT("OneBraham", "MatchEnd_ModeAscent", "ASCENT");
		}
	}

	FLinearColor WithAlpha(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Alpha);
	}

	int32 TeamScore(const AOneBrahamGameState* GameState, int32 TeamIndex)
	{
		return GameState != nullptr && GameState->TeamScores.IsValidIndex(TeamIndex) ? GameState->TeamScores[TeamIndex] : 0;
	}
}

UOneBrahamMatchEndWidget::UOneBrahamMatchEndWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Nothing on it is clicked: the HUD component gives the screen the input
	// (UI only), so the game underneath stops taking it, and nothing needs focus.
	SetIsFocusable(false);
}

void UOneBrahamMatchEndWidget::NativeDestruct()
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
FText UOneBrahamMatchEndWidget::SquadFateLabel(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, bool bWinner,
	int32 InFight, int32 Extracted)
{
	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		if (Ending == EOneBrahamMatchEnding::Draw)
		{
			return NSLOCTEXT("OneBraham", "MatchEnd_Fate_Level", "LEVEL");
		}
		return bWinner
			? NSLOCTEXT("OneBraham", "MatchEnd_Fate_DrillWon", "EVACUATED TO STANDARD")
			: NSLOCTEXT("OneBraham", "MatchEnd_Fate_DrillLost", "BELOW STANDARD");
	}

	if (Extracted > 0)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Fate_Extracted", "EXTRACTED");
	}

	if (InFight <= 0)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Fate_SignedOut", "SIGNED OUT");
	}

	if (bWinner && Ending == EOneBrahamMatchEnding::LastSquadStanding)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Fate_LastStanding", "LEFT STANDING");
	}

	// On their feet, and still in the building when it closed.
	return NSLOCTEXT("OneBraham", "MatchEnd_Fate_StillInside", "STILL INSIDE");
}

FText UOneBrahamMatchEndWidget::VerdictLine(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, bool bInSquad,
	bool bOwnSquadWon, int32 OwnInFight, int32 OwnExtracted)
{
	if (!bInSquad)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Verdict_Visitor", "You watched from the lobby. Nobody will ask you to sign anything.");
	}

	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		if (Ending == EOneBrahamMatchEnding::Draw)
		{
			return NSLOCTEXT("OneBraham", "MatchEnd_Verdict_DrillDraw", "Your department finished level. Nobody gets the certificate.");
		}
		return bOwnSquadWon
			? NSLOCTEXT("OneBraham", "MatchEnd_Verdict_DrillWon", "Your department passed the drill.")
			: NSLOCTEXT("OneBraham", "MatchEnd_Verdict_DrillLost", "Your department failed the drill. There will be a refresher course.");
	}

	if (OwnExtracted > 0)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Verdict_Out", "Your squad got out.");
	}

	if (bOwnSquadWon && Ending == EOneBrahamMatchEnding::LastSquadStanding)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Verdict_LastStanding", "Your squad is the last one in the building. The building is yours. Nobody else's.");
	}

	if (OwnInFight > 0)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Verdict_Inside", "Your squad is still inside. The building has closed around you.");
	}

	return NSLOCTEXT("OneBraham", "MatchEnd_Verdict_Lost", "Your squad did not make it out.");
}

FText UOneBrahamMatchEndWidget::WinnerLine(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, const FText& WinnerName,
	int32 ExtractedCount, int32 StaffScore, int32 ContractorScore)
{
	switch (Ending)
	{
	case EOneBrahamMatchEnding::Extracted:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Winner_Extracted",
			"{0} left by the winch. {1} {1}|plural(one=person,other=people) aboard."), WinnerName, FText::AsNumber(ExtractedCount));

	case EOneBrahamMatchEnding::LastSquadStanding:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Winner_LastSquad",
			"{0} outlasted every other squad. Nobody reached the wire."), WinnerName);

	case EOneBrahamMatchEnding::NoSurvivors:
		return NSLOCTEXT("OneBraham", "MatchEnd_Winner_NoSurvivors", "Every squad was signed out. The building won.");

	case EOneBrahamMatchEnding::StaffWithdrawn:
		return NSLOCTEXT("OneBraham", "MatchEnd_Winner_Withdrawn", "Every human on site was signed out. The contractors carry on without you.");

	case EOneBrahamMatchEnding::TimeOut:
		if (Mode == EOneBrahamMatchMode::FireDrill)
		{
			return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Winner_DrillTime",
				"Time. {0} ahead, {1} to {2}."), WinnerName,
				FText::AsNumber(FMath::Max(StaffScore, ContractorScore)), FText::AsNumber(FMath::Min(StaffScore, ContractorScore)));
		}
		return NSLOCTEXT("OneBraham", "MatchEnd_Winner_TimeOut", "The building closed with everybody still in it. Nobody extracted. Everybody lost.");

	case EOneBrahamMatchEnding::ScoreLimit:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Winner_ScoreLimit",
			"{0} reached the target. {1} to {2}."), WinnerName,
			FText::AsNumber(FMath::Max(StaffScore, ContractorScore)), FText::AsNumber(FMath::Min(StaffScore, ContractorScore)));

	case EOneBrahamMatchEnding::Draw:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Winner_Draw", "Level at {0} each."), FText::AsNumber(StaffScore));

	case EOneBrahamMatchEnding::None:
	default:
		return FText::GetEmpty();
	}
}

FText UOneBrahamMatchEndWidget::LeaveLine(float SecondsLeft, EOneBrahamPostMatchAction Action)
{
	const FText Clock = UOneBrahamUIStyle::ClockLabel(FMath::Max(0.f, SecondsLeft));

	switch (Action)
	{
	case EOneBrahamPostMatchAction::RestartMatch:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Leave_Restart", "NEXT SHIFT STARTS IN {0}"), Clock);
	case EOneBrahamPostMatchAction::ReturnToMenu:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Leave_Menu", "BACK TO RECEPTION IN {0}"), Clock);
	case EOneBrahamPostMatchAction::ReloadMap:
	default:
		return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Leave_Reload", "THE BUILDING REOPENS IN {0}"), Clock);
	}
}

// ==========================================================================
//  Show / refresh
// ==========================================================================
void UOneBrahamMatchEndWidget::ShowMatchEnd()
{
	bShowing = true;
	SetVisibility(ESlateVisibility::Visible);

	RefreshNow();

	if (!RefreshHandle.IsValid())
	{
		RefreshHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UOneBrahamMatchEndWidget::HandleRefreshTick), RefreshIntervalSeconds);
	}
}

bool UOneBrahamMatchEndWidget::HandleRefreshTick(float DeltaTime)
{
	if (!bShowing)
	{
		RefreshHandle.Reset();
		return false;
	}

	RefreshNow();
	return true;
}

void UOneBrahamMatchEndWidget::RefreshNow()
{
	using namespace OneBrahamMatchEnd_UI;

	if (!bBuiltLayoutInCode || SquadList == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GameState = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GameState == nullptr)
	{
		return;
	}

	const FOneBrahamMatchResult& Result = GameState->MatchResult;
	const EOneBrahamMatchMode Mode = GameState->MatchMode;
	const UOneBrahamExtractionComponent* Extraction = GameState->GetExtractionComponent();

	// The server's own word for the ending. Until it has replicated (it travels
	// with the winch state, a packet or two behind the phase), the screen reads
	// "CONCLUDED" and fills in on the next refresh.
	const EOneBrahamMatchEnding Ending = Extraction != nullptr ? Extraction->GetMatchEnding() : EOneBrahamMatchEnding::None;
	const int32 WinningSquadId = Extraction != nullptr ? Extraction->GetEndingWinningSquadId() : INDEX_NONE;
	ShownEnding = Ending;

	// --- context and headline ----------------------------------------------
	const int32 Staff = TeamScore(GameState, 0);
	const int32 Contractors = TeamScore(GameState, 1);

	FText Context = FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Context", "{0}  /  MATCH CONCLUDED  /  {1}"),
		ModeLabel(Mode), UOneBrahamUIStyle::ClockLabel(Result.MatchDurationSeconds));
	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		Context = FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_ContextScore", "{0}  /  INTERNAL STAFF {1} : {2} EXTERNAL CONTRACTORS"),
			Context, FText::AsNumber(Staff), FText::AsNumber(Contractors));
	}
	Text_Context->SetText(Context);

	Text_Headline->SetText(OneBrahamMatchEnd::HeadlineFor(Ending));
	Text_Winner->SetText(WinnerLine(Ending, Mode, Result.WinningSquadName, Result.ExtractedCount, Staff, Contractors));

	// --- the local squad -------------------------------------------------------
	const AOneBrahamPlayerState* Local = GetOwningPlayerState<AOneBrahamPlayerState>();
	const int32 LocalSquad = Local != nullptr ? Local->SquadId : INDEX_NONE;
	int32 OwnInFight = 0;
	int32 OwnExtracted = 0;
	if (GameState->Squads.IsValidIndex(LocalSquad))
	{
		for (const FOneBrahamSquadMemberInfo& Member : GameState->Squads[LocalSquad].Members)
		{
			OwnInFight += (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed) ? 1 : 0;
			OwnExtracted += Member.LifeState == EOneBrahamLifeState::Extracted ? 1 : 0;
		}
	}

	const bool bOwnWon = LocalSquad != INDEX_NONE && LocalSquad == WinningSquadId;
	Text_Verdict->SetText(VerdictLine(Ending, Mode, LocalSquad != INDEX_NONE, bOwnWon, OwnInFight, OwnExtracted));
	Text_Verdict->SetColorAndOpacity(FSlateColor(bOwnWon || OwnExtracted > 0 ? Accent() : Ink()));

	// --- the tannoy ----------------------------------------------------------------
	const FText Tannoy = OneBrahamMatchEnd::AnnouncementFor(Ending,
		OneBrahamMatchEnd::HasWinner(Ending) || (Ending == EOneBrahamMatchEnding::TimeOut && Mode == EOneBrahamMatchMode::FireDrill)
			? Result.WinningSquadName.ToString() : FString());
	Text_Tannoy->SetText(Tannoy.IsEmpty() ? FText::GetEmpty()
		: FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Tannoy", "TANNOY: \"{0}\""), Tannoy));

	// --- the squads ------------------------------------------------------------------
	RebuildSquads(GameState, Ending, WinningSquadId);

	// --- the way out -------------------------------------------------------------------
	const double LeaveAt = Extraction != nullptr ? Extraction->GetLeaveServerTime() : 0.0;
	if (LeaveAt > 0.0)
	{
		const float SecondsLeft = static_cast<float>(LeaveAt - GameState->GetServerWorldTimeSeconds());
		Text_Footer->SetText(LeaveLine(SecondsLeft, Extraction->GetPostMatchAction()));
	}
}

void UOneBrahamMatchEndWidget::RebuildSquads(const AOneBrahamGameState* GameState, EOneBrahamMatchEnding Ending, int32 WinningSquadId)
{
	using namespace OneBrahamMatchEnd_UI;

	const FOneBrahamMatchResult& Result = GameState->MatchResult;
	const AOneBrahamPlayerState* Local = GetOwningPlayerState<AOneBrahamPlayerState>();
	const int32 LocalSquad = Local != nullptr ? Local->SquadId : INDEX_NONE;
	const FString LocalName = Local != nullptr ? Local->GetPlayerName() : FString();

	// The local squad first: it is the one the player wants to read about.
	TArray<const FOneBrahamSquadInfo*> Order;
	for (const FOneBrahamSquadInfo& Squad : GameState->Squads)
	{
		if (Squad.Members.Num() > 0)
		{
			Order.Add(&Squad);
		}
	}
	Order.StableSort([LocalSquad](const FOneBrahamSquadInfo& A, const FOneBrahamSquadInfo& B)
	{
		if (A.SquadId == LocalSquad) { return B.SquadId != LocalSquad; }
		if (B.SquadId == LocalSquad) { return false; }
		return A.SquadId < B.SquadId;
	});

	// The life states change under the screen only until the server froze the
	// match; the signature keeps a steady screen from being rebuilt every half
	// second for nothing.
	FString Signature = FString::Printf(TEXT("%d/%d/%d:"), static_cast<int32>(Ending), WinningSquadId, Result.PlayerSummaries.Num());
	for (const FOneBrahamSquadInfo* Squad : Order)
	{
		for (const FOneBrahamSquadMemberInfo& Member : Squad->Members)
		{
			Signature += FString::Printf(TEXT("%d.%d,"), Member.PlayerId, static_cast<int32>(Member.LifeState));
		}
	}
	if (Signature == SquadSignature)
	{
		return;
	}
	SquadSignature = Signature;

	SquadList->ClearChildren();

	auto AddRow = [this](UHorizontalBox* Row, const FText& Text, float Width, const FLinearColor& Colour, bool bBold, ETextJustify::Type Justify)
	{
		Row->AddChildToHorizontalBox(MakeCell(MakeLabel(Text, EOneBrahamTextSize::Small, Colour, bBold, Justify), Width));
	};

	for (const FOneBrahamSquadInfo* Squad : Order)
	{
		int32 InFight = 0;
		int32 Extracted = 0;
		for (const FOneBrahamSquadMemberInfo& Member : Squad->Members)
		{
			InFight += (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed) ? 1 : 0;
			Extracted += Member.LifeState == EOneBrahamLifeState::Extracted ? 1 : 0;
		}

		const bool bOwn = Squad->SquadId == LocalSquad;
		const bool bWinner = Squad->SquadId == WinningSquadId;

		// --- header: name, fate --------------------------------------------
		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(MakeLabel(
			FText::FromString(Squad->SquadName.ToString().ToUpper()), EOneBrahamTextSize::Medium, bOwn ? Accent() : Ink(), true));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Header->AddChildToHorizontalBox(MakeLabel(SquadFateLabel(Ending, GameState->MatchMode, bWinner, InFight, Extracted),
			EOneBrahamTextSize::Medium, bWinner || Extracted > 0 ? Accent() : InkMuted(), true, ETextJustify::Right));
		SquadList->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 12.f, 0.f, 4.f));

		// --- members, from the result (which has their numbers) ----------------
		const TArray<FOneBrahamPlayerSummary> Members = Result.PlayerSummaries.FilterByPredicate(
			[Squad](const FOneBrahamPlayerSummary& Summary) { return Summary.Team == Squad->Team; });

		for (const FOneBrahamPlayerSummary& Summary : Members)
		{
			const FOneBrahamSquadMemberInfo* Info = Squad->Members.FindByPredicate(
				[&Summary](const FOneBrahamSquadMemberInfo& Member) { return Member.PlayerName == Summary.PlayerName; });
			const EOneBrahamLifeState State = Summary.bExtracted ? EOneBrahamLifeState::Extracted
				: (Info != nullptr ? Info->LifeState : EOneBrahamLifeState::Dead);
			const bool bYou = !LocalName.IsEmpty() && Summary.PlayerName == LocalName;
			const FLinearColor Colour = bYou ? Accent() : (State == EOneBrahamLifeState::Dead || State == EOneBrahamLifeState::Spectating ? InkFaint() : Ink());

			// The name takes what the numbers leave, so the status column ends on
			// the panel's right edge, under the squad's fate.
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			Row->AddChildToHorizontalBox(MakeLabel(FText::FromString(Summary.PlayerName), EOneBrahamTextSize::Small, Colour, bYou))
				->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			AddRow(Row, FText::AsNumber(Summary.Kills), Stat, Colour, false, ETextJustify::Right);
			AddRow(Row, FText::AsNumber(Summary.Downs), Stat, Colour, false, ETextJustify::Right);
			AddRow(Row, FText::AsNumber(Summary.Deaths), Stat, Colour, false, ETextJustify::Right);
			AddRow(Row, FText::AsNumber(Summary.Assists), Stat, Colour, false, ETextJustify::Right);
			AddRow(Row, UOneBrahamScoreboardWidget::LifeStateLabel(State), Status, Colour, State == EOneBrahamLifeState::Extracted, ETextJustify::Right);
			SquadList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 1.f));
		}
	}
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamMatchEndWidget::BuildLayout()
{
	using namespace OneBrahamMatchEnd_UI;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MatchEndRoot"));
	WidgetTree->RootWidget = Root;

	// Heavier than the scoreboard's dim: the match is over, and the building
	// behind the board is a backdrop now, not a threat.
	UImage* Dim = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	Dim->SetBrush(UOneBrahamUIStyle::SolidBrush(WithAlpha(UOneBrahamUIStyle::Navy(), 0.72f)));
	if (UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim))
	{
		DimSlot->SetHorizontalAlignment(HAlign_Fill);
		DimSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	Text_Context = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Micro, InkMuted());
	Column->AddChildToVerticalBox(Text_Context)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	Text_Headline = MakeLabel(OneBrahamMatchEnd::HeadlineFor(EOneBrahamMatchEnding::None), EOneBrahamTextSize::Display, Ink(), true);
	Column->AddChildToVerticalBox(Text_Headline);

	Text_Winner = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Medium, Ink());
	Text_Winner->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(Text_Winner)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	Text_Verdict = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Medium, Accent(), true);
	Text_Verdict->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(Text_Verdict)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));

	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 16.f, 0.f, 10.f));

	Text_Tannoy = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Small, InkMuted());
	Text_Tannoy->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(Text_Tannoy);

	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 10.f, 0.f, 6.f));

	// Column headings, aligned with the member rows by sharing their widths.
	UHorizontalBox* Headings = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	auto Heading = [this, Headings](const FText& Text, float Width, ETextJustify::Type Justify)
	{
		Headings->AddChildToHorizontalBox(MakeCell(MakeLabel(Text, EOneBrahamTextSize::Micro, InkMuted(), true, Justify), Width));
	};
	Headings->AddChildToHorizontalBox(MakeLabel(NSLOCTEXT("OneBraham", "MatchEnd_ColName", "NAME"), EOneBrahamTextSize::Micro, InkMuted(), true))
		->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Heading(NSLOCTEXT("OneBraham", "MatchEnd_ColKills", "K"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "MatchEnd_ColDowns", "DOWNS"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "MatchEnd_ColDeaths", "D"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "MatchEnd_ColAssists", "A"), Stat, ETextJustify::Right);
	Heading(NSLOCTEXT("OneBraham", "MatchEnd_ColStatus", "STATUS"), Status, ETextJustify::Right);
	Column->AddChildToVerticalBox(Headings);

	USizeBox* ListFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ListFrame->SetMaxDesiredHeight(520.f);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Scroll->SetScrollbarThickness(FVector2D(2.f, 2.f));
	SquadList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SquadList"));
	Scroll->AddChild(SquadList);
	ListFrame->AddChild(Scroll);
	Column->AddChildToVerticalBox(ListFrame);

	Column->AddChildToVerticalBox(MakeRule(InkFaint()))->SetPadding(FMargin(0.f, 10.f, 0.f, 8.f));
	Text_Footer = MakeLabel(FText::GetEmpty(), EOneBrahamTextSize::Micro, InkMuted(), true);
	Column->AddChildToVerticalBox(Text_Footer);

	UBorder* Panel = MakePanel(Column, 0.96f, FMargin(36.f, 28.f, 36.f, 22.f));

	// The one accent: a bar down the left edge, as on a floor directory.
	UHorizontalBox* Framed = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UHorizontalBoxSlot* BarSlot = Framed->AddChildToHorizontalBox(MakeSwatch(Accent(), FVector2D(4.f, 4.f)));
	BarSlot->SetVerticalAlignment(VAlign_Fill);
	Framed->AddChildToHorizontalBox(Panel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(MakeCell(Framed, PanelWidth)))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}
