// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Core.BuildingLayout
//
// The building is data, and the data is compiled in: sixteen floor rows that are
// a transcription of the same table Tools/Python/onebraham/build_environment.py
// lays the level out from. Everything that talks about "where the player is" -
// the lockdown director deciding what to seal, the extraction deciding whether
// you are on the Skydeck, the plan and the compass drawing a name, the audio
// zones, the spawn anchors, the analytics - reads this one table.
//
// Which makes it the highest-consequence data in the project. The first version
// of it put the executive wing on floor 11 and the server floor on 10: a game
// that told players they were somewhere they were not, on a map where the
// difference between the server floor and the executive floor is the difference
// between two entirely different fights. check_building_layout.py compares this
// table against the map builder's; this suite asserts the same table's own
// internal consistency, so the two checks fail for different reasons.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Data/OneBrahamBuildingLayoutAsset.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  The building
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBuildingLayoutTest, "OneBraham.Core.BuildingLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBuildingLayoutTest::RunTest(const FString& Parameters)
{
	// A default-constructed layout asset is the shipping building: the class
	// constructor fills the table, and the authored data asset exists to change
	// the numbers without a rebuild, not to provide them.
	const UOneBrahamBuildingLayoutAsset* Building = GetDefault<UOneBrahamBuildingLayoutAsset>();

	if (Building == nullptr)
	{
		AddError(TEXT("The building layout class has no default object."));
		return false;
	}

	TestFalse(TEXT("the building is named"), Building->BuildingName.IsEmpty());
	TestFalse(TEXT("and has an address line"), Building->AddressLine.IsEmpty());

	// Sixteen floors: the plaza plus fifteen storeys, with the Skydeck on top.
	TestEqual(TEXT("the building has sixteen floors"), Building->FloorCount, 16);
	TestEqual(TEXT("and sixteen floor rows"), Building->Floors.Num(), 16);
	TestEqual(TEXT("the extraction floor is the Skydeck"), Building->ExtractionFloorIndex,
		OneBrahamConstants::FloorSkydeck);
	TestEqual(TEXT("players start on the ground floor"), Building->GroundFloorIndex,
		OneBrahamConstants::FloorPlaza);

	// The table ascends without gaps or repeats. A missing floor row is a floor
	// whose area id is None, which reaches the plan as a blank label and the
	// lockdown director as "unknown storey".
	{
		TSet<int32> Indices;
		int32 Gaps = 0;
		int32 Unnamed = 0;

		for (int32 Row = 0; Row < Building->Floors.Num(); ++Row)
		{
			const FOneBrahamFloorDescription& Floor = Building->Floors[Row];

			if (Indices.Contains(Floor.FloorIndex))
			{
				AddError(FString::Printf(TEXT("floor %d appears twice"), Floor.FloorIndex));
			}

			Indices.Add(Floor.FloorIndex);

			if (Row > 0 && Floor.FloorIndex != Building->Floors[Row - 1].FloorIndex + 1)
			{
				++Gaps;
			}

			if (Floor.AreaId.IsNone() || Floor.AreaName.IsEmpty() || Floor.ShortName.IsEmpty())
			{
				AddError(FString::Printf(TEXT("floor %d has no area id or no name"), Floor.FloorIndex));
				++Unnamed;
			}
		}

		TestEqual(TEXT("no floor is missing from the table"), Gaps, 0);
		TestEqual(TEXT("every floor has an id, a name and a short name"), Unnamed, 0);
		TestEqual(TEXT("the first row is the ground floor"), Building->Floors[0].FloorIndex,
			OneBrahamConstants::FloorPlaza);
		TestEqual(TEXT("the last row is the Skydeck"), Building->Floors.Last().FloorIndex,
			OneBrahamConstants::FloorSkydeck);
	}

	// The eight areas, in the order the building has them. Asserted by name
	// because these ids are the interface between the map, the audio zones, the
	// spawn anchors and the analytics: renaming one is renaming a room.
	{
		struct FAreaExpectation
		{
			const TCHAR* AreaId;
			int32 FirstFloor;
			int32 LastFloor;
		};

		static const FAreaExpectation Areas[] =
		{
			{ TEXT("Ground_Plaza"), 0, 0 },
			{ TEXT("Lobby"),        1, 1 },
			{ TEXT("Mezzanine"),    2, 2 },
			{ TEXT("OpenPlan"),     3, 8 },
			{ TEXT("ServerComms"),  9, 11 },
			{ TEXT("Executive"),    12, 12 },
			{ TEXT("Canteen"),      13, 14 },
			{ TEXT("Skydeck"),      15, 15 }
		};

		TSet<FName> SeenAreas;

		for (const FAreaExpectation& Area : Areas)
		{
			const FName AreaId(Area.AreaId);
			SeenAreas.Add(AreaId);

			// Every floor in the block belongs to the area, and nothing else does.
			for (int32 Floor = Area.FirstFloor; Floor <= Area.LastFloor; ++Floor)
			{
				TestEqual(*FString::Printf(TEXT("floor %d is %s"), Floor, Area.AreaId),
					Building->GetAreaIdForFloor(Floor), AreaId);
			}

			TestEqual(*FString::Printf(TEXT("%s starts on floor %d"), Area.AreaId, Area.FirstFloor),
				Building->GetFloorForAreaId(AreaId), Area.FirstFloor);

			if (Area.LastFloor + 1 < Building->FloorCount)
			{
				TestNotEqual(*FString::Printf(TEXT("%s ends on floor %d"), Area.AreaId, Area.LastFloor),
					Building->GetAreaIdForFloor(Area.LastFloor + 1), AreaId);
			}
		}

		// And no floor claims an area outside that list.
		for (const FOneBrahamFloorDescription& Floor : Building->Floors)
		{
			TestTrue(*FString::Printf(TEXT("floor %d's area is one of the eight"), Floor.FloorIndex),
				SeenAreas.Contains(Floor.AreaId));
		}
	}

	// The endgame floors - the canteen block and the Skydeck - are the ones the
	// lockdown never seals and the plan draws in signal yellow.
	{
		int32 Endgame = 0;

		for (const FOneBrahamFloorDescription& Floor : Building->Floors)
		{
			if (Floor.bEndgameFloor)
			{
				++Endgame;
				TestTrue(*FString::Printf(TEXT("floor %d is part of the endgame"), Floor.FloorIndex),
					Floor.FloorIndex >= OneBrahamConstants::FloorCanteen);
			}
		}

		TestEqual(TEXT("three endgame floors"), Endgame, 3);
		TestTrue(TEXT("the extraction floor is an endgame floor"),
			Building->FindFloor(Building->ExtractionFloorIndex) != nullptr
			&& Building->FindFloor(Building->ExtractionFloorIndex)->bEndgameFloor);
		TestTrue(TEXT("and is never breachable"),
			!Building->IsFloorBreachable(Building->ExtractionFloorIndex));
	}

	// Breach walls: four of them, all in the middle of the building, none of them
	// on a plate-glass endgame floor. Each one is a route that exists only if the
	// player spends a charge on it.
	{
		TArray<int32> Breachable;

		for (const FOneBrahamFloorDescription& Floor : Building->Floors)
		{
			if (Floor.bBreachable)
			{
				Breachable.Add(Floor.FloorIndex);
			}
		}

		TestEqual(TEXT("four breachable floors"), Breachable.Num(), 4);

		const int32 Expected[] = { 5, 9, 12, 14 };

		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected); ++Index)
		{
			if (Breachable.IsValidIndex(Index))
			{
				TestEqual(*FString::Printf(TEXT("breach wall %d is on the documented floor"), Index + 1),
					Breachable[Index], Expected[Index]);
			}
		}

		// One per quarter of the building rather than four on one landing: a
		// breach is a shortcut between distinct fights or it is a novelty.
		TestTrue(TEXT("the first breach wall is above the open plan's first floors"),
			Breachable.Num() > 0 && Breachable[0] >= 3);
		TestTrue(TEXT("the last is below the canteen"),
			Breachable.Num() > 0 && Breachable.Last() < OneBrahamConstants::FloorCanteen);
	}

	// Asking about a floor that does not exist is a null, an empty name and an
	// absent breach rather than a crash or a plausible-looking lie.
	TestNull(TEXT("there is no floor 16"), Building->FindFloor(16));
	TestNull(TEXT("nor a floor -1"), Building->FindFloor(-1));
	TestTrue(TEXT("floor 16 has no area"), Building->GetAreaIdForFloor(16).IsNone());
	TestTrue(TEXT("floor 16 has no short name"), Building->GetFloorShortName(16).IsEmpty());
	TestFalse(TEXT("floor 16 is not breachable"), Building->IsFloorBreachable(16));
	TestEqual(TEXT("an unknown area has no floor"), Building->GetFloorForAreaId(TEXT("Rooftop")), INDEX_NONE);
	TestEqual(TEXT("and neither does an empty one"), Building->GetFloorForAreaId(NAME_None), INDEX_NONE);

	// The short names are what fits a compass strip, so they are shorter than the
	// lift wording by construction. A "short" name longer than the full one is a
	// copy-paste, and the compass is where it shows.
	for (const FOneBrahamFloorDescription& Floor : Building->Floors)
	{
		TestTrue(*FString::Printf(TEXT("floor %d's short name is the shorter one"), Floor.FloorIndex),
			Floor.ShortName.ToString().Len() <= Floor.AreaName.ToString().Len());
	}

	// The table has one row per floor between the plaza and the Skydeck: sixteen,
	// which is also the floor count. A row short is a floor the lockdown cannot
	// name and the plan cannot draw.
	TestEqual(TEXT("there is a row for every floor"),
		Building->FloorCount, OneBrahamConstants::FloorSkydeck - OneBrahamConstants::FloorPlaza + 1);

	// The extraction gate, from the two constants that define it: the winch
	// terminal is on the Skydeck and refuses a caller standing a storey below it,
	// so the gate is (15 - 1) x 400 = 5600 cm - the number Docs/06 publishes as
	// "the claiming pawn's Z >= 5600". Change the storey height and this fails,
	// which is the point: the documentation, the gate and the map all move
	// together or none of them do.
	TestNearlyEqual(TEXT("the extraction gate is at 5600 cm"),
		static_cast<float>(OneBrahamConstants::FloorSkydeck - 1) * OneBrahamConstants::FloorHeightCm,
		5600.f, 0.01f);

	// The datum both ways (Docs/09 section 1): the lobby is floor 1 at Z = 0,
	// the plaza floor 0, the Skydeck floor 15 at 5600. Every Z / 400 in the
	// module used to read the lobby as floor 0, so the lockdown purged the
	// storey above the one it announced.
	TestNearlyEqual(TEXT("the lobby slab is the datum"), OneBrahamConstants::FloorZ(1), 0.f, 0.01f);
	TestNearlyEqual(TEXT("the Skydeck slab is at 5600 cm"),
		OneBrahamConstants::FloorZ(OneBrahamConstants::FloorSkydeck), 5600.f, 0.01f);
	TestEqual(TEXT("a player in the lobby is on floor 1"), OneBrahamConstants::FloorIndexForZ(90.f), 1);
	TestEqual(TEXT("a player in the plaza is on floor 0"), OneBrahamConstants::FloorIndexForZ(-310.f), 0);
	TestEqual(TEXT("a player on the Skydeck is on floor 15"), OneBrahamConstants::FloorIndexForZ(5690.f), 15);
	TestEqual(TEXT("a player on floor 14 is not on the Skydeck"), OneBrahamConstants::FloorIndexForZ(5290.f), 14);
	TestEqual(TEXT("floor and Z round-trip on every storey"),
		OneBrahamConstants::FloorIndexForZ(OneBrahamConstants::FloorZ(9) + 90.f), 9);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
