// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamSaveSettings.h"

#include "OneBraham.h"

void UOneBrahamSaveSettings::MigrateIfNeeded()
{
	if (Version == SaveVersion)
	{
		return;
	}

	OB_LOG("Migrating One Braham save from version %d to %d.", Version, SaveVersion);

	// v1 -> v2: utilities moved from a fixed 4-slot array into UtilityIds.
	// v2 -> v3: cosmetics gained UnlockedUtc for the "recently acquired" UI badge.
	// v3 -> v4: clan tag added.
	// v4 -> v5: per-weapon skins (EquippedWeaponSkins).

	for (FOneBrahamCosmeticProgress& Cosmetic : CosmeticProgress)
	{
		if (Cosmetic.UnlockedUtc.GetTicks() == 0)
		{
			Cosmetic.UnlockedUtc = FDateTime::UtcNow();
		}
	}

	if (ClanTag.IsEmpty())
	{
		ClanTag = TEXT("OB");
	}

	// Ensure at least one usable loadout exists after migration.
	if (Loadouts.Num() == 0)
	{
		FOneBrahamSavedLoadout Default;
		Default.UtilityIds = { TEXT("Utility_Frag"), TEXT("Utility_Flashbang"), TEXT("Utility_Smoke"), TEXT("Utility_BreachCharge") };
		Loadouts.Add(Default);
	}

	SelectedLoadoutIndex = FMath::Clamp(SelectedLoadoutIndex, 0, FMath::Max(0, Loadouts.Num() - 1));
	Version = SaveVersion;
}
