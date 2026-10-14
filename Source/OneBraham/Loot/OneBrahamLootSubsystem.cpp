// Copyright (c) One Braham. All Rights Reserved.

#include "Loot/OneBrahamLootSubsystem.h"

#include "Access/OneBrahamKeycard.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Loot/OneBrahamLootSpawnPoint.h"
#include "Loot/OneBrahamSupplyPickup.h"
#include "Math/RandomStream.h"
#include "OneBraham.h"
#include "Weapons/OneBrahamPickupBase.h"

namespace
{
	int32 GOneBrahamLootSeed = 0;
	FAutoConsoleVariableRef CVarOneBrahamLootSeed(
		TEXT("ob.loot.seed"),
		GOneBrahamLootSeed,
		TEXT("Fixes the next match's loot draw. 0 (default) draws a seed from the clock; every match logs the one it used."),
		ECVF_Cheat);

	/** An ammo box is this share of each carried weapon's maximum reserve, in percent. */
	constexpr int32 AmmoBoxSharePercent = 35;

	/** Lifted off the desk so a box does not z-fight the surface it sits on. */
	constexpr float RestHeightCm = 4.f;

	/** Takes up to Count points allowing Category from Pool, at random. Taken points leave the pool. */
	TArray<AOneBrahamLootSpawnPoint*> TakePoints(TArray<AOneBrahamLootSpawnPoint*>& Pool,
		EOneBrahamLootCategory Category, int32 Count, FRandomStream& Stream)
	{
		TArray<AOneBrahamLootSpawnPoint*> Taken;

		for (int32 Pick = 0; Pick < Count; ++Pick)
		{
			TArray<int32> Candidates;
			for (int32 Index = 0; Index < Pool.Num(); ++Index)
			{
				if (Pool[Index]->Allows(Category))
				{
					Candidates.Add(Index);
				}
			}

			if (Candidates.Num() == 0)
			{
				break;
			}

			const int32 Chosen = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
			Taken.Add(Pool[Chosen]);
			Pool.RemoveAtSwap(Chosen);
		}

		return Taken;
	}

	FVector RestingPlace(const AOneBrahamLootSpawnPoint* Point)
	{
		return Point->GetActorLocation() + FVector(0.f, 0.f, RestHeightCm);
	}

	AOneBrahamSupplyPickup* SpawnSupply(UWorld* World, const FVector& Location, float Yaw,
		EOneBrahamSupplyKind Kind, FName UtilityId, int32 Amount)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AOneBrahamSupplyPickup* Supply = World->SpawnActor<AOneBrahamSupplyPickup>(
			AOneBrahamSupplyPickup::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Location), Params);

		if (Supply != nullptr)
		{
			Supply->ServerConfigure(Kind, UtilityId, Amount);
		}

		return Supply;
	}
}

bool UOneBrahamLootSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

const TArray<FName>& UOneBrahamLootSubsystem::UtilityIds()
{
	static const TArray<FName> Ids = {
		TEXT("Utility_Frag"), TEXT("Utility_Flashbang"), TEXT("Utility_Smoke"), TEXT("Utility_BreachCharge")
	};
	return Ids;
}

FName UOneBrahamLootSubsystem::ChooseWeapon(const UOneBrahamMatchSettingsAsset* Settings,
	const UOneBrahamWeaponDataAsset* Registry, int32 FloorIndex, FRandomStream& Stream,
	TArray<FName>* OutBundledUtilities)
{
	struct FCandidate
	{
		FName WeaponId;
		float Weight = 0.f;
		const TArray<FName>* Bundled = nullptr;
	};

	TArray<FCandidate> Candidates;
	float Total = 0.f;

	if (Settings != nullptr && Settings->WeaponSpawnTable.Num() > 0)
	{
		for (const FOneBrahamSpawnTableEntry& Entry : Settings->WeaponSpawnTable)
		{
			if (Entry.WeaponId != NAME_None && Entry.Weight > 0.f
				&& FloorIndex >= Entry.MinFloor && FloorIndex <= Entry.MaxFloor)
			{
				Candidates.Add({ Entry.WeaponId, Entry.Weight, &Entry.BundledUtilityIds });
				Total += Entry.Weight;
			}
		}
	}
	else if (Registry != nullptr)
	{
		// No mode table: the registry's own roll, by each definition's weight and gate.
		const UOneBrahamWeaponDefinition* Rolled = Registry->RollWeaponForFloor(FloorIndex, Stream);
		return Rolled != nullptr ? Rolled->WeaponId : NAME_None;
	}

	if (Candidates.Num() == 0 || Total <= 0.f)
	{
		return NAME_None;
	}

	float Roll = Stream.FRandRange(0.f, Total);
	for (const FCandidate& Candidate : Candidates)
	{
		Roll -= Candidate.Weight;
		if (Roll <= 0.f)
		{
			if (OutBundledUtilities != nullptr && Candidate.Bundled != nullptr)
			{
				*OutBundledUtilities = *Candidate.Bundled;
			}
			return Candidate.WeaponId;
		}
	}

	return Candidates.Last().WeaponId;
}

FOneBrahamLootReport UOneBrahamLootSubsystem::SpawnMatchLoot(const UOneBrahamMatchSettingsAsset* Settings,
	UOneBrahamWeaponDataAsset* Registry, int32 Seed)
{
	UWorld* World = GetWorld();
	if (bSpawned || World == nullptr || World->GetNetMode() == NM_Client || Settings == nullptr)
	{
		return LastReport;
	}

	bSpawned = true;

	FOneBrahamLootReport Report;
	Report.Seed = Seed != 0 ? Seed
		: (GOneBrahamLootSeed != 0 ? GOneBrahamLootSeed : static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7FFFFFFF));
	FRandomStream Stream(Report.Seed);

	// Points by floor, in a stable order so a seed means the same desks.
	TMap<int32, TArray<AOneBrahamLootSpawnPoint*>> PointsByFloor;
	for (TActorIterator<AOneBrahamLootSpawnPoint> It(World); It; ++It)
	{
		if (It->bEnabled)
		{
			PointsByFloor.FindOrAdd(It->FloorIndex).Add(*It);
			++Report.SpawnPoints;
		}
	}

	for (TPair<int32, TArray<AOneBrahamLootSpawnPoint*>>& Pair : PointsByFloor)
	{
		Pair.Value.Sort([](const AOneBrahamLootSpawnPoint& A, const AOneBrahamLootSpawnPoint& B)
		{
			return A.GetFName().LexicalLess(B.GetFName());
		});
	}

	for (const FOneBrahamFloorLootProfile& Profile : Settings->FloorLootProfiles)
	{
		TArray<AOneBrahamLootSpawnPoint*>* Found = PointsByFloor.Find(Profile.FloorIndex);
		TArray<AOneBrahamLootSpawnPoint*> Pool = Found != nullptr ? *Found : TArray<AOneBrahamLootSpawnPoint*>();

		const int32 Asked = Profile.WeaponSpawns + Profile.AmmoSpawns + Profile.UtilitySpawns
			+ Profile.MedkitSpawns + Profile.KeycardSpawns;

		// Keycards first: they are the scarcest points (only some rooms issue
		// cards) and the one item a floor cannot do without.
		for (AOneBrahamLootSpawnPoint* Point : TakePoints(Pool, EOneBrahamLootCategory::Keycard, Profile.KeycardSpawns, Stream))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			if (AOneBrahamKeycard* Card = World->SpawnActor<AOneBrahamKeycard>(AOneBrahamKeycard::StaticClass(),
				FTransform(FRotator(0.f, Stream.FRandRange(0.f, 360.f), 0.f), RestingPlace(Point)), Params))
			{
				Card->IssuedFloor = Profile.FloorIndex;
				Card->ApplyTier(Profile.KeycardTier);
				++Report.Keycards;
			}
		}

		for (AOneBrahamLootSpawnPoint* Point : TakePoints(Pool, EOneBrahamLootCategory::Weapon, Profile.WeaponSpawns, Stream))
		{
			TArray<FName> Bundled;
			const FName WeaponId = ChooseWeapon(Settings, Registry, Profile.FloorIndex, Stream, &Bundled);
			if (WeaponId == NAME_None)
			{
				continue;
			}

			const float Yaw = Stream.FRandRange(0.f, 360.f);
			if (AOneBrahamPickupBase::SpawnLoot(World, Registry, WeaponId, RestingPlace(Point), Yaw) != nullptr)
			{
				++Report.Weapons;

				// A table entry may bring its own utility: a breach charge beside
				// the shotgun on the server floor is a suggestion, not a coincidence.
				for (int32 Index = 0; Index < Bundled.Num(); ++Index)
				{
					const FVector Beside = RestingPlace(Point) + FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 35.f + 18.f * Index, 0.f));
					if (SpawnSupply(World, Beside, Yaw, EOneBrahamSupplyKind::Utility, Bundled[Index], 1) != nullptr)
					{
						++Report.Utilities;
					}
				}
			}
		}

		for (AOneBrahamLootSpawnPoint* Point : TakePoints(Pool, EOneBrahamLootCategory::Ammo, Profile.AmmoSpawns, Stream))
		{
			if (SpawnSupply(World, RestingPlace(Point), Stream.FRandRange(0.f, 360.f), EOneBrahamSupplyKind::Ammo,
				NAME_None, AmmoBoxSharePercent) != nullptr)
			{
				++Report.Ammo;
			}
		}

		for (AOneBrahamLootSpawnPoint* Point : TakePoints(Pool, EOneBrahamLootCategory::Utility, Profile.UtilitySpawns, Stream))
		{
			const FName UtilityId = UtilityIds()[Stream.RandRange(0, UtilityIds().Num() - 1)];
			if (SpawnSupply(World, RestingPlace(Point), Stream.FRandRange(0.f, 360.f), EOneBrahamSupplyKind::Utility,
				UtilityId, 1) != nullptr)
			{
				++Report.Utilities;
			}
		}

		for (AOneBrahamLootSpawnPoint* Point : TakePoints(Pool, EOneBrahamLootCategory::Medkit, Profile.MedkitSpawns, Stream))
		{
			if (SpawnSupply(World, RestingPlace(Point), Stream.FRandRange(0.f, 360.f), EOneBrahamSupplyKind::Medkit,
				NAME_None, 1) != nullptr)
			{
				++Report.Medkits;
			}
		}

		const int32 Short = Asked - (Found != nullptr ? FMath::Min(Asked, Found->Num()) : 0);
		if (Short > 0)
		{
			Report.Unplaced += Short;
			OB_WARN("Loot: floor %d asks for %d items and has %d spawn points; %d not placed.",
				Profile.FloorIndex, Asked, Found != nullptr ? Found->Num() : 0, Short);
		}
	}

	OB_LOG("Loot seed %d: %d weapons, %d ammo, %d utilities, %d medkits, %d keycards from %d points (%d unplaced).",
		Report.Seed, Report.Weapons, Report.Ammo, Report.Utilities, Report.Medkits, Report.Keycards,
		Report.SpawnPoints, Report.Unplaced);

	LastReport = Report;
	return Report;
}
