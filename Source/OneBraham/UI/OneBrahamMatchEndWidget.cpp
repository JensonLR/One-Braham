// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMatchEndWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/FrontEnd/OneBrahamPaint.h"
#include "UI/FrontEnd/OneBrahamPaintedControl.h"
#include "UI/HUD/OneBrahamBuildingStatus.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/HUD/OneBrahamSectionPainter.h"
#include "UI/HUD/OneBrahamTween.h"
#include "UI/OneBrahamScoreboardWidget.h"

using namespace OneBrahamHUDStyle;

namespace OneBrahamMatchEnd_UI
{
	FText ModeLabel(EOneBrahamMatchMode Mode)
	{
		switch (Mode)
		{
		case EOneBrahamMatchMode::FireDrill:	return NSLOCTEXT("OneBraham", "MatchEnd_ModeFireDrill", "FIRE DRILL");
		case EOneBrahamMatchMode::NightShift:	return NSLOCTEXT("OneBraham", "MatchEnd_ModeNightShift", "NIGHT SHIFT");
		default:								return NSLOCTEXT("OneBraham", "MatchEnd_ModeAscent", "ASCENT");
		}
	}

	int32 TeamScore(const AOneBrahamGameState* GameState, int32 TeamIndex)
	{
		return GameState != nullptr && GameState->TeamScores.IsValidIndex(TeamIndex) ? GameState->TeamScores[TeamIndex] : 0;
	}

	/** The letterbox: 2.39:1 inside whatever the screen is. */
	float BarHeight(const FVector2D& Screen)
	{
		return FMath::Max(0.f, (Screen.Y - Screen.X / 2.39f) * 0.5f);
	}

	/** Words typed on, 80 ms a word. */
	FString TypeWords(const FString& Text, float Elapsed)
	{
		if (Elapsed <= 0.f)
		{
			return FString();
		}
		TArray<FString> Words;
		Text.ParseIntoArray(Words, TEXT(" "), true);
		const int32 Shown = FMath::Clamp(FMath::FloorToInt(Elapsed / 0.08f) + 1, 0, Words.Num());
		Words.SetNum(Shown);
		return FString::Join(Words, TEXT(" "));
	}

	/** Wraps text to a width (whole words), at most MaxLines lines. */
	TArray<FString> Wrap(const FString& Text, const FSlateFontInfo& Font, float Width, int32 MaxLines = 3)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		TArray<FString> Lines;
		FString Current;
		for (const FString& Word : Words)
		{
			const FString Trial = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
			if (!Current.IsEmpty() && Measure(Trial, Font).X > Width)
			{
				Lines.Add(Current);
				Current = Word;
			}
			else
			{
				Current = Trial;
			}
		}
		if (!Current.IsEmpty())
		{
			Lines.Add(Current);
		}
		Lines.SetNum(FMath::Min(Lines.Num(), MaxLines));
		return Lines;
	}
}

UOneBrahamMatchEndWidget::UOneBrahamMatchEndWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The HUD component gives the screen the input (UI only), so the game
	// underneath stops taking it; CONTINUE takes the keyboard focus when it shows.
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

FText UOneBrahamMatchEndWidget::EyebrowFor(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, bool bOwnSquadWon)
{
	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		return NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_Drill", "ASSEMBLY POINT REPORT");
	}

	switch (Ending)
	{
	case EOneBrahamMatchEnding::Extracted:			return NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_Extracted", "EXTRACTION CONFIRMED");
	case EOneBrahamMatchEnding::LastSquadStanding:	return bOwnSquadWon
		? NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_LastSquad", "LAST SQUAD STANDING")
		: NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_Outlasted", "OUTLASTED");
	case EOneBrahamMatchEnding::NoSurvivors:		return NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_NoSurvivors", "NO SURVIVORS");
	case EOneBrahamMatchEnding::TimeOut:			return NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_TimeOut", "THE BUILDING HAS CLOSED");
	case EOneBrahamMatchEnding::StaffWithdrawn:		return NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_Withdrawn", "STAFF WITHDRAWN");
	default:										return NSLOCTEXT("OneBraham", "MatchEnd_Eyebrow_Report", "END OF DAY REPORT");
	}
}

// ==========================================================================
//  Show / refresh
// ==========================================================================
void UOneBrahamMatchEndWidget::ShowMatchEnd()
{
	if (!bShowing)
	{
		ShownAt = FPlatformTime::Seconds();
	}
	bShowing = true;
	SetVisibility(ESlateVisibility::Visible);

	RefreshNow();

	if (!RefreshHandle.IsValid())
	{
		RefreshHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UOneBrahamMatchEndWidget::HandleRefreshTick), RefreshIntervalSeconds);
	}

	// CONTINUE holds the keyboard, so Enter answers it; nothing is pressed for
	// the player, and the server sends everybody on regardless.
	if (Row_Continue != nullptr)
	{
		Row_Continue->SetKeyboardFocus();
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

	if (!bBuiltLayoutInCode)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GameState = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GameState == nullptr)
	{
		return;
	}

	// A dev demonstration fills in what a real match would have recorded, so its
	// lines read as they would at the end of a real day.
	FOneBrahamMatchResult DemoResult;
	if (bDemoEnding && GameState->MatchResult.WinningSquadName.IsEmpty())
	{
		DemoResult = GameState->MatchResult;
		DemoResult.WinningSquadName = GameState->Squads.Num() > 0 && !GameState->Squads[0].SquadName.IsEmpty()
			? GameState->Squads[0].SquadName : FText::FromString(TEXT("Your squad"));
		DemoResult.ExtractedCount = FMath::Max(DemoResult.ExtractedCount, 3);
		DemoResult.MatchDurationSeconds = FMath::Max(DemoResult.MatchDurationSeconds, 612.f);
	}
	const FOneBrahamMatchResult& Result = DemoResult.WinningSquadName.IsEmpty() ? GameState->MatchResult : DemoResult;
	const EOneBrahamMatchMode Mode = GameState->MatchMode;
	const UOneBrahamExtractionComponent* Extraction = GameState->GetExtractionComponent();

	// The server's own word for the ending. Until it has replicated (it travels
	// with the winch state, a packet or two behind the phase), the screen reads
	// "END OF DAY REPORT" and fills in on the next refresh.
	EOneBrahamMatchEnding Ending = Extraction != nullptr ? Extraction->GetMatchEnding() : EOneBrahamMatchEnding::None;
	int32 WinningSquadId = Extraction != nullptr ? Extraction->GetEndingWinningSquadId() : INDEX_NONE;
	if (bDemoEnding && Ending == EOneBrahamMatchEnding::None)
	{
		Ending = EOneBrahamMatchEnding::Extracted;
		WinningSquadId = 0;
	}
	ShownEnding = Ending;

	const int32 Staff = TeamScore(GameState, 0);
	const int32 Contractors = TeamScore(GameState, 1);

	// --- the local squad ---------------------------------------------------------
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
	if (bDemoEnding)
	{
		OwnExtracted = FMath::Max(OwnExtracted, 3);
	}
	const bool bOwnWon = LocalSquad != INDEX_NONE && LocalSquad == WinningSquadId;

	// --- the words ------------------------------------------------------------------
	const FString EndClock = LastFrame.DuskClock.IsEmpty()
		? OneBrahamDuskClock::MatchSecondsToClock(Result.MatchDurationSeconds, OneBrahamDuskClock::PurgeAtSeconds(World)).ToString()
		: LastFrame.DuskClock;

	Report.Eyebrow = EyebrowFor(Ending, Mode, bOwnWon).ToString();
	Report.bLoss = false;
	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		const int32 Ours = Local != nullptr && Local->Team == EOneBrahamTeam::ExternalContractors ? Contractors : Staff;
		const int32 Theirs = Local != nullptr && Local->Team == EOneBrahamTeam::ExternalContractors ? Staff : Contractors;
		Report.Headline = Ending == EOneBrahamMatchEnding::Draw ? FString::Printf(TEXT("Level at %d each."), Ours)
			: (bOwnWon ? FString::Printf(TEXT("Drill passed, %d to %d."), Ours, Theirs) : FString(TEXT("A refresher course has been booked.")));
		Report.bLoss = !bOwnWon && Ending != EOneBrahamMatchEnding::Draw;
	}
	else if (OwnExtracted > 0)
	{
		Report.Headline = FString::Printf(TEXT("Signed out at %s."), *EndClock);
	}
	else if (bOwnWon && Ending == EOneBrahamMatchEnding::LastSquadStanding)
	{
		Report.Headline = TEXT("The building is yours.");
	}
	else if (Ending == EOneBrahamMatchEnding::NoSurvivors)
	{
		Report.Headline = TEXT("All staff accounted for. There are no survivors.");
		Report.bLoss = true;
	}
	else if (Ending == EOneBrahamMatchEnding::None)
	{
		Report.Headline = FString::Printf(TEXT("The day ended at %s."), *EndClock);
	}
	else
	{
		Report.Headline = VerdictLine(Ending, Mode, LocalSquad != INDEX_NONE, bOwnWon, OwnInFight, OwnExtracted).ToString();
		Report.bLoss = true;
	}

	Report.WinnerLine = WinnerLine(Ending, Mode, Result.WinningSquadName, Result.ExtractedCount, Staff, Contractors).ToString();
	Report.Verdict = VerdictLine(Ending, Mode, LocalSquad != INDEX_NONE, bOwnWon, OwnInFight, OwnExtracted).ToString();
	if (Report.Verdict == Report.Headline)
	{
		Report.Verdict.Reset();
	}
	Report.Context = FString::Printf(TEXT("%s  ·  %s  ·  %s ON SITE"), *ModeLabel(Mode).ToString(), *EndClock,
		*UOneBrahamUIStyle::ClockLabel(Result.MatchDurationSeconds).ToString());

	const FText Tannoy = OneBrahamMatchEnd::AnnouncementFor(Ending,
		OneBrahamMatchEnd::HasWinner(Ending) || (Ending == EOneBrahamMatchEnding::TimeOut && Mode == EOneBrahamMatchMode::FireDrill)
			? Result.WinningSquadName.ToString() : FString());
	Report.Tannoy = Tannoy.ToString();

	// --- the player's day, in four numbers -------------------------------------------
	Report.Stats.Reset();
	const FString LocalName = Local != nullptr ? Local->GetPlayerName() : FString();
	const FOneBrahamPlayerSummary* Mine = Result.PlayerSummaries.FindByPredicate(
		[&LocalName](const FOneBrahamPlayerSummary& Summary) { return Summary.PlayerName == LocalName; });
	FOneBrahamPlayerSummary Demo;
	if (Mine == nullptr && bDemoEnding)
	{
		Demo.FloorsClimbed = 15; Demo.DamageDealt = 1284.f; Demo.Revives = 2; Demo.Kills = 6;
		Mine = &Demo;
	}
	if (Mine != nullptr)
	{
		if (Mode == EOneBrahamMatchMode::FireDrill)
		{
			Report.Stats.Add({ TEXT("SCORE"), static_cast<float>(Mine->Score) });
		}
		else
		{
			Report.Stats.Add({ TEXT("FLOORS CLIMBED"), static_cast<float>(Mine->FloorsClimbed) });
		}
		Report.Stats.Add({ TEXT("DAMAGE"), Mine->DamageDealt });
		Report.Stats.Add({ TEXT("REVIVES"), static_cast<float>(Mine->Revives) });
		Report.Stats.Add({ TEXT("SIGNED OUT"), static_cast<float>(Mine->Kills) });
	}

	// --- every squad, the local one first --------------------------------------------
	Report.Squads.Reset();
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
	for (const FOneBrahamSquadInfo* Squad : Order)
	{
		int32 InFight = 0;
		int32 Extracted = 0;
		for (const FOneBrahamSquadMemberInfo& Member : Squad->Members)
		{
			InFight += (Member.LifeState == EOneBrahamLifeState::Alive || Member.LifeState == EOneBrahamLifeState::Downed) ? 1 : 0;
			Extracted += Member.LifeState == EOneBrahamLifeState::Extracted ? 1 : 0;
		}
		// The demonstration's winners left by the winch, as its headline says.
		if (bDemoEnding && Squad->SquadId == WinningSquadId && Ending == EOneBrahamMatchEnding::Extracted)
		{
			Extracted = FMath::Max(Extracted, 3);
		}

		FSquadLine& Line = Report.Squads.AddDefaulted_GetRef();
		Line.Name = Squad->SquadName.ToString().ToUpper();
		Line.bOwn = Squad->SquadId == LocalSquad;
		Line.bWinner = Squad->SquadId == WinningSquadId;
		Line.Fate = SquadFateLabel(Ending, Mode, Line.bWinner, InFight, Extracted).ToString();
		Line.bOut = InFight == 0 && Extracted == 0;

		for (const FOneBrahamPlayerSummary& Summary : Result.PlayerSummaries)
		{
			if (Summary.Team != Squad->Team)
			{
				continue;
			}
			const FOneBrahamSquadMemberInfo* Info = Squad->Members.FindByPredicate(
				[&Summary](const FOneBrahamSquadMemberInfo& Member) { return Member.PlayerName == Summary.PlayerName; });
			FMemberLine& Member = Line.Members.AddDefaulted_GetRef();
			Member.Name = Summary.PlayerName;
			Member.Kills = Summary.Kills;
			Member.Downs = Summary.Downs;
			Member.Deaths = Summary.Deaths;
			Member.Assists = Summary.Assists;
			Member.State = Summary.bExtracted ? EOneBrahamLifeState::Extracted : (Info != nullptr ? Info->LifeState : EOneBrahamLifeState::Dead);
			Member.bYou = !LocalName.IsEmpty() && Summary.PlayerName == LocalName;
		}
	}

	// --- the way out ----------------------------------------------------------------------
	const double LeaveAt = Extraction != nullptr ? Extraction->GetLeaveServerTime() : 0.0;
	Report.Leave = LeaveAt > 0.0
		? LeaveLine(static_cast<float>(LeaveAt - GameState->GetServerWorldTimeSeconds()), Extraction->GetPostMatchAction()).ToString()
		: FString();
}

// ==========================================================================
//  The chart
// ==========================================================================
void UOneBrahamMatchEndWidget::BuildChart(FOneBrahamSectionParams& Params) const
{
	Params.Mode = EOneBrahamSectionMode::Chart;
	Params.FloorCount = FMath::Max(16, LastFrame.Building.FloorCount() > 0 ? LastFrame.Building.FloorCount() : 18);
	Params.PurgeCeiling = Replay.PurgeCeiling;

	const float Duration = FMath::Max(1.f, Replay.DurationSeconds);
	Params.PurgeAtTime01 = Replay.PurgeAtSeconds >= 0.f ? Replay.PurgeAtSeconds / Duration : -1.f;

	for (const FOneBrahamReplaySample& Sample : Replay.Samples)
	{
		Params.SealCeilingOverTime.Add(FVector2D(Sample.MatchSeconds / Duration, Sample.SealCeiling));
	}

	// One route a squad: the highest floor any of its members had reached at
	// each sample. A squad's day is its lead climber's.
	TMap<int32, int32> SquadIndex;
	for (const FOneBrahamReplayPerson& Person : Replay.People)
	{
		if (Person.SquadId == INDEX_NONE || SquadIndex.Contains(Person.SquadId))
		{
			continue;
		}
		SquadIndex.Add(Person.SquadId, Params.Routes.Num());
		FOneBrahamSectionRoute& Route = Params.Routes.AddDefaulted_GetRef();
		Route.Label = FString::Chr(TEXT('A') + FMath::Clamp(Person.SquadId, 0, 25));
		Route.bYours = Person.bLocalSquad;
	}

	for (const FOneBrahamReplaySample& Sample : Replay.Samples)
	{
		TMap<int32, int32> Highest;
		for (const TPair<int32, int32>& Entry : Sample.Floors)
		{
			if (const FOneBrahamReplayPerson* Person = Replay.FindPerson(Entry.Key))
			{
				int32& Top = Highest.FindOrAdd(Person->SquadId, INDEX_NONE);
				Top = FMath::Max(Top, Entry.Value);
			}
		}
		for (const TPair<int32, int32>& Squad : Highest)
		{
			if (const int32* Index = SquadIndex.Find(Squad.Key))
			{
				Params.Routes[*Index].Samples.Add(FVector2D(Sample.MatchSeconds / Duration, Squad.Value));
			}
		}
	}

	for (const FOneBrahamReplayKill& Kill : Replay.Kills)
	{
		if (const FOneBrahamReplayPerson* Person = Replay.FindPerson(Kill.KillerId))
		{
			if (const int32* Index = SquadIndex.Find(Person->SquadId))
			{
				Params.Routes[*Index].Kills.Add(FVector2D(Kill.MatchSeconds / Duration, Kill.Floor));
			}
		}
	}

	// Yours drawn last, on top.
	Params.Routes.StableSort([](const FOneBrahamSectionRoute& A, const FOneBrahamSectionRoute& B) { return !A.bYours && B.bYours; });
}

// ==========================================================================
//  The report
// ==========================================================================
void UOneBrahamMatchEndWidget::PaintReport(FOneBrahamPainter& P, double Now) const
{
	using namespace OneBrahamMatchEnd_UI;

	const FVector2D Screen = P.Size();
	const float T = static_cast<float>(Now - ShownAt);
	const bool bFlat = UOneBrahamUIStyle::ReduceMotion();

	// --- the letterbox: 2.39:1, eased in over 900 ms ---------------------------------
	const float BarIn = bFlat ? 1.f : UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, T / 0.9f);
	const float Bar = BarHeight(Screen) * BarIn;
	const float Top = Bar;
	const float Bottom = Screen.Y - Bar;

	// The picture behind dims a little, and darker on the left where the chart is.
	P.Rect(FVector2D(0.f, Top), FVector2D(Screen.X, Bottom - Top), WithAlpha(Navy(), 0.35f * BarIn));
	P.Gradient(FVector2D(0.f, Top), FVector2D(Screen.X * 0.55f, Bottom - Top), WithAlpha(OLEDBlack(), 0.7f * BarIn), WithAlpha(OLEDBlack(), 0.f));
	// The report's side is held dark from its first column on: the words sit
	// on black, the room shows through it only faintly.
	P.Gradient(FVector2D(Screen.X * 0.34f, Top), FVector2D(Screen.X * 0.12f, Bottom - Top), WithAlpha(OLEDBlack(), 0.f), WithAlpha(OLEDBlack(), 0.74f * BarIn));
	P.Rect(FVector2D(Screen.X * 0.46f, Top), FVector2D(Screen.X * 0.54f, Bottom - Top), WithAlpha(OLEDBlack(), 0.74f * BarIn));
	P.Rect(FVector2D::ZeroVector, FVector2D(Screen.X, Bar), OLEDBlack());
	P.Rect(FVector2D(0.f, Bottom), FVector2D(Screen.X, Bar), OLEDBlack());

	// --- the bars' own words ---------------------------------------------------------------
	const FSlateFontInfo BarFont = Font(EOneBrahamHUDType::TelemetryLight, 11.f, 200);
	const float BarText = FMath::Clamp((T - 0.6f) / 0.4f, 0.f, 1.f);
	{
		FOneBrahamPainter B = P.Faded(BarText);
		const float TopY = Bar * 0.5f - 8.f;
		B.Text(FVector2D(Screen.X * 0.03f, TopY), TEXT("ONE BRAHAM / END OF DAY REPORT"), BarFont, WithAlpha(LightGrey(), 0.55f));
		if (!Report.Tannoy.IsEmpty())
		{
			// The building has the last word, as it has had every other.
			const FSlateFontInfo Quote = Font(EOneBrahamHUDType::Aside, 18.f);
			B.Text(FVector2D(Screen.X * 0.97f, TopY - 3.f), FString::Printf(TEXT("“%s”"), *Report.Tannoy), Quote,
				WithAlpha(LightGrey(), 0.7f), EOneBrahamTextAnchor::Right);
		}
		const float BottomY = Bottom + Bar * 0.5f - 8.f;
		B.Text(FVector2D(Screen.X * 0.03f, BottomY), Report.Context, BarFont, WithAlpha(LightGrey(), 0.5f));
		B.Text(FVector2D(Screen.X * 0.97f, BottomY), Report.Leave, Font(EOneBrahamHUDType::Telemetry, 11.f, 200), WithAlpha(LightGrey(), 0.8f),
			EOneBrahamTextAnchor::Right);
	}

	// --- left: the day, replayed in section ----------------------------------------------
	const float ChartX = Screen.X * 0.04f;
	const float ChartW = Screen.X * 0.36f;
	const float ChartTop = Top + 48.f;
	const float ChartH = Bottom - Top - 96.f;
	{
		FOneBrahamPainter L = P.Faded(FMath::Clamp((T - 0.3f) / 0.4f, 0.f, 1.f));
		L.Text(FVector2D(ChartX, ChartTop - 26.f), TEXT("THE DAY, FLOOR BY FLOOR"), Font(EOneBrahamHUDType::Eyebrow, 12.f, 180), WithAlpha(LightGrey(), 0.7f));

		FOneBrahamSectionParams Params;
		BuildChart(Params);
		Params.Time = Now;
		Params.Reveal = bFlat ? 1.f : FMath::Clamp((T - 0.4f) / 4.f, 0.f, 1.f);
		Params.LineColour = WithAlpha(LightGrey(), 0.5f);
		Params.Opacity = L.Opacity;

		if (Replay.Samples.Num() < 2)
		{
			// Nothing recorded on this machine (a late joiner, a test): the
			// building, without the day.
			Params = FOneBrahamSectionParams();
			Params.Mode = EOneBrahamSectionMode::Full;
			Params.Reveal = FMath::Clamp((T - 0.4f) / 0.8f, 0.f, 1.f);
			Params.LineColour = WithAlpha(LightGrey(), 0.5f);
			Params.Opacity = L.Opacity;
			Params.Time = Now;
		}

		const FGeometry ChartGeometry = L.Geometry.MakeChild(FVector2D(ChartW, ChartH), FSlateLayoutTransform(FVector2D(ChartX, ChartTop)));
		*L.Layer = FOneBrahamSectionPainter::Paint(*L.Out, *L.Layer + 1, ChartGeometry, Params);

		// The key, under the chart: every mark has a shape as well as a colour.
		const float KeyY = ChartTop + ChartH + 12.f;
		const FSlateFontInfo KeyFont = Font(EOneBrahamHUDType::Hint, 11.f, 80);
		float KX = ChartX + 40.f;
		L.Rect(FVector2D(KX, KeyY + 6.f), FVector2D(18.f, 2.5f), SignalBlue());
		KX += 24.f + L.Text(FVector2D(KX + 24.f, KeyY), TEXT("YOUR SQUAD"), KeyFont, WithAlpha(LightGrey(), 0.7f)).X + 18.f;
		L.Rect(FVector2D(KX, KeyY + 6.f), FVector2D(18.f, 2.f), WithAlpha(LightGrey(), 0.5f));
		KX += 24.f + L.Text(FVector2D(KX + 24.f, KeyY), TEXT("THE OTHERS"), KeyFont, WithAlpha(LightGrey(), 0.7f)).X + 18.f;
		L.Rect(FVector2D(KX, KeyY + 1.f), FVector2D(16.f, 11.f), WithAlpha(EmergencyRed(), 0.3f));
		L.Hatch(FVector2D(KX, KeyY + 1.f), FVector2D(16.f, 11.f), EmergencyRed(), 4.f);
		KX += 22.f + L.Text(FVector2D(KX + 22.f, KeyY), TEXT("SEALED"), KeyFont, WithAlpha(LightGrey(), 0.7f)).X + 18.f;
		L.Outline(FVector2D(KX, KeyY + 2.f), FVector2D(9.f, 9.f), SignalBlue(), 1.f);
		L.Cross(FVector2D(KX + 4.5f, KeyY + 6.5f), 5.f, SignalBlue(), 1.f);
		L.Text(FVector2D(KX + 16.f, KeyY), TEXT("SIGNED OUT BY YOUR SQUAD"), KeyFont, WithAlpha(LightGrey(), 0.7f));
	}

	// --- right: the report --------------------------------------------------------------------
	const float RX = Screen.X * 0.47f;
	const float RW = Screen.X * 0.49f;
	float Y = Top + 52.f;

	FOneBrahamPainter R = P.Faded(FMath::Clamp((T - 0.5f) / 0.3f, 0.f, 1.f));
	const FLinearColor EyebrowInk = Report.bLoss ? EmergencyRed() : (Report.Eyebrow.Contains(TEXT("EXTRACTION")) ? SafetyYellow() : SignalBlue());
	R.Text(FVector2D(RX, Y), Report.Eyebrow, Font(EOneBrahamHUDType::Eyebrow, 13.f, 200), EyebrowInk);
	Y += 26.f;

	// The headline, a word every 80 ms.
	const FString Typed = bFlat ? Report.Headline : TypeWords(Report.Headline, T - 0.7f);
	if (Report.bLoss)
	{
		// A loss is the building speaking: the serif, on a red field.
		const FSlateFontInfo Serif = Font(EOneBrahamHUDType::Aside, 40.f);
		const TArray<FString> Lines = Wrap(Report.Headline, Serif, RW - 48.f, 3);
		const float LineH = Measure(TEXT("Ag"), Serif).Y;
		const float FieldH = Lines.Num() * LineH + 30.f;
		const float Field = FMath::Clamp((T - 0.6f) / 0.28f, 0.f, 1.f);
		R.Gradient(FVector2D(RX - 18.f, Y), FVector2D((RW + 18.f) * UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, Field), FieldH),
			EmergencyRed(), PurgeFieldDark(), true, 4.f);
		int32 Budget = Typed.Len();
		float LY = Y + 14.f;
		for (const FString& Line : Lines)
		{
			const FString Part = Line.Left(FMath::Max(0, Budget));
			R.Text(FVector2D(RX, LY), Part, Serif, White());
			Budget -= Line.Len() + 1;
			LY += LineH;
		}
		Y += FieldH + 18.f;
	}
	else
	{
		const FSlateFontInfo Title = Font(EOneBrahamHUDType::Title, Report.Headline.Len() > 24 ? 48.f : 64.f, -20);
		const TArray<FString> Lines = Wrap(Report.Headline, Title, RW, 2);
		const float LineH = Measure(TEXT("Ag"), Title).Y * 0.92f;
		int32 Budget = Typed.Len();
		for (const FString& Line : Lines)
		{
			R.Text(FVector2D(RX - 3.f, Y), Line.Left(FMath::Max(0, Budget)), Title, White());
			Budget -= Line.Len() + 1;
			Y += LineH;
		}
		// The ribbon rule under it, drawn in 400 ms once the words are down.
		const float Rule = UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, (T - 1.2f) / 0.4f);
		R.Gradient(FVector2D(RX, Y + 6.f), FVector2D(RW * 0.44f * Rule, 2.f), RibbonPurple(), RibbonBlue());
		Y += 26.f;
	}

	FOneBrahamPainter Body = R.Faded(FMath::Clamp((T - 1.1f) / 0.3f, 0.f, 1.f));
	for (const FString& Line : Wrap(Report.WinnerLine, Font(EOneBrahamHUDType::Body, 18.f), RW, 2))
	{
		Body.Text(FVector2D(RX, Y), Line, Font(EOneBrahamHUDType::Body, 18.f), WithAlpha(LightGrey(), 0.9f));
		Y += 24.f;
	}
	if (!Report.Verdict.IsEmpty())
	{
		Body.Text(FVector2D(RX, Y + 2.f), Report.Verdict, Font(EOneBrahamHUDType::Aside, 20.f), WithAlpha(LightGrey(), 0.8f));
		Y += 30.f;
	}
	Y += 18.f;

	// --- the day in numbers, counting up over 900 ms ------------------------------------------
	if (Report.Stats.Num() > 0)
	{
		const float Count = bFlat ? 1.f : FMath::Clamp((T - 1.4f) / 0.9f, 0.f, 1.f);
		const float Col = RW / Report.Stats.Num();
		Body.Rect(FVector2D(RX, Y), FVector2D(RW, 1.f), WithAlpha(LightGrey(), 0.14f));
		for (int32 Index = 0; Index < Report.Stats.Num(); ++Index)
		{
			const float X = RX + Index * Col;
			const int32 Value = FMath::RoundToInt(Report.Stats[Index].Value * Count);
			Body.Text(FVector2D(X, Y + 12.f), FString::FromInt(Value), Font(EOneBrahamHUDType::TelemetryStrong, 40.f, -20), White());
			Body.Text(FVector2D(X, Y + 66.f), Report.Stats[Index].Key, Font(EOneBrahamHUDType::Eyebrow, 11.f, 180), WithAlpha(LightGrey(), 0.6f));
		}
		Y += 96.f;
		Body.Rect(FVector2D(RX, Y), FVector2D(RW, 1.f), WithAlpha(LightGrey(), 0.14f));
		Y += 18.f;
	}

	// --- every squad's fate ----------------------------------------------------------------------
	const FSlateFontInfo SquadFont = Font(EOneBrahamHUDType::Label, 14.f, 80);
	const FSlateFontInfo FateFont = Font(EOneBrahamHUDType::Telemetry, 11.f, 120);
	const FSlateFontInfo MemberFont = Font(EOneBrahamHUDType::Hint, 13.f);
	for (int32 Index = 0; Index < Report.Squads.Num() && Y < Bottom - 170.f; ++Index)
	{
		const FSquadLine& Squad = Report.Squads[Index];
		FOneBrahamPainter S = R.Faded(FMath::Clamp((T - 1.8f - Index * 0.08f) / 0.3f, 0.f, 1.f) * (Squad.bOut ? 0.5f : 1.f));
		const FLinearColor Ink = Squad.bOwn ? SignalBlue() : White();
		S.Text(FVector2D(RX, Y), Squad.Name, SquadFont, Ink);
		S.Text(FVector2D(RX + RW, Y + 2.f), Squad.Fate, FateFont, Squad.bWinner ? SafetyYellow() : WithAlpha(LightGrey(), 0.7f), EOneBrahamTextAnchor::Right);

		TArray<FString> Names;
		for (const FMemberLine& Member : Squad.Members)
		{
			Names.Add(FString::Printf(TEXT("%s%s %d/%d"), *Member.Name, Member.bYou ? TEXT(" (you)") : TEXT(""), Member.Kills, Member.Deaths));
		}
		S.Text(FVector2D(RX, Y + 20.f), FString::Join(Names, TEXT("   ·   ")), MemberFont, WithAlpha(LightGrey(), 0.6f));
		Y += 46.f;
	}
}

// ==========================================================================
//  Layout
// ==========================================================================
void UOneBrahamMatchEndWidget::BuildLayout()
{
	using namespace OneBrahamPaint;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MatchEndRoot"));
	WidgetTree->RootWidget = Root;

	// The report: one painted layer the size of the screen.
	UOneBrahamPaintedControl* Report_ = CreateWidget<UOneBrahamPaintedControl>(this, UOneBrahamPaintedControl::StaticClass());
	Report_->Setup(FVector2D::ZeroVector, false);
	TWeakObjectPtr<UOneBrahamMatchEndWidget> WeakThis(this);
	Report_->PaintFn = [WeakThis](const FPainter& C, const FOneBrahamControlState& State)
	{
		if (const UOneBrahamMatchEndWidget* Widget = WeakThis.Get())
		{
			int32 Layer = C.Layer;
			FOneBrahamPainter P(C.Out, C.Geometry, Layer, C.Opacity);
			Widget->PaintReport(P, State.Now);
		}
	};
	if (UOverlaySlot* ReportSlot = Root->AddChildToOverlay(Report_))
	{
		ReportSlot->SetHorizontalAlignment(HAlign_Fill);
		ReportSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// The two ways on, as the front end's directory rows.
	auto MakeRow = [this, WeakThis](const FText& Verb, TFunction<FString()> Where, TFunction<void()> OnChosen) -> UOneBrahamPaintedControl*
	{
		UOneBrahamPaintedControl* Row = CreateWidget<UOneBrahamPaintedControl>(this, UOneBrahamPaintedControl::StaticClass());
		Row->Setup(FVector2D(0.f, 56.f), true);
		const FString VerbText = Verb.ToString();
		Row->PaintFn = [VerbText, Where, WeakThis](const FPainter& C, const FOneBrahamControlState& State)
		{
			const UOneBrahamMatchEndWidget* Widget = WeakThis.Get();
			const float Appear = Widget != nullptr ? FMath::Clamp(static_cast<float>(State.Now - Widget->ShownAt - 2.2) / 0.3f, 0.f, 1.f) : 1.f;
			const FPainter Shown(C.Out, C.Geometry, C.Layer, C.Opacity * Appear);
			const FVector2D Size = Shown.Size();
			const float Lit = State.bLit
				? UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.14f)
				: 1.f - UOneBrahamUIStyle::Ease(EOneBrahamEase::Ribbon, State.SinceLit / 0.12f);
			const float Baseline = 37.f;
			if (Lit > 0.f)
			{
				Box(Shown, FVector2D(-18.f, 0.f), FVector2D(Size.X + 18.f, Size.Y), Tok(EOneBrahamColour::White, 0.06f * Lit));
				const float BarH = Size.Y * Lit;
				Box(Shown, FVector2D(-18.f, (Size.Y - BarH) * 0.5f), FVector2D(6.f, BarH), Tok(EOneBrahamColour::SignalBlue));
			}
			const FSlateFontInfo VerbFont = Type(EOneBrahamType::Verb, 28.f, -10);
			const FSlateFontInfo WhereFont = Type(EOneBrahamType::Caption, 15.f, 20);
			const float VerbX = 12.f * Lit;
			const FVector2D VerbSize = TextOnBaseline(Shown, VerbX, Baseline, VerbText, VerbFont, Tok(EOneBrahamColour::White, FMath::Lerp(0.8f, 1.f, Lit)));
			const FString WhereText = Where();
			const FVector2D WhereSize = OneBrahamPaint::Measure(WhereText, WhereFont);
			TextOnBaseline(Shown, Size.X, Baseline, WhereText, WhereFont, Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.55f, 0.85f, Lit)), 1.f);
			const float March = UOneBrahamUIStyle::ReduceMotion() ? 0.f : static_cast<float>(FMath::Fmod(State.Now, 2.0) * 8.0) * Lit;
			DottedH(Shown, VerbX + VerbSize.X + 14.f, Size.X - WhereSize.X - 14.f, Baseline - 1.f,
				Tok(EOneBrahamColour::LightGrey, FMath::Lerp(0.18f, 0.35f, Lit)), 4.f, 1.f, March);
			Box(Shown, FVector2D(0.f, Size.Y - 1.f), FVector2D(Size.X, 1.f), Tok(EOneBrahamColour::LightGrey, 0.08f));
		};
		Row->OnActivate = [WeakThis, OnChosen]()
		{
			if (WeakThis.IsValid())
			{
				OnChosen();
			}
		};
		return Row;
	};

	// CONTINUE takes the player back to reception now, rather than when the
	// server goes on. A host with people still on the server waits for them:
	// leaving would close the building on everybody else's report.
	auto IsHostingOthers = [WeakThis]()
	{
		const UOneBrahamMatchEndWidget* Widget = WeakThis.Get();
		const UWorld* World = Widget != nullptr ? Widget->GetWorld() : nullptr;
		return World != nullptr && World->GetNetMode() == NM_ListenServer && World->GetNumPlayerControllers() > 1;
	};
	Row_Continue = MakeRow(NSLOCTEXT("OneBraham", "MatchEnd_Continue", "CONTINUE"),
		[IsHostingOthers]() { return IsHostingOthers() ? FString(TEXT("Hosting: the building waits for everyone")) : FString(TEXT("Back to reception")); },
		[this, IsHostingOthers]()
		{
			if (IsHostingOthers())
			{
				return;
			}
			if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
			{
				GameInstance->TravelToMainMenu();
			}
		});
	Row_SignOut = MakeRow(NSLOCTEXT("OneBraham", "MatchEnd_SignOut", "SIGN OUT"), []() { return FString(TEXT("Quit to desktop")); },
		[this]() { UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false); });

	const TWeakObjectPtr<UOneBrahamPaintedControl> First = Row_Continue.Get();
	const TWeakObjectPtr<UOneBrahamPaintedControl> Second = Row_SignOut.Get();
	Row_Continue->OnNavigate = [Second](EUINavigation Direction) -> UWidget*
	{
		return Direction == EUINavigation::Up || Direction == EUINavigation::Down ? Second.Get() : nullptr;
	};
	Row_SignOut->OnNavigate = [First](EUINavigation Direction) -> UWidget*
	{
		return Direction == EUINavigation::Up || Direction == EUINavigation::Down ? First.Get() : nullptr;
	};

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Rows->AddChildToVerticalBox(Row_Continue);
	Rows->AddChildToVerticalBox(Row_SignOut);
	if (UCanvasPanelSlot* RowsSlot = Canvas->AddChildToCanvas(Rows))
	{
		// Bottom of the right column, inside the letterbox.
		RowsSlot->SetAnchors(FAnchors(0.47f, 0.86f, 0.96f, 0.86f));
		RowsSlot->SetAlignment(FVector2D(0.f, 1.f));
		RowsSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 112.f));
	}
	if (UOverlaySlot* CanvasSlot = Root->AddChildToOverlay(Canvas))
	{
		CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
		CanvasSlot->SetVerticalAlignment(VAlign_Fill);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}
