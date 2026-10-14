// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDestructibleProp.h
// ---------------------------------------------------------------------------
// Office furniture that does not survive a firefight.
//
// A monitor, a printer, a water cooler, a server rack's front panel: cover
// that lasts until somebody puts enough rounds into it. When it goes it stops
// being cover (its collision goes with it), it makes the noise that tells the
// floor where the fight is, and some of it leaves something behind:
//
//   * Electrical (monitors, printers, racks): a shower of sparks and a short.
//     Anyone standing against it when it goes takes a small burn.
//   * Water (coolers): a spray and a wet patch. Loud, harmless.
//   * Extinguisher: a discharge that hangs like a small smoke grenade.
//
// The server decides it broke; every machine plays the break from the
// replicated flag, so a late joiner sees the room as it is, wrecked.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneBrahamDestructibleProp.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EOneBrahamPropHazard : uint8
{
	None = 0,
	Electrical = 1,
	Water = 2,
	Extinguisher = 3
};

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamDestructibleProp : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamDestructibleProp();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	/** Stable id, written into the build manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Prop")
	FName PropId = NAME_None;

	/** Damage it takes to break. A monitor is a magazine of pistol, a rack panel more. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Prop")
	float MaxHealth = 80.f;

	/** What it leaves when it goes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Prop")
	EOneBrahamPropHazard Hazard = EOneBrahamPropHazard::None;

	/** Size in cm, for the blockout box the builder places before the art pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Prop")
	FVector SizeCm = FVector(55.f, 18.f, 34.f);

	/** Burn from an electrical short, to anyone within ShortRadiusCm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Prop")
	float ShortDamage = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Prop")
	float ShortRadiusCm = 140.f;

	UFUNCTION(BlueprintPure, Category = "One Braham|Prop")
	bool IsDestroyed() const { return bDestroyed; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Prop")
	float GetHealth01() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

protected:
	UFUNCTION()
	void OnRep_Destroyed();

	/** Every machine: the wreck, the sound and the effect. */
	void PlayBreakLocally();

	/** Server: what the hazard does to people. */
	void ServerApplyHazard(AController* EventInstigator);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Destroyed)
	bool bDestroyed = false;

	float Health = 0.f;
};
