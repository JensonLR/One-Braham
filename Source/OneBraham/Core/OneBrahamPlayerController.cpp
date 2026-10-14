// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamPlayerController.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerState.h"
#include "Core/OneBrahamSaveSettings.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Interaction/OneBrahamInteractComponent.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "UI/OneBrahamHUDComponent.h"
#include "UI/OneBrahamHUDWidget.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "VFX/OneBrahamCameraShake.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

AOneBrahamPlayerController::AOneBrahamPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;

	// The FPS camera is driven by the pawn, so the controller does not need a
	// view target of its own except while spectating.
	bAttachToPawn = false;

	HUDComponent = CreateDefaultSubobject<UOneBrahamHUDComponent>(TEXT("HUDComponent"));
	AudioManager = CreateDefaultSubobject<UOneBrahamAudioManagerComponent>(TEXT("AudioManager"));

	// A 90 degree default FOV matches the weapon art and the HUD safe area.
	PlayerCameraManagerClass = APlayerCameraManager::StaticClass();
}

void AOneBrahamPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamPlayerController, bIsSpectating);
	DOREPLIFETIME(AOneBrahamPlayerController, SpectatedPlayerId);
}

// ==========================================================================
//  Lifecycle
// ==========================================================================
void AOneBrahamPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings != nullptr)
	{
		EffectiveSensitivity = Settings->MouseSensitivityX;
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->SetFOV(Settings->FOV);
		}
		Settings->OnSettingsChanged.AddDynamic(this, &AOneBrahamPlayerController::HandleSettingsChanged);
	}

	// Validate the input config early: a missing IA_* asset is a support ticket,
	// not a silent dead key.
	if (InputConfig != nullptr)
	{
		TArray<FName> Missing;
		if (!InputConfig->ValidateAllActionsSet(Missing))
		{
			OB_ERROR("Input config '%s' is missing %d action(s). Those keys will do nothing until "
				"Tools/Python/onebraham/build_input.py has been run and the config re-saved.",
				*InputConfig->GetName(), Missing.Num());
		}
	}
	else if (!IsRunningDedicatedServer())
	{
		OB_WARN("AOneBrahamPlayerController has no InputConfig assigned - no input will be bound. "
			"Open BP_OneBrahamPlayerController and set InputConfig = DA_OneBrahamInputConfig.");
	}

	// The mapping contexts may be set on the Blueprint, or named in Project
	// Settings. Blueprint first: it is the more specific statement of intent.
	if (CommonMappingContext == nullptr)
	{
		CommonMappingContext = UOneBrahamGameSettings::Get().CommonMappingContext.LoadSynchronous();
	}

	if (GameplayMappingContext == nullptr)
	{
		GameplayMappingContext = UOneBrahamGameSettings::Get().DefaultMappingContext.LoadSynchronous();
	}

	// Install the local mapping contexts.
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (CommonMappingContext)
		{
			Subsystem->AddMappingContext(CommonMappingContext, 0);
		}
		if (GameplayMappingContext)
		{
			Subsystem->AddMappingContext(GameplayMappingContext, 1);
		}
	}

	// Create the HUD once we have a viewport.
	if (IsLocalController() && HUDComponent != nullptr)
	{
		HUDComponent->CreateHUD(this);
	}

	OB_LOG("PlayerController ready (local=%s authority=%s)", IsLocalController() ? TEXT("yes") : TEXT("no"), HasAuthority() ? TEXT("yes") : TEXT("no"));
}

void AOneBrahamPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Settings->OnSettingsChanged.RemoveDynamic(this, &AOneBrahamPlayerController::HandleSettingsChanged);
	}

	if (HUDComponent != nullptr)
	{
		HUDComponent->DestroyHUD();
	}

	Super::EndPlay(EndPlayReason);
}

void AOneBrahamPlayerController::InitPlayerState()
{
	Super::InitPlayerState();

	if (AOneBrahamPlayerState* OBState = GetPlayerState<AOneBrahamPlayerState>())
	{
		// Seed the archetype + cosmetics from the local profile so a listen host
		// looks like what they chose in the locker. A dedicated server overwrites
		// this in PostLogin from the travel options.
		if (const UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>())
		{
			if (const UOneBrahamSaveSettings* Profile = GameInstance->GetProfile())
			{
				if (Profile->Loadouts.IsValidIndex(Profile->SelectedLoadoutIndex))
				{
					const FOneBrahamSavedLoadout& Loadout = Profile->Loadouts[Profile->SelectedLoadoutIndex];
					OBState->EquippedCosmetics = Profile->EquippedCosmetics;
					OBState->ClanTag = Profile->ClanTag;

					// The weapons travel with it. The profile is local, so this
					// machine is the only place the choice exists; everything
					// after this point reads the player state.
					if (HasAuthority())
					{
						ServerSetLoadout(Loadout.PrimaryWeaponId, Loadout.SidearmWeaponId,
							Loadout.UtilityIds, Loadout.WeaponSkinId);
					}
				}
			}
		}

		OBState->SessionMode = GetWorld() && GetWorld()->GetGameState<AOneBrahamGameState>()
			? GetWorld()->GetGameState<AOneBrahamGameState>()->MatchMode
			: EOneBrahamMatchMode::Ascent;
	}
}

void AOneBrahamPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (IsLocalController() && HUDComponent != nullptr)
	{
		HUDComponent->BindToPawn(InPawn);
	}

	PushMappingContext(GameplayMappingContext, 1);
}

void AOneBrahamPlayerController::OnUnPossess()
{
	if (HUDComponent != nullptr)
	{
		HUDComponent->UnbindFromPawn();
	}

	Super::OnUnPossess();
}

AOneBrahamCharacter* AOneBrahamPlayerController::GetOneBrahamCharacter() const
{
	return Cast<AOneBrahamCharacter>(GetPawn());
}

// ==========================================================================
//  Input setup
// ==========================================================================
void AOneBrahamPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BindInputActions();
}

void AOneBrahamPlayerController::BindInputActions()
{
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (EIC == nullptr)
	{
		// The project sets DefaultInputComponentClass to EnhancedInputComponent;
		// if that was overridden in a derived BP we cannot bind anything.
		OB_ERROR("InputComponent is not an UEnhancedInputComponent - check "
			"Config/DefaultInput.ini DefaultInputComponentClass.");
		return;
	}

	if (InputConfig == nullptr)
	{
		return;
	}

	// --- Movement ---------------------------------------------------------
	if (InputConfig->Move)			EIC->BindAction(InputConfig->Move, ETriggerEvent::Triggered, this, &AOneBrahamPlayerController::Input_Move);
	if (InputConfig->Look)
	{
		// Mouse and gamepad share the action but use different handlers so the
		// aim-assist curve only applies to the stick.
		EIC->BindAction(InputConfig->Look, ETriggerEvent::Triggered, this, &AOneBrahamPlayerController::Input_Look);
	}
	if (InputConfig->Jump)
	{
		EIC->BindAction(InputConfig->Jump, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_JumpStarted);
		EIC->BindAction(InputConfig->Jump, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_JumpCompleted);
	}
	if (InputConfig->Sprint)
	{
		EIC->BindAction(InputConfig->Sprint, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_SprintStarted);
		EIC->BindAction(InputConfig->Sprint, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_SprintCompleted);
	}
	if (InputConfig->Walk)			EIC->BindAction(InputConfig->Walk, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_WalkToggle);
	if (InputConfig->Crouch)		EIC->BindAction(InputConfig->Crouch, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_CrouchToggle);
	if (InputConfig->Lean)			EIC->BindAction(InputConfig->Lean, ETriggerEvent::Triggered, this, &AOneBrahamPlayerController::Input_Lean);
	if (InputConfig->Lean)			EIC->BindAction(InputConfig->Lean, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_Lean);
	if (InputConfig->Mantle)		EIC->BindAction(InputConfig->Mantle, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Mantle);
	if (InputConfig->Slide)			EIC->BindAction(InputConfig->Slide, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Slide);

	// --- Combat -----------------------------------------------------------
	if (InputConfig->Fire)
	{
		EIC->BindAction(InputConfig->Fire, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_FireStarted);
		EIC->BindAction(InputConfig->Fire, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_FireCompleted);
	}
	if (InputConfig->Aim)
	{
		EIC->BindAction(InputConfig->Aim, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_AimStarted);
		EIC->BindAction(InputConfig->Aim, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_AimCompleted);
	}
	if (InputConfig->Reload)		EIC->BindAction(InputConfig->Reload, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Reload);
	if (InputConfig->FireModeSwitch) EIC->BindAction(InputConfig->FireModeSwitch, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_FireModeSwitch);
	if (InputConfig->Melee)			EIC->BindAction(InputConfig->Melee, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Melee);
	if (InputConfig->Inspect)		EIC->BindAction(InputConfig->Inspect, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Inspect);

	// --- Inventory --------------------------------------------------------
	if (InputConfig->WeaponSlotPrimary)	EIC->BindAction(InputConfig->WeaponSlotPrimary, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_WeaponSlotPrimary);
	if (InputConfig->WeaponSlotSidearm)	EIC->BindAction(InputConfig->WeaponSlotSidearm, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_WeaponSlotSidearm);
	if (InputConfig->WeaponSlotMelee)	EIC->BindAction(InputConfig->WeaponSlotMelee, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Melee);
	if (InputConfig->WeaponCycleNext)	EIC->BindAction(InputConfig->WeaponCycleNext, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_WeaponCycleNext);
	if (InputConfig->WeaponCyclePrev)	EIC->BindAction(InputConfig->WeaponCyclePrev, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_WeaponCyclePrev);
	if (InputConfig->ThrowGrenade)		EIC->BindAction(InputConfig->ThrowGrenade, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_ThrowGrenade);
	if (InputConfig->UseUtility)		EIC->BindAction(InputConfig->UseUtility, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_UseUtility);
	if (InputConfig->UseMedkit)			EIC->BindAction(InputConfig->UseMedkit, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_UseMedkit);
	if (InputConfig->DropWeapon)		EIC->BindAction(InputConfig->DropWeapon, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_DropWeapon);

	// --- Interaction ------------------------------------------------------
	if (InputConfig->Interact)		EIC->BindAction(InputConfig->Interact, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Interact);
	if (InputConfig->InteractHold)
	{
		EIC->BindAction(InputConfig->InteractHold, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_InteractHoldStarted);
		EIC->BindAction(InputConfig->InteractHold, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_InteractHoldCompleted);
	}
	if (InputConfig->ToggleFlashlight) EIC->BindAction(InputConfig->ToggleFlashlight, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_ToggleFlashlight);
	if (InputConfig->ToggleLaser)	EIC->BindAction(InputConfig->ToggleLaser, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_ToggleLaser);

	// --- Voice ------------------------------------------------------------
	if (InputConfig->PushToTalk)
	{
		// Push-to-talk needs both Started and Completed, and it must keep
		// working while a menu is open (players talk in the lobby).
		EIC->BindAction(InputConfig->PushToTalk, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_PushToTalkStarted);
		EIC->BindAction(InputConfig->PushToTalk, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_PushToTalkCompleted);
	}
	if (InputConfig->VoiceChannelCycle)	EIC->BindAction(InputConfig->VoiceChannelCycle, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_VoiceChannelCycle);
	if (InputConfig->VoiceMuteSelf)		EIC->BindAction(InputConfig->VoiceMuteSelf, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_VoiceMuteSelf);

	// --- UI ---------------------------------------------------------------
	if (InputConfig->Scoreboard)
	{
		EIC->BindAction(InputConfig->Scoreboard, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_ScoreboardStarted);
		EIC->BindAction(InputConfig->Scoreboard, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_ScoreboardCompleted);
	}
	if (InputConfig->PauseMenu)		EIC->BindAction(InputConfig->PauseMenu, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_PauseMenu);
	if (InputConfig->Ping)			EIC->BindAction(InputConfig->Ping, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_Ping);
	if (InputConfig->SpectateNextPlayer) EIC->BindAction(InputConfig->SpectateNextPlayer, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_SpectateNext);
	if (InputConfig->SpectatePrevPlayer) EIC->BindAction(InputConfig->SpectatePrevPlayer, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_SpectatePrev);
	if (InputConfig->SpectateToggleFreeCam) EIC->BindAction(InputConfig->SpectateToggleFreeCam, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_SpectateFreeCam);
	if (InputConfig->ClipMarker)	EIC->BindAction(InputConfig->ClipMarker, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_ClipMarker);

	// --- Overlays ---------------------------------------------------------
	// Three actions the content pipeline has always generated and nothing ever
	// bound: the HUD toggle, the floor plan and the comms wheel. An Enhanced
	// Input action with no handler is silent in both directions - the key does
	// nothing and nothing says why - so they are bound here like every other
	// action, with the wheel on press-and-release.
	if (InputConfig->ToggleHUD)		EIC->BindAction(InputConfig->ToggleHUD, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_ToggleHUD);
	if (InputConfig->Map)			EIC->BindAction(InputConfig->Map, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_MapToggle);
	if (InputConfig->VoiceLineMenu)
	{
		EIC->BindAction(InputConfig->VoiceLineMenu, ETriggerEvent::Started, this, &AOneBrahamPlayerController::Input_VoiceLineMenuStarted);
		EIC->BindAction(InputConfig->VoiceLineMenu, ETriggerEvent::Completed, this, &AOneBrahamPlayerController::Input_VoiceLineMenuCompleted);
	}
}

void AOneBrahamPlayerController::PushMappingContext(UInputMappingContext* Context, int32 Priority)
{
	if (Context == nullptr)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Context, Priority);
	}
}

void AOneBrahamPlayerController::PopMappingContext(UInputMappingContext* Context)
{
	if (Context == nullptr)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->RemoveMappingContext(Context);
	}
}

// ==========================================================================
//  Tick
// ==========================================================================
void AOneBrahamPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Interaction prompt refresh at 10 Hz: fast enough to feel immediate, cheap
	// enough that 16 players do 160 traces a second instead of 960.
	InteractionTraceAccumulator += DeltaTime;
	if (InteractionTraceAccumulator >= InteractionTraceInterval)
	{
		InteractionTraceAccumulator = 0.f;
		UpdateInteractionTarget();
	}

	// Sensitivity can change from the settings menu at any moment.
	if (const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		const AOneBrahamCharacter* Character = GetOneBrahamCharacter();
		const bool bAiming = Character != nullptr && Character->GetWeaponComponent() != nullptr && Character->GetWeaponComponent()->IsAimingDownSights();
		EffectiveSensitivity = Settings->MouseSensitivityX * (bAiming ? Settings->MouseSensitivityADSMultiplier : 1.f);
	}

	if (bIsSpectating)
	{
		ApplySpectatorView();
	}

	TickFlash(DeltaTime);
}

// ==========================================================================
//  Movement input -> character
// ==========================================================================
void AOneBrahamPlayerController::Input_Move(const FInputActionValue& Value)
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr || bPauseMenuOpen)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	Character->HandleMoveInput(Axis);
}

void AOneBrahamPlayerController::Input_Look(const FInputActionValue& Value)
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr || bPauseMenuOpen)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const float Scale = Settings != nullptr ? EffectiveSensitivity : 0.07f;

	// Mouse: direct 1:1 rotation, no acceleration (bViewAccelerationEnabled=False).
	AddYawInput(Axis.X * Scale * 20.f);
	AddPitchInput((Settings != nullptr && Settings->bInvertMouseY ? Axis.Y : -Axis.Y) * Scale * 20.f);
}

void AOneBrahamPlayerController::Input_LookGamepad(const FInputActionValue& Value)
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr || bPauseMenuOpen)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const float SensX = Settings != nullptr ? Settings->GamepadSensitivityX : 0.07f;
	const float SensY = Settings != nullptr ? Settings->GamepadSensitivityY : 0.07f;
	const bool bInvertY = Settings != nullptr && Settings->bInvertGamepadY;

	// Frame-rate independent: the input exponent curve lives in the IMC (1.7),
	// which gives the "smooth in the middle, fast at the edges" feel players
	// expect from a pad without any extra code here.
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	AddYawInput(Axis.X * SensX * 300.f * DeltaTime);
	AddPitchInput((bInvertY ? Axis.Y : -Axis.Y) * SensY * 220.f * DeltaTime);
}

void AOneBrahamPlayerController::Input_JumpStarted(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->HandleJumpPressed();
	}
}

void AOneBrahamPlayerController::Input_JumpCompleted(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->HandleJumpReleased();
	}
}

void AOneBrahamPlayerController::Input_SprintStarted(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->SetSprintRequested(true);
	}
}

void AOneBrahamPlayerController::Input_SprintCompleted(const FInputActionValue& Value)
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const bool bIsHold = Settings == nullptr || !Settings->bHoldToSprint;

	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		// With "hold to sprint" off, tapping the key latches sprint until the
		// player stops or shoots; with it on, releasing ends the sprint.
		Character->SetSprintRequested(!bIsHold ? Character->IsSprintRequested() : false);
	}
}

void AOneBrahamPlayerController::Input_WalkToggle(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->ToggleWalkState();
	}
}

void AOneBrahamPlayerController::Input_CrouchToggle(const FInputActionValue& Value)
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr)
	{
		return;
	}

	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings != nullptr && Settings->bToggleCrouch)
	{
		Character->ToggleCrouchState();
	}
	else
	{
		Character->SetCrouchRequested(!Character->IsCrouchRequested());
	}
}

void AOneBrahamPlayerController::Input_Lean(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->SetLeanInput(Value.Get<float>());
	}
}

void AOneBrahamPlayerController::Input_Mantle(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->TryStartMantle();
	}
}

void AOneBrahamPlayerController::Input_Slide(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		Character->TryStartSlide();
	}
}

// ==========================================================================
//  Combat input -> weapon component
// ==========================================================================
void AOneBrahamPlayerController::Input_FireStarted(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->SetTriggerHeld(true);
		}
	}
}

void AOneBrahamPlayerController::Input_FireCompleted(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->SetTriggerHeld(false);
		}
	}
}

void AOneBrahamPlayerController::Input_AimStarted(const FInputActionValue& Value)
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr)
	{
		return;
	}

	UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent();
	if (Weapons == nullptr)
	{
		return;
	}

	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const bool bToggle = Settings != nullptr && Settings->bToggleADS;
	Weapons->SetAimingDownSights(bToggle ? !Weapons->IsAimingDownSights() : true);
}

void AOneBrahamPlayerController::Input_AimCompleted(const FInputActionValue& Value)
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Character == nullptr || (Settings != nullptr && Settings->bToggleADS))
	{
		return; // toggle mode keeps the sight up
	}

	if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
	{
		Weapons->SetAimingDownSights(false);
	}
}

void AOneBrahamPlayerController::Input_Reload(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->RequestReload();
		}
	}
}

void AOneBrahamPlayerController::Input_FireModeSwitch(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->CycleFireMode();
		}
	}
}

void AOneBrahamPlayerController::Input_Melee(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->MeleeAttack();
		}
	}
}

void AOneBrahamPlayerController::Input_Inspect(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->PlayInspect();
		}
	}
}

void AOneBrahamPlayerController::Input_WeaponSlotPrimary(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->EquipSlot(EOneBrahamWeaponSlot::Primary);
		}
	}
}

void AOneBrahamPlayerController::Input_WeaponSlotSidearm(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->EquipSlot(EOneBrahamWeaponSlot::Sidearm);
		}
	}
}

void AOneBrahamPlayerController::Input_WeaponCycleNext(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->CycleWeapon(+1);
		}
	}
}

void AOneBrahamPlayerController::Input_WeaponCyclePrev(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->CycleWeapon(-1);
		}
	}
}

void AOneBrahamPlayerController::Input_ThrowGrenade(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->ThrowUtility();
		}
	}
}

void AOneBrahamPlayerController::Input_UseUtility(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->UseUtilityItem();
		}
	}
}

void AOneBrahamPlayerController::Input_UseMedkit(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->UseMedkit();
		}
	}
}

void AOneBrahamPlayerController::Input_DropWeapon(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->DropCurrentWeapon();
		}
	}
}

// ==========================================================================
//  Interaction
// ==========================================================================
void AOneBrahamPlayerController::Input_Interact(const FInputActionValue& Value)
{
	NotifyInteractionStarted();
}

void AOneBrahamPlayerController::Input_InteractHoldStarted(const FInputActionValue& Value)
{
	NotifyInteractionStarted();
}

void AOneBrahamPlayerController::Input_InteractHoldCompleted(const FInputActionValue& Value)
{
	NotifyInteractionReleased();
}

void AOneBrahamPlayerController::Input_ToggleFlashlight(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->ToggleFlashlight();
		}
	}
}

void AOneBrahamPlayerController::Input_ToggleLaser(const FInputActionValue& Value)
{
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->ToggleLaser();
		}
	}
}

namespace
{
	/**
	 * True when an actor is a shoot-through surface: a cubicle wall, a plasterboard
	 * partition. The test is deliberately the same one the weapon trace uses for
	 * penetration (OneBrahamWeaponBase, the `bCanPenetrateCover` block): the root
	 * component's collision object type is the Penetrable channel. Two ways of
	 * deciding what "penetrable" means would eventually disagree, and the day they
	 * did, bullets would go through a wall that a hand could not.
	 */
	bool IsPenetrableSurface(const AActor* Actor)
	{
		return Actor != nullptr && Actor->GetRootComponent() != nullptr &&
			Actor->GetRootComponent()->GetCollisionObjectType() == ECC_GameTraceChannel4; /*Penetrable*/
	}
}

void AOneBrahamPlayerController::UpdateInteractionTarget()
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	const UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;
	const UOneBrahamDownedComponent* Downed = Character != nullptr ? Character->GetDownedComponent() : nullptr;

	// Downed players are the target of an interaction, never the initiator: no
	// crawling through a fire door to escape a fight. `IsAlive()` is not enough to
	// say that - a downed player is at 1 HP and very much alive - so the downed
	// state is asked separately. The interact component makes the same check on
	// the server, because this one runs on the client that presses E.
	const bool bCanInteract = Character != nullptr && Health != nullptr && Health->IsAlive() &&
		(Downed == nullptr || !Downed->IsDowned());

	if (!bCanInteract)
	{
		if (CurrentInteractionTarget.Actor != nullptr)
		{
			CurrentInteractionTarget = FOneBrahamInteractionTarget();
			OnInteractionTargetChanged.Broadcast(CurrentInteractionTarget);
		}
		return;
	}

	FOneBrahamInteractionTarget NewTarget;

	// Trace from the camera so what the player sees is what they interact with.
	const FVector Start = Character->GetEyeLocation();
	const FVector End = Start + Character->GetEyeRotation().Vector() * Character->GetInteractRange();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBInteraction), /*bTraceComplex*/ false, Character);
	Params.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	const bool bHit = GetWorld() != nullptr && GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ECC_GameTraceChannel3 /*Interactable*/, Params);

	// A body behind a cubicle wall is still reachable. The trace is allowed one
	// penetrable surface - the same one bullet budget the weapon system spends on
	// `bCanPenetrateCover` - and then reports whatever it finds beyond it. This is
	// the code that makes "you can be revived through a partition" true, and it is
	// the single best clip moment the game has: a hand reaching through a wall to
	// pull somebody up, with both of them blind to the corridor.
	if (bHit && Hit.GetActor() != nullptr &&
		Cast<IOneBrahamInteractionInterface>(Hit.GetActor()) == nullptr &&
		IsPenetrableSurface(Hit.GetActor()))
	{
		// Only a *surface* is stepped through, never an interactable that happens
		// to be penetrable: if the thing you are looking at already answers to the
		// interface, it is the thing you are interacting with.
		const AActor* Surface = Hit.GetActor();

		FCollisionQueryParams SecondParams = Params;
		SecondParams.AddIgnoredActor(Surface);

		FHitResult SecondHit;
		const FVector SecondStart = Hit.ImpactPoint + Character->GetEyeRotation().Vector() * 2.f;

		if (GetWorld()->LineTraceSingleByChannel(SecondHit, SecondStart, End, ECC_GameTraceChannel3 /*Interactable*/, SecondParams))
		{
			// Hit.Distance is measured from the start of *its own* trace, so the
			// step-through is added back on: Distance means "how far is the thing
			// I am looking at from my eye", for every hit, from every path.
			Hit = SecondHit;
			Hit.Distance += (SecondStart - Start).Size();
		}
	}

	if (Hit.GetActor() != nullptr)
	{
		if (IOneBrahamInteractionInterface* Interface = Cast<IOneBrahamInteractionInterface>(Hit.GetActor()))
		{
			NewTarget.Actor = Hit.GetActor();
			NewTarget.Distance = Hit.Distance;
			NewTarget.Prompt = Interface->GetInteractionPrompt_Implementation(this);
			NewTarget.InteractionTag = Interface->GetInteractionTag_Implementation(this);
			NewTarget.bEnabled = Interface->CanInteract_Implementation(this);
		}
	}

	// Only broadcast on a meaningful change: the HUD widget should not rebuild a
	// prompt every 100 ms while the player stares at the same fire door.
	const bool bChanged =
		NewTarget.Actor != CurrentInteractionTarget.Actor ||
		!NewTarget.Prompt.EqualTo(CurrentInteractionTarget.Prompt) ||
		NewTarget.bEnabled != CurrentInteractionTarget.bEnabled;

	if (IsLocalController() && CurrentInteractionTarget.Actor != NewTarget.Actor)
	{
		// The outline pass: what you are looking at lights up and what you were
		// looking at goes back to normal. Custom depth, not a material instance:
		// the highlight has to be able to outline a MetaHuman body, a fire door and
		// a dropped badge without three different material setups. Stencil 2 is
		// the "interacting with this" value, and M_OB_Highlight is the
		// post-process material that reads it: the graph and its two parameters are
		// in Docs/10 section 2.7, and Tools/Python/onebraham/build_materials.py
		// authors it. Before that material existed, this call was a write into a
		// depth buffer that nothing consumed.
		if (IOneBrahamInteractionInterface* Previous = IsValid(CurrentInteractionTarget.Actor)
			? Cast<IOneBrahamInteractionInterface>(CurrentInteractionTarget.Actor) : nullptr)
		{
			Previous->SetInteractionHighlight_Implementation(false);
		}

		if (IOneBrahamInteractionInterface* Next = IsValid(NewTarget.Actor)
			? Cast<IOneBrahamInteractionInterface>(NewTarget.Actor) : nullptr)
		{
			Next->SetInteractionHighlight_Implementation(true);
		}
	}

	CurrentInteractionTarget = NewTarget;

	if (bChanged)
	{
		OnInteractionTargetChanged.Broadcast(CurrentInteractionTarget);
	}
}

void AOneBrahamPlayerController::NotifyInteractionStarted()
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr || CurrentInteractionTarget.Actor == nullptr)
	{
		return;
	}

	if (UOneBrahamInteractComponent* Interactor = Character->GetInteractComponent())
	{
		Interactor->BeginInteraction(CurrentInteractionTarget.Actor, CurrentInteractionTarget.InteractionTag, /*bIsHold*/ true);
	}
}

void AOneBrahamPlayerController::NotifyInteractionReleased()
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr)
	{
		return;
	}

	if (UOneBrahamInteractComponent* Interactor = Character->GetInteractComponent())
	{
		Interactor->EndInteraction();
	}
}

// ==========================================================================
//  Voice input
// ==========================================================================
void AOneBrahamPlayerController::Input_PushToTalkStarted(const FInputActionValue& Value)
{
	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(this))
	{
		Voice->SetTransmitting(this, true);
	}
}

void AOneBrahamPlayerController::Input_PushToTalkCompleted(const FInputActionValue& Value)
{
	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(this))
	{
		Voice->SetTransmitting(this, false);
	}
}

void AOneBrahamPlayerController::Input_ToggleHUD(const FInputActionValue& Value)
{
	if (HUDComponent == nullptr || !IsLocalController())
	{
		return;
	}

	HUDComponent->SetHUDVisible(!HUDComponent->IsHUDVisible());

	// A banner that says so, because a HUD that vanished because of a mistyped
	// key is indistinguishable from a HUD that broke. Two seconds, no sound.
	if (HUDComponent->IsHUDVisible())
	{
		HUDComponent->ShowSubBanner(
			NSLOCTEXT("OneBraham", "HUDShown", "Interface shown"), 2.f);
	}
}

void AOneBrahamPlayerController::Input_MapToggle(const FInputActionValue& Value)
{
	if (HUDComponent == nullptr || !IsLocalController())
	{
		return;
	}

	// The comms wheel and the plan are never both open: they are both read at a
	// glance, and two overlays competing for that glance is neither.
	if (bQuickLineMenuOpen)
	{
		Input_VoiceLineMenuCompleted(Value);
	}

	HUDComponent->SetFloorPlanVisible(!HUDComponent->IsFloorPlanVisible());
}

void AOneBrahamPlayerController::Input_VoiceLineMenuStarted(const FInputActionValue& Value)
{
	if (HUDComponent == nullptr || !IsLocalController() || bQuickLineMenuOpen)
	{
		return;
	}

	bQuickLineMenuOpen = true;

	// Opening closes the plan for the same reason the plan closes the wheel.
	if (HUDComponent->IsFloorPlanVisible())
	{
		HUDComponent->SetFloorPlanVisible(false);
	}

	HUDComponent->SetQuickLineMenuVisible(true);
}

void AOneBrahamPlayerController::Input_VoiceLineMenuCompleted(const FInputActionValue& Value)
{
	if (!bQuickLineMenuOpen)
	{
		return;
	}

	bQuickLineMenuOpen = false;

	if (HUDComponent != nullptr)
	{
		// Closing the wheel commits whatever is under the cursor. The wheel's own
		// widget decides that, because it is the thing that knows where the mouse
		// is; the component forwards the chosen line back here.
		HUDComponent->SetQuickLineMenuVisible(false);
	}
}

void AOneBrahamPlayerController::RequestQuickLine(FName LineId)
{
	if (LineId.IsNone())
	{
		return;
	}

	// Deliberately no local play and no prediction: a line is the only thing in
	// this game that other players hear, and hearing it before the server agreed
	// it happened would make the voice channel client-authoritative by accident.
	if (HasAuthority())
	{
		ServerRequestQuickLine_Implementation(LineId);
	}
	else
	{
		ServerRequestQuickLine(LineId);
	}
}

void AOneBrahamPlayerController::ServerSetLoadout_Implementation(FName PrimaryWeaponId, FName SidearmWeaponId,
	const TArray<FName>& UtilityIds, FName WeaponSkinId)
{
	AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>();
	if (State == nullptr)
	{
		// Called from InitPlayerState, where the state must already exist; if it
		// somehow does not, the spawn path gives the standard-issue kit and the
		// player is armed anyway.
		return;
	}

	const bool bFirstReceipt = State->StoreLoadout(PrimaryWeaponId, SidearmWeaponId, UtilityIds, WeaponSkinId);
	if (!bFirstReceipt)
	{
		return;
	}

	// The first receipt usually lands after PostLogin has already put this player
	// on the floor - the client only knows its profile once it is in - so the kit
	// it was given a moment ago is the standard issue one. Replace it now rather
	// than making them play a life with the wrong rifle.
	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->ResetOnRespawn();
		}
	}
}

void AOneBrahamPlayerController::ServerRequestQuickLine_Implementation(FName LineId)
{
	if (LineId.IsNone())
	{
		return;
	}

	// The voice subsystem speaks through a character: it resolves the line from
	// the speaker's archetype, plays the sound on the speaker's own component and
	// measures distance from the speaker's head. A controller has none of those,
	// so the pawn is the speaker and the PlayerState is the fallback for the one
	// frame where a spectating player has no pawn at all.
	AActor* Speaker = GetPawn();

	if (Speaker == nullptr)
	{
		Speaker = GetPlayerState<AOneBrahamPlayerState>();
	}

	if (Speaker == nullptr)
	{
		return;
	}

	// Priority comes from the line itself, raised by the subsystem when the line
	// is urgent. Asking for a quick line is a request, not an override.
	UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(this);

	if (Voice == nullptr)
	{
		return;
	}

	const bool bSpoken = Voice->SpeakContextualLine(Speaker, LineId, 0.6f);

	// Refusals are logged at Verbose: a refused "enemy spotted" because the
	// player said it four seconds ago is the system working, and a log file full
	// of the system working is a log file nobody reads.
	UE_LOG(LogOneBrahamVoice, Verbose, TEXT("[OB.VOICE] Quick line '%s' requested by '%s': %s."),
		*LineId.ToString(), *GetName(), bSpoken ? TEXT("said") : TEXT("refused"));
}

void AOneBrahamPlayerController::Input_VoiceChannelCycle(const FInputActionValue& Value)
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings != nullptr && !Settings->bVoiceChannelCycleEnabled)
	{
		return;
	}

	AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>();
	if (State == nullptr)
	{
		return;
	}

	// Proximity -> Squad -> Team -> Proximity. A dead player cannot use proximity
	// (the server enforces this too, see ServerSetVoiceChannel).
	EOneBrahamVoiceChannel Next = EOneBrahamVoiceChannel::Proximity;
	switch (State->VoiceChannel)
	{
	case EOneBrahamVoiceChannel::Proximity:	Next = EOneBrahamVoiceChannel::Squad; break;
	case EOneBrahamVoiceChannel::Squad:		Next = EOneBrahamVoiceChannel::Team; break;
	case EOneBrahamVoiceChannel::Team:		Next = EOneBrahamVoiceChannel::Proximity; break;
	default:								Next = EOneBrahamVoiceChannel::Proximity; break;
	}

	State->ServerSetVoiceChannel(Next);

	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(this))
	{
		Voice->PlayChannelSwitchCue(Next);
	}
}

void AOneBrahamPlayerController::Input_VoiceMuteSelf(const FInputActionValue& Value)
{
	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		State->ServerSetSelfMuted(!State->bSelfMuted);
	}
}

// ==========================================================================
//  UI input
// ==========================================================================
void AOneBrahamPlayerController::Input_ScoreboardStarted(const FInputActionValue& Value)
{
	SetScoreboardVisible(true);
}

void AOneBrahamPlayerController::Input_ScoreboardCompleted(const FInputActionValue& Value)
{
	SetScoreboardVisible(false);
}

void AOneBrahamPlayerController::Input_PauseMenu(const FInputActionValue& Value)
{
	TogglePauseMenu();
}

void AOneBrahamPlayerController::Input_Ping(const FInputActionValue& Value)
{
	PingLookDirection();
}

void AOneBrahamPlayerController::Input_SpectateNext(const FInputActionValue& Value)
{
	SpectateNextPlayer();
}

void AOneBrahamPlayerController::Input_SpectatePrev(const FInputActionValue& Value)
{
	SpectatePreviousPlayer();
}

void AOneBrahamPlayerController::Input_SpectateFreeCam(const FInputActionValue& Value)
{
	ToggleFreeCamera();
}

void AOneBrahamPlayerController::Input_ClipMarker(const FInputActionValue& Value)
{
	MarkClipMoment(TEXT("manual"));
}

// ==========================================================================
//  UI implementation
// ==========================================================================
void AOneBrahamPlayerController::TogglePauseMenu()
{
	bPauseMenuOpen = !bPauseMenuOpen;

	if (bPauseMenuOpen)
	{
		if (PauseMenuWidgetClass != nullptr)
		{
			if (UUserWidget* Widget = CreateWidget<UUserWidget>(this, PauseMenuWidgetClass))
			{
				Widget->AddToViewport(50);
				bShowMouseCursor = true;
				SetInputMode(FInputModeGameAndUI());
			}
		}
		else
		{
			// No widget authored yet: fall back to the engine pause so QA can at
			// least halt the game from a code-only checkout.
			SetPause(true);
		}
	}
	else
	{
		// The pause widget removes itself on close; here we only restore input.
		if (!IsRunningDedicatedServer())
		{
			if (UOneBrahamHUDWidget* HudWidget = Cast<UOneBrahamHUDWidget>(HUDComponent ? HUDComponent->GetRootWidget() : nullptr))
			{
				HudWidget->TogglePauseMenu(false);
			}
		}
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
		SetPause(false);
	}
}

void AOneBrahamPlayerController::SetScoreboardVisible(bool bVisible)
{
	bScoreboardOpen = bVisible;

	if (HUDComponent != nullptr)
	{
		HUDComponent->SetScoreboardVisible(bVisible);
	}
}

void AOneBrahamPlayerController::ClientShowDeathScreen_Implementation(bool bShow, const FText& CauseText)
{
	if (HUDComponent != nullptr)
	{
		HUDComponent->SetDeathScreenVisible(bShow, CauseText);
	}
}

void AOneBrahamPlayerController::ClientShowBanner_Implementation(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	if (HUDComponent != nullptr)
	{
		HUDComponent->ShowBanner(Text, DurationSeconds, Colour);
	}
}

void AOneBrahamPlayerController::ClientDamageTaken_Implementation(float Damage, FVector_NetQuantize InstigatorLocation)
{
	// The screen-edge arc. The HUD converts the world position into a bearing
	// against the character's own facing, so a player who has already turned
	// towards the shooter sees it straight ahead - which is what makes it read as
	// an answer rather than a decoration.
	if (HUDComponent != nullptr)
	{
		HUDComponent->NotifyDamageTaken(Damage, InstigatorLocation);
	}

	// The impact, not the report: this is what tells a player they were hit
	// rather than shot past. Id-based through the audio director, so it obeys the
	// player's mix, their hearing-damage setting and the impact category's limits
	// - and it is the flesh-impact cue, played flat, because it happened to the
	// person listening.
	if (UOneBrahamAudioManagerComponent* Audio = GetAudioManager())
	{
		Audio->PlayLocalUISound(TEXT("SFX_Bullet_Impact_Flesh"));
	}
}

void AOneBrahamPlayerController::HandleSettingsChanged()
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings != nullptr && PlayerCameraManager != nullptr)
	{
		PlayerCameraManager->SetFOV(Settings->FOV);
	}

	// The HUD owns its own view of the settings (element visibility, opacity and
	// scale), so it has to be told. Without this, changing "HUD Scale" in the
	// options menu does nothing until the next level load.
	if (HUDComponent != nullptr)
	{
		HUDComponent->ApplyAccessibilitySettings();
	}
}

// ==========================================================================
//  Effects
// ==========================================================================
void AOneBrahamPlayerController::ClientApplyFlash_Implementation(float DurationSeconds, bool bDirectHit)
{
	ApplyFlashLocally(DurationSeconds, bDirectHit);
}

void AOneBrahamPlayerController::ApplyFlashLocally(float DurationSeconds, bool bDirectHit)
{
	if (DurationSeconds <= 0.f)
	{
		return;
	}

	// Tinnitus and the white-out are the whole point of a flashbang: for the next
	// few seconds the player is functionally blind and deaf, which is terrifying
	// in a server room with three people in it.
	FlashRemaining = FMath::Max(FlashRemaining, DurationSeconds * (bDirectHit ? 1.f : 0.7f));
	FlashStrength01 = FMath::Clamp(DurationSeconds / 5.f, 0.f, 1.f);

	// Local audio: ringing, muffled world for the duration.
	if (UOneBrahamAudioManagerComponent* Audio = GetAudioManager())
	{
		Audio->SetTinnitusState(true, FlashRemaining);
		Audio->PlayLocalUISound(TEXT("SFX_Flashbang_Ring"));
	}
}

void AOneBrahamPlayerController::ClientApplyExplosionShake_Implementation(FVector_NetQuantize Origin, float Magnitude)
{
	ApplyExplosionShakeLocally(Origin, Magnitude);
}

void AOneBrahamPlayerController::ApplyExplosionShakeLocally(const FVector& Origin, float Magnitude)
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();

	if (Settings == nullptr || !Settings->bCameraShake)
	{
		return; // accessibility: screen shake can be switched off entirely
	}

	APawn* Pawn = GetPawn();
	if (Pawn == nullptr || PlayerCameraManager == nullptr)
	{
		return;
	}

	// Distance falloff over 40 m, then the user's intensity slider. A shake from
	// the far end of the open-plan office should be a nudge, not a lurch.
	const float Distance = FVector::Dist(Pawn->GetActorLocation(), Origin);
	const float Falloff = FMath::Clamp(1.f - (Distance / 4000.f), 0.f, 1.f);

	if (Falloff <= 0.f)
	{
		return;
	}

	PlayerCameraManager->StartCameraShake(UOneBrahamCameraShake::GetExplosionShakeClass(),
		Falloff * Magnitude * Settings->CameraShakeIntensity);
}

void AOneBrahamPlayerController::TickFlash(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Resolve the global parameter collection once and keep it: this runs every
	// frame while blinded and a synchronous load in Tick would hitch the game.
	if (!CachedFlashCollection.IsValid())
	{
		CachedFlashCollection = LoadObject<UMaterialParameterCollection>(nullptr,
			TEXT("/Game/OneBraham/Materials/MPC_OneBraham_Global.MPC_OneBraham_Global"));
	}

	if (FlashRemaining > 0.f)
	{
		FlashRemaining = FMath::Max(0.f, FlashRemaining - DeltaTime);
	}

	if (!CachedFlashCollection.IsValid())
	{
		return;
	}

	if (FlashRemaining > 0.f)
	{
		// Peak white-out for the first moment, then a two-stage recovery: a fast
		// initial return of vision followed by a long low-contrast smear. That is
		// how flashbang blindness actually presents.
		const float Elapsed = FlashStrength01 * 5.f - FlashRemaining;
		const float Peak = FMath::Clamp(1.f - Elapsed * 1.6f, 0.f, 1.f);
		const float Tail = FMath::Pow(FMath::Clamp(FlashRemaining / 5.f, 0.f, 1.f), 0.6f);
		const float Alpha = FMath::Clamp(FMath::Max(Peak, Tail) * FlashStrength01, 0.f, 1.f);

		UKismetMaterialLibrary::SetScalarParameterValue(World, CachedFlashCollection.Get(), TEXT("FlashAlpha"), Alpha);
	}
	else if (bFlashActiveLastFrame)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, CachedFlashCollection.Get(), TEXT("FlashAlpha"), 0.f);

		if (UOneBrahamAudioManagerComponent* Audio = GetAudioManager())
		{
			Audio->SetTinnitusState(false, 0.f);
		}
	}

	bFlashActiveLastFrame = FlashRemaining > 0.f;
}

// ==========================================================================
//  Spectating
// ==========================================================================
void AOneBrahamPlayerController::OnRep_Spectating()
{
	if (bIsSpectating)
	{
		ApplySpectatorView();
	}
}

void AOneBrahamPlayerController::ApplySpectatorView()
{
	// The server tells us who to watch; the client resolves the pawn and sets it
	// as the view target. If that player dies we fall through to the next one.
	AOneBrahamGameState* GS = GetWorld() ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	AOneBrahamCharacter* Target = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOneBrahamPlayerController* OtherPC = Cast<AOneBrahamPlayerController>(It->Get()))
		{
			if (OtherPC != this && OtherPC->SpectatedPlayerId == SpectatedPlayerId)
			{
				Target = OtherPC->GetOneBrahamCharacter();
				break;
			}
		}
	}

	if (Target == nullptr)
	{
		// Fall back to any living pawn matching the id via PlayerState lookup.
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AOneBrahamPlayerController* OtherPC = Cast<AOneBrahamPlayerController>(It->Get());
			if (OtherPC == nullptr || OtherPC == this)
			{
				continue;
			}

			if (AOneBrahamPlayerState* OtherState = OtherPC->GetPlayerState<AOneBrahamPlayerState>())
			{
				if (OtherState->GetPlayerId() == SpectatedPlayerId && !OtherState->IsEliminated())
				{
					Target = OtherPC->GetOneBrahamCharacter();
					break;
				}
			}
		}
	}

	if (Target != nullptr && Target != GetViewTarget())
	{
		SetViewTargetWithBlend(Target, 0.35f, EViewTargetBlendFunction::VTBlend_Cubic);
		OnSpectatorTargetChanged.Broadcast(Target);

		// Spectators follow the killer in first person by default.
		if (bFreeCamActive)
		{
			SetViewTargetWithBlend(Target, 0.0f);
		}
	}
	else if (Target == nullptr && bFreeCamActive == false)
	{
		// Nobody valid to watch: hand control back to a free camera so the player
		// is never stuck on a black screen.
		bFreeCamActive = true;
	}
}

void AOneBrahamPlayerController::SpectateNextPlayer()
{
	if (!bIsSpectating)
	{
		return;
	}

	ServerBeginSpectating(INDEX_NONE); // INDEX_NONE = "next in round-robin order"
}

void AOneBrahamPlayerController::SpectatePreviousPlayer()
{
	if (!bIsSpectating)
	{
		return;
	}

	ServerBeginSpectating(-1); // -1 = previous
}

void AOneBrahamPlayerController::SpectatePlayer(int32 PlayerId)
{
	if (!bIsSpectating)
	{
		return;
	}

	ServerBeginSpectating(PlayerId);
}

void AOneBrahamPlayerController::ToggleFreeCamera()
{
	if (!bIsSpectating)
	{
		return;
	}

	bFreeCamActive = !bFreeCamActive;

	if (AOneBrahamCharacter* Character = GetOneBrahamCharacter())
	{
		// Free cam is implemented as a detached spectator pawn so the collision
		// and pawn-based interaction code paths are reused rather than special-cased.
		Character->SetActorHiddenInGame(bFreeCamActive);
	}

	OB_LOG("Free camera %s.", bFreeCamActive ? TEXT("enabled") : TEXT("disabled"));
}

void AOneBrahamPlayerController::ServerBeginSpectating_Implementation(int32 PreferredTargetPlayerId)
{
	AOneBrahamGameState* GS = GetWorld() ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	TArray<int32> Candidates;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* OtherPC = Cast<AOneBrahamPlayerController>(It->Get());
		if (OtherPC == nullptr || OtherPC == this)
		{
			continue;
		}

		if (AOneBrahamPlayerState* OtherState = OtherPC->GetPlayerState<AOneBrahamPlayerState>())
		{
			if (OtherState->LifeState == EOneBrahamLifeState::Alive || OtherState->LifeState == EOneBrahamLifeState::Downed)
			{
				Candidates.Add(OtherState->GetPlayerId());
			}
		}
	}

	if (Candidates.Num() == 0)
	{
		// Everyone is dead: watch the Skydeck cameras. -2 is the "no survivors"
		// sentinel the spectator HUD renders as a locked-off security feed.
		bIsSpectating = true;
		SpectatedPlayerId = -2;
		ForceNetUpdate();
		return;
	}

	// Sort for deterministic round-robin cycling.
	Candidates.Sort();

	int32 NewTarget = INDEX_NONE;
	if (PreferredTargetPlayerId > 0 && Candidates.Contains(PreferredTargetPlayerId))
	{
		NewTarget = PreferredTargetPlayerId;
	}
	else if (PreferredTargetPlayerId == -1)
	{
		// Previous
		const int32 CurrentIndex = Candidates.IndexOfByKey(SpectatedPlayerId);
		NewTarget = Candidates.IsValidIndex(CurrentIndex - 1) ? Candidates[CurrentIndex - 1] : Candidates.Last();
	}
	else
	{
		// Next (default) - wraps around.
		const int32 CurrentIndex = Candidates.IndexOfByKey(SpectatedPlayerId);
		NewTarget = Candidates.IsValidIndex(CurrentIndex + 1) ? Candidates[CurrentIndex + 1] : Candidates[0];
	}

	bIsSpectating = true;
	SpectatedPlayerId = NewTarget;
	ForceNetUpdate();
}

void AOneBrahamPlayerController::PingLookDirection()
{
	AOneBrahamCharacter* Character = GetOneBrahamCharacter();
	if (Character == nullptr)
	{
		return;
	}

	// Ping lands on whatever the crosshair is over, up to 60 m; failing that it
	// lands 12 m in front of the player so it is always useful.
	const FVector Start = Character->GetEyeLocation();
	const FVector End = Start + Character->GetEyeRotation().Vector() * 6000.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBPing), false, Character);
	FHitResult Hit;
	FVector PingLocation = Start + Character->GetEyeRotation().Vector() * 1200.f;

	if (GetWorld() != nullptr && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		PingLocation = Hit.ImpactPoint;
	}

	ServerPingLocation(PingLocation);
}

void AOneBrahamPlayerController::ServerPingLocation_Implementation(FVector_NetQuantize Location)
{
	// Sanity: a ping must be within the tower envelope (roughly 120 m around the
	// building origin, 0-100 m up). Anything else is a forged ping and is dropped.
	const FVector Ping = Location;
	if (!Ping.IsNearlyZero() &&
		(Ping.Z < -500.f || Ping.Z > 15000.f || Ping.SizeSquared2D() > FMath::Square(30000.f)))
	{
		ReportViolation(TEXT("Ping outside tower envelope"), 0.5f);
		return;
	}

	if (AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>())
	{
		EOneBrahamTeam PingTeam = State->Team;

		// Only teammates see the ping: enemies hearing a stray "over there" is a
		// gameplay leak, not a feature.
		MulticastPingLocation(Ping, PingTeam, State->GetPlayerName());
	}
}

void AOneBrahamPlayerController::MulticastPingLocation_Implementation(FVector_NetQuantize Location, EOneBrahamTeam PingingTeam, const FString& PingingName)
{
	AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>();
	if (State == nullptr)
	{
		return;
	}

	const UOneBrahamGameSettings& GameSettings = UOneBrahamGameSettings::Get();
	const EOneBrahamMatchMode Mode = GetWorld() && GetWorld()->GetGameState<AOneBrahamGameState>()
		? GetWorld()->GetGameState<AOneBrahamGameState>()->MatchMode
		: GameSettings.DefaultMode;

	if (!OneBrahamTags::AreTeamsFriendly(State->Team, PingingTeam, Mode))
	{
		return;
	}

	if (HUDComponent != nullptr)
	{
		HUDComponent->AddWorldPing(Location, PingingTeam == State->Team ? PingingName : FString(), static_cast<int32>(PingingTeam));
	}
}

// ==========================================================================
//  Clip markers
// ==========================================================================
void AOneBrahamPlayerController::MarkClipMoment(const FString& Label)
{
	if (!IsLocalController())
	{
		return;
	}

	// One CSV per launch: the clip pipeline (Docs/19) ingests it and cuts the
	// highlights out of the ShadowPlay/Steam-recording output.
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Clips");
	const FString FilePath = Directory / TEXT("markers.csv");

	IPlatformFile& FileSystem = FPlatformFileManager::Get().GetPlatformFile();
	FileSystem.CreateDirectoryTree(*Directory);

	FString Existing;
	FFileHelper::LoadFileToString(Existing, *FilePath);

	if (Existing.IsEmpty())
	{
		Existing = TEXT("WallClock,MatchTimeSeconds,Player,Label,Floor,LocationX,LocationY,LocationZ\n");
	}

	const FString PlayerName = PlayerState ? PlayerState->GetPlayerName() : TEXT("Unknown");
	const float MatchTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const int32 Floor = GetOneBrahamCharacter() ? GetOneBrahamCharacter()->GetCurrentFloorIndex() : 0;
	const FVector Location = GetOneBrahamCharacter() ? GetOneBrahamCharacter()->GetActorLocation() : FVector::ZeroVector;

	Existing += FString::Printf(TEXT("%s,%.2f,%s,%s,%d,%.1f,%.1f,%.1f\n"),
		*FDateTime::UtcNow().ToString(TEXT("%Y-%m-%d.%H:%M:%S")),
		MatchTime,
		*PlayerName,
		*Label,
		Floor,
		Location.X, Location.Y, Location.Z);

	FFileHelper::SaveStringToFile(Existing, *FilePath);

	if (HUDComponent != nullptr)
	{
		HUDComponent->ShowBanner(NSLOCTEXT("OneBraham", "ClipMarked", "CLIP MARKED"), 1.5f, FLinearColor(1.f, 0.8f, 0.f, 1.f));
	}
}

// ==========================================================================
//  Anti-cheat: RPC budget
// ==========================================================================
bool AOneBrahamPlayerController::CheckRPCBudget(const TCHAR* RPCLabel)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();

	// Drop everything older than the window (cheap: the array is small and
	// bounded by the budget itself).
	while (RPCWindowTimestamps.Num() > 0 && Now - RPCWindowTimestamps[0] > RPCWindowSeconds)
	{
		RPCWindowTimestamps.RemoveAt(0, 1, EAllowShrinking::No);
	}

	const UOneBrahamGameSettings& GameSettings = UOneBrahamGameSettings::Get();
	const int32 Budget = FMath::Max(30, GameSettings.RpcBudgetPerSecond);

	if (RPCWindowTimestamps.Num() >= Budget)
	{
		// Over budget: reject without executing. Note that this is a coarse
		// per-controller budget on purpose; per-RPC limiting happens in the
		// individual Server_* handlers on top of this.
		OB_AUDIT_LOG("RPC budget exceeded by '%s' (%d in %.1fs) on RPC '%s'. Action dropped.",
			*GetName(), RPCWindowTimestamps.Num(), RPCWindowSeconds, RPCLabel ? RPCLabel : TEXT("Unknown"));

		ReportViolation(FString::Printf(TEXT("RPC flood: %s"), RPCLabel ? RPCLabel : TEXT("Unknown")), 2.f);
		return false;
	}

	RPCWindowTimestamps.Add(Now);
	return true;
}

void AOneBrahamPlayerController::ReportViolation(const FString& Reason, float Severity)
{
	if (!HasAuthority())
	{
		return;
	}

	const UOneBrahamGameSettings& GameSettings = UOneBrahamGameSettings::Get();
	ViolationScore += FMath::Max(1, FMath::RoundToInt(Severity * 5.f));

	if (GameSettings.bLogSuspiciousActions)
	{
		const AOneBrahamPlayerState* State = GetPlayerState<AOneBrahamPlayerState>();
		OB_AUDIT_LOG("SECURITY: player='%s' id=%d reason='%s' severity=%.1f score=%d",
			State ? *State->GetPlayerName() : *GetName(),
			State ? State->GetPlayerId() : -1,
			*Reason,
			Severity,
			ViolationScore);
	}

	if (GameSettings.bKickOnRepeatedViolations && ViolationScore >= GameSettings.ViolationsBeforeKick)
	{
		OB_AUDIT_LOG("SECURITY: kicking '%s' after violation score reached %d.", *GetName(), ViolationScore);
		ClientReturnToMainMenuWithTextReason(NSLOCTEXT("OneBraham", "Kicked", "Disconnected by the server."));
	}
}
