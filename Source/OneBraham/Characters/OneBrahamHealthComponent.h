// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHealthComponent.h
// ---------------------------------------------------------------------------
// Server-authoritative health, damage application and the one place in the
// codebase where a player's HP actually changes.
//
// Damage pipeline (all on the server):
//   1. AOneBrahamWeaponBase / grenade / environmental source builds an
//      FOneBrahamDamageReport.
//   2. UOneBrahamHealthComponent::ApplyDamage runs it through:
//        armour -> hit zone multiplier -> penetration multiplier ->
//        friendly-fire policy -> invulnerability windows -> match damage scale
//   3. Health changes, delegates fire, and the OnRep pushes it to clients.
//
// The client never sends damage. Ever. It only reports where it aimed; the
// server does the trace (see UOneBrahamWeaponComponent::ServerFire).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamHealthUpdated, float, NewHealth, float, MaxHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamDeathEvent, AActor*, Killer, const FOneBrahamDamageReport&, Report);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamInvulnerabilityChanged, bool, bInvulnerable);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamArmourChanged, float, Armour, float, MaxArmour);
/** Fired on the machine that owns this component whenever damage actually lands. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamDamageTaken, float, Damage, AActor*, Instigator, FVector, InstigatorLocation);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamHealthComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Config (copied from the match settings on spawn)
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float HealthRegenPerSecond = 4.f;

	/** Seconds after the last damage before regeneration starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float HealthRegenDelay = 6.f;

	/**
	 * Regeneration ceiling: you heal back to 70, not to 100. This is what makes
	 * a medkit worth carrying and makes every fight cost something durable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float HealthRegenCeiling = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float MaxArmour = 50.f;

	/**
	 * Armour soaks this fraction of incoming damage while it lasts.
	 *
	 * 60%, and the documentation's worked example depends on it: a 30 damage shot
	 * against full armour takes 12 health and 18 armour (Docs/04). The code said
	 * 0.55 for a while, which made that example wrong by a point of damage and
	 * changed nothing anybody would have noticed by playing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float ArmourAbsorption = 0.60f;

	/** Damage multiplier per hit zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float HeadshotMultiplier = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float NeckMultiplier = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float TorsoMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Health")
	float LimbMultiplier = 0.82f;

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	float GetArmour() const { return Armour; }

	/** Where the last hit came from, in world space. Drives the damage indicator. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	FVector GetLastDamageInstigatorLocation() const;

	/** World time of the last damage taken; 0 if never. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	float GetLastDamageTime() const { return LastDamageTime; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	bool IsAlive() const { return Health > 0.f && !bDead; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	bool IsInvulnerable() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Health")
	bool IsCritical() const { return Health > 0.f && Health <= MaxHealth * 0.25f; }

	// ======================================================================
	//  Mutation (server only unless noted)
	// ======================================================================
	/** Sets both current and max health; used on spawn / mode change. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	void InitializeHealth(float NewMaxHealth, bool bRefill = true);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	void ResetHealth();

	/**
	 * Applies damage. Returns the damage actually dealt (0 if rejected).
	 * Server only: calling this on a client does nothing but log a warning.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	float ApplyDamage(float RawDamage, AActor* DamageInstigator, const FOneBrahamDamageReport& IncomingReport, class AController* InstigatorController);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	float Heal(float Amount);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	float AddArmour(float Amount);

	/** Makes the owner untargetable for N seconds (spawn grace, extraction boards). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	void GrantInvulnerability(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	void ClearInvulnerability();

	/** Kills the owner outright (fall damage, purge, out-of-bounds). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Health")
	void Kill(AActor* Killer, FName CauseId = NAME_None);

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Health") FOneBrahamHealthUpdated OnHealthChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Health") FOneBrahamDeathEvent OnDeath;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Health") FOneBrahamInvulnerabilityChanged OnInvulnerabilityChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Health") FOneBrahamArmourChanged OnArmourChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Health") FOneBrahamDamageTaken OnDamageTaken;

	// --- Networked state ---------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "One Braham|Health")
	float Health = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Armour, BlueprintReadOnly, Category = "One Braham|Health")
	float Armour = 50.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Health")
	float LastDamageTime = -1000.f;

	/** Server/composite damage multiplier from the match settings + ob.DamageScale. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Health")
	float GlobalDamageScale = 1.f;

	/** Who last damaged us, for the kill credit + "you were killed by" screen. */
	TWeakObjectPtr<AActor> LastDamageInstigator;

	/** Full report of the killing blow, replayed on the death screen. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Health")
	FOneBrahamDamageReport LastDamageReport;

protected:
	UFUNCTION() void OnRep_Health();
	UFUNCTION() void OnRep_Armour();

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Health")
	bool bDead = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Health")
	float InvulnerableUntil = -1.f;

	/** Accumulated damage from each attacker in the last 5 s, for assists. */
	TMap<TWeakObjectPtr<AActor>, float> RecentAttackers;

	/** Time of the last attacker-map prune. */
	float LastAttackerPruneTime = 0.f;

public:
	/**
	 * Returns everyone who contributed at least 20% of the target's max health in
	 * the last 5 seconds. Used for assists in the killfeed and scoreboard.
	 */
	void GetRecentAttackers(TArray<AActor*>& OutAttackers) const;

	/** Maps a bone name to a hit zone multiplier. Public so the damage tests can
	 *  check the table without going through a full hit. */
	float GetZoneMultiplier(EOneBrahamHitZone Zone) const;
};
