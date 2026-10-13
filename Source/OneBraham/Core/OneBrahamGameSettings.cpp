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
		{ TEXT("ClipMarker"), ClipMarker }
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
