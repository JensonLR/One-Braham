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

	Elapsed += DeltaSeconds;

	FVector Location;
	FRotator Rotation;
	DriftPose(BaseLocation, LookAt, OrbitDegrees, RiseCm, PeriodSeconds, FramingYawDegrees, Elapsed, Location, Rotation);
	SetActorLocationAndRotation(Location, Rotation);
}

void AOneBrahamFrontEndCamera::Reposition(const FVector& NewLocation, const TOptional<FVector>& NewLookAt, float NewFOV)
{
	BaseLocation = NewLocation;
	if (NewLookAt.IsSet())
	{
		LookAt = NewLookAt.GetValue();
	}
	if (NewFOV > 1.f && GetCameraComponent() != nullptr)
	{
		GetCameraComponent()->SetFieldOfView(NewFOV);
	}
	Elapsed = 0.f;
	Tick(0.f);
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

	UseMenuCamera();
	ShowMenu();

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

			for (TActorIterator<AOneBrahamFrontEndCamera> It(World); It; ++It)
			{
				It->Reposition(Location, LookAt, FOV);
			}
		}));
#endif
}
