// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamFrontEndGameMode.h"

#include "OneBraham.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamSessionSubsystem.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UI/OneBrahamMainMenuWidget.h"
#include "UnrealClient.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "World/OneBrahamDisplay.h"
#include "UI/FrontEnd/SOneBrahamLiftLoading.h"

// ==========================================================================
//  Camera
// ==========================================================================
AOneBrahamFrontEndCamera::AOneBrahamFrontEndCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	if (UCameraComponent* Camera = GetCameraComponent())
	{
		// A long lens from across the street: the tower without the wide-angle
		// lean that makes a building look like a model of one.
		Camera->SetFieldOfView(55.f);
		Camera->bConstrainAspectRatio = false;
	}
}

void AOneBrahamFrontEndCamera::BeginPlay()
{
	Super::BeginPlay();
	BaseLocation = GetActorLocation();
	Elapsed = 0.f;
	if (Shots.IsEmpty())
	{
		Shots = AOneBrahamFrontEndGameMode::DefaultShots();
	}
	if (GetCameraComponent() != nullptr)
	{
		ShotFOV = GetCameraComponent()->FieldOfView;
	}
	ApplyFilm(nullptr);
	Tick(0.f);
}

void AOneBrahamFrontEndCamera::DriftPose(const FVector& InBase, const FVector& InLookAt, float InOrbitDegrees, float InRiseCm,
	float InPeriodSeconds, float InFramingYawDegrees, float Time, FVector& OutLocation, FRotator& OutRotation)
{
	const float Period = FMath::Max(InPeriodSeconds, 1.f);
	const float Phase = 2.f * PI * Time / Period;

	// Orbit: the placed offset from the point of interest, turned about the
	// vertical through it. Sine, so it eases at both ends and never stops dead.
	const FVector Offset = InBase - InLookAt;
	const float Yaw = InOrbitDegrees * FMath::Sin(Phase);
	const FVector Turned = FRotator(0.f, Yaw, 0.f).RotateVector(Offset);

	// Rise on half the frequency, a quarter-cycle out of step with the orbit,
	// so the path is a slow figure and not a line.
	const float Rise = InRiseCm * 0.5f * (1.f - FMath::Cos(Phase * 0.5f));

	OutLocation = InLookAt + Turned + FVector(0.f, 0.f, Rise);

	FRotator Look = (InLookAt - OutLocation).Rotation();
	Look.Yaw -= InFramingYawDegrees;
	Look.Roll = 0.f;
	OutRotation = Look;
}

void AOneBrahamFrontEndCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;

	// A dip's cut, made inside the black.
	if (!PendingShot.IsNone() && Now >= PendingAt)
	{
		const FName Id = PendingShot;
		PendingShot = NAME_None;
		if (const FOneBrahamMenuShot* Shot = FindShot(Id))
		{
			bBlending = false;
			PushSeconds = 0.f;
			ApplyShot(*Shot);
		}
	}

	if (!bFlat)
	{
		Elapsed += DeltaSeconds;
	}

	FVector Location;
	FRotator Rotation;
	DriftPose(BaseLocation, LookAt, bFlat ? 0.f : OrbitDegrees, bFlat ? 0.f : RiseCm, PeriodSeconds, FramingYawDegrees,
		Elapsed, Location, Rotation);
	float FOV = ShotFOV;

	// The lift: forward along the view, the lens opening.
	if (PushSeconds > 0.f)
	{
		const float T = UOneBrahamUIStyle::Ease(EOneBrahamEase::Lift, static_cast<float>((Now - PushAt) / PushSeconds));
		Location += Rotation.Vector() * PushCm * T;
		FOV = FMath::Lerp(PushFromFOV, PushToFOV, T);
	}

	// A blend: from wherever the camera was to the new shot's drifting pose.
	if (bBlending)
	{
		const float T = UOneBrahamUIStyle::Ease(EOneBrahamEase::Lift, static_cast<float>((Now - BlendAt) / BlendSeconds));
		Location = FMath::Lerp(BlendFromLocation, Location, T);
		Rotation = FQuat::Slerp(BlendFromRotation.Quaternion(), Rotation.Quaternion(), T).Rotator();
		FOV = FMath::Lerp(BlendFromFOV, FOV, T);
		bBlending = T < 1.f;
	}

	SetActorLocationAndRotation(Location, Rotation);
	if (GetCameraComponent() != nullptr && !FMath::IsNearlyEqual(GetCameraComponent()->FieldOfView, FOV))
	{
		GetCameraComponent()->SetFieldOfView(FOV);
	}
}

void AOneBrahamFrontEndCamera::Reposition(const FVector& NewLocation, const TOptional<FVector>& NewLookAt, float NewFOV)
{
	BaseLocation = NewLocation;
	if (NewLookAt.IsSet())
	{
		LookAt = NewLookAt.GetValue();
	}
	if (NewFOV > 1.f)
	{
		ShotFOV = NewFOV;
		if (GetCameraComponent() != nullptr)
		{
			GetCameraComponent()->SetFieldOfView(NewFOV);
		}
	}
	Elapsed = 0.f;
	bBlending = false;
	PushSeconds = 0.f;
	Tick(0.f);
}

const FOneBrahamMenuShot* AOneBrahamFrontEndCamera::FindShot(FName Id) const
{
	return Shots.FindByPredicate([Id](const FOneBrahamMenuShot& Shot) { return Shot.Id == Id; });
}

FName AOneBrahamFrontEndCamera::ResolveShot(FName Id) const
{
	const FOneBrahamMenuShot* Shot = FindShot(Id);
	if (Shot == nullptr)
	{
		return NAME_None;
	}
	if (Shot->bInterior && !Shot->FallbackId.IsNone())
	{
		// The interior shots need the reception in the level; the exterior-only
		// L_MainMenu falls back to an outside view of the same idea.
		bool bReception = false;
		for (TActorIterator<AOneBrahamDisplay> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("ReceptionWall")))
			{
				bReception = true;
				break;
			}
		}
		if (!bReception)
		{
			return Shot->FallbackId;
		}
	}
	return Shot->Id;
}

bool AOneBrahamFrontEndCamera::PlayShot(FName Id, EOneBrahamShotTransition Transition)
{
	const FName Resolved = ResolveShot(Id);
	const FOneBrahamMenuShot* Shot = FindShot(Resolved);
	if (Shot == nullptr)
	{
		UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] No menu shot '%s'."), *Id.ToString());
		return false;
	}

	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;
	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Shot %s (%s)."), *Resolved.ToString(),
		Transition == EOneBrahamShotTransition::Cut ? TEXT("cut") : Transition == EOneBrahamShotTransition::Dip ? TEXT("dip") : TEXT("blend"));

	if (Transition == EOneBrahamShotTransition::Dip && DipHandler)
	{
		DipHandler(DipOutSeconds, DipHoldSeconds, DipInSeconds);
		PendingShot = Resolved;
		// The cut lands in the middle of the held black.
		PendingAt = Now + DipOutSeconds + DipHoldSeconds * 0.5f;
		CurrentShot = Resolved;
		return true;
	}

	if (Transition == EOneBrahamShotTransition::Blend && !bFlat)
	{
		bBlending = true;
		BlendAt = Now;
		BlendFromLocation = GetActorLocation();
		BlendFromRotation = GetActorRotation();
		BlendFromFOV = GetCameraComponent() != nullptr ? GetCameraComponent()->FieldOfView : ShotFOV;
		PendingShot = NAME_None;
		PushSeconds = 0.f;
		BaseLocation = Shot->Location;
		LookAt = Shot->LookAt;
		OrbitDegrees = Shot->OrbitDeg;
		RiseCm = Shot->RiseCm;
		PeriodSeconds = FMath::Max(10.f, Shot->DriftPeriodS);
		FramingYawDegrees = Shot->FramingYaw;
		ShotFOV = Shot->FOV;
		Elapsed = 0.f;
		CurrentShot = Resolved;
		ApplyFilm(Shot);
		return true;
	}

	PendingShot = NAME_None;
	bBlending = false;
	PushSeconds = 0.f;
	ApplyShot(*Shot);
	return true;
}

void AOneBrahamFrontEndCamera::ApplyShot(const FOneBrahamMenuShot& Shot)
{
	OrbitDegrees = Shot.bLocked ? 0.f : Shot.OrbitDeg;
	RiseCm = Shot.bLocked ? 0.f : Shot.RiseCm;
	PeriodSeconds = FMath::Max(10.f, Shot.DriftPeriodS);
	FramingYawDegrees = Shot.FramingYaw;
	CurrentShot = Shot.Id;
	ApplyFilm(&Shot);
	Reposition(Shot.Location, TOptional<FVector>(Shot.LookAt), Shot.FOV);
}

void AOneBrahamFrontEndCamera::PushIn(float Cm, float ToFOV, float Seconds)
{
	const UWorld* World = GetWorld();
	PushAt = World != nullptr ? World->GetRealTimeSeconds() : 0.0;
	PushSeconds = bFlat ? 0.f : FMath::Max(0.01f, Seconds);
	PushCm = Cm;
	PushFromFOV = ShotFOV;
	PushToFOV = ToFOV;
}

void AOneBrahamFrontEndCamera::ApplyFilm(const FOneBrahamMenuShot* Shot)
{
	UCameraComponent* Camera = GetCameraComponent();
	if (Camera == nullptr)
	{
		return;
	}

	// The film treatment (palette_and_motion): a light grain scaled by the
	// player's own Film Grain switch, a vignette, a daylight white balance, a
	// touch less saturation, and depth of field only where there is an
	// interior to throw out of focus. No fake light leaks: the light in the
	// picture is the building's.
	const UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const bool bGrain = User == nullptr || User->bFilmGrain;
	const bool bLowRes = User != nullptr && User->ResolutionScale < 75.f;

	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = bGrain ? (bLowRes ? 0.15f : 0.22f) : 0.f;
	PP.bOverride_VignetteIntensity = true;
	// The darkened frontage shots close in harder at the corners too.
	PP.VignetteIntensity = Shot != nullptr && Shot->ExposureBias < 0.f ? 0.62f : 0.40f;
	PP.bOverride_WhiteTemp = true;
	PP.WhiteTemp = 5600.f;
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(0.92f, 0.92f, 0.92f, 1.f);

	const bool bDoF = Shot != nullptr && Shot->bInterior && Shot->FocusDistanceCm > 0.f;
	PP.bOverride_DepthOfFieldFocalDistance = bDoF;
	PP.DepthOfFieldFocalDistance = bDoF ? Shot->FocusDistanceCm : 0.f;
	PP.bOverride_DepthOfFieldFstop = bDoF;
	PP.DepthOfFieldFstop = bDoF ? Shot->Aperture : 4.f;
	PP.bOverride_AutoExposureBias = Shot != nullptr && Shot->ExposureBias != 0.f;
	PP.AutoExposureBias = Shot != nullptr ? Shot->ExposureBias : 0.f;
	Camera->PostProcessBlendWeight = 1.f;
}

// ==========================================================================
//  Controller
// ==========================================================================
namespace OneBrahamFrontEnd
{
	/** Console work that arrived before the menu was built. */
	TArray<TFunction<void(UOneBrahamMainMenuWidget&)>>& PendingMenuWork()
	{
		static TArray<TFunction<void(UOneBrahamMainMenuWidget&)>> Work;
		return Work;
	}
}

AOneBrahamFrontEndPlayerController::AOneBrahamFrontEndPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void AOneBrahamFrontEndPlayerController::QueueForMenu(TFunction<void(UOneBrahamMainMenuWidget&)> Work)
{
	OneBrahamFrontEnd::PendingMenuWork().Add(MoveTemp(Work));
}

void AOneBrahamFrontEndPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	PrepareReceptionWall();
	UseMenuCamera();
	ShowMenu();

	// The camera's dips are the menu's black: the cut happens inside it.
	if (MenuCamera != nullptr)
	{
		TWeakObjectPtr<AOneBrahamFrontEndPlayerController> WeakThis(this);
		MenuCamera->DipHandler = [WeakThis](float Out, float Hold, float In)
		{
			if (WeakThis.IsValid() && WeakThis->Menu != nullptr)
			{
				WeakThis->Menu->DipToBlack(Out, Hold, In);
			}
		};
		if (Menu != nullptr)
		{
			Menu->AttachCamera(MenuCamera);
		}
	}

	if (UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
	{
		// Whatever brought the player back - a dropped connection, a kick, the
		// end of a match - is said once, on the menu, and then forgotten.
		const FText Notice = GameInstance->ConsumeMenuNotice();
		if (Menu != nullptr && !Notice.IsEmpty())
		{
			Menu->ShowNotice(Notice);
		}
		GameInstance->HideLoadingScreen();
	}

	if (Menu != nullptr)
	{
		TArray<TFunction<void(UOneBrahamMainMenuWidget&)>> Work = MoveTemp(OneBrahamFrontEnd::PendingMenuWork());
		OneBrahamFrontEnd::PendingMenuWork().Reset();
		for (TFunction<void(UOneBrahamMainMenuWidget&)>& Item : Work)
		{
			Item(*Menu);
		}
	}

	UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] Front end up: main menu shown."));
}

void AOneBrahamFrontEndPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Menu != nullptr)
	{
		Menu->RemoveFromParent();
		Menu = nullptr;
	}

	// The menu had the cursor and UI-only input; the match's controller sets
	// its own mode, but not before its first frame, so nothing is left behind.
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}

	Super::EndPlay(EndPlayReason);
}

void AOneBrahamFrontEndPlayerController::UseMenuCamera()
{
	TActorIterator<AOneBrahamFrontEndCamera> First(GetWorld());
	AOneBrahamFrontEndCamera* Camera = First ? *First : nullptr;

	if (Camera == nullptr)
	{
		// No camera in the level (a code-only checkout, or a level built before
		// the front end): stand where the level's camera would.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Camera = GetWorld()->SpawnActor<AOneBrahamFrontEndCamera>(AOneBrahamFrontEndGameMode::DefaultCameraLocation(),
			FRotator::ZeroRotator, Params);
		if (Camera != nullptr)
		{
			Camera->LookAt = AOneBrahamFrontEndGameMode::DefaultCameraLookAt();
			Camera->Reposition(AOneBrahamFrontEndGameMode::DefaultCameraLocation(), TOptional<FVector>(), 0.f);
		}
		FallbackCamera = Camera;
	}

	if (Camera != nullptr)
	{
		SetViewTarget(Camera);
	}
	MenuCamera = Camera;
}

namespace
{
	/** The reception wall's copy, emissive x this (M_UI_DisplayText's Glow). */
	constexpr float ReceptionCopyGlow = 2400.f;
}

void AOneBrahamFrontEndPlayerController::PrepareReceptionWall()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	AOneBrahamDisplay* Wall = nullptr;
	for (TActorIterator<AOneBrahamDisplay> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("ReceptionWall")))
		{
			Wall = *It;
			break;
		}
	}

	if (Wall == nullptr)
	{
		// L_MainMenu is the outside of the tower only. The reception's screen
		// is placed here, on the entrance front behind where the glass would
		// be, at the height the real one hangs over the desk: the title
		// designer of the front end, and the first thing the cold open shows.
		UMaterialInterface* Screen = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OneBraham/Materials/M_OB_AdScreen.M_OB_AdScreen"),
			nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Screen == nullptr)
		{
			UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] No M_OB_AdScreen: the reception wall is not placed (the reel skips it)."));
			return;
		}

		const FTransform Where(FRotator(0.f, -90.f, 0.f), FVector(760.f, -1795.f, -105.f));
		Wall = World->SpawnActorDeferred<AOneBrahamDisplay>(AOneBrahamDisplay::StaticClass(), Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Wall == nullptr)
		{
			return;
		}
		Wall->WidthCm = 1080.f;
		Wall->HeightCm = 300.f;
		// L_MainMenu holds dusk's exposure (EV 11.3), far above an office
		// floor's: the screen burns brighter to read as a screen.
		Wall->Brightness = 900.f;
		Wall->ScreenMaterial = Screen;
		Wall->BezelMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/OneBraham/Materials/Instances/MI_OB_District_BlackCladding.MI_OB_District_BlackCladding"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		Wall->AccentMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/OneBraham/Materials/Instances/MI_OB_Signage_White.MI_OB_Signage_White"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		// The copy glows like the panel (build_materials.build_ui_display_text):
		// the engine's lit text is a grey smudge at the dusk exposure.
		Wall->TextMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/OneBraham/UI/Materials/MI_UI_DisplayText_FrontEnd.MI_UI_DisplayText_FrontEnd"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Wall->TextMaterial == nullptr)
		{
			Wall->TextMaterial = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/EngineMaterials/DefaultTextMaterialTranslucent.DefaultTextMaterialTranslucent"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		else
		{
			// Seen through the entrance glass the copy loses most of its light:
			// at the instance's glow ON BREAK read grey on the aurora. The title
			// of the whole front end burns white, a stop over the panel.
			UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(Wall->TextMaterial, Wall);
			Glow->SetScalarParameterValue(TEXT("Glow"), ReceptionCopyGlow);
			Wall->TextMaterial = Glow;
		}
		Wall->Tags.Add(TEXT("ReceptionWall"));
		Wall->SetFlags(RF_Transient);
		Wall->FinishSpawning(Where);
	}

	// The wall says ON BREAK for the whole front end: one slide, held.
	FOneBrahamDisplaySlide Slide;
	AOneBrahamFrontEndGameMode::ReceptionSlide(AOneBrahamFrontEndGameMode::IsReturningFromMatch(),
		AOneBrahamFrontEndGameMode::SignedOutClock(), Slide.Eyebrow, Slide.Headline, Slide.Subline);
	Slide.ColourA = UOneBrahamUIStyle::Colour(EOneBrahamColour::RibbonA);
	Slide.ColourB = UOneBrahamUIStyle::Colour(EOneBrahamColour::RibbonB);
	Wall->Slides = { Slide };
	Wall->SlideSeconds = 600.f;
	Wall->PhaseSeconds = Wall->FadeSeconds * 2.f;
	Wall->RerunConstructionScripts();
	Wall->SetActorTickEnabled(true);
}

void AOneBrahamFrontEndPlayerController::ShowMenu()
{
	if (Menu == nullptr)
	{
		Menu = CreateWidget<UOneBrahamMainMenuWidget>(this, UOneBrahamMainMenuWidget::StaticClass());
	}

	if (Menu == nullptr)
	{
		UE_LOG(LogOneBraham, Error, TEXT("[OB.MENU] The main menu widget could not be created."));
		return;
	}

	if (!Menu->IsInViewport())
	{
		Menu->AddToViewport(10);
	}

	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetWidgetToFocus(Menu->TakeWidget());
	SetInputMode(Mode);
	bShowMouseCursor = true;
}

// ==========================================================================
//  Game mode
// ==========================================================================
AOneBrahamFrontEndGameMode::AOneBrahamFrontEndGameMode()
{
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	PlayerControllerClass = AOneBrahamFrontEndPlayerController::StaticClass();
	HUDClass = AHUD::StaticClass();
	GameStateClass = AGameStateBase::StaticClass();
	PlayerStateClass = APlayerState::StaticClass();
	bStartPlayersAsSpectators = false;
	bUseSeamlessTravel = false;
}

namespace OneBrahamFrontEnd
{
	/** Process-wide: survives the map loads between the menu and a match. */
	bool bLeftForMatch = false;
	FString ReturnClock;
}

void AOneBrahamFrontEndGameMode::NoteLeavingForMatch()
{
	OneBrahamFrontEnd::bLeftForMatch = true;
	OneBrahamFrontEnd::ReturnClock.Reset();
}

bool AOneBrahamFrontEndGameMode::IsReturningFromMatch()
{
	return OneBrahamFrontEnd::bLeftForMatch;
}

FString AOneBrahamFrontEndGameMode::SignedOutClock()
{
	if (OneBrahamFrontEnd::bLeftForMatch && OneBrahamFrontEnd::ReturnClock.IsEmpty())
	{
		OneBrahamFrontEnd::ReturnClock = UOneBrahamUIStyle::LocalClock(false).ToString();
	}
	return OneBrahamFrontEnd::ReturnClock;
}

bool& AOneBrahamFrontEndGameMode::ColdOpenPlayed()
{
	static bool bPlayed = false;
	return bPlayed;
}

const TArray<FOneBrahamMenuShot>& AOneBrahamFrontEndGameMode::DefaultShots()
{
	// Authored against L_MainMenu (build_frontend.py): the tower at the
	// origin, its glass at x +-2215 / y +-1815, the plaza at z -400, the
	// entrance front on -y (Braham Street), fins on the other three faces.
	auto Shot = [](const TCHAR* Id, const FVector& Location, const FVector& LookAt, float FOV, float Orbit, float Rise,
		float Period, float FramingYaw, bool bLocked = false, bool bInterior = false, const TCHAR* Fallback = nullptr)
	{
		FOneBrahamMenuShot Out;
		Out.Id = Id;
		Out.Location = Location;
		Out.LookAt = LookAt;
		Out.FOV = FOV;
		Out.OrbitDeg = Orbit;
		Out.RiseCm = Rise;
		Out.DriftPeriodS = Period;
		Out.FramingYaw = FramingYaw;
		Out.bLocked = bLocked;
		Out.bInterior = bInterior;
		Out.FallbackId = Fallback != nullptr ? FName(Fallback) : NAME_None;
		return Out;
	};

	// The street-level shots at the frontage: under the dusk exposure the
	// entrance's spandrel panels are a flat grey grid. A stop or so down and a
	// near focus at a wide aperture, they fall to soft dark and the one thing
	// lit is the wall: a glow behind the badge, the ledger and the board.
	// L_MainMenu sits at EV 11.3; Lumen's cached lighting clips (and says so,
	// in red, over the picture) past EV 12. Two thirds of a stop is the room.
	constexpr float MaxStopsUnder = -0.6f;
	auto Frontage = [](FOneBrahamMenuShot Out, float FocusCm, float Fstop, float Exposure)
	{
		Out.FocusDistanceCm = FocusCm;
		Out.Aperture = Fstop;
		Out.ExposureBias = Exposure;
		return Out;
	};

	static const TArray<FOneBrahamMenuShot> Shots = {
		// From the north-east at the fourteenth floor, long lens: the stepped
		// crown and its fins against the City's towers and the dusk.
		Shot(TEXT("crown"),		FVector(11000.f, 7000.f, 4400.f),	FVector(0.f, 0.f, 6500.f),		24.f, 3.f, 150.f, 40.f, 11.f),
		// The south-east corner from the pavement, straight up the fins.
		Shot(TEXT("fins"),		FVector(2900.f, -2600.f, -230.f),	FVector(1500.f, -1200.f, 6000.f), 32.f, 2.f, 60.f, 30.f, 0.f),
		// Braham Street: the reception's OLED wall through the entrance front.
		// Held a stop under, so the screen is the light in the picture.
		Frontage(Shot(TEXT("reception"), FVector(2300.f, -4700.f, -210.f), FVector(760.f, -1795.f, -120.f), 32.f, 2.f, 40.f, 30.f, 3.f),
			0.f, 2.f, MaxStopsUnder),
		// The south-west corner of the plaza, looking straight up to the crown.
		Shot(TEXT("plaza_up"),	FVector(-2700.f, -2450.f, -240.f),	FVector(-1700.f, -1300.f, 7400.f), 30.f, 2.f, 40.f, 30.f, 0.f),
		Shot(TEXT("plaza_low"),	FVector(-3000.f, -6000.f, -240.f),	FVector(0.f, -1800.f, 600.f),	40.f, 2.f, 40.f, 34.f, -8.f),
		// HOST and JOIN: the wall held in the left half, the ledger on the right.
		Frontage(Shot(TEXT("reception_desk"), FVector(-300.f, -5200.f, -220.f), FVector(760.f, -1795.f, -100.f), 30.f, 1.5f, 30.f, 34.f, -5.f,
			false, true, TEXT("plaza_low")), 180.f, 1.2f, MaxStopsUnder),
		// The Skydeck, out over the City: locked off for the credits.
		Shot(TEXT("skydeck"),	FVector(-1500.f, 1300.f, 5790.f),	FVector(-15000.f, 4000.f, 5200.f), 40.f, 2.f, 40.f, 36.f, 0.f, true),
		// SIGN OUT: along the frontage from the street, the wall glowing behind the badge.
		Frontage(Shot(TEXT("doors_out"), FVector(-1200.f, -3600.f, -240.f), FVector(900.f, -1795.f, -60.f), 32.f, 1.f, 20.f, 30.f, 0.f,
			false, true, TEXT("plaza_low")), 150.f, 1.2f, MaxStopsUnder),
	};
	return Shots;
}

void AOneBrahamFrontEndGameMode::ReceptionSlide(bool bReturning, const FString& SignedOutClock, FText& OutEyebrow,
	FText& OutHeadline, FText& OutSubline)
{
	OutHeadline = FText::FromString(ONE_BRAHAM_DISPLAY_NAME);
	if (bReturning)
	{
		OutEyebrow = NSLOCTEXT("OneBraham", "Reception_WelcomeBack", "WELCOME BACK");
		OutSubline = SignedOutClock.IsEmpty()
			? NSLOCTEXT("OneBraham", "Reception_SignedOutNoClock", "You have signed out.")
			: FText::Format(NSLOCTEXT("OneBraham", "Reception_SignedOut", "You signed out at {0}."), FText::FromString(SignedOutClock));
	}
	else
	{
		OutEyebrow = NSLOCTEXT("OneBraham", "Reception_Eyebrow", "ONE BRAHAM \u00B7 ALDGATE");
		OutSubline = NSLOCTEXT("OneBraham", "Reception_SignIn", "Please sign in at reception.");
	}
}

void AOneBrahamFrontEndGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	// A client that reaches a machine sitting in its menu (an old LAN beacon,
	// a stale address) is turned away rather than dropped into a level with
	// no pawn and nothing to do.
	if (ErrorMessage.IsEmpty() && !Address.IsEmpty())
	{
		ErrorMessage = TEXT("That machine is in its main menu, not a match.");
	}
}

// ==========================================================================
//  Console commands
// ==========================================================================
namespace OneBrahamFrontEnd
{
	UOneBrahamGameInstance* GameInstanceFor(UWorld* World)
	{
		return World != nullptr ? World->GetGameInstance<UOneBrahamGameInstance>() : nullptr;
	}

	/** The live menu in this world, or null. */
	UOneBrahamMainMenuWidget* MenuIn(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}

		const AOneBrahamFrontEndPlayerController* Controller = Cast<AOneBrahamFrontEndPlayerController>(World->GetFirstPlayerController());
		return Controller != nullptr ? Controller->GetMenu() : nullptr;
	}

	bool InFrontEnd(UWorld* World)
	{
		return World != nullptr && World->GetAuthGameMode<AOneBrahamFrontEndGameMode>() != nullptr;
	}

	/** Runs Work on the menu now, or as soon as it exists. */
	void WithMenu(UWorld* World, TFunction<void(UOneBrahamMainMenuWidget&)> Work)
	{
		if (UOneBrahamMainMenuWidget* Menu = MenuIn(World))
		{
			Work(*Menu);
			return;
		}
		AOneBrahamFrontEndPlayerController::QueueForMenu(MoveTemp(Work));
	}

	EOneBrahamMatchMode ModeArg(const TArray<FString>& Args, int32 Index, EOneBrahamMatchMode Default)
	{
		EOneBrahamMatchMode Mode = Default;
		if (Args.IsValidIndex(Index) && !OneBrahamSessionRules::ParseMatchMode(Args[Index], Mode))
		{
			UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] '%s' is not a mode (ascent, firedrill) - using %s."),
				*Args[Index], *OneBrahamSessionRules::ModeKey(Default));
		}
		return Mode;
	}

	FAutoConsoleCommandWithWorldAndArgs MenuPlayCommand(
		TEXT("ob.menu.play"),
		TEXT("ob.menu.play <ascent|firedrill> - a solo match from the main menu, bots in the empty slots."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const EOneBrahamMatchMode Mode = ModeArg(Args, 0, EOneBrahamMatchMode::Ascent);
			UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.play %s"), *OneBrahamSessionRules::ModeKey(Mode));

			if (!InFrontEnd(World))
			{
				if (UOneBrahamGameInstance* GameInstance = GameInstanceFor(World))
				{
					GameInstance->StartSoloMatch(Mode);
				}
				return;
			}
			WithMenu(World, [Mode](UOneBrahamMainMenuWidget& Menu) { Menu.PlaySolo(Mode); });
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuHostCommand(
		TEXT("ob.menu.host"),
		TEXT("ob.menu.host [ascent|firedrill] [lan|online] - create a session and travel to the tower with ?listen."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const EOneBrahamMatchMode Mode = ModeArg(Args, 0, EOneBrahamMatchMode::Ascent);
			const bool bLAN = !(Args.IsValidIndex(1) && Args[1].Equals(TEXT("online"), ESearchCase::IgnoreCase));
			UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.host %s %s"), *OneBrahamSessionRules::ModeKey(Mode),
				bLAN ? TEXT("lan") : TEXT("online"));
			WithMenu(World, [Mode, bLAN](UOneBrahamMainMenuWidget& Menu) { Menu.Host(Mode, bLAN); });
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuJoinCommand(
		TEXT("ob.menu.join"),
		TEXT("ob.menu.join [row] - open JOIN, search (again, for up to a minute) and join that row of the list."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const int32 Row = Args.IsValidIndex(0) ? FMath::Max(0, FCString::Atoi(*Args[0])) : 0;
			UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.join %d"), Row);
			WithMenu(World, [Row](UOneBrahamMainMenuWidget& Menu) { Menu.JoinWhenFound(Row, 30); });
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuPageCommand(
		TEXT("ob.menu.page"),
		TEXT("ob.menu.page <home|play|host|join|settings|credits> - show a page of the main menu."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UOneBrahamMainMenuWidget::EPage Page = UOneBrahamMainMenuWidget::EPage::Home;
			if (!Args.IsValidIndex(0) || !UOneBrahamMainMenuWidget::ParsePage(Args[0], Page))
			{
				UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] ob.menu.page <home|play|host|join|settings|credits>"));
				return;
			}
			WithMenu(World, [Page](UOneBrahamMainMenuWidget& Menu) { Menu.ShowPage(Page); });
		}));

	FAutoConsoleCommandWithWorld MenuLeaveCommand(
		TEXT("ob.menu.leave"),
		TEXT("ob.menu.leave - leave the match for the main menu, as the pause menu's LEAVE MATCH does."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.leave"));
			if (UOneBrahamGameInstance* GameInstance = GameInstanceFor(World))
			{
				GameInstance->TravelToMainMenu();
			}
		}));

#if !UE_BUILD_SHIPPING
	/** The game world a deferred command should run in, whichever map is up now. */
	UWorld* CurrentGameWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
			{
				if (Context.World() != nullptr)
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs MenuAfterCommand(
		TEXT("ob.menu.after"),
		TEXT("ob.menu.after <seconds> <command ...> - runs the command later, in whatever map is loaded then. Non-Shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] ob.menu.after <seconds> <command ...>"));
				return;
			}

			const float Delay = FMath::Max(0.f, FCString::Atof(*Args[0]));
			TArray<FString> Rest(Args.GetData() + 1, Args.Num() - 1);
			const FString Command = FString::Join(Rest, TEXT(" "));

			// The core ticker, not a world timer: the command usually outlives the
			// map it was typed in (a leave scheduled from the menu runs in the tower).
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Command](float) -> bool
			{
				UWorld* Now = CurrentGameWorld();
				UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.after: %s"), *Command);
				if (GEngine != nullptr)
				{
					GEngine->Exec(Now, *Command);
				}
				return false;
			}), Delay);
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuShotCommand(
		TEXT("ob.menu.shot"),
		TEXT("ob.menu.shot <name> [quit] - a screenshot (UI included) to Saved/Screenshots/<platform>/<name>.png; quit once it is written. Non-Shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FString Name = Args.IsValidIndex(0) ? FPaths::MakeValidFileName(Args[0]) : FString(TEXT("OBMenu"));
			const bool bQuit = Args.IsValidIndex(1) && (Args[1] == TEXT("1") || Args[1].Equals(TEXT("quit"), ESearchCase::IgnoreCase));

			FString Path = FPaths::Combine(FPaths::ScreenShotDir(), Name + TEXT(".png"));
			IFileManager::Get().Delete(*Path, false, true, true);
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			Path = FPaths::ConvertRelativePathToFull(Path);
			UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.shot requested: %s"), *Path);

			// Wait for the file (a frame or two), then report it - and quit if asked.
			const double Deadline = FPlatformTime::Seconds() + 20.0;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Path, bQuit, Deadline](float) -> bool
			{
				const bool bWritten = IFileManager::Get().FileSize(*Path) > 0;
				if (!bWritten && FPlatformTime::Seconds() < Deadline)
				{
					return true;
				}

				UE_LOG(LogOneBraham, Log, TEXT("[OB.MENU] ob.menu.shot %s: %s"), bWritten ? TEXT("written") : TEXT("NOT written"), *Path);
				if (bQuit)
				{
					FPlatformMisc::RequestExit(false, TEXT("ob.menu.shot"));
				}
				return false;
			}), 0.25f);
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuCutCommand(
		TEXT("ob.menu.cut"),
		TEXT("ob.menu.cut <shot> [cut|dip|blend] - play one of the menu camera's authored shots. Non-Shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1 || World == nullptr)
			{
				UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] ob.menu.cut <shot> [cut|dip|blend]"));
				return;
			}
			EOneBrahamShotTransition Transition = EOneBrahamShotTransition::Cut;
			if (Args.IsValidIndex(1))
			{
				Transition = Args[1].Equals(TEXT("dip"), ESearchCase::IgnoreCase) ? EOneBrahamShotTransition::Dip
					: Args[1].Equals(TEXT("blend"), ESearchCase::IgnoreCase) ? EOneBrahamShotTransition::Blend
					: EOneBrahamShotTransition::Cut;
			}
			WithMenu(World, [Shot = FName(*Args[0]), Transition](UOneBrahamMainMenuWidget& Menu) { Menu.HoldShot(Shot, Transition); });
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuModeCommand(
		TEXT("ob.menu.mode"),
		TEXT("ob.menu.mode <ascent|firedrill> - focus that mode's card on PLAY (its section draws on the right). Non-Shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const EOneBrahamMatchMode Mode = ModeArg(Args, 0, EOneBrahamMatchMode::Ascent);
			WithMenu(World, [Mode](UOneBrahamMainMenuWidget& Menu) { Menu.FocusMode(Mode); });
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuLoadingCommand(
		TEXT("ob.menu.loading"),
		TEXT("ob.menu.loading [seconds] - puts the lift loading page over the viewport (for a screenshot), then takes it down. Non-Shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (GEngine == nullptr || GEngine->GameViewport == nullptr)
			{
				return;
			}
			const float Seconds = Args.IsValidIndex(0) ? FMath::Max(1.f, FCString::Atof(*Args[0])) : 8.f;
			TSharedRef<SWidget> Page = SNew(SOneBrahamLiftLoading)
				.ModeName(NSLOCTEXT("OneBraham", "Menu_ModeAscent", "ASCENT"))
				.Status(NSLOCTEXT("OneBraham", "LoadingBuilding", "Entering One Braham..."))
				.Squad({ FString(TEXT("You")) })
				.TargetFloor(18);
			GEngine->GameViewport->AddViewportWidgetContent(Page, 1000);
			TWeakPtr<SWidget> Weak = Page;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float) -> bool
			{
				if (GEngine != nullptr && GEngine->GameViewport != nullptr && Weak.IsValid())
				{
					GEngine->GameViewport->RemoveViewportWidgetContent(Weak.Pin().ToSharedRef());
				}
				return false;
			}), Seconds);
		}));

	FAutoConsoleCommandWithWorldAndArgs MenuCameraCommand(
		TEXT("ob.menu.camera"),
		TEXT("ob.menu.camera X Y Z [LookX LookY LookZ] [FOV] - moves the menu camera's orbit centre. Non-Shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 3 || World == nullptr)
			{
				UE_LOG(LogOneBraham, Warning, TEXT("[OB.MENU] ob.menu.camera X Y Z [LookX LookY LookZ] [FOV]"));
				return;
			}

			const FVector Location(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
			TOptional<FVector> LookAt;
			if (Args.Num() >= 6)
			{
				LookAt = FVector(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), FCString::Atof(*Args[5]));
			}
			const float FOV = Args.Num() >= 7 ? FCString::Atof(*Args[6]) : 0.f;

			// A hand-placed camera holds: the HOME reel stops cutting away from it.
			WithMenu(World, [](UOneBrahamMainMenuWidget& Menu) { Menu.HoldShot(NAME_None, EOneBrahamShotTransition::Cut); });
			for (TActorIterator<AOneBrahamFrontEndCamera> It(World); It; ++It)
			{
				It->Reposition(Location, LookAt, FOV);
			}
		}));
#endif
}
