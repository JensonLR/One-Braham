// Copyright (c) One Braham. All Rights Reserved.

#include "AI/OneBrahamBotRules.h"

#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"

namespace OneBrahamBotRules
{
	FString MakeBotName(int32 GlobalOrdinal)
	{
		// Surnames off a floor directory: a bot reads as a colleague in the kill
		// feed ("BOT Hughes"), and the BOT prefix means nobody mistakes one for a
		// person on the scoreboard.
		static const TCHAR* Surnames[] =
		{
			TEXT("Hughes"), TEXT("Okafor"), TEXT("Patel"), TEXT("Lindqvist"), TEXT("Moreau"),
			TEXT("Kowalski"), TEXT("Brennan"), TEXT("Adeyemi"), TEXT("Tanaka"), TEXT("Whitlock"),
			TEXT("Castell"), TEXT("Duarte"), TEXT("Fairweather"), TEXT("Nakamura"), TEXT("Osei"),
			TEXT("Pryce")
		};

		const int32 Count = UE_ARRAY_COUNT(Surnames);
		const int32 Index = FMath::Abs(GlobalOrdinal) % Count;
		const int32 Lap = FMath::Abs(GlobalOrdinal) / Count;

		return Lap == 0
			? FString::Printf(TEXT("BOT %s"), Surnames[Index])
			: FString::Printf(TEXT("BOT %s %d"), Surnames[Index], Lap + 1);
	}

	bool ReadSettingsBotFill(const UObject* Settings, bool& bOutFill, int32& OutMaxBots)
	{
		if (Settings == nullptr)
		{
			return false;
		}

		const UClass* Class = Settings->GetClass();

		// By name rather than by member: the fields belong to
		// UOneBrahamMatchSettingsAsset, and an asset built before they existed
		// must still load and fall back to the per-mode default.
		if (const FIntProperty* MaxProp = FindFProperty<FIntProperty>(Class, TEXT("MaxBots")))
		{
			OutMaxBots = MaxProp->GetPropertyValue_InContainer(Settings);
		}

		if (const FBoolProperty* FillProp = FindFProperty<FBoolProperty>(Class, TEXT("bFillWithBots")))
		{
			bOutFill = FillProp->GetPropertyValue_InContainer(Settings);
			return true;
		}

		return false;
	}
}
