// Copyright (c) One Braham. All Rights Reserved.

#include "Online/OneBrahamSteamSubsystem.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineAchievementsInterface.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineLeaderboardInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "OnlineStats.h"
#include "OnlineSubsystem.h"
#include "UI/OneBrahamUIStyle.h"

const TCHAR* const UOneBrahamSteamSubsystem::StatMatches = TEXT("STAT_MATCHES");
const TCHAR* const UOneBrahamSteamSubsystem::StatExtractions = TEXT("STAT_EXTRACTIONS");
const TCHAR* const UOneBrahamSteamSubsystem::StatKills = TEXT("STAT_KILLS");
const TCHAR* const UOneBrahamSteamSubsystem::StatRevives = TEXT("STAT_REVIVES");
const TCHAR* const UOneBrahamSteamSubsystem::StatFireDrillWins = TEXT("STAT_FIREDRILL_WINS");
const TCHAR* const UOneBrahamSteamSubsystem::StatHighestFloor = TEXT("STAT_HIGHEST_FLOOR");

namespace
{
	/** Ten minutes from the lobby to the winch: a fast, clean run. */
	constexpr float ExpressLiftSeconds = 600.f;

	const FOneBrahamAchievementDefinition GAchievements[] =
	{
		{ TEXT("ACH_CLOCKED_IN"), TEXT("Clocked In"), TEXT("Finish your first match."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.TotalMatchesPlayed >= 1; } },
		{ TEXT("ACH_OVERTIME"), TEXT("Overtime"), TEXT("Finish 100 matches."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.TotalMatchesPlayed >= 100; } },
		{ TEXT("ACH_FIRST_EXTRACT"), TEXT("Signed Out"), TEXT("Extract from the Skydeck."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.TotalExtractions >= 1; } },
		{ TEXT("ACH_EXTRACT_5"), TEXT("Frequent Flyer"), TEXT("Extract five times."), TEXT("OB.Cosmetic.Unlock.Milestone.Extract5"),
			[](const UOneBrahamSaveSettings& R) { return R.TotalExtractions >= 5; } },
		{ TEXT("ACH_EXTRACT_25"), TEXT("Gold Status"), TEXT("Extract 25 times."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.TotalExtractions >= 25; } },
		{ TEXT("ACH_SKYDECK"), TEXT("Top Floor"), TEXT("Reach the Skydeck in ASCENT."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.LongestAscentFloorReached >= OneBrahamConstants::FloorSkydeck; } },
		{ TEXT("ACH_EXPRESS_LIFT"), TEXT("Express Lift"), TEXT("Extract from ASCENT in under ten minutes."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.BestAscentTimeSeconds > 0.f && R.BestAscentTimeSeconds < ExpressLiftSeconds; } },
		{ TEXT("ACH_CONSCIENTIOUS_OBJECTOR"), TEXT("Conscientious Objector"), TEXT("Extract without dealing a point of damage."), TEXT("OB.Cosmetic.Unlock.Milestone.NoDamageExtract"),
			[](const UOneBrahamSaveSettings& R) { return R.bExtractedWithoutDealingDamage; } },
		{ TEXT("ACH_HOLD_THE_FLOOR"), TEXT("Hold the Floor"), TEXT("Win ASCENT as the last squad standing, on Server & Comms."), TEXT("OB.Cosmetic.Unlock.Milestone.ServerFloorWin"),
			[](const UOneBrahamSaveSettings& R) { return R.bWonOnServerFloors; } },
		{ TEXT("ACH_HEADCOUNT"), TEXT("Headcount Reduction"), TEXT("Eliminate 100 players."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.TotalKills >= 100; } },
		{ TEXT("ACH_FIRST_AIDER"), TEXT("Designated First Aider"), TEXT("Revive 25 squadmates."), nullptr,
			[](const UOneBrahamSaveSettings& R) { return R.TotalRevives >= 25; } },
		{ TEXT("ACH_FIRE_WARDEN"), TEXT("Fire Warden"), TEXT("Win 50 FIRE DRILL matches."), TEXT("OB.Cosmetic.Unlock.Achievement.FireDrill50"),
			[](const UOneBrahamSaveSettings& R) { return R.TotalFireDrillWins >= 50; } },
	};

	IOnlineSubsystem* GetOSS()
	{
		return IOnlineSubsystem::Get();
	}

	/** The signed-in local user, or null when there is no one to write for. */
	FUniqueNetIdPtr GetLocalUserId()
	{
		const IOnlineSubsystem* OnlineSubsystem = GetOSS();
		const IOnlineIdentityPtr Identity = OnlineSubsystem ? OnlineSubsystem->GetIdentityInterface() : nullptr;
		if (!Identity.IsValid() || Identity->GetLoginStatus(0) != ELoginStatus::LoggedIn)
		{
			return nullptr;
		}
		return Identity->GetUniquePlayerId(0);
	}
}

bool UOneBrahamSteamSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UOneBrahamSteamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const IOnlineSubsystem* OnlineSubsystem = GetOSS())
	{
		if (const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem->GetExternalUIInterface())
		{
			OverlayDelegateHandle = ExternalUI->AddOnExternalUIChangeDelegate_Handle(
				FOnExternalUIChangeDelegate::CreateUObject(this, &UOneBrahamSteamSubsystem::HandleOverlayChange));
		}
	}

	PresenceTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOneBrahamSteamSubsystem::PresenceTick), PresenceIntervalSeconds);

	QueryAchievements();
}

void UOneBrahamSteamSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(PresenceTickHandle);
	PresenceTickHandle.Reset();

	if (const IOnlineSubsystem* OnlineSubsystem = GetOSS())
	{
		if (const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem->GetExternalUIInterface())
		{
			ExternalUI->ClearOnExternalUIChangeDelegate_Handle(OverlayDelegateHandle);
		}
	}

	Super::Deinitialize();
}

// ==========================================================================
//  The career record
// ==========================================================================
TConstArrayView<FOneBrahamAchievementDefinition> UOneBrahamSteamSubsystem::GetAchievementDefinitions()
{
	return MakeArrayView(GAchievements);
}

const FOneBrahamPlayerSummary* UOneBrahamSteamSubsystem::FindSummary(const FOneBrahamMatchResult& Result, int32 PlayerId)
{
	if (PlayerId == INDEX_NONE)
	{
		return nullptr;
	}
	return Result.PlayerSummaries.FindByPredicate([PlayerId](const FOneBrahamPlayerSummary& Row)
	{
		return Row.PlayerId == PlayerId;
	});
}

bool UOneBrahamSteamSubsystem::ApplyMatchToRecord(UOneBrahamSaveSettings& Record, const FOneBrahamMatchResult& Result, const FOneBrahamPlayerSummary& Summary)
{
	if (Result.Ending == EOneBrahamMatchEnding::None)
	{
		return false;
	}

	++Record.TotalMatchesPlayed;
	Record.TotalKills += FMath::Max(0, Summary.Kills);
	Record.TotalDowns += FMath::Max(0, Summary.Downs);
	Record.TotalRevives += FMath::Max(0, Summary.Revives);

	const bool bWon = Summary.Team != EOneBrahamTeam::None
		&& Summary.Team == Result.WinningTeam
		&& !Result.bNoSurvivors;

	if (Summary.bExtracted)
	{
		++Record.TotalExtractions;
	}

	if (Result.Mode == EOneBrahamMatchMode::Ascent)
	{
		Record.LongestAscentFloorReached = FMath::Max(Record.LongestAscentFloorReached, Summary.FloorsClimbed);

		if (Summary.bExtracted)
		{
			if (Result.MatchDurationSeconds > 0.f
				&& (Record.BestAscentTimeSeconds <= 0.f || Result.MatchDurationSeconds < Record.BestAscentTimeSeconds))
			{
				Record.BestAscentTimeSeconds = Result.MatchDurationSeconds;
			}
			if (Summary.DamageDealt <= 0.f)
			{
				Record.bExtractedWithoutDealingDamage = true;
			}
		}

		const bool bOnServerFloors = Summary.FloorsClimbed >= OneBrahamConstants::FloorServer
			&& Summary.FloorsClimbed < OneBrahamConstants::FloorExecutive;
		if (bWon && Result.Ending == EOneBrahamMatchEnding::LastSquadStanding && bOnServerFloors)
		{
			Record.bWonOnServerFloors = true;
		}
	}
	else if (Result.Mode == EOneBrahamMatchMode::FireDrill && bWon)
	{
		++Record.TotalFireDrillWins;
	}

	return true;
}

TArray<FName> UOneBrahamSteamSubsystem::EvaluateAchievements(UOneBrahamSaveSettings& Record)
{
	TArray<FName> Newly;
	for (const FOneBrahamAchievementDefinition& Definition : GAchievements)
	{
		const FName AchievementId(Definition.Id);
		if (Record.UnlockedAchievements.Contains(AchievementId) || !Definition.IsEarned(Record))
		{
			continue;
		}
		Record.UnlockedAchievements.Add(AchievementId);
		if (Definition.UnlockTag != nullptr)
		{
			Record.EarnedUnlockTags.AddUnique(FName(Definition.UnlockTag));
		}
		Newly.Add(AchievementId);
	}
	return Newly;
}

UOneBrahamSaveSettings* UOneBrahamSteamSubsystem::GetRecord() const
{
	const UOneBrahamGameInstance* GameInstance = Cast<UOneBrahamGameInstance>(GetGameInstance());
	return GameInstance ? GameInstance->GetProfile() : nullptr;
}

bool UOneBrahamSteamSubsystem::IsAchievementUnlocked(FName AchievementId) const
{
	const UOneBrahamSaveSettings* Record = GetRecord();
	return Record != nullptr && Record->UnlockedAchievements.Contains(AchievementId);
}

void UOneBrahamSteamSubsystem::HandleMatchResult(const FOneBrahamMatchResult& Result)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	UOneBrahamGameInstance* GameInstance = Cast<UOneBrahamGameInstance>(GetGameInstance());
	UOneBrahamSaveSettings* Record = GetRecord();
	if (GameState == nullptr || GameInstance == nullptr || Record == nullptr)
	{
		return;
	}

	// The result replicates as a struct and can arrive more than once; a match
	// is one game state, and it counts once.
	if (LastRecordedGameState.Get() == GameState)
	{
		return;
	}

	const APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World);
	const APlayerState* LocalState = PC ? PC->PlayerState : nullptr;
	const FOneBrahamPlayerSummary* Summary = FindSummary(Result, LocalState ? LocalState->GetPlayerId() : INDEX_NONE);
	if (Summary == nullptr || !ApplyMatchToRecord(*Record, Result, *Summary))
	{
		return;
	}
	LastRecordedGameState = GameState;

	const TArray<FName> Newly = EvaluateAchievements(*Record);
	LastMatchAchievements = Newly;
	GameInstance->SaveProfile();

	for (const FName& AchievementId : Newly)
	{
		for (const FOneBrahamAchievementDefinition& Definition : GAchievements)
		{
			if (AchievementId == FName(Definition.Id))
			{
				OB_LOG("Achievement earned: %s (%s)", Definition.Id, Definition.Title);
				OnAchievementUnlocked.Broadcast(AchievementId, FText::FromString(Definition.Title));

				// The results screen is up; the banner sits over it. Steam shows
				// its own toast too, when the overlay is enabled.
				if (AOneBrahamPlayerController* OwnController = Cast<AOneBrahamPlayerController>(GameInstance->GetFirstLocalPlayerController(World)))
				{
					OwnController->ClientShowBanner(
						FText::Format(NSLOCTEXT("OneBraham", "AchievementBanner", "ACHIEVEMENT - {0}"), FText::FromString(FString(Definition.Title).ToUpper())),
						4.f, UOneBrahamUIStyle::SafetyYellow());
				}
			}
		}
	}

	FlushAchievements();
	WriteStats();
	RefreshRichPresence();
}

// ==========================================================================
//  Achievements and stats
// ==========================================================================
void UOneBrahamSteamSubsystem::QueryAchievements()
{
	if (bAchievementsQueried || bAchievementQueryInFlight)
	{
		return;
	}

	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlineAchievementsPtr Achievements = OnlineSubsystem ? OnlineSubsystem->GetAchievementsInterface() : nullptr;
	const FUniqueNetIdPtr UserId = GetLocalUserId();
	if (!Achievements.IsValid() || !UserId.IsValid())
	{
		return;	// the presence tick tries again once someone is signed in
	}

	bAchievementQueryInFlight = true;
	Achievements->QueryAchievements(*UserId, FOnQueryAchievementsCompleteDelegate::CreateWeakLambda(this,
		[this](const FUniqueNetId&, const bool bWasSuccessful)
		{
			bAchievementQueryInFlight = false;
			bAchievementsQueried = bWasSuccessful;
			if (bWasSuccessful)
			{
				// Anything earned offline, or lost to a crash, goes up now.
				if (UOneBrahamSaveSettings* Record = GetRecord())
				{
					EvaluateAchievements(*Record);
				}
				FlushAchievements();
			}
		}));
}

void UOneBrahamSteamSubsystem::FlushAchievements()
{
	const UOneBrahamSaveSettings* Record = GetRecord();
	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlineAchievementsPtr Achievements = OnlineSubsystem ? OnlineSubsystem->GetAchievementsInterface() : nullptr;
	const FUniqueNetIdPtr UserId = GetLocalUserId();
	if (Record == nullptr || !Achievements.IsValid() || !UserId.IsValid())
	{
		return;
	}
	if (!bAchievementsQueried)
	{
		QueryAchievements();	// the write follows the answer
		return;
	}

	const FOnlineAchievementsWriteRef WriteObject = MakeShared<FOnlineAchievementsWrite, ESPMode::ThreadSafe>();
	int32 Pending = 0;
	for (const FName& AchievementId : Record->UnlockedAchievements)
	{
		FOnlineAchievement Cached;
		const bool bKnown = Achievements->GetCachedAchievement(*UserId, AchievementId.ToString(), Cached) == EOnlineCachedResult::Success;
		if (bKnown && Cached.Progress >= 100.0)
		{
			continue;
		}
		if (!bKnown)
		{
			// Not configured on the partner site: say so once, write nothing.
			OB_WARN("Achievement %s is not configured on Steamworks.", *AchievementId.ToString());
			continue;
		}
		WriteObject->SetFloatStat(AchievementId.ToString(), 100.f);
		++Pending;
	}

	if (Pending > 0)
	{
		FOnlineAchievementsWriteRef WriteRef = WriteObject;
		Achievements->WriteAchievements(*UserId, WriteRef, FOnAchievementsWrittenDelegate::CreateWeakLambda(this,
			[Pending](const FUniqueNetId&, bool bWasSuccessful)
			{
				OB_LOG("Steam achievements: %d written, %s.", Pending, bWasSuccessful ? TEXT("stored") : TEXT("failed; retried next match"));
			}));
	}
}

void UOneBrahamSteamSubsystem::WriteStats()
{
	const UOneBrahamSaveSettings* Record = GetRecord();
	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlineLeaderboardsPtr Leaderboards = OnlineSubsystem ? OnlineSubsystem->GetLeaderboardsInterface() : nullptr;
	const FUniqueNetIdPtr UserId = GetLocalUserId();
	if (Record == nullptr || !Leaderboards.IsValid() || !UserId.IsValid())
	{
		return;
	}

	// The totals, not the deltas: a lost write is repaired by the next one.
	FOnlineLeaderboardWrite Write;
	Write.SetIntStat(StatMatches, Record->TotalMatchesPlayed);
	Write.SetIntStat(StatExtractions, Record->TotalExtractions);
	Write.SetIntStat(StatKills, Record->TotalKills);
	Write.SetIntStat(StatRevives, Record->TotalRevives);
	Write.SetIntStat(StatFireDrillWins, Record->TotalFireDrillWins);
	Write.SetIntStat(StatHighestFloor, Record->LongestAscentFloorReached);
	Leaderboards->WriteLeaderboards(UOneBrahamGameInstance::SessionName, *UserId, Write);
	Leaderboards->FlushLeaderboards(UOneBrahamGameInstance::SessionName);
}

// ==========================================================================
//  Rich presence
// ==========================================================================
bool UOneBrahamSteamSubsystem::PresenceTick(float DeltaSeconds)
{
	QueryAchievements();
	RefreshRichPresence();
	return true;
}

void UOneBrahamSteamSubsystem::ComputePresence(FString& OutText, FString& OutToken, FString& OutFloor) const
{
	OutFloor.Reset();

	const UOneBrahamGameInstance* GameInstance = Cast<UOneBrahamGameInstance>(GetGameInstance());
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const AOneBrahamGameState* GameState = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	if (GameState == nullptr)
	{
		const bool bSearching = GameInstance != nullptr && GameInstance->IsMatchmaking();
		OutToken = bSearching ? TEXT("#Status_Searching") : TEXT("#Status_Menu");
		OutText = bSearching ? TEXT("Looking for a match") : TEXT("In the lobby");
		return;
	}

	if (GameState->MatchPhase == EOneBrahamMatchPhase::Complete || GameState->MatchPhase == EOneBrahamMatchPhase::PostMatch)
	{
		OutToken = TEXT("#Status_Results");
		OutText = TEXT("Reading the results");
		return;
	}

	const APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World);
	const AOneBrahamPlayerState* State = PC ? PC->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	if (State != nullptr && State->IsEliminated())
	{
		OutToken = TEXT("#Status_Spectating");
		OutText = TEXT("Out of it, spectating");
		return;
	}

	switch (GameState->MatchMode)
	{
	case EOneBrahamMatchMode::FireDrill:
		OutToken = TEXT("#Status_FireDrill");
		OutText = TEXT("Running a FIRE DRILL");
		break;
	case EOneBrahamMatchMode::NightShift:
		OutToken = TEXT("#Status_NightShift");
		OutText = TEXT("On the NIGHT SHIFT");
		break;
	default:
	{
		const int32 Floor = State ? FMath::Max(State->CurrentFloor, OneBrahamConstants::FloorLobby) : OneBrahamConstants::FloorLobby;
		OutFloor = FString::FromInt(Floor);
		OutToken = TEXT("#Status_Ascent");
		OutText = FString::Printf(TEXT("ASCENT - floor %d of %d"), Floor, OneBrahamConstants::FloorSkydeck);
		break;
	}
	}
}

void UOneBrahamSteamSubsystem::RefreshRichPresence()
{
	FString Text;
	FString Token;
	FString Floor;
	ComputePresence(Text, Token, Floor);
	LastPresenceText = Text;

	const FString Key = Token + TEXT("|") + Floor;
	if (Key == LastSentPresence)
	{
		return;
	}

	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlinePresencePtr Presence = OnlineSubsystem ? OnlineSubsystem->GetPresenceInterface() : nullptr;
	const FUniqueNetIdPtr UserId = GetLocalUserId();
	if (!Presence.IsValid() || !UserId.IsValid())
	{
		return;
	}

	FOnlineUserPresenceStatus Status;
	Status.State = EOnlinePresenceState::Online;
	Status.StatusStr = Text;
	// steam_display picks the localised line; its %floor% comes from "floor".
	Status.Properties.Add(TEXT("steam_display"), FVariantData(Token));
	Status.Properties.Add(TEXT("floor"), FVariantData(Floor));
	Presence->SetPresence(*UserId, Status);
	LastSentPresence = Key;
}

// ==========================================================================
//  Overlay
// ==========================================================================
void UOneBrahamSteamSubsystem::HandleOverlayChange(bool bIsOpening)
{
	bOverlayOpen = bIsOpening;

	// Keys held when the overlay opened never see their release: let go of them
	// all, or the player comes back still sprinting.
	if (bIsOpening && GetGameInstance() != nullptr)
	{
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
		{
			PC->FlushPressedKeys();
		}
	}

	OnOverlayChanged.Broadcast(bIsOpening);
}

bool UOneBrahamSteamSubsystem::ShowFriendsOverlay()
{
	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem ? OnlineSubsystem->GetExternalUIInterface() : nullptr;
	return ExternalUI.IsValid() && ExternalUI->ShowFriendsUI(0);
}

bool UOneBrahamSteamSubsystem::ShowInviteOverlay()
{
	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem ? OnlineSubsystem->GetExternalUIInterface() : nullptr;
	return ExternalUI.IsValid() && ExternalUI->ShowInviteUI(0, UOneBrahamGameInstance::SessionName);
}

bool UOneBrahamSteamSubsystem::ShowStoreOverlay()
{
	const IOnlineSubsystem* OnlineSubsystem = GetOSS();
	const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem ? OnlineSubsystem->GetExternalUIInterface() : nullptr;
	return ExternalUI.IsValid() && ExternalUI->ShowStoreUI(0, FShowStoreParams());
}
