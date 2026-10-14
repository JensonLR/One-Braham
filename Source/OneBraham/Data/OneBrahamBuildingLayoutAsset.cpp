// Copyright (c) One Braham. All Rights Reserved.

#include "Data/OneBrahamBuildingLayoutAsset.h"

#include "OneBraham.h"
#include "Engine/World.h"

namespace
{
	/** The authored asset, by path. Absent on a clean checkout, which is why the
	 *  class default object carries the same data. */
	const TCHAR* BuildingLayoutPath = TEXT("/Game/OneBraham/Data/DA_BuildingLayout.DA_BuildingLayout");
}

UOneBrahamBuildingLayoutAsset::UOneBrahamBuildingLayoutAsset()
{
	BuildingName = NSLOCTEXT("OneBraham", "BuildingName", "ONE BRAHAM");
	AddressLine = NSLOCTEXT("OneBraham", "BuildingAddress", "ONE BRAHAM, ALDGATE, LONDON");

	// -----------------------------------------------------------------------
	//  The building.
	//
	//  Fifteen floors plus the Skydeck. The area names are the lift's own
	//  wording - sentence case, no acronyms - and the short names are what fits
	//  on a compass strip. Both matter: "Server and comms" fits a lift panel and
	//  will not fit a 120-pixel compass, which is why there are two.
	//
	//  This table is the same one build_environment.py lays the level out from.
	//  If they disagree, the map and the map data disagree, and a player is told
	//  they are somewhere they are not.
	// -----------------------------------------------------------------------
	Floors.Reset();

	auto AddFloor = [this](int32 Index, const TCHAR* AreaId, const FText& Name, const FText& Short,
		bool bEndgame, bool bBreachable)
	{
		FOneBrahamFloorDescription Floor;
		Floor.FloorIndex = Index;
		Floor.AreaId = FName(AreaId);
		Floor.AreaName = Name;
		Floor.ShortName = Short;
		Floor.bEndgameFloor = bEndgame;
		Floor.bBreachable = bBreachable;
		Floors.Add(Floor);
	};

	// index, area id, lift wording, plan wording, endgame, breachable
	//
	// This table is a transcription of common.AREAS and BREACHABLE_WALLS in
	// Tools/Python/onebraham/build_environment.py, which is what the content
	// build lays the level out from:
	//
	//   Ground_Plaza 0 | Lobby 1 | Mezzanine 2 | OpenPlan 3-8
	//   ServerComms 9-11 | Executive 12 | Canteen 13-14 | Skydeck 15
	//
	// Not a paraphrase: a transcription, and there is a checker for it
	// (check_building_layout.py) because the first version of this file put the
	// executive wing on floor 11 and the server floor on 10, which is a game that
	// tells players they are somewhere they are not.
	AddFloor(0,  TEXT("Ground_Plaza"),
		NSLOCTEXT("OneBraham", "Area_0", "Ground plaza and reception"),
		NSLOCTEXT("OneBraham", "AreaShort_0", "PLAZA"), false, false);
	AddFloor(1,  TEXT("Lobby"),
		NSLOCTEXT("OneBraham", "Area_1", "Lobby"),
		NSLOCTEXT("OneBraham", "AreaShort_1", "LOBBY"), false, false);
	AddFloor(2,  TEXT("Mezzanine"),
		NSLOCTEXT("OneBraham", "Area_2", "Mezzanine and lift lobby"),
		NSLOCTEXT("OneBraham", "AreaShort_2", "MEZZANINE"), false, false);
	AddFloor(3,  TEXT("OpenPlan"),
		NSLOCTEXT("OneBraham", "Area_3", "Open plan office, north"),
		NSLOCTEXT("OneBraham", "AreaShort_3", "OFFICES"), false, false);
	AddFloor(4,  TEXT("OpenPlan"),
		NSLOCTEXT("OneBraham", "Area_4", "Open plan office, south"),
		NSLOCTEXT("OneBraham", "AreaShort_4", "OFFICES"), false, false);
	AddFloor(5,  TEXT("OpenPlan"),
		NSLOCTEXT("OneBraham", "Area_5", "Open plan office and kitchenette"),
		NSLOCTEXT("OneBraham", "AreaShort_5", "OFFICES"), false, true);
	AddFloor(6,  TEXT("OpenPlan"),
		NSLOCTEXT("OneBraham", "Area_6", "Open plan office, meeting rooms"),
		NSLOCTEXT("OneBraham", "AreaShort_6", "OFFICES"), false, false);
	AddFloor(7,  TEXT("OpenPlan"),
		NSLOCTEXT("OneBraham", "Area_7", "Open plan office, east wing"),
		NSLOCTEXT("OneBraham", "AreaShort_7", "OFFICES"), false, false);
	AddFloor(8,  TEXT("OpenPlan"),
		NSLOCTEXT("OneBraham", "Area_8", "Open plan office, plant room"),
		NSLOCTEXT("OneBraham", "AreaShort_8", "OFFICES"), false, false);
	AddFloor(9,  TEXT("ServerComms"),
		NSLOCTEXT("OneBraham", "Area_9", "Server and comms"),
		NSLOCTEXT("OneBraham", "AreaShort_9", "SERVER"), false, true);
	AddFloor(10, TEXT("ServerComms"),
		NSLOCTEXT("OneBraham", "Area_10", "Server hall and network operations"),
		NSLOCTEXT("OneBraham", "AreaShort_10", "SERVER"), false, false);
	AddFloor(11, TEXT("ServerComms"),
		NSLOCTEXT("OneBraham", "Area_11", "Comms room and roof riser"),
		NSLOCTEXT("OneBraham", "AreaShort_11", "COMMS"), false, false);
	AddFloor(12, TEXT("Executive"),
		NSLOCTEXT("OneBraham", "Area_12", "Executive wing and boardroom"),
		NSLOCTEXT("OneBraham", "AreaShort_12", "EXECUTIVE"), false, true);
	AddFloor(13, TEXT("Executive"),
		NSLOCTEXT("OneBraham", "Area_13", "Executive offices"),
		NSLOCTEXT("OneBraham", "AreaShort_13", "EXECUTIVE"), false, false);
	AddFloor(14, TEXT("Executive"),
		NSLOCTEXT("OneBraham", "Area_14", "Executive suites"),
		NSLOCTEXT("OneBraham", "AreaShort_14", "EXECUTIVE"), false, true);
	AddFloor(15, TEXT("Skydeck"),
		NSLOCTEXT("OneBraham", "Area_15", "Canteen and Skydeck balcony"),
		NSLOCTEXT("OneBraham", "AreaShort_15", "CANTEEN / SKYDECK"), true, false);

	FloorCount = Floors.Num();
	ExtractionFloorIndex = 15;
	GroundFloorIndex = 0;
}

const UOneBrahamBuildingLayoutAsset* UOneBrahamBuildingLayoutAsset::Get(const UObject* WorldContext)
{
	// The authored asset wins: a designer who has moved the executive floor up a
	// storey should not have to file a ticket to see it.
	if (WorldContext != nullptr)
	{
		static const UOneBrahamBuildingLayoutAsset* CachedAuthored = nullptr;
		static bool bLookedUp = false;

		if (!bLookedUp)
		{
			bLookedUp = true;
			CachedAuthored = LoadObject<UOneBrahamBuildingLayoutAsset>(nullptr, BuildingLayoutPath);

			UE_CLOG(CachedAuthored == nullptr, LogOneBraham, Log,
				TEXT("[OB] No DA_BuildingLayout asset: using the compiled-in building layout. "
					 "This is expected on a clean checkout and is not an error - the defaults "
					 "are the shipping building."));
		}

		if (CachedAuthored != nullptr)
		{
			return CachedAuthored;
		}
	}

	return GetDefault<UOneBrahamBuildingLayoutAsset>();
}

const FOneBrahamFloorDescription* UOneBrahamBuildingLayoutAsset::FindFloor(int32 FloorIndex) const
{
	for (const FOneBrahamFloorDescription& Floor : Floors)
	{
		if (Floor.FloorIndex == FloorIndex)
		{
			return &Floor;
		}
	}

	return nullptr;
}

FText UOneBrahamBuildingLayoutAsset::GetFloorAnnouncement(int32 FloorIndex) const
{
	const FOneBrahamFloorDescription* Floor = FindFloor(FloorIndex);

	if (Floor == nullptr)
	{
		return NSLOCTEXT("OneBraham", "AreaUnknown", "Unknown floor");
	}

	// The Skydeck and the ground floor are named, not numbered: nobody in the
	// building's history has ever said "floor fifteen", and nobody has said
	// "floor zero" either.
	if (FloorIndex == ExtractionFloorIndex)
	{
		return Floor->AreaName;
	}

	if (FloorIndex == GroundFloorIndex)
	{
		return Floor->AreaName;
	}

	return FText::Format(NSLOCTEXT("OneBraham", "FloorAnnouncement", "Floor {0} - {1}"),
		FText::AsNumber(FloorIndex), Floor->AreaName);
}

FText UOneBrahamBuildingLayoutAsset::GetFloorShortName(int32 FloorIndex) const
{
	const FOneBrahamFloorDescription* Floor = FindFloor(FloorIndex);

	return Floor != nullptr ? Floor->ShortName : FText::GetEmpty();
}

FName UOneBrahamBuildingLayoutAsset::GetAreaIdForFloor(int32 FloorIndex) const
{
	const FOneBrahamFloorDescription* Floor = FindFloor(FloorIndex);

	return Floor != nullptr ? Floor->AreaId : NAME_None;
}

int32 UOneBrahamBuildingLayoutAsset::GetFloorForAreaId(FName AreaId) const
{
	for (const FOneBrahamFloorDescription& Floor : Floors)
	{
		if (Floor.AreaId == AreaId)
		{
			return Floor.FloorIndex;
		}
	}

	return INDEX_NONE;
}

bool UOneBrahamBuildingLayoutAsset::IsFloorBreachable(int32 FloorIndex) const
{
	const FOneBrahamFloorDescription* Floor = FindFloor(FloorIndex);

	return Floor != nullptr && Floor->bBreachable;
}

void UOneBrahamBuildingLayoutAsset::GetFloorsAscending(TArray<FOneBrahamFloorDescription>& OutFloors) const
{
	OutFloors = Floors;
	OutFloors.Sort([](const FOneBrahamFloorDescription& A, const FOneBrahamFloorDescription& B)
	{
		return A.FloorIndex < B.FloorIndex;
	});
}
