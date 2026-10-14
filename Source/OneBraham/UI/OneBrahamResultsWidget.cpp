// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamResultsWidget.h"

#include "Components/TextBlock.h"
#include "Core/OneBrahamGameInstance.h"
#include "Online/OneBrahamSteamSubsystem.h"

namespace
{
	FText TeamName(EOneBrahamTeam Team)
	{
		const UEnum* Enum = StaticEnum<EOneBrahamTeam>();
		return Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(Team)) : FText::GetEmpty();
	}

	FText Clock(float Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::RoundToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60));
	}
}

UOneBrahamResultsWidget::UOneBrahamResultsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BackdropOpacity = 0.85f;
	ColumnWidth = 980.f;
}

void UOneBrahamResultsWidget::SetResult(const FOneBrahamMatchResult& InResult, int32 InLocalPlayerId)
{
	Result = InResult;
	LocalPlayerId = InLocalPlayerId;
	ShowPage();
}

FText UOneBrahamResultsWidget::GetHeadline(const FOneBrahamMatchResult& InResult, bool bLocalWon)
{
	switch (InResult.Ending)
	{
	case EOneBrahamMatchEnding::Extracted:
		return bLocalWon
			? NSLOCTEXT("OneBraham", "Results_YouExtracted", "You got out")
			: FText::Format(NSLOCTEXT("OneBraham", "Results_TheyExtracted", "{0} got out"),
				InResult.WinningSquadName.IsEmpty() ? TeamName(InResult.WinningTeam) : InResult.WinningSquadName);
	case EOneBrahamMatchEnding::LastSquadStanding:
		return bLocalWon
			? NSLOCTEXT("OneBraham", "Results_YouStanding", "Last squad standing")
			: FText::Format(NSLOCTEXT("OneBraham", "Results_TheyStanding", "{0} held the building"), TeamName(InResult.WinningTeam));
	case EOneBrahamMatchEnding::NoSurvivors:
		return NSLOCTEXT("OneBraham", "Results_NoSurvivors", "No survivors");
	case EOneBrahamMatchEnding::TimeOut:
		return NSLOCTEXT("OneBraham", "Results_TimeOut", "Out of time");
	case EOneBrahamMatchEnding::ScoreLimit:
		return bLocalWon
			? NSLOCTEXT("OneBraham", "Results_DrillWon", "Drill complete")
			: FText::Format(NSLOCTEXT("OneBraham", "Results_DrillLost", "{0} won the drill"), TeamName(InResult.WinningTeam));
	default:
		return NSLOCTEXT("OneBraham", "Results_Over", "Match over");
	}
}

FText UOneBrahamResultsWidget::GetPageTitle() const
{
	const FOneBrahamPlayerSummary* Mine = UOneBrahamSteamSubsystem::FindSummary(Result, LocalPlayerId);
	const bool bWon = Mine != nullptr && Mine->Team != EOneBrahamTeam::None && Mine->Team == Result.WinningTeam && !Result.bNoSurvivors;
	return GetHeadline(Result, bWon);
}

FText UOneBrahamResultsWidget::GetFooterText() const
{
	return NSLOCTEXT("OneBraham", "Results_Footer", "The building sends everyone back to the lobby shortly.");
}

void UOneBrahamResultsWidget::BuildPage()
{
	const UEnum* ModeEnum = StaticEnum<EOneBrahamMatchMode>();
	AddText(FText::Format(NSLOCTEXT("OneBraham", "Results_Summary", "{0}  -  {1}  -  {2} extracted"),
		ModeEnum ? ModeEnum->GetDisplayNameTextByValue(static_cast<int64>(Result.Mode)) : FText::GetEmpty(),
		Clock(Result.MatchDurationSeconds), FText::AsNumber(Result.ExtractedCount)),
		EOneBrahamTextSize::Small, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()));

	// You.
	if (const FOneBrahamPlayerSummary* Mine = UOneBrahamSteamSubsystem::FindSummary(Result, LocalPlayerId))
	{
		AddHeading(NSLOCTEXT("OneBraham", "Results_You", "You"));
		AddText(FText::Format(NSLOCTEXT("OneBraham", "Results_YourLine",
			"{0}. Floor {1}. {2} eliminations, {3} downs, {4} assists, {5} revives, {6} damage, {7} alive."),
			Mine->bExtracted ? NSLOCTEXT("OneBraham", "Results_Extracted", "Extracted") : NSLOCTEXT("OneBraham", "Results_NotOut", "Did not get out"),
			FText::AsNumber(Mine->FloorsClimbed), FText::AsNumber(Mine->Kills), FText::AsNumber(Mine->Downs),
			FText::AsNumber(Mine->Assists), FText::AsNumber(Mine->Revives), FText::AsNumber(FMath::RoundToInt(Mine->DamageDealt)),
			Clock(Mine->TimeAliveSeconds)), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::LightGrey());
	}

	// What it earned.
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UOneBrahamSteamSubsystem* Steam = GameInstance->GetSubsystem<UOneBrahamSteamSubsystem>())
		{
			const TArray<FName> Earned = Steam->GetLastMatchAchievements();
			if (Earned.Num() > 0)
			{
				AddHeading(NSLOCTEXT("OneBraham", "Results_Earned", "Earned"));
				for (const FOneBrahamAchievementDefinition& Entry : UOneBrahamSteamSubsystem::GetAchievementDefinitions())
				{
					if (Earned.Contains(FName(Entry.Id)))
					{
						AddText(FText::Format(NSLOCTEXT("OneBraham", "Results_Achievement", "{0}  -  {1}"),
							FText::FromString(FString(Entry.Title).ToUpper()), FText::FromString(Entry.Description)),
							EOneBrahamTextSize::Small, UOneBrahamUIStyle::SafetyYellow());
					}
				}
			}
		}
	}

	// Every squad, in the order the result lists them (squad by squad).
	EOneBrahamTeam CurrentTeam = EOneBrahamTeam::None;
	bool bFirstRow = true;
	for (const FOneBrahamPlayerSummary& Row : Result.PlayerSummaries)
	{
		if (bFirstRow || Row.Team != CurrentTeam)
		{
			CurrentTeam = Row.Team;
			bFirstRow = false;
			UTextBlock* Heading = AddHeading(TeamName(Row.Team));
			if (Heading != nullptr && Row.Team == Result.WinningTeam && !Result.bNoSurvivors)
			{
				Heading->SetColorAndOpacity(FSlateColor(UOneBrahamUIStyle::SuccessGreen()));
			}
		}
		const bool bMe = Row.PlayerId == LocalPlayerId;
		AddText(FText::Format(NSLOCTEXT("OneBraham", "Results_Row", "{0}{1}  -  {2} / {3} / {4}  -  {5} revives  -  {6} damage  -  floor {7}"),
			FText::FromString(Row.PlayerName), Row.bExtracted ? NSLOCTEXT("OneBraham", "Results_RowOut", " (out)") : FText::GetEmpty(),
			FText::AsNumber(Row.Kills), FText::AsNumber(Row.Downs), FText::AsNumber(Row.Assists), FText::AsNumber(Row.Revives),
			FText::AsNumber(FMath::RoundToInt(Row.DamageDealt)), FText::AsNumber(Row.FloorsClimbed)),
			EOneBrahamTextSize::Small, bMe ? UOneBrahamUIStyle::SignalBlue() : UOneBrahamUIStyle::LightGrey());
	}

	AddButton(NSLOCTEXT("OneBraham", "Results_Leave", "Return to menu"), [this]()
	{
		if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
		{
			GameInstance->TravelToMainMenu();
		}
	});
}
