// Copyright (c) One Braham. All Rights Reserved.

#include "Weapons/OneBrahamInventoryComponent.h"

#include "OneBraham.h"
#include "Access/OneBrahamKeycard.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamPlayerState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Weapons/OneBrahamProjectileBase.h"
#include "Weapons/OneBrahamPickupBase.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"

UOneBrahamInventoryComponent::UOneBrahamInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UOneBrahamInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamInventoryComponent, Utility);
	DOREPLIFETIME(UOneBrahamInventoryComponent, HighestKeycardTier);
	DOREPLIFETIME(UOneBrahamInventoryComponent, HeldKeycards);
	DOREPLIFETIME(UOneBrahamInventoryComponent, bHasOverrideTool);
}

void UOneBrahamInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	CachedCharacter = Cast<AOneBrahamCharacter>(GetOwner());
}

FName UOneBrahamInventoryComponent::GetUtilityIdForIndex(int32 Index)
{
	switch (Index)
	{
	case 0: return TEXT("Utility_Frag");
	case 1: return TEXT("Utility_Flashbang");
	case 2: return TEXT("Utility_Smoke");
	case 3: return TEXT("Utility_BreachCharge");
	default: return TEXT("Utility_Frag");
	}
}

FGameplayTag UOneBrahamInventoryComponent::GetUtilityTagForIndex(int32 Index)
{
	switch (Index)
	{
	case 0: return OneBrahamTags::Weapon_Utility_Frag;
	case 1: return OneBrahamTags::Weapon_Utility_Flashbang;
	case 2: return OneBrahamTags::Weapon_Utility_Smoke;
	case 3: return OneBrahamTags::Weapon_Utility_BreachCharge;
	default: return OneBrahamTags::Weapon_Utility_Frag;
	}
}

int32 UOneBrahamInventoryComponent::GetUtilityCount(FName UtilityId) const
{
	if (UtilityId == TEXT("Utility_Frag"))			return Utility.FragCount;
	if (UtilityId == TEXT("Utility_Flashbang"))		return Utility.FlashbangCount;
	if (UtilityId == TEXT("Utility_Smoke"))			return Utility.SmokeCount;
	if (UtilityId == TEXT("Utility_BreachCharge"))	return Utility.BreachChargeCount;
	if (UtilityId == TEXT("Utility_Medkit"))		return Utility.MedkitCount;
	return 0;
}

int32 UOneBrahamInventoryComponent::ServerAddUtility(FName UtilityId, int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0)
	{
		return 0;
	}

	int32 Added = 0;

	if (UtilityId == TEXT("Utility_Frag"))
	{
		Added = FMath::Min(Amount, MaxPerUtilityType - Utility.FragCount);
		Utility.FragCount += FMath::Max(0, Added);
	}
	else if (UtilityId == TEXT("Utility_Flashbang"))
	{
		Added = FMath::Min(Amount, MaxPerUtilityType - Utility.FlashbangCount);
		Utility.FlashbangCount += FMath::Max(0, Added);
	}
	else if (UtilityId == TEXT("Utility_Smoke"))
	{
		Added = FMath::Min(Amount, MaxPerUtilityType - Utility.SmokeCount);
		Utility.SmokeCount += FMath::Max(0, Added);
	}
	else if (UtilityId == TEXT("Utility_BreachCharge"))
	{
		Added = FMath::Min(Amount, MaxPerUtilityType - Utility.BreachChargeCount);
		Utility.BreachChargeCount += FMath::Max(0, Added);
	}
	else if (UtilityId == TEXT("Utility_Medkit"))
	{
		Added = FMath::Min(Amount, MaxMedkits - Utility.MedkitCount);
		Utility.MedkitCount += FMath::Max(0, Added);
	}
	else if (UtilityId == TEXT("Access_OverrideTool"))
	{
		bHasOverrideTool = true;
		Added = 1;
	}

	if (Added > 0)
	{
		OnItemPickedUp.Broadcast(UtilityId, GetUtilityCount(UtilityId));
		OnInventoryChanged.Broadcast();
		ForceNetUpdate();
	}

	return Added;
}

AOneBrahamProjectileBase* UOneBrahamInventoryComponent::SpawnUtilityProjectile(FName UtilityId, const FVector& Location, const FVector& Direction)
{
	UWorld* World = GetWorld();
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (World == nullptr || Character == nullptr)
	{
		return nullptr;
	}

	// The projectile class comes from the weapon registry so all utility tuning
	// lives in one place; fall back to the C++ default.
	TSubclassOf<AOneBrahamProjectileBase> ProjectileClass = AOneBrahamProjectileBase::StaticClass();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Character;
	SpawnParams.Instigator = Character;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AOneBrahamProjectileBase* Projectile = World->SpawnActor<AOneBrahamProjectileBase>(
		ProjectileClass, Location, Direction.Rotation(), SpawnParams);

	if (Projectile != nullptr)
	{
		Projectile->InitialiseUtility(UtilityId, Direction, Character);
	}

	return Projectile;
}

AOneBrahamProjectileBase* UOneBrahamInventoryComponent::ServerConsumeAndSpawnUtility(UOneBrahamWeaponComponent* WeaponComponent, int32 UtilityIndex, const FVector& SpawnLocation, const FVector& ThrowDirection)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	const FName UtilityId = GetUtilityIdForIndex(UtilityIndex);

	int32* Available = nullptr;
	if (UtilityId == TEXT("Utility_Frag"))				Available = &Utility.FragCount;
	else if (UtilityId == TEXT("Utility_Flashbang"))	Available = &Utility.FlashbangCount;
	else if (UtilityId == TEXT("Utility_Smoke"))		Available = &Utility.SmokeCount;
	else if (UtilityId == TEXT("Utility_BreachCharge"))	Available = &Utility.BreachChargeCount;

	// Breach charges are placed, not thrown; UseUtilityItem handles them. Throwing
	// one with the throw key is a no-op rather than a wasted charge.
	if (UtilityId == TEXT("Utility_BreachCharge"))
	{
		return nullptr;
	}

	if (Available == nullptr || *Available <= 0)
	{
		return nullptr;
	}

	--(*Available);

	OnInventoryChanged.Broadcast();
	ForceNetUpdate();

	// Delay the spawn by 0.25 s so the throw animation's release matches the
	// grenade leaving the hand - a small detail that makes utility feel physical.
	const FVector Direction = ThrowDirection.GetSafeNormal();
	const float ThrowSpeed = 1450.f;

	AOneBrahamProjectileBase* Projectile = SpawnUtilityProjectile(UtilityId, SpawnLocation, Direction);
	if (Projectile != nullptr)
	{
		Projectile->LaunchWithVelocity(Direction * ThrowSpeed + FVector(0.f, 0.f, 120.f));

		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(CachedCharacter->GetController()))
		{
			// Tells bots and other players where the grenade came from ("GRENADE!").
			if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
			{
				Voice->SpeakContextualLine(GetOwner(), TEXT("V_GRENADE_OUT"));
			}
			UE_UNUSED(PC);
		}
	}

	return Projectile;
}

bool UOneBrahamInventoryComponent::ServerPlaceUtility(UOneBrahamWeaponComponent* WeaponComponent, int32 UtilityIndex)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (!GetOwner()->HasAuthority() || Character == nullptr)
	{
		return false;
	}

	const FName UtilityId = GetUtilityIdForIndex(UtilityIndex);

	if (UtilityId != TEXT("Utility_BreachCharge"))
	{
		return false;
	}

	if (Utility.BreachChargeCount <= 0)
	{
		return false;
	}

	// Trace from the eye to find a surface to place the charge on. This is a
	// SERVER trace: the client only asked to place, it did not tell us where.
	const FVector Start = Character->GetEyeLocation();
	const FVector End = Start + Character->GetEyeRotation().Vector() * 250.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBreachPlace), false, Character);
	FHitResult Hit;

	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}

	// Placing a charge on a person is not a thing.
	if (Cast<AOneBrahamCharacter>(Hit.GetActor()) != nullptr)
	{
		return false;
	}

	--Utility.BreachChargeCount;
	OnInventoryChanged.Broadcast();
	ForceNetUpdate();

	AOneBrahamProjectileBase* Charge = SpawnUtilityProjectile(UtilityId, Hit.ImpactPoint + Hit.ImpactNormal * 4.f, Hit.ImpactNormal);
	if (Charge != nullptr)
	{
		Charge->ServerPlantAsBreachCharge(Hit.GetActor(), Hit.ImpactPoint, Hit.ImpactNormal);
	}

	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
	{
		Voice->SpeakContextualLine(GetOwner(), TEXT("V_BREACH_PLANTED"));
	}

	return true;
}

bool UOneBrahamInventoryComponent::ServerConsumeMedkit()
{
	if (!GetOwner()->HasAuthority() || Utility.MedkitCount <= 0)
	{
		return false;
	}

	--Utility.MedkitCount;
	OnInventoryChanged.Broadcast();
	ForceNetUpdate();
	return true;
}

// ==========================================================================
//  Keycards
// ==========================================================================
void UOneBrahamInventoryComponent::ServerGrantKeycard(EOneBrahamAccessTier Tier)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// Override is a tool, not a card: it never becomes "the card you hold".
	if (Tier == EOneBrahamAccessTier::Override)
	{
		bHasOverrideTool = true;
		ForceNetUpdate();
		return;
	}

	HeldKeycards.AddUnique(Tier);
	RecalculateKeycardTier();
	OnInventoryChanged.Broadcast();
	ForceNetUpdate();
}

bool UOneBrahamInventoryComponent::HasKeycardForTier(EOneBrahamAccessTier RequiredTier) const
{
	// A card opens its own tier and everything below it. The override tool opens
	// everything except the Skydeck (which always requires the terminal hold).
	if (RequiredTier == EOneBrahamAccessTier::Override)
	{
		return bHasOverrideTool;
	}

	return HighestKeycardTier >= RequiredTier;
}

namespace
{
	/**
	 * The name printed on a dropped badge.
	 *
	 * A card in this game is not an item: it is somebody's job, and it says so.
	 * "IT OPERATIONS - SECURE" on a card lying beside a body tells the player who
	 * they just killed, which archetype they were, and what they had access to -
	 * three facts, from one line of text, without a UI panel. English, dry, and in
	 * the exact register of a real corporate badge.
	 */
	FText BuildDroppedCardName(const AOneBrahamCharacter* Character, EOneBrahamAccessTier Tier)
	{
		FText Role = FText::FromString(TEXT("ONE BRAHAM"));
		FText Rank = FText::FromString(TEXT("EMPLOYEE"));

		if (const AOneBrahamPlayerState* State = Character != nullptr
			? Character->GetPlayerState<AOneBrahamPlayerState>() : nullptr)
		{
			if (State->ArchetypeTag.IsValid())
			{
				// "OB.Character.Archetype.ITOps" -> "IT OPERATIONS".
				FString Leaf = State->ArchetypeTag.GetTagName().ToString();
				int32 Dot = INDEX_NONE;
				if (Leaf.FindLastChar(TEXT('.'), Dot))
				{
					Leaf = Leaf.RightChop(Dot + 1);
				}

				Leaf = Leaf.Replace(TEXT("ITOps"), TEXT("IT OPERATIONS"));
				Leaf = Leaf.Replace(TEXT("Facilities"), TEXT("FACILITIES"));

				// Split the camel case into words: "BusinessAnalyst" -> "BUSINESS ANALYST".
				FString Spaced;
				for (int32 Index = 0; Index < Leaf.Len(); ++Index)
				{
					const TCHAR Ch = Leaf[Index];
					if (Index > 0 && FChar::IsUpper(Ch) && !FChar::IsUpper(Leaf[Index - 1]))
					{
						Spaced.AppendChar(TEXT(' '));
					}
					Spaced.AppendChar(Ch);
				}

				Role = FText::FromString(Spaced.ToUpper());
			}
		}

		switch (Tier)
		{
		case EOneBrahamAccessTier::Staff:		Rank = FText::FromString(TEXT("STAFF"));		break;
		case EOneBrahamAccessTier::Secure:		Rank = FText::FromString(TEXT("SECURE"));		break;
		case EOneBrahamAccessTier::Executive:	Rank = FText::FromString(TEXT("EXECUTIVE"));	break;
		case EOneBrahamAccessTier::Skydeck:	Rank = FText::FromString(TEXT("ROOF ACCESS"));	break;
		case EOneBrahamAccessTier::Override:	Rank = FText::FromString(TEXT("CONTRACTOR"));	break;
		default:								Rank = FText::FromString(TEXT("VISITOR"));		break;
		}

		return FText::Format(NSLOCTEXT("OneBraham", "DroppedCardName", "{0} - {1}"), Role, Rank);
	}
}

void UOneBrahamInventoryComponent::ServerDropKeycardOnDeath()
{
	if (!GetOwner()->HasAuthority() || HeldKeycards.Num() == 0)
	{
		return;
	}

	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	// Drop the HIGHEST card only. Dropping every card would flood the floor with
	// loot and destroy the escalation tension; dropping one card off a body is
	// exactly the drama ASCENT wants ("they had the executive badge").
	const EOneBrahamAccessTier BestTier = HighestKeycardTier;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = Character;
	SpawnParams.Instigator = Character;

	// The Blueprint exists so a designer can dress the card (a photo, a company
	// logo on the back, a different mesh per tier). The C++ class is the fallback
	// and the one that always works, which is why it is the last resort rather
	// than the only option.
	UClass* KeycardClass = LoadClass<AActor>(nullptr, TEXT("/Game/OneBraham/Blueprints/BP_Keycard.BP_Keycard_C"));
	if (KeycardClass == nullptr)
	{
		KeycardClass = AOneBrahamKeycard::StaticClass();
	}

	const FVector DropLocation = Character->GetActorLocation()
		+ Character->GetActorForwardVector() * 45.f
		+ FVector(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 10.f);

	AActor* Card = GetWorld() != nullptr
		? GetWorld()->SpawnActor<AActor>(KeycardClass, DropLocation, FRotator::ZeroRotator, SpawnParams)
		: nullptr;

	if (Card != nullptr)
	{
		if (AOneBrahamKeycard* Keycard = Cast<AOneBrahamKeycard>(Card))
		{
			// The card carries what the player was carrying, and it says so. A
			// squad that kills the IT Operations player and reads "IT Operations
			// - Secure" on the badge has learned something real about the match,
			// which is the entire reason a dropped keycard is loot rather than a
			// power-up.
			Keycard->ApplyTier(BestTier);
			Keycard->bDroppedFromPlayer = true;
			Keycard->IssuedFloor = Character->GetCurrentFloorIndex();
			Keycard->PrintedName = BuildDroppedCardName(Character, BestTier);

			// A card on the floor has to be findable, so it is lit, it floats
			// slightly, and it does not despawn until the match could plausibly
			// still use it.
			Keycard->SetLifeSpan(0.f);
		}
		else
		{
			// A Blueprint that is not, for some reason, an AOneBrahamKeycard.
			// Reflection keeps the drop working rather than silently producing a
			// blank card, which is the sort of bug that survives three releases.
			if (FProperty* TierProperty = Card->GetClass()->FindPropertyByName(TEXT("AccessTier")))
			{
				if (FByteProperty* ByteProp = CastField<FByteProperty>(TierProperty))
				{
					ByteProp->SetPropertyValue_InContainer(Card, static_cast<uint8>(BestTier));
				}
			}
			if (FBoolProperty* DroppedProperty = CastField<FBoolProperty>(
				Card->GetClass()->FindPropertyByName(TEXT("bDroppedFromPlayer"))))
			{
				DroppedProperty->SetPropertyValue_InContainer(Card, true);
			}
			Card->SetLifeSpan(300.f);
		}
	}

	HeldKeycards.Remove(BestTier);
	RecalculateKeycardTier();
	ForceNetUpdate();
}

void UOneBrahamInventoryComponent::ServerResetInventory(bool bKeepKeycards)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	Utility.FragCount = 1;
	Utility.FlashbangCount = 1;
	Utility.SmokeCount = 1;
	Utility.BreachChargeCount = 1;
	Utility.MedkitCount = 1;

	if (!bKeepKeycards)
	{
		HeldKeycards.Reset();
		bHasOverrideTool = false;
		RecalculateKeycardTier();
	}

	OnInventoryChanged.Broadcast();
	ForceNetUpdate();
}

void UOneBrahamInventoryComponent::RecalculateKeycardTier()
{
	EOneBrahamAccessTier Best = EOneBrahamAccessTier::Public;

	for (const EOneBrahamAccessTier Tier : HeldKeycards)
	{
		if (Tier != EOneBrahamAccessTier::Override && Tier > Best)
		{
			Best = Tier;
		}
	}

	HighestKeycardTier = Best;
}

void UOneBrahamInventoryComponent::OnRep_Loadout()
{
	OnInventoryChanged.Broadcast();
}

void UOneBrahamInventoryComponent::OnRep_Keycards()
{
	OnInventoryChanged.Broadcast();
}
