// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamHUDStringTable.h"

#include "OneBraham.h"

namespace OneBrahamHUDStrings
{
	const TArray<TPair<FName, FText>>& All()
	{
		// Built once. The strings are literals; the array exists so the table can
		// be iterated by the checker, the test and a future CSV export, rather
		// than only by the call that wanted one line.
		static const TArray<TPair<FName, FText>> Table = {
		{ TEXT("HUD_Mode_Ascent"), NSLOCTEXT("OneBraham", "HUD_Mode_Ascent", "ASCENT") },
		{ TEXT("HUD_Mode_FireDrill"), NSLOCTEXT("OneBraham", "HUD_Mode_FireDrill", "FIRE DRILL") },
		{ TEXT("HUD_Mode_NightShift"), NSLOCTEXT("OneBraham", "HUD_Mode_NightShift", "NIGHT SHIFT") },
		{ TEXT("HUD_Objective_SignIn"), NSLOCTEXT("OneBraham", "HUD_Objective_SignIn", "Sign in at reception") },
		{ TEXT("HUD_Objective_Ascend"), NSLOCTEXT("OneBraham", "HUD_Objective_Ascend", "Reach the Skydeck") },
		{ TEXT("HUD_Objective_Winch"), NSLOCTEXT("OneBraham", "HUD_Objective_Winch", "Winch zone is open - take the Skydeck") },
		{ TEXT("HUD_Objective_Hold"), NSLOCTEXT("OneBraham", "HUD_Objective_Hold", "Winch is holding - get to the pad") },
		{ TEXT("HUD_Objective_Lockdown"), NSLOCTEXT("OneBraham", "HUD_Objective_Lockdown", "Shutters down - the Skydeck is still open") },
		{ TEXT("HUD_Objective_Purge"), NSLOCTEXT("OneBraham", "HUD_Objective_Purge", "Security purge on lower floors - get to floor 15") },
		{ TEXT("HUD_Prompt_Override"), NSLOCTEXT("OneBraham", "HUD_Prompt_Override", "Hold E - override") },
		{ TEXT("HUD_Prompt_Card"), NSLOCTEXT("OneBraham", "HUD_Prompt_Card", "Use badge") },
		{ TEXT("HUD_Prompt_Revive"), NSLOCTEXT("OneBraham", "HUD_Prompt_Revive", "Hold E - revive") },
		{ TEXT("HUD_Prompt_Execute"), NSLOCTEXT("OneBraham", "HUD_Prompt_Execute", "Hold E - finish") },
		{ TEXT("HUD_Prompt_Breach"), NSLOCTEXT("OneBraham", "HUD_Prompt_Breach", "Plant charge") },
		{ TEXT("HUD_Locked_Executive"), NSLOCTEXT("OneBraham", "HUD_Locked_Executive", "Executive badge required") },
		{ TEXT("HUD_Locked_Secure"), NSLOCTEXT("OneBraham", "HUD_Locked_Secure", "IT Operations badge required") },
		{ TEXT("HUD_Locked_Skydeck"), NSLOCTEXT("OneBraham", "HUD_Locked_Skydeck", "Facilities master badge required") },
		{ TEXT("HUD_Downed_BleedOut"), NSLOCTEXT("OneBraham", "HUD_Downed_BleedOut", "BLEEDING OUT") },
		{ TEXT("HUD_Downed_CallHelp"), NSLOCTEXT("OneBraham", "HUD_Downed_CallHelp", "CALL FOR HELP") },
		{ TEXT("HUD_Extraction_Hold"), NSLOCTEXT("OneBraham", "HUD_Extraction_Hold", "HOLD THE PAD") },
		{ TEXT("HUD_Extraction_Contested"), NSLOCTEXT("OneBraham", "HUD_Extraction_Contested", "CONTESTED") },
		{ TEXT("HUD_Extraction_Failed"), NSLOCTEXT("OneBraham", "HUD_Extraction_Failed", "WINCH ABORTED") },
		{ TEXT("HUD_Match_NoSurvivors"), NSLOCTEXT("OneBraham", "HUD_Match_NoSurvivors", "NO SURVIVORS") },
		{ TEXT("HUD_Match_Extracted"), NSLOCTEXT("OneBraham", "HUD_Match_Extracted", "EXTRACTED") },
		{ TEXT("HUD_Squad_You"), NSLOCTEXT("OneBraham", "HUD_Squad_You", "YOU") },
		{ TEXT("HUD_Squad_Downed"), NSLOCTEXT("OneBraham", "HUD_Squad_Downed", "DOWN") },
		{ TEXT("HUD_Squad_Dead"), NSLOCTEXT("OneBraham", "HUD_Squad_Dead", "KIA") },
		{ TEXT("HUD_Floor_Format"), NSLOCTEXT("OneBraham", "HUD_Floor_Format", "FL {0}") },
		{ TEXT("HUD_Voice_Proximity"), NSLOCTEXT("OneBraham", "HUD_Voice_Proximity", "PROXIMITY") },
		{ TEXT("HUD_Voice_Squad"), NSLOCTEXT("OneBraham", "HUD_Voice_Squad", "SQUAD") },
		{ TEXT("HUD_Voice_Team"), NSLOCTEXT("OneBraham", "HUD_Voice_Team", "TEAM") },
		};

		return Table;
	}

	FText Get(FName Key)
	{
		for (const TPair<FName, FText>& Row : All())
		{
			if (Row.Key == Key)
			{
				return Row.Value;
			}
		}

		OB_WARN("HUD string '%s' is not in the copy table; the HUD will show nothing "
			"rather than a wrong sentence.", *Key.ToString());

		return FText::GetEmpty();
	}

	bool Has(FName Key)
	{
		for (const TPair<FName, FText>& Row : All())
		{
			if (Row.Key == Key)
			{
				return true;
			}
		}

		return false;
	}
}
