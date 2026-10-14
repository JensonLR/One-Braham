// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAnimInstance.h
// ---------------------------------------------------------------------------
// The C++ half of ABP_OneBrahamCharacter: every value the animation graph
// reads, computed once a frame from the character, its movement and its weapon.
//
// The graph is authored in the editor and only reads these; the logic that
// decides what the body is doing stays in code, where it can be reviewed and
// tested, and where a renamed Blueprint variable cannot silently stop a downed
// player from looking downed. Until this class existed the ABP was created
// with no parent and its "parameters" were a log line: nothing drove the body.
//
// PERSONALITY
//   Six archetypes share one skeleton and one set of clips. What makes the
//   Facilities Manager walk differently from the Business Analyst is the
//   FOneBrahamAnimationPersonality on their archetype definition, copied here
//   so the graph can scale play rates and blend weights by it.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Core/OneBrahamTypes.h"
#include "Data/OneBrahamCharacterDataAsset.h"
#include "OneBrahamAnimInstance.generated.h"

class AOneBrahamCharacter;

/** The weapon pose family the upper body holds. */
UENUM(BlueprintType)
enum class EOneBrahamWeaponPose : uint8
{
	Unarmed = 0,
	Pistol = 1,
	Rifle = 2,
	Shotgun = 3
};

UCLASS()
class ONEBRAHAM_API UOneBrahamAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** The pose family for a weapon category. Pure, for the tests. */
	static EOneBrahamWeaponPose PoseForCategory(EOneBrahamWeaponCategory Category);

	/** Signed angle in degrees between a velocity and a facing, -180 to 180. */
	static float DirectionOf(const FVector& Velocity, const FRotator& Facing);

	// --- Locomotion -------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float Speed = 0.f;

	/** Movement direction relative to where the body faces, degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float Direction = 0.f;

	/** Speed-matched play rate for the locomotion cycle, personality applied. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float GaitPlayRate = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsCrouched = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsSliding = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsMantling = false;

	// --- Life -------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsDowned = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsDead = false;

	// --- Aim and weapon ---------------------------------------------------
	/** Where the player is looking relative to the body: the aim offset's inputs. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float AimPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float AimYaw = 0.f;

	/** 0 hip, 1 fully down the sights. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float ADSAlpha = 0.f;

	/** -1 left to 1 right. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float Lean = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	bool bIsReloading = false;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	EOneBrahamWeaponPose WeaponPose = EOneBrahamWeaponPose::Unarmed;

	// --- Personality ------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	FOneBrahamAnimationPersonality Personality;

	/** Arm swing for the current gait: the personality's scale, damped while aiming. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float ArmSwing = 1.f;

	/** The spine hunch while sprinting, from the personality. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Anim")
	float SprintHunchAlpha = 0.f;

protected:
	/** Reads the archetype's personality when the character's archetype changes. */
	void RefreshPersonality();

	TWeakObjectPtr<AOneBrahamCharacter> Character;

	FGameplayTag PersonalityFor;

	/** The speed the locomotion clips were authored at, cm/s, walk and run. */
	static constexpr float AuthoredWalkSpeed = 180.f;
	static constexpr float AuthoredRunSpeed = 520.f;
};
