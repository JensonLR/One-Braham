// Copyright (c) One Braham. All Rights Reserved.

#include "Extraction/OneBrahamMatchEnding.h"

namespace OneBrahamMatchEnd
{
	FOneBrahamEndVerdict DecideEliminationEnding(EOneBrahamMatchMode Mode,
		const TArray<FOneBrahamSquadStanding>& Squads, int32 ExtractionSquadId,
		int32 HumansInSquads, int32 HumansInFight, int32 BotsInFight)
	{
		FOneBrahamEndVerdict Verdict;

		// FIRE DRILL and NIGHT SHIFT respawn: nobody is ever out for good.
		if (Mode != EOneBrahamMatchMode::Ascent)
		{
			return Verdict;
		}

		int32 SquadsInMatch = 0;
		int32 SquadsInFight = 0;
		int32 LastInFight = INDEX_NONE;
		for (const FOneBrahamSquadStanding& Squad : Squads)
		{
			if (Squad.Members <= 0)
			{
				continue;
			}

			++SquadsInMatch;
			if (Squad.InFight > 0)
			{
				++SquadsInFight;
				LastInFight = Squad.SquadId;
			}
		}

		// An empty roster is a lobby, not an ending.
		if (SquadsInMatch == 0)
		{
			return Verdict;
		}

		if (SquadsInFight == 0)
		{
			Verdict.bEnds = true;
			Verdict.Ending = EOneBrahamMatchEnding::NoSurvivors;
			return Verdict;
		}

		if (SquadsInFight == 1 && SquadsInMatch >= 2)
		{
			// The last squad on the wire gets to finish the job: "one squad left
			// standing with the pad claimed" extracts, or the winch fails and the
			// match ends here on the next check.
			if (ExtractionSquadId != INDEX_NONE && ExtractionSquadId == LastInFight)
			{
				return Verdict;
			}

			const FOneBrahamSquadStanding* Last = Squads.FindByPredicate([LastInFight](const FOneBrahamSquadStanding& Squad)
			{
				return Squad.SquadId == LastInFight;
			});
			if (Last != nullptr && Last->OnPurgedFloor >= Last->InFight)
			{
				return Verdict;
			}

			Verdict.bEnds = true;
			Verdict.Ending = EOneBrahamMatchEnding::LastSquadStanding;
			Verdict.WinningSquadId = LastInFight;
			return Verdict;
		}

		// The humans are all out and the bots are still at it. An extraction in
		// progress is let run: it is the only thing left worth watching.
		if (BotsInFight > 0 && HumansInSquads > 0 && HumansInFight == 0 && ExtractionSquadId == INDEX_NONE)
		{
			Verdict.bEnds = true;
			Verdict.Ending = EOneBrahamMatchEnding::StaffWithdrawn;
		}

		return Verdict;
	}

	EOneBrahamMatchEnding EndingForTimeOut(EOneBrahamMatchMode Mode, bool bDraw)
	{
		if (Mode == EOneBrahamMatchMode::FireDrill && bDraw)
		{
			return EOneBrahamMatchEnding::Draw;
		}

		return EOneBrahamMatchEnding::TimeOut;
	}

	EOneBrahamPostMatchAction DecidePostMatchAction(bool bDedicatedServer, bool bMainMenuExists)
	{
		// A dedicated server has no menu to go to: it is the menu's server list.
		if (bDedicatedServer)
		{
			return EOneBrahamPostMatchAction::RestartMatch;
		}

		return bMainMenuExists ? EOneBrahamPostMatchAction::ReturnToMenu : EOneBrahamPostMatchAction::ReloadMap;
	}

	FText AnnouncementFor(EOneBrahamMatchEnding Ending, const FString& WinnerName)
	{
		const FText Winner = FText::FromString(WinnerName.IsEmpty() ? FString(TEXT("A squad")) : WinnerName);

		// The same voice that has read out every lockdown: procedural, polite, and
		// completely uninterested in what has happened on floor 11.
		switch (Ending)
		{
		case EOneBrahamMatchEnding::Extracted:
			return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_Extracted",
				"{0} has been extracted. Thank you for visiting One Braham."), Winner);

		case EOneBrahamMatchEnding::LastSquadStanding:
			return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_LastSquad",
				"{0} is the only party remaining on site. All other visitors have been signed out."), Winner);

		case EOneBrahamMatchEnding::NoSurvivors:
			return NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_NoSurvivors",
				"All staff accounted for. There are no survivors. Please await further instruction.");

		case EOneBrahamMatchEnding::TimeOut:
			return WinnerName.IsEmpty()
				? NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_TimeOut",
					"The building is now closed. Nobody has been extracted. Please await further instruction.")
				: FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_DrillTimeOut",
					"The fire drill has ended. {0} recorded the better evacuation. Please return to your desks."), Winner);

		case EOneBrahamMatchEnding::StaffWithdrawn:
			return NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_Withdrawn",
				"All registered visitors have been signed out. The remaining occupants are not covered by this notice.");

		case EOneBrahamMatchEnding::ScoreLimit:
			return FText::Format(NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_ScoreLimit",
				"The fire drill is complete. {0} evacuated to standard. Please return to your desks."), Winner);

		case EOneBrahamMatchEnding::Draw:
			return NSLOCTEXT("OneBraham", "MatchEnd_Tannoy_Draw",
				"The fire drill has ended. Both departments are level. Please return to your desks.");

		case EOneBrahamMatchEnding::None:
		default:
			return FText::GetEmpty();
		}
	}

	FText HeadlineFor(EOneBrahamMatchEnding Ending)
	{
		switch (Ending)
		{
		case EOneBrahamMatchEnding::Extracted:			return NSLOCTEXT("OneBraham", "MatchEnd_Head_Extracted", "EXTRACTED");
		case EOneBrahamMatchEnding::LastSquadStanding:	return NSLOCTEXT("OneBraham", "MatchEnd_Head_LastSquad", "LAST SQUAD STANDING");
		case EOneBrahamMatchEnding::NoSurvivors:		return NSLOCTEXT("OneBraham", "MatchEnd_Head_NoSurvivors", "NO SURVIVORS");
		case EOneBrahamMatchEnding::TimeOut:			return NSLOCTEXT("OneBraham", "MatchEnd_Head_TimeOut", "TIME OUT");
		case EOneBrahamMatchEnding::StaffWithdrawn:		return NSLOCTEXT("OneBraham", "MatchEnd_Head_Withdrawn", "SIGNED OUT");
		case EOneBrahamMatchEnding::ScoreLimit:			return NSLOCTEXT("OneBraham", "MatchEnd_Head_ScoreLimit", "DRILL COMPLETE");
		case EOneBrahamMatchEnding::Draw:				return NSLOCTEXT("OneBraham", "MatchEnd_Head_Draw", "DRAW");
		case EOneBrahamMatchEnding::None:
		default:										return NSLOCTEXT("OneBraham", "MatchEnd_Head_Concluded", "CONCLUDED");
		}
	}

	FString LogNameFor(EOneBrahamMatchEnding Ending)
	{
		switch (Ending)
		{
		case EOneBrahamMatchEnding::Extracted:			return TEXT("extracted");
		case EOneBrahamMatchEnding::LastSquadStanding:	return TEXT("last squad standing");
		case EOneBrahamMatchEnding::NoSurvivors:		return TEXT("no survivors");
		case EOneBrahamMatchEnding::TimeOut:			return TEXT("time out");
		case EOneBrahamMatchEnding::StaffWithdrawn:		return TEXT("staff withdrawn");
		case EOneBrahamMatchEnding::ScoreLimit:			return TEXT("score limit");
		case EOneBrahamMatchEnding::Draw:				return TEXT("draw");
		case EOneBrahamMatchEnding::None:
		default:										return TEXT("none");
		}
	}

	bool HasWinner(EOneBrahamMatchEnding Ending)
	{
		return Ending == EOneBrahamMatchEnding::Extracted
			|| Ending == EOneBrahamMatchEnding::LastSquadStanding
			|| Ending == EOneBrahamMatchEnding::ScoreLimit;
	}

	bool IsResultCoherent(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode,
		int32 WinningSquadId, int32 ExtractedCount, const TArray<FOneBrahamSquadStanding>& Squads,
		const TArray<int32>& TeamScores, int32 ScoreToWin, FString& OutProblem)
	{
		OutProblem.Reset();

		auto Fail = [&OutProblem](const FString& Problem)
		{
			OutProblem = Problem;
			return false;
		};

		const FOneBrahamSquadStanding* Winner = Squads.FindByPredicate([WinningSquadId](const FOneBrahamSquadStanding& Squad)
		{
			return Squad.SquadId == WinningSquadId;
		});

		int32 SquadsInMatch = 0;
		int32 SquadsInFight = 0;
		int32 TotalExtracted = 0;
		for (const FOneBrahamSquadStanding& Squad : Squads)
		{
			SquadsInMatch += Squad.Members > 0 ? 1 : 0;
			SquadsInFight += Squad.InFight > 0 ? 1 : 0;
			TotalExtracted += Squad.Extracted;
		}

		const bool bFireDrill = Mode == EOneBrahamMatchMode::FireDrill;
		const int32 BestScore = TeamScores.Num() > 0 ? FMath::Max(TeamScores) : 0;
		int32 TeamsOnBest = 0;
		for (const int32 Score : TeamScores)
		{
			TeamsOnBest += Score == BestScore ? 1 : 0;
		}

		switch (Ending)
		{
		case EOneBrahamMatchEnding::None:
			return Fail(TEXT("the match has no ending"));

		case EOneBrahamMatchEnding::Extracted:
			if (bFireDrill) { return Fail(TEXT("an extraction in FIRE DRILL")); }
			if (Winner == nullptr) { return Fail(TEXT("extracted with no winning squad")); }
			if (ExtractedCount < 1 || Winner->Extracted < 1) { return Fail(TEXT("extracted with nobody on the wire")); }
			if (TotalExtracted != Winner->Extracted) { return Fail(TEXT("players of a losing squad marked extracted")); }
			return true;

		case EOneBrahamMatchEnding::LastSquadStanding:
			if (bFireDrill) { return Fail(TEXT("last squad standing in FIRE DRILL")); }
			if (Winner == nullptr) { return Fail(TEXT("last squad standing with no squad")); }
			if (SquadsInMatch < 2) { return Fail(TEXT("last squad standing in a one-squad match")); }
			if (SquadsInFight != 1 || Winner->InFight < 1) { return Fail(FString::Printf(TEXT("last squad standing with %d squads still in the fight"), SquadsInFight)); }
			if (TotalExtracted > 0) { return Fail(TEXT("last squad standing with players extracted")); }
			return true;

		case EOneBrahamMatchEnding::NoSurvivors:
			if (Winner != nullptr) { return Fail(TEXT("no survivors with a winner")); }
			if (SquadsInFight > 0) { return Fail(FString::Printf(TEXT("no survivors with %d squads in the fight"), SquadsInFight)); }
			return true;

		case EOneBrahamMatchEnding::StaffWithdrawn:
			if (Winner != nullptr) { return Fail(TEXT("staff withdrawn with a winner")); }
			if (TotalExtracted > 0) { return Fail(TEXT("staff withdrawn with players extracted")); }
			return true;

		case EOneBrahamMatchEnding::TimeOut:
			if (!bFireDrill)
			{
				if (Winner != nullptr) { return Fail(TEXT("an ASCENT time out with a winner")); }
				if (TotalExtracted > 0) { return Fail(TEXT("an ASCENT time out with players extracted")); }
				return true;
			}
			if (!TeamScores.IsValidIndex(WinningSquadId)) { return Fail(TEXT("a FIRE DRILL time out with no winning team")); }
			if (TeamScores[WinningSquadId] != BestScore || TeamsOnBest != 1) { return Fail(TEXT("a FIRE DRILL time out won without the most points")); }
			return true;

		case EOneBrahamMatchEnding::ScoreLimit:
			if (!bFireDrill) { return Fail(TEXT("a score limit outside FIRE DRILL")); }
			if (!TeamScores.IsValidIndex(WinningSquadId)) { return Fail(TEXT("a score limit with no winning team")); }
			if (TeamScores[WinningSquadId] < ScoreToWin) { return Fail(FString::Printf(TEXT("a score limit won on %d of %d points"), TeamScores[WinningSquadId], ScoreToWin)); }
			if (TeamScores[WinningSquadId] != BestScore) { return Fail(TEXT("a score limit won without the most points")); }
			return true;

		case EOneBrahamMatchEnding::Draw:
			if (!bFireDrill) { return Fail(TEXT("a draw outside FIRE DRILL")); }
			if (WinningSquadId != INDEX_NONE) { return Fail(TEXT("a draw with a winner")); }
			if (TeamsOnBest < 2 && TeamScores.Num() > 1) { return Fail(TEXT("a draw with one team ahead")); }
			return true;

		default:
			return Fail(TEXT("an unknown ending"));
		}
	}
}
