// Copyright (c) One Braham. All Rights Reserved.

#include "Access/OneBrahamAccessZoneComponent.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "Weapons/OneBrahamInventoryComponent.h"

namespace
{
	/** Feedback cadence for the override hum/progress bar. 10 Hz is smooth enough. */
	constexpr float OBOverrideFeedbackInterval = 0.1f;

	/** The override progress bleeds off when nobody is holding the panel. */
	constexpr float OBOverrideDecayPerSecond = 0.35f;
}

UOneBrahamAccessZoneComponent::UOneBrahamAccessZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UOneBrahamAccessZoneComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bUnlockedAtStart && GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		ApplyUnlockState(true, EOneBrahamUnlockMethod::None, INDEX_NONE);
	}

	OnRep_Unlocked();

	if (ZoneId.IsNone())
	{
		OB_WARN("Access zone on '%s' has no ZoneId; it cannot be referenced by the lockdown director or the map spec.",
			GetOwner() != nullptr ? *GetOwner()->GetName() : TEXT("<no owner>"));
	}
}

void UOneBrahamAccessZoneComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamAccessZoneComponent, bUnlocked);
	DOREPLIFETIME(UOneBrahamAccessZoneComponent, LastUnlockMethod);
	DOREPLIFETIME(UOneBrahamAccessZoneComponent, UnlockedByPlayerId);
}

void UOneBrahamAccessZoneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// An override that is no longer being held bleeds off slowly. Not instantly:
	// being shot off a terminal should hurt, but a player who stands back up
	// within a second should not lose all of their progress to a server tick.
	if (!ActiveOverrider.IsValid() && OverrideProgress > 0.f)
	{
		OverrideProgress = FMath::Max(0.f, OverrideProgress - (DeltaTime * OBOverrideDecayPerSecond));

		FeedbackAccumulator += DeltaTime;
		if (FeedbackAccumulator >= OBOverrideFeedbackInterval)
		{
			FeedbackAccumulator = 0.f;
			MulticastOverrideFeedback(OverrideProgress);
		}

		if (OverrideProgress <= 0.f)
		{
			OnOverrideProgress.Broadcast(ZoneId, 0.f);
		}
	}
}

// ===========================================================================
//  Unlocking
// ===========================================================================

bool UOneBrahamAccessZoneComponent::CanUnlockWithTier(EOneBrahamAccessTier HeldTier) const
{
	if (bUnlocked)
	{
		return true;
	}

	// An Override card does not exist: only a held terminal opens an Override zone.
	if (RequiredTier == EOneBrahamAccessTier::Override)
	{
		return false;
	}

	return static_cast<uint8>(HeldTier) >= static_cast<uint8>(RequiredTier);
}

bool UOneBrahamAccessZoneComponent::ServerUnlock(EOneBrahamUnlockMethod Method, int32 UnlockerPlayerId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || bUnlocked)
	{
		return false;
	}

	ApplyUnlockState(true, Method, UnlockerPlayerId);
	MulticastZoneUnlocked(Method, UnlockerPlayerId);

	return true;
}

void UOneBrahamAccessZoneComponent::ServerLock()
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || !bUnlocked)
	{
		return;
	}

	ApplyUnlockState(false, EOneBrahamUnlockMethod::None, INDEX_NONE);
	OverrideProgress = 0.f;
	ActiveOverrider.Reset();
	FeedbackAccumulator = 0.f;
}

void UOneBrahamAccessZoneComponent::ApplyUnlockState(bool bNewUnlocked, EOneBrahamUnlockMethod Method, int32 UnlockerPlayerId)
{
	bUnlocked = bNewUnlocked;
	LastUnlockMethod = Method;
	UnlockedByPlayerId = UnlockerPlayerId;

	OnRep_Unlocked();
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	OB_ACCESS_LOG("Zone '%s' (floor %d, tier %s) is now %s via %s (player %d)",
		*ZoneId.ToString(), FloorIndex,
		*UEnum::GetValueAsString(RequiredTier),
		bNewUnlocked ? TEXT("OPEN") : TEXT("SHUT"),
		*UEnum::GetValueAsString(Method),
		UnlockerPlayerId);
}

void UOneBrahamAccessZoneComponent::OnRep_Unlocked()
{
	ApplyLocalZoneEffects(bUnlocked, LastUnlockMethod);

	if (OnZoneChanged.IsBound())
	{
		OnZoneChanged.Broadcast(ZoneId, bUnlocked, LastUnlockMethod);
	}
}

void UOneBrahamAccessZoneComponent::ApplyLocalZoneEffects(bool bNewUnlocked, EOneBrahamUnlockMethod Method)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	AOneBrahamPlayerController* PC = World->GetFirstPlayerController<AOneBrahamPlayerController>();
	if (PC == nullptr)
	{
		return;
	}

	UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager();
	if (Audio == nullptr)
	{
		return;
	}

	// One sound per method, because the method is information: a keycard click
	// means somebody had the right badge, a terminal accept means they earned it,
	// and a breach means the floor has about four seconds to react.
	if (!bNewUnlocked)
	{
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Door_Locked")), GetComponentLocation(), 1.f, 1.f, GetOwner());
		return;
	}

	switch (Method)
	{
	case EOneBrahamUnlockMethod::Keycard:
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Keycard_Use")), GetComponentLocation(), 1.f, 1.f, GetOwner());
		break;

	case EOneBrahamUnlockMethod::Override:
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Terminal_Accept")), GetComponentLocation(), 1.f, 1.f, GetOwner());
		break;

	case EOneBrahamUnlockMethod::Breach:
		// The charge already made its own noise; the door is the follow-up.
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Door_Open")), GetComponentLocation(), 1.f, 0.92f, GetOwner());
		break;

	case EOneBrahamUnlockMethod::Escalation:
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Shutter_Drop")), GetComponentLocation(), 1.f, 1.f, GetOwner());
		break;

	default:
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Door_Open")), GetComponentLocation(), 0.8f, 1.f, GetOwner());
		break;
	}
}

// ===========================================================================
//  Terminal override
// ===========================================================================

float UOneBrahamAccessZoneComponent::ServerHoldOverride(AOneBrahamPlayerController* Interactor, bool bPairedPanelHeld, float DeltaTime)
{
	if (Interactor == nullptr || bUnlocked || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return OverrideProgress;
	}

	// One person on the panel at a time. A second player pressing E on the same
	// panel gets nothing: the map's design is two people on two panels, and
	// allowing a stack would let four players open a floor in three seconds.
	if (ActiveOverrider.IsValid() && ActiveOverrider.Get() != Interactor)
	{
		return OverrideProgress;
	}

	ActiveOverrider = Interactor;

	const float Required = bPairedPanelHeld ? OverrideTwoPersonSeconds : OverrideSeconds;
	const float Rate = 1.f / FMath::Max(0.5f, Required);

	OverrideProgress = FMath::Clamp(OverrideProgress + Rate * DeltaTime, 0.f, 1.f);

	FeedbackAccumulator += DeltaTime;
	if (FeedbackAccumulator >= OBOverrideFeedbackInterval)
	{
		FeedbackAccumulator = 0.f;
		MulticastOverrideFeedback(OverrideProgress);
	}

	if (OverrideProgress >= 1.f)
	{
		const AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
		const int32 PlayerId = PS != nullptr ? PS->GetPlayerId() : INDEX_NONE;

		ServerUnlock(EOneBrahamUnlockMethod::Override, PlayerId);

		OB_ACCESS_LOG("Zone '%s' opened by terminal override (%s, player %d)",
			*ZoneId.ToString(), bPairedPanelHeld ? TEXT("two-panel") : TEXT("single-panel"), PlayerId);
	}

	return OverrideProgress;
}

void UOneBrahamAccessZoneComponent::ServerCancelOverride(AOneBrahamPlayerController* Interactor)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (Interactor == nullptr || ActiveOverrider.Get() == Interactor)
	{
		ActiveOverrider.Reset();
	}
}

void UOneBrahamAccessZoneComponent::MulticastOverrideFeedback_Implementation(float Progress01)
{
	OnOverrideProgress.Broadcast(ZoneId, Progress01);

	if (OverrideProgress != Progress01)
	{
		OverrideProgress = Progress01;
	}
}

void UOneBrahamAccessZoneComponent::MulticastZoneUnlocked_Implementation(EOneBrahamUnlockMethod Method, int32 UnlockerPlayerId)
{
	// On the server, ApplyUnlockState already ran. On clients, this is the moment
	// the door, the light, and the sign all change.
	ApplyLocalZoneEffects(true, Method);

	UE_LOG(LogOneBrahamAccess, Verbose, TEXT("[OB.ACCESS] Multicast: zone '%s' opened via %s by player %d"),
		*ZoneId.ToString(), *UEnum::GetValueAsString(Method), UnlockerPlayerId);
}

// ===========================================================================
//  Prompts
// ===========================================================================

FText UOneBrahamAccessZoneComponent::GetInteractionPrompt(const AOneBrahamPlayerController* Interactor) const
{
	const FText ZoneName = ZoneDisplayName.IsEmpty()
		? FText::FromName(ZoneId)
		: ZoneDisplayName;

	if (bUnlocked)
	{
		return FText::Format(NSLOCTEXT("OneBraham", "Zone_Open", "Open {0}"), ZoneName);
	}

	if (RequiredTier == EOneBrahamAccessTier::Override)
	{
		return FText::Format(NSLOCTEXT("OneBraham", "Zone_OverrideOnly", "Hold E - override {0}"), ZoneName);
	}

	// Can they just use it, or are they about to be told no?
	bool bHasBadge = false;
	if (Interactor != nullptr)
	{
		if (const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Interactor->GetPawn()))
		{
			if (const UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent())
			{
				bHasBadge = Inventory->HasKeycardForTier(RequiredTier);
			}
		}
	}

	if (bHasBadge)
	{
		return FText::Format(NSLOCTEXT("OneBraham", "Zone_Card", "Use badge - {0}"), ZoneName);
	}

	return FText::Format(NSLOCTEXT("OneBraham", "Zone_OverrideNeeded", "Hold E - override {0}"), ZoneName);
}

float UOneBrahamAccessZoneComponent::GetOverrideSecondsFor(const AOneBrahamPlayerController* Interactor) const
{
	if (Interactor == nullptr || PairedZoneId.IsNone())
	{
		return OverrideSeconds;
	}

	// The paired panel is only fast if somebody is actually standing at it. The
	// check is done by the partner's own interaction component calling this zone
	// with bPairedPanelHeld = true, so here we only report the best case.
	return OverrideTwoPersonSeconds;
}

FText UOneBrahamAccessZoneComponent::GetRefusalText(const AOneBrahamPlayerController* Interactor) const
{
	if (bUnlocked)
	{
		return FText::GetEmpty();
	}

	if (RequiredTier == EOneBrahamAccessTier::Override)
	{
		return NSLOCTEXT("OneBraham", "Zone_Refusal_Override", "No reader. Terminal override required.");
	}

	switch (RequiredTier)
	{
	case EOneBrahamAccessTier::Staff:
		return NSLOCTEXT("OneBraham", "Zone_Refusal_Staff", "Staff badge required.");
	case EOneBrahamAccessTier::Secure:
		return NSLOCTEXT("OneBraham", "Zone_Refusal_Secure", "IT Operations badge required.");
	case EOneBrahamAccessTier::Executive:
		return NSLOCTEXT("OneBraham", "Zone_Refusal_Executive", "Executive badge required.");
	case EOneBrahamAccessTier::Skydeck:
		return NSLOCTEXT("OneBraham", "Zone_Refusal_Skydeck", "Facilities master badge required.");
	default:
		return NSLOCTEXT("OneBraham", "Zone_Refusal_Generic", "Access denied.");
	}
}
