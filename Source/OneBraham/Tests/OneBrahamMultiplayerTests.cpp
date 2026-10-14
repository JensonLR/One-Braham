// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Multiplayer.*
//
// Three things can be true about a networked game and invisible until it is on a
// server: an RPC that is unreliable when it must not be, a replicated property
// whose OnRep never fires, and a class the config names but the code no longer
// has. All three are decidable without a world, which is why they are decided
// here rather than in the manual test plan.
//
// The replicate-and-notify contract is the one worth being pedantic about: a
// property declared ReplicatedUsing a function that does not exist fails to
// compile, but a property declared ReplicatedUsing a function on the wrong class
// (or with the wrong spelling) compiles and then silently never notifies - the
// value arrives, the HUD does not update, and the bug looks like lag.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"

#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** Every class that lives in this project's game module. */
	void GatherGameClasses(TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;

			if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}

			const UPackage* Package = Class->GetOutermost();

			if (Package != nullptr && Package->GetName() == TEXT("/Script/OneBraham"))
			{
				OutClasses.Add(Class);
			}
		}
	}

	/** Reads one class path out of a config file, or an empty string. */
	FString ReadIniValue(const TCHAR* Section, const TCHAR* Key)
	{
		const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
		FString Ini;

		if (!FFileHelper::LoadFileToString(Ini, *IniPath))
		{
			return FString();
		}

		bool bInSection = false;

		TArray<FString> Lines;
		Ini.ParseIntoArrayLines(Lines, true);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();

			if (Trimmed.StartsWith(TEXT("[")))
			{
				bInSection = Trimmed.Equals(FString::Printf(TEXT("[%s]"), Section), ESearchCase::IgnoreCase);
				continue;
			}

			if (bInSection && Trimmed.StartsWith(FString::Printf(TEXT("%s="), Key), ESearchCase::IgnoreCase))
			{
				return Trimmed.Mid(Trimmed.Find(TEXT("=")) + 1).TrimStartAndEnd();
			}
		}

		return FString();
	}
}

// ===========================================================================
//  The replication contract
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamRpcContractTest, "OneBraham.Multiplayer.ReplicationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamRpcContractTest::RunTest(const FString& Parameters)
{
	TArray<UClass*> Classes;
	GatherGameClasses(Classes);

	TestTrue(TEXT("the game module has classes to audit"), Classes.Num() > 50);

	// The deliberate exceptions to "every client/server RPC is reliable": streams
	// sent many times a second, where a reliable copy would queue behind lost
	// packets and can overflow the reliable buffer (which disconnects the
	// client), and cosmetic feedback whose loss changes nothing.
	static const TSet<FString> IntentionallyUnreliable = {
		TEXT("OneBrahamCharacter::ServerSetLean"),				// every lean input; the next one supersedes it
		TEXT("OneBrahamCharacter::ServerReportSpeed"),			// per-tick telemetry for the anti-cheat
		TEXT("OneBrahamCharacter::ServerReportFloor"),			// per-tick telemetry, superseded next tick
		TEXT("OneBrahamProximityVoiceComponent::ServerPushVoiceFrame"),	// 20 ms audio frames
		TEXT("OneBrahamProximityVoiceComponent::ClientReceiveVoiceFrame"),	// the same frames, relayed
		TEXT("OneBrahamWeaponComponent::ClientHitConfirmed"),		// hit marker only
		TEXT("OneBrahamPlayerController::ClientDamageTaken"),		// hit direction indicator only
		TEXT("OneBrahamPlayerController::ClientApplyExplosionShake"),	// camera shake only
	};

	int32 ServerRpcs = 0;
	int32 ClientRpcs = 0;
	int32 MulticastRpcs = 0;
	int32 RepNotifies = 0;
	int32 ValidateRpcs = 0;

	for (UClass* Class : Classes)
	{
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const UFunction* Function = *It;

			if (Function == nullptr)
			{
				continue;
			}

			const bool bServer = Function->HasAnyFunctionFlags(FUNC_NetServer);
			const bool bClient = Function->HasAnyFunctionFlags(FUNC_NetClient);
			const bool bMulticast = Function->HasAnyFunctionFlags(FUNC_NetMulticast);
			const bool bReliable = Function->HasAnyFunctionFlags(FUNC_NetReliable);
			const bool bValidate = Function->HasAnyFunctionFlags(FUNC_NetValidate);

			if (bServer)
			{
				++ServerRpcs;

				// Every server RPC in this project is a state change the client is
				// asking for: a shot, a revive, an override, a squad message. Losing
				// one silently is a player pressing a button and nothing happening,
				// and the client has no way to tell that from lag.
				TestTrue(*FString::Printf(TEXT("%s::%s is reliable"), *Class->GetName(), *Function->GetName()),
					bReliable || IntentionallyUnreliable.Contains(Class->GetName() + TEXT("::") + Function->GetName()));

				// Server RPCs must be validated by hand rather than by
				// WithValidation: the project has zero of those by decision, because
				// the checks that matter here (range, state, budget) need the actor
				// and the world, not the parameter list.
				TestFalse(*FString::Printf(TEXT("%s::%s does not use WithValidation"), *Class->GetName(), *Function->GetName()),
					bValidate);
			}

			if (bClient)
			{
				++ClientRpcs;
				TestTrue(*FString::Printf(TEXT("%s::%s is reliable"), *Class->GetName(), *Function->GetName()),
					bReliable || IntentionallyUnreliable.Contains(Class->GetName() + TEXT("::") + Function->GetName()));
			}

			if (bMulticast)
			{
				++MulticastRpcs;
			}

			if (bValidate)
			{
				++ValidateRpcs;
			}
		}

		// ReplicatedUsing has to name a function that exists on the same class.
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FProperty* Property = *It;

			if (Property == nullptr || (Property->GetPropertyFlags() & CPF_RepNotify) == 0)
			{
				continue;
			}

			++RepNotifies;

			const FName RepNotifyFunc = Property->RepNotifyFunc;
			TestFalse(*FString::Printf(TEXT("%s::%s names a rep notify"), *Class->GetName(), *Property->GetName()),
				RepNotifyFunc.IsNone());

			if (!RepNotifyFunc.IsNone())
			{
				const UFunction* Notify = Class->FindFunctionByName(RepNotifyFunc);

				TestNotNull(*FString::Printf(TEXT("%s::%s -> %s exists"), *Class->GetName(),
					*Property->GetName(), *RepNotifyFunc.ToString()), Notify);
			}
		}
	}

	// The shape of the network surface, as counts rather than as prose: this
	// project is server-authoritative, so there are far more server RPCs than
	// client ones, and the client RPCs that exist are the ones that tell a player
	// something the server decided.
	TestTrue(TEXT("the game has server RPCs"), ServerRpcs > 20);
	TestTrue(TEXT("and client RPCs"), ClientRpcs > 5);
	TestTrue(TEXT("and multicast announcements"), MulticastRpcs > 0);
	TestTrue(TEXT("and replicated properties that notify"), RepNotifies > 20);

	// Zero WithValidation RPCs, by decision. If one appears, this fails and the
	// author has to either delete it or delete this rule deliberately.
	TestEqual(TEXT("no WithValidation RPCs in this project"), ValidateRpcs, 0);

	return true;
}

// ===========================================================================
//  What a match boots from
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchBootTest, "OneBraham.Multiplayer.Smoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchBootTest::RunTest(const FString& Parameters)
{
	// The smoke test that `Scripts/RunTests.bat smoke` runs on a dedicated server.
	// It is a composition check rather than a 12-player simulation: every piece a
	// match boots from - the classes the config names, the mode's composition, the
	// building's floors, the spawn budget - has to be present and consistent
	// before a match can start. A client-and-server simulation is the job of the
	// Q2 checklist in Docs/20, which drives real processes; a smoke test that
	// takes an hour is a test nobody runs while working.
	static const TCHAR* RequiredClasses[] = {
		TEXT("/Script/OneBraham.OneBrahamGameMode"),
		TEXT("/Script/OneBraham.OneBrahamGameState"),
		TEXT("/Script/OneBraham.OneBrahamPlayerState"),
		TEXT("/Script/OneBraham.OneBrahamPlayerController"),
		TEXT("/Script/OneBraham.OneBrahamGameInstance"),
		TEXT("/Script/OneBraham.OneBrahamCharacter"),
		TEXT("/Script/OneBraham.OneBrahamHUDComponent")
	};

	for (const TCHAR* Path : RequiredClasses)
	{
		UClass* Class = LoadObject<UClass>(nullptr, Path);
		TestNotNull(*FString::Printf(TEXT("%s loads"), Path), Class);
	}

	// The config names the same classes. A class that is renamed in C++ and not in
	// the ini is a game that will not start, and it is the sort of thing that only
	// shows up on a packaged build.
	{
		const FString ConfiguredGameMode = ReadIniValue(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"));
		const FString ConfiguredGameInstance = ReadIniValue(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameInstanceClass"));

		TestFalse(TEXT("DefaultEngine.ini names a game mode"), ConfiguredGameMode.IsEmpty());
		TestFalse(TEXT("DefaultEngine.ini names a game instance"), ConfiguredGameInstance.IsEmpty());

		if (!ConfiguredGameMode.IsEmpty())
		{
			UClass* GameModeClass = LoadObject<UClass>(nullptr, *ConfiguredGameMode);
			TestNotNull(*FString::Printf(TEXT("the configured game mode exists: %s"), *ConfiguredGameMode), GameModeClass);
			TestTrue(TEXT("and is a game mode"), GameModeClass != nullptr
				&& GameModeClass->IsChildOf(AGameModeBase::StaticClass()));
		}

		if (!ConfiguredGameInstance.IsEmpty())
		{
			UClass* GameInstanceClass = LoadObject<UClass>(nullptr, *ConfiguredGameInstance);
			TestNotNull(*FString::Printf(TEXT("the configured game instance exists: %s"), *ConfiguredGameInstance),
				GameInstanceClass);
		}
	}

	// The game mode's own defaults: ASCENT, and a squad map that has not been
	// populated yet (a CDO is not a running match - this asserts the default mode
	// rather than pretending to have played one).
	const AOneBrahamGameMode* GameModeDefaults = GetDefault<AOneBrahamGameMode>();
	TestNotNull(TEXT("the game mode has a CDO"), GameModeDefaults);

	if (GameModeDefaults != nullptr)
	{
		TestTrue(TEXT("the default mode is ASCENT"),
			GameModeDefaults->GetMatchMode() == EOneBrahamMatchMode::Ascent);
		TestEqual(TEXT("no squads exist before a match starts"), GameModeDefaults->GetSquadCount(), 0);
		TestTrue(TEXT("and an unknown squad has no team"),
			GameModeDefaults->GetTeamForSquad(0) == EOneBrahamTeam::None);
	}

	// The composition a 12-player match needs, from the mode's own settings.
	{
		UOneBrahamMatchSettingsAsset* Ascent = NewObject<UOneBrahamMatchSettingsAsset>(
			GetTransientPackage(), NAME_None, RF_Transient);

		if (Ascent == nullptr)
		{
			AddError(TEXT("Could not create a transient ASCENT settings asset."));
			return false;
		}

		Ascent->Mode = EOneBrahamMatchMode::Ascent;
		Ascent->EnsureDefaults();

		TestTrue(TEXT("the squad count fits the mode"),
			Ascent->TeamCount <= OneBrahamConstants::MaxSquads);
		TestTrue(TEXT("the squad size fits the mode"),
			Ascent->PlayersPerTeam <= OneBrahamConstants::SquadSize);
		TestEqual(TEXT("an ASCENT match is exactly twelve players"),
			Ascent->GetExpectedPlayerCount(), 12);
		TestTrue(TEXT("and the server has room for a full match"),
			Ascent->GetExpectedPlayerCount() <= OneBrahamConstants::MaxPlayers);
	}

	// The building: fifteen storeys plus the plaza, and the Skydeck is the top of
	// it. A match that cannot find the extraction floor cannot end.
	TestEqual(TEXT("the Skydeck is the fifteenth floor"), OneBrahamConstants::FloorSkydeck, 15);
	TestTrue(TEXT("the extraction zone is above every purged floor"),
		OneBrahamConstants::FloorSkydeck > 13);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
