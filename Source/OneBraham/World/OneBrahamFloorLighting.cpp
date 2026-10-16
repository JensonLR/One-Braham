// Copyright (c) One Braham. All Rights Reserved.

#include "World/OneBrahamFloorLighting.h"

#include "OneBraham.h"
#include "Components/LocalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

const FName UOneBrahamFloorLightingSubsystem::FloorLightTag(TEXT("OB.FloorLight"));
const TCHAR* UOneBrahamFloorLightingSubsystem::OpenToTagPrefix = TEXT("OB.FloorLight.OpenTo.");

namespace
{
	TAutoConsoleVariable<int32> CVarOBFloorLighting(
		TEXT("ob.FloorLighting"),
		1,
		TEXT("1 (default): only the local lights within ob.FloorLighting.Radius floors of a local player are in the scene. ")
		TEXT("0: every OB.FloorLight-tagged light as the level authored it."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarOBFloorLightingRadius(
		TEXT("ob.FloorLighting.Radius"),
		FOneBrahamFloorLightingRule::DefaultRadius,
		TEXT("Floors either side of a local player's camera whose lights stay lit (default 1)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarOBFloorLightingTrimShadows(
		TEXT("ob.FloorLighting.TrimShadows"),
		1,
		TEXT("1 (default): down-facing fittings on the floor below a viewer drop their shadows (nothing for them to cast onto that ")
		TEXT("anyone can see). 0: every managed light keeps the shadow the level authored."),
		ECVF_Default);

	/** A rect light, or a spot light, aimed at the floor: it cannot light anything above its own plane. */
	bool IsDownFacing(const ULocalLightComponent* Light)
	{
		const bool bHemisphere = Light->IsA<URectLightComponent>() || Light->IsA<USpotLightComponent>();
		return bHemisphere && Light->GetForwardVector().Z < -0.9f;
	}

	FAutoConsoleCommandWithWorld OBFloorLightingDumpCommand(
		TEXT("ob.FloorLighting.Dump"),
		TEXT("Logs the floor-lighting subsystem's managed and lit light counts and the viewer floors."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			const UOneBrahamFloorLightingSubsystem* Subsystem =
				World ? World->GetSubsystem<UOneBrahamFloorLightingSubsystem>() : nullptr;
			if (Subsystem == nullptr)
			{
				UE_LOG(LogOneBraham, Display, TEXT("FloorLighting: no subsystem in this world"));
				return;
			}

			TArray<FString> Floors;
			for (const int32 Floor : Subsystem->GetViewerFloors())
			{
				Floors.Add(FString::FromInt(Floor));
			}

			UE_LOG(LogOneBraham, Display, TEXT("FloorLighting: %d of %d managed lights lit; viewers on floor(s) [%s]"),
				Subsystem->GetLitLightCount(), Subsystem->GetManagedLightCount(), *FString::Join(Floors, TEXT(", ")));
		}));

#if !UE_BUILD_SHIPPING
	/** Where ob.Viewpoint asked the first local player to be. Applied by
	 *  whichever game world's subsystem first has a pawn to move. */
	struct FOBPendingViewpoint
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		bool bPending = false;
		bool bApplied = false;
	};

	TAutoConsoleVariable<int32> CVarOBViewpointHold(
		TEXT("ob.Viewpoint.Hold"),
		0,
		TEXT("1: keep the camera turned the way ob.Viewpoint set it (a -OBSmoke run then shoots and times one view). Non-Shipping."),
		ECVF_Default);

	FOBPendingViewpoint GOBPendingViewpoint;

	FAutoConsoleCommand OBViewpointCommand(
		TEXT("ob.Viewpoint"),
		TEXT("ob.Viewpoint X Y Z Yaw [Pitch] - moves the first local player's pawn there (now, or as soon as it has one) ")
		TEXT("and turns the camera. Non-Shipping; for aiming -OBSmoke runs at a room through -ExecCmds."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 4)
			{
				UE_LOG(LogOneBraham, Warning, TEXT("ob.Viewpoint X Y Z Yaw [Pitch]"));
				return;
			}

			GOBPendingViewpoint.Location = FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
			GOBPendingViewpoint.Rotation = FRotator(Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.f, FCString::Atof(*Args[3]), 0.f);
			GOBPendingViewpoint.bPending = true;
		}));
#endif
}

// ===========================================================================
//  The rule
// ===========================================================================

int32 FOneBrahamFloorLightingRule::ViewerFloor(float CameraZ, int32 PreviousFloor, float Hysteresis)
{
	const int32 Raw = FMath::Clamp(OneBrahamConstants::FloorIndexForZ(CameraZ), 0, ONE_BRAHAM_FLOORS);

	if (PreviousFloor == INDEX_NONE || Raw == PreviousFloor)
	{
		return Raw;
	}

	// Still within the hysteresis band round the previous floor: stay on it.
	// The band is measured on the unclamped storey, so the plaza below floor 0
	// and the sky above the roof behave like any other boundary.
	const float Bottom = OneBrahamConstants::GroundFloorZ + PreviousFloor * OneBrahamConstants::FloorHeightCm;
	const float Top = Bottom + OneBrahamConstants::FloorHeightCm;
	const float Band = FMath::Max(0.f, Hysteresis);

	if (CameraZ >= Bottom - Band && CameraZ < Top + Band)
	{
		return PreviousFloor;
	}

	return Raw;
}

int32 FOneBrahamFloorLightingRule::LevelFloor(TConstArrayView<int32> LightFloors)
{
	int32 Best = INDEX_NONE;
	int32 BestCount = 0;
	for (const int32 Candidate : LightFloors)
	{
		int32 Count = 0;
		for (const int32 Floor : LightFloors)
		{
			Count += (Floor == Candidate) ? 1 : 0;
		}

		if (Count > BestCount || (Count == BestCount && Candidate < Best))
		{
			Best = Candidate;
			BestCount = Count;
		}
	}
	return Best;
}

bool FOneBrahamFloorLightingRule::IsFloorLit(int32 LightFloor, TConstArrayView<int32> ViewerFloors, int32 Radius)
{
	if (ViewerFloors.Num() == 0)
	{
		return true;
	}

	const int32 Reach = FMath::Max(0, Radius);
	for (const int32 Viewer : ViewerFloors)
	{
		if (FMath::Abs(LightFloor - Viewer) <= Reach)
		{
			return true;
		}
	}

	return false;
}

bool FOneBrahamFloorLightingRule::IsLightLit(int32 LightFloor, bool bDownFacing, uint32 OpenToFloors,
	TConstArrayView<int32> ViewerFloors, int32 Radius)
{
	if (ViewerFloors.Num() == 0)
	{
		return true;
	}

	const int32 Reach = FMath::Max(0, Radius);
	for (const int32 Viewer : ViewerFloors)
	{
		if (FMath::Abs(LightFloor - Viewer) > Reach)
		{
			continue;
		}

		if (LightFloor == Viewer || !bDownFacing || (OpenToFloors & FloorBit(Viewer)) != 0)
		{
			return true;
		}
	}

	return false;
}

bool FOneBrahamFloorLightingRule::NeedsShadow(int32 LightFloor, bool bDownFacing, TConstArrayView<int32> ViewerFloors, int32 Radius)
{
	// Anything that can shine upward keeps its shadow: unshadowed, a lift-hall
	// downlight or a stair bulkhead lights the floor above straight through
	// the slab. So does everything while nobody is looking.
	if (!bDownFacing || ViewerFloors.Num() == 0)
	{
		return true;
	}

	// A down-facing fitting needs its shadow for a viewer on its own floor
	// (the cover it draws) and for one on the floor below (the slab it would
	// otherwise shine straight through). A viewer above it sees its floor
	// only down a stair or a void, and nothing it could leak onto.
	const int32 Reach = FMath::Max(0, Radius);
	for (const int32 Viewer : ViewerFloors)
	{
		if (LightFloor >= Viewer && LightFloor - Viewer <= Reach)
		{
			return true;
		}
	}

	return false;
}

// ===========================================================================
//  The subsystem
// ===========================================================================

bool UOneBrahamFloorLightingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || IsRunningDedicatedServer())
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->GetNetMode() != NM_DedicatedServer;
}

bool UOneBrahamFloorLightingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOneBrahamFloorLightingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// The floors are always-loaded streaming levels: normally in before
	// BeginPlay, but a level added later (or removed) is picked up too.
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UOneBrahamFloorLightingSubsystem::OnLevelChanged);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UOneBrahamFloorLightingSubsystem::OnLevelChanged);

	bLightsDirty = true;
	bActive = true;
}

void UOneBrahamFloorLightingSubsystem::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);

	RestoreAuthoredVisibility();
	Lights.Reset();
	bActive = false;

	Super::Deinitialize();
}

TStatId UOneBrahamFloorLightingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamFloorLightingSubsystem, STATGROUP_Tickables);
}

void UOneBrahamFloorLightingSubsystem::OnLevelChanged(ULevel* Level, UWorld* World)
{
	if (World == GetWorld())
	{
		bLightsDirty = true;
	}
}

void UOneBrahamFloorLightingSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if !UE_BUILD_SHIPPING
	ApplyPendingViewpoint();
#endif

	const bool bEnabled = CVarOBFloorLighting.GetValueOnGameThread() != 0;
	const int32 Radius = FMath::Max(0, CVarOBFloorLightingRadius.GetValueOnGameThread());
	const bool bTrimShadows = CVarOBFloorLightingTrimShadows.GetValueOnGameThread() != 0;

	bool bRegathered = false;
	if (bLightsDirty)
	{
		GatherLights();
		bLightsDirty = false;
		bRegathered = true;
	}

	if (!bEnabled)
	{
		if (bAppliedEnabled || bRegathered)
		{
			RestoreAuthoredVisibility();
			AppliedViewerFloors.Reset();
			bAppliedEnabled = false;
		}
		return;
	}

	TArray<int32> ViewerFloors;
	CollectViewerFloors(ViewerFloors);

	if (!bRegathered && bAppliedEnabled && Radius == AppliedRadius && bTrimShadows == bAppliedTrimShadows
		&& ViewerFloors == AppliedViewerFloors)
	{
		return;
	}

	ApplyVisibility(ViewerFloors, Radius, bTrimShadows);
	AppliedViewerFloors = MoveTemp(ViewerFloors);
	AppliedRadius = Radius;
	bAppliedTrimShadows = bTrimShadows;
	bAppliedEnabled = true;
}

void UOneBrahamFloorLightingSubsystem::GatherLights()
{
	// Lights already managed go back to how they were authored first: a
	// regather must not mistake a light this hid for one the level hid.
	RestoreAuthoredVisibility();
	Lights.Reset();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TSet<int32> Floors;
	for (const ULevel* Level : World->GetLevels())
	{
		if (Level == nullptr || !Level->bIsVisible)
		{
			continue;
		}

		// Each level's lights take the level's floor (LevelFloor), found once
		// they are all in.
		const int32 FirstOfLevel = Lights.Num();
		TArray<int32> LightFloors;

		for (const AActor* Actor : Level->Actors)
		{
			if (Actor == nullptr || !Actor->ActorHasTag(FloorLightTag))
			{
				continue;
			}

			uint32 OpenToFloors = 0;
			for (const FName& Tag : Actor->Tags)
			{
				const FString TagString = Tag.ToString();
				if (TagString.StartsWith(OpenToTagPrefix))
				{
					const int32 OpenFloor = FCString::Atoi(*TagString.RightChop(FCString::Strlen(OpenToTagPrefix)));
					OpenToFloors |= FOneBrahamFloorLightingRule::FloorBit(OpenFloor);
				}
			}

			TInlineComponentArray<ULocalLightComponent*> Components(Actor);
			for (ULocalLightComponent* Light : Components)
			{
				FManagedLight& Managed = Lights.AddDefaulted_GetRef();
				Managed.Light = Light;
				Managed.Floor = FMath::Clamp(OneBrahamConstants::FloorIndexForZ(Light->GetComponentLocation().Z),
					0, ONE_BRAHAM_FLOORS);
				Managed.bAuthoredVisible = Light->IsVisible();
				Managed.bAuthoredShadows = Light->CastShadows;
				Managed.bDownFacing = IsDownFacing(Light);
				Managed.OpenToFloors = OpenToFloors;
				LightFloors.Add(Managed.Floor);
			}
		}

		const int32 FloorOfLevel = FOneBrahamFloorLightingRule::LevelFloor(LightFloors);
		for (int32 Index = FirstOfLevel; Index < Lights.Num(); ++Index)
		{
			Lights[Index].Floor = FloorOfLevel;
		}
		if (FloorOfLevel != INDEX_NONE)
		{
			Floors.Add(FloorOfLevel);
		}
	}

	UE_LOG(LogOneBraham, Log, TEXT("FloorLighting: %d tagged light(s) over %d floor(s)"), Lights.Num(), Floors.Num());
}

void UOneBrahamFloorLightingSubsystem::CollectViewerFloors(TArray<int32>& OutFloors)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TArray<TPair<TWeakObjectPtr<APlayerController>, int32>> NextHistory;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC == nullptr || !PC->IsLocalController() || PC->PlayerCameraManager == nullptr)
		{
			continue;
		}

		int32 Previous = INDEX_NONE;
		for (const TPair<TWeakObjectPtr<APlayerController>, int32>& Entry : ViewerHistory)
		{
			if (Entry.Key.Get() == PC)
			{
				Previous = Entry.Value;
				break;
			}
		}

		FVector Location;
		FRotator Rotation;
		PC->GetPlayerViewPoint(Location, Rotation);

		const int32 Floor = FOneBrahamFloorLightingRule::ViewerFloor(static_cast<float>(Location.Z), Previous);
		NextHistory.Emplace(PC, Floor);
		OutFloors.AddUnique(Floor);
	}

	OutFloors.Sort();
	ViewerHistory = MoveTemp(NextHistory);
}

void UOneBrahamFloorLightingSubsystem::ApplyVisibility(TConstArrayView<int32> ViewerFloors, int32 Radius, bool bTrimShadows)
{
	for (const FManagedLight& Managed : Lights)
	{
		ULocalLightComponent* Light = Managed.Light.Get();
		if (Light == nullptr)
		{
			continue;
		}

		const bool bWanted = Managed.bAuthoredVisible
			&& FOneBrahamFloorLightingRule::IsLightLit(Managed.Floor, Managed.bDownFacing, Managed.OpenToFloors, ViewerFloors,
				Radius);

		// Shadows first, so a light coming back into the scene is created
		// once, with the shadow it needs, rather than twice.
		const bool bShadow = Managed.bAuthoredShadows && (!bTrimShadows
			|| FOneBrahamFloorLightingRule::NeedsShadow(Managed.Floor, Managed.bDownFacing, ViewerFloors, Radius));

		if (Light->CastShadows != bShadow)
		{
			Light->SetCastShadows(bShadow);
		}

		if (Light->IsVisible() != bWanted)
		{
			Light->SetVisibility(bWanted);
		}
	}
}

void UOneBrahamFloorLightingSubsystem::RestoreAuthoredVisibility()
{
	for (const FManagedLight& Managed : Lights)
	{
		ULocalLightComponent* Light = Managed.Light.Get();
		if (Light == nullptr)
		{
			continue;
		}

		if (Light->CastShadows != Managed.bAuthoredShadows)
		{
			Light->SetCastShadows(Managed.bAuthoredShadows);
		}

		if (Light->IsVisible() != Managed.bAuthoredVisible)
		{
			Light->SetVisibility(Managed.bAuthoredVisible);
		}
	}
}

int32 UOneBrahamFloorLightingSubsystem::GetLitLightCount() const
{
	int32 Lit = 0;
	for (const FManagedLight& Managed : Lights)
	{
		const ULocalLightComponent* Light = Managed.Light.Get();
		Lit += (Light != nullptr && Light->IsVisible()) ? 1 : 0;
	}
	return Lit;
}

#if !UE_BUILD_SHIPPING
void UOneBrahamFloorLightingSubsystem::ApplyPendingViewpoint()
{
	if (!GOBPendingViewpoint.bPending)
	{
		if (GOBPendingViewpoint.bApplied && CVarOBViewpointHold.GetValueOnGameThread() != 0)
		{
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				PC->SetControlRotation(GOBPendingViewpoint.Rotation);
			}
		}
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (Pawn == nullptr || !PC->IsLocalController())
	{
		return;
	}

	GOBPendingViewpoint.bPending = false;
	GOBPendingViewpoint.bApplied = true;
	Pawn->SetActorLocation(GOBPendingViewpoint.Location, false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(GOBPendingViewpoint.Rotation);

	UE_LOG(LogOneBraham, Display, TEXT("ob.Viewpoint: %s moved to %s facing yaw %.0f"), *Pawn->GetName(),
		*GOBPendingViewpoint.Location.ToCompactString(), GOBPendingViewpoint.Rotation.Yaw);
}
#endif
