// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamOverrideTerminal.cpp

#include "Access/OneBrahamOverrideTerminal.h"

#include "OneBraham.h"
#include "Access/OneBrahamAccessZone.h"
#include "Access/OneBrahamAccessZoneComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Screen colours. Amber is "in use", red is "alarm", blue is "ready". */
	const FLinearColor OBScreenReady(0.05f, 0.42f, 0.85f);
	const FLinearColor OBScreenBusy(1.0f, 0.62f, 0.02f);
	const FLinearColor OBScreenOpen(0.05f, 0.85f, 0.32f);
	const FLinearColor OBScreenAlarm(1.0f, 0.06f, 0.04f);

	/** Stand this close to use a panel. Walls and desks are in the way, so this
	 *  is tighter than a door reader. */
	constexpr float OBTerminalInteractionRange = 200.f;

	/** Alarm strobe rate, in hertz. Deliberately the same as the shutter strobe:
	 *  one visual language for "the building is doing something to you". */
	constexpr float OBStrobeHz = 2.f;
}

AOneBrahamOverrideTerminal::AOneBrahamOverrideTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyMesh->SetCollisionObjectType(ECC_WorldStatic);
	BodyMesh->SetCollisionResponseToAllChannels(ECR_Block);
	BodyMesh->SetCanEverAffectNavigation(false);

	ScreenMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenMesh"));
	ScreenMesh->SetupAttachment(Root);
	ScreenMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScreenMesh->SetRelativeLocation(FVector(0.f, 18.f, 62.f));
	ScreenMesh->SetRelativeScale3D(FVector(0.5f, 0.02f, 0.34f));

	UseVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("UseVolume"));
	UseVolume->SetupAttachment(Root);
	UseVolume->SetBoxExtent(FVector(90.f, 90.f, 100.f));
	UseVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Interaction is a trace, not an overlap.
	UseVolume->SetCanEverAffectNavigation(false);

	ScreenLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ScreenLight"));
	ScreenLight->SetupAttachment(ScreenMesh);
	ScreenLight->SetRelativeLocation(FVector(-10.f, 30.f, 0.f));
	ScreenLight->SetIntensity(260.f);
	ScreenLight->SetAttenuationRadius(420.f);
	ScreenLight->SetLightColor(OBScreenReady);
	ScreenLight->SetCastShadows(false);
	ScreenLight->SetMobility(EComponentMobility::Movable);

	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelText"));
	LabelText->SetupAttachment(Root);
	LabelText->SetRelativeLocation(FVector(0.f, 16.f, 84.f));
	LabelText->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	LabelText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LabelText->SetWorldSize(14.f);
	LabelText->SetTextRenderColor(FColor(24, 26, 30));
	LabelText->SetText(FText::FromString(TEXT("ACCESS CONTROL")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeFinder.Object);
		ScreenMesh->SetStaticMesh(CubeFinder.Object);
	}

	BodyMesh->SetRelativeScale3D(FVector(0.6f, 0.3f, 1.2f));
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
}

void AOneBrahamOverrideTerminal::BeginPlay()
{
	Super::BeginPlay();

	ResolveTargetZone();

	// Wall-mounted panels are thinner and higher; a designer ticks one box.
	if (bWallMounted)
	{
		BodyMesh->SetRelativeScale3D(FVector(0.6f, 0.12f, 0.7f));
		BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 130.f));
		ScreenMesh->SetRelativeLocation(FVector(0.f, 10.f, 138.f));
		LabelText->SetRelativeLocation(FVector(0.f, 9.f, 152.f));
	}

	LabelText->SetText(GetTerminalDisplayName());

	ScreenLight->SetLightColor(bAlarmActive ? OBScreenAlarm : OBScreenReady);

	OB_ACCESS_LOG("Terminal '%s' online: target '%s', pair '%s', alarm %s",
		*TerminalId.ToString(),
		*TargetZoneId.ToString(),
		*PairedTerminalId.ToString(),
		bTripsAlarm ? "armed" : "silent");
}

void AOneBrahamOverrideTerminal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamOverrideTerminal, bAlarmActive);
}

void AOneBrahamOverrideTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	RefreshTargetZone();

	if (HasAuthority() && bAlarmActive && World->GetTimeSeconds() >= AlarmEndTime)
	{
		ServerClearAlarm();
	}

	// Screen state: blue idle, amber while held, green when the door is open, red
	// under alarm. The alarm wins, because the alarm is the thing that matters.
	FLinearColor Desired = OBScreenReady;
	float Intensity = 260.f;

	const AOneBrahamAccessZone* Target = GetTargetZone();
	const bool bOpen = Target != nullptr && Target->GetZone() != nullptr && Target->GetZone()->IsUnlocked();
	const bool bHeld = HoldInteractor.IsValid() ||
		(Target != nullptr && Target->GetZone() != nullptr && Target->GetZone()->IsOverrideInProgress());

	if (bAlarmActive)
	{
		const bool bBright = FMath::Fmod(World->GetTimeSeconds(), 1.f / OBStrobeHz) < (0.5f / OBStrobeHz);
		Desired = OBScreenAlarm;
		Intensity = bBright ? 1400.f : 120.f;
	}
	else if (bOpen)
	{
		Desired = OBScreenOpen;
		Intensity = 420.f;
	}
	else if (bHeld)
	{
		Desired = OBScreenBusy;
		Intensity = 620.f;
	}
	else if (bHighlighted)
	{
		Intensity = 380.f;
	}

	ScreenLight->SetLightColor(Desired);
	ScreenLight->SetIntensity(FMath::FInterpTo(ScreenLight->Intensity, Intensity, DeltaSeconds, 12.f));
}

// ===========================================================================
//  Binding
// ===========================================================================

void AOneBrahamOverrideTerminal::ResolveTargetZone()
{
	if (!TargetZoneId.IsNone())
	{
		if (AOneBrahamAccessZone* Found = AOneBrahamAccessZone::FindZoneById(this, TargetZoneId))
		{
			CachedZone = Found;
			return;
		}

		OB_WARN("Terminal '%s' targets zone '%s' which is not placed (yet).",
			*TerminalId.ToString(), *TargetZoneId.ToString());
	}

	// Proximity binding: the map places the panel beside the door it opens, so
	// nearest-within-radius is exact in practice and survives a designer moving
	// the panel without updating an id.
	TArray<AOneBrahamAccessZone*> Zones;
	AOneBrahamAccessZone::GetAllZones(this, Zones);

	AOneBrahamAccessZone* Best = nullptr;
	float BestDistanceSq = FMath::Square(ZoneLinkRadius);

	for (AOneBrahamAccessZone* Candidate : Zones)
	{
		if (Candidate == nullptr)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Candidate;
		}
	}

	CachedZone = Best;

	if (Best != nullptr && TargetZoneId.IsNone())
	{
		TargetZoneId = Best->GetZoneId();
		OB_ACCESS_LOG("Terminal '%s' bound by proximity to door '%s' (%.0f cm).",
			*TerminalId.ToString(), *TargetZoneId.ToString(), FMath::Sqrt(BestDistanceSq));
	}
}

AOneBrahamAccessZone* AOneBrahamOverrideTerminal::GetTargetZone() const
{
	// Queries are const and never do work: the cache is refreshed from Tick, which
	// is the only place streaming-related invalidation can be noticed anyway.
	return CachedZone.Get();
}

void AOneBrahamOverrideTerminal::RefreshTargetZone()
{
	if (CachedZone.IsValid())
	{
		return;
	}

	ResolveTargetZone();
}

FText AOneBrahamOverrideTerminal::GetTerminalDisplayName() const
{
	if (!TerminalDisplayName.IsEmpty())
	{
		return TerminalDisplayName;
	}

	return FText::FromName(TerminalId);
}

float AOneBrahamOverrideTerminal::GetOverrideProgress01() const
{
	const AOneBrahamAccessZone* Target = GetTargetZone();
	if (Target == nullptr || Target->GetZone() == nullptr)
	{
		return 0.f;
	}

	return Target->GetZone()->GetOverrideProgress01();
}

// ===========================================================================
//  Interaction
// ===========================================================================

FText AOneBrahamOverrideTerminal::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const AOneBrahamAccessZone* Target = GetTargetZone();
	UOneBrahamAccessZoneComponent* ZoneComponent = Target != nullptr ? Target->GetZone() : nullptr;

	if (ZoneComponent == nullptr)
	{
		return NSLOCTEXT("OneBraham", "Terminal_NoLink", "Access panel offline");
	}

	if (ZoneComponent->IsUnlocked())
	{
		return NSLOCTEXT("OneBraham", "Terminal_AlreadyOpen", "Already released");
	}

	if (IsPairedPanelHeld())
	{
		return NSLOCTEXT("OneBraham", "Terminal_Paired", "Hold {Interact} - paired override (3s)");
	}

	return ZoneComponent->GetInteractionPrompt(Interactor);
}

FGameplayTag AOneBrahamOverrideTerminal::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Terminal;
}

bool AOneBrahamOverrideTerminal::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Interactor == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
	if (PS != nullptr && PS->IsEliminated())
	{
		return false;
	}

	const AOneBrahamAccessZone* Target = GetTargetZone();
	if (Target == nullptr || Target->GetZone() == nullptr)
	{
		return false;
	}

	return !Target->GetZone()->IsUnlocked();
}

bool AOneBrahamOverrideTerminal::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return true; // Forcing a door is always a commitment.
}

float AOneBrahamOverrideTerminal::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const AOneBrahamAccessZone* Target = GetTargetZone();
	UOneBrahamAccessZoneComponent* ZoneComponent = Target != nullptr ? Target->GetZone() : nullptr;

	if (ZoneComponent == nullptr)
	{
		return 12.f;
	}

	// The panel reports the best case for the player standing here, and the zone
	// re-times the hold per frame as the partner arrives or leaves.
	return IsPairedPanelHeld()
		? ZoneComponent->OverrideTwoPersonSeconds
		: ZoneComponent->OverrideSeconds;
}

float AOneBrahamOverrideTerminal::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OBTerminalInteractionRange;
}

bool AOneBrahamOverrideTerminal::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr || !HasAuthority())
	{
		return false;
	}

	const AOneBrahamAccessZone* Target = GetTargetZone();
	if (Target == nullptr || Target->GetZone() == nullptr || Target->GetZone()->IsUnlocked())
	{
		return false;
	}

	HoldInteractor = Interactor;
	HoldLastSeconds = 0.f;
	bAlarmRaised = false;

	MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Hum")), 0.7f);

	OB_ACCESS_LOG("Terminal '%s': override started by %s.",
		*TerminalId.ToString(), *Interactor->GetName());

	return true;
}

void AOneBrahamOverrideTerminal::OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01)
{
	if (Interactor == nullptr || !HasAuthority() || !HoldInteractor.IsValid())
	{
		return;
	}

	AOneBrahamAccessZone* Target = GetTargetZone();
	if (Target == nullptr || Target->GetZone() == nullptr)
	{
		return;
	}

	// Delta timing for the zone's accumulator: the interact component reports
	// absolute held seconds, and the zone wants a per-frame increment.
	const UWorld* World = GetWorld();
	float Delta = World != nullptr ? World->GetDeltaSeconds() : 0.1f;
	if (HoldInteractor.Get() == Interactor)
	{
		Delta = FMath::Clamp(HeldSeconds - HoldLastSeconds, 0.f, 0.5f);
	}
	else
	{
		HoldInteractor = Interactor;
	}

	HoldLastSeconds = HeldSeconds;

	Target->GetZone()->ServerHoldOverride(Interactor, IsPairedPanelHeld(), Delta);

	// The alarm starts partway through a solo override and *always* finishes, so
	// a silent forced entry does not exist in this game.
	if (bTripsAlarm && !bAlarmRaised && HeldSeconds >= AlarmStartSeconds)
	{
		ServerRaiseAlarm();
	}
}

void AOneBrahamOverrideTerminal::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	if (Interactor == nullptr || !HasAuthority())
	{
		return;
	}

	AOneBrahamAccessZone* Target = GetTargetZone();
	const bool bOpened = bCompleted && Target != nullptr &&
		Target->GetZone() != nullptr && Target->GetZone()->IsUnlocked();

	if (bOpened)
	{
		MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Accept")), 0.9f);

		// Once it is open the alarm has done its job: it runs for its full
		// duration anyway, because that is the price of the shortcut and everyone
		// who heard it is already on their way.
		if (bTripsAlarm && !bAlarmRaised)
		{
			ServerRaiseAlarm();
		}
	}
	else if (Target != nullptr && Target->GetZone() != nullptr)
	{
		Target->GetZone()->ServerCancelOverride(Interactor);
		MulticastPlayTerminalEffects(FName(TEXT("SFX_Terminal_Deny")), 0.7f);
	}

	HoldInteractor.Reset();
	HoldLastSeconds = 0.f;
}

void AOneBrahamOverrideTerminal::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (HasAuthority())
	{
		if (AOneBrahamAccessZone* Target = GetTargetZone())
		{
			if (Target->GetZone() != nullptr)
			{
				Target->GetZone()->ServerCancelOverride(Interactor);
			}
		}
	}

	HoldInteractor.Reset();
	HoldLastSeconds = 0.f;
}

void AOneBrahamOverrideTerminal::SetInteractionHighlight_Implementation(bool bHighlightedIn)
{
	bHighlighted = bHighlightedIn;
}

// ===========================================================================
//  Alarm
// ===========================================================================

void AOneBrahamOverrideTerminal::ServerRaiseAlarm()
{
	if (!HasAuthority() || bAlarmRaised)
	{
		return;
	}

	bAlarmRaised = true;
	bAlarmActive = true;

	const UWorld* World = GetWorld();
	AlarmEndTime = (World != nullptr ? World->GetTimeSeconds() : 0.f) + AlarmDurationSeconds;

	ForceNetUpdate();
	OnRep_Alarm();

	OB_ACCESS_LOG("ALARM: terminal '%s' forced. Sounding for %.0f s.",
		*TerminalId.ToString(), AlarmDurationSeconds);
}

void AOneBrahamOverrideTerminal::ServerClearAlarm()
{
	if (!HasAuthority() || !bAlarmActive)
	{
		return;
	}

	bAlarmActive = false;
	ForceNetUpdate();
	OnRep_Alarm();

	OB_ACCESS_LOG("Alarm cleared at terminal '%s'.", *TerminalId.ToString());
}

void AOneBrahamOverrideTerminal::OnRep_Alarm()
{
	ApplyAlarmPresentation(bAlarmActive);
	OnAlarmChanged.Broadcast(TerminalId, bAlarmActive);
}

void AOneBrahamOverrideTerminal::ApplyAlarmPresentation(bool bActive)
{
	ScreenLight->SetLightColor(bActive ? OBScreenAlarm : OBScreenReady);

	if (bActive)
	{
		PlayTerminalEffects(FName(TEXT("SFX_Terminal_Deny")), 1.0f);
		PlayTerminalEffects(FName(TEXT("SFX_Lockdown_Siren")), 0.8f);
	}

	// The tannoy line. Written plainly, the way a building would say it: the game
	// gets its tension from the building being real, not from a joke.
	if (bActive && bAnnouncesOnTannoy && HasAuthority())
	{
		UWorld* World = GetWorld();
		AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

		if (GS != nullptr)
		{
			// Said the way a building says it. The humour in this game comes from
			// the building being exactly this polite while people are shooting.
			const FText Announcement = FText::Format(
				NSLOCTEXT("OneBraham", "Terminal_AlarmTannoy",
					"Security notice. Override attempt in progress at {0}. Security is responding."),
				GetTerminalDisplayName());

			GS->MulticastAnnounce(Announcement, /*bPlayTannoySound*/ true);
		}
	}
}

bool AOneBrahamOverrideTerminal::IsPairedPanelHeld() const
{
	const AOneBrahamOverrideTerminal* Partner = FindPairedTerminal();
	return Partner != nullptr && Partner->HoldInteractor.IsValid();
}

AOneBrahamOverrideTerminal* AOneBrahamOverrideTerminal::FindPairedTerminal() const
{
	if (PairedTerminalId.IsNone())
	{
		return nullptr;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<AOneBrahamOverrideTerminal> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->TerminalId == PairedTerminalId && *It != this)
		{
			return *It;
		}
	}

	return nullptr;
}

void AOneBrahamOverrideTerminal::PlayTerminalEffects(FName SoundId, float Volume, float Pitch)
{
	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
	{
		const FVector Location = ScreenMesh != nullptr ? ScreenMesh->GetComponentLocation() : GetActorLocation();
		Audio->PlaySoundAtLocation(SoundId, Location, Volume, Pitch, this);
	}
}

void AOneBrahamOverrideTerminal::MulticastPlayTerminalEffects_Implementation(FName SoundId, float Volume, float Pitch)
{
	PlayTerminalEffects(SoundId, Volume, Pitch);
}
