// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBuildingLayoutAsset.h
// ---------------------------------------------------------------------------
// Which floor is which part of the building.
//
// ONE BRAHAM is one building, fifteen floors, eight areas, and an absurd number
// of places that need to answer "where am I?" in words: the floor plan, the
// compass, the lockdown announcements, the spawn selector, the kill feed, the
// results screen. Every one of them used to invent its own answer, and the
// answers drifted - the audio manager called floor 15 "Canteen" and the
// environment build called it "Canteen and Skydeck".
//
// So there is one table, and it is a data asset so a designer can move the
// executive floor from 12 to 11 without a programmer, and it has a compiled-in
// default that matches the map the content build actually authors, so a clean
// checkout with no content is still coherent.
//
// FLOOR IS THE KEY, NOT THE AREA
//   Floors are physical and never move; areas are a design decision and did move
//   twice during development. Anything that needs to place a player, seal a
//   floor or label a lift panel asks by floor index. The layout asset exists to
//   turn that number into words.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OneBrahamBuildingLayoutAsset.generated.h"

/** One floor of the building. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamFloorDescription
{
	GENERATED_BODY()

	/** 0 = Ground Plaza and entrance. 15 = Canteen and Skydeck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	int32 FloorIndex = 0;

	/** Stable id used by audio zones, spawn anchors and analytics: "Lobby". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FName AreaId = NAME_None;

	/** What the lift says. Sentence case, no acronyms, no floor number:
	 *  the number is drawn next to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FText AreaName;

	/** The shortest honest label for the plan and the compass: "SERVER". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FText ShortName;

	/** True for floors the extraction and the endgame happen on: the Skydeck
	 *  and the canteen below it. Used by the lockdown director, which never
	 *  seals them, and by the plan, which draws them in brand yellow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	bool bEndgameFloor = false;

	/** True for floors with a breachable structural wall. Kept here so the
	 *  breach warning on the plan matches what the map actually has. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	bool bBreachable = false;
};

/**
 * The building, as data.
 *
 * The compiled-in defaults below are the real building - the same eight areas,
 * the same fifteen floors and the same four breachable walls the environment
 * build authors - because a game whose map and whose map data disagree is a game
 * with an invisible bug. The authored asset exists so the numbers can change
 * without a rebuild, not so they can be absent.
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamBuildingLayoutAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UOneBrahamBuildingLayoutAsset();

	/** What the building is called on the plan's header. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FText BuildingName;

	/** Lower-case, no prefix: the address line under the name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FText AddressLine;

	/** How many floors the building has, Skydeck included. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	int32 FloorCount = 16;

	/** Every floor, in ascending order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FOneBrahamFloorDescription> Floors;

	/** The floor of the extraction winch. Index 15 in the shipping building. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	int32 ExtractionFloorIndex = 15;

	/** The floor players spawn on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	int32 GroundFloorIndex = 0;

	/** The layout the game uses when no asset has been authored.
	 *
	 *  Loads /Game/OneBraham/Data/DA_BuildingLayout when it exists and falls back
	 *  to the class default object otherwise - which is why every default above
	 *  is real data rather than a zero. Returns null only if the class itself is
	 *  somehow unavailable, which cannot happen at runtime. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	static const UOneBrahamBuildingLayoutAsset* Get(const UObject* WorldContext);

	/** The floor description for an index, or null. Never assumes the array is
	 *  dense: a designer deleting a floor row must not make the plan crash. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	const FOneBrahamFloorDescription* FindFloor(int32 FloorIndex) const;

	/** The lift's wording for a floor: "Floor 9 - Server and comms" on the
	 *  ground floor, "Skydeck" at the top, and the area name in between. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	FText GetFloorAnnouncement(int32 FloorIndex) const;

	/** The plan's one-word label, or an empty string for an unknown floor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	FText GetFloorShortName(int32 FloorIndex) const;

	/** Everyone else's answer, in one place: the area id for a floor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	FName GetAreaIdForFloor(int32 FloorIndex) const;

	/** The floor an area id occupies, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	int32 GetFloorForAreaId(FName AreaId) const;

	/** True when a breachable wall exists on this floor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	bool IsFloorBreachable(int32 FloorIndex) const;

	/** Every floor in ascending index order, for anything drawing the whole
	 *  building (the plan, the spawn selector, the QA console command). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Building")
	void GetFloorsAscending(TArray<FOneBrahamFloorDescription>& OutFloors) const;
};
