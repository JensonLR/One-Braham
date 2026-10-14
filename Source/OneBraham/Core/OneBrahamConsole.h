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

	/** QA helpers, routed by ConsoleCommand and named for the command each one
	 *  answers, so the table here and the table in Docs/18 section 6 are the same
	 *  list. `ob.NetDebug` is not here: it is a console variable, handled by the
	 *  engine's own console before anything reaches this class. */
	void OBGiveWeapon(const FString& WeaponId);
	void OBRemoveCurrentWeapon();
	void OBSetLockdownStage(int32 Stage);
	void OBEndMatch(int32 WinningTeamIndex);
	void OBExtractionFast();
	void OBExtractionAbort();
	void OBReportNetBudget();
	void OBDumpAudio();
	void OBBenchSkydeck(float SecondsPerLeg);
	void OBStopBench();
};
