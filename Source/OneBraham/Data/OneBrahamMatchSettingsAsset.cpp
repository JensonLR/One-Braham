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

		// The ten ids, weights and floor gates are the armoury table in
		// Tools/Python/onebraham/build_weapons.py, which is also what the
		// registry asset is generated from and what Docs/05 documents. They were
		// not: this table used a second naming scheme (Pistol_Glock17,
		// Carbine_L85A2) that the registry has never contained, so every row
		// rolled an id that resolves to nothing and the floor loot spawned no
		// weapons at all, while the gates it applied disagreed with the
		// documentation. check_weapon_ids.py now holds the two tables together,
		// in order.
		AddEntry(TEXT("W_Pistol"),			1.0f, 0,  17, {});
		AddEntry(TEXT("W_CompactPistol"),	0.8f, 0,  17, {});
		AddEntry(TEXT("W_SuppressedPistol"), 0.9f, 0,  17, {});
		AddEntry(TEXT("W_SMG"),				1.1f, 0,  17, {});
		AddEntry(TEXT("W_PDW"),				1.0f, 3,  17, {});
		AddEntry(TEXT("W_PumpShotgun"),		1.4f, 0,  17, {});
		AddEntry(TEXT("W_SemiShotgun"),		1.5f, 6,  17, {});
		AddEntry(TEXT("W_Carbine556"),		1.6f, 0,  17, { TEXT("Utility_Frag") });
		AddEntry(TEXT("W_CompactAR"),		1.5f, 5,  17, { TEXT("Utility_Smoke") });
		AddEntry(TEXT("W_MarksmanRifle"),	1.9f, 6,  17, {});
	}

	if (FloorLootProfiles.Num() == 0)
	{
		// 18 floors (0 = plaza). Density ramps up so that floor 9 (server) and
		// floor 15 (canteen/skydeck) are the most contested looting grounds.
		for (int32 Floor = 0; Floor <= ONE_BRAHAM_FLOORS; ++Floor)
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

	// The class defaults are ASCENT's (three squads of four, no respawns, an
	// extraction). FIRE DRILL is two teams of 6-8 with respawns and no
	// extraction - those are the mode, not tuning, so they are not left to
	// whoever authors the asset. Team size is tuning within that range
	// (DefaultGame.ini FireDrillTeamSize=6).
	if (Mode == EOneBrahamMatchMode::FireDrill)
	{
		TeamCount = 2;
		if (PlayersPerTeam < 6 || PlayersPerTeam > 8 || PlayersPerTeam % 2 != 0)
		{
			PlayersPerTeam = 6;
		}
		bRespawnEnabled = true;
		bExtractionEnabled = false;
		// A respawn into a live fight: long enough to look around, short
		// enough that it is not a shield to push with.
		SpawnGraceInvulnerabilitySeconds = 3.f;
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

		// Docs/06 "FIRE DRILL scoring". A point earns while its holder stands on
		// it unopposed, so with both teams on the map a team scores on one or
		// two points at a time: 0.11-0.22 a second from one room, the kills (1
		// each) and captures (2) the rest of a 200-point drill that runs eight
		// to ten minutes when the map is split. Measured (6v6 bots, real time):
		// with 5 a capture, close drills ended at 7.4 and 7.9 minutes, the
		// winner's flips a quarter of its 200; at 2 a capture they ended at
		// 9.2 and 8.5, and a 6v3 at 6.2 (it flips three points, not twenty). The 1 : 1.5 : 2 ratio is
		// the climb: the Skydeck is fourteen floors above the doors and worth
		// twice the Reception beside them. Capture time grows with it, so the
		// deep points are a fight to take and not a walk-on. (They were
		// 1.0 / 1.5 / 2.0 a second with 4-7 s captures and a 10-point capture:
		// a team on two points made 200 in 85 seconds.)
		AddPoint(TEXT("CP_Lobby"),	TEXT("Reception"),		1, 6.f,  0.11f);
		AddPoint(TEXT("CP_Server"),	TEXT("Server Floor 9"),	9, 8.f,  0.165f);
		AddPoint(TEXT("CP_Skydeck"),	TEXT("Skydeck"),		15, 10.f, 0.22f);
	}

	if (Mode == EOneBrahamMatchMode::FireDrill && MultiPointHoldMultipliers.Num() == 0)
	{
		MultiPointHoldMultipliers = { 1.f, 1.f, 1.5f, 2.f };
	}

	if (FloorLootProfiles.Num() > 0 && FloorLootProfiles.Num() < ONE_BRAHAM_FLOORS + 1)
	{
		OB_WARN("UOneBrahamMatchSettingsAsset '%s': floor loot profile has %d entries, expected %d.",
			*GetName(), FloorLootProfiles.Num(), ONE_BRAHAM_FLOORS + 1);
	}
}
