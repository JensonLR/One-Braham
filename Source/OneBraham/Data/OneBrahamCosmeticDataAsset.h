// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCosmeticDataAsset.h
// ---------------------------------------------------------------------------
// The cosmetic catalogue: which wardrobe items are earned rather than issued,
// and what earns them.
//
// The meshes and slots are the cast registry's (UOneBrahamCharacterDataAsset);
// this asset only says which ids are locked and which OB.Cosmetic.Unlock.* tag
// opens each one. An id with no row here is issued to everyone. The tags are
// earned by achievements (UOneBrahamSteamSubsystem) and kept in the profile.
//
// Cosmetic, therefore client-side: the server cannot see a player's profile
// and does not try. A locked item is simply never sent (SendProfileToServer
// leaves its slot to the archetype default), and nothing in it changes how
// the game plays.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OneBrahamCosmeticDataAsset.generated.h"

class UOneBrahamSaveSettings;

/** One earned cosmetic. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamCosmeticUnlock
{
	GENERATED_BODY()

	/** The wardrobe id, as in the cast registry's slot options. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FName CosmeticId = NAME_None;

	/** The OB.Cosmetic.Unlock.* tag that opens it, as a name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FName UnlockTag = NAME_None;

	/** What the wardrobe says under a locked item. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic")
	FText HowToUnlock;
};

UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamCosmeticDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UOneBrahamCosmeticDataAsset();

	/** Every earned cosmetic. The constructor fills the shipping four. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetics", meta = (TitleProperty = "CosmeticId"))
	TArray<FOneBrahamCosmeticUnlock> Unlocks;

	/** The row for an id, or null when the id is issued to everyone. */
	const FOneBrahamCosmeticUnlock* FindUnlock(FName CosmeticId) const;

	/** True when Profile may wear the id: no row, or its tag is earned. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Cosmetics")
	bool IsUnlockedFor(const UOneBrahamSaveSettings* Profile, FName CosmeticId) const;

	/**
	 * The catalogue the game runs on: DA_CosmeticCatalogue when the content
	 * build has made one, else the class defaults, which carry the same rows.
	 * Never null.
	 */
	static const UOneBrahamCosmeticDataAsset* LoadCatalogue();
};
