// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamConsole.h
// ---------------------------------------------------------------------------
// Console shim registered via ConsoleClassName in DefaultEngine.ini.
// Provides the ob.* cheat/QA commands used throughout Docs/18 and the QA plan,
// and suppresses the default "cheat" console in shipping builds.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Console.h"
#include "OneBrahamConsole.generated.h"

UCLASS()
class ONEBRAHAM_API UOneBrahamConsole : public UConsole
{
	GENERATED_BODY()

public:
	UOneBrahamConsole();

	virtual void PostRender_Console_GameViewport(UCanvas* Canvas) override;
	virtual bool ConsoleCommand(const FString& Command) override;

	/** QA helpers, bound in the constructor as exec functions. */
	void OBGiveWeapon(const FString& WeaponId);
	void OBSetLockdownStage(int32 Stage);
	void OBEndMatch(int32 WinningTeamIndex);
	void OBReportNetBudget();
};
