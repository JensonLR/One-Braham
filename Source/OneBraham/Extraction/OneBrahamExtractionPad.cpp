// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamExtractionPad.cpp

#include "Extraction/OneBrahamExtractionPad.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameState.h"
#include "Extraction/OneBrahamExtractionComponent.h"

#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** Beam colours. Warm white is "something is coming", green is "this is
	 *  yours", red is "someone else is in here with you". */
	const FLinearColor OBBeamInbound(1.0f, 0.93f, 0.82f);
	const FLinearColor OBBeamHolding(0.72f, 1.0f, 0.78f);
	const FLinearColor OBBeamContested(1.0f, 0.08f, 0.05f);
	const FLinearColor OBBeamAborted(0.80f, 0.06f, 0.04f);

	/** Intensity at the two ends of the arrival, in candelas. */
	constexpr float OBBeamInboundMin = 2500.f;
	constexpr float OBBeamInboundMax = 14000.f;
	constexpr float OBBeamHold = 12000.f;
	constexpr float OBBeamContestedIntensity = 9000.f;
	constexpr float OBBeamFailed = 1200.f;

	/** Cosmetics: how often the pad re-evaluates the beam. 20 Hz is smooth for a
	 *  light and costs nothing on the machine that is also holding the match. */
	constexpr float OBPadTickInterval = 0.05f;

	constexpr float OBTwoPi = 6.2831853f;
}

AOneBrahamExtractionPad::AOneBrahamExtractionPad()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = OBPadTickInterval;

	// The registration flag is the only replicated thing here: the beam is a
	// function of the component's state, which is already replicated.
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PadZone = CreateDefaultSubobject<UBoxComponent>(TEXT("PadZone"));
	PadZone->SetupAttachment(Root);
	PadZone->SetBoxExtent(FVector(900.f, 900.f, 150.f));
	PadZone->SetCollisionProfileName(TEXT("OneBrahamVolume"));
	PadZone->SetCanEverAffectNavigation(false);
	PadZone->SetHiddenInGame(true);

	Searchlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Searchlight"));
	Searchlight->SetupAttachment(Root);
	Searchlight->SetRelativeLocation(FVector(0.f, 0.f, 600.f));
	Searchlight->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	Searchlight->SetInnerConeAngle(12.f);
	Searchlight->SetOuterConeAngle(38.f);
	Searchlight->SetAttenuationRadius(7000.f);
	Searchlight->SetIntensity(0.f);
	Searchlight->SetLightColor(OBBeamInbound);
	Searchlight->SetCastShadows(true);
	Searchlight->SetMobility(EComponentMobility::Movable);
}

void AOneBrahamExtractionPad::BeginPlay()
{
	Super::BeginPlay();

	// The volume is the reported radius, so a designer moving the pad marker in
	// the editor moves the rule with it.
	PadZone->SetBoxExtent(FVector(HoldRadiusCm, HoldRadiusCm, ZoneHeightCm * 0.5f), false);
	Searchlight->SetRelativeLocation(FVector(0.f, 0.f, SearchlightHeightCm));

	RegisterWithWinch();
	ApplyBeam();
}

void AOneBrahamExtractionPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamExtractionPad, bRegistered);
}

void AOneBrahamExtractionPad::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// One retry a second until the component has the pad, for the case where the
	// game state was not awake in time. After that the tick is only the beam.
	if (!bRegistered && HasAuthority())
	{
		RegisterRetrySeconds += DeltaSeconds;
		if (RegisterRetrySeconds >= 1.f)
		{
			RegisterRetrySeconds = 0.f;
			RegisterWithWinch();
		}
	}

	ApplyBeam();
}

// ===========================================================================
//  Registration
// ===========================================================================

void AOneBrahamExtractionPad::RegisterWithWinch()
{
	if (!HasAuthority() || bRegistered)
	{
		return;
	}

	UOneBrahamExtractionComponent* Extraction = GetExtraction();
	if (Extraction == nullptr)
	{
		// Not a failure: the pad retries. A pad placed in a sublevel that streams
		// in before the game state is awake must not be silently unregistered
		// forever, because the failure mode is invisible - the winch can be called
		// and the wire is measured against a point nobody placed.
		return;
	}

	Extraction->ServerSetHelipadLocation(GetActorLocation(), HoldRadiusCm);
	bRegistered = true;

	OB_EXTRACT_LOG("Winch pad '%s' registered at %s (radius %.0f cm, zone height %.0f cm)",
		*PadId.ToString(), *GetActorLocation().ToCompactString(), HoldRadiusCm, ZoneHeightCm);
}

UOneBrahamExtractionComponent* AOneBrahamExtractionPad::GetExtraction() const
{
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	return GS != nullptr ? GS->GetExtractionComponent() : nullptr;
}

bool AOneBrahamExtractionPad::IsOnPad(const FVector& WorldLocation) const
{
	const FVector Delta = WorldLocation - GetActorLocation();

	// The same test the component makes, so a HUD marker and the win condition
	// agree about what "on the pad" means.
	return FVector::DistSquared2D(Delta, FVector::ZeroVector) <= HoldRadiusCm * HoldRadiusCm
		&& FMath::Abs(Delta.Z) <= ZoneHeightCm;
}

// ===========================================================================
//  The beam
// ===========================================================================

void AOneBrahamExtractionPad::ApplyBeam()
{
	const UOneBrahamExtractionComponent* Extraction = GetExtraction();
	if (Extraction == nullptr)
	{
		Searchlight->SetIntensity(0.f);
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World != nullptr ? World->GetTimeSeconds() : 0.f;

	FLinearColor Colour = OBBeamInbound;
	float Intensity = 0.f;
	float YawDegrees = 0.f;

	switch (Extraction->GetExtractionState())
	{
	case EOneBrahamExtractionState::Inactive:
		// Standing by: the deck is lit by its own lights, and the mast is dark.
		Intensity = 0.f;
		break;

	case EOneBrahamExtractionState::Broadcast:
	case EOneBrahamExtractionState::Arriving:
		// The beam hunts across the deck while the helicopter is on its way, and
		// hardens as it closes. This is the only warning a squad gets that the
		// fight is about to move upstairs, and it is visible from the stairwell.
		Intensity = FMath::Lerp(OBBeamInboundMin, OBBeamInboundMax, Extraction->GetArrivalAlpha01());
		YawDegrees = FMath::Sin(Now * SweepHz * OBTwoPi) * SweepDegrees;
		Colour = OBBeamInbound;
		break;

	case EOneBrahamExtractionState::Holding:
		// Down on the pad. Contested is the design's most legible moment: the light
		// goes red and the hold stops counting.
		Intensity = Extraction->IsContested() ? OBBeamContestedIntensity : OBBeamHold;
		Colour = Extraction->IsContested() ? OBBeamContested : OBBeamHolding;
		break;

	case EOneBrahamExtractionState::Complete:
		Intensity = OBBeamInboundMax;
		Colour = OBBeamHolding;
		break;

	case EOneBrahamExtractionState::Failed:
	default:
		Intensity = OBBeamFailed;
		Colour = OBBeamAborted;
		break;
	}

	Searchlight->SetIntensity(Intensity);
	Searchlight->SetLightColor(Colour);
	Searchlight->SetRelativeRotation(FRotator(-90.f, YawDegrees, 0.f));
}
