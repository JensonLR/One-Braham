// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFrontEndGameMode.h
// ---------------------------------------------------------------------------
// The front end: L_MainMenu's game mode, its player controller and the camera
// that looks at the building while the player decides what to do in it.
//
//   AOneBrahamFrontEndGameMode        no pawn, ever. Nobody walks round the menu
//   AOneBrahamFrontEndPlayerController  the menu widget, the cursor, UI-only
//                                     input, and the view through the camera
//   AOneBrahamFrontEndCamera          a CameraActor that drifts: a few degrees
//                                     of slow orbit round the tower and a
//                                     metre of rise, over two minutes, always
//                                     looking at the same point
//
// The level (build_frontend.py) carries the camera and sets this game mode
// as its WorldSettings override; nothing here needs the level to be right to
// work. With no camera placed, the controller looks at the tower from where
// the level's camera would stand.
//
// CONSOLE COMMANDS (registered in the .cpp; -ExecCmds drives a test run)
//   ob.menu.play <ascent|firedrill>            a solo match with bots
//   ob.menu.host [ascent|firedrill] [lan|online]  create a session, travel ?listen
//   ob.menu.join [row]                         search, and join that row when found
//   ob.menu.page <home|play|host|join|settings|credits>
//   ob.menu.leave                              back to the front end from a match
//   ob.menu.after <seconds> <command ...>      run a command later (non-Shipping)
//   ob.menu.shot <name> [quit]                 screenshot, optionally quit (non-Shipping)
//   ob.menu.camera X Y Z [LookX LookY LookZ] [FOV]  move the menu camera (non-Shipping)

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "OneBrahamFrontEndGameMode.generated.h"

class UOneBrahamMainMenuWidget;

/** The menu camera: a slow orbit round a fixed point of interest. */
UCLASS()
class ONEBRAHAM_API AOneBrahamFrontEndCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AOneBrahamFrontEndCamera(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The point the camera keeps in view: the upper half of the tower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End")
	FVector LookAt = FVector(0.f, 0.f, 3600.f);

	/** Degrees either side of the placed position the orbit swings through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End", meta = (ClampMin = "0", ClampMax = "45"))
	float OrbitDegrees = 4.f;

	/** Centimetres the camera rises and falls over a cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End", meta = (ClampMin = "0"))
	float RiseCm = 120.f;

	/** Seconds for one full swing. Slow enough that nobody notices it moving,
	 *  only that the picture is not a photograph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End", meta = (ClampMin = "10"))
	float PeriodSeconds = 150.f;

	/**
	 * Degrees the view is turned away from the point of interest, so the
	 * building sits right of centre and the menu column on the left covers
	 * sky and street, not the tower.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End")
	float FramingYawDegrees = 12.f;

	/** Moves the orbit's centre (and optionally the point of interest) at runtime. */
	void Reposition(const FVector& NewLocation, const TOptional<FVector>& NewLookAt, float NewFOV);

	/** The pose the drift has at Time seconds: location and rotation. Pure. */
	static void DriftPose(const FVector& BaseLocation, const FVector& InLookAt, float InOrbitDegrees, float InRiseCm,
		float InPeriodSeconds, float InFramingYawDegrees, float Time, FVector& OutLocation, FRotator& OutRotation);

private:
	FVector BaseLocation = FVector::ZeroVector;
	float Elapsed = 0.f;
};

/** The menu's controller. Local only: the front end is never networked. */
UCLASS()
class ONEBRAHAM_API AOneBrahamFrontEndPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AOneBrahamFrontEndPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UOneBrahamMainMenuWidget* GetMenu() const { return Menu; }

	/** Puts the menu up (again) and gives it the keyboard, the pad and the cursor. */
	void ShowMenu();

	/** Work queued by a console command that arrived before the menu existed. */
	static void QueueForMenu(TFunction<void(UOneBrahamMainMenuWidget&)> Work);

private:
	void UseMenuCamera();

	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamMainMenuWidget> Menu = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AOneBrahamFrontEndCamera> FallbackCamera = nullptr;
};

/** L_MainMenu's game mode: a controller, a camera and a menu. No pawn. */
UCLASS()
class ONEBRAHAM_API AOneBrahamFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOneBrahamFrontEndGameMode();

	/** Nobody is spawned in the front end. */
	virtual void RestartPlayer(AController* NewPlayer) override {}

	/** The front end is one person's: nobody logs in to it from outside. */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	/** Where the camera stands when the level carries none: 150 m west-south-
	 *  west of the tower at a neighbour's roof height, looking at the crown
	 *  over the roofs. Kept in step with build_frontend.py's CAMERA. */
	static FVector DefaultCameraLocation() { return FVector(-14772.f, -2605.f, 2500.f); }
	static FVector DefaultCameraLookAt() { return FVector(0.f, -600.f, 3300.f); }
};
