// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamUILibrary.h
// ---------------------------------------------------------------------------
// What the menus need that the HUD does not: the achievement list with its
// unlock state, the wardrobe's lock state and the line under a locked item, a
// server row's ping band, and the Steam overlay. Styling (colours, fonts,
// clocks, distances) is UOneBrahamUIStyle's; this is data.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OneBrahamUILibrary.generated.h"

class UOneBrahamSaveSettings;

/** One achievement as the menu shows it. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamAchievementView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Achievement")
	FName AchievementId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement")
	bool bUnlocked = false;
};

/** How a server row's ping reads. */
UENUM(BlueprintType)
enum class EOneBrahamPingBand : uint8
{
	Good = 0,
	Playable = 1,
	Poor = 2
};

UCLASS()
class ONEBRAHAM_API UOneBrahamUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every achievement in menu order, with this profile's unlock state. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI", meta = (WorldContext = "WorldContextObject"))
	static TArray<FOneBrahamAchievementView> GetAchievements(const UObject* WorldContextObject);

	/** True when the local profile may wear a cosmetic id. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI", meta = (WorldContext = "WorldContextObject"))
	static bool IsCosmeticUnlocked(const UObject* WorldContextObject, FName CosmeticId);

	/** The line under a locked wardrobe item; empty for an unlocked one. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI", meta = (WorldContext = "WorldContextObject"))
	static FText GetCosmeticLockText(const UObject* WorldContextObject, FName CosmeticId);

	/** Under GoodPingMs good, up to the matchmaker's ceiling playable, beyond it poor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI")
	static EOneBrahamPingBand GetPingBand(int32 PingMs);

	/** Opens a Steam overlay page: 0 friends, 1 invite, 2 store. False without an overlay. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI", meta = (WorldContext = "WorldContextObject"))
	static bool OpenSteamOverlay(const UObject* WorldContextObject, int32 Page);

	/** Ping under this reads good. */
	static constexpr int32 GoodPingMs = 60;

private:
	static const UOneBrahamSaveSettings* LocalProfile(const UObject* WorldContextObject);
};
