// Copyright (c) One Braham. All Rights Reserved.

#include "Access/OneBrahamDestructibleProp.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "UObject/ConstructorHelpers.h"
#include "VFX/OneBrahamVFXStatics.h"

AOneBrahamDestructibleProp::AOneBrahamDestructibleProp()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetNetDormancy(DORM_DormantAll);	// nothing changes until it breaks
	SetCanBeDamaged(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
}

void AOneBrahamDestructibleProp::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;

	// The blockout is the engine cube at the prop's size; an authored mesh
	// keeps its own scale.
	if (Mesh != nullptr && Mesh->GetStaticMesh() != nullptr
		&& Mesh->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/")))
	{
		Mesh->SetRelativeScale3D(SizeCm / 100.f);
	}

	if (bDestroyed)
	{
		PlayBreakLocally();
	}
}

void AOneBrahamDestructibleProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamDestructibleProp, bDestroyed);
}

float AOneBrahamDestructibleProp::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || bDestroyed || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	Health -= DamageAmount;
	if (Health > 0.f)
	{
		return DamageAmount;
	}

	FlushNetDormancy();
	bDestroyed = true;
	ForceNetUpdate();

	// The listen host does not get the OnRep.
	PlayBreakLocally();
	ServerApplyHazard(EventInstigator);

	OB_LOG("Prop %s broke (%s).", *PropId.ToString(), *UEnum::GetValueAsString(Hazard));
	return Applied > 0.f ? Applied : DamageAmount;
}

void AOneBrahamDestructibleProp::OnRep_Destroyed()
{
	if (bDestroyed)
	{
		PlayBreakLocally();
	}
}

void AOneBrahamDestructibleProp::PlayBreakLocally()
{
	// No longer cover: rounds and people go through where it stood. The wreck
	// stays as a crushed, tilted shape so the room still remembers it.
	if (Mesh != nullptr)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FVector Scale = Mesh->GetRelativeScale3D();
		Mesh->SetRelativeScale3D(FVector(Scale.X, Scale.Y, Scale.Z * 0.35f));
		Mesh->AddRelativeRotation(FRotator(0.f, 0.f, 9.f));
		Mesh->AddRelativeLocation(FVector(0.f, 0.f, -SizeCm.Z * 0.3f));
	}

	const FVector Centre = GetActorLocation();
	EOneBrahamVFXEffect Effect = EOneBrahamVFXEffect::DustPuff;
	FName Sound = TEXT("SFX_Bullet_Impact_Wood");

	switch (Hazard)
	{
	case EOneBrahamPropHazard::Electrical:
		Effect = EOneBrahamVFXEffect::SparkShower;
		Sound = TEXT("SFX_Power_Down");
		break;
	case EOneBrahamPropHazard::Water:
		Effect = EOneBrahamVFXEffect::WaterSpray;
		Sound = TEXT("SFX_Bullet_Impact_Glass");
		break;
	case EOneBrahamPropHazard::Extinguisher:
		Effect = EOneBrahamVFXEffect::SmokeCloud;
		Sound = TEXT("SFX_Smoke_Deploy");
		break;
	default:
		break;
	}

	UOneBrahamVFXStatics::SpawnEffect(this, Effect, Centre, FRotator::ZeroRotator,
		FVector(Hazard == EOneBrahamPropHazard::Extinguisher ? 0.45f : 1.f));

	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
	{
		Audio->PlaySoundAtLocation(Sound, Centre, 1.f, 1.f, this);
	}
}

void AOneBrahamDestructibleProp::ServerApplyHazard(AController* EventInstigator)
{
	if (Hazard != EOneBrahamPropHazard::Electrical || ShortDamage <= 0.f)
	{
		return;
	}

	// A short: everyone pressed against it gets a jolt, credited to the building.
	for (TActorIterator<AOneBrahamCharacter> It(GetWorld()); It; ++It)
	{
		AOneBrahamCharacter* Character = *It;
		UOneBrahamHealthComponent* CharacterHealth = Character->GetHealthComponent();
		if (CharacterHealth == nullptr || !CharacterHealth->IsAlive()
			|| FVector::Dist(Character->GetActorLocation(), GetActorLocation()) > ShortRadiusCm)
		{
			continue;
		}

		FOneBrahamDamageReport Report;
		Report.BaseDamage = ShortDamage;
		Report.FinalDamage = ShortDamage;
		Report.HitLocation = GetActorLocation();
		Report.CauseId = TEXT("ElectricalShort");
		Report.bLethal = ShortDamage >= CharacterHealth->GetHealth();
		CharacterHealth->ApplyDamage(ShortDamage, this, Report, nullptr);
	}
}
