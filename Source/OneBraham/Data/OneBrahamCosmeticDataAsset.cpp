// Copyright (c) One Braham. All Rights Reserved.

#include "Data/OneBrahamCosmeticDataAsset.h"

#include "Core/OneBrahamSaveSettings.h"

UOneBrahamCosmeticDataAsset::UOneBrahamCosmeticDataAsset()
{
	// One item per achievement that grants a tag (Docs/19 section 9). None of
	// them is a starter item or any archetype's default outfit, so locking them
	// never leaves a slot empty; check_cosmetic_ids.py holds that.
	const auto AddUnlock = [this](const TCHAR* CosmeticId, const TCHAR* Tag, const FText& How)
	{
		FOneBrahamCosmeticUnlock Row;
		Row.CosmeticId = CosmeticId;
		Row.UnlockTag = Tag;
		Row.HowToUnlock = How;
		Unlocks.Add(Row);
	};

	AddUnlock(TEXT("Jacket_Bomber"), TEXT("OB.Cosmetic.Unlock.Milestone.Extract5"),
		NSLOCTEXT("OneBraham", "Unlock_Extract5", "Extract five times."));
	AddUnlock(TEXT("BagTag_Event"), TEXT("OB.Cosmetic.Unlock.Milestone.NoDamageExtract"),
		NSLOCTEXT("OneBraham", "Unlock_NoDamage", "Extract without dealing a point of damage."));
	AddUnlock(TEXT("Fleece_Branded"), TEXT("OB.Cosmetic.Unlock.Milestone.ServerFloorWin"),
		NSLOCTEXT("OneBraham", "Unlock_ServerFloor", "Win ASCENT as the last squad standing, on Server & Comms."));
	AddUnlock(TEXT("HiVis_Orange"), TEXT("OB.Cosmetic.Unlock.Achievement.FireDrill50"),
		NSLOCTEXT("OneBraham", "Unlock_FireDrill50", "Win 50 FIRE DRILL matches."));
}

const FOneBrahamCosmeticUnlock* UOneBrahamCosmeticDataAsset::FindUnlock(FName CosmeticId) const
{
	return Unlocks.FindByPredicate([CosmeticId](const FOneBrahamCosmeticUnlock& Row)
	{
		return Row.CosmeticId == CosmeticId;
	});
}

bool UOneBrahamCosmeticDataAsset::IsUnlockedFor(const UOneBrahamSaveSettings* Profile, FName CosmeticId) const
{
	const FOneBrahamCosmeticUnlock* Row = FindUnlock(CosmeticId);
	if (Row == nullptr || Row->UnlockTag.IsNone())
	{
		return true;
	}
	return Profile != nullptr && Profile->EarnedUnlockTags.Contains(Row->UnlockTag);
}

const UOneBrahamCosmeticDataAsset* UOneBrahamCosmeticDataAsset::LoadCatalogue()
{
	if (const UOneBrahamCosmeticDataAsset* Built = LoadObject<UOneBrahamCosmeticDataAsset>(
		nullptr, TEXT("/Game/OneBraham/Characters/DA_CosmeticCatalogue.DA_CosmeticCatalogue"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Built;
	}
	return GetDefault<UOneBrahamCosmeticDataAsset>();
}
