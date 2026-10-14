// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamExtractionTerminal.cpp

#include "Extraction/OneBrahamExtractionTerminal.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Extraction/OneBrahamExtractionComponent.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Screen colours. The same four states as the access panels, one meaning up:
	 *  blue is "ready", amber is "the building has heard you", green is "on the
	 *  wire", red is "aborted". */
	const FLinearColor OBWinchReady(0.05f, 0.42f, 0.85f);
	const FLinearColor OBWinchCalled(1.0f, 0.62f, 0.02f);
	const FLinearColor OBWinchLive(0.05f, 0.85f, 0.32f);
	const FLinearColor OBWinchAborted(1.0f, 0.06f, 0.04f);

	/** The material parameter the screen tint is written to. It exists on
	 *  M_OB_Screen and on nothing else, so a console whose screen mesh has a plain
	 *  surface still reads through ScreenLight - the tint call is a no-op there
	 *  rather than a bug. */
	const FName OBScreenTintParameter = FName(TEXT("ScreenTint"));
}

AOneBrahamExtractionTerminal::AOneBrahamExtractionTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	// The state lives on the game state's replicated extraction component, so
	// this actor has nothing of its own to replicate.
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyMesh->SetCollisionObjectType(ECC_WorldStatic);
	BodyMesh->SetCollisionResponseToAllChannels(ECR_Block);
	BodyMesh->SetCanEverAffectNavigation(false);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 52.f));
	BodyMesh->SetRelativeScale3D(FVector(1.4f, 0.7f, 1.04f));

	ScreenMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenMesh"));
	ScreenMesh->SetupAttachment(Root);
	ScreenMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScreenMesh->SetRelativeLocation(FVector(0.f, 34.f, 78.f));
	ScreenMesh->SetRelativeScale3D(FVector(1.1f, 0.03f, 0.46f));
	ScreenMesh->SetCanEverAffectNavigation(false);

	ScreenLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ScreenLight"));
	ScreenLight->SetupAttachment(ScreenMesh);
	ScreenLight->SetRelativeLocation(FVector(-10.f, 30.f, 0.f));
	ScreenLight->SetIntensity(300.f);
	ScreenLight->SetAttenuationRadius(460.f);
	ScreenLight->SetLightColor(OBWinchReady);
	ScreenLight->SetCastShadows(false);
	ScreenLight->SetMobility(EComponentMobility::Movable);

	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelText"));
	LabelText->SetupAttachment(Root);
	LabelText->SetRelativeLocation(FVector(0.f, 30.f, 108.f));
	LabelText->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	LabelText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LabelText->SetWorldSize(13.f);
	LabelText->SetTextRenderColor(FColor(24, 26, 30));
	LabelText->SetText(FText::FromString(TEXT("WINCH CONTROL")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeFinder.Object);
		ScreenMesh->SetStaticMesh(CubeFinder.Object);
	}
}

void AOneBrahamExtractionTerminal::BeginPlay()
{
	Super::BeginPlay();

	// A dynamic instance on the screen, because SetVectorParameterValue* only
	// writes to material instances: a screen mesh with a plain material would
	// ignore every tint below, and the console - which is the clearest state read
	// on the Skydeck - would sit on one colour for the whole match.
	if (ScreenMesh != nullptr)
	{
		if (UMaterialInstanceDynamic* ScreenMID = ScreenMesh->CreateDynamicMaterialInstance(0))
		{
			ScreenMID->SetVectorParameterValue(OBScreenTintParameter, FLinearColor::White);
		}
	}

	LabelText->SetText(GetTerminalDisplayName());
	ApplyScreenState();

	OB_EXTRACT_LOG("Winch console '%s' placed at %s", *TerminalId.ToString(),
		*GetActorLocation().ToCompactString());
}

void AOneBrahamExtractionTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The label and the colour are driven from replicated state, so a client that
	// joins late reads the same screen as everybody else without a single RPC of
	// this actor's own.
	ApplyScreenState();
}

// ===========================================================================
//  Queries
// ===========================================================================

UOneBrahamExtractionComponent* AOneBrahamExtractionTerminal::GetExtraction() const
{
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	return GS != nullptr ? GS->GetExtractionComponent() : nullptr;
}

bool AOneBrahamExtractionTerminal::IsWinchLive() const
{
	const UOneBrahamExtractionComponent* Extraction = GetExtraction();

	return Extraction != nullptr && Extraction->GetExtractionState() != EOneBrahamExtractionState::Inactive
		&& Extraction->GetExtractionState() != EOneBrahamExtractionState::Failed;
}

FText AOneBrahamExtractionTerminal::GetTerminalDisplayName() const
{
	if (!TerminalDisplayName.IsEmpty())
	{
		return TerminalDisplayName;
	}

	return FText::FromName(TerminalId);
}

// ===========================================================================
//  IOneBrahamInteractionInterface
// ===========================================================================

FText AOneBrahamExtractionTerminal::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const UOneBrahamExtractionComponent* Extraction = GetExtraction();
	if (Extraction == nullptr)
	{
		return NSLOCTEXT("OneBraham", "Winch_Offline", "Winch control offline");
	}

	switch (Extraction->GetExtractionState())
	{
	case EOneBrahamExtractionState::Inactive:
		return NSLOCTEXT("OneBraham", "Winch_Ready", "Hold {Interact} - call the winch");

	case EOneBrahamExtractionState::Broadcast:
		return NSLOCTEXT("OneBraham", "Winch_Called", "Called - ten seconds");

	case EOneBrahamExtractionState::Arriving:
		return NSLOCTEXT("OneBraham", "Winch_Arriving", "Called - helicopter inbound");

	case EOneBrahamExtractionState::Holding:
		return NSLOCTEXT("OneBraham", "Winch_Live", "Winch live - hold the pad");

	case EOneBrahamExtractionState::Complete:
		return NSLOCTEXT("OneBraham", "Winch_Done", "Winch complete");

	case EOneBrahamExtractionState::Failed:
	default:
		return NSLOCTEXT("OneBraham", "Winch_Aborted", "Winch aborted - stands by");
	}
}

FGameplayTag AOneBrahamExtractionTerminal::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	// The tag the montage contract reserves AM_OB_Interact_Radio for: the player
	// keys a handheld set on the desk. It is the same tag the pad's thirty second
	// hold was designed around, and the last of the eight waiting tags to arrive.
	return OneBrahamTags::Interaction_Extraction;
}

bool AOneBrahamExtractionTerminal::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Interactor == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
	if (PS == nullptr || PS->IsEliminated())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr || GS->MatchMode != EOneBrahamMatchMode::Ascent)
	{
		return false;
	}

	const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent();
	if (Extraction == nullptr)
	{
		return false;
	}

	// One call at a time, and only while the pad is standing by: an aborted
	// attempt re-arms itself five seconds after it fails, and this console must
	// not offer a call the component would refuse.
	const EOneBrahamExtractionState State = Extraction->GetExtractionState();

	return State == EOneBrahamExtractionState::Inactive;
}

bool AOneBrahamExtractionTerminal::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return true;
}

float AOneBrahamExtractionTerminal::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return CallHoldSeconds;
}

float AOneBrahamExtractionTerminal::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return InteractionRangeCm;
}

bool AOneBrahamExtractionTerminal::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr || !HasAuthority() || !CanInteract_Implementation(Interactor))
	{
		return false;
	}

	HoldInteractor = Interactor;
	LastProgressSecond = -1;

	MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Hum")), 0.7f);

	OB_EXTRACT_LOG("Winch console '%s': call started by %s.", *TerminalId.ToString(),
		*Interactor->GetName());

	return true;
}

void AOneBrahamExtractionTerminal::OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor,
	float HeldSeconds, float Progress01)
{
	if (Interactor == nullptr || !HasAuthority() || Interactor != HoldInteractor.Get())
	{
		return;
	}

	// A tick a second, so the hold has a sound and the player can hear that the
	// server is still counting: the interact component owns the timer, this is
	// the console's half of the feedback.
	const int32 WholeSecond = FMath::FloorToInt(HeldSeconds);
	if (WholeSecond > LastProgressSecond)
	{
		LastProgressSecond = WholeSecond;
		MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Progress")), 0.6f);
	}
}

void AOneBrahamExtractionTerminal::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	HoldInteractor = nullptr;
	LastProgressSecond = -1;

	if (Interactor == nullptr || !HasAuthority() || !bCompleted)
	{
		return;
	}

	UOneBrahamExtractionComponent* Extraction = GetExtraction();
	AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
	const int32 SquadId = ResolveCallingSquadId(Interactor);

	FText Refusal;
	const bool bCalled = Extraction != nullptr
		&& SquadId != INDEX_NONE
		&& Extraction->ServerCallExtraction(SquadId, PS, Refusal);

	if (!bCalled)
	{
		// The component's own sentence, on the caller's banner. A refusal the
		// player can read is the difference between "the button is broken" and
		// "the winch is already taken".
		MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Deny")), 0.7f);

		Interactor->ClientShowBanner(
			Refusal.IsEmpty()
				? NSLOCTEXT("OneBraham", "Winch_NoReason", "The winch does not answer.")
				: Refusal,
			3.5f, OBWinchAborted);

		OB_EXTRACT_LOG("Winch console '%s': call refused for %s (%s).", *TerminalId.ToString(),
			*Interactor->GetName(), *Refusal.ToString());
	}
	else
	{
		MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Accept")), 1.f);
	}

	ApplyScreenState();
}

void AOneBrahamExtractionTerminal::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
	HoldInteractor = nullptr;
	LastProgressSecond = -1;
}

void AOneBrahamExtractionTerminal::SetInteractionHighlight_Implementation(bool bHighlightedIn)
{
	bHighlighted = bHighlightedIn;
	ApplyHighlight(bHighlightedIn);
}

// ===========================================================================
//  Presentation
// ===========================================================================

void AOneBrahamExtractionTerminal::ApplyHighlight(bool bEnabled)
{
	// The same convention the keycards and the access panels use: the interaction
	// outline is a post-process material reading custom depth stencil 2, so every
	// interactable that wants to light up writes the same value.
	static constexpr int32 OBInteractionStencil = 2;

	SetRenderCustomDepth(bEnabled);

	if (BodyMesh != nullptr)
	{
		BodyMesh->SetRenderCustomDepth(bEnabled);
		BodyMesh->SetCustomDepthStencilValue(OBInteractionStencil);
	}

	if (ScreenMesh != nullptr)
	{
		ScreenMesh->SetRenderCustomDepth(bEnabled);
		ScreenMesh->SetCustomDepthStencilValue(OBInteractionStencil);
	}
}

void AOneBrahamExtractionTerminal::ApplyScreenState()
{
	const UOneBrahamExtractionComponent* Extraction = GetExtraction();

	FLinearColor Colour = OBWinchReady;
	FText Label = GetTerminalDisplayName();

	if (Extraction == nullptr)
	{
		Colour = OBWinchAborted;
		Label = NSLOCTEXT("OneBraham", "Winch_LabelOffline", "WINCH - NO LINK");
	}
	else
	{
		switch (Extraction->GetExtractionState())
		{
		case EOneBrahamExtractionState::Inactive:
			Colour = OBWinchReady;
			Label = NSLOCTEXT("OneBraham", "Winch_LabelStandby", "WINCH CONTROL - STANDBY");
			break;

		case EOneBrahamExtractionState::Broadcast:
		case EOneBrahamExtractionState::Arriving:
			Colour = OBWinchCalled;
			Label = NSLOCTEXT("OneBraham", "Winch_LabelCalled", "WINCH CALLED");
			break;

		case EOneBrahamExtractionState::Holding:
			Colour = OBWinchLive;
			Label = Extraction->IsContested()
				? NSLOCTEXT("OneBraham", "Winch_LabelContested", "WINCH - CONTESTED")
				: NSLOCTEXT("OneBraham", "Winch_LabelLive", "WINCH LIVE - HOLD THE PAD");
			break;

		case EOneBrahamExtractionState::Complete:
			Colour = OBWinchLive;
			Label = NSLOCTEXT("OneBraham", "Winch_LabelComplete", "WINCH COMPLETE");
			break;

		case EOneBrahamExtractionState::Failed:
		default:
			Colour = OBWinchAborted;
			Label = NSLOCTEXT("OneBraham", "Winch_LabelFailed", "WINCH ABORTED");
			break;
		}
	}

	if (LabelText != nullptr)
	{
		LabelText->SetText(Label);
	}

	// Written once per change: the light and the material are uninteresting when
	// they already say the right thing, and this actor ticks on every machine.
	if (Colour.Equals(AppliedColour, 0.001f))
	{
		return;
	}

	AppliedColour = Colour;

	if (ScreenLight != nullptr)
	{
		ScreenLight->SetLightColor(Colour);
	}

	if (ScreenMesh != nullptr)
	{
		ScreenMesh->SetVectorParameterValueOnMaterials(OBScreenTintParameter, FVector(Colour));
	}
}

void AOneBrahamExtractionTerminal::MulticastPlayTerminalEffects_Implementation(FName SoundId, float Volume, float Pitch)
{
	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
	{
		const FVector Location = ScreenMesh != nullptr ? ScreenMesh->GetComponentLocation() : GetActorLocation();
		Audio->PlaySoundAtLocation(SoundId, Location, Volume, Pitch, this);
	}
}

int32 AOneBrahamExtractionTerminal::ResolveCallingSquadId(const AOneBrahamPlayerController* Interactor) const
{
	const AOneBrahamPlayerState* PS = Interactor != nullptr
		? Interactor->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

	if (PS == nullptr)
	{
		return INDEX_NONE;
	}

	// The game state owns the squad table, and it is the same lookup the score,
	// the HUD and the winch component use - so the squad that calls is the squad
	// the HUD has been calling "yours" all match.
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	return GS != nullptr ? GS->GetSquadIdForPlayer(PS->GetPlayerId()) : INDEX_NONE;
}

