// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSteamSubsystem.h
// ---------------------------------------------------------------------------
// The player's machine talking to Steam: the career record, achievements,
// stats, rich presence and the overlay.
//
// THE CAREER RECORD
//   The match result replicates to every machine. When it lands, this finds
//   the local player's own row in it and folds it into the profile: matches,
//   extractions, kills, downs, revives, FIRE DRILL wins, the highest floor and
//   the fastest extraction. That record is the truth. It is saved with the
//   profile, so a build without Steam keeps it, and everything below reads
//   from it rather than from the match.
//
// ACHIEVEMENTS
//   Each one is a rule over the record (GetAchievementDefinitions). Earned ids
//   are kept in the profile and written to Steam once Steam has answered the
//   achievement query: Steam refuses a write before that, so writes queue.
//   Re-evaluating on every start re-sends anything a crashed session lost.
//
// STATS
//   The career totals are written as Steam stats through the online
//   subsystem's leaderboard write, which is how OnlineSubsystemSteam exposes
//   SetStat and StoreStats. The stat names are the STAT_* constants below and
//   must exist on the Steamworks partner site (Docs/19, section 9).
//
// RICH PRESENCE
//   What the friends list says: in the lobby, looking for a match, which mode
//   and floor, spectating, reading the results. Recomputed every few seconds
//   and sent only when it changes. The steam_display tokens live in
//   Config/Steam/rich_presence_english.vdf.
//
// OVERLAY
//   Shift+Tab is Steam's. When it opens, every held key is released, so a
//   player does not come back to a character still sprinting into a wall.
//
// None of this runs on a dedicated server, and every call is a no-op when
// there is no online subsystem.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamSteamSubsystem.generated.h"

class UOneBrahamSaveSettings;

/** One achievement: its Steam API name, what the player reads, and the rule that earns it. */
struct ONEBRAHAM_API FOneBrahamAchievementDefinition
{
	/** The API name on the Steamworks partner site. */
	const TCHAR* Id;
	const TCHAR* Title;
	const TCHAR* Description;
	/** The cosmetic unlock tag it also grants, or null. */
	const TCHAR* UnlockTag;
	/** True when the career record has earned it. */
	bool (*IsEarned)(const UOneBrahamSaveSettings& Record);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamAchievementUnlocked, FName, AchievementId, const FText&, Title);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamOverlayChanged, bool, bOverlayOpen);

UCLASS()
class ONEBRAHAM_API UOneBrahamSteamSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ======================================================================
	//  The career record and achievements (pure, testable)
	// ======================================================================

	/** Every achievement, in the order the menu lists them. */
	static TConstArrayView<FOneBrahamAchievementDefinition> GetAchievementDefinitions();

	/**
	 * Folds one finished match into the record. Summary is the local player's
	 * own row. Returns false (and changes nothing) for a result with no ending.
	 */
	static bool ApplyMatchToRecord(UOneBrahamSaveSettings& Record, const FOneBrahamMatchResult& Result, const FOneBrahamPlayerSummary& Summary);

	/** Adds every newly earned achievement to the record; returns the new ids. */
	static TArray<FName> EvaluateAchievements(UOneBrahamSaveSettings& Record);

	/** The local player's row in a result, or null when they are not in it. */
	static const FOneBrahamPlayerSummary* FindSummary(const FOneBrahamMatchResult& Result, int32 PlayerId);

	// ======================================================================
	//  Steam
	// ======================================================================

	/** Bound to every game state's OnMatchResultReady; records each match once. */
	UFUNCTION()
	void HandleMatchResult(const FOneBrahamMatchResult& Result);

	/** Recomputes and, if it changed, sends the rich presence. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Steam")
	void RefreshRichPresence();

	/** The status line rich presence would show now (also used offline by the menu). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Steam")
	FString GetRichPresenceText() const { return LastPresenceText; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Steam")
	bool IsAchievementUnlocked(FName AchievementId) const;

	/** Achievements the last recorded match earned; the results screen lists them. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Steam")
	TArray<FName> GetLastMatchAchievements() const { return LastMatchAchievements; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Steam")
	bool IsOverlayOpen() const { return bOverlayOpen; }

	/** Opens the Steam friends list. False when there is no overlay. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Steam")
	bool ShowFriendsOverlay();

	/** Opens Steam's invite dialog for the current session. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Steam")
	bool ShowInviteOverlay();

	/** Opens the game's store page (cosmetics, DLC) in the overlay. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Steam")
	bool ShowStoreOverlay();

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Steam")
	FOneBrahamAchievementUnlocked OnAchievementUnlocked;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Steam")
	FOneBrahamOverlayChanged OnOverlayChanged;

	// Steam stat API names (Docs/19, section 9).
	static const TCHAR* const StatMatches;
	static const TCHAR* const StatExtractions;
	static const TCHAR* const StatKills;
	static const TCHAR* const StatRevives;
	static const TCHAR* const StatFireDrillWins;
	static const TCHAR* const StatHighestFloor;

	/** Seconds between rich presence recomputes. Steam rate-limits updates; this is well inside it. */
	static constexpr float PresenceIntervalSeconds = 5.f;

protected:
	bool PresenceTick(float DeltaSeconds);

	/** Asks Steam for the achievement list; writes wait for the answer. */
	void QueryAchievements();

	/** Sends every earned achievement Steam does not have yet. */
	void FlushAchievements();

	/** Sends the career totals as Steam stats. */
	void WriteStats();

	void HandleOverlayChange(bool bIsOpening);

	/** The presence for the current world: status text and steam_display token. */
	void ComputePresence(FString& OutText, FString& OutToken, FString& OutFloor) const;

	UOneBrahamSaveSettings* GetRecord() const;

	FTSTicker::FDelegateHandle PresenceTickHandle;
	FDelegateHandle OverlayDelegateHandle;

	/** The game state whose result was last recorded: a result replicates more than once. */
	TWeakObjectPtr<const UObject> LastRecordedGameState;

	TArray<FName> LastMatchAchievements;

	FString LastPresenceText;
	FString LastSentPresence;
	bool bAchievementsQueried = false;
	bool bAchievementQueryInFlight = false;
	bool bOverlayOpen = false;
};
