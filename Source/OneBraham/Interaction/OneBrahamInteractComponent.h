// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInteractComponent.h
// ---------------------------------------------------------------------------
// The player's interaction driver. Lives on AOneBrahamCharacter; the controller
// does the tracing and the HUD does the drawing, but everything that is
// authoritative happens in here on the server.
//
// Anti-cheat properties:
//   * The client sends an *actor reference* + intent, never a result.
//   * The server re-traces on its own copy of the world to confirm the target is
//     reachable and in front of the player.
//   * Hold progress is advanced server-side from the server clock; the client's
//     progress value is ignored beyond interpolation for the UI.
//   * Interaction distance is clamped (MaxInteractDistanceTolerance in settings),
//     so a modified client cannot reach a terminal from three floors away.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamInteractComponent.generated.h"

class AOneBrahamPlayerController;
class AOneBrahamCharacter;

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamInteractComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamInteractComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Client entry points (called by AOneBrahamPlayerController)
	// ======================================================================
	/**
	 * Starts (or continues) an interaction with a target.
	 * On a client this forwards to the server; on the server it validates and runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	void BeginInteraction(AActor* Target, FGameplayTag InteractionTag, bool bIsHold);

	/** Releases the button: a tap interaction fires here, a hold is cancelled or completed. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	void EndInteraction();

	/** Aborts without completing (death, moving out of range, being shot). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	void CancelInteraction();

	UFUNCTION(BlueprintPure, Category = "One Braham|Interaction")
	bool IsInteracting() const { return CurrentTarget != nullptr; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Interaction")
	float GetHoldProgress() const { return HoldProgress; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Interaction")
	AActor* GetCurrentInteractionTarget() const { return CurrentTarget; }

	// ======================================================================
	//  Server API (used by revives, and by anything scripted)
	// ======================================================================
	/** Server-only: immediately performs a tap interaction (used by AI/bots). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	bool ServerPerformInstantInteraction(AActor* Target);

	/** Revalidates that the interaction is still legal (distance, facing, LOS). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	bool ValidateInteractionStillLegal() const;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Interaction")
	FOneBrahamHoldProgressChanged OnHoldProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Interaction")
	FOneBrahamInteractionPromptChanged OnPromptChanged;

	// --- Replicated -------------------------------------------------------
	/** The target the local client is interacting with (for the HUD + third-person animation). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Interaction")
	TObjectPtr<AActor> CurrentTarget = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Interaction")
	float HoldProgress = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Interaction")
	bool bIsHoldInteraction = false;

	/** Interactors currently interacting with US (revive/execute targeting). */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Interaction")
	TArray<TWeakObjectPtr<AActor>> IncomingInteractors;

protected:
	// --- RPCs -------------------------------------------------------------
	UFUNCTION(Server, Reliable, Category = "One Braham|Interaction")
	void ServerBeginInteraction(AActor* Target, FGameplayTag InteractionTag, bool bIsHold);

	UFUNCTION(Server, Reliable, Category = "One Braham|Interaction")
	void ServerEndInteraction(bool bCompleted);

	// --- Internal ---------------------------------------------------------
	/** Server: ticks hold progress and completes when the duration is met. */
	void TickHoldProgress(float DeltaTime);

	/** Plays the correct first-person + third-person montage for the interaction. */
	void PlayInteractionMontage(FGameplayTag InteractionTag, bool bStart);

	TWeakObjectPtr<AOneBrahamCharacter> CachedCharacter;

	float HoldStartTime = -1.f;
	float RequiredHoldSeconds = 0.f;

	/** Server clock time of the last time we re-validated the interaction. */
	float LastValidationTime = 0.f;

	/** The tag captured at interaction start, so OnInteractionEnd knows what ended. */
	FGameplayTag ActiveInteractionTag;

	/** Tracks whether the server has already told the target the interaction began. */
	bool bTargetNotified = false;
};
