// Copyright (c) One Braham. All Rights Reserved.

#include "Data/OneBrahamMatchSettingsAsset.h"

#include "OneBraham.h"

void UOneBrahamMatchSettingsAsset::EnsureDefaults()
{
	if (WeaponSpawnTable.Num() == 0)
	{
		// Sensible ASCENT spawn table: plenty of pistols low down, long guns from
		// the office floors up, marksman rifles only where there are sightlines.
		auto AddEntry = [this](FName Id, float Weight, int32 MinFloor, int32 MaxFloor, std::initializer_list<FName> Utility)
		{
			FOneBrahamSpawnTableEntry Entry;
			Entry.WeaponId = Id;
			Entry.Weight = Weight;
			Entry.MinFloor = MinFloor;
			Entry.MaxFloor = MaxFloor;
			for (FName Item : Utility)
			{
				Entry.BundledUtilityIds.Add(Item);
			}
			WeaponSpawnTable.Add(Entry);
		};

		AddEntry(TEXT("Pistol_Glock17"),			3.0f, 0,  15, {});
		AddEntry(TEXT("Pistol_P320_Compact"),	2.5f, 0,  15, {});
		AddEntry(TEXT("Pistol_Suppressed_P226"), 1.2f, 2,  15, {});
		AddEntry(TEXT("SMG_MP5A3"),				1.6f, 1,  13, {});
		AddEntry(TEXT("PDW_P90"),				1.0f, 5,  15, {});
		AddEntry(TEXT("Shotgun_Rem870"),		1.4f, 0,  10, {});
		AddEntry(TEXT("Shotgun_SA_SX3"),		1.0f, 3,  15, {});
		AddEntry(TEXT("Carbine_L85A2"),			2.0f, 2,  15, { TEXT("Utility_Frag") });
		AddEntry(TEXT("Rifle_AR_Compact"),		1.8f, 3,  15, { TEXT("Utility_Smoke") });
		AddEntry(TEXT("Marksman_L129A1"),		0.8f, 9,  15, {});
	}

	if (FloorLootProfiles.Num() == 0)
	{
		// 16 floors (0 = plaza). Density ramps up so that floor 9 (server) and
		// floor 15 (canteen/skydeck) are the most contested looting grounds.
		for (int32 Floor = 0; Floor <= 15; ++Floor)
		{
			FOneBrahamFloorLootProfile Profile;
			Profile.FloorIndex = Floor;

			if (Floor == 0)
			{
				Profile.WeaponSpawns = 4; Profile.AmmoSpawns = 4; Profile.UtilitySpawns = 2; Profile.MedkitSpawns = 2; Profile.KeycardSpawns = 6;
				Profile.KeycardTier = EOneBrahamAccessTier::Staff;
			}
			else if (Floor <= 2)
			{
				Profile.WeaponSpawns = 5; Profile.AmmoSpawns = 6; Profile.UtilitySpawns = 3; Profile.MedkitSpawns = 2; Profile.KeycardSpawns = 4;
				Profile.KeycardTier = EOneBrahamAccessTier::Staff;
			}
			else if (Floor <= 8)
			{
				Profile.WeaponSpawns = 7; Profile.AmmoSpawns = 9; Profile.UtilitySpawns = 5; Profile.MedkitSpawns = 3; Profile.KeycardSpawns = 3;
				Profile.KeycardTier = Floor >= 7 ? EOneBrahamAccessTier::Secure : EOneBrahamAccessTier::Staff;
			}
			else if (Floor <= 11)
			{
				Profile.WeaponSpawns = 8; Profile.AmmoSpawns = 10; Profile.UtilitySpawns = 5; Profile.MedkitSpawns = 3; Profile.KeycardSpawns = 4;
				Profile.KeycardTier = EOneBrahamAccessTier::Executive;
			}
			else if (Floor <= 14)
			{
				Profile.WeaponSpawns = 7; Profile.AmmoSpawns = 9; Profile.UtilitySpawns = 6; Profile.MedkitSpawns = 4; Profile.KeycardSpawns = 3;
				Profile.KeycardTier = EOneBrahamAccessTier::Skydeck;
			}
			else
			{
				Profile.WeaponSpawns = 6; Profile.AmmoSpawns = 8; Profile.UtilitySpawns = 6; Profile.MedkitSpawns = 4; Profile.KeycardSpawns = 0;
				Profile.KeycardTier = EOneBrahamAccessTier::Override;
			}

			FloorLootProfiles.Add(Profile);
		}
	}

	if (ControlPoints.Num() == 0 && Mode == EOneBrahamMatchMode::FireDrill)
	{
		auto AddPoint = [this](FName Id, const FString& Name, int32 Floor, float Capture, float ScorePerSec)
		{
			FOneBrahamControlPointDef Point;
			Point.PointId = Id;
			Point.DisplayName = FText::FromString(Name);
			Point.FloorIndex = Floor;
			Point.CaptureSeconds = Capture;
			Point.ScorePerSecond = ScorePerSec;
			Point.VolumeActorTag = Id;
			ControlPoints.Add(Point);
		};

		AddPoint(TEXT("CP_Lobby"),	TEXT("Reception"),		1, 4.f, 1.0f);
		AddPoint(TEXT("CP_Server"),	TEXT("Server Floor 9"),	9, 6.f, 1.5f);
		AddPoint(TEXT("CP_Skydeck"),	TEXT("Skydeck"),		15, 7.f, 2.0f);
	}

	if (FloorLootProfiles.Num() > 0 && FloorLootProfiles.Num() < ONE_BRAHAM_FLOORS + 1)
	{
		OB_WARN("UOneBrahamMatchSettingsAsset '%s': floor loot profile has %d entries, expected %d.",
			*GetName(), FloorLootProfiles.Num(), ONE_BRAHAM_FLOORS + 1);
	}
}
