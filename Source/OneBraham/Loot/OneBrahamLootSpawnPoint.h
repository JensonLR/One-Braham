// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLootSpawnPoint.h
// ---------------------------------------------------------------------------
// A place where loot may appear: a desk, a shelf, a server-room trolley.
//
// The map builder places these in every room, several more than any floor's
// loot profile asks for, and the loot subsystem picks among them at the start
// of each match. That surplus is the point: the same rooms, different desks,
// so a squad that learned last match's rifle spot has to look again.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneBrahamLootSpawnPoint.generated.h"

/** What may spawn at a point. Bit values, combined in a point's CategoryMask. */
UENUM(BlueprintType)
enum class EOneBrahamLootCategory : uint8
{
	Weapon = 0,
	Ammo = 1,
	Utility = 2,
	Medkit = 3,
	Keycard = 4
};

namespace OneBrahamLoot
{
	/** The mask bit for one category. */
	constexpr int32 Bit(EOneBrahamLootCategory Category)
	{
		return 1 << static_cast<int32>(Category);
	}

	/** Everything but keycards: the default for a desk. */
	constexpr int32 DefaultMask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);
}

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamLootSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamLootSpawnPoint();

	/** Floor index, plaza 0 to Skydeck 15. The loot profiles are per floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Loot")
	int32 FloorIndex = 1;

	/** Which categories may spawn here, as OneBrahamLoot::Bit values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Loot")
	int32 CategoryMask = OneBrahamLoot::DefaultMask;

	/** False to take a point out of the draw without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Loot")
	bool bEnabled = true;

	UFUNCTION(BlueprintPure, Category = "One Braham|Loot")
	bool Allows(EOneBrahamLootCategory Category) const
	{
		return bEnabled && (CategoryMask & OneBrahamLoot::Bit(Category)) != 0;
	}

#if WITH_EDITORONLY_DATA
private:
	/** The marker in the editor viewport. Not in a cooked build. */
	UPROPERTY()
	TObjectPtr<class UBillboardComponent> EditorSprite = nullptr;
#endif
};
