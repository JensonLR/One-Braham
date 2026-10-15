// Copyright (c) One Braham. All Rights Reserved.

#include "Interaction/OneBrahamInteractComponent.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UOneBrahamInteractComponent::UOneBrahamInteractComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 10 Hz: hold progress for a 12-second terminal override is smooth enough at
	// 10 Hz for the UI ring, and it keeps the server tick budget free.
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UOneBrahamInteractComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamInteractComponent, CurrentTarget);
	DOREPLIFETIME(UOneBrahamInteractComponent, HoldProgress);
	DOREPLIFETIME(UOneBrahamInteractComponent, bIsHoldInteraction);
}

void UOneBrahamInteractComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<AOneBrahamCharacter>(GetOwner());
}

// ==========================================================================
//  Client -> server
// ==========================================================================
void UOneBrahamInteractComponent::BeginInteraction(AActor* Target, FGameplayTag InteractionTag, bool bIsHold)
{
	if (Target == nullptr)
	{
		return;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		// Local prediction: show the hold ring immediately so the UI feels instant.
		CurrentTarget = Target;
		bIsHoldInteraction = bIsHold;
		HoldProgress = 0.f;
		OnHoldProgressChanged.Broadcast(0.f);

		ServerBeginInteraction(Target, InteractionTag, bIsHold);
		return;
	}

	// Server path (listen host, or a bot calling directly).
	ServerBeginInteraction_Implementation(Target, InteractionTag, bIsHold);
}

void UOneBrahamInteractComponent::EndInteraction()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerEndInteraction(HoldProgress >= 1.f);
		// Client-side reset: the server's replicated values will confirm.
		CurrentTarget = nullptr;
		HoldProgress = 0.f;
		OnHoldProgressChanged.Broadcast(0.f);
		return;
	}

	ServerEndInteraction_Implementation(HoldProgress >= 1.f);
}

void UOneBrahamInteractComponent::CancelInteraction()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerEndInteraction(false);
		CurrentTarget = nullptr;
		HoldProgress = 0.f;
		OnHoldProgressChanged.Broadcast(0.f);
		return;
	}

	if (CurrentTarget != nullptr)
	{
		if (IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(CurrentTarget))
		{
			Interface->OnInteractionCancelled_Implementation(Cast<AOneBrahamPlayerController>(GetOwner()->GetInstigatorController()));
		}
	}

	CurrentTarget = nullptr;
	HoldProgress = 0.f;
	bIsHoldInteraction = false;
	HoldStartTime = -1.f;
	bTargetNotified = false;
	OnHoldProgressChanged.Broadcast(0.f);
}

// ==========================================================================
//  Server implementation
// ==========================================================================
void UOneBrahamInteractComponent::ServerBeginInteraction_Implementation(AActor* Target, FGameplayTag InteractionTag, bool bIsHold)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority() || Target == nullptr)
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController());
	if (PC == nullptr || !PC->CheckRPCBudget(TEXT("ServerBeginInteraction")))
	{
		return;
	}

	// --- Validation 1: are we allowed to interact at all? -----------------
	if (UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		if (!Health->IsAlive())
		{
			return;
		}
	}

	// Downed players may only be the target of an interaction, never the
	// initiator: no crawling through doors to escape. This is a second check, not
	// a duplicate of the one above it: a downed player is at 1 HP, so `IsAlive()`
	// is true for them, and the client that presses E is not the authority on what
	// a body on the carpet can do.
	if (const UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
	{
		if (Downed->IsDowned())
		{
			return;
		}
	}

	// --- Validation 2: distance (server-side, using server positions) -----
	const float Distance = FVector::Dist(Character->GetEyeLocation(), Target->GetActorLocation());
	const float MaxTolerance = UOneBrahamGameSettings::Get().MaxInteractDistanceTolerance;

	float AllowedRange = 220.f;
	if (const IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(Target))
	{
		AllowedRange = Interface->GetInteractionRange_Implementation(PC);
	}

	if (Distance > AllowedRange + MaxTolerance)
	{
		PC->ReportViolation(FString::Printf(TEXT("Interaction out of range: %.0f cm (allowed %.0f) with %s"),
			Distance, AllowedRange, *Target->GetName()), 2.f);
		return;
	}

	// --- Validation 3: the interface agrees -------------------------------
	IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(Target);
	if (Interface == nullptr)
	{
		PC->ReportViolation(FString::Printf(TEXT("Interaction with non-interactable '%s'"), *Target->GetName()), 1.f);
		return;
	}

	if (!Interface->CanInteract_Implementation(PC))
	{
		// Legitimate failure (locked door without a badge): no violation, just a
		// negative response so the HUD can shake the prompt.
		OnPromptChanged.Broadcast(Interface->GetInteractionPrompt_Implementation(PC));
		return;
	}

	// --- Proceed ----------------------------------------------------------
	if (CurrentTarget != nullptr && CurrentTarget != Target)
	{
		CancelInteraction();
	}

	// Tag, hold flag and hold duration all come from the interactable on the
	// server. The client's InteractionTag and bIsHold are only its prediction for
	// the HUD ring: trusting bIsHold=false here turned a 12-second terminal
	// override into an instant one, and a client-picked tag could make a door
	// play the execution montage.
	CurrentTarget = Target;
	ActiveInteractionTag = Interface->GetInteractionTag_Implementation(PC);
	bIsHoldInteraction = Interface->RequiresHold_Implementation(PC);
	RequiredHoldSeconds = bIsHoldInteraction ? FMath::Max(0.f, Interface->GetHoldDuration_Implementation(PC)) : 0.f;
	HoldStartTime = GetWorld()->GetTimeSeconds();
	HoldProgress = 0.f;

	bTargetNotified = Interface->OnInteractionStart_Implementation(PC);

	if (!bTargetNotified)
	{
		// The target refused at start (e.g. a lift that is already moving).
		CurrentTarget = nullptr;
		HoldStartTime = -1.f;
		return;
	}

	PlayInteractionMontage(ActiveInteractionTag, /*bStart*/ true);
	OnHoldProgressChanged.Broadcast(0.f);

	// A zero-duration "hold" is really a tap: complete immediately.
	if (bIsHoldInteraction && RequiredHoldSeconds <= 0.f)
	{
		HoldProgress = 1.f;
		Interface->OnInteractionEnd_Implementation(PC, /*bCompleted*/ true);
		CurrentTarget = nullptr;
		bTargetNotified = false;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
}

void UOneBrahamInteractComponent::ServerEndInteraction_Implementation(bool bCompleted)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority())
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController());

	if (CurrentTarget == nullptr)
	{
		return;
	}

	IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(CurrentTarget);
	if (Interface != nullptr && bTargetNotified)
	{
		// For a tap interaction, "end of press" IS the completion. For a hold, the
		// client's bCompleted is only a request: the hold completes on release only
		// if the server's own clock says it was held long enough. The tolerance
		// covers the 10 Hz tick not having caught up before the release arrived.
		bool bActuallyCompleted = false;
		if (bIsHoldInteraction)
		{
			const float HeldSeconds = HoldStartTime >= 0.f ? GetWorld()->GetTimeSeconds() - HoldStartTime : 0.f;
			bActuallyCompleted = bCompleted
				&& HasHeldLongEnough(HeldSeconds, RequiredHoldSeconds)
				&& ValidateInteractionStillLegal()
				&& Interface->CanInteract_Implementation(PC);
		}
		else
		{
			bActuallyCompleted = Interface->CanInteract_Implementation(PC);
		}

		if (!bIsHoldInteraction && bActuallyCompleted && !ValidateInteractionStillLegal())
		{
			// Tap interactions must still be legal at the moment of release.
			Interface->OnInteractionCancelled_Implementation(PC);
		}
		else
		{
			Interface->OnInteractionEnd_Implementation(PC, bActuallyCompleted);
		}
	}

	PlayInteractionMontage(ActiveInteractionTag, /*bStart*/ false);

	CurrentTarget = nullptr;
	ActiveInteractionTag = FGameplayTag();
	HoldProgress = 0.f;
	bIsHoldInteraction = false;
	HoldStartTime = -1.f;
	bTargetNotified = false;
	RequiredHoldSeconds = 0.f;

	OnHoldProgressChanged.Broadcast(0.f);
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
}

// ==========================================================================
//  Tick: server-side hold progress
// ==========================================================================
void UOneBrahamInteractComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority() || CurrentTarget == nullptr || !bIsHoldInteraction)
	{
		return;
	}

	TickHoldProgress(DeltaTime);
}

void UOneBrahamInteractComponent::TickHoldProgress(float DeltaTime)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || HoldStartTime < 0.f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController());

	// Re-validate every 0.4 s: the player may have walked away, been shot, or the
	// target may have been destroyed by someone else's breaching charge.
	if (World->GetTimeSeconds() - LastValidationTime > 0.4f)
	{
		LastValidationTime = World->GetTimeSeconds();

		const bool bStillLegal = ValidateInteractionStillLegal() &&
			Cast<IOneBrahamInteractionInterface>(CurrentTarget) != nullptr &&
			Cast<IOneBrahamInteractionInterface>(CurrentTarget)->CanInteract_Implementation(PC);

		if (!bStillLegal)
		{
			CancelInteraction();
			return;
		}
	}

	const float HeldSeconds = World->GetTimeSeconds() - HoldStartTime;
	HoldProgress = RequiredHoldSeconds > 0.f ? FMath::Clamp(HeldSeconds / RequiredHoldSeconds, 0.f, 1.f) : 1.f;

	IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(CurrentTarget);
	if (Interface != nullptr)
	{
		Interface->OnInteractionHold_Implementation(PC, HeldSeconds, HoldProgress);
	}

	OnHoldProgressChanged.Broadcast(HoldProgress);

	if (HoldProgress >= 1.f)
	{
		// Complete: the server decides, and only the server.
		if (Interface != nullptr)
		{
			Interface->OnInteractionEnd_Implementation(PC, /*bCompleted*/ true);
		}

		PlayInteractionMontage(ActiveInteractionTag, false);

		CurrentTarget = nullptr;
		ActiveInteractionTag = FGameplayTag();
		HoldProgress = 0.f;
		bIsHoldInteraction = false;
		HoldStartTime = -1.f;
		bTargetNotified = false;
		RequiredHoldSeconds = 0.f;

		OnHoldProgressChanged.Broadcast(0.f);
		if (AActor* OwnerActor = GetOwner())
		{
			OwnerActor->ForceNetUpdate();
		}
	}
}

// ==========================================================================
//  Validation
// ==========================================================================
bool UOneBrahamInteractComponent::HasHeldLongEnough(float HeldSeconds, float RequiredSeconds)
{
	if (RequiredSeconds <= 0.f)
	{
		return true;
	}
	return HeldSeconds >= RequiredSeconds - HoldCompletionToleranceSeconds;
}

bool UOneBrahamInteractComponent::ValidateInteractionStillLegal() const
{
	const AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || CurrentTarget == nullptr)
	{
		return false;
	}

	// Alive and standing, the same rule ServerBeginInteraction applies at the
	// start: otherwise a client that keeps E held after being downed (or a tap
	// released by the death path) still finishes the override.
	if (const UOneBrahamHealthComponent* Health = Character->GetHealthComponent())
	{
		if (!Health->IsAlive())
		{
			return false;
		}
	}
	if (const UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
	{
		if (Downed->IsDowned())
		{
			return false;
		}
	}

	// Distance
	const float Distance = FVector::Dist(Character->GetEyeLocation(), CurrentTarget->GetActorLocation());
	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController());

	float AllowedRange = 220.f;
	if (const IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(CurrentTarget))
	{
		AllowedRange = Interface->GetInteractionRange_Implementation(PC);
	}

	if (Distance > AllowedRange + UOneBrahamGameSettings::Get().MaxInteractDistanceTolerance)
	{
		return false;
	}

	// Facing: the player must be looking at the target (dot > 0.35, i.e. within
	// ~70 degrees). This is what stops "spam E while spinning" exploits and also
	// simply feels right.
	const FVector ToTarget = (CurrentTarget->GetActorLocation() - Character->GetEyeLocation()).GetSafeNormal();
	const float Facing = FVector::DotProduct(Character->GetEyeRotation().Vector(), ToTarget);

	return Facing > 0.35f;
}

bool UOneBrahamInteractComponent::ServerPerformInstantInteraction(AActor* Target)
{
	if (Target == nullptr || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetOwner()->GetInstigatorController());

	if (IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(Target))
	{
		if (!Interface->CanInteract_Implementation(PC))
		{
			return false;
		}

		if (!Interface->OnInteractionStart_Implementation(PC))
		{
			return false;
		}

		Interface->OnInteractionEnd_Implementation(PC, /*bCompleted*/ true);
		return true;
	}

	return false;
}

// ==========================================================================
//  Animation
// ==========================================================================
void UOneBrahamInteractComponent::PlayInteractionMontage(FGameplayTag InteractionTag, bool bStart)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	// Eight montages, one per interaction family, loaded by name from
	// /Game/OneBraham/Characters/Shared/Anims. They are played on the upper body
	// so the player keeps their lower-body locomotion, and the same montage is
	// played on the first-person arms mesh so the hands visibly reach for the
	// button.
	//
	// THE CONTRACT (also written down in Docs/13, table 4)
	//   Door       -> AM_OB_Interact_Door
	//   Terminal   -> AM_OB_Interact_Terminal
	//   Lift       -> AM_OB_Interact_LiftPanel
	//   Revive     -> AM_OB_Interact_Revive
	//   Execute    -> AM_OB_Interact_Execute
	//   Keycard    -> AM_OB_Interact_Keycard
	//   Body       -> AM_OB_Interact_Loot
	//   Extraction -> AM_OB_Interact_Radio
	//
	// The fallback is Door, and that is a trap: a tag whose family is missing here
	// silently plays the door animation, which is how the execution - the single
	// most deliberate thing a player can do to another player - was going to be
	// animated as opening a fire door. check_interaction.py now compares this chain
	// against the interaction table in Docs/13, tag by tag, so a new verb cannot
	// inherit the wrong animation in silence.
	//
	// A missing montage is not an error: the interaction still happens, because
	// the animation is the presentation and the state change is the gameplay.
	// The content build creates the folder; the animation team fills it.
	const FString TagString = InteractionTag.IsValid() ? InteractionTag.ToString() : TEXT("OB.Interaction.Door");

	FString MontageName;
	if (TagString.Contains(TEXT("Terminal")))      { MontageName = TEXT("AM_OB_Interact_Terminal"); }
	else if (TagString.Contains(TEXT("Lift")))     { MontageName = TEXT("AM_OB_Interact_LiftPanel"); }
	else if (TagString.Contains(TEXT("Revive")))   { MontageName = TEXT("AM_OB_Interact_Revive"); }
	else if (TagString.Contains(TEXT("Execute")))  { MontageName = TEXT("AM_OB_Interact_Execute"); }
	else if (TagString.Contains(TEXT("Keycard")))  { MontageName = TEXT("AM_OB_Interact_Keycard"); }
	else if (TagString.Contains(TEXT("Body")))     { MontageName = TEXT("AM_OB_Interact_Loot"); }
	else if (TagString.Contains(TEXT("Extraction"))) { MontageName = TEXT("AM_OB_Interact_Radio"); }
	else                                           { MontageName = TEXT("AM_OB_Interact_Door"); }

	const FString FullPath = FString::Printf(TEXT("/Game/OneBraham/Characters/Shared/Anims/%s.%s"), *MontageName, *MontageName);
	if (UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *FullPath))
	{
		if (USkeletalMeshComponent* Body = Character->GetMesh())
		{
			if (UAnimInstance* AnimInstance = Body->GetAnimInstance())
			{
				if (bStart)
				{
					AnimInstance->Montage_Play(Montage, 1.f, EMontagePlayReturnType::Duration, 0.f, /*bStopAllMontages*/ false);
				}
				else
				{
					// Blend out over 0.25 s rather than snapping back.
					AnimInstance->Montage_Stop(0.25f, Montage);
				}
			}
		}

		// The first-person arms play the same montage on the owner-only mesh so
		// the hands visibly reach for the button.
		if (USkeletalMeshComponent* Arms = Character->GetFirstPersonMesh())
		{
			if (UAnimInstance* ArmsAnimInstance = Arms->GetAnimInstance())
			{
				if (bStart)
				{
					ArmsAnimInstance->Montage_Play(Montage, 1.f, EMontagePlayReturnType::Duration, 0.f, false);
				}
				else
				{
					ArmsAnimInstance->Montage_Stop(0.25f, Montage);
				}
			}
		}
	}
}
