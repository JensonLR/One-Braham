// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFloorLightingTests.cpp
// ---------------------------------------------------------------------------
// Which floors' lights are in the scene. A rule that hides one floor too many
// leaves a player looking up a stair into a black hole; one that hides too
// few is the 700-light frame this exists to cut; one without hysteresis
// strobes two floors of lights while someone stands on a landing.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OneBraham.h"
#include "World/OneBrahamFloorLighting.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** Camera height above floor N's slab top. */
	float OBCameraZ(int32 Floor, float AboveSlab)
	{
		return OneBrahamConstants::GroundFloorZ + Floor * OneBrahamConstants::FloorHeightCm + AboveSlab;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFloorLightingViewerFloorTest, "OneBraham.World.FloorLighting.ViewerFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFloorLightingViewerFloorTest::RunTest(const FString& Parameters)
{
	using FRule = FOneBrahamFloorLightingRule;

	// The first look is the plain conversion.
	TestEqual(TEXT("eye height on the ground floor"), FRule::ViewerFloor(OBCameraZ(0, 165.f), INDEX_NONE), 0);
	TestEqual(TEXT("eye height on floor 5"), FRule::ViewerFloor(OBCameraZ(5, 165.f), INDEX_NONE), 5);
	TestEqual(TEXT("agrees with FloorIndexForZ"), FRule::ViewerFloor(OBCameraZ(9, 10.f), INDEX_NONE),
		OneBrahamConstants::FloorIndexForZ(OBCameraZ(9, 10.f)));

	// Clamped to the building: the street below the plaza and the sky above the roof.
	TestEqual(TEXT("below the plaza is floor 0"), FRule::ViewerFloor(OBCameraZ(0, -900.f), INDEX_NONE), 0);
	TestEqual(TEXT("above the roof is the top floor"), FRule::ViewerFloor(OBCameraZ(ONE_BRAHAM_FLOORS + 6, 0.f), INDEX_NONE),
		ONE_BRAHAM_FLOORS);

	// Hysteresis: just over a boundary keeps the floor the camera came from.
	const float Band = FRule::HysteresisCm;
	TestEqual(TEXT("just above floor 5's ceiling line is still floor 5"),
		FRule::ViewerFloor(OBCameraZ(6, Band * 0.5f), 5), 5);
	TestEqual(TEXT("just below floor 5's slab is still floor 5"),
		FRule::ViewerFloor(OBCameraZ(5, -Band * 0.5f), 5), 5);
	TestEqual(TEXT("past the band, floor 6"), FRule::ViewerFloor(OBCameraZ(6, Band + 1.f), 5), 6);
	TestEqual(TEXT("past the band going down, floor 4"), FRule::ViewerFloor(OBCameraZ(5, -Band - 1.f), 5), 4);
	TestEqual(TEXT("a lift jump of several floors is not held back"), FRule::ViewerFloor(OBCameraZ(12, 165.f), 3), 12);
	TestEqual(TEXT("no band: the boundary is the boundary"), FRule::ViewerFloor(OBCameraZ(6, 1.f), 5, 0.f), 6);
	TestEqual(TEXT("a negative band is no band"), FRule::ViewerFloor(OBCameraZ(6, 1.f), 5, -50.f), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFloorLightingIsLitTest, "OneBraham.World.FloorLighting.IsFloorLit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFloorLightingIsLitTest::RunTest(const FString& Parameters)
{
	using FRule = FOneBrahamFloorLightingRule;
	const int32 Radius = FRule::DefaultRadius;

	TestEqual(TEXT("the default reach is one floor either side"), Radius, 1);

	const TArray<int32> OnFive = { 5 };
	TestTrue(TEXT("your own floor"), FRule::IsFloorLit(5, OnFive, Radius));
	TestTrue(TEXT("the floor above, up the stair"), FRule::IsFloorLit(6, OnFive, Radius));
	TestTrue(TEXT("the floor below, down the stair"), FRule::IsFloorLit(4, OnFive, Radius));
	TestFalse(TEXT("two floors up is out of the scene"), FRule::IsFloorLit(7, OnFive, Radius));
	TestFalse(TEXT("two floors down is out of the scene"), FRule::IsFloorLit(3, OnFive, Radius));
	TestFalse(TEXT("the Skydeck from floor 5"), FRule::IsFloorLit(15, OnFive, Radius));

	// The reception is double height (0 and 1), the crown's atrium too (16 and 17).
	TestTrue(TEXT("floor 1's fittings light the reception void"), FRule::IsFloorLit(1, TArray<int32>{ 0 }, Radius));
	TestTrue(TEXT("floor 17's fittings light the atrium"), FRule::IsFloorLit(17, TArray<int32>{ 16 }, Radius));

	// Split screen: the union of every local viewer's floors.
	const TArray<int32> Split = { 2, 11 };
	TestTrue(TEXT("first viewer's floor"), FRule::IsFloorLit(3, Split, Radius));
	TestTrue(TEXT("second viewer's floor"), FRule::IsFloorLit(10, Split, Radius));
	TestFalse(TEXT("between the two, nobody's"), FRule::IsFloorLit(6, Split, Radius));

	// Nobody looking: nothing is hidden.
	TestTrue(TEXT("no viewers leaves everything lit"), FRule::IsFloorLit(9, TArray<int32>(), Radius));

	// The radius.
	TestFalse(TEXT("radius 0: the floor above is out"), FRule::IsFloorLit(6, OnFive, 0));
	TestTrue(TEXT("radius 0: your own floor stays"), FRule::IsFloorLit(5, OnFive, 0));
	TestTrue(TEXT("radius 2 reaches two floors"), FRule::IsFloorLit(7, OnFive, 2));
	TestFalse(TEXT("a negative radius is radius 0"), FRule::IsFloorLit(6, OnFive, -3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFloorLightingPanelTest, "OneBraham.World.FloorLighting.IsLightLit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFloorLightingPanelTest::RunTest(const FString& Parameters)
{
	using FRule = FOneBrahamFloorLightingRule;
	const int32 Radius = FRule::DefaultRadius;
	const TArray<int32> OnFive = { 5 };

	// A ceiling panel through a closed slab: only on its own floor.
	TestTrue(TEXT("your own floor's panels"), FRule::IsLightLit(5, true, 0u, OnFive, Radius));
	TestFalse(TEXT("the floor above's panels, behind your ceiling"), FRule::IsLightLit(6, true, 0u, OnFive, Radius));
	TestFalse(TEXT("the floor below's panels, under your floor"), FRule::IsLightLit(4, true, 0u, OnFive, Radius));

	// A bulkhead or a lift-hall lamp, seen up the stairwell or the shaft: the plain rule.
	TestTrue(TEXT("a stair bulkhead on the floor above"), FRule::IsLightLit(6, false, 0u, OnFive, Radius));
	TestTrue(TEXT("a stair bulkhead on the floor below"), FRule::IsLightLit(4, false, 0u, OnFive, Radius));
	TestFalse(TEXT("but not two floors away"), FRule::IsLightLit(7, false, 0u, OnFive, Radius));

	// The double-height reception: floor 1's panels open to floor 0, and the ground floor's to 1.
	TestTrue(TEXT("floor 1's panels over the reception void, from the ground floor"),
		FRule::IsLightLit(1, true, FRule::FloorBit(0), TArray<int32>{ 0 }, Radius));
	TestTrue(TEXT("the reception's panels from the floor 1 gallery"),
		FRule::IsLightLit(0, true, FRule::FloorBit(1), TArray<int32>{ 1 }, Radius));
	TestFalse(TEXT("floor 1's panels from floor 2, which is not open to it"),
		FRule::IsLightLit(1, true, FRule::FloorBit(0), TArray<int32>{ 2 }, Radius));
	TestFalse(TEXT("an open floor is still bound by the radius"),
		FRule::IsLightLit(17, true, FRule::FloorBit(15), TArray<int32>{ 15 }, Radius));

	// Split screen and nobody looking.
	TestTrue(TEXT("a second viewer on the panel's floor lights it"), FRule::IsLightLit(6, true, 0u, TArray<int32>{ 5, 6 }, Radius));
	TestTrue(TEXT("no viewers leaves everything lit"), FRule::IsLightLit(9, true, 0u, TArray<int32>(), Radius));

	// The mask.
	TestEqual(TEXT("floor 0's bit"), FRule::FloorBit(0), 1u);
	TestEqual(TEXT("floor 17's bit"), FRule::FloorBit(17), 1u << 17);
	TestEqual(TEXT("no bit below the ground floor"), FRule::FloorBit(-1), 0u);
	TestEqual(TEXT("no bit past 31"), FRule::FloorBit(32), 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFloorLightingShadowTest, "OneBraham.World.FloorLighting.NeedsShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFloorLightingShadowTest::RunTest(const FString& Parameters)
{
	using FRule = FOneBrahamFloorLightingRule;
	const int32 Radius = FRule::DefaultRadius;
	const TArray<int32> OnFive = { 5 };

	// A ceiling panel, aimed at the floor.
	TestTrue(TEXT("your own floor's fittings draw the cover's shadows"), FRule::NeedsShadow(5, true, OnFive, Radius));
	TestTrue(TEXT("the floor above's fittings would shine through your ceiling"), FRule::NeedsShadow(6, true, OnFive, Radius));
	TestFalse(TEXT("the floor below's fittings cannot reach you"), FRule::NeedsShadow(4, true, OnFive, Radius));

	// Anything that can shine upward keeps it: a bulkhead below you lights your floor through the slab.
	TestTrue(TEXT("a point light below keeps its shadow"), FRule::NeedsShadow(4, false, OnFive, Radius));

	// Split screen: one viewer below the light is enough to need it.
	TestTrue(TEXT("a viewer on the floor below needs the shadow even if another is above"),
		FRule::NeedsShadow(4, true, TArray<int32>{ 3, 5 }, Radius));
	TestFalse(TEXT("two viewers above it, neither does"), FRule::NeedsShadow(4, true, TArray<int32>{ 5, 5 }, Radius));

	// Nobody looking: as authored.
	TestTrue(TEXT("no viewers keeps every shadow"), FRule::NeedsShadow(4, true, TArray<int32>(), Radius));

	// The reception void: from floor 1 the ground floor below is seen without its fittings' shadows.
	TestFalse(TEXT("ground floor fittings from the floor 1 gallery"), FRule::NeedsShadow(0, true, TArray<int32>{ 1 }, Radius));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFloorLightingLevelFloorTest, "OneBraham.World.FloorLighting.LevelFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFloorLightingLevelFloorTest::RunTest(const FString& Parameters)
{
	using FRule = FOneBrahamFloorLightingRule;

	// Floor 5's level: its ceiling panels and lobby bulkheads on 5, the two
	// mid-landing bulkheads 60 cm into floor 6's storey. All of them are 5's.
	const float LandingBulkheadZ = OBCameraZ(5, OneBrahamConstants::FloorHeightCm * 0.5f + 260.f);
	TestEqual(TEXT("the landing bulkhead's own Z is the next storey"), OneBrahamConstants::FloorIndexForZ(LandingBulkheadZ), 6);
	TestEqual(TEXT("but it belongs to the level's floor"),
		FRule::LevelFloor(TArray<int32>{ 5, 5, 5, 5, 6, 5, 5, 6, 5 }), 5);

	TestEqual(TEXT("a level of one light"), FRule::LevelFloor(TArray<int32>{ 12 }), 12);
	TestEqual(TEXT("a tie goes to the lower floor"), FRule::LevelFloor(TArray<int32>{ 8, 7 }), 7);
	TestEqual(TEXT("no lights, no floor"), FRule::LevelFloor(TArray<int32>()), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFloorLightingTagTest, "OneBraham.World.FloorLighting.Tag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFloorLightingTagTest::RunTest(const FString& Parameters)
{
	// build_environment.FLOOR_LIGHT_TAG writes these; renaming one side alone
	// leaves every light in the building on, silently.
	TestEqual(TEXT("the tag the content build writes"), UOneBrahamFloorLightingSubsystem::FloorLightTag,
		FName(TEXT("OB.FloorLight")));
	TestEqual(TEXT("the open-to prefix it writes (build_environment.FLOOR_LIGHT_OPEN_TO)"),
		FString(UOneBrahamFloorLightingSubsystem::OpenToTagPrefix), FString(TEXT("OB.FloorLight.OpenTo.")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
