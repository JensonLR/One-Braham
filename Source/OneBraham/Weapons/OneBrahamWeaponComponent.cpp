// Copyright (c) One Braham. All Rights Reserved.

#include "Weapons/OneBrahamWeaponComponent.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Characters/OneBrahamCharacterMovementComponent.h"
#include "UI/OneBrahamHUDComponent.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamPickupBase.h"
#include "Weapons/OneBrahamProjectileBase.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

UOneBrahamWeaponComponent::UOneBrahamWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	SpreadStream.Initialize(FMath::Rand());

	// The standard-issue kit: the same four utility types the default profile is
	// created with, so a bot, a joining client and a fresh install all carry the
	// same thing.
	DefaultUtilityIds = {
		TEXT("Utility_Frag"),
		TEXT("Utility_Flashbang"),
		TEXT("Utility_Smoke"),
		TEXT("Utility_BreachCharge")
	};
}

void UOneBrahamWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamWeaponComponent, CurrentWeapon);
	DOREPLIFETIME(UOneBrahamWeaponComponent, CurrentSlot);
	DOREPLIFETIME(UOneBrahamWeaponComponent, bAimingDownSights);
	DOREPLIFETIME(UOneBrahamWeaponComponent, bIsReloading);
	DOREPLIFETIME(UOneBrahamWeaponComponent, CurrentFireMode);
	DOREPLIFETIME(UOneBrahamWeaponComponent, SelectedUtilityIndex);
	DOREPLIFETIME(UOneBrahamWeaponComponent, bMeleeOnCooldown);
	DOREPLIFETIME(UOneBrahamWeaponComponent, ReplicatedReserveAmmo);
	DOREPLIFETIME(UOneBrahamWeaponComponent, ReplicatedOwnedWeapons);
}

void UOneBrahamWeaponComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	ReplicatedReserveAmmo.Reset(ReserveAmmoByWeapon.Num());
	for (const TPair<FName, int32>& Pair : ReserveAmmoByWeapon)
	{
		ReplicatedReserveAmmo.Add({ Pair.Key, Pair.Value });
	}

	ReplicatedOwnedWeapons.Reset(OwnedWeapons.Num());
	for (const TPair<EOneBrahamWeaponSlot, TObjectPtr<AOneBrahamWeaponBase>>& Pair : OwnedWeapons)
	{
		ReplicatedOwnedWeapons.Add({ Pair.Key, Pair.Value });
	}
}

void UOneBrahamWeaponComponent::OnRep_ReserveAmmo()
{
	ReserveAmmoByWeapon.Reset();
	for (const FOneBrahamReserveAmmoEntry& Entry : ReplicatedReserveAmmo)
	{
		ReserveAmmoByWeapon.Add(Entry.WeaponId, Entry.Rounds);
	}
}

void UOneBrahamWeaponComponent::OnRep_OwnedWeapons()
{
	// A weapon actor that has not reached this client yet arrives as null; the
	// engine calls this again once it resolves, so null entries are skipped
	// rather than stored.
	OwnedWeapons.Reset();
	for (const FOneBrahamOwnedWeaponEntry& Entry : ReplicatedOwnedWeapons)
	{
		if (Entry.Weapon != nullptr)
		{
			OwnedWeapons.Add(Entry.Slot, Entry.Weapon);
		}
	}
}

void UOneBrahamWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<AOneBrahamCharacter>(GetOwner());

	// Fall back to the project-wide registry if the Blueprint did not set one, so
	// a code-only checkout still has working weapons. Project Settings ->
	// One Braham -> Content wins when it is set: that is the field a designer
	// edits, and a field nobody reads is worse than no field at all.
	if (WeaponRegistry == nullptr)
	{
		WeaponRegistry = UOneBrahamGameSettings::Get().WeaponRegistry.LoadSynchronous();
	}

	if (WeaponRegistry == nullptr)
	{
		WeaponRegistry = LoadObject<UOneBrahamWeaponDataAsset>(nullptr,
			TEXT("/Game/OneBraham/Data/DA_WeaponRegistry.DA_WeaponRegistry"));
	}

	if (WeaponRegistry == nullptr)
	{
		OB_ERROR("No weapon registry on '%s'. Weapons will not function. "
			"Run Tools/Python/create_data_assets.py and assign DA_WeaponRegistry.", *GetOwner()->GetName());
	}

	HitboxHistory.Reserve(64);
}

AOneBrahamCharacter* UOneBrahamWeaponComponent::GetCharacter() const
{
	return CachedCharacter.IsValid() ? CachedCharacter.Get() : Cast<AOneBrahamCharacter>(GetOwner());
}

AOneBrahamPlayerController* UOneBrahamWeaponComponent::GetOneBrahamPlayerController() const
{
	const AOneBrahamCharacter* Character = GetCharacter();
	return Character != nullptr ? Cast<AOneBrahamPlayerController>(Character->GetController()) : nullptr;
}

// ==========================================================================
//  Tick: local prediction, recoil recovery, lag-comp history
// ==========================================================================
void UOneBrahamWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return;
	}

	const bool bLocallyControlled = Character->IsLocallyControlled();
	const bool bAuthority = GetOwner()->HasAuthority();

	// --- ADS blend --------------------------------------------------------
	const float TargetADS = bAimingDownSights ? 1.f : 0.f;
	const float ADSSpeed = CurrentWeapon != nullptr ? 1.f / FMath::Max(0.05f, CurrentWeapon->GetEffectiveADSTime()) : 5.f;
	ADSAlpha = FMath::FInterpTo(ADSAlpha, TargetADS, DeltaTime, ADSSpeed);

	if (CurrentWeapon != nullptr)
	{
		// The first-person weapon pose is a blend of hip and ADS positions; the
		// anim BP reads this alpha and the camera FOV lerps with it.
		if (bLocallyControlled)
		{
			if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
			{
				const float HipFOV = UOneBrahamGameUserSettings::GetOneBrahamSettings() != nullptr
					? UOneBrahamGameUserSettings::GetOneBrahamSettings()->FOV : 90.f;

				float TargetFOV = HipFOV;
				if (CurrentWeapon->Definition != nullptr)
				{
					// Optics override the base ADS FOV, which is why a 3x sight
					// genuinely changes how the executive floor plays.
					float Magnification = 1.f;
					const FName OpticId = CurrentWeapon->GetAttachmentInSlot(EOneBrahamAttachmentSlot::Optic);
					if (!OpticId.IsNone())
					{
						if (const FOneBrahamAttachmentDefinition* Optic = GetAttachmentDefinition(OpticId))
						{
							Magnification = FMath::Max(1.f, Optic->Magnification);
						}
					}
					TargetFOV = CurrentWeapon->Definition->ADSFieldOfView / Magnification;
				}

				PC->PlayerCameraManager->SetFOV(FMath::Lerp(HipFOV, TargetFOV, ADSAlpha));
			}
		}
	}

	// --- Bloom decay ------------------------------------------------------
	if (CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr && !bTriggerHeld)
	{
		CurrentBloom = FMath::Max(0.f, CurrentBloom - CurrentWeapon->Definition->Spread.BloomDecayPerSecond * DeltaTime);
	}

	// --- Recoil recovery --------------------------------------------------
	ConsumeRecoil(DeltaTime);

	// --- Trigger handling -------------------------------------------------
	if (bTriggerHeld && CurrentWeapon != nullptr && CurrentWeapon->GetWeaponState() == EOneBrahamWeaponState::Ready)
	{
		// Automatic fire is driven by the player's own machine; the server
		// validates each shot against its own clock and shoots back any that
		// arrive too fast.
		const bool bIsAutomatic = CurrentFireMode == EOneBrahamFireMode::Auto;
		const bool bBurst = CurrentFireMode == EOneBrahamFireMode::Burst;

		if (bIsAutomatic || bBurst)
		{
			const float Now = GetWorld()->GetTimeSeconds();
			const float MinInterval = CurrentWeapon->Definition->GetSecondsBetweenShots();

			if (Now - LastLocalFireTime >= MinInterval)
			{
				// Burst mode fires three rounds then requires a re-press.
				if (bBurst)
				{
					if (ServerBurstRemaining <= 0)
					{
						ServerBurstRemaining = 3;
					}
					if (ServerBurstRemaining > 0)
					{
						++ClientShotIndex;
						LastLocalFireTime = Now;
						--ServerBurstRemaining;
						PredictLocalShot();
						ServerRequestFire(Character->GetEyeLocation(), Character->GetEyeRotation().Vector(), ClientShotIndex);
					}
				}
				else
				{
					++ClientShotIndex;
					LastLocalFireTime = Now;
					PredictLocalShot();
					ServerRequestFire(Character->GetEyeLocation(), Character->GetEyeRotation().Vector(), ClientShotIndex);
				}
			}
		}
	}

	// --- Weapon visual kick decay ----------------------------------------
	WeaponKickOffset = FMath::Vector2DInterpTo(WeaponKickOffset, FVector2D::ZeroVector, DeltaTime, WeaponKickRecoverySpeed);
	WeaponKickRoll = FMath::FInterpTo(WeaponKickRoll, 0.f, DeltaTime, WeaponKickRecoverySpeed);

	// --- Lag compensation snapshots (server only) -------------------------
	if (bAuthority)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastSnapshotTime >= (1.f / 30.f))
		{
			LastSnapshotTime = Now;
			CaptureHitboxSnapshot();
		}
	}
}

// ==========================================================================
//  Equipping
// ==========================================================================
AOneBrahamWeaponBase* UOneBrahamWeaponComponent::ServerGiveWeapon(FName WeaponId, bool bEquip, int32 AmmoInMag, int32 ReserveAmmo)
{
	if (!GetOwner()->HasAuthority() || WeaponRegistry == nullptr)
	{
		return nullptr;
	}

	UOneBrahamWeaponDefinition* Definition = WeaponRegistry->FindWeapon(WeaponId);
	if (Definition == nullptr)
	{
		OB_WARN("ServerGiveWeapon: unknown weapon '%s'.", *WeaponId.ToString());
		return nullptr;
	}

	AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	const FTransform SpawnTransform = Character->GetActorTransform();

	TSubclassOf<AOneBrahamWeaponBase> SpawnClass = WeaponRegistry->WeaponActorClass.IsValid()
		? WeaponRegistry->WeaponActorClass.LoadSynchronous()
		: AOneBrahamWeaponBase::StaticClass();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Character;
	SpawnParams.Instigator = Character;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AOneBrahamWeaponBase* NewWeapon = World->SpawnActor<AOneBrahamWeaponBase>(SpawnClass, SpawnTransform, SpawnParams);
	if (NewWeapon == nullptr)
	{
		OB_ERROR("Failed to spawn weapon actor for '%s'.", *WeaponId.ToString());
		return nullptr;
	}

	NewWeapon->SetOwningWeaponComponent(this);
	NewWeapon->InitialiseFromDefinition(Definition);

	if (AmmoInMag >= 0)
	{
		NewWeapon->SetAmmoInMagazine(AmmoInMag);
	}

	// Reserve pool: created on first pick-up of that weapon type.
	if (ReserveAmmo >= 0)
	{
		ReserveAmmoByWeapon.Add(WeaponId, FMath::Clamp(ReserveAmmo, 0, Definition->MaxReserve));
	}
	else if (!ReserveAmmoByWeapon.Contains(WeaponId))
	{
		ReserveAmmoByWeapon.Add(WeaponId, Definition->StartingReserve);
	}

	// Slot management: the new weapon replaces whatever was in its slot.
	const EOneBrahamWeaponSlot Slot = Definition->DefaultSlot;
	if (TObjectPtr<AOneBrahamWeaponBase>* Existing = OwnedWeapons.Find(Slot))
	{
		if (*Existing != nullptr && *Existing != NewWeapon)
		{
			// Dropping the old weapon rather than destroying it keeps the loot loop
			// intact: you can always pick up what you left behind.
			if (AOneBrahamWeaponBase* OldWeapon = *Existing)
			{
				OldWeapon->SetOwningWeaponComponent(nullptr);
				OldWeapon->OnUnequipped();

				if (AOneBrahamPickupBase* Pickup = Cast<AOneBrahamPickupBase>(OldWeapon))
				{
					Pickup->ServerDropAt(Character->GetActorLocation() + Character->GetActorForwardVector() * 60.f);
				}
			}
		}
	}

	OwnedWeapons.Add(Slot, NewWeapon);

	if (bEquip)
	{
		EquipSlot(Slot);
	}

	OB_WEAPON_LOG("'%s' received '%s' (%d in mag, %d reserve).",
		*Character->GetName(), *WeaponId.ToString(), NewWeapon->GetAmmoInMagazine(),
		ReserveAmmoByWeapon.Contains(WeaponId) ? ReserveAmmoByWeapon[WeaponId] : 0);

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return NewWeapon;
}

void UOneBrahamWeaponComponent::ServerRemoveCurrentWeapon()
{
	if (!GetOwner()->HasAuthority() || CurrentWeapon == nullptr)
	{
		return;
	}

	OwnedWeapons.Remove(CurrentSlot);
	CurrentWeapon->SetOwningWeaponComponent(nullptr);
	CurrentWeapon->Destroy();
	CurrentWeapon = nullptr;
	CurrentSlot = EOneBrahamWeaponSlot::Melee;

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::ResetOnRespawn()
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Destroyed, not dropped. The death path has already put the previous kit on
	// the floor as pickups; anything still owned here belongs to a life that is
	// over, and dropping it would arm whoever is standing at the spawn.
	for (const TPair<EOneBrahamWeaponSlot, TObjectPtr<AOneBrahamWeaponBase>>& Pair : OwnedWeapons)
	{
		if (AOneBrahamWeaponBase* Weapon = Pair.Value)
		{
			Weapon->SetOwningWeaponComponent(nullptr);
			Weapon->Destroy();
		}
	}

	OwnedWeapons.Reset();
	ReserveAmmoByWeapon.Reset();
	CurrentWeapon = nullptr;
	CurrentSlot = EOneBrahamWeaponSlot::Melee;

	// The reload's timer belongs to the weapon actor, which has just gone; only
	// the component's own flag is left to clear.
	bIsReloading = false;
	OnRep_Reloading();

	ApplyLoadoutForCurrentLife();

	OnWeaponChanged.Broadcast(CurrentWeapon, nullptr);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::ApplyLoadoutForCurrentLife()
{
	const AOneBrahamCharacter* Character = GetCharacter();
	const AOneBrahamPlayerState* State = Character != nullptr
		? Character->GetPlayerState<AOneBrahamPlayerState>()
		: nullptr;

	// A bot has no player state and a joining client has one that has not heard
	// from them yet. Both get the standard-issue kit: the alternative is a player
	// standing at a spawn point with nothing to shoot with.
	if (State == nullptr || !State->bLoadoutReceived)
	{
		ServerApplyLoadout(DefaultPrimaryWeaponId, DefaultSidearmWeaponId, DefaultUtilityIds, NAME_None);
		return;
	}

	ServerApplyLoadout(State->LoadoutPrimaryWeaponId, State->LoadoutSidearmWeaponId,
		State->LoadoutUtilityIds, State->LoadoutWeaponSkinId);
}

void UOneBrahamWeaponComponent::ServerApplyLoadout(FName PrimaryWeaponId, FName SidearmWeaponId,
	const TArray<FName>& UtilityIds, FName WeaponSkinId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return;
	}

	// ServerGiveWeapon owns the registry lookup, the slot rules and the reserve
	// pool, so it is also the thing that refuses an id that does not exist. It
	// equips the primary: a player spawns holding their rifle, not their pistol.
	AOneBrahamWeaponBase* Primary = PrimaryWeaponId.IsNone()
		? nullptr
		: ServerGiveWeapon(PrimaryWeaponId, /*bEquip*/ true);
	AOneBrahamWeaponBase* Sidearm = SidearmWeaponId.IsNone()
		? nullptr
		: ServerGiveWeapon(SidearmWeaponId, /*bEquip*/ false);

	// One skin on everything this player carries. The table is per weapon type,
	// so a skin that only exists for the carbine leaves the pistol in its issue
	// finish rather than breaking it.
	if (!WeaponSkinId.IsNone())
	{
		const TArray<AOneBrahamWeaponBase*> Armed = { Primary, Sidearm };
		for (AOneBrahamWeaponBase* Weapon : Armed)
		{
			if (Weapon != nullptr)
			{
				Weapon->ApplySkin(WeaponSkinId);
			}
		}
	}

	// Utility lives on the inventory component, which owns the per-type caps and
	// the whitelist of ids it knows. Added one at a time, on top of the kit the
	// respawn already handed out, so the caps do the arithmetic.
	if (UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent())
	{
		for (const FName& UtilityId : UtilityIds)
		{
			Inventory->ServerAddUtility(UtilityId, 1);
		}
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

AOneBrahamWeaponBase* UOneBrahamWeaponComponent::DropCurrentWeapon()
{
	if (!GetOwner()->HasAuthority() || CurrentWeapon == nullptr)
	{
		return nullptr;
	}

	AOneBrahamWeaponBase* Dropped = CurrentWeapon;
	OwnedWeapons.Remove(CurrentSlot);
	Dropped->SetOwningWeaponComponent(nullptr);
	Dropped->OnUnequipped();

	const AOneBrahamCharacter* Character = GetCharacter();
	if (AOneBrahamPickupBase* Pickup = Cast<AOneBrahamPickupBase>(Dropped))
	{
		Pickup->ServerDropAt(Character->GetActorLocation() + Character->GetActorForwardVector() * 70.f);
	}
	else
	{
		// Non-pickup subclass: just detach it and let physics/cleanup handle it.
		Dropped->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Dropped->SetLifeSpan(60.f);
	}

	CurrentWeapon = nullptr;
	CurrentSlot = EOneBrahamWeaponSlot::Melee;

	// Auto-equip the sidearm, which is what a player actually expects.
	EquipSlot(EOneBrahamWeaponSlot::Sidearm);

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return Dropped;
}

AOneBrahamWeaponBase* UOneBrahamWeaponComponent::ServerPickupFromWorld(AOneBrahamWeaponBase* WorldWeapon)
{
	if (!GetOwner()->HasAuthority() || WorldWeapon == nullptr)
	{
		return nullptr;
	}

	AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr || WorldWeapon->Definition == nullptr)
	{
		return nullptr;
	}

	// Anti-cheat: the server re-checks the distance the client claimed. The
	// interaction component's own validation is a courtesy to the player; this is
	// the rule. 60 cm of slack covers the pickup settling while the player walks.
	constexpr float PickupRangeCm = 220.f;
	const float Distance = FVector::Dist(Character->GetActorLocation(), WorldWeapon->GetActorLocation());
	if (Distance > PickupRangeCm + 60.f)
	{
		OB_WARN("ServerPickupFromWorld rejected: '%s' is %.0f cm away (limit %.0f).",
			*WorldWeapon->Definition->WeaponId.ToString(), Distance, PickupRangeCm + 60.f);
		return nullptr;
	}

	// Already held by somebody: never steal a weapon out of another player's hands.
	if (!WorldWeapon->IsGroundPickup())
	{
		return nullptr;
	}

	const EOneBrahamWeaponSlot Slot = WorldWeapon->Definition->DefaultSlot;

	// Displace whatever is in that slot. Dropping it (rather than deleting it)
	// keeps the loot loop honest: swapping a rifle for a better rifle leaves the
	// old one on the floor where the next player can find it.
	if (TObjectPtr<AOneBrahamWeaponBase>* Existing = OwnedWeapons.Find(Slot))
	{
		if (AOneBrahamWeaponBase* Old = *Existing)
		{
			if (Old != WorldWeapon)
			{
				Old->SetOwningWeaponComponent(nullptr);
				Old->OnUnequipped();

				if (AOneBrahamPickupBase* OldPickup = Cast<AOneBrahamPickupBase>(Old))
				{
					OldPickup->ServerDropAt(Character->GetActorLocation() + Character->GetActorForwardVector() * 70.f, true);
				}
				else
				{
					Old->SetLifeSpan(45.f);
				}
			}
		}
	}

	// Reserve ammo travels with the weapon: a dropped rifle is worth exactly what
	// is left in it, which is what makes looting a body a real decision.
	if (const AOneBrahamPickupBase* Pickup = Cast<AOneBrahamPickupBase>(WorldWeapon))
	{
		const int32 Carried = FMath::Clamp(Pickup->GetGroundReserveAmmo(), 0, WorldWeapon->Definition->MaxReserve);
		ReserveAmmoByWeapon.Add(WorldWeapon->Definition->WeaponId, Carried);
	}
	else if (!ReserveAmmoByWeapon.Contains(WorldWeapon->Definition->WeaponId))
	{
		ReserveAmmoByWeapon.Add(WorldWeapon->Definition->WeaponId, WorldWeapon->Definition->StartingReserve);
	}

	WorldWeapon->SetOwningWeaponComponent(this);

	// Take it off the floor before it is attached: the pickup actor owns the
	// ground-state visuals (glow, world mesh, lifetime) and must stand down.
	WorldWeapon->SetIsGroundPickup(false);
	if (AOneBrahamPickupBase* Pickup = Cast<AOneBrahamPickupBase>(WorldWeapon))
	{
		Pickup->ServerTakeOffGround();
	}

	OwnedWeapons.Add(Slot, WorldWeapon);

	// Equip it immediately: a player who pressed the pickup key wants the weapon
	// in their hands, not in a bag they then have to open.
	EquipSlot(Slot);

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}

	UE_LOG(LogOneBrahamWeapons, Log, TEXT("[OB.WPN] %s picked up '%s' (%d + %d rounds)"),
		*Character->GetName(),
		*WorldWeapon->Definition->WeaponId.ToString(),
		WorldWeapon->GetAmmoInMagazine(),
		ReserveAmmoByWeapon.FindRef(WorldWeapon->Definition->WeaponId));

	return WorldWeapon;
}

void UOneBrahamWeaponComponent::DropAllOnDeath()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// Death bag: weapons are dropped as pickups at the body, plus the reserve ammo
	// is transferred to the ground so a squaddie can arm themselves off your corpse.
	for (const TPair<EOneBrahamWeaponSlot, TObjectPtr<AOneBrahamWeaponBase>>& Pair : OwnedWeapons)
	{
		if (Pair.Value == nullptr)
		{
			continue;
		}

		Pair.Value->SetOwningWeaponComponent(nullptr);
		Pair.Value->OnUnequipped();

		if (AOneBrahamPickupBase* Pickup = Cast<AOneBrahamPickupBase>(Pair.Value))
		{
			const FVector Scatter = FVector(FMath::FRandRange(-45.f, 45.f), FMath::FRandRange(-45.f, 45.f), 15.f);
			Pickup->ServerDropAt(GetOwner()->GetActorLocation() + Scatter);
		}
		else
		{
			Pair.Value->SetLifeSpan(30.f);
		}
	}

	OwnedWeapons.Reset();
	CurrentWeapon = nullptr;
	CurrentSlot = EOneBrahamWeaponSlot::Melee;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::EquipSlot(EOneBrahamWeaponSlot Slot)
{
	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerRequestEquipSlot(Slot);
		// Local prediction: switch the anim instantly so the weapon switch does
		// not feel laggy at 100 ms ping.
		if (TObjectPtr<AOneBrahamWeaponBase>* Found = OwnedWeapons.Find(Slot))
		{
			CurrentWeapon = *Found;
			CurrentSlot = Slot;
			OnRep_CurrentWeapon();
		}
		return;
	}

	if (CurrentWeapon != nullptr && CurrentSlot == Slot)
	{
		return;
	}

	AOneBrahamWeaponBase* Previous = CurrentWeapon;

	if (TObjectPtr<AOneBrahamWeaponBase>* Found = OwnedWeapons.Find(Slot))
	{
		CurrentWeapon = *Found;
	}
	else
	{
		CurrentWeapon = nullptr;
	}

	CurrentSlot = Slot;

	if (Previous != nullptr)
	{
		Previous->OnUnequipped();
	}

	// A reload belongs to the weapon being put away. Its timer lives on that
	// actor and would otherwise finish into a holstered weapon, transferring the
	// ammo and playing the second half of the animation off-screen; and the flag
	// is cleared outside the block below, because switching to an *empty* slot
	// ends the reload just as surely as switching to a loaded one.
	if (bIsReloading)
	{
		if (Previous != nullptr)
		{
			Previous->ServerCancelReload();
		}

		bIsReloading = false;
		OnRep_Reloading();
	}

	if (CurrentWeapon != nullptr)
	{
		CurrentWeapon->SetOwningWeaponComponent(this);
		CurrentWeapon->OnEquippedBy(GetCharacter());

		SanitiseFireMode();
	}

	OnWeaponChanged.Broadcast(CurrentWeapon, Previous);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::CycleWeapon(int32 Direction)
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestCycleWeapon(Direction);
		return;
	}

	// Only cycle between the slots that actually hold something: a player with a
	// rifle and a pistol should not cycle into an empty utility slot.
	TArray<EOneBrahamWeaponSlot> Occupied;
	for (const TPair<EOneBrahamWeaponSlot, TObjectPtr<AOneBrahamWeaponBase>>& Pair : OwnedWeapons)
	{
		if (Pair.Value != nullptr)
		{
			Occupied.Add(Pair.Key);
		}
	}

	if (Occupied.Num() < 2)
	{
		return;
	}

	Occupied.Sort([](const EOneBrahamWeaponSlot& A, const EOneBrahamWeaponSlot& B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});

	const int32 CurrentIndex = Occupied.IndexOfByKey(CurrentSlot);
	const int32 NextIndex = (CurrentIndex + Direction + Occupied.Num()) % Occupied.Num();

	EquipSlot(Occupied[NextIndex]);
}

AOneBrahamWeaponBase* UOneBrahamWeaponComponent::GetWeaponInSlot(EOneBrahamWeaponSlot Slot) const
{
	const TObjectPtr<AOneBrahamWeaponBase>* Found = OwnedWeapons.Find(Slot);
	return Found != nullptr ? *Found : nullptr;
}

bool UOneBrahamWeaponComponent::HasWeapon(FName WeaponId) const
{
	for (const TPair<EOneBrahamWeaponSlot, TObjectPtr<AOneBrahamWeaponBase>>& Pair : OwnedWeapons)
	{
		if (Pair.Value != nullptr && Pair.Value->Definition != nullptr && Pair.Value->Definition->WeaponId == WeaponId)
		{
			return true;
		}
	}
	return false;
}

void UOneBrahamWeaponComponent::ServerGiveWeaponDebug_Implementation(FName WeaponId)
{
	ServerGiveWeapon(WeaponId, true);
}

// ==========================================================================
//  Trigger
// ==========================================================================
void UOneBrahamWeaponComponent::SetTriggerHeld(bool bHeld)
{
	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr || CurrentWeapon == nullptr)
	{
		return;
	}

	// Cannot fire while sprinting, mantling or downed: this is a tactical shooter,
	// not a run-and-gun. Reloading also blocks firing.
	if (bHeld && (Character->IsSprintRequested() ||
		(Character->GetOBCharacterMovement() != nullptr &&
		 Character->GetOBCharacterMovement()->GetOBMovementMode() == EOneBrahamMovementMode::DownedCrawl) ||
		bIsReloading))
	{
		return;
	}

	bTriggerHeld = bHeld;

	if (!bHeld)
	{
		ServerBurstRemaining = 0;
		return;
	}

	// --- Semi-auto / pump / bolt: one shot per press ----------------------
	const bool bSingleShot = CurrentFireMode == EOneBrahamFireMode::Single ||
							 CurrentFireMode == EOneBrahamFireMode::Pump ||
							 CurrentFireMode == EOneBrahamFireMode::Bolt;

	if (!bSingleShot)
	{
		return;
	}

	if (CurrentWeapon->GetWeaponState() != EOneBrahamWeaponState::Ready)
	{
		return;
	}

	if (CurrentWeapon->IsMagazineEmpty())
	{
		CurrentWeapon->MulticastPlayDryFire();
		return;
	}

	++ClientShotIndex;
	LastLocalFireTime = GetWorld()->GetTimeSeconds();
	PredictLocalShot();
	ServerRequestFire(Character->GetEyeLocation(), Character->GetEyeRotation().Vector(), ClientShotIndex);
}

// ==========================================================================
//  The server fire path
// ==========================================================================
void UOneBrahamWeaponComponent::ServerRequestFire_Implementation(FVector_NetQuantize AimOrigin, FVector_NetQuantizeNormal AimDirection, int32 InClientShotIndex)
{
	AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr || !GetOwner()->HasAuthority() || CurrentWeapon == nullptr)
	{
		return;
	}

	EndSpawnProtection();

	AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(Character->GetController());
	if (PC == nullptr)
	{
		return;
	}

	// --- Validation 1: weapon state ---------------------------------------
	if (CurrentWeapon->GetWeaponState() != EOneBrahamWeaponState::Ready)
	{
		++RejectedShotCount;
		return;
	}

	if (CurrentWeapon->IsMagazineEmpty())
	{
		++RejectedShotCount;
		return;
	}

	// --- Validation 2: rate of fire (belt and braces; the weapon also checks) ---
	const float Now = GetWorld()->GetTimeSeconds();
	const float MinInterval = CurrentWeapon->Definition != nullptr ? CurrentWeapon->Definition->GetSecondsBetweenShots() : 0.1f;
	const float Tolerance = UOneBrahamGameSettings::Get().MaxFireRateTolerance;

	if (Now - LastServerFireTime < MinInterval - Tolerance)
	{
		++RejectedShotCount;
		PC->ReportViolation(FString::Printf(TEXT("Server rejected shot: %.4fs < %.4fs (weapon %s)"),
			Now - LastServerFireTime, MinInterval,
			CurrentWeapon->Definition ? *CurrentWeapon->Definition->WeaponId.ToString() : TEXT("?")), 1.5f);
		return;
	}

	// --- Validation 3: eye position ---------------------------------------
	// The client's eye must be near where the server thinks the pawn's eye is.
	// A large disagreement means either extreme lag (rare, and we forgive it) or
	// a teleport/hitbox-manipulation cheat (which we do not).
	const FVector ServerEye = Character->GetEyeLocation();
	const FVector ClientEye = AimOrigin;
	const float EyeDrift = FVector::Dist(ServerEye, ClientEye);

	if (EyeDrift > MaxEyePositionDrift * 3.f)
	{
		++RejectedShotCount;
		PC->ReportViolation(FString::Printf(TEXT("Shot origin drift %.0f cm (max %.0f)"),
			EyeDrift, MaxEyePositionDrift * 3.f), 3.f);
		return;
	}

	// --- Validation 4: aim direction within the legal cone -----------------
	const FVector ServerForward = Character->GetEyeRotation().Vector();
	const FVector ClientDir = AimDirection;
	const float AimAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ServerForward, ClientDir), -1.f, 1.f)));

	const float LegalSpread = ComputeCurrentSpreadDegrees() + MaxAimDeviationDegrees;
	if (AimAngle > LegalSpread + 6.f)
	{
		++RejectedShotCount;
		PC->ReportViolation(FString::Printf(TEXT("Aim deviation %.1f deg (legal %.1f)"), AimAngle, LegalSpread), 2.f);
		return;
	}

	// --- Validation 5: not firing from inside geometry ---------------------
	// (Noclip users shoot through walls; a point-in-geometry test catches it.)
	FCollisionQueryParams InsideParams(SCENE_QUERY_STAT(OBSelfIntersect), false, Character);
	if (GetWorld()->OverlapAnyTestByChannel(ClientEye, FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(6.f), InsideParams))
	{
		++RejectedShotCount;
		PC->ReportViolation(TEXT("Shot origin inside world geometry"), 4.f);
		return;
	}

	// --- Rewind for lag compensation --------------------------------------
	const float ShooterPingMs = PC->PlayerState != nullptr ? static_cast<float>(PC->PlayerState->GetPingInMilliseconds()) : 0.f;
	const float RewindTime = RewindPlayersForLagCompensation(ShooterPingMs);

	// --- Fire -------------------------------------------------------------
	LastServerFireTime = Now;
	++ServerShotIndex;

	bool bFired = FireOnce(ClientEye, ClientDir);

	RestorePlayersAfterLagCompensation();

	(void)RewindTime;
	(void)bFired;
}

void UOneBrahamWeaponComponent::GetLocalAim(FVector& OutOrigin, FVector& OutDirection) const
{
	// One definition of "where this player is aiming", used by the fire path, the
	// throw path and the placement path, so a change to the shot origin cannot
	// apply to a bullet and not to a grenade.
	const AOneBrahamCharacter* Character = CachedCharacter.Get();

	if (Character == nullptr)
	{
		OutOrigin = GetOwner() != nullptr ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
		OutDirection = GetOwner() != nullptr ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
		return;
	}

	OutOrigin = Character->GetWeaponTraceOrigin();
	OutDirection = Character->GetWeaponTraceDirection();
}

void UOneBrahamWeaponComponent::OnWeaponReady()
{
	// The weapon has finished being raised. Everything that is true about a settled
	// weapon is reset here rather than in six places that each remember to.
	ResetRecoilAndSpread();
	ServerBurstRemaining = 0;
	LastLocalFireTime = -1000.f;
	LastServerFireTime = -1000.f;

	SpreadStream.Reset();

	// Re-announce the magazine: a weapon drawn mid-swap has a different capacity
	// from the one it replaced, and the HUD counter is driven by this delegate.
	// OnWeaponAmmoChanged resolves the reserve from the per-weapon map, so the
	// counter cannot show the previous gun's reserve for one frame.
	if (CurrentWeapon != nullptr)
	{
		OnWeaponAmmoChanged(CurrentWeapon->GetAmmoInMagazine());
	}

	// Hold-through-the-swap: a player who keeps the trigger down while a weapon is
	// being raised expects it to open fire the instant it is ready. Automatic and
	// burst fire are handled by TickComponent the moment the state becomes Ready;
	// single-shot weapons fire on the press, so the press is re-delivered here.
	if (!bTriggerHeld || CurrentWeapon == nullptr || bIsReloading)
	{
		return;
	}

	const bool bSingleShot = CurrentFireMode == EOneBrahamFireMode::Single ||
							 CurrentFireMode == EOneBrahamFireMode::Pump ||
							 CurrentFireMode == EOneBrahamFireMode::Bolt;

	if (bSingleShot)
	{
		// SetTriggerHeld clears the flag first, so the re-delivery is a genuine
		// press rather than a duplicate of an already-counted one.
		bTriggerHeld = false;
		SetTriggerHeld(true);
	}
}

bool UOneBrahamWeaponComponent::FireOnce(const FVector& AimOrigin, const FVector& AimDirection)
{
	if (CurrentWeapon == nullptr)
	{
		return false;
	}

	AOneBrahamCharacter* Character = GetCharacter();

	// Ammo: AOneBrahamWeaponBase consumes and validates the magazine itself.
	TArray<FOneBrahamImpactInfo> Impacts;
	AActor* FirstHitActor = nullptr;

	const float Damage = CurrentWeapon->ServerFireShot(AimOrigin, AimDirection, Impacts, FirstHitActor);
	(void)Damage;

	// Auto-reload when the magazine runs dry (player preference, default on).
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings != nullptr && Settings->bAutoReloadOnEmpty && CurrentWeapon->IsMagazineEmpty() && !bIsReloading)
	{
		RequestReload();
	}

	// ADS drops on the first shot unless the player is holding the sight: this is
	// what makes burst discipline matter at range.
	if (Settings == nullptr || !Settings->bToggleADS)
	{
		// Nothing to do: hold-ADS players keep ADS while firing.
	}

	return true;
}

void UOneBrahamWeaponComponent::PredictLocalShot()
{
	// Local prediction: view kick, muzzle FX and audio. No damage, ever.
	if (CurrentWeapon == nullptr)
	{
		return;
	}

	ApplyRecoilKick(CurrentWeapon->GetShotCounter());

	CurrentBloom = FMath::Min(CurrentBloom + (CurrentWeapon->Definition != nullptr ? CurrentWeapon->Definition->Spread.BloomPerShot : 0.f),
		CurrentWeapon->Definition != nullptr ? CurrentWeapon->Definition->Spread.MaxBloom : 3.f);

	// Muzzle flash + shell + tracer are all multicast by the server, so the local
	// prediction only handles the *pose* change (kick) - which is what the player
	// actually feels. Firing the FX twice would double the muzzle flash.
	if (CurrentWeapon->Definition != nullptr && CurrentWeapon->Definition->FireMontage.IsValid())
	{
		if (USkeletalMeshComponent* Arms = GetCharacter() ? GetCharacter()->GetFirstPersonMesh() : nullptr)
		{
			if (UAnimInstance* AnimInstance = Arms->GetAnimInstance())
			{
				if (UAnimMontage* FireMontage = CurrentWeapon->Definition->FireMontage.LoadSynchronous())
				{
					AnimInstance->Montage_Play(FireMontage, 1.f / FMath::Max(0.4f, (CurrentWeapon->Definition->RoundsPerMinute / 600.f)));
				}
			}
		}
	}
}

void UOneBrahamWeaponComponent::ApplyRecoilKick(int32 ShotIndex)
{
	if (CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return;
	}

	const FVector2D Step = CurrentWeapon->Definition->GetRecoilStep(ShotIndex);

	const float RecoilMultiplier = CurrentWeapon->GetEffectiveRecoilMultiplier();
	const float VMultiplier = RecoilMultiplier * (bAimingDownSights ? CurrentWeapon->Definition->Recoil.ADSVRecoilMultiplier : 1.f);
	const float HMultiplier = RecoilMultiplier * (bAimingDownSights ? CurrentWeapon->Definition->Recoil.ADSHRecoilMultiplier : 1.f);

	// First shot from a settled weapon kicks less: rewards trigger discipline.
	const float FirstShotMultiplier = (ShotIndex == 0) ? CurrentWeapon->Definition->Recoil.FirstShotMultiplier : 1.f;

	const float Pitch = -Step.X * VMultiplier * FirstShotMultiplier;
	float Yaw = Step.Y * HMultiplier * FirstShotMultiplier;

	Yaw += FMath::FRandRange(-CurrentWeapon->Definition->Recoil.YawJitter, CurrentWeapon->Definition->Recoil.YawJitter);

	LastRecoilStep = FVector2D(Pitch, Yaw);
	AccumulatedRecoil += LastRecoilStep;

	// Apply to the controller immediately on the local machine: this is the part
	// of the recoil the player feels in their wrist.
	if (AOneBrahamPlayerController* PC = GetOneBrahamPlayerController())
	{
		PC->AddPitchInput(Pitch);
		PC->AddYawInput(Yaw);
	}

	// Weapon visual kick: the gun model jumps back and up, the muzzle climbs. It
	// scales with the weapon's own recoil (a marksman rifle shoves harder than
	// an SMG) and shrinks, but does not vanish, at full ADS. The feel component
	// on the character turns this into the arms mesh transform.
	const float KickScale = FMath::Clamp(RecoilMultiplier, 0.25f, 2.f) * FMath::Lerp(1.f, WeaponKickADSScale, ADSAlpha);
	WeaponKickOffset += FVector2D(-WeaponKickBackCm, WeaponKickUpCm) * KickScale;
	WeaponKickOffset.X = FMath::Max(WeaponKickOffset.X, -WeaponKickMaxBackCm);
	WeaponKickOffset.Y = FMath::Min(WeaponKickOffset.Y, WeaponKickMaxBackCm * 0.5f);
	WeaponKickRoll = FMath::Clamp(WeaponKickRoll + FMath::FRandRange(-WeaponKickRollDegrees, WeaponKickRollDegrees) * KickScale,
		-3.f * WeaponKickRollDegrees, 3.f * WeaponKickRollDegrees);
}

float UOneBrahamWeaponComponent::GetSwayMovementScale() const
{
	return CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr
		? FMath::Max(0.f, CurrentWeapon->Definition->SwayMovementScale)
		: 1.f;
}

float UOneBrahamWeaponComponent::GetSwayLoopScale() const
{
	return CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr
		? FMath::Max(0.f, CurrentWeapon->Definition->SwayLoopScale)
		: 1.f;
}

void UOneBrahamWeaponComponent::ConsumeRecoil(float DeltaTime)
{
	if (AccumulatedRecoil.IsNearlyZero(0.001f))
	{
		return;
	}

	const float RecoverySpeed = CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr
		? CurrentWeapon->Definition->Recoil.RecoverySpeed
		: 28.f;

	const float RecoveryFraction = CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr
		? CurrentWeapon->Definition->Recoil.RecoveryFraction
		: 0.92f;

	// The player already felt the full kick; recovery pulls the view back toward
	// the pre-fire aim by the recovery fraction (not all the way: that would make
	// recoil purely cosmetic).
	const FVector2D RecoverStep = -AccumulatedRecoil.GetSafeNormal() * FMath::Min(AccumulatedRecoil.Size(), RecoverySpeed * DeltaTime) * RecoveryFraction;

	if (AOneBrahamPlayerController* PC = GetOneBrahamPlayerController())
	{
		if (PC->IsLocalController())
		{
			PC->AddPitchInput(RecoverStep.X);
			PC->AddYawInput(RecoverStep.Y);
		}
	}

	AccumulatedRecoil -= FVector2D(FMath::Abs(RecoverStep.X), FMath::Abs(RecoverStep.Y)) * FVector2D(FMath::Sign(AccumulatedRecoil.X), FMath::Sign(AccumulatedRecoil.Y));

	if (AccumulatedRecoil.SizeSquared() < 0.0004f)
	{
		AccumulatedRecoil = FVector2D::ZeroVector;
	}
}

void UOneBrahamWeaponComponent::ResetRecoilAndSpread()
{
	AccumulatedRecoil = FVector2D::ZeroVector;
	CurrentBloom = 0.f;
	WeaponKickOffset = FVector2D::ZeroVector;
	WeaponKickRoll = 0.f;
}

float UOneBrahamWeaponComponent::ComputeCurrentSpreadDegrees() const
{
	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr || CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return 2.f;
	}

	const FOneBrahamSpreadProfile& Profile = CurrentWeapon->Definition->Spread;
	const float AttachmentMultiplier = CurrentWeapon->GetEffectiveSpreadMultiplier();

	float BaseSpread = bAimingDownSights ? Profile.ADSSpread : Profile.HipStandingSpread;

	if (bAimingDownSights)
	{
		const float Speed = Character->GetVelocity().Size2D();
		if (Speed > 60.f)
		{
			// Moving while aiming: a modest penalty, scaled by speed rather than a
			// binary switch, so tap-strafing is viable but not free.
			const float MoveAlpha = FMath::Clamp(Speed / 400.f, 0.f, 1.f);
			BaseSpread = FMath::Lerp(Profile.ADSSpread, Profile.ADSMovingSpread, MoveAlpha);
		}
	}
	else
	{
		if (Character->IsCrouchRequested())
		{
			BaseSpread = Profile.HipCrouchingSpread;
		}

		const float Speed = Character->GetVelocity().Size2D();
		if (Speed > 450.f && Character->IsSprintRequested())
		{
			BaseSpread = Profile.HipSprintingSpread;
		}
		else if (Speed > 60.f)
		{
			BaseSpread = FMath::Lerp(Profile.HipStandingSpread, Profile.HipMovingSpread, FMath::Clamp(Speed / 400.f, 0.f, 1.f));
		}
	}

	// Jumping ruins accuracy: this is the single most important spread rule.
	if (Character->GetCharacterMovement()->IsFalling())
	{
		BaseSpread *= 2.6f;
	}

	return (BaseSpread + CurrentBloom) * AttachmentMultiplier;
}

// ==========================================================================
//  Reload
// ==========================================================================
int32 UOneBrahamWeaponComponent::GetReserveAmmoForCurrentWeapon() const
{
	if (CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return 0;
	}

	const int32* Found = ReserveAmmoByWeapon.Find(CurrentWeapon->Definition->WeaponId);
	return Found != nullptr ? *Found : 0;
}

float UOneBrahamWeaponComponent::GetReloadProgress01() const
{
	if (!bIsReloading || CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return 0.f;
	}

	// The weapon actor owns the authoritative reload timer, so the progress is
	// derived from its remaining time rather than duplicated here: one source of
	// truth for the HUD ring, the audio cue and the animation.
	const float Total = CurrentWeapon->GetReloadDuration();
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	return FMath::Clamp(1.f - (CurrentWeapon->GetReloadTimeRemaining() / Total), 0.f, 1.f);
}

void UOneBrahamWeaponComponent::RequestReload()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestReload();
		return;
	}

	if (CurrentWeapon == nullptr || bIsReloading)
	{
		return;
	}

	if (CurrentWeapon->Definition == nullptr)
	{
		return;
	}

	// Nothing to do if the mag is full or there is no reserve.
	const int32 Reserve = ReserveAmmoByWeapon.Contains(CurrentWeapon->Definition->WeaponId)
		? ReserveAmmoByWeapon[CurrentWeapon->Definition->WeaponId] : 0;

	if (CurrentWeapon->GetAmmoInMagazine() >= CurrentWeapon->GetMagazineSize() || Reserve <= 0)
	{
		return;
	}

	bIsReloading = true;
	OnRep_Reloading();
	OnReloadStateChanged.Broadcast(true);

	// The weapon actor drives the timer; we only mirror the state for the HUD.
	CurrentWeapon->ServerStartReload();
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::ServerCompleteReload()
{
	if (!GetOwner()->HasAuthority() || CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		bIsReloading = false;
		return;
	}

	const FName WeaponId = CurrentWeapon->Definition->WeaponId;
	int32& Reserve = ReserveAmmoByWeapon.FindOrAdd(WeaponId, 0);

	const int32 MagazineSize = CurrentWeapon->GetMagazineSize();
	int32 Needed = MagazineSize - CurrentWeapon->GetAmmoInMagazine();

	// Tactical reload keeps the chambered round: the player is rewarded for
	// reloading early, which is a real skill expression in this game.
	if (CurrentWeapon->Definition->bKeepsChamberedRound && CurrentWeapon->GetAmmoInMagazine() > 0)
	{
		Needed = MagazineSize - CurrentWeapon->GetAmmoInMagazine();
	}

	const int32 Transferred = FMath::Min(Needed, Reserve);

	Reserve -= Transferred;
	CurrentWeapon->SetAmmoInMagazine(CurrentWeapon->GetAmmoInMagazine() + Transferred);

	bIsReloading = false;
	OnRep_Reloading();
	OnReloadStateChanged.Broadcast(false);
	OnAmmoChanged.Broadcast(CurrentWeapon->GetAmmoInMagazine(), Reserve);

	// Low-ammo voice line: the character says something tired about it.
	if (Reserve <= CurrentWeapon->Definition->MagazineSize && GetCharacter() != nullptr)
	{
		if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
		{
			Voice->SpeakContextualLine(GetOwner(), TEXT("V_LOW_AMMO"));
		}
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::OnWeaponAmmoChanged(int32 AmmoInMagazine)
{
	const FName WeaponId = CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr
		? CurrentWeapon->Definition->WeaponId : NAME_None;

	const int32 Reserve = ReserveAmmoByWeapon.Contains(WeaponId) ? ReserveAmmoByWeapon[WeaponId] : 0;

	OnAmmoChanged.Broadcast(AmmoInMagazine, Reserve);
}

void UOneBrahamWeaponComponent::NotifyLocalHitConfirmed(bool bHeadshot, bool bKill, float DistanceCm)
{
	// Server-side entry point. If we are the server AND the local machine (a listen
	// server), ClientHitConfirmed runs immediately in-process, which is correct:
	// the host's own hits still produce a hit marker.
	AOneBrahamPlayerController* PC = GetOneBrahamPlayerController();
	if (PC == nullptr)
	{
		return;
	}

	ClientHitConfirmed(bHeadshot, bKill, DistanceCm);
}

void UOneBrahamWeaponComponent::ClientHitConfirmed_Implementation(bool bHeadshot, bool bKill, float DistanceCm)
{
	// Hit feedback is the most important 200 ms in any shooter: the player must
	// know instantly that they connected, whether it was a head, and whether the
	// target is finished. Everything below is that, and only that.
	OnHitConfirmed.Broadcast(bHeadshot, bKill, DistanceCm);

	if (AOneBrahamPlayerController* PC = GetOneBrahamPlayerController())
	{
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->PlayHitFeedback(bHeadshot, /*bArmourAbsorbed*/ false, bKill, DistanceCm);
		}

		if (UOneBrahamHUDComponent* HUD = PC->GetHUDComponent())
		{
			HUD->NotifyHitConfirmed(bHeadshot, bKill);
		}
	}

	(void)DistanceCm;
}

void UOneBrahamWeaponComponent::OnServerShotFired(int32 ShotIndex)
{
	// Bloom accumulation is server-side so that the server's spread cone matches
	// the client's visual cone.
	if (CurrentWeapon != nullptr && CurrentWeapon->Definition != nullptr)
	{
		CurrentBloom = FMath::Min(CurrentBloom + CurrentWeapon->Definition->Spread.BloomPerShot,
			CurrentWeapon->Definition->Spread.MaxBloom);
	}

	(void)ShotIndex;
}

// ==========================================================================
//  Fire modes
// ==========================================================================
void UOneBrahamWeaponComponent::CycleFireMode()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestFireMode();
		return;
	}

	if (CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return;
	}

	const TArray<EOneBrahamFireMode>& Modes = CurrentWeapon->Definition->AvailableFireModes;
	if (Modes.Num() <= 1)
	{
		return;
	}

	const int32 CurrentIndex = Modes.IndexOfByKey(CurrentFireMode);
	const int32 NextIndex = (CurrentIndex + 1) % Modes.Num();

	CurrentFireMode = Modes[NextIndex];
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}

	OB_WEAPON_LOG("Fire mode: %s", *UEnum::GetValueAsString(CurrentFireMode));
}

void UOneBrahamWeaponComponent::SanitiseFireMode()
{
	if (CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return;
	}

	const TArray<EOneBrahamFireMode>& Modes = CurrentWeapon->Definition->AvailableFireModes;
	if (Modes.Num() == 0)
	{
		return;
	}

	if (!Modes.Contains(CurrentFireMode))
	{
		CurrentFireMode = Modes.Contains(CurrentWeapon->Definition->DefaultFireMode)
			? CurrentWeapon->Definition->DefaultFireMode : Modes[0];
	}
}

// ==========================================================================
//  Melee / inspect / attachments
// ==========================================================================
bool UOneBrahamWeaponComponent::CanMelee() const
{
	return !bMeleeOnCooldown && CurrentWeapon != nullptr && !bIsReloading;
}

void UOneBrahamWeaponComponent::MeleeAttack()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestMelee();
		return;
	}

	if (!CanMelee() || CurrentWeapon == nullptr)
	{
		return;
	}

	bMeleeOnCooldown = true;

	const float Damage = CurrentWeapon->ServerMelee();
	(void)Damage;

	if (CurrentWeapon->Definition != nullptr && CurrentWeapon->Definition->MeleeMontage.IsValid())
	{
		if (USkeletalMeshComponent* Arms = GetCharacter() ? GetCharacter()->GetFirstPersonMesh() : nullptr)
		{
			if (UAnimInstance* AnimInstance = Arms->GetAnimInstance())
			{
				if (UAnimMontage* Montage = CurrentWeapon->Definition->MeleeMontage.LoadSynchronous())
				{
					AnimInstance->Montage_Play(Montage);
				}
			}
		}
	}

	GetWorld()->GetTimerManager().SetTimer(SourceMeleeCooldownHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bMeleeOnCooldown = false;
		if (AActor* Owner = GetOwner())
		{
			Owner->ForceNetUpdate();
		}
	}), CurrentWeapon->Definition != nullptr ? CurrentWeapon->Definition->MeleeCooldown : 0.8f, false);

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::PlayInspect()
{
	if (CurrentWeapon == nullptr || CurrentWeapon->Definition == nullptr)
	{
		return;
	}

	if (!CurrentWeapon->Definition->InspectMontage.IsValid())
	{
		return;
	}

	// Purely cosmetic, and the only "emote" in the game: 2.5 seconds of a tired
	// person looking at a rifle. Players will use it to express despair.
	if (USkeletalMeshComponent* Arms = GetCharacter() ? GetCharacter()->GetFirstPersonMesh() : nullptr)
	{
		if (UAnimInstance* AnimInstance = Arms->GetAnimInstance())
		{
			if (UAnimMontage* Montage = CurrentWeapon->Definition->InspectMontage.LoadSynchronous())
			{
				AnimInstance->Montage_Play(Montage);
			}
		}
	}
}

void UOneBrahamWeaponComponent::ToggleFlashlight()
{
	if (CurrentWeapon == nullptr)
	{
		return;
	}

	const bool bNewState = !CurrentWeapon->IsFlashlightEnabled();

	if (!GetOwner()->HasAuthority())
	{
		ServerRequestToggleAttachment(true, bNewState);
		CurrentWeapon->SetFlashlightEnabled(bNewState); // local prediction
		return;
	}

	CurrentWeapon->SetFlashlightEnabled(bNewState);
}

void UOneBrahamWeaponComponent::ToggleLaser()
{
	if (CurrentWeapon == nullptr)
	{
		return;
	}

	const bool bNewState = !CurrentWeapon->IsLaserEnabled();

	if (!GetOwner()->HasAuthority())
	{
		ServerRequestToggleAttachment(false, bNewState);
		CurrentWeapon->SetLaserEnabled(bNewState);
		return;
	}

	CurrentWeapon->SetLaserEnabled(bNewState);
}

bool UOneBrahamWeaponComponent::ServerAttachToCurrentWeapon(FName AttachmentId)
{
	if (!GetOwner()->HasAuthority() || CurrentWeapon == nullptr)
	{
		return false;
	}

	return CurrentWeapon->AttachAttachment(AttachmentId);
}

// ==========================================================================
//  Utility / medkit
// ==========================================================================
void UOneBrahamWeaponComponent::SelectUtilityIndex(int32 Index)
{
	SelectedUtilityIndex = FMath::Clamp(Index, 0, 3);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UOneBrahamWeaponComponent::ThrowUtility()
{
	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerRequestThrowUtility(SelectedUtilityIndex, Character->GetEyeLocation(), Character->GetEyeRotation().Vector());
		return;
	}

	UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (Inventory == nullptr)
	{
		return;
	}

	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	(void)Settings;

	// Rate limit: a player cannot throw four frags in one second.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastUtilityTime < 0.75f)
	{
		return;
	}

	AOneBrahamProjectileBase* Projectile = Inventory->ServerConsumeAndSpawnUtility(this, SelectedUtilityIndex,
		Character->GetEyeLocation() + Character->GetEyeRotation().Vector() * 60.f,
		Character->GetEyeRotation().Vector());

	if (Projectile != nullptr)
	{
		LastUtilityTime = Now;
	}
}

void UOneBrahamWeaponComponent::UseUtilityItem()
{
	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		// Breach charges are placed, not thrown: the client sends the aim point and
		// the server raycasts to confirm there is a valid surface there.
		ServerRequestUseUtility(SelectedUtilityIndex, Character->GetEyeLocation() + Character->GetEyeRotation().Vector() * 200.f);
		return;
	}

	UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (Inventory == nullptr)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastUtilityTime < 1.0f)
	{
		return;
	}

	if (Inventory->ServerPlaceUtility(this, SelectedUtilityIndex))
	{
		LastUtilityTime = Now;
	}
}

void UOneBrahamWeaponComponent::UseMedkit()
{
	const AOneBrahamCharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerRequestMedkit();
		return;
	}

	if (bMedkitInProgress)
	{
		return;
	}

	UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent();
	UOneBrahamHealthComponent* Health = Character->GetHealthComponent();
	if (Inventory == nullptr || Health == nullptr || !Health->IsAlive())
	{
		return;
	}

	if (!Inventory->ServerHasMedkit())
	{
		return;
	}

	// Health is restored at the END of the animation: being shot mid-medkit
	// cancels it and costs the kit. This is the "no free healing" rule.
	bMedkitInProgress = true;

	const UOneBrahamMatchSettingsAsset* Settings = nullptr;
	if (const AOneBrahamGameState* GameState = GetWorld()->GetGameState<AOneBrahamGameState>())
	{
		Settings = GameState->GetMatchSettings();
	}

	const float UseSeconds = Settings != nullptr ? Settings->MedkitUseSeconds : 3.5f;
	const float HealAmount = Settings != nullptr ? Settings->MedkitHealAmount : 60.f;

	GetWorld()->GetTimerManager().SetTimer(MedkitTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, HealAmount, Inventory]()
	{
		bMedkitInProgress = false;

		AOneBrahamCharacter* MedCharacter = GetCharacter();
		if (MedCharacter == nullptr || Inventory == nullptr)
		{
			return;
		}

		UOneBrahamHealthComponent* MedHealth = MedCharacter->GetHealthComponent();
		if (MedHealth == nullptr || !MedHealth->IsAlive())
		{
			return;
		}

		if (Inventory->ServerConsumeMedkit())
		{
			MedHealth->Heal(HealAmount);

			if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
			{
				Voice->SpeakContextualLine(GetOwner(), TEXT("V_MEDKIT"));
			}
		}
	}), UseSeconds, false);
}

// ==========================================================================
//  Lag compensation
// ==========================================================================
void UOneBrahamWeaponComponent::CaptureHitboxSnapshot()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	FOneBrahamHitboxSnapshot Snapshot;
	Snapshot.ServerTime = World->GetTimeSeconds();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		if (PC == nullptr)
		{
			continue;
		}

		const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(PC->GetPawn());
		const AOneBrahamPlayerState* State = PC->GetPlayerState<AOneBrahamPlayerState>();
		if (Character == nullptr || State == nullptr)
		{
			continue;
		}

		const int32 PlayerId = State->GetPlayerId();
		const USkeletalMeshComponent* Body = Character->GetMesh();

		if (Body != nullptr)
		{
			// Three anchors are enough for capsule-level compensation; per-bone
			// transforms are stored too but only used for limb-accurate rewinds.
			Snapshot.HeadTransforms.Add(PlayerId, Body->GetSocketTransform(TEXT("head"), RTS_World));
			Snapshot.ChestTransforms.Add(PlayerId, Body->GetSocketTransform(TEXT("spine_03"), RTS_World));
			Snapshot.PelvisTransforms.Add(PlayerId, Body->GetSocketTransform(TEXT("pelvis"), RTS_World));

			TMap<FName, FTransform>& Bones = Snapshot.BoneTransforms.Add(PlayerId);
			Bones.Add(TEXT("head"), Snapshot.HeadTransforms[PlayerId]);
			Bones.Add(TEXT("spine_03"), Snapshot.ChestTransforms[PlayerId]);
			Bones.Add(TEXT("pelvis"), Snapshot.PelvisTransforms[PlayerId]);
		}
	}

	HitboxHistory.Add(MoveTemp(Snapshot));

	// Trim to the sliding window.
	const float Oldest = World->GetTimeSeconds() - LagCompensationHistorySeconds;
	while (HitboxHistory.Num() > 0 && HitboxHistory[0].ServerTime < Oldest)
	{
		HitboxHistory.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

float UOneBrahamWeaponComponent::RewindPlayersForLagCompensation(float ShooterPingMs)
{
	// The rewind target: half the shooter's round trip, capped.
	const float ClampedPingMs = FMath::Min(ShooterPingMs, MaxLagCompensationMs);
	const float RewindSeconds = (ClampedPingMs * 0.5f) / 1000.f;

	if (HitboxHistory.Num() == 0 || RewindSeconds <= 0.f)
	{
		return 0.f;
	}

	const float TargetTime = GetWorld()->GetTimeSeconds() - RewindSeconds;

	// Find the closest snapshot at or before the target time.
	const FOneBrahamHitboxSnapshot* Best = &HitboxHistory[0];
	for (const FOneBrahamHitboxSnapshot& Snapshot : HitboxHistory)
	{
		if (Snapshot.ServerTime <= TargetTime)
		{
			Best = &Snapshot;
		}
		else
		{
			break;
		}
	}

	// The rewind itself is applied by moving the *hit-testing* bodies: rather than
	// teleporting the real (visible) characters - which would look catastrophic -
	// we move the per-bone hitbox volumes used by the Weapon channel and restore
	// them after the trace. The visible mesh is never touched.
	//
	// (See Docs/07_Networking.md, "Hitbox rewind": the hitboxes are separate
	// USkeletalMeshComponents on the OneBrahamHitbox collision profile, so this
	// swap is invisible to players and costs one component transform per bone.)
	ActiveRewindSnapshot = Best != nullptr ? *Best : FOneBrahamHitboxSnapshot();

	return Best != nullptr ? Best->ServerTime : 0.f;
}

void UOneBrahamWeaponComponent::RestorePlayersAfterLagCompensation()
{
	ActiveRewindSnapshot = FOneBrahamHitboxSnapshot();
}

// ==========================================================================
//  Registry accessors
// ==========================================================================
UOneBrahamWeaponDefinition* UOneBrahamWeaponComponent::GetWeaponDefinition(FName WeaponId) const
{
	return WeaponRegistry != nullptr ? WeaponRegistry->FindWeapon(WeaponId) : nullptr;
}

const FOneBrahamAttachmentDefinition* UOneBrahamWeaponComponent::GetAttachmentDefinition(FName AttachmentId) const
{
	return WeaponRegistry != nullptr ? WeaponRegistry->FindAttachment(AttachmentId) : nullptr;
}

const FOneBrahamWeaponSkin* UOneBrahamWeaponComponent::GetSkinDefinition(FName SkinId) const
{
	return WeaponRegistry != nullptr ? WeaponRegistry->FindSkin(SkinId) : nullptr;
}

EOneBrahamAttachmentSlot UOneBrahamWeaponComponent::GetSlotForAttachment(FName AttachmentId) const
{
	const FOneBrahamAttachmentDefinition* Attachment = GetAttachmentDefinition(AttachmentId);
	return Attachment != nullptr ? Attachment->Slot : EOneBrahamAttachmentSlot::Optic;
}

bool UOneBrahamWeaponComponent::IsSuppressorAttachment(FName AttachmentId) const
{
	if (const FOneBrahamAttachmentDefinition* Attachment = GetAttachmentDefinition(AttachmentId))
	{
		return Attachment->Slot == EOneBrahamAttachmentSlot::Muzzle && Attachment->bHidesTracer;
	}
	return false;
}

// ==========================================================================
//  RPC implementations (thin wrappers around the authoritative functions)
// ==========================================================================
void UOneBrahamWeaponComponent::ServerRequestReload_Implementation()
{
	RequestReload();
}

void UOneBrahamWeaponComponent::ServerRequestEquipSlot_Implementation(EOneBrahamWeaponSlot Slot)
{
	EquipSlot(Slot);
}

void UOneBrahamWeaponComponent::ServerRequestCycleWeapon_Implementation(int32 Direction)
{
	CycleWeapon(Direction);
}

void UOneBrahamWeaponComponent::ServerRequestFireMode_Implementation()
{
	CycleFireMode();
}

void UOneBrahamWeaponComponent::EndSpawnProtection()
{
	const AOneBrahamCharacter* Character = GetCharacter();
	UOneBrahamHealthComponent* Health = Character ? Character->GetHealthComponent() : nullptr;
	if (Health != nullptr && Health->IsInvulnerable())
	{
		Health->ClearInvulnerability();
	}
}

void UOneBrahamWeaponComponent::ServerRequestMelee_Implementation()
{
	EndSpawnProtection();
	MeleeAttack();
}

void UOneBrahamWeaponComponent::ServerRequestThrowUtility_Implementation(int32 UtilityIndex, FVector_NetQuantize AimOrigin, FVector_NetQuantizeNormal AimDirection)
{
	EndSpawnProtection();
	SelectUtilityIndex(UtilityIndex);
	ThrowUtility();
}

void UOneBrahamWeaponComponent::ServerRequestUseUtility_Implementation(int32 UtilityIndex, FVector_NetQuantize TargetLocation)
{
	EndSpawnProtection();
	SelectUtilityIndex(UtilityIndex);
	UseUtilityItem();
}

void UOneBrahamWeaponComponent::ServerRequestMedkit_Implementation()
{
	UseMedkit();
}

void UOneBrahamWeaponComponent::ServerRequestToggleAttachment_Implementation(bool bFlashlight, bool bEnabled)
{
	if (CurrentWeapon == nullptr)
	{
		return;
	}

	if (bFlashlight)
	{
		CurrentWeapon->SetFlashlightEnabled(bEnabled);
	}
	else
	{
		CurrentWeapon->SetLaserEnabled(bEnabled);
	}
}

// ==========================================================================
//  Replication
// ==========================================================================
void UOneBrahamWeaponComponent::OnRep_CurrentWeapon()
{
	AOneBrahamWeaponBase* Previous = nullptr;
	(void)Previous;

	if (CurrentWeapon != nullptr)
	{
		CurrentWeapon->SetOwningWeaponComponent(this);
		SanitiseFireMode();
	}

	OnWeaponChanged.Broadcast(CurrentWeapon, nullptr);
}

void UOneBrahamWeaponComponent::OnRep_Reloading()
{
	OnReloadStateChanged.Broadcast(bIsReloading);
}

void UOneBrahamWeaponComponent::SetAimingDownSights(bool bAiming)
{
	// ADS is a pure presentation + spread change; the spread change is applied
	// server-side from the replicated flag, so a modified client cannot get the
	// ADS accuracy benefit without the movement penalty.
	bAimingDownSights = bAiming;

	// Sprinting overrides ADS: you drop the sight to run.
	if (bAiming && GetCharacter() != nullptr && GetCharacter()->IsSprintRequested())
	{
		GetCharacter()->SetSprintRequested(false);
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}
