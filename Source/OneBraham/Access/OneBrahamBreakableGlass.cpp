// Copyright (c) One Braham. All Rights Reserved.

#include "Access/OneBrahamBreakableGlass.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Voice/OneBrahamVoiceDirector.h"

#include "VFX/OneBrahamVFXStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "OneBraham.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** The engine's basic cube, scaled to the pane's dimensions. */
	const TCHAR* OBPaneMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** Damage below this is a pockmark: it darkens the glass and nothing else. */
	constexpr float OBGlassMinimumDamage = 1.f;
}

AOneBrahamBreakableGlass::AOneBrahamBreakableGlass()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pane"));
	PaneMesh->SetupAttachment(Root);

	// The OneBrahamGlass profile: blocks the Weapon channel (so it is cover),
	// blocks Camera, and carries the Glass object type that both the weapon's
	// glass branch and its own impact detection look for.
	PaneMesh->SetCollisionProfileName(TEXT("OneBrahamGlass"));
	PaneMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PaneMesh->SetMobility(EComponentMobility::Static);
	PaneMesh->SetCastShadow(true);
	PaneMesh->bReceivesDecals = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PaneAsset(OBPaneMeshPath);
	if (PaneAsset.Succeeded())
	{
		PaneMesh->SetStaticMesh(PaneAsset.Object);
	}
}

void AOneBrahamBreakableGlass::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyDimensions();
}

void AOneBrahamBreakableGlass::BeginPlay()
{
	Super::BeginPlay();

	// Dimensions are applied in `OnConstruction`, which runs for a pane placed by
	// a designer, spawned by the map builder and spawned at runtime alike. Doing
	// it again here would mean scaling a Static component after registration,
	// which the engine warns about and which the navmesh has already baked.
	if (bBroken)
	{
		// A late joiner, or a PIE session restarted on top of a broken pane.
		ApplyBrokenState();
	}
	else if (CrackLevel > 0)
	{
		ApplyCrack();
	}
}

void AOneBrahamBreakableGlass::OnRep_Crack()
{
	ApplyCrack();
}

void AOneBrahamBreakableGlass::ApplyCrack()
{
	if (PaneMesh == nullptr || IsRunningDedicatedServer())
	{
		return;
	}
	if (PaneMaterial == nullptr)
	{
		PaneMaterial = PaneMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (PaneMaterial != nullptr)
	{
		PaneMaterial->SetScalarParameterValue(TEXT("CrackAmount"), static_cast<float>(CrackLevel) / 255.f);
	}
}

void AOneBrahamBreakableGlass::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamBreakableGlass, bBroken);
	DOREPLIFETIME(AOneBrahamBreakableGlass, CrackLevel);
}

void AOneBrahamBreakableGlass::ApplyDimensions()
{
	if (PaneMesh == nullptr)
	{
		return;
	}

	// The engine cube is 100 cm on every side, so the scale is the size in metres
	// divided by that. Scaling the *component* rather than the actor keeps the
	// actor's own transform clean, which is what the builder and the manifest
	// record, and it means a rotated pane still reports its true size.
	const float Width = FMath::Max(20.f, static_cast<float>(PaneSizeCm.X));
	const float Height = FMath::Max(20.f, static_cast<float>(PaneSizeCm.Y));
	const float Thickness = FMath::Max(1.f, ThicknessCm);

	PaneMesh->SetRelativeScale3D(FVector(Width / 100.f, Thickness / 100.f, Height / 100.f));
}

float AOneBrahamBreakableGlass::GetPaneHealth01() const
{
	if (bBroken || PaneHealth <= 0.f)
	{
		return bBroken ? 0.f : 1.f;
	}

	return FMath::Clamp(1.f - AccumulatedDamage / PaneHealth, 0.f, 1.f);
}

FVector AOneBrahamBreakableGlass::GetPaneCentre() const
{
	return PaneMesh != nullptr ? PaneMesh->Bounds.Origin : GetActorLocation();
}

float AOneBrahamBreakableGlass::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || bBroken || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	// Where the hit landed, for the way the pane fails. A radial event (a frag's
	// blast) has no impact point of its own, so the pane fails from the direction
	// of whatever caused it, which is close enough for a window.
	FVector ImpactPoint = GetPaneCentre();
	FVector ImpactNormal = GetActorForwardVector();

	if (DamageEvent.HitInfo.bBlockingHit || !DamageEvent.HitInfo.ImpactPoint.IsNearlyZero())
	{
		ImpactPoint = DamageEvent.HitInfo.ImpactPoint;
		ImpactNormal = DamageEvent.HitInfo.ImpactNormal.IsNearlyZero()
			? (ImpactPoint - GetActorLocation()).GetSafeNormal()
			: DamageEvent.HitInfo.ImpactNormal;
	}
	else if (DamageCauser != nullptr)
	{
		ImpactPoint = DamageCauser->GetActorLocation();
		ImpactNormal = (GetActorLocation() - ImpactPoint).GetSafeNormal();
	}

	AccumulatedDamage += DamageAmount;

	// The cracks every machine sees, a quarter-crack minimum so the first hit
	// is visible. OnRep does not run where the value was set.
	const uint8 NewCrack = static_cast<uint8>(FMath::Clamp(
		FMath::Max(0.25f, AccumulatedDamage / FMath::Max(PaneHealth, 1.f)) * 255.f, 0.f, 255.f));
	if (NewCrack > CrackLevel)
	{
		CrackLevel = NewCrack;
		ApplyCrack();
	}

	if (DamageAmount < OBGlassMinimumDamage)
	{
		// A graze. It marks the glass, but nothing breaks.
		return 0.f;
	}

	if (AccumulatedDamage >= PaneHealth)
	{
		Shatter(ImpactPoint, ImpactNormal);
	}

	// The pane absorbed it, so nothing downstream is damaged by this hit. The
	// return value is what a weapon's hit report calls "damage dealt", and a
	// bullet that stopped in the glazing dealt none.
	return 0.f;
}

void AOneBrahamBreakableGlass::Shatter(const FVector& ImpactPoint, const FVector& ImpactNormal)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	FOneBrahamVoiceDirector::OnGlassBroken(GetWorld(), GetPaneCentre(), FloorIndex);

	// One path for the server and every client. The multicast runs locally on the
	// authority as well, so the pane's state is set in exactly one place and the
	// server cannot end up with a broken pane that no machine has seen break.
	MulticastShatter(ImpactPoint, ImpactNormal);

	ForceNetUpdate();

	OB_LOG("Pane '%s' broke at %s.", *PaneId.ToString(), *ImpactPoint.ToCompactString());
}

void AOneBrahamBreakableGlass::MulticastShatter_Implementation(FVector_NetQuantize ImpactPoint,
	FVector_NetQuantizeNormal ImpactNormal)
{
	bBroken = true;
	ApplyBrokenState();

	// The debris, the sparkle and the sound of a pane going. Positioned at the
	// hit rather than the pane's centre so a shot from the far end of a partition
	// breaks where the player was aiming, and oriented along the normal so the
	// glass falls into the room the bullet came from.
	UOneBrahamVFXStatics::SpawnGlassShatter(this, ImpactPoint, ImpactNormal, ShatterAreaCm2);
}

void AOneBrahamBreakableGlass::ApplyBrokenState()
{
	if (PaneMesh == nullptr)
	{
		return;
	}

	// Gone, in one step: invisible, no collision, no shadow. The frame is the
	// wall the builder placed this pane in, so what is left is an opening.
	PaneMesh->SetVisibility(false, /*bPropagateToChildren*/ true);
	PaneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PaneMesh->SetCastShadow(false);
	PaneMesh->SetReceivesDecals(false);
	// The opening is a route now, for players and for bots: the navigation mesh
	// over the pane's bounds has to be rebuilt, or an AI will keep treating the
	// hole as glass for the rest of the match.
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		Nav->AddDirtyArea(PaneMesh->Bounds.GetBox(), ENavigationDirtyFlag::All);
	}
}

void AOneBrahamBreakableGlass::OnRep_Broken()
{
	if (bBroken)
	{
		ApplyBrokenState();
	}
}
