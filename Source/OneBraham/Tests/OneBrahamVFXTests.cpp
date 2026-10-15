// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.VFX.*
//
// The three VFX rules that broke silently, each for a long time, because
// nothing about them shows up as an error:
//   * decals were projected along a yaw-only rotation, so every bullet hole on
//     a floor or a ceiling was projected sideways into nothing;
//   * the content build creates every Niagara system empty, and an empty system
//     "spawns" successfully - so every impact above the Low preset was invisible;
//   * ricochets must stay a grazing-angle, hard-surface event, or they become
//     a sparkler on every carpet in the building.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "NiagaraSystem.h"
#include "OneBraham.h"
#include "VFX/OneBrahamVFXStatics.h"
#include "VFX/OneBrahamVFXSubsystem.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  Decal orientation
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVFXDecalOrientationTest, "OneBraham.VFX.DecalOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVFXDecalOrientationTest::RunTest(const FString& Parameters)
{
	// A decal projects along its local +X, so +X must point into the surface -
	// on a floor, a wall, a ceiling and anything in between.
	const FVector Normals[] = {
		FVector::UpVector,							// floor
		FVector::DownVector,						// ceiling
		FVector(1.f, 0.f, 0.f),						// wall
		FVector(0.f, -1.f, 0.f),					// wall
		FVector(0.4f, -0.3f, 0.86f).GetSafeNormal()	// ramp
	};

	for (const FVector& Normal : Normals)
	{
		const FRotator Round = UOneBrahamVFXSubsystem::DecalRotationForSurface(Normal, FVector::ZeroVector, 37.f);
		TestTrue(*FString::Printf(TEXT("round decal on %s projects into the surface"), *Normal.ToCompactString()),
			FRotationMatrix(Round).GetUnitAxis(EAxis::X).Equals(-Normal, 0.01f));

		// A streaked splatter: still into the surface, and its long axis (Y)
		// follows the shot as projected onto the surface.
		const FVector Shot = FVector(0.7f, 0.5f, -0.5f).GetSafeNormal();
		const FRotator Streak = UOneBrahamVFXSubsystem::DecalRotationForSurface(Normal, Shot, 0.f);
		const FRotationMatrix Axes(Streak);
		const FVector Along = FVector::VectorPlaneProject(Shot, Normal).GetSafeNormal();

		TestTrue(*FString::Printf(TEXT("streaked decal on %s projects into the surface"), *Normal.ToCompactString()),
			Axes.GetUnitAxis(EAxis::X).Equals(-Normal, 0.01f));
		TestTrue(*FString::Printf(TEXT("streaked decal on %s runs along the shot"), *Normal.ToCompactString()),
			Along.IsNearlyZero() || Axes.GetUnitAxis(EAxis::Y).Equals(Along, 0.01f));
	}

	// The old bug, pinned: a floor decal's rotation is not a yaw-only rotation.
	const FRotator Floor = UOneBrahamVFXSubsystem::DecalRotationForSurface(FVector::UpVector, FVector::ZeroVector, 0.f);
	TestTrue(TEXT("a floor decal is pitched to face down, not left level"), FMath::IsNearlyEqual(Floor.Pitch, -90.f, 0.5f));

	return true;
}

// ===========================================================================
//  Empty Niagara systems
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVFXEmptySystemTest, "OneBraham.VFX.EmptySystemIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVFXEmptySystemTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no system is not usable"), UOneBrahamVFXStatics::IsNiagaraSystemUsable(nullptr));

	// What build_vfx.py creates: a named system with no emitters. It must route
	// to the procedural fallback, not "spawn" and draw nothing.
	UNiagaraSystem* Empty = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("a transient system can be made"), Empty);
	if (Empty != nullptr)
	{
		TestEqual(TEXT("the generated system has no emitters"), Empty->GetEmitterHandles().Num(), 0);
		TestFalse(TEXT("a system with no emitters is not usable"), UOneBrahamVFXStatics::IsNiagaraSystemUsable(Empty));
	}

	return true;
}

// ===========================================================================
//  Ricochet
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVFXRicochetTest, "OneBraham.VFX.Ricochet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVFXRicochetTest::RunTest(const FString& Parameters)
{
	const FVector Wall(1.f, 0.f, 0.f);
	const FVector HeadOn(-1.f, 0.f, 0.f);
	const FVector Grazing = FVector(-0.1f, 1.f, 0.f).GetSafeNormal();	// about 6 degrees off the wall

	TestEqual(TEXT("a head-on round never ricochets"),
		UOneBrahamVFXStatics::RicochetChance(EOneBrahamFootstepSurface::Metal, HeadOn, Wall), 0.f);

	const float Metal = UOneBrahamVFXStatics::RicochetChance(EOneBrahamFootstepSurface::Metal, Grazing, Wall);
	const float Concrete = UOneBrahamVFXStatics::RicochetChance(EOneBrahamFootstepSurface::Concrete, Grazing, Wall);

	TestTrue(TEXT("a grazing round can ricochet off metal"), Metal > 0.f && Metal <= 1.f);
	TestTrue(TEXT("concrete is less lively than metal"), Concrete > 0.f && Concrete < Metal);

	TestEqual(TEXT("carpet never ricochets"),
		UOneBrahamVFXStatics::RicochetChance(EOneBrahamFootstepSurface::Carpet, Grazing, Wall), 0.f);
	TestEqual(TEXT("plasterboard never ricochets"),
		UOneBrahamVFXStatics::RicochetChance(EOneBrahamFootstepSurface::Plasterboard, Grazing, Wall), 0.f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
