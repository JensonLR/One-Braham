// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamConsole.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Characters/OneBrahamCharacter.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UOneBrahamConsole::UOneBrahamConsole()
{
}

void UOneBrahamConsole::PostRender_Console_GameViewport(UCanvas* Canvas)
{
	// Shipping builds keep the console available for the ops team on dedicated
	// servers, but clients only see it when the player asked for it with ~ and
	// the build allows cheats. This is enforced by the engine's own
	// bAllowCheatsInShipping flag; we only add ONE BRAHAM branding.
#if !UE_BUILD_SHIPPING
	if (Canvas != nullptr)
	{
		// Deliberately empty: the engine draws the console body. The override
		// exists so the ONE BRAHAM HUD can draw QA telemetry under the console
		// (see UOneBrahamHUDComponent::DrawDebugTelemetry) and so the function
		// is a documented hook rather than a hidden engine default.
	}
#endif
	Super::PostRender_Console_GameViewport(Canvas);
}

bool UOneBrahamConsole::ConsoleCommand(const FString& Command)
{
	// Route ONE BRAHAM QA commands, then fall through to the engine console.
	const FString Trimmed = Command.TrimStartAndEnd();
	FString Verb;
	FString Args;
	if (Trimmed.Split(TEXT(" "), &Verb, &Args))
	{
		Verb = Verb.ToLower();

		TArray<FString> ArgList;
		Args.ParseIntoArrayWS(ArgList);

		if (Verb == TEXT("ob.give"))
		{
			OBGiveWeapon(ArgList.Num() > 0 ? ArgList[0] : TEXT("Carbine_L85A2"));
			return true;
		}
		if (Verb == TEXT("ob.lockdown"))
		{
			OBSetLockdownStage(ArgList.Num() > 0 ? FCString::Atoi(*ArgList[0]) : -1);
			return true;
		}
		if (Verb == TEXT("ob.endmatch"))
		{
			OBEndMatch(ArgList.Num() > 0 ? FCString::Atoi(*ArgList[0]) : -1);
			return true;
		}
		if (Verb == TEXT("ob.netbudget"))
		{
			OBReportNetBudget();
			return true;
		}
	}

	return Super::ConsoleCommand(Command);
}

void UOneBrahamConsole::OBGiveWeapon(const FString& WeaponId)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AOneBrahamCharacter* Character = PC ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
	if (Character == nullptr || Character->GetWeaponComponent() == nullptr)
	{
		OB_WARN("ob.give: no local character with a weapon component.");
		return;
	}

	Character->GetWeaponComponent()->ServerGiveWeaponDebug(FName(*WeaponId));
	OB_LOG("ob.give: requested '%s'.", *WeaponId);
}

void UOneBrahamConsole::OBSetLockdownStage(int32 Stage)
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr || GS->GetLockdownComponent() == nullptr)
	{
		OB_WARN("ob.lockdown: no One Braham game state.");
		return;
	}

	GS->GetLockdownComponent()->ServerForceStage(static_cast<EOneBrahamLockdownStage>(FMath::Clamp(Stage, 0, 3)));
	OB_LOG("ob.lockdown: forced stage %d.", Stage);
}

void UOneBrahamConsole::OBEndMatch(int32 WinningTeamIndex)
{
	UWorld* World = GetWorld();
	AOneBrahamGameMode* GM = World ? World->GetAuthGameMode<AOneBrahamGameMode>() : nullptr;
	if (GM == nullptr)
	{
		OB_WARN("ob.endmatch: only the server can end a match.");
		return;
	}

	GM->DebugForceMatchResult(WinningTeamIndex);
	OB_LOG("ob.endmatch: forced result for team index %d.", WinningTeamIndex);
}

void UOneBrahamConsole::OBReportNetBudget()
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	OB_LOG("=== ONE BRAHAM NET BUDGET ===");
	GS->LogNetBudget();
}
