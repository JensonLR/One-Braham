// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDeathRecap.h
// ---------------------------------------------------------------------------
// The death screen's answer to "what just happened": every hit you took in the
// last few seconds of your life, grouped by who dealt it - how many hits, how
// much damage, where they landed, from how far - and how much the person who
// finished you had left. It is what a killcam tells a player without a replay
// driver: who, with what, from where, and whether the fight was close.
//
// Pure: the health component records the hits on the server, this turns them
// into the lines the death screen shows, and the tests drive it directly.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"

/** One hit, as the server's health component saw it land. */
struct FOneBrahamHitRecord
{
	FString AttackerName;
	float HealthDamage = 0.f;
	float ArmourDamage = 0.f;
	EOneBrahamHitZone Zone = EOneBrahamHitZone::Torso;
	float DistanceCm = 0.f;
	double Time = 0.0;
};

namespace OneBrahamDeathRecap
{
	/** Hits older than this before the death are not part of the fight. */
	constexpr double WindowSeconds = 12.0;

	/** The last hits the recap keeps; older ones fall off the front. */
	constexpr int32 MaxRecords = 32;

	/** Appends a hit, dropping the oldest when full. */
	inline void Record(TArray<FOneBrahamHitRecord>& Hits, const FOneBrahamHitRecord& Hit)
	{
		if (Hits.Num() >= MaxRecords)
		{
			Hits.RemoveAt(0, Hits.Num() - MaxRecords + 1);
		}
		Hits.Add(Hit);
	}

	inline const TCHAR* ZoneWord(EOneBrahamHitZone Zone)
	{
		switch (Zone)
		{
		case EOneBrahamHitZone::Head:	return TEXT("head");
		case EOneBrahamHitZone::Neck:	return TEXT("neck");
		case EOneBrahamHitZone::Torso:	return TEXT("body");
		default:						return TEXT("limb");
		}
	}

	/**
	 * The recap lines, most damaging attacker first:
	 *   "Alex - 4 hits, 112 damage (1 head, 3 body) at 18 m"
	 *   "Alex had 63 health and 10 armour left"
	 * KillerHealth < 0 means the killer's state is unknown (left, or no killer).
	 */
	inline FString Summarise(const TArray<FOneBrahamHitRecord>& Hits, double DeathTime,
		const FString& KillerName, float KillerHealth, float KillerArmour)
	{
		struct FAttacker
		{
			FString Name;
			int32 Count = 0;
			float Damage = 0.f;
			float DistanceSum = 0.f;
			TMap<FString, int32> Zones;
		};

		TArray<FAttacker> Attackers;
		for (const FOneBrahamHitRecord& Hit : Hits)
		{
			if (DeathTime - Hit.Time > WindowSeconds || Hit.AttackerName.IsEmpty())
			{
				continue;
			}
			FAttacker* Found = Attackers.FindByPredicate([&Hit](const FAttacker& A) { return A.Name == Hit.AttackerName; });
			if (Found == nullptr)
			{
				Found = &Attackers.AddDefaulted_GetRef();
				Found->Name = Hit.AttackerName;
			}
			++Found->Count;
			Found->Damage += Hit.HealthDamage + Hit.ArmourDamage;
			Found->DistanceSum += Hit.DistanceCm;
			++Found->Zones.FindOrAdd(ZoneWord(Hit.Zone));
		}

		Attackers.Sort([](const FAttacker& A, const FAttacker& B) { return A.Damage > B.Damage; });

		TArray<FString> Lines;
		for (const FAttacker& A : Attackers)
		{
			TArray<FString> ZoneParts;
			for (const TCHAR* Word : { TEXT("head"), TEXT("neck"), TEXT("body"), TEXT("limb") })
			{
				if (const int32* N = A.Zones.Find(Word))
				{
					ZoneParts.Add(FString::Printf(TEXT("%d %s"), *N, Word));
				}
			}
			const int32 Metres = FMath::RoundToInt(A.DistanceSum / FMath::Max(1, A.Count) / 100.f);
			Lines.Add(FString::Printf(TEXT("%s - %d %s, %d damage (%s) at %d m"),
				*A.Name, A.Count, A.Count == 1 ? TEXT("hit") : TEXT("hits"),
				FMath::RoundToInt(A.Damage), *FString::Join(ZoneParts, TEXT(", ")), Metres));
		}

		if (!KillerName.IsEmpty() && KillerHealth >= 0.f)
		{
			Lines.Add(KillerArmour > 0.5f
				? FString::Printf(TEXT("%s had %d health and %d armour left"), *KillerName,
					FMath::RoundToInt(KillerHealth), FMath::RoundToInt(KillerArmour))
				: FString::Printf(TEXT("%s had %d health left"), *KillerName, FMath::RoundToInt(KillerHealth)));
		}

		return FString::Join(Lines, TEXT("\n"));
	}
}
