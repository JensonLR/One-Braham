// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDownedComponent.h
// ---------------------------------------------------------------------------
// The ASCENT downed state. This is the emotional core of the mode: you are on
// the carpet, you can crawl, you can talk, and you have sixty seconds to be
// dragged back up by someone who is probably also about to die.
//
// Rules (all tunable in DA_MatchSettings_Ascent):
//   * A lethal hit puts you down rather than out, unless it was a headshot or
//     you have no revives left.
//   * You crawl at 60 cm/s, you bleed out in 60 s, you can still use proximity
//     voice (this is where the funniest and most human moments happen).
//   * A squadmate holds interact for 4.5 s to revive you to 40 HP.
//   * An enemy can hold interact for 2 s to execute you (instant, no revive).
//   * You cannot shoot, sprint, mantle, slide, use a terminal or open a door.
//   * You CAN be revived through one penetrable surface (cubicle wall), which
//     produces the single best clip moment in the game.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamDownedComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamDownedStateChanged, bool, bIsDowned, float, BleedOutRemaining);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamReviveProgress, float, Progress, AActor*, Reviver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamExecuted, AActor*, Executor);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamDownedComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamDownedComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Config
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	float BleedOutSeconds = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	float ReviveSeconds = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	float ReviveHealth = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	float ExecuteSeconds = 2.f;

	/** Downed players get a pistol-out crawl animation but cannot fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	float CrawlSpeed = 60.f;

	/** You can be revived through a thin penetrable surface (cubicle wall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	bool bAllowReviveThroughPenetrable = true;

	/** Headshots and explosions skip the downed state entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	bool bHeadshotsBypassDowned = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Downed")
	bool bExplosionsBypassDowned = true;

	// ======================================================================
	//  State
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Downed")
	bool IsDowned() const { return bDowned; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Downed")
	float GetBleedOutRemaining() const { return BleedOutRemaining; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Downed")
	float GetBleedOutFraction() const { return BleedOutSeconds > 0.f ? FMath::Clamp(BleedOutRemaining / BleedOutSeconds, 0.f, 1.f) : 0.f; }

	/** True while this player is dragging a downed squadmate. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Downed")
	bool IsCarryingBody() const { return bCarryingBody; }

	/** Server: decides whether a lethal hit should down the player instead of killing them. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Downed")
	bool ShouldEnterDownedState(AActor* Killer, const FOneBrahamDamageReport& Report) const;

	/** Server: puts the owner into the downed state. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Downed")
	void EnterDownedState(AActor* Killer, const FOneBrahamDamageReport& Report);

	/** Server: revives the owner (called by the reviver's interact component). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Downed")
	void ServerRevive(AController* ReviverController);

	/** Server: executes the owner (enemy hold-interact, instant death). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Downed")
	void ServerExecute(AController* ExecutorController);

	/** Server: bleeds out (timer expired) - counts as a death to the original killer. */
	void BleedOut();

	/** Called by the interact component each frame while a reviver holds the button. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Downed")
	void SetReviveProgress(float Progress, AActor* Reviver);

	UFUNCTION(BlueprintPure, Category = "One Braham|Downed")
	float GetReviveProgress() const { return ReviveProgress; }

	/** Progressive "last words" voice lines: fires the down line once, then quips. */
	void TriggerDownVoiceLine();

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Downed") FOneBrahamDownedStateChanged OnDownedStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Downed") FOneBrahamReviveProgress OnReviveProgress;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Downed") FOneBrahamExecuted OnExecuted;

	// --- Replicated -------------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Downed, BlueprintReadOnly, Category = "One Braham|Downed")
	bool bDowned = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Downed")
	float BleedOutRemaining = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Downed")
	int32 ReviveCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Downed")
	float ReviveProgress = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Downed")
	bool bCarryingBody = false;

	/** Who put us down - credited with the kill if we bleed out. */
	TWeakObjectPtr<AActor> DownedBy;

protected:
	UFUNCTION() void OnRep_Downed();

	/** The kill credit report, replayed when we eventually die. */
	FOneBrahamDamageReport DownedReport;

	/** Time-based quips while waiting to be revived. */
	float NextQuipTime = 0.f;
	int32 QuipsSpoken = 0;

	/** Cached character/movement references. */
	TWeakObjectPtr<class AOneBrahamCharacter> CachedCharacter;
};
