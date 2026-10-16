// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFloorLighting.h
// ---------------------------------------------------------------------------
// Lights on the floors nobody can see are switched off.
//
// WHY
//   Every floor of the tower is loaded at once (link_floor_levels: always
//   loaded, so the stairwells and the atrium never pop), and every floor's
//   ceiling grid, stair bulkheads and lift-hall downlights were live in every
//   frame: 874 local lights in the building (1,531 with the old per-fitting
//   bounce lights), every one within draw distance in the renderer's light
//   grid and lit into Lumen's surface cache. On an office floor at Epic that
//   was 385 lights in the view's light grid and 530 in Lumen's direct
//   lighting; with this subsystem (and no bounce lights) it is 53 and 69.
//   A player only ever sees their own floor, the floors directly above and
//   below (the stairs, the double-height reception, the crown's atrium) and
//   the outside.
//
// WHAT
//   A world subsystem, client-side only. The content build tags each light it
//   places on a floor with OB.FloorLight (build_environment._tag_floor_lights);
//   the subsystem finds them when a level is added, groups them by floor
//   (each floor level's, by OneBrahamConstants::FloorIndexForZ of most of its
//   lights - see LevelFloor), and keeps lit only the lights within
//   Radius floors of a local player's camera. The rest are hidden with
//   SetVisibility(false), which takes the light out of the scene - out of the
//   light grid, the deferred passes, the shadow maps and Lumen - not just dark.
//
//   Ceiling panels (down-facing rect lights) are kept tighter still: one on
//   another floor is lit only where the two floors share a space - the
//   double-height reception, the crown - which the build marks with
//   OB.FloorLight.OpenTo.<floor> tags. Through a slab, the floor above's
//   panels are 40-odd shadowed lights over every pixel of the viewer's floor,
//   lighting nothing it can see (and overflowing VSM one-pass projection).
//
//   The fittings on the floor below a viewer also drop their shadows: a ceiling
//   panel aims at the floor and cannot light anything above its own slab, so
//   its shadow there only costs (a cube of virtual shadow map pages, and the
//   distance-field shadow Lumen traces for it off screen). Anything that can
//   shine upward - a point light - keeps its shadow everywhere.
//
//   Untagged lights are never touched: everything outside the floor levels
//   (the sun, the skylight, the extraction beacon, anything the district or
//   the Skydeck adds to the persistent level) and every light a gameplay actor
//   carries (lift cars, readers, pickups, weapon lights).
//
// COST
//   Per frame: one floor index per local player. The light list is walked only
//   when a viewer changes floor (a stair, a lift) or a level comes or goes.
//
// THE CAMERA ON A STAIR
//   The viewer's floor has a little hysteresis (HysteresisCm): a camera
//   bobbing on a landing at the boundary does not flick two floors of lights
//   on and off every step.
//
// NETWORKING
//   None. Visibility is cosmetic and local; a dedicated server has no view and
//   never creates the subsystem. A listen server lights for its own player.
//
// CONSOLE
//   ob.FloorLighting 0|1          off = every tagged light as authored
//   ob.FloorLighting.Radius N     floors either side of a viewer (default 1)
//   ob.FloorLighting.TrimShadows  0 = keep the shadows of fittings below a viewer
//   ob.FloorLighting.Dump         managed / lit counts and the viewer floors
//   ob.Viewpoint X Y Z Yaw [Pitch]  (non-Shipping) puts the first local player
//                                 there as soon as it has a pawn: with
//                                 -ExecCmds it aims a -OBSmoke run at a room
//   ob.Viewpoint.Hold 1           (non-Shipping) keeps it facing that way, so
//                                 every smoke shot and frame is the one view

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamFloorLighting.generated.h"

class APlayerController;
class ULevel;
class ULocalLightComponent;

/** The rule, kept pure so it can be tested without a world. */
struct ONEBRAHAM_API FOneBrahamFloorLightingRule
{
	/** Floors either side of a viewer that stay lit: their own, and the ones
	 *  seen through a stair, the reception void or the atrium. */
	static constexpr int32 DefaultRadius = 1;

	/** How far past a floor boundary the camera must go before it counts as
	 *  being on the next floor. A camera at eye height on a stair landing
	 *  sits close to a boundary for whole seconds. */
	static constexpr float HysteresisCm = 60.f;

	/** The floor a camera at CameraZ is on, given the floor it was on last
	 *  (INDEX_NONE for none). Clamped to the building's floors 0..ONE_BRAHAM_FLOORS:
	 *  a camera on the plaza or above the roof lights the nearest floors. */
	static int32 ViewerFloor(float CameraZ, int32 PreviousFloor, float Hysteresis = HysteresisCm);

	/** Whether a light on LightFloor is lit for these viewers. No viewers at
	 *  all (a loading screen, a world with no local player yet) leaves every
	 *  light lit: nothing is hidden until someone is looking. */
	static bool IsFloorLit(int32 LightFloor, TConstArrayView<int32> ViewerFloors, int32 Radius);

	/** Whether a lit light needs its shadow. A down-facing fitting (a rect or
	 *  spot light aimed at the floor) cannot reach anything above its own
	 *  floor, so for a viewer above it the shadow only costs: seen down a
	 *  stair or a void, a floor below is lit without its fittings' shadows.
	 *  Anything else, and anything with no viewers, keeps it. */
	static bool NeedsShadow(int32 LightFloor, bool bDownFacing, TConstArrayView<int32> ViewerFloors, int32 Radius);

	/** The floor a floor level's lights belong to: the one most of them are
	 *  on by FloorIndexForZ (ties to the lower), INDEX_NONE for none. A light
	 *  is the floor of the level that placed it, not of its own Z: each
	 *  stair's mid-landing bulkhead hangs 60 cm into the next storey, and
	 *  binned by Z it was lit for the floors 0..+2 of its own stair section
	 *  instead of -1..+1 like everything else the level holds. */
	static int32 LevelFloor(TConstArrayView<int32> LightFloors);

	/** The bit for a floor in an OpenToFloors mask (floors 0..31). */
	static uint32 FloorBit(int32 Floor) { return (Floor >= 0 && Floor < 32) ? (1u << Floor) : 0u; }

	/** Whether a light is in the scene for these viewers: IsFloorLit, and a
	 *  down-facing fitting (a ceiling panel) on another floor only where the
	 *  two floors share a space - OpenToFloors has the viewer's floor's bit.
	 *  Elsewhere a slab stands between them: the fittings above light only
	 *  the far side of the viewer's ceiling, and the ones below the far side
	 *  of the floor, and both still cost a pass over every pixel in reach.
	 *  Lights that are not down-facing (stair bulkheads, lift-hall lamps,
	 *  lit up a stairwell or a lift shaft) keep the plain floor rule. */
	static bool IsLightLit(int32 LightFloor, bool bDownFacing, uint32 OpenToFloors, TConstArrayView<int32> ViewerFloors,
		int32 Radius);
};

/**
 * Keeps the local lights of floors no local player can see out of the scene.
 * One per game world; never on a dedicated server.
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamFloorLightingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The actor tag the content build puts on every managed light. */
	static const FName FloorLightTag;

	/** "OB.FloorLight.OpenTo.<floor>": this light's floor shares a space with
	 *  that floor (the double-height reception, the crown's atrium), so a
	 *  viewer there can see what it lights. One tag per floor. */
	static const TCHAR* OpenToTagPrefix;

	// USubsystem / UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

	/** Tagged lights found in the world. */
	int32 GetManagedLightCount() const { return Lights.Num(); }

	/** Of those, how many are in the scene now. */
	int32 GetLitLightCount() const;

	/** The floors the local players are on, as last applied. */
	const TArray<int32>& GetViewerFloors() const { return AppliedViewerFloors; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FManagedLight
	{
		TWeakObjectPtr<ULocalLightComponent> Light;
		int32 Floor = 0;

		/** Visible as the level authored it. A light the build left hidden stays hidden. */
		bool bAuthoredVisible = true;

		/** Shadowed as the level authored it. Only ever trimmed, never added. */
		bool bAuthoredShadows = true;

		/** A rect or spot light aimed at the floor (see NeedsShadow). */
		bool bDownFacing = false;

		/** FloorBit of every floor this light's floor is open to (see IsLightLit). */
		uint32 OpenToFloors = 0;
	};

	void OnLevelChanged(ULevel* Level, UWorld* World);
	void GatherLights();
	void CollectViewerFloors(TArray<int32>& OutFloors);
	void ApplyVisibility(TConstArrayView<int32> ViewerFloors, int32 Radius, bool bTrimShadows);
	void RestoreAuthoredVisibility();

#if !UE_BUILD_SHIPPING
	void ApplyPendingViewpoint();
#endif

	TArray<FManagedLight> Lights;

	/** Each local player's last floor, for the hysteresis. */
	TArray<TPair<TWeakObjectPtr<APlayerController>, int32>> ViewerHistory;

	TArray<int32> AppliedViewerFloors;
	int32 AppliedRadius = INDEX_NONE;
	bool bAppliedEnabled = false;
	bool bAppliedTrimShadows = false;

	bool bActive = false;
	bool bLightsDirty = true;

	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
};
