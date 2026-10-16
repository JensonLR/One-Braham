// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameSettings.h"

#include "OneBraham.h"
#include "EnhancedInput/Public/InputAction.h"

UOneBrahamGameSettings::UOneBrahamGameSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("ONE BRAHAM");
}

const UOneBrahamGameSettings& UOneBrahamGameSettings::Get()
{
	const UOneBrahamGameSettings* Settings = GetDefault<UOneBrahamGameSettings>();
	check(Settings);
	return *Settings;
}

bool UOneBrahamInputConfig::ValidateAllActionsSet(TArray<FName>& OutMissing) const
{
	OutMissing.Reset();

	// Table driven so the validator stays in sync with the class by construction:
	// adding a UPROPERTY without adding a row here will be caught by the
	// OneBraham.Input.AllActionsBound automation test.
	const TMap<FName, const UInputAction*> Required = {
		{ TEXT("Move"), Move },
		{ TEXT("Look"), Look },
		{ TEXT("LookGamepad"), LookGamepad },
		{ TEXT("Jump"), Jump },
		{ TEXT("Sprint"), Sprint },
		{ TEXT("Walk"), Walk },
		{ TEXT("Crouch"), Crouch },
		{ TEXT("Lean"), Lean },
		{ TEXT("Mantle"), Mantle },
		{ TEXT("Slide"), Slide },
		{ TEXT("Fire"), Fire },
		{ TEXT("Aim"), Aim },
		{ TEXT("Reload"), Reload },
		{ TEXT("FireModeSwitch"), FireModeSwitch },
		{ TEXT("Melee"), Melee },
		{ TEXT("Inspect"), Inspect },
		{ TEXT("WeaponSlotPrimary"), WeaponSlotPrimary },
		{ TEXT("WeaponSlotSidearm"), WeaponSlotSidearm },
		{ TEXT("ThrowGrenade"), ThrowGrenade },
		{ TEXT("UseUtility"), UseUtility },
		{ TEXT("UseMedkit"), UseMedkit },
		{ TEXT("Interact"), Interact },
		{ TEXT("InteractHold"), InteractHold },
		{ TEXT("ToggleFlashlight"), ToggleFlashlight },
		{ TEXT("PushToTalk"), PushToTalk },
		{ TEXT("VoiceChannelCycle"), VoiceChannelCycle },
		{ TEXT("Scoreboard"), Scoreboard },
		{ TEXT("PauseMenu"), PauseMenu },
		{ TEXT("SpectateNextPlayer"), SpectateNextPlayer },
		{ TEXT("SpectatePrevPlayer"), SpectatePrevPlayer },
		// Eight rows were missing here for a while: DropWeapon, Ping,
		// SpectateToggleFreeCam, ToggleLaser, VoiceMuteSelf, WeaponCycleNext,
		// WeaponCyclePrev and WeaponSlotMelee. The comment above claimed the
		// automation test would catch that. It did not, because the table was the
		// only list of its kind and nothing compared it to the fields -
		// check_input_actions.py does now.
		{ TEXT("WeaponSlotMelee"), WeaponSlotMelee },
		{ TEXT("WeaponCycleNext"), WeaponCycleNext },
		{ TEXT("WeaponCyclePrev"), WeaponCyclePrev },
		{ TEXT("DropWeapon"), DropWeapon },
		{ TEXT("ToggleLaser"), ToggleLaser },
		{ TEXT("VoiceMuteSelf"), VoiceMuteSelf },
		{ TEXT("Ping"), Ping },
		{ TEXT("SpectateToggleFreeCam"), SpectateToggleFreeCam },
		{ TEXT("ClipMarker"), ClipMarker },

		// The three overlays. They belong in this table for the same reason as
		// everything else: this is the list the control schema automation test
		// reads, and a field that is not in it is a field nobody checks.
		{ TEXT("ToggleHUD"), ToggleHUD },
		{ TEXT("Map"), Map },
		{ TEXT("VoiceLineMenu"), VoiceLineMenu }
	};

	for (const TPair<FName, const UInputAction*>& Pair : Required)
	{
		if (Pair.Value == nullptr)
		{
			OutMissing.Add(Pair.Key);
		}
	}

	if (OutMissing.Num() > 0)
	{
		OB_WARN("UOneBrahamInputConfig is missing %d input action(s): %s. "
			"Run Tools/Python/onebraham/build_input.py to generate the missing IA_* assets.",
			OutMissing.Num(), *FString::JoinBy(OutMissing, TEXT(", "), [](const FName& N) { return N.ToString(); }));
	}

	return OutMissing.Num() == 0;
}
