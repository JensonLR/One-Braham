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
//                                     metre of rise, always looking at the
//                                     same point - and, since the redesign,
//                                     a director's shot list: authored shots
//                                     it cuts, dips or blends between, with
//                                     its own film treatment (grain, vignette,
//                                     5600 K, saturation 0.92)
//
// THE SHOTS (DefaultShots(), authored here so no content is needed)
//   crown      aerial from the north-east over the fins, the default
//   fins       the plaza at the south-east corner, looking up the fins
//   reception  the entrance front, the reception OLED wall reading ON BREAK
//   plaza_up   the plaza, straight up the tower to the lit crown (PLAY)
//   plaza_low  low across Braham Street to the entrance (HOST, JOIN)
//   skydeck    from the Skydeck out over the City (VISITORS' BOOK)
//   doors_out  the revolving doors, facing the street (SIGN OUT)
// A shot marked bInterior falls back to its FallbackId when the reception is
// not in the level (no AOneBrahamDisplay tagged "ReceptionWall").
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
//   ob.menu.cut <shot> [cut|dip|blend]         play one of the director's shots (non-Shipping)

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "OneBrahamFrontEndGameMode.generated.h"

class UOneBrahamMainMenuWidget;
class AOneBrahamDisplay;

/** How the camera gets from one shot to the next. */
UENUM(BlueprintType)
enum class EOneBrahamShotTransition : uint8
{
	/** Straight there, this frame. */
	Cut,
	/** Through black: out 120 ms, hold 40 ms, in 220 ms; the cut happens in the black. */
	Dip,
	/** A 1.4 s ease-in-out move of position, aim and lens. */
	Blend
};

/** One authored shot of the front end's camera. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamMenuShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") FVector LookAt = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float FOV = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float OrbitDeg = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float RiseCm = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float DriftPeriodS = 36.f;
	/** Degrees the aim is turned off the point of interest (positive: subject to the right). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float FramingYaw = 0.f;
	/** Depth of field (interior shots only): focus distance, cm; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float FocusDistanceCm = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float Aperture = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") bool bInterior = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") FName FallbackId;
	/** Locked off: no drift at all (the credits' Skydeck). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") bool bLocked = false;
	/** Stops under (negative) the level's dusk exposure: the street goes dark, the screens keep their light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End") float ExposureBias = 0.f;
};

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

	/** The director's shots. Empty in the level: filled from the game mode's defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Front End")
	TArray<FOneBrahamMenuShot> Shots;

	/**
	 * Plays a shot. Returns false for an unknown id. A Dip asks DipHandler for
	 * the black (the menu's cinema overlay) and cuts inside it; with nobody to
	 * dip, it cuts. An interior shot without the reception plays its fallback.
	 */
	bool PlayShot(FName Id, EOneBrahamShotTransition Transition);

	/** The shot on screen (after fallback), or NAME_None before the first. */
	FName GetCurrentShot() const { return CurrentShot; }

	/** The shot a request for Id resolves to here (its fallback when interior and no reception). */
	FName ResolveShot(FName Id) const;

	/** The lift: pushes forward Cm along the view and to ToFOV over Seconds, ease-in-out. */
	void PushIn(float Cm, float ToFOV, float Seconds);

	/** Holds still: no drift (Reduce Motion, quality Low, the flat front end). */
	void SetFlat(bool bInFlat) { bFlat = bInFlat; }

	/** Who draws a dip's black: (out, hold, in) seconds. Set by the controller. */
	TFunction<void(float, float, float)> DipHandler;

	/** Seconds of a Dip's out-fade and black hold: the cut lands at their sum minus a frame. */
	static constexpr float DipOutSeconds = 0.12f;
	static constexpr float DipHoldSeconds = 0.04f;
	static constexpr float DipInSeconds = 0.22f;
	static constexpr float BlendSeconds = 1.4f;

	/** The pose the drift has at Time seconds: location and rotation. Pure. */
	static void DriftPose(const FVector& BaseLocation, const FVector& InLookAt, float InOrbitDegrees, float InRiseCm,
		float InPeriodSeconds, float InFramingYawDegrees, float Time, FVector& OutLocation, FRotator& OutRotation);

private:
	const FOneBrahamMenuShot* FindShot(FName Id) const;
	void ApplyShot(const FOneBrahamMenuShot& Shot);
	void ApplyFilm(const FOneBrahamMenuShot* Shot);

	FVector BaseLocation = FVector::ZeroVector;
	float Elapsed = 0.f;

	FName CurrentShot;
	bool bFlat = false;

	/** A dip's pending cut: the shot and the world real time to cut at. */
	FName PendingShot;
	double PendingAt = 0.0;

	/** A blend in progress: from this pose to the shot's, over BlendSeconds. */
	bool bBlending = false;
	double BlendAt = 0.0;
	FVector BlendFromLocation = FVector::ZeroVector;
	FRotator BlendFromRotation = FRotator::ZeroRotator;
	float BlendFromFOV = 35.f;
	float ShotFOV = 35.f;

	/** The lift push. */
	double PushAt = 0.0;
	float PushSeconds = 0.f;
	float PushCm = 0.f;
	float PushFromFOV = 35.f;
	float PushToFOV = 35.f;
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

	/** The camera the menu looks through (placed or spawned). */
	AOneBrahamFrontEndCamera* GetMenuCamera() const { return MenuCamera; }

	/** Puts the menu up (again) and gives it the keyboard, the pad and the cursor. */
	void ShowMenu();

	/** Work queued by a console command that arrived before the menu existed. */
	static void QueueForMenu(TFunction<void(UOneBrahamMainMenuWidget&)> Work);

private:
	void UseMenuCamera();

	/** The reception's OLED wall, reading ON BREAK: the level's, or one placed now. */
	void PrepareReceptionWall();

	UPROPERTY(Transient)
	TObjectPtr<AOneBrahamFrontEndCamera> MenuCamera = nullptr;

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

	/** Marks that this session has left the front end for a match: the next
	 *  visit is a return (no cold open; the wall says WELCOME BACK). */
	static void NoteLeavingForMatch();

	/** True when the front end has been left for a match this session. */
	static bool IsReturningFromMatch();

	/** The clock (HH:MM) at which the player came back, for the wall's line. */
	static FString SignedOutClock();

	/** True once the cold open has played this session. */
	static bool& ColdOpenPlayed();

	/** The director's shot list, authored for L_MainMenu's exterior. */
	static const TArray<FOneBrahamMenuShot>& DefaultShots();

	/**
	 * The slide the reception wall shows through the front end: ON BREAK, and
	 * either the sign-in line (bReturning false) or "You signed out at HH:MM".
	 */
	static void ReceptionSlide(bool bReturning, const FString& SignedOutClock, FText& OutEyebrow, FText& OutHeadline,
		FText& OutSubline);
};
