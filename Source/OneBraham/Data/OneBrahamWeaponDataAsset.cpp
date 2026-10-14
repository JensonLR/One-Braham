// Copyright (c) One Braham. All Rights Reserved.

#include "Data/OneBrahamWeaponDataAsset.h"

#include "OneBraham.h"

// ==========================================================================
//  UOneBrahamWeaponDefinition
// ==========================================================================
float UOneBrahamWeaponDefinition::GetDamageAtDistance(float Distance) const
{
	if (Distance <= Falloff.FullDamageRange)
	{
		return BaseDamage;
	}

	if (Distance >= Falloff.MinDamageRange)
	{
		return BaseDamage * Falloff.MinDamageMultiplier;
	}

	const float Alpha = (Distance - Falloff.FullDamageRange) / FMath::Max(1.f, Falloff.MinDamageRange - Falloff.FullDamageRange);
	const float Multiplier = FMath::Lerp(1.f, Falloff.MinDamageMultiplier, Alpha);
	return BaseDamage * Multiplier;
}

FVector2D UOneBrahamWeaponDefinition::GetRecoilStep(int32 ShotIndex) const
{
	if (Recoil.Steps.Num() == 0)
	{
		// Sensible default: mostly vertical climb with a gentle right-then-left arc.
		static const FVector2D DefaultSteps[] =
		{
			FVector2D(0.55f, 0.00f), FVector2D(0.48f, 0.08f), FVector2D(0.42f, 0.14f), FVector2D(0.38f, 0.16f),
			FVector2D(0.34f, 0.12f), FVector2D(0.32f, 0.02f), FVector2D(0.30f, -0.10f), FVector2D(0.28f, -0.18f),
			FVector2D(0.26f, -0.14f), FVector2D(0.24f, -0.06f)
		};
		const int32 Index = ShotIndex % UE_ARRAY_COUNT(DefaultSteps);
		return DefaultSteps[Index];
	}

	// Shot index walks the array; once past the end we clamp to the last few
	// steps and add a small randomised walk so sustained fire is not a laser.
	const int32 LastIndex = Recoil.Steps.Num() - 1;
	const FVector2D Step = Recoil.Steps[FMath::Clamp(ShotIndex, 0, LastIndex)];

	if (ShotIndex > LastIndex)
	{
		const float Overheat = FMath::Min(1.f, (ShotIndex - LastIndex) / 10.f);
		return FVector2D(Step.X * (1.f - Overheat * 0.25f), Step.Y + FMath::FRandRange(-Overheat, Overheat) * 0.25f);
	}

	return Step;
}

// ==========================================================================
//  UOneBrahamWeaponDataAsset
// ==========================================================================
UOneBrahamWeaponDefinition* UOneBrahamWeaponDataAsset::FindWeapon(FName WeaponId) const
{
	for (UOneBrahamWeaponDefinition* Definition : Weapons)
	{
		if (Definition != nullptr && Definition->WeaponId == WeaponId)
		{
			return Definition;
		}
	}
	return nullptr;
}

const FOneBrahamAttachmentDefinition* UOneBrahamWeaponDataAsset::FindAttachment(FName AttachmentId) const
{
	return Attachments.FindByPredicate([AttachmentId](const FOneBrahamAttachmentDefinition& Definition)
	{
		return Definition.AttachmentId == AttachmentId;
	});
}

const FOneBrahamWeaponSkin* UOneBrahamWeaponDataAsset::FindSkin(FName SkinId) const
{
	return Skins.FindByPredicate([SkinId](const FOneBrahamWeaponSkin& Skin)
	{
		return Skin.SkinId == SkinId;
	});
}

UOneBrahamWeaponDefinition* UOneBrahamWeaponDataAsset::RollWeaponForFloor(int32 FloorIndex, const FRandomStream& RandomStream) const
{
	// Build the candidate list with cumulative weights so the roll is O(n) with
	// no allocation of a second array.
	float TotalWeight = 0.f;
	TArray<TPair<UOneBrahamWeaponDefinition*, float>> Candidates;

	for (UOneBrahamWeaponDefinition* Definition : Weapons)
	{
		if (Definition == nullptr || Definition->LootWeight <= 0.f)
		{
			continue;
		}

		if (FloorIndex < Definition->MinFloor || FloorIndex > Definition->MaxFloor)
		{
			continue;
		}

		Candidates.Add({ Definition, Definition->LootWeight });
		TotalWeight += Definition->LootWeight;
	}

	if (Candidates.Num() == 0 || TotalWeight <= 0.f)
	{
		return nullptr;
	}

	FRandomStream Stream = RandomStream;
	const float Roll = Stream.FRandRange(0.f, TotalWeight);
	float Accumulator = 0.f;

	for (const TPair<UOneBrahamWeaponDefinition*, float>& Candidate : Candidates)
	{
		Accumulator += Candidate.Value;
		if (Roll <= Accumulator)
		{
			return Candidate.Key;
		}
	}

	return Candidates.Last().Key;
}

void UOneBrahamWeaponDataAsset::ValidateDefinitions(TArray<FString>& OutProblems) const
{
	OutProblems.Reset();

	TSet<FName> SeenIds;
	for (const UOneBrahamWeaponDefinition* Definition : Weapons)
	{
		if (Definition == nullptr)
		{
			OutProblems.Add(TEXT("Weapons array contains a null entry."));
			continue;
		}

		if (Definition->WeaponId.IsNone())
		{
			OutProblems.Add(FString::Printf(TEXT("Weapon '%s' has no WeaponId."), *Definition->GetName()));
			continue;
		}

		if (SeenIds.Contains(Definition->WeaponId))
		{
			OutProblems.Add(FString::Printf(TEXT("Duplicate weapon id '%s'."), *Definition->WeaponId.ToString()));
		}
		SeenIds.Add(Definition->WeaponId);

		// --- Sanity checks the QA automation also runs (see Docs/18) ------
		if (Definition->MagazineSize <= 0)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s': MagazineSize must be > 0."), *Definition->WeaponId.ToString()));
		}

		if (Definition->RoundsPerMinute <= 0.f || Definition->RoundsPerMinute > 1500.f)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s': RoundsPerMinute %.0f is outside the sane 1-1500 band."),
				*Definition->WeaponId.ToString(), Definition->RoundsPerMinute));
		}

		if (Definition->BaseDamage <= 0.f || Definition->BaseDamage > 200.f)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s': BaseDamage %.1f is outside the sane 1-200 band."),
				*Definition->WeaponId.ToString(), Definition->BaseDamage));
		}

		if (Definition->Falloff.MinDamageRange <= Definition->Falloff.FullDamageRange)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s': falloff ranges are inverted."), *Definition->WeaponId.ToString()));
		}

		if (Definition->AvailableFireModes.Num() == 0)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s': no fire modes defined."), *Definition->WeaponId.ToString()));
		}

		if (Definition->bPerShellReload && Definition->PerShellReloadTime <= 0.f)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s': per-shell reload has no reload time."), *Definition->WeaponId.ToString()));
		}

		// The one hard rule of the design: no weapon in ONE BRAHAM may kill a full
		// health player with a single torso shot from a rifle-class weapon.
		const float MaxTorsoDamage = Definition->GetDamageAtDistance(Definition->Falloff.FullDamageRange);
		if (Definition->Category != EOneBrahamWeaponCategory::MarksmanRifle &&
			Definition->Category != EOneBrahamWeaponCategory::PumpShotgun &&
			Definition->Category != EOneBrahamWeaponCategory::SemiAutoShotgun &&
			MaxTorsoDamage >= 100.f)
		{
			OutProblems.Add(FString::Printf(
				TEXT("'%s': %.1f damage one-shots a full-health torso hit - not allowed outside shotgun/marksman classes."),
				*Definition->WeaponId.ToString(), MaxTorsoDamage));
		}
	}

	// Attachment sanity: nothing may exceed a 1.15 multiplier in a beneficial
	// direction (that is the "no pay-to-win" rule expressed as a validator).
	for (const FOneBrahamAttachmentDefinition& Attachment : Attachments)
	{
		if (Attachment.RecoilMultiplier > 1.15f || Attachment.SpreadMultiplier > 1.15f)
		{
			OutProblems.Add(FString::Printf(
				TEXT("Attachment '%s' is overpowered: Recoil=%.2f Spread=%.2f (max 1.15)."),
				*Attachment.AttachmentId.ToString(), Attachment.RecoilMultiplier, Attachment.SpreadMultiplier));
		}

		if (Attachment.MagazineSizeDelta > 20)
		{
			OutProblems.Add(FString::Printf(TEXT("Attachment '%s' adds %d magazine rounds (max 20)."),
				*Attachment.AttachmentId.ToString(), Attachment.MagazineSizeDelta));
		}
	}

	OB_LOG("Weapon registry validated: %d weapons, %d attachments, %d skins, %d problem(s).",
		Weapons.Num(), Attachments.Num(), Skins.Num(), OutProblems.Num());
}
