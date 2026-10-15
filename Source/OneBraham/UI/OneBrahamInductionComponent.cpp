// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamInductionComponent.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamSaveSettings.h"
#include "UI/OneBrahamHUDComponent.h"
#include "UI/OneBrahamUIStyle.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"

namespace
{
	/** The gate line on the ground floor, world Y (build_environment.GATE_LINE_Y). */
	constexpr float OBInductionGateLineY = -1150.f;

	/** How long each prompt stays up before it is shown again. */
	constexpr float OBInductionPromptSeconds = 6.f;
}

UOneBrahamInductionComponent::UOneBrahamInductionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UOneBrahamInductionComponent::StartIfFirstTime()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC == nullptr || !PC->IsLocalController())
	{
		return;
	}
	if (const UOneBrahamGameInstance* GI = PC->GetGameInstance<UOneBrahamGameInstance>())
	{
		if (const UOneBrahamSaveSettings* Profile = GI->GetProfile())
		{
			if (Profile->bHasSeenTutorial)
			{
				return;
			}
		}
	}
	Induction = FOneBrahamInduction();
	Measured = FOneBrahamInductionSnapshot();
	bHaveLast = false;
	bRunning = true;
	// The first prompt waits for a character: the menu map has none.
	PromptRefresh = OBInductionPromptSeconds;
	SetComponentTickEnabled(true);
}

void UOneBrahamInductionComponent::Skip()
{
	if (bRunning)
	{
		Finish();
	}
}

void UOneBrahamInductionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bRunning)
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const AOneBrahamCharacter* Character = PC != nullptr ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
	if (Character == nullptr)
	{
		return;
	}

	// Measure.
	const FVector Location = Character->GetActorLocation();
	const float Yaw = PC->GetControlRotation().Yaw;
	if (bHaveLast)
	{
		Measured.DistanceWalkedCm += FVector::Dist2D(Location, LastLocation);
		Measured.YawTurnedDeg += FMath::Abs(FMath::FindDeltaAngleDegrees(LastYaw, Yaw));
	}
	LastLocation = Location;
	LastYaw = Yaw;
	bHaveLast = true;

	Measured.bSprinting = Character->IsSprintRequested() && Character->GetVelocity().Size2D() > 450.f;
	Measured.bCrouched = Character->bIsCrouched;
	Measured.Floor = Character->GetCurrentFloorIndex();
	Measured.bPastGates = Measured.Floor == 0 && Location.Y > OBInductionGateLineY + 60.f;
	if (const UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent())
	{
		Measured.bHasKeycard = Inventory->HasKeycardForTier(EOneBrahamAccessTier::Staff);
	}
	if (const UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
	{
		Measured.bReloading = Weapons->IsReloading();
		if (const AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon())
		{
			Measured.ShotsFired = Weapon->GetShotCounter();
		}
	}

	// Advance.
	if (Induction.Tick(Measured, DeltaTime))
	{
		PromptRefresh = 0.f;
		ShowPrompt();
		if (Induction.IsDone())
		{
			Finish();
		}
		return;
	}

	PromptRefresh += DeltaTime;
	if (PromptRefresh >= OBInductionPromptSeconds)
	{
		PromptRefresh = 0.f;
		ShowPrompt();
	}
}

void UOneBrahamInductionComponent::ShowPrompt()
{
	if (const AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(GetOwner()))
	{
		if (UOneBrahamHUDComponent* HUD = PC->GetHUDComponent())
		{
			HUD->ShowSubBanner(Induction.GetPrompt(), OBInductionPromptSeconds + 0.5f);
		}
	}
}

void UOneBrahamInductionComponent::Finish()
{
	bRunning = false;
	SetComponentTickEnabled(false);
	if (const APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		if (UOneBrahamGameInstance* GI = PC->GetGameInstance<UOneBrahamGameInstance>())
		{
			if (UOneBrahamSaveSettings* Profile = GI->GetProfile())
			{
				Profile->bHasSeenTutorial = true;
				GI->SaveProfile();
			}
		}
	}
}
