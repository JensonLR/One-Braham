// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInductionComponent.h
// ---------------------------------------------------------------------------
// Runs the first-time induction (Core/OneBrahamInduction.h) for the local
// player: measures what they have done, shows each prompt on the HUD's
// sub-banner, and marks the profile when they finish so it never runs twice.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamInduction.h"
#include "OneBrahamInductionComponent.generated.h"

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamInductionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamInductionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Starts the induction if this player has never finished it. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Induction")
	void StartIfFirstTime();

	/** Ends it early (pause menu, or the player opts out). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Induction")
	void Skip();

	UFUNCTION(BlueprintPure, Category = "One Braham|Induction")
	bool IsRunning() const { return bRunning; }

private:
	void ShowPrompt();
	void Finish();

	FOneBrahamInduction Induction;
	FOneBrahamInductionSnapshot Measured;
	FVector LastLocation = FVector::ZeroVector;
	float LastYaw = 0.f;
	bool bHaveLast = false;
	bool bRunning = false;
	float PromptRefresh = 0.f;
};
