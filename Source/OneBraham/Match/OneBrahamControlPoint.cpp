// Copyright (c) One Braham. All Rights Reserved.

#include "Match/OneBrahamControlPoint.h"

#include "OneBraham.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UI/OneBrahamUIStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	int32 TeamIndexOf(EOneBrahamTeam Team)
	{
		return Team == EOneBrahamTeam::InternalStaff ? 0 : (Team == EOneBrahamTeam::ExternalContractors ? 1 : INDEX_NONE);
	}
}

AOneBrahamControlPoint::AOneBrahamControlPoint()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	SetNetUpdateFrequency(10.f);
	bAlwaysRelevant = true;

	PointRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PointRoot"));
	SetRootComponent(PointRoot);

	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(PointRoot);
	Ring->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Ring->SetStaticMesh(Cylinder.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(PointRoot);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	Light->SetIntensity(20000.f);
	Light->SetAttenuationRadius(1400.f);
	Light->SetCastShadows(false);
}

void AOneBrahamControlPoint::Configure(FName InPointId, const FText& InDisplayName, float InRadiusCm, float InCaptureSeconds, float InScorePerSecond)
{
	PointId = InPointId;
	DisplayName = InDisplayName;
	RadiusCm = FMath::Max(100.f, InRadiusCm);
	CaptureSeconds = FMath::Max(0.5f, InCaptureSeconds);
	ScorePerSecond = FMath::Max(0.f, InScorePerSecond);

	// A thin disc the size of the point: the engine cylinder is 100 cm across.
	Ring->SetRelativeScale3D(FVector(RadiusCm * 2.f / 100.f, RadiusCm * 2.f / 100.f, 0.02f));
}

void AOneBrahamControlPoint::BeginPlay()
{
	Super::BeginPlay();

	if (!IsRunningDedicatedServer())
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (Base != nullptr)
		{
			RingMaterial = UMaterialInstanceDynamic::Create(Base, this);
			Ring->SetMaterial(0, RingMaterial);
		}
	}
	ApplyColours();
}

void AOneBrahamControlPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOneBrahamControlPoint, PointId);
	DOREPLIFETIME(AOneBrahamControlPoint, DisplayName);
	DOREPLIFETIME(AOneBrahamControlPoint, RadiusCm);
	DOREPLIFETIME(AOneBrahamControlPoint, OwnerTeam);
	DOREPLIFETIME(AOneBrahamControlPoint, CapturingTeam);
	DOREPLIFETIME(AOneBrahamControlPoint, Progress);
	DOREPLIFETIME(AOneBrahamControlPoint, bContested);
}

void AOneBrahamControlPoint::StepCapture(int32 TeamAInside, int32 TeamBInside, float DeltaSeconds, float InCaptureSeconds,
	int32& InOutOwner, int32& InOutCapturer, float& InOutProgress, bool& bOutContested)
{
	bOutContested = TeamAInside > 0 && TeamBInside > 0;
	const int32 Present = bOutContested ? INDEX_NONE : (TeamAInside > 0 ? 0 : (TeamBInside > 0 ? 1 : INDEX_NONE));
	if (Present == INDEX_NONE)
	{
		// Empty or contested: frozen. A half-built claim by nobody's team stays
		// where it was, so a capture interrupted is not a capture lost.
		InOutCapturer = INDEX_NONE;
		return;
	}

	const float Step = DeltaSeconds / FMath::Max(0.5f, InCaptureSeconds);

	if (InOutOwner == Present)
	{
		InOutProgress = FMath::Min(1.f, InOutProgress + Step);
		InOutCapturer = InOutProgress < 1.f ? Present : INDEX_NONE;
		return;
	}

	InOutCapturer = Present;
	if (InOutOwner != INDEX_NONE)
	{
		// The other team's point: drain their claim first.
		InOutProgress -= Step;
		if (InOutProgress <= 0.f)
		{
			InOutOwner = INDEX_NONE;
			InOutProgress = 0.f;
		}
		return;
	}

	InOutProgress += Step;
	if (InOutProgress >= 1.f)
	{
		InOutOwner = Present;
		InOutProgress = 1.f;
		InOutCapturer = INDEX_NONE;
	}
}

void AOneBrahamControlPoint::CountInside(int32& OutTeamA, int32& OutTeamB) const
{
	OutTeamA = 0;
	OutTeamB = 0;
	const AOneBrahamGameState* GS = GetWorld() ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	const FVector Centre = GetActorLocation();
	for (const APlayerState* Base : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* State = Cast<AOneBrahamPlayerState>(Base);
		const APawn* Pawn = State ? State->GetPawn() : nullptr;
		if (Pawn == nullptr || !State->IsAlive())
		{
			continue;
		}
		const FVector Where = Pawn->GetActorLocation();
		if (FVector::Dist2D(Where, Centre) > RadiusCm || FMath::Abs(Where.Z - Centre.Z) > HeightToleranceCm)
		{
			continue;
		}
		const int32 Index = TeamIndexOf(State->Team);
		OutTeamA += Index == 0 ? 1 : 0;
		OutTeamB += Index == 1 ? 1 : 0;
	}
}

void AOneBrahamControlPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	const AOneBrahamGameState* GS = GetWorld() ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr || GS->MatchPhase != EOneBrahamMatchPhase::Active)
	{
		return;
	}

	int32 TeamA = 0;
	int32 TeamB = 0;
	CountInside(TeamA, TeamB);

	const int32 PreviousOwner = OwnerTeam;
	StepCapture(TeamA, TeamB, DeltaSeconds, CaptureSeconds, OwnerTeam, CapturingTeam, Progress, bContested);

	if (OwnerTeam != PreviousOwner)
	{
		ApplyColours();
		if (OwnerTeam != INDEX_NONE)
		{
			OB_MATCH_LOG("Control point %s taken by team %d.", *PointId.ToString(), OwnerTeam);
		}
	}

	// Pay the owner, in whole points.
	if (OwnerTeam != INDEX_NONE && ScorePerSecond > 0.f)
	{
		ScoreAccumulator += ScorePerSecond * DeltaSeconds;
		const int32 Whole = FMath::FloorToInt(ScoreAccumulator);
		if (Whole > 0)
		{
			ScoreAccumulator -= Whole;
			if (AOneBrahamGameMode* GameMode = GetWorld()->GetAuthGameMode<AOneBrahamGameMode>())
			{
				GameMode->AddTeamScoreByIndex(OwnerTeam, Whole);
			}
		}
	}
}

void AOneBrahamControlPoint::OnRep_State()
{
	ApplyColours();
}

void AOneBrahamControlPoint::ApplyColours()
{
	// The FIRE DRILL team colours (UOneBrahamUIStyle::TeamColour), grey when
	// nobody holds it, and the alarm colour while it is fought over.
	const FLinearColor Colour = bContested ? UOneBrahamUIStyle::EmergencyRed()
		: (OwnerTeam == INDEX_NONE ? UOneBrahamUIStyle::LightGrey() : UOneBrahamUIStyle::TeamColour(OwnerTeam));
	if (RingMaterial != nullptr)
	{
		RingMaterial->SetVectorParameterValue(TEXT("Color"), Colour);
	}
	if (Light != nullptr)
	{
		Light->SetLightColor(Colour);
	}
}
