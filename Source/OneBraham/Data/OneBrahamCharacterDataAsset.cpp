// Copyright (c) One Braham. All Rights Reserved.

#include "Data/OneBrahamCharacterDataAsset.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "OneBraham.h"

// ===========================================================================
//  Lookups
// ===========================================================================

const FOneBrahamArchetypeDefinition* UOneBrahamCharacterDataAsset::FindArchetype(const FGameplayTag& ArchetypeTag) const
{
	if (!ArchetypeTag.IsValid())
	{
		return Archetypes.Num() > 0 ? &Archetypes[0] : nullptr;
	}

	for (const FOneBrahamArchetypeDefinition& Definition : Archetypes)
	{
		if (Definition.ArchetypeTag == ArchetypeTag)
		{
			return &Definition;
		}
	}

	// An unknown tag is a content bug, not a crash: fall back to the default cast
	// member so a misconfigured player still deploys as a person.
	for (const FOneBrahamArchetypeDefinition& Definition : Archetypes)
	{
		if (Definition.ArchetypeTag == DefaultArchetypeTag)
		{
			return &Definition;
		}
	}

	return Archetypes.Num() > 0 ? &Archetypes[0] : nullptr;
}

const FOneBrahamArchetypeDefinition* UOneBrahamCharacterDataAsset::FindArchetypeByIndex(int32 Index) const
{
	return Archetypes.IsValidIndex(Index) ? &Archetypes[Index] : nullptr;
}

const FOneBrahamCosmeticSlot* UOneBrahamCharacterDataAsset::FindCosmeticSlot(FName SlotName) const
{
	for (const FOneBrahamCosmeticSlot& Slot : CosmeticSlots)
	{
		if (Slot.SlotName == SlotName)
		{
			return &Slot;
		}
	}

	return nullptr;
}

bool UOneBrahamCharacterDataAsset::IsValidCosmetic(FName CosmeticId) const
{
	return !FindSlotForCosmetic(CosmeticId).IsNone();
}

FName UOneBrahamCharacterDataAsset::FindSlotForCosmetic(FName CosmeticId) const
{
	if (CosmeticId.IsNone())
	{
		return NAME_None;
	}

	for (const FOneBrahamCosmeticSlot& Slot : CosmeticSlots)
	{
		if (Slot.Options.Contains(CosmeticId))
		{
			return Slot.SlotName;
		}
	}

	return NAME_None;
}

// ===========================================================================
//  Applying cosmetics to a body mesh
// ===========================================================================

USkeletalMeshComponent* UOneBrahamCharacterDataAsset::ApplyCosmeticToMesh(USkeletalMeshComponent* BodyMesh, const FOneBrahamCosmeticSlot* Slot, USkeletalMesh* CosmeticMesh) const
{
	if (BodyMesh == nullptr || Slot == nullptr || CosmeticMesh == nullptr || Slot->SlotName.IsNone())
	{
		return nullptr;
	}

	AActor* Owner = BodyMesh->GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	const FName ComponentName(*FString::Printf(TEXT("Cosmetic_%s"), *Slot->SlotName.ToString()));

	// Reuse an existing component for this slot: switching a jacket should not
	// grow the component list on a long session, and cosmetics are swapped in the
	// wardrobe and on deploy.
	USkeletalMeshComponent* Component = nullptr;
	for (UActorComponent* Existing : Owner->GetComponents())
	{
		if (Existing != nullptr && Existing->GetFName() == ComponentName)
		{
			Component = Cast<USkeletalMeshComponent>(Existing);
			break;
		}
	}

	if (Component == nullptr)
	{
		Component = NewObject<USkeletalMeshComponent>(Owner, ComponentName);
		if (Component == nullptr)
		{
			return nullptr;
		}

		Component->SetMobility(EComponentMobility::Movable);
		Component->SetupAttachment(BodyMesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(true);
		Component->bSelfShadowOnly = false;
		Component->SetReceivesDecals(true);
		Component->RegisterComponent();
	}

	// The cosmetic is a follower mesh: it copies the body's pose, which means one
	// animation evaluation per character no matter how many garments are on it.
	Component->SetLeaderPoseComponent(BodyMesh, /*bForceUpdate*/ true);

	Component->SetSkeletalMesh(CosmeticMesh);

	// bHiddenWhenArmed is applied by the character when the loadout changes: a
	// hi-vis vest is worn in the lobby and tucked under a plate carrier on the
	// deploy, which the character decides (it knows whether the player is armed).
	Component->SetVisibility(true);

	if (Slot->AttachSocket != NAME_None)
	{
		Component->AttachToComponent(BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Slot->AttachSocket);
	}
	else
	{
		Component->SetRelativeLocation(Slot->RelativeOffset);
		Component->SetRelativeRotation(Slot->RelativeRotation);
	}

	Component->SetVisibility(true);

	UE_LOG(LogOneBraham, Verbose, TEXT("[OB] Cosmetic slot '%s' -> '%s'"),
		*Slot->SlotName.ToString(), *CosmeticMesh->GetName());

	return Component;
}

void UOneBrahamCharacterDataAsset::ClearCosmeticFromMesh(USkeletalMeshComponent* BodyMesh, FName SlotName) const
{
	if (BodyMesh == nullptr || SlotName.IsNone())
	{
		return;
	}

	AActor* Owner = BodyMesh->GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	const FName ComponentName(*FString::Printf(TEXT("Cosmetic_%s"), *SlotName.ToString()));

	for (UActorComponent* Existing : Owner->GetComponents())
	{
		if (Existing != nullptr && Existing->GetFName() == ComponentName)
		{
			Existing->DestroyComponent();
			return;
		}
	}
}

// ===========================================================================
//  Validation
// ===========================================================================

bool UOneBrahamCharacterDataAsset::ValidateRegistry(TArray<FString>& OutProblems) const
{
	OutProblems.Reset();

	// --- The cast ---------------------------------------------------------
	if (Archetypes.Num() != 6)
	{
		OutProblems.Add(FString::Printf(
			TEXT("Cast should be exactly six archetypes (Business Analyst, Graduate, Finance, IT/Ops, Facilities/Security, Consultant); found %d."),
			Archetypes.Num()));
	}

	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < Archetypes.Num(); ++Index)
	{
		const FOneBrahamArchetypeDefinition& Definition = Archetypes[Index];

		if (!Definition.ArchetypeTag.IsValid())
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype %d has no gameplay tag."), Index));
		}
		else if (SeenTags.Contains(Definition.ArchetypeTag))
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype %d duplicates tag '%s'."), Index, *Definition.ArchetypeTag.ToString()));
		}
		else
		{
			SeenTags.Add(Definition.ArchetypeTag);
		}

		if (!Definition.ArchetypeTag.ToString().StartsWith(TEXT("OB.Character.Archetype.")))
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype '%s' is not under OB.Character.Archetype."), *Definition.ArchetypeTag.ToString()));
		}

		if (Definition.DisplayName.IsEmpty())
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype '%s' has no display name."), *Definition.ArchetypeTag.ToString()));
		}

		if (Definition.BodyMesh.IsNull())
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype '%s' has no body mesh (Docs/13 lists the MetaHuman presets)."), *Definition.ArchetypeTag.ToString()));
		}

		if (Definition.AnimInstanceClass == nullptr)
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype '%s' has no animation blueprint (ABP_OneBrahamCharacter)."), *Definition.ArchetypeTag.ToString()));
		}

		if (Definition.VoiceSetId.IsNone())
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype '%s' has no voice set id (Docs/14 lists the six casting briefs)."), *Definition.ArchetypeTag.ToString()));
		}

		// The balance rule, enforced by the data: no archetype may carry a stat.
		// (There is nowhere to put one, and this check documents that intent.)
		if (Definition.HomeFloor < 0 || Definition.HomeFloor > OneBrahamConstants::FloorSkydeck)
		{
			OutProblems.Add(FString::Printf(TEXT("Archetype '%s' has out-of-range home floor %d."), *Definition.ArchetypeTag.ToString(), Definition.HomeFloor));
		}
	}

	if (!DefaultArchetypeTag.IsValid())
	{
		OutProblems.Add(TEXT("DefaultArchetypeTag is not set."));
	}
	else if (!SeenTags.Contains(DefaultArchetypeTag))
	{
		OutProblems.Add(TEXT("DefaultArchetypeTag does not exist in Archetypes."));
	}

	// --- Cosmetics --------------------------------------------------------
	if (CosmeticSlots.Num() == 0)
	{
		OutProblems.Add(TEXT("No cosmetic slots defined; the wardrobe will be empty."));
	}

	TSet<FName> SeenSlots;
	for (const FOneBrahamCosmeticSlot& Slot : CosmeticSlots)
	{
		if (Slot.SlotName.IsNone())
		{
			OutProblems.Add(TEXT("A cosmetic slot has no name."));
			continue;
		}

		if (SeenSlots.Contains(Slot.SlotName))
		{
			OutProblems.Add(FString::Printf(TEXT("Duplicate cosmetic slot '%s'."), *Slot.SlotName.ToString()));
		}
		SeenSlots.Add(Slot.SlotName);

		if (Slot.Options.Num() == 0)
		{
			OutProblems.Add(FString::Printf(TEXT("Cosmetic slot '%s' has no options."), *Slot.SlotName.ToString()));
		}

		for (const TPair<FName, TSoftObjectPtr<USkeletalMesh>>& Option : Slot.Options)
		{
			if (Option.Value.IsNull())
			{
				OutProblems.Add(FString::Printf(TEXT("Cosmetic '%s' in slot '%s' has no mesh."),
					*Option.Key.ToString(), *Slot.SlotName.ToString()));
			}
		}
	}

	// --- Defaults referenced by archetypes --------------------------------
	for (const FOneBrahamArchetypeDefinition& Definition : Archetypes)
	{
		for (const TPair<FName, FName>& Pair : Definition.DefaultCosmetics)
		{
			const FOneBrahamCosmeticSlot* Slot = FindCosmeticSlot(Pair.Key);
			if (Slot == nullptr)
			{
				OutProblems.Add(FString::Printf(TEXT("Archetype '%s' defaults slot '%s', which does not exist."),
					*Definition.ArchetypeTag.ToString(), *Pair.Key.ToString()));
				continue;
			}

			if (!Slot->Options.Contains(Pair.Value))
			{
				OutProblems.Add(FString::Printf(TEXT("Archetype '%s' default cosmetic '%s' is not an option in slot '%s'."),
					*Definition.ArchetypeTag.ToString(), *Pair.Value.ToString(), *Pair.Key.ToString()));
			}
		}
	}

	for (const FString& Problem : OutProblems)
	{
		OB_AUDIT_LOG("Cast registry: %s", *Problem);
	}

	if (OutProblems.Num() == 0)
	{
		UE_LOG(LogOneBraham, Log, TEXT("[OB] Cast registry '%s' validates: %d archetypes, %d cosmetic slots."),
			*GetName(), Archetypes.Num(), CosmeticSlots.Num());
	}

	return OutProblems.Num() == 0;
}

#if WITH_EDITOR
void UOneBrahamCharacterDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Keep the shipping default in place without a designer having to remember it.
	if (Archetypes.Num() > 0 && !DefaultArchetypeTag.IsValid())
	{
		DefaultArchetypeTag = Archetypes[0].ArchetypeTag;
	}

	// Two slots that are always needed for the game's own systems: the lanyard
	// (identity) and the badge (access tier read at a glance in the lobby).
	// Everything else is content the wardrobe can grow into.
	if (CosmeticSlots.Num() == 0)
	{
		FOneBrahamCosmeticSlot Jacket;
		Jacket.SlotName = TEXT("Jacket");
		Jacket.DisplayName = NSLOCTEXT("OneBraham", "Slot_Jacket", "Jacket");
		Jacket.ParentBone = TEXT("spine_03");
		CosmeticSlots.Add(Jacket);

		FOneBrahamCosmeticSlot Lanyard;
		Lanyard.SlotName = TEXT("Lanyard");
		Lanyard.DisplayName = NSLOCTEXT("OneBraham", "Slot_Lanyard", "Lanyard");
		Lanyard.ParentBone = TEXT("spine_03");
		Lanyard.RelativeOffset = FVector(-4.f, 0.f, 12.f);
		CosmeticSlots.Add(Lanyard);

		FOneBrahamCosmeticSlot Badge;
		Badge.SlotName = TEXT("Badge");
		Badge.DisplayName = NSLOCTEXT("OneBraham", "Slot_Badge", "Access Badge");
		Badge.ParentBone = TEXT("spine_03");
		Badge.bHiddenWhenArmed = false;
		CosmeticSlots.Add(Badge);

		FOneBrahamCosmeticSlot Bag;
		Bag.SlotName = TEXT("Bag");
		Bag.DisplayName = NSLOCTEXT("OneBraham", "Slot_Bag", "Bag");
		Bag.ParentBone = TEXT("spine_03");
		CosmeticSlots.Add(Bag);

		FOneBrahamCosmeticSlot Headwear;
		Headwear.SlotName = TEXT("Headwear");
		Headwear.DisplayName = NSLOCTEXT("OneBraham", "Slot_Headwear", "Headwear");
		Headwear.ParentBone = TEXT("head");
		CosmeticSlots.Add(Headwear);
	}
}
#endif
