// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLift.cpp

#include "Lifts/OneBrahamLift.h"

#include "OneBraham.h"
#include "Lifts/OneBrahamLiftPanel.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Lockdown/OneBrahamLockdownComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Wall thickness of a solid car, and of a glass one. */
	constexpr float OBLiftWallCm = 6.f;
	constexpr float OBLiftGlassWallCm = 3.f;

	/** Car floor slab thickness. Thick enough that a fast fall never tunnels it. */
	constexpr float OBLiftFloorSlabCm = 12.f;

	/** Door leaf thickness, car and landing. */
	constexpr float OBLiftDoorLeafCm = 4.f;

	/** How often the server asks the lockdown director whether lifts may run. */
	constexpr float OBLiftLockdownPollSeconds = 0.25f;

	/** Client clock: a disagreement larger than this snaps rather than glides. */
	constexpr double OBLiftClockSnapSeconds = 0.25;

	/** Client clock: how quickly the smoothed offset follows the replicated one, per second. */
	constexpr double OBLiftClockFollowRate = 1.5;

	/** Height of the call buttons above the floor, and the car pair. */
	constexpr float OBLiftLandingButtonZ = 115.f;
	constexpr float OBLiftCarUpButtonZ = 125.f;
	constexpr float OBLiftCarDownButtonZ = 97.f;

	/** Car interior light: enough to read the buttons, not enough to be a lamp. */
	constexpr float OBLiftCarLightIntensity = 1400.f;

	const FLinearColor OBLiftIndicatorNormal(1.f, 0.62f, 0.08f);
	const FLinearColor OBLiftIndicatorLocked(1.f, 0.08f, 0.05f);

	/** Shapes a linear 0..1 into the slow-fast-slow slide of a real lift door. */
	float OBLiftEaseDoor(float Alpha)
	{
		return FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(Alpha, 0.f, 1.f), 2.f);
	}
}

AOneBrahamLift::AOneBrahamLift()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.TickInterval = 0.f; // Riders need the car where it is every frame.

	bReplicates = true;
	SetReplicateMovement(false); // The trip is replicated, not the position.

	// The actor stands at the bottom of a sixty-metre shaft. Relevancy measured
	// from there would drop the lift for the people at the top who are riding it.
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(2.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	FacingRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FacingRoot"));
	FacingRoot->SetupAttachment(Root);
	FacingRoot->SetMobility(EComponentMobility::Movable);

	CarRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CarRoot"));
	CarRoot->SetupAttachment(FacingRoot);
	CarRoot->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		CubeMesh = CubeFinder.Object;
	}

	auto MakePart = [this](const TCHAR* Name, USceneComponent* Parent) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// The shaft is not navigable and the car is never where the navmesh was
		// built, so nothing here may dirty it - a moving car that did would rebuild
		// navigation tiles every frame of every trip.
		Part->SetCanEverAffectNavigation(false);
		Part->bReceivesDecals = true;
		if (CubeMesh != nullptr)
		{
			Part->SetStaticMesh(CubeMesh);
		}
		return Part;
	};

	CarFloor = MakePart(TEXT("CarFloor"), CarRoot);
	CarCeiling = MakePart(TEXT("CarCeiling"), CarRoot);
	CarWallBack = MakePart(TEXT("CarWallBack"), CarRoot);
	CarWallLeft = MakePart(TEXT("CarWallLeft"), CarRoot);
	CarWallRight = MakePart(TEXT("CarWallRight"), CarRoot);
	CarFrontLeft = MakePart(TEXT("CarFrontLeft"), CarRoot);
	CarFrontRight = MakePart(TEXT("CarFrontRight"), CarRoot);
	CarFrontHeader = MakePart(TEXT("CarFrontHeader"), CarRoot);
	CarDoorLeft = MakePart(TEXT("CarDoorLeft"), CarRoot);
	CarDoorRight = MakePart(TEXT("CarDoorRight"), CarRoot);

	// Riders stand on this, so it must be a base the movement component will
	// follow: movable, and allowed to be stepped onto.
	CarFloor->CanCharacterStepUpOn = ECB_Yes;

	CarLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CarLight"));
	CarLight->SetupAttachment(CarRoot);
	CarLight->SetMobility(EComponentMobility::Movable);
	CarLight->SetIntensity(OBLiftCarLightIntensity);
	CarLight->SetAttenuationRadius(320.f);
	CarLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.82f));
	CarLight->SetCastShadows(false);

	CarIndicator = CreateDefaultSubobject<UTextRenderComponent>(TEXT("CarIndicator"));
	CarIndicator->SetupAttachment(CarRoot);
	CarIndicator->SetMobility(EComponentMobility::Movable);
	CarIndicator->SetHorizontalAlignment(EHTA_Center);
	CarIndicator->SetVerticalAlignment(EVRTA_TextCenter);
	CarIndicator->SetWorldSize(16.f);
	CarIndicator->SetTextRenderColor(OBLiftIndicatorNormal.ToFColor(true));
	CarIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LandingDoors = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LandingDoors"));
	LandingDoors->SetupAttachment(FacingRoot);
	LandingDoors->SetMobility(EComponentMobility::Movable);
	LandingDoors->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	LandingDoors->SetCanEverAffectNavigation(false);
	if (CubeMesh != nullptr)
	{
		LandingDoors->SetStaticMesh(CubeMesh);
	}

	GlassFrame = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GlassFrame"));
	GlassFrame->SetupAttachment(CarRoot);
	GlassFrame->SetMobility(EComponentMobility::Movable);
	GlassFrame->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the glass is the wall
	GlassFrame->SetCanEverAffectNavigation(false);
	if (CubeMesh != nullptr)
	{
		GlassFrame->SetStaticMesh(CubeMesh);
	}
}

// ===========================================================================
//  Builder API
// ===========================================================================

void AOneBrahamLift::ConfigureLift(AOneBrahamLift* Lift, FName InLiftId, int32 InMinFloor, int32 InMaxFloor,
	bool bInGlass, EOneBrahamLiftFacing InFacing)
{
	if (Lift == nullptr)
	{
		return;
	}

	Lift->Modify();
	Lift->LiftId = InLiftId;
	Lift->MinFloor = FMath::Min(InMinFloor, InMaxFloor);
	Lift->MaxFloor = FMath::Max(InMinFloor, InMaxFloor);
	Lift->bGlass = bInGlass;
	Lift->DoorFacing = InFacing;
	Lift->RebuildLift();
}

void AOneBrahamLift::RebuildLift()
{
	BuildLift();
}

FOneBrahamLiftConfig AOneBrahamLift::MakeConfig() const
{
	FOneBrahamLiftConfig Config;
	Config.MinFloor = FMath::Min(MinFloor, MaxFloor);
	Config.MaxFloor = FMath::Max(MinFloor, MaxFloor);
	Config.FloorZ0 = FloorZ0;
	Config.FloorHeightCm = FMath::Max(FloorHeightCm, 100.f);
	Config.SpeedCmPerSec = FMath::Max(SpeedCmPerSec, 50.f);
	Config.AccelCmPerSec2 = FMath::Max(AccelerationCmPerSec2, 20.f);
	Config.DoorMoveSeconds = FMath::Max(DoorMoveSeconds, 0.2f);
	Config.DoorDwellSeconds = FMath::Max(DoorOpenSeconds, 0.5f);
	Config.LockdownExcludedFloor = LockdownExcludedFloor;
	return Config;
}

// ===========================================================================
//  Geometry
// ===========================================================================

float AOneBrahamLift::GetFacingYaw() const
{
	switch (DoorFacing)
	{
	case EOneBrahamLiftFacing::NegativeX: return 180.f;
	case EOneBrahamLiftFacing::NegativeY: return -90.f;
	case EOneBrahamLiftFacing::PositiveY: return 90.f;
	case EOneBrahamLiftFacing::PositiveX:
	default:
		return 0.f;
	}
}

// The door stack, front to back along the facing axis, for a bank shaft
// (car depth 220, hall face 125):
//
//   110        car front wall face
//   111..115   car door leaves
//   117..121   landing door leaves (inside the shaft wall's opening)
//   125        hall face of the shaft wall
//
// and the car floor runs forward to 116 as a sill, so the gap a player steps
// across is a few centimetres, not a drop down the shaft.

float AOneBrahamLift::GetCarDoorX() const
{
	return CarDepthCm * 0.5f + 1.f + OBLiftDoorLeafCm * 0.5f;
}

float AOneBrahamLift::GetLandingDoorX() const
{
	return LandingDoorOffsetCm - 4.f - OBLiftDoorLeafCm * 0.5f;
}

float AOneBrahamLift::GetSillFrontX() const
{
	return FMath::Max(CarDepthCm * 0.5f, GetLandingDoorX() - OBLiftDoorLeafCm * 0.5f - 1.f);
}

void AOneBrahamLift::PlaceBox(UStaticMeshComponent* Mesh, const FVector& Centre, const FVector& Size) const
{
	if (Mesh == nullptr)
	{
		return;
	}

	// The engine cube is 100 cm with its pivot at the centre: size / 100 is the scale.
	const bool bVisible = Size.X > 0.5f && Size.Y > 0.5f && Size.Z > 0.5f;
	Mesh->SetVisibility(bVisible);
	Mesh->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(Centre);
	Mesh->SetRelativeScale3D(Size.ComponentMax(FVector(0.5f)) / 100.f);
}

FTransform AOneBrahamLift::GetLandingDoorTransform(int32 Floor, bool bLeft, float OpenAlpha) const
{
	const float Side = bLeft ? -1.f : 1.f;
	const float LeafWidth = DoorWidthCm * 0.5f + 2.f;
	const float ClosedY = DoorWidthCm * 0.25f;
	const float Y = Side * (ClosedY + OBLiftEaseDoor(OpenAlpha) * DoorWidthCm * 0.5f);
	const float Z = GetFloorWorldZ(Floor) - GetActorLocation().Z + DoorHeightCm * 0.5f;

	return FTransform(FRotator::ZeroRotator, FVector(GetLandingDoorX(), Y, Z),
		FVector(OBLiftDoorLeafCm, LeafWidth, DoorHeightCm) / 100.f);
}

void AOneBrahamLift::BuildLift()
{
	if (MaxFloor < MinFloor)
	{
		Swap(MinFloor, MaxFloor);
	}

	StartFloor = FMath::Clamp(StartFloor, MinFloor, MaxFloor);
	Logic.Config = MakeConfig();

	FacingRoot->SetRelativeRotation(FRotator(0.f, GetFacingYaw(), 0.f));

	const float W = CarWidthCm;
	const float D = CarDepthCm;
	const float H = CarHeightCm;
	const float DW = FMath::Min(DoorWidthCm, W - 20.f);
	const float DH = FMath::Min(DoorHeightCm, H - 10.f);
	const float T = bGlass ? OBLiftGlassWallCm : OBLiftWallCm;
	const float SillFront = GetSillFrontX();

	if (GetLandingDoorX() - OBLiftDoorLeafCm * 0.5f < GetCarDoorX() + OBLiftDoorLeafCm * 0.5f)
	{
		OB_WARN("Lift '%s': car depth %.0f does not leave room for the doors in front of it (landing door offset %.0f). "
			"Shrink CarDepthCm or raise LandingDoorOffsetCm.", *LiftId.ToString(), D, LandingDoorOffsetCm);
	}

	// --- the car ------------------------------------------------------------
	PlaceBox(CarFloor, FVector((SillFront - D * 0.5f) * 0.5f, 0.f, -OBLiftFloorSlabCm * 0.5f),
		FVector(SillFront + D * 0.5f, W, OBLiftFloorSlabCm));
	PlaceBox(CarCeiling, FVector(0.f, 0.f, H + 4.f), FVector(D, W, 8.f));
	PlaceBox(CarWallBack, FVector(-D * 0.5f + T * 0.5f, 0.f, H * 0.5f), FVector(T, W, H));
	PlaceBox(CarWallLeft, FVector(0.f, -W * 0.5f + T * 0.5f, H * 0.5f), FVector(D, T, H));
	PlaceBox(CarWallRight, FVector(0.f, W * 0.5f - T * 0.5f, H * 0.5f), FVector(D, T, H));

	const float SideWidth = FMath::Max(0.f, W * 0.5f - DW * 0.5f);
	const float FrontX = D * 0.5f - T * 0.5f;
	PlaceBox(CarFrontLeft, FVector(FrontX, -(DW * 0.5f + SideWidth * 0.5f), H * 0.5f), FVector(T, SideWidth, H));
	PlaceBox(CarFrontRight, FVector(FrontX, DW * 0.5f + SideWidth * 0.5f, H * 0.5f), FVector(T, SideWidth, H));
	PlaceBox(CarFrontHeader, FVector(FrontX, 0.f, (DH + H) * 0.5f), FVector(T, DW, H - DH));

	// Leaves are placed closed here and slid by ApplyPresentation.
	PlaceBox(CarDoorLeft, FVector(GetCarDoorX(), -DW * 0.25f, DH * 0.5f), FVector(OBLiftDoorLeafCm, DW * 0.5f + 2.f, DH));
	PlaceBox(CarDoorRight, FVector(GetCarDoorX(), DW * 0.25f, DH * 0.5f), FVector(OBLiftDoorLeafCm, DW * 0.5f + 2.f, DH));

	// --- materials ------------------------------------------------------------
	// Set unconditionally (null restores the mesh default), so turning bGlass off
	// in the editor really does put the steel back.
	// The glass lift is glass all round - walls, the panels either side of the
	// door, the car doors and the landing doors - held in a steel frame. Only
	// the floor, ceiling and door header stay solid.
	const bool bUseGlass = bGlass && GlassMaterial != nullptr;
	UMaterialInterface* WallMaterial = bUseGlass ? GlassMaterial.Get() : CarMaterial.Get();
	UMaterialInterface* LeafMaterial = bUseGlass ? GlassMaterial.Get() : DoorMaterial.Get();
	for (UStaticMeshComponent* Part : { CarFloor.Get(), CarCeiling.Get(), CarFrontHeader.Get() })
	{
		Part->SetMaterial(0, CarMaterial);
	}

	for (UStaticMeshComponent* Part : { CarWallBack.Get(), CarWallLeft.Get(), CarWallRight.Get(),
		CarFrontLeft.Get(), CarFrontRight.Get(), CarDoorLeft.Get(), CarDoorRight.Get() })
	{
		Part->SetMaterial(0, Part == CarDoorLeft.Get() || Part == CarDoorRight.Get() ? LeafMaterial : WallMaterial);
		Part->SetCastShadow(!bUseGlass); // A glass car that throws a solid shadow is a box.
	}

	LandingDoors->SetMaterial(0, LeafMaterial);
	LandingDoors->SetCastShadow(!bUseGlass);

	// --- the glass car's frame --------------------------------------------
	GlassFrame->ClearInstances();
	GlassFrame->SetMaterial(0, CarMaterial);
	if (bGlass)
	{
		const float P = 6.f; // post and rail section, cm
		auto Member = [this](const FVector& Centre, const FVector& Size)
		{
			GlassFrame->AddInstance(FTransform(FRotator::ZeroRotator, Centre, Size / 100.f));
		};
		// Corner posts, and a post either side of the door opening.
		for (const float X : { -D * 0.5f, D * 0.5f })
		{
			for (const float Y : { -W * 0.5f, W * 0.5f })
			{
				Member(FVector(X, Y, H * 0.5f), FVector(P, P, H));
			}
		}
		Member(FVector(D * 0.5f, -DW * 0.5f - P * 0.5f, H * 0.5f), FVector(P, P, H));
		Member(FVector(D * 0.5f, DW * 0.5f + P * 0.5f, H * 0.5f), FVector(P, P, H));
		// Top and bottom rails round all four faces.
		for (const float Z : { P * 0.5f, H - P * 0.5f })
		{
			Member(FVector(-D * 0.5f, 0.f, Z), FVector(P, W, P));
			Member(FVector(D * 0.5f, 0.f, Z), FVector(P, W, P));
			Member(FVector(0.f, -W * 0.5f, Z), FVector(D, P, P));
			Member(FVector(0.f, W * 0.5f, Z), FVector(D, P, P));
		}
		// A mid-height rail on the three closed sides: the handrail line.
		Member(FVector(-D * 0.5f + P, 0.f, 95.f), FVector(P, W - 2.f * P, 4.f));
		Member(FVector(0.f, -W * 0.5f + P, 95.f), FVector(D - 2.f * P, P, 4.f));
		Member(FVector(0.f, W * 0.5f - P, 95.f), FVector(D - 2.f * P, P, 4.f));
	}

	// --- fittings -------------------------------------------------------------
	if (CarLight != nullptr)
	{
		CarLight->SetRelativeLocation(FVector(0.f, 0.f, H - 18.f));
	}
	// Text render components are not created on a dedicated server: the floor
	// indicator is presentation only, and dereferencing it crashed the server
	// on map load.
	if (CarIndicator != nullptr)
	{
		CarIndicator->SetRelativeLocation(FVector(D * 0.5f - T - 1.f, 0.f, DH + (H - DH) * 0.5f));
		CarIndicator->SetRelativeRotation(FRotator(0.f, 180.f, 0.f)); // Read from inside the car.
	}

	// --- landing doors --------------------------------------------------------
	LandingDoors->ClearInstances();
	if (bBuildLandingDoors)
	{
		for (int32 Floor = MinFloor; Floor <= MaxFloor; ++Floor)
		{
			LandingDoors->AddInstance(GetLandingDoorTransform(Floor, /*bLeft*/ true, 0.f));
			LandingDoors->AddInstance(GetLandingDoorTransform(Floor, /*bLeft*/ false, 0.f));
		}
	}

	// --- park the car -----------------------------------------------------------
	CarRoot->SetRelativeLocation(FVector(0.f, 0.f, GetFloorWorldZ(StartFloor) - GetActorLocation().Z));

	AnimatedLandingFloor = INDEX_NONE;
	LastAppliedCarRelZ = TNumericLimits<float>::Max();
	LastAppliedDoorAlpha = -1.f;
	LastLandingDoorAlpha = -1.f;
	LastIndicatorFloor = INDEX_NONE;
}

void AOneBrahamLift::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	BuildLift();
}

// ===========================================================================
//  Lifecycle
// ===========================================================================

void AOneBrahamLift::BeginPlay()
{
	Super::BeginPlay();

	// Rebuilt at runtime as well as in construction: the landing door instances
	// are derived data, and deriving them again here means a cooked level, a
	// streamed level and a level saved by an older builder all agree.
	BuildLift();

	if (HasAuthority())
	{
		Logic.Reset(StartFloor, GetLiftTime());
		LiftState = Logic.GetState();
		SpawnPanels();
		PollLockdown();
	}
	else
	{
		Logic.ApplyReplicatedState(LiftState);
	}

	LastSeenPhase = LiftState.Phase;
	bLastSeenLockedDown = LiftState.bLockedDown;
	ApplyPresentation(GetLiftTime());

	OB_ACCESS_LOG("Lift '%s' online: floors %d-%d, parked at %d, %s, facing %s",
		*LiftId.ToString(), MinFloor, MaxFloor, LiftState.CurrentFloor,
		bGlass ? TEXT("glass") : TEXT("steel"), *UEnum::GetValueAsString(DoorFacing));
}

void AOneBrahamLift::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		for (AOneBrahamLiftPanel* Panel : Panels)
		{
			if (IsValid(Panel))
			{
				Panel->Destroy();
			}
		}
	}

	Panels.Reset();

	Super::EndPlay(EndPlayReason);
}

void AOneBrahamLift::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamLift, LiftState);
}

void AOneBrahamLift::SpawnPanels()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !HasAuthority())
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform Facing = FacingRoot->GetComponentTransform();
	const float ActorZ = GetActorLocation().Z;
	const float DW = FMath::Min(DoorWidthCm, CarWidthCm - 20.f);
	const float T = bGlass ? OBLiftGlassWallCm : OBLiftWallCm;

	// Landing call buttons: on the hall face, beside the door, at every served floor.
	for (int32 Floor = MinFloor; Floor <= MaxFloor; ++Floor)
	{
		const FVector Local(LandingDoorOffsetCm + 4.f, DW * 0.5f + 28.f, GetFloorWorldZ(Floor) - ActorZ + OBLiftLandingButtonZ);
		const FTransform PanelXf(Facing.GetRotation(), Facing.TransformPosition(Local));

		if (AOneBrahamLiftPanel* Panel = World->SpawnActor<AOneBrahamLiftPanel>(AOneBrahamLiftPanel::StaticClass(), PanelXf, Params))
		{
			Panel->InitPanel(this, EOneBrahamLiftPanelRole::Landing, Floor);
			Panels.Add(Panel);
		}
	}

	// The car pair: inside, on the door wall, facing the rider. Attached to the
	// car so it rides with it on every machine.
	const FTransform CarXf = CarRoot->GetComponentTransform();
	const FQuat Inward = CarXf.GetRotation() * FQuat(FVector::UpVector, PI);

	struct FOBCarButton { EOneBrahamLiftPanelRole Role; float Z; };
	const FOBCarButton Buttons[] = {
		{ EOneBrahamLiftPanelRole::CarUp, OBLiftCarUpButtonZ },
		{ EOneBrahamLiftPanelRole::CarDown, OBLiftCarDownButtonZ },
	};

	for (const FOBCarButton& Button : Buttons)
	{
		const FVector Local(CarDepthCm * 0.5f - T - 3.f, DW * 0.5f + 22.f, Button.Z);
		const FTransform PanelXf(Inward, CarXf.TransformPosition(Local));

		if (AOneBrahamLiftPanel* Panel = World->SpawnActor<AOneBrahamLiftPanel>(AOneBrahamLiftPanel::StaticClass(), PanelXf, Params))
		{
			Panel->InitPanel(this, Button.Role, INDEX_NONE);
			Panel->AttachToComponent(CarRoot, FAttachmentTransformRules::KeepWorldTransform);
			Panels.Add(Panel);
		}
	}
}

// ===========================================================================
//  Tick
// ===========================================================================

double AOneBrahamLift::GetLiftTime() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0.0;
	}

	if (HasAuthority())
	{
		return World->GetTimeSeconds();
	}

	if (bClockInitialised)
	{
		return World->GetTimeSeconds() + SmoothedClockOffset;
	}

	const AGameStateBase* GS = World->GetGameState();
	return GS != nullptr ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void AOneBrahamLift::UpdateClock(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World != nullptr ? World->GetGameState() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	// The replicated server clock arrives in steps, and each step carries the
	// jitter of the packet that brought it. A car positioned straight off that
	// clock moves in small jerks a rider feels as the floor twitching under the
	// camera. Following it smoothly costs a few milliseconds of accuracy and buys
	// a ride that feels like a lift.
	const double Target = GS->GetServerWorldTimeSeconds() - World->GetTimeSeconds();

	if (!bClockInitialised || FMath::Abs(Target - SmoothedClockOffset) > OBLiftClockSnapSeconds)
	{
		SmoothedClockOffset = Target;
		bClockInitialised = true;
		return;
	}

	const double Blend = FMath::Clamp(static_cast<double>(DeltaSeconds) * OBLiftClockFollowRate, 0.0, 1.0);
	SmoothedClockOffset += (Target - SmoothedClockOffset) * Blend;
}

void AOneBrahamLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		UpdateClock(DeltaSeconds);
	}

	const double Now = GetLiftTime();

	if (HasAuthority())
	{
		LockdownPollAccumulator += DeltaSeconds;
		if (LockdownPollAccumulator >= OBLiftLockdownPollSeconds)
		{
			LockdownPollAccumulator = 0.f;
			PollLockdown();
		}

		CheckDoorwaySafety(Now);

		if (Logic.Update(Now))
		{
			PushStateFromLogic();
		}
	}

	ApplyPresentation(Now);
}

void AOneBrahamLift::PushStateFromLogic()
{
	LiftState = Logic.GetState();
	ForceNetUpdate();
	NotifyStateTransitions();
}

void AOneBrahamLift::OnRep_LiftState()
{
	Logic.ApplyReplicatedState(LiftState);
	NotifyStateTransitions();
}

void AOneBrahamLift::NotifyStateTransitions()
{
	if (LastSeenPhase == EOneBrahamLiftPhase::Moving && LiftState.Phase != EOneBrahamLiftPhase::Moving)
	{
		OnLiftArrived.Broadcast(LiftId, LiftState.CurrentFloor);
	}

	if (bLastSeenLockedDown != LiftState.bLockedDown)
	{
		OnServiceChanged.Broadcast(LiftId, LiftState.bLockedDown);
	}

	LastSeenPhase = LiftState.Phase;
	bLastSeenLockedDown = LiftState.bLockedDown;
}

// ===========================================================================
//  Server: calls, lockdown, safety
// ===========================================================================

bool AOneBrahamLift::ServerCallToFloor(int32 Floor, AOneBrahamPlayerController* Caller)
{
	if (!HasAuthority())
	{
		return false;
	}

	const bool bAccepted = Logic.RequestLanding(Floor, GetLiftTime());
	if (bAccepted)
	{
		PushStateFromLogic();
	}

	OB_ACCESS_LOG("Lift '%s': call to floor %d by %s - %s",
		*LiftId.ToString(), Floor, Caller != nullptr ? *Caller->GetName() : TEXT("nobody"),
		bAccepted ? TEXT("accepted") : TEXT("refused"));

	return bAccepted;
}

bool AOneBrahamLift::ServerCycleCarDestination(int32 Direction, AOneBrahamPlayerController* Caller)
{
	if (!HasAuthority())
	{
		return false;
	}

	const int32 Next = Logic.GetNextCarSelection(Direction);
	const bool bAccepted = Next != INDEX_NONE && Logic.SelectCarDestination(Next, GetLiftTime());
	if (bAccepted)
	{
		PushStateFromLogic();
	}

	OB_ACCESS_LOG("Lift '%s': destination %+d -> floor %d by %s - %s",
		*LiftId.ToString(), Direction, Next, Caller != nullptr ? *Caller->GetName() : TEXT("nobody"),
		bAccepted ? TEXT("accepted") : TEXT("refused"));

	return bAccepted;
}

void AOneBrahamLift::PollLockdown()
{
	bool bShouldLock = false;

	if (bStopsOnLockdown)
	{
		const UWorld* World = GetWorld();
		const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
		const UOneBrahamLockdownComponent* Lockdown = GS != nullptr ? GS->GetLockdownComponent() : nullptr;

		// "Lifts stop" is the Lockdown stage's line in the director's own header,
		// and the Purge is worse, not better. PreLockdown leaves the lifts running:
		// it is the warning, and the lifts are how people act on it.
		bShouldLock = Lockdown != nullptr
			&& static_cast<uint8>(Lockdown->GetStage()) >= static_cast<uint8>(EOneBrahamLockdownStage::Lockdown);
	}

	if (bShouldLock != Logic.IsLockedDown())
	{
		Logic.SetLockedDown(bShouldLock, GetLiftTime());
		PushStateFromLogic();

		OB_ACCESS_LOG("Lift '%s': %s (car at floor %d, %s)", *LiftId.ToString(),
			bShouldLock ? TEXT("out of service for lockdown") : TEXT("back in service"),
			GetCarFloor(), *UEnum::GetValueAsString(LiftState.Phase));
	}
}

void AOneBrahamLift::CheckDoorwaySafety(double Now)
{
	if (LiftState.Phase != EOneBrahamLiftPhase::DoorsClosing)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The light curtain: a thin slab in the door plane, from the car's front wall
	// to the landing doors. A rider standing beside the doors inside the car does
	// not hold them; somebody stepping through them does.
	const float T = bGlass ? OBLiftGlassWallCm : OBLiftWallCm;
	const float BackX = CarDepthCm * 0.5f - T;
	const float FrontX = GetLandingDoorX();
	const float DW = FMath::Min(DoorWidthCm, CarWidthCm - 20.f);
	const float CarRelZ = CarRoot->GetRelativeLocation().Z;

	const FVector LocalCentre((BackX + FrontX) * 0.5f, 0.f, CarRelZ + DoorHeightCm * 0.5f);
	const FVector Extent(FMath::Max(2.f, (FrontX - BackX) * 0.5f), DW * 0.5f - 5.f, DoorHeightCm * 0.5f - 10.f);

	const FTransform Facing = FacingRoot->GetComponentTransform();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBLiftDoorSafety), /*bTraceComplex*/ false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Facing.TransformPosition(LocalCentre), Facing.GetRotation(),
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(Extent), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Cast<APawn>(Overlap.GetActor()) != nullptr)
		{
			if (Logic.HoldDoors(Now))
			{
				PushStateFromLogic();
			}
			return;
		}
	}
}

// ===========================================================================
//  Presentation (every machine)
// ===========================================================================

int32 AOneBrahamLift::GetCarFloor() const
{
	return Logic.Config.ZToNearestFloor(FOneBrahamLiftLogic::GetCarZ(LiftState, Logic.Config, GetLiftTime()));
}

float AOneBrahamLift::GetDoorOpenAlpha() const
{
	return FOneBrahamLiftLogic::GetDoorOpenAlpha(LiftState, Logic.Config, GetLiftTime());
}

bool AOneBrahamLift::IsCarOpenAtFloor(int32 Floor) const
{
	return LiftState.Phase != EOneBrahamLiftPhase::Moving
		&& LiftState.Phase != EOneBrahamLiftPhase::Idle
		&& LiftState.CurrentFloor == Floor;
}

void AOneBrahamLift::ApplyPresentation(double Now)
{
	const FOneBrahamLiftConfig& Config = Logic.Config;

	// --- the car ------------------------------------------------------------
	const float CarRelZ = FOneBrahamLiftLogic::GetCarZ(LiftState, Config, Now) - GetActorLocation().Z;
	if (!FMath::IsNearlyEqual(CarRelZ, LastAppliedCarRelZ, 0.01f))
	{
		// Teleport, not sweep: the car is the thing that defines where the shaft
		// is, and a sweep would stop it on the very riders it is carrying. The
		// movement component reads the base's motion and carries them along.
		CarRoot->SetRelativeLocation(FVector(0.f, 0.f, CarRelZ), /*bSweep*/ false, nullptr, ETeleportType::None);
		LastAppliedCarRelZ = CarRelZ;
	}

	// --- the doors ------------------------------------------------------------
	const float DoorAlpha = FOneBrahamLiftLogic::GetDoorOpenAlpha(LiftState, Config, Now);
	const float DW = FMath::Min(DoorWidthCm, CarWidthCm - 20.f);

	if (!FMath::IsNearlyEqual(DoorAlpha, LastAppliedDoorAlpha, 0.0005f))
	{
		const float Slide = OBLiftEaseDoor(DoorAlpha) * DW * 0.5f;
		const float DoorZ = FMath::Min(DoorHeightCm, CarHeightCm - 10.f) * 0.5f;
		CarDoorLeft->SetRelativeLocation(FVector(GetCarDoorX(), -(DW * 0.25f + Slide), DoorZ));
		CarDoorRight->SetRelativeLocation(FVector(GetCarDoorX(), DW * 0.25f + Slide, DoorZ));
		LastAppliedDoorAlpha = DoorAlpha;
	}

	// Only the landing the car is standing at ever opens.
	const int32 OpenFloor = (LiftState.Phase == EOneBrahamLiftPhase::Moving || !bBuildLandingDoors)
		? INDEX_NONE : LiftState.CurrentFloor;

	auto SetLandingLeaves = [this](int32 Floor, float Alpha)
	{
		if (Floor < MinFloor || Floor > MaxFloor)
		{
			return;
		}

		const int32 Base = (Floor - MinFloor) * 2;
		if (Base + 1 < LandingDoors->GetInstanceCount())
		{
			LandingDoors->UpdateInstanceTransform(Base, GetLandingDoorTransform(Floor, true, Alpha), false, false, true);
			LandingDoors->UpdateInstanceTransform(Base + 1, GetLandingDoorTransform(Floor, false, Alpha), false, true, true);
		}
	};

	if (AnimatedLandingFloor != OpenFloor && AnimatedLandingFloor != INDEX_NONE)
	{
		SetLandingLeaves(AnimatedLandingFloor, 0.f);
		AnimatedLandingFloor = INDEX_NONE;
		LastLandingDoorAlpha = -1.f;
	}

	if (OpenFloor != INDEX_NONE && !FMath::IsNearlyEqual(DoorAlpha, LastLandingDoorAlpha, 0.0005f))
	{
		SetLandingLeaves(OpenFloor, DoorAlpha);
		AnimatedLandingFloor = OpenFloor;
		LastLandingDoorAlpha = DoorAlpha;
	}

	// --- the indicator --------------------------------------------------------
	const int32 ShownFloor = Config.ZToNearestFloor(CarRelZ + GetActorLocation().Z);
	if (CarIndicator != nullptr && (ShownFloor != LastIndicatorFloor || bLastIndicatorLocked != LiftState.bLockedDown))
	{
		CarIndicator->SetText(LiftState.bLockedDown
			? FText::Format(NSLOCTEXT("OneBraham", "Lift_IndicatorLocked", "{0}  STOPPED"), FText::AsNumber(ShownFloor))
			: FText::AsNumber(ShownFloor));
		CarIndicator->SetTextRenderColor((LiftState.bLockedDown ? OBLiftIndicatorLocked : OBLiftIndicatorNormal).ToFColor(true));
		LastIndicatorFloor = ShownFloor;
		bLastIndicatorLocked = LiftState.bLockedDown;
	}
}
