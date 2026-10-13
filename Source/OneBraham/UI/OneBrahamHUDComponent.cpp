// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamHUDComponent.h"

#include "UI/OneBrahamHUDStringTable.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Interaction/OneBrahamInteractComponent.h"
#include "UI/OneBrahamFloorPlanWidget.h"
#include "UI/OneBrahamHUDWidget.h"
#include "UI/OneBrahamQuickLineWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerState.h"

namespace
{
	/** How often the HUD re-gathers continuous state. */
	constexpr float GatherIntervalSeconds = 1.f / 15.f;

	/** Feedback durations. Short is professional: long flashes read as amateur. */
	constexpr float HitMarkerSeconds = 0.22f;
	constexpr float DamageIndicatorSeconds = 1.35f;
	constexpr float KillFeedLifetimeSeconds = 7.f;
	constexpr float SubtitleMaxSeconds = 6.f;
}

UOneBrahamHUDComponent::UOneBrahamHUDComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = GatherIntervalSeconds;
}

void UOneBrahamHUDComponent::BeginPlay()
{
	Super::BeginPlay();

	// A dedicated server has no local player and therefore no HUD. Skipping the
	// whole component there saves the gather tick on every bot and player.
	bHUDEnabled = GetWorld() != nullptr && !IsRunningDedicatedServer();

	if (!bHUDEnabled)
	{
		SetComponentTickEnabled(false);
		return;
	}

	ReadUserSettings();
}

void UOneBrahamHUDComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundToGameState)
	{
		if (UWorld* World = GetWorld())
		{
			if (AOneBrahamGameState* GS = World->GetGameState<AOneBrahamGameState>())
			{
				GS->OnAnnouncement.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleServerAnnouncement);
				GS->OnWarningBanner.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleWarningBanner);
			}
		}

		bBoundToGameState = false;
	}

	UnbindFromPawn();
	DestroyHUD();

	Super::EndPlay(EndPlayReason);
}

// ==========================================================================
//  Lifecycle
// ==========================================================================
void UOneBrahamHUDComponent::CreateHUD(AOneBrahamPlayerController* InOwningController)
{
	OwningController = InOwningController;

	if (!bHUDEnabled || HUDWidget != nullptr || InOwningController == nullptr)
	{
		return;
	}

	// Prefer the authored Blueprint (BP_OneBrahamHUD) so a designer owns the
	// layout; the C++ class builds an equivalent HUD from scratch when the
	// Blueprint is absent, which is what keeps a clean checkout playable.
	if (HUDWidgetClass == nullptr)
	{
		HUDWidgetClass = LoadClass<UOneBrahamHUDWidget>(nullptr,
			TEXT("/Game/OneBraham/UI/HUD/WBP_OneBrahamHUD.WBP_OneBrahamHUD_C"));

		if (HUDWidgetClass == nullptr)
		{
			HUDWidgetClass = UOneBrahamHUDWidget::StaticClass();
		}
	}

	HUDWidget = CreateWidget<UOneBrahamHUDWidget>(InOwningController, HUDWidgetClass);

	if (HUDWidget == nullptr)
	{
		UE_LOG(LogOneBrahamUI, Warning, TEXT("[OB.UI] Failed to create the HUD widget."));
		return;
	}

	HUDWidget->AddToPlayerScreen(0);

	// Apply the player's HUD scale and element switches before the first frame is
	// drawn, so the HUD is never seen at the wrong size and then resized.
	ApplyAccessibilitySettings();

	// The widget pulls its first snapshot immediately, so there is never a frame
	// of default values on screen after a respawn or a level load.
	HUDWidget->ApplyHUDData(Data);

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD created for '%s' using %s."),
		*InOwningController->GetName(),
		HUDWidgetClass->GetName().Contains(TEXT("WBP")) ? TEXT("the authored Blueprint") : TEXT("the C++ fallback"));
}

void UOneBrahamHUDComponent::DestroyHUD()
{
	if (HUDWidget != nullptr)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
}

UUserWidget* UOneBrahamHUDComponent::GetRootWidget() const
{
	return HUDWidget;
}

void UOneBrahamHUDComponent::BindToPawn(APawn* InPawn)
{
	UnbindFromPawn();

	AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(InPawn);
	if (Character == nullptr)
	{
		return;
	}

	BoundCharacter = Character;
	CacheComponents();

	// --- Delegate subscriptions -------------------------------------------
	// Everything transient is event driven: the gather tick is far too slow to
	// catch a hit marker or a damage flash.
	if (UOneBrahamHealthComponent* Health = CachedHealth.Get())
	{
		Health->OnHealthChanged.AddDynamic(this, &UOneBrahamHUDComponent::HandleHealthChanged);
		Health->OnArmourChanged.AddDynamic(this, &UOneBrahamHUDComponent::HandleArmourChanged);
	}

	if (UOneBrahamInventoryComponent* Inventory = CachedInventory.Get())
	{
		Inventory->OnInventoryChanged.AddDynamic(this, &UOneBrahamHUDComponent::HandleInventoryChanged);
	}

	if (UOneBrahamWeaponComponent* Weapons = CachedWeapons.Get())
	{
		Weapons->OnHitConfirmed.AddDynamic(this, &UOneBrahamHUDComponent::HandleHitConfirmed);
	}

	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(Character))
	{
		Voice->OnVoiceLineSpoken.AddDynamic(this, &UOneBrahamHUDComponent::HandleVoiceLineSpoken);
	}

	if (UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
	{
		Downed->OnDownedStateChanged.AddDynamic(this, &UOneBrahamHUDComponent::NotifyDownedStateChanged);
	}

	bIsBound = true;

	// First snapshot right now so the HUD is correct on the spawn frame.
	GatherHUDData();
}

void UOneBrahamHUDComponent::UnbindFromPawn()
{
	if (!bIsBound)
	{
		return;
	}

	if (UOneBrahamHealthComponent* Health = CachedHealth.Get())
	{
		Health->OnHealthChanged.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleHealthChanged);
		Health->OnArmourChanged.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleArmourChanged);
	}

	if (UOneBrahamInventoryComponent* Inventory = CachedInventory.Get())
	{
		Inventory->OnInventoryChanged.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleInventoryChanged);
	}

	if (UOneBrahamWeaponComponent* Weapons = CachedWeapons.Get())
	{
		Weapons->OnHitConfirmed.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleHitConfirmed);
	}

	if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(GetOwner()))
	{
		Voice->OnVoiceLineSpoken.RemoveDynamic(this, &UOneBrahamHUDComponent::HandleVoiceLineSpoken);
	}

	if (AOneBrahamCharacter* Character = BoundCharacter.Get())
	{
		if (UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
		{
			Downed->OnDownedStateChanged.RemoveDynamic(this, &UOneBrahamHUDComponent::NotifyDownedStateChanged);
		}
	}

	CachedHealth = nullptr;
	CachedWeapons = nullptr;
	CachedInventory = nullptr;
	CachedInteraction = nullptr;
	BoundCharacter = nullptr;
	bIsBound = false;
}

void UOneBrahamHUDComponent::CacheComponents()
{
	AOneBrahamCharacter* Character = BoundCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	CachedHealth = Character->GetHealthComponent();
	CachedWeapons = Character->GetWeaponComponent();
	CachedInventory = Character->GetInventoryComponent();
	CachedInteraction = Character->GetInteractComponent();
}

// ==========================================================================
//  Tick / gather
// ==========================================================================
void UOneBrahamHUDComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHUDEnabled || HUDWidget == nullptr)
	{
		return;
	}

	BindToGameState();
	ReadUserSettings();
	GatherHUDData();

	// --- Transient feedback decay -----------------------------------------
	// Decayed here rather than in the widget so the value is authoritative for
	// anyone reading the snapshot (tests included).
	HitMarkerRemaining = FMath::Max(0.f, HitMarkerRemaining - DeltaTime);
	DamageIndicatorRemaining = FMath::Max(0.f, DamageIndicatorRemaining - DeltaTime);
	BannerRemaining = FMath::Max(0.f, BannerRemaining - DeltaTime);
	SubBannerRemaining = FMath::Max(0.f, SubBannerRemaining - DeltaTime);
	SubtitleRemaining = FMath::Max(0.f, SubtitleRemaining - DeltaTime);

	// --- Push to the widget ------------------------------------------------
	Data.HitMarkerAlpha = HitMarkerRemaining / HitMarkerSeconds;
	Data.bShowHitMarker = HitMarkerRemaining > 0.f;
	Data.bLastHitWasHeadshot = bLastHitHeadshot;
	Data.bLastHitWasKill = bLastHitWasKill;
	Data.DamageIndicatorAlpha = DamageIndicatorRemaining / DamageIndicatorSeconds;
	Data.BannerRemaining = BannerRemaining;
	Data.SubBannerText = SubBannerRemaining > 0.f ? Data.SubBannerText : FText::GetEmpty();
	Data.SubtitleRemaining = SubtitleRemaining;

	if (SubtitleRemaining <= 0.f)
	{
		Data.SubtitleText = FText::GetEmpty();
	}

	// Low health pulse: 0.35 Hz, and only below 30%. A permanently pulsing HUD is
	// the fastest way to make a tense game feel cheap.
	const float HealthFraction = Data.MaxHealth > 0.f ? Data.Health / Data.MaxHealth : 1.f;
	Data.LowHealthPulseAlpha = HealthFraction < 0.3f && Data.LifeState == EOneBrahamLifeState::Alive
		? (0.55f + 0.45f * FMath::Sin(GetWorld()->GetTimeSeconds() * 2.2f))
		: 0.f;

	// Prune the kill feed on the same cadence as everything else.
	KillFeedPruneTimer += DeltaTime;
	if (KillFeedPruneTimer >= 0.5f)
	{
		KillFeedPruneTimer = 0.f;

		const float Now = GetWorld()->GetTimeSeconds();
		const int32 Removed = Data.KillFeed.RemoveAll([Now](const FOneBrahamHUDKillFeedEntry& Entry)
		{
			return (Now - Entry.EventTime) > KillFeedLifetimeSeconds;
		});

		if (Removed > 0)
		{
			OnKillFeedUpdated.Broadcast(Data.KillFeed);
		}
	}

	// World pings expire on the same clock.
	const float Now = GetWorld()->GetTimeSeconds();
	Data.WorldPings.RemoveAll([Now](const FOneBrahamHUDWorldPing& Ping)
	{
		return (Now - Ping.SpawnTime) > Ping.Lifetime;
	});

	// Announcements run after the snapshot is gathered, so they read the same
	// state the widget is about to draw.
	AnnounceStateChanges();

	HUDWidget->ApplyHUDData(Data);
}

void UOneBrahamHUDComponent::Announce(FName Key, float DurationSeconds, FLinearColor Colour)
{
	ShowBanner(OneBrahamHUDStrings::Get(Key), DurationSeconds, Colour);
}

void UOneBrahamHUDComponent::AnnounceSub(FName Key, float DurationSeconds)
{
	ShowSubBanner(OneBrahamHUDStrings::Get(Key), DurationSeconds);
}

void UOneBrahamHUDComponent::BindToGameState()
{
	if (bBoundToGameState)
	{
		return;
	}

	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	if (GS == nullptr)
	{
		return;
	}

	// The building's two voices: the tannoy paragraph, read as a subtitle while
	// the audio plays, and the short imperative banner. The client does not
	// replicate these itself; the game state multicasts them.
	GS->OnAnnouncement.AddDynamic(this, &UOneBrahamHUDComponent::HandleServerAnnouncement);
	GS->OnWarningBanner.AddDynamic(this, &UOneBrahamHUDComponent::HandleWarningBanner);
	bBoundToGameState = true;
}

void UOneBrahamHUDComponent::HandleServerAnnouncement(const FText& Announcement)
{
	if (Announcement.IsEmpty())
	{
		return;
	}

	// A tannoy sentence is four or five seconds of reading. Six is generous
	// without being a permanent fixture on screen.
	Data.SubtitleText = Announcement;
	SubtitleRemaining = TannoySubtitleSeconds;
}

void UOneBrahamHUDComponent::HandleWarningBanner(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	if (Text.IsEmpty())
	{
		return;
	}

	ShowBanner(Text, DurationSeconds, Colour);
}

void UOneBrahamHUDComponent::AnnounceStateChanges()
{
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	if (GS == nullptr)
	{
		return;
	}

	// The palette, not four floats: anything the whole building hears is the
	// emergency red, and the winch is the signal blue. Same pair the rest of
	// the HUD uses for the same two meanings.
	const FLinearColor AlarmRed = UOneBrahamUIStyle::EmergencyRed();
	const FLinearColor WinchBlue = UOneBrahamUIStyle::SignalBlue();

	// --- the winch: five states, and before this the HUD drew a bar ---------
	if (const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent())
	{
		const EOneBrahamExtractionState State = Extraction->GetExtractionState();

		if (!bAnnouncementsPrimed)
		{
			LastExtractionState = State;
		}
		else if (State != LastExtractionState)
		{
			switch (State)
			{
			case EOneBrahamExtractionState::Broadcast:
				// The beacon is lit and the whole building knows. This is the
				// moment the match turns, so it gets the full banner.
				Announce(TEXT("HUD_Objective_Winch"), 4.f, WinchBlue);
				break;

			case EOneBrahamExtractionState::Holding:
				Announce(TEXT("HUD_Extraction_Hold"), 4.f, WinchBlue);
				break;

			case EOneBrahamExtractionState::Complete:
				Announce(TEXT("HUD_Match_Extracted"), 5.f, WinchBlue);
				break;

			case EOneBrahamExtractionState::Failed:
			{
				// Contested is worth its own word: it is the one failure the
				// losing squad caused themselves.
				const bool bContested =
					Extraction->GetLastFailReason() == EOneBrahamExtractionFailReason::Contested;

				if (bContested)
				{
					Announce(TEXT("HUD_Extraction_Contested"), 4.f, AlarmRed);
				}
				else
				{
					Announce(TEXT("HUD_Extraction_Failed"), 4.f, AlarmRed);
				}
				break;
			}

			case EOneBrahamExtractionState::Inactive:
			case EOneBrahamExtractionState::Arriving:
			default:
				break;
			}

			LastExtractionState = State;
		}
	}

	// --- the shutters: the tannoy does the talking, this rebuilds the plan --
	if (const UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
	{
		const EOneBrahamLockdownStage Stage = Lockdown->GetStage();

		if (!bAnnouncementsPrimed)
		{
			LastLockdownStage = Stage;
		}
		else if (Stage != LastLockdownStage)
		{
			LastLockdownStage = Stage;

			// The floor plan dims the floors the shutters have taken, so it is
			// stale the moment the stage moves.
			RefreshOverlays();
		}
	}

	// --- a squadmate going down --------------------------------------------
	int32 DownedSquadmates = 0;

	for (const FOneBrahamHUDSquadMember& Row : Data.Squad)
	{
		if (Row.LifeState == EOneBrahamLifeState::Downed)
		{
			++DownedSquadmates;
		}
	}

	if (bAnnouncementsPrimed && DownedSquadmates > LastDownedSquadmates)
	{
		AnnounceSub(TEXT("HUD_Squad_Downed"), 3.f);
		RefreshOverlays();
	}

	LastDownedSquadmates = DownedSquadmates;
	bAnnouncementsPrimed = true;
}

void UOneBrahamHUDComponent::RefreshNow()
{
	GatherHUDData();
}

void UOneBrahamHUDComponent::ReadUserSettings()
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (Settings == nullptr)
	{
		return;
	}

	// Accessibility: every one of these is a real togglable option, because a HUD
	// that cannot be simplified is a HUD that excludes players.
	Data.bShowCrosshair = Settings->bShowCrosshair;
	Data.bShowKillFeed = Settings->bShowKillFeed;
	Data.bShowCompass = Settings->bShowCompass;
	Data.bShowSquadPanel = Settings->bShowTeamPanel;
	Data.bShowAmmoCount = Settings->bShowAmmoCounter;
	Data.bShowSubtitles = Settings->bSubtitlesEnabled;
	Data.bShowDamageDirection = Settings->bShowDamageDirection;
	Data.bShowHitMarkers = Settings->bShowHitMarkers;
	Data.bShowObjectiveMarkers = Settings->bShowObjectiveMarkers;
	Data.bShowFloorIndicator = Settings->bShowFloorIndicator;
	Data.bShowInteractionPrompt = Settings->bShowInteractionPrompts;
	Data.bShowVoiceIndicator = Settings->bShowVoiceIndicators;
	Data.HUDOpacity = Settings->HUDOpacity;
}

void UOneBrahamHUDComponent::ApplyAccessibilitySettings()
{
	UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();

	// Visibility switches are re-read continuously (ReadUserSettings is called
	// from the tick), because a player toggles them while looking at the thing
	// they are toggling. Scale is different: it forces a layout pass, so it is
	// pushed only when the settings actually change.
	ReadUserSettings();

	if (Settings == nullptr || HUDWidget == nullptr)
	{
		return;
	}

	const float Scale = FMath::Clamp(Settings->HUDScale, 0.75f, 1.5f);

	// A render-scale change invalidates the layout of every child, so it is only
	// pushed when the number has actually moved. This function is called from the
	// options menu's apply path, and "apply" is a button people press repeatedly.
	if (!FMath::IsNearlyEqual(CachedHUDScale, Scale, 0.001f))
	{
		// Scaling about the centre keeps the crosshair under the player's eye line;
		// scaling about the default top-left origin walks the whole HUD off-screen
		// at 1.5x, which is the classic broken "HUD scaling" implementation.
		HUDWidget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		HUDWidget->SetRenderScale(FVector2D(Scale, Scale));

		CachedHUDScale = Scale;
	}
}

void UOneBrahamHUDComponent::GatherHUDData()
{
	AOneBrahamCharacter* Character = BoundCharacter.Get();
	AOneBrahamPlayerController* PC = OwningController.Get();
	if (Character == nullptr || PC == nullptr)
	{
		return;
	}

	AOneBrahamPlayerState* State = PC->GetPlayerState<AOneBrahamPlayerState>();

	// --- Player -----------------------------------------------------------
	if (UOneBrahamHealthComponent* Health = CachedHealth.Get())
	{
		Data.Health = Health->GetHealth();
		Data.MaxHealth = Health->GetMaxHealth();
		Data.Armour = Health->GetArmour();
	}

	if (State != nullptr)
	{
		// Armour ceiling is a match setting, not a player stat: 50 for ASCENT and
		// FIRE DRILL, 0 for NIGHT SHIFT (no armour on the survival ruleset).
		Data.MaxArmour = FMath::Max(1.f, Data.MaxArmour);
		Data.LifeState = State->LifeState;
		Data.VoiceChannel = State->VoiceChannel;
		Data.bSelfMuted = State->bSelfMuted;
		Data.RevivesRemaining = State->RemainingRevives;
	}

	if (UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
	{
		Data.BleedOutRemaining = Downed->IsDowned() ? Downed->GetBleedOutRemaining() : 0.f;
	}

	// --- Weapon -----------------------------------------------------------
	if (UOneBrahamWeaponComponent* Weapons = CachedWeapons.Get())
	{
		if (AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon())
		{
			Data.WeaponId = Weapon->Definition != nullptr ? Weapon->Definition->WeaponId : NAME_None;
			Data.WeaponDisplayName = Weapon->Definition != nullptr
				? Weapon->Definition->DisplayName
				: NSLOCTEXT("OneBraham", "HUD_Unarmed", "UNARMED");
			Data.AmmoInMagazine = Weapon->GetAmmoInMagazine();
			Data.MagazineSize = Weapon->GetMagazineSize();
			Data.bLaserOn = Weapon->IsLaserEnabled();
			Data.bFlashlightOn = Weapon->IsFlashlightEnabled();
		}
		else
		{
			Data.WeaponId = NAME_None;
			Data.WeaponDisplayName = NSLOCTEXT("OneBraham", "HUD_Unarmed", "UNARMED");
			Data.AmmoInMagazine = 0;
			Data.MagazineSize = 0;
		}

		Data.SpreadDegrees = Weapons->ComputeCurrentSpreadDegrees();
		Data.bMeleeAvailable = Weapons->CanMelee();

		// The fire mode belongs to the component, not the definition: a single
		// weapon can be switched between semi and auto, and the HUD must show what
		// the weapon will actually do on the next trigger pull.
		Data.FireMode = Weapons->GetCurrentFireMode();

		// Reserve ammo also lives on the component (the weapon actor only knows its
		// magazine), and it is per weapon id - which is why it is read here and not
		// in the inventory block.
		Data.AmmoReserve = Weapons->GetReserveAmmoForCurrentWeapon();
		Data.bReloading = Weapons->IsReloading();
		Data.ReloadProgress01 = Weapons->GetReloadProgress01();
	}

	// --- Inventory --------------------------------------------------------
	if (UOneBrahamInventoryComponent* Inventory = CachedInventory.Get())
	{
		Data.FragCount = Inventory->GetUtilityCount(TEXT("Frag"));
		Data.FlashCount = Inventory->GetUtilityCount(TEXT("Flashbang"));
		Data.SmokeCount = Inventory->GetUtilityCount(TEXT("Smoke"));
		Data.BreachCount = Inventory->GetUtilityCount(TEXT("BreachCharge"));
		Data.MedkitCount = Inventory->GetUtilityCount(TEXT("Medkit"));
		Data.HighestKeycardTier = Inventory->HighestKeycardTier;
		Data.bHasOverrideTool = Inventory->bHasOverrideTool;
	}

	// --- Interaction ------------------------------------------------------
	if (UOneBrahamInteractComponent* Interaction = CachedInteraction.Get())
	{
		Data.bHasInteractionTarget = Interaction->GetCurrentInteractionTarget() != nullptr;
		Data.InteractionHoldProgress01 = Interaction->GetHoldProgress();
	}

	// --- Match ------------------------------------------------------------
	if (const AOneBrahamGameState* GS = GetWorld() != nullptr
		? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr)
	{
		Data.MatchPhaseTag = FName(*UEnum::GetDisplayValueAsText(GS->MatchPhase).ToString());
		Data.AlivePlayersRemaining = GS->GetAlivePlayerCount();
		Data.SquadsRemaining = GS->GetSquadCount();
		Data.ScoreHome = GS->TeamScores.IsValidIndex(0) ? GS->TeamScores[0] : 0;
		Data.ScoreAway = GS->TeamScores.IsValidIndex(1) ? GS->TeamScores[1] : 0;

		if (State != nullptr)
		{
			Data.LocalTeamScore = GS->TeamScores.IsValidIndex(static_cast<int32>(State->Team))
				? GS->TeamScores[static_cast<int32>(State->Team)]
				: 0;
		}

		// Phase clock: the HUD counts *down* to the phase deadline, which is what
		// players actually need to know.
		if (GS->MatchTimeLimitSeconds > 0.f)
		{
			Data.MatchTimeRemaining = FMath::Max(0.f, GS->MatchTimeLimitSeconds - GS->PhaseElapsedSeconds);
		}

		// --- Kill feed conversion ------------------------------------------
		Data.KillFeed.Reset(Data.KillFeed.Num());
		const float Now = GetWorld()->GetTimeSeconds();

		for (const FOneBrahamKillFeedEntry& Entry : GS->KillFeed)
		{
			FOneBrahamHUDKillFeedEntry HUDEntry;
			HUDEntry.KillerName = Entry.KillerName;
			HUDEntry.VictimName = Entry.VictimName;
			HUDEntry.WeaponId = Entry.WeaponId;
			HUDEntry.bHeadshot = Entry.Verb == EOneBrahamKillFeedVerb::Headshot;
			HUDEntry.bMelee = Entry.WeaponId == TEXT("Melee");
			HUDEntry.bTeamKill = Entry.KillerTeam == Entry.VictimTeam
				&& Entry.KillerTeam != EOneBrahamTeam::None
				&& Entry.Verb != EOneBrahamKillFeedVerb::Building;
			HUDEntry.bKillerIsLocalPlayer = State != nullptr && Entry.KillerName == State->GetPlayerName();
			// The game state's kill feed carries the server timestamp of the kill;
			// the HUD ages entries from the local clock so the fade is smooth even
			// when the replication of the array is bursty.
			HUDEntry.EventTime = (Entry.Timestamp > 0.f)
				? Now - FMath::Clamp(GS->GetServerWorldTimeSeconds() - Entry.Timestamp, 0.f, 30.f)
				: Now;

			Data.KillFeed.Add(HUDEntry);
		}

		// --- Squad panel ----------------------------------------------------
		// Everything the squad panel shows comes from the replicated squad roster
		// plus live per-pawn state. A teammate on another floor shows as "FL 9",
		// because a distance is a lie when there are fifteen floors between you.
		if (State != nullptr && State->SquadId != INDEX_NONE)
		{
			const FOneBrahamSquadInfo SquadInfo = GS->GetSquadInfo(State->SquadId);
			Data.Squad.Reset(SquadInfo.Members.Num());

			for (const FOneBrahamSquadMemberInfo& Member : SquadInfo.Members)
			{
				FOneBrahamHUDSquadMember Row;
				Row.PlayerId = Member.PlayerId;
				Row.PlayerName = Member.PlayerName;
				Row.LifeState = Member.LifeState;
				Row.bIsSquadLeader = Member.bIsSquadLeader;
				Row.Kills = Member.Kills;
				Row.DownedCount = Member.Downs;

				// Local player gets their own row marked so the widget can style it.
				Row.bIsLocalPlayer = (Member.PlayerId == State->GetPlayerId());

				const AOneBrahamPlayerState* MemberState = nullptr;
				for (APlayerState* BaseState : GS->PlayerArray)
				{
					if (BaseState != nullptr && BaseState->GetPlayerId() == Member.PlayerId)
					{
						MemberState = Cast<AOneBrahamPlayerState>(BaseState);
						break;
					}
				}

				if (MemberState != nullptr)
				{
					// The mic icon is authoritative from the PlayerState: the server
					// sets it on the multicast that follows a real transmission.
					Row.bIsTransmitting = MemberState->bIsTransmitting;

					if (const APawn* MemberPawn = MemberState->GetPawn())
					{
						if (const AOneBrahamCharacter* MemberCharacter = Cast<AOneBrahamCharacter>(MemberPawn))
						{
							if (const UOneBrahamHealthComponent* MemberHealth = MemberCharacter->GetHealthComponent())
							{
								Row.HealthFraction = MemberHealth->GetHealthFraction();
							}

							if (const UOneBrahamDownedComponent* MemberDowned = MemberCharacter->GetDownedComponent())
							{
								if (MemberDowned->IsDowned())
								{
									Row.BleedOutFraction = MemberDowned->GetBleedOutRemaining() / FMath::Max(1.f, OneBrahamConstants::BleedOutSeconds);
								}
							}

							// Distance only when actually on the same floor.
							const int32 MemberFloor = FMath::FloorToInt(MemberPawn->GetActorLocation().Z / 400.f);
							if (MemberCharacter != Character)
							{
								Row.DistanceCm = MemberFloor == Data.CurrentFloorIndex
									? FVector::Dist(Character->GetActorLocation(), MemberPawn->GetActorLocation())
									: -1.f;
								Row.FloorIndex = MemberFloor;
							}
							else
							{
								Row.DistanceCm = -1.f;
								Row.FloorIndex = Data.CurrentFloorIndex;
							}
						}
					}
				}

				Data.Squad.Add(Row);
			}
		}

		// --- Objective state ------------------------------------------------
		if (const UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
		{
			Data.bLockdownActive = Lockdown->IsLockdownActive();
			Data.LockdownSecondsRemaining = Lockdown->GetSecondsToNextStage();
		}

		if (const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent())
		{
			Data.bExtractionActive = Extraction->IsExtractionActive();
			Data.ExtractionHoldProgress01 = Extraction->GetHoldProgress01();
			Data.ExtractionSecondsRemaining = Extraction->GetSecondsRemainingInHold();
			Data.HoldingSquadId = Extraction->GetHoldingSquadId();
		}
	}

	// --- Floor ------------------------------------------------------------
	Data.CurrentFloorIndex = Character->GetCurrentFloorIndex();
	Data.FloorDisplayName = UOneBrahamUIStyle::FloorLabel(Data.CurrentFloorIndex);

	// --- Voice ------------------------------------------------------------
	if (const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(Character))
	{
		Data.VoiceChannelName = Voice->GetChannelDisplayName(Data.VoiceChannel);
	}

	if (const UOneBrahamProximityVoiceComponent* VoiceComponent = Character->GetVoiceComponent())
	{
		Data.bLocalTransmitting = VoiceComponent->IsLocalTransmitting();
		Data.bMicrophoneAvailable = VoiceComponent->HasWorkingMicrophone();
	}
}

// ==========================================================================
//  Widget commands
// ==========================================================================
void UOneBrahamHUDComponent::SetScoreboardVisible(bool bVisible)
{
	if (HUDWidget != nullptr)
	{
		HUDWidget->SetScoreboardVisible(bVisible);
	}
}

void UOneBrahamHUDComponent::SetDeathScreenVisible(bool bVisible, const FText& CauseText)
{
	if (HUDWidget != nullptr)
	{
		HUDWidget->SetDeathScreenVisible(bVisible, CauseText);
	}
}

void UOneBrahamHUDComponent::SetPauseMenuVisible(bool bVisible)
{
	if (HUDWidget != nullptr)
	{
		HUDWidget->TogglePauseMenu(bVisible);
	}
}

void UOneBrahamHUDComponent::ShowBanner(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	Data.BannerText = Text;
	Data.BannerColour = Colour;
	BannerRemaining = FMath::Max(0.2f, DurationSeconds);

	if (HUDWidget != nullptr)
	{
		HUDWidget->ShowBanner(Text, BannerRemaining, Colour);
	}
}

void UOneBrahamHUDComponent::ShowSubBanner(const FText& Text, float DurationSeconds)
{
	Data.SubBannerText = Text;
	SubBannerRemaining = FMath::Max(0.2f, DurationSeconds);

	if (HUDWidget != nullptr)
	{
		HUDWidget->ShowSubBanner(Text, SubBannerRemaining);
	}
}

void UOneBrahamHUDComponent::AddWorldPing(const FVector& Location, const FString& InstigatorName,
	int32 Team, FName PingType)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Cap the number of pings: a squad of four spamming the ping key must not turn
	// the screen into a pin board.
	static constexpr int32 MaxPings = 6;

	if (Data.WorldPings.Num() >= MaxPings)
	{
		Data.WorldPings.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FOneBrahamHUDWorldPing Ping;
	Ping.Location = Location;
	Ping.InstigatorName = InstigatorName;
	Ping.Team = Team;
	Ping.PingType = PingType;
	Ping.SpawnTime = World->GetTimeSeconds();
	Ping.Lifetime = 6.f;

	Data.WorldPings.Add(Ping);

	if (HUDWidget != nullptr)
	{
		HUDWidget->AddWorldPing(Ping);
	}
}

// ==========================================================================
//  Transient feedback
// ==========================================================================
void UOneBrahamHUDComponent::NotifyHitConfirmed(bool bHeadshot, bool bKill)
{
	HitMarkerRemaining = HitMarkerSeconds;
	bLastHitHeadshot = bHeadshot;
	bLastHitWasKill = bKill;

	// A killing hit holds the marker slightly longer: the player needs to register
	// the trade as finished, and 60 ms of extra dwell does that without feeling slow.
	if (bKill)
	{
		HitMarkerRemaining += 0.12f;
	}
}

void UOneBrahamHUDComponent::NotifyDamageTaken(float Damage, const FVector& InstigatorLocation)
{
	DamageIndicatorRemaining = DamageIndicatorSeconds;

	AOneBrahamCharacter* Character = BoundCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	// Convert the world-space attacker position into a screen-relative angle for
	// the indicator, in the horizontal plane only: an indicator that points up or
	// down tells the player nothing they can act on.
	const FVector ToInstigator = InstigatorLocation - Character->GetActorLocation();
	const FVector Flat(ToInstigator.X, ToInstigator.Y, 0.f);

	if (!Flat.IsNearlyZero())
	{
		const FVector Forward = Character->GetActorForwardVector();
		const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
		const float ForwardAngle = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));

		Data.DamageDirectionDegrees = FRotator::NormalizeAxis(TargetAngle - ForwardAngle);
	}

	// Damage clamps the indicator strength: a 3-point graze should whisper.
	Data.DamageIndicatorAlpha = FMath::Clamp(Damage / 40.f, 0.25f, 1.f) * (DamageIndicatorSeconds / DamageIndicatorSeconds);
}

void UOneBrahamHUDComponent::NotifyDownedStateChanged(bool bDowned, float BleedOutRemaining)
{
	Data.BleedOutRemaining = bDowned ? BleedOutRemaining : 0.f;
	Data.LifeState = bDowned ? EOneBrahamLifeState::Downed : EOneBrahamLifeState::Alive;

	if (HUDWidget != nullptr)
	{
		HUDWidget->SetDownedVisible(bDowned);
	}

	if (bDowned)
	{
		// Going down is the loudest thing that happens to a player in ASCENT:
		// one life, up to a minute of bleed-out, and a squadmate who may or may
		// not be able to reach you. The prompt is the copy table's, so a writer
		// can turn "CALL FOR HELP" into "TELL YOUR SQUAD" without a recompile.
		Announce(TEXT("HUD_Downed_CallHelp"), 4.f, UOneBrahamUIStyle::EmergencyRed());
	}
}

void UOneBrahamHUDComponent::NotifyInteractionProgress(const FText& Prompt, float Progress01)
{
	Data.bHasInteractionTarget = !Prompt.IsEmpty();
	Data.InteractionPrompt = Prompt;
	Data.InteractionHoldProgress01 = FMath::Clamp(Progress01, 0.f, 1.f);
}

// ==========================================================================
//  Delegate handlers
// ==========================================================================
void UOneBrahamHUDComponent::HandleHealthChanged(float NewHealth, float MaxHealthValue, float Delta)
{
	Data.Health = NewHealth;
	Data.MaxHealth = MaxHealthValue;
}

void UOneBrahamHUDComponent::HandleArmourChanged(float NewArmour, float MaxArmourValue)
{
	Data.Armour = NewArmour;
	Data.MaxArmour = MaxArmourValue;
}

void UOneBrahamHUDComponent::HandleInventoryChanged()
{
	// The next gather tick picks the new counts up; nothing to do here beyond
	// making sure the HUD is not a frame behind on a pickup.
	GatherHUDData();
}

void UOneBrahamHUDComponent::HandleVoiceLineSpoken(APlayerState* Speaker, FName LineId,
	const FText& Subtitle, float DurationSeconds)
{
	if (!Data.bShowSubtitles || Subtitle.IsEmpty())
	{
		return;
	}

	AOneBrahamCharacter* Character = BoundCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	Data.SubtitleText = Subtitle;
	Data.SubtitleSpeaker = Speaker != nullptr ? Speaker->GetPlayerName() : FString();
	SubtitleRemaining = FMath::Clamp(DurationSeconds, 0.75f, SubtitleMaxSeconds);

	if (HUDWidget != nullptr)
	{
		HUDWidget->ShowSubtitle(Data.SubtitleText, Data.SubtitleSpeaker, SubtitleRemaining);
	}
}

void UOneBrahamHUDComponent::HandleHitConfirmed(bool bHeadshot, bool bKill, float DistanceCm)
{
	NotifyHitConfirmed(bHeadshot, bKill);
}

// ==========================================================================
//  Overlays: the HUD toggle, the floor plan and the comms wheel
// ==========================================================================

void UOneBrahamHUDComponent::SetHUDVisible(bool bVisible)
{
	if (bHUDVisible == bVisible)
	{
		return;
	}

	bHUDVisible = bVisible;

	// The widget is hidden rather than destroyed: rebuilding a HUD on every
	// press of the screenshot key would drop frames exactly when somebody is
	// trying to take a screenshot.
	if (HUDWidget != nullptr)
	{
		HUDWidget->SetVisibility(bHUDVisible ? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	// The overlays follow the HUD: hiding the interface and then opening the
	// map is a contradiction, and "the plan is what is left of the HUD" is not
	// a state anybody wants to explain.
	if (!bHUDVisible)
	{
		SetFloorPlanVisible(false);
		SetQuickLineMenuVisible(false);
	}

	UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.UI] HUD %s."), bHUDVisible ? TEXT("shown") : TEXT("hidden"));
}

void UOneBrahamHUDComponent::EnsureOverlayWidgets()
{
	AOneBrahamPlayerController* Controller = OwningController.Get();

	if (Controller == nullptr || !bHUDEnabled)
	{
		return;
	}

	if (FloorPlanWidget == nullptr)
	{
		// The authored Blueprint when it exists, the code-drawn C++ class
		// otherwise: the plan is generated geometry, so the fallback is the real
		// thing rather than a stand-in.
		TSubclassOf<UOneBrahamFloorPlanWidget> PlanClass =
			LoadClass<UOneBrahamFloorPlanWidget>(nullptr,
				TEXT("/Game/OneBraham/UI/HUD/WBP_FloorPlan.WBP_FloorPlan_C"));

		if (PlanClass == nullptr)
		{
			PlanClass = UOneBrahamFloorPlanWidget::StaticClass();
		}

		FloorPlanWidget = CreateWidget<UOneBrahamFloorPlanWidget>(Controller, PlanClass);

		if (FloorPlanWidget != nullptr)
		{
			FloorPlanWidget->AddToPlayerScreen(1);
			FloorPlanWidget->SetPlanVisible(false);
		}
	}

	if (QuickLineWidget == nullptr)
	{
		TSubclassOf<UOneBrahamQuickLineWidget> WheelClass =
			LoadClass<UOneBrahamQuickLineWidget>(nullptr,
				TEXT("/Game/OneBraham/UI/HUD/WBP_QuickLineWheel.WBP_QuickLineWheel_C"));

		if (WheelClass == nullptr)
		{
			WheelClass = UOneBrahamQuickLineWidget::StaticClass();
		}

		QuickLineWidget = CreateWidget<UOneBrahamQuickLineWidget>(Controller, WheelClass);

		if (QuickLineWidget != nullptr)
		{
			// Above the plan: the wheel is a reflex, the plan is a decision, and
			// the two are never open for the same reason.
			QuickLineWidget->AddToPlayerScreen(2);
			QuickLineWidget->OnQuickLineChosen.AddDynamic(this, &UOneBrahamHUDComponent::HandleQuickLineChosen);
			QuickLineWidget->SetWheelVisible(false);
		}
	}
}

void UOneBrahamHUDComponent::SetFloorPlanVisible(bool bVisible)
{
	if (bFloorPlanVisible == bVisible)
	{
		return;
	}

	bFloorPlanVisible = bVisible;

	if (!bFloorPlanVisible)
	{
		if (FloorPlanWidget != nullptr)
		{
			FloorPlanWidget->SetPlanVisible(false);
		}

		return;
	}

	EnsureOverlayWidgets();

	if (FloorPlanWidget == nullptr)
	{
		// No viewport: a dedicated server, a headless test, or a controller that
		// has not reached BeginPlay. The state still flips, so the input handler
		// is consistent, and the next call after the widget exists will draw it.
		UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.UI] Floor plan requested with no widget available."));
		return;
	}

	FOneBrahamFloorPlanData PlanData;
	GatherFloorPlanData(PlanData);
	FloorPlanWidget->SetPlanData(PlanData);
	FloorPlanWidget->SetPlanVisible(true);
}

void UOneBrahamHUDComponent::SetQuickLineMenuVisible(bool bVisible)
{
	if (bQuickLineMenuVisible == bVisible)
	{
		return;
	}

	bQuickLineMenuVisible = bVisible;

	if (!bQuickLineMenuVisible)
	{
		if (QuickLineWidget != nullptr)
		{
			// A press-and-release wheel commits on release, so the close path
			// asks the widget what is selected rather than assuming nothing is.
			QuickLineWidget->CommitHoveredSegment();
		}

		return;
	}

	EnsureOverlayWidgets();

	if (QuickLineWidget == nullptr)
	{
		return;
	}

	FOneBrahamQuickLineData WheelData;
	GatherQuickLineData(WheelData);
	QuickLineWidget->SetWheelData(WheelData);
	QuickLineWidget->SetWheelVisible(true);
}

void UOneBrahamHUDComponent::RefreshOverlays()
{
	// Called when something the plan or the wheel draws has changed: a lockdown
	// stage, a squadmate going down, a line's cooldown lapsing. Rebuilding only
	// what is open keeps a closed overlay at zero cost.
	if (bFloorPlanVisible && FloorPlanWidget != nullptr)
	{
		FOneBrahamFloorPlanData PlanData;
		GatherFloorPlanData(PlanData);
		FloorPlanWidget->SetPlanData(PlanData);
	}

	if (bQuickLineMenuVisible && QuickLineWidget != nullptr)
	{
		FOneBrahamQuickLineData WheelData;
		GatherQuickLineData(WheelData);
		QuickLineWidget->SetWheelData(WheelData);
	}
}

void UOneBrahamHUDComponent::HandleQuickLineChosen(FName LineId)
{
	bQuickLineMenuVisible = false;

	AOneBrahamPlayerController* Controller = OwningController.Get();

	if (Controller == nullptr)
	{
		return;
	}

	// The request goes to the server, which validates it against the line set,
	// the cooldowns and the speaker's own components before anything is said.
	// A client that opens the wheel and presses a key has asked a question, not
	// made a statement.
	Controller->RequestQuickLine(LineId);
}

void UOneBrahamHUDComponent::GatherFloorPlanData(FOneBrahamFloorPlanData& OutData) const
{
	const UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	const UOneBrahamBuildingLayoutAsset* Layout = UOneBrahamBuildingLayoutAsset::Get(this);

	if (Layout == nullptr)
	{
		return;
	}

	const AOneBrahamGameState* GameState = World->GetGameState<AOneBrahamGameState>();
	const AOneBrahamCharacter* Character = BoundCharacter.Get();
	const AOneBrahamPlayerState* LocalState = Character != nullptr
		? Character->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

	const int32 LocalFloor = Character != nullptr ? Character->GetCurrentFloorIndex() : INDEX_NONE;

	OutData.BuildingName = Layout->BuildingName;
	OutData.LocalFloorLabel = Layout->GetFloorAnnouncement(LocalFloor);
	OutData.Floors.Reset();

	// Floors are collected ascending, then reversed: the plan draws the Skydeck
	// at the top because that is the direction the match moves in.
	TArray<FOneBrahamFloorDescription> Ascending;
	Layout->GetFloorsAscending(Ascending);

	const UOneBrahamLockdownComponent* Lockdown = GameState != nullptr
		? GameState->GetLockdownComponent() : nullptr;

	// Squad count per floor, and whether anybody is down on it. Read from the
	// player array rather than from replicated per-floor counters: the plan is
	// open for a second at a time and this is a handful of iterations.
	for (int32 Index = Ascending.Num() - 1; Index >= 0; --Index)
	{
		const FOneBrahamFloorDescription& Description = Ascending[Index];

		FOneBrahamFloorPlanFloor Row;
		Row.FloorIndex = Description.FloorIndex;
		Row.AreaName = Layout->GetFloorShortName(Description.FloorIndex);
		Row.bLocalPlayerFloor = Description.FloorIndex == LocalFloor;
		Row.bSealed = Lockdown != nullptr && Lockdown->IsFloorSealed(Description.FloorIndex);

		if (GameState != nullptr && LocalState != nullptr && LocalState->SquadId != INDEX_NONE)
		{
			for (APlayerState* OtherState : GameState->PlayerArray)
			{
				const AOneBrahamPlayerState* Other = Cast<AOneBrahamPlayerState>(OtherState);

				if (Other == nullptr || Other->SquadId != LocalState->SquadId)
				{
					continue;
				}

				const AOneBrahamCharacter* OtherCharacter = Cast<AOneBrahamCharacter>(Other->GetPawn());

				if (OtherCharacter == nullptr || OtherCharacter->GetCurrentFloorIndex() != Description.FloorIndex)
				{
					continue;
				}

				++Row.SquadCount;

				if (Other->IsDowned())
				{
					Row.bSquadmateDown = true;
				}
			}
		}

		OutData.Floors.Add(Row);
	}

	if (const UOneBrahamExtractionComponent* Extraction = GameState != nullptr
		? GameState->GetExtractionComponent() : nullptr)
	{
		OutData.bExtractionInProgress = Extraction->IsExtractionActive();
		OutData.ExtractionProgress01 = Extraction->GetHoldProgress01();
		OutData.ExtractingSquadId = Extraction->GetHoldingSquadId();
	}

	if (Lockdown != nullptr)
	{
		OutData.LockdownStageLabel = Lockdown->IsWarningActive()
			? NSLOCTEXT("OneBraham", "PlanWarning", "LOCKDOWN INBOUND")
			: UEnum::GetDisplayValueAsText(Lockdown->GetStage());
	}

	if (GameState != nullptr)
	{
		// The game state keeps a limit and a phase clock rather than a countdown,
		// because a countdown has to be replicated and a limit plus an elapsed
		// time does not. The plan does the subtraction, and clamps: a match in
		// sudden death has no remaining time and should not draw a negative one.
		OutData.MatchTimeRemaining = FMath::Max(0.f,
			GameState->MatchTimeLimitSeconds - GameState->PhaseElapsedSeconds);
	}
}

void UOneBrahamHUDComponent::GatherQuickLineData(FOneBrahamQuickLineData& OutData) const
{
	OutData.Entries.Reset();
	OutData.bTransmitting = Data.bLocalTransmitting;

	const AOneBrahamCharacter* Character = BoundCharacter.Get();

	if (Character == nullptr)
	{
		return;
	}

	if (const UOneBrahamProximityVoiceComponent* Voice = Character->GetVoiceComponent())
	{
		OutData.bTransmitting = Voice->IsLocalTransmitting();
	}

	UOneBrahamVoiceSubsystem* VoiceSubsystem = UOneBrahamVoiceSubsystem::Get(Character);
	UOneBrahamVoiceLineSet* LineSet = VoiceSubsystem != nullptr ? VoiceSubsystem->GetLineSet() : nullptr;

	if (LineSet == nullptr)
	{
		return;
	}

	// The wheel's ids and labels live on the voice subsystem, next to the line
	// table they are checked against, so the list cannot be extended in the UI
	// without extending the script. The component only decides what state each
	// segment is in.
	for (int32 SegmentIndex = 0; SegmentIndex < 8; ++SegmentIndex)
	{
		FName LineId;
		FText WheelLabel;
		UOneBrahamVoiceSubsystem::GetQuickLineWheel(LineId, WheelLabel, SegmentIndex);

		if (LineId.IsNone())
		{
			continue;
		}

		TArray<const FOneBrahamVoiceLine*> Lines;
		LineSet->FindLines(LineId, Lines);

		if (Lines.Num() == 0)
		{
			// A segment whose line is not in the script is dropped rather than
			// drawn empty: the voice script check reports the missing id, and an
			// empty button on a wheel is a bug a player cannot act on.
			OB_VOICE_LOG("Comms wheel segment %d requests '%s', which the voice script does not have.",
				SegmentIndex, *LineId.ToString());
			continue;
		}

		FOneBrahamQuickLineEntry Entry;
		Entry.LineId = LineId;
		Entry.Label = WheelLabel;

		if (VoiceSubsystem != nullptr)
		{
			Entry.CooldownRemaining = VoiceSubsystem->GetLineCooldownRemaining(Character, LineId);
			Entry.bAvailable = Entry.CooldownRemaining <= 0.f;
		}

		OutData.Entries.Add(Entry);
	}
}

// ==========================================================================
//  Debug
// ==========================================================================
void UOneBrahamHUDComponent::DumpHUDState() const
{
	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD state:"));
	UE_LOG(LogOneBrahamUI, Log, TEXT("   health %.0f/%.0f  armour %.0f  state %s"),
		Data.Health, Data.MaxHealth, Data.Armour,
		*UEnum::GetDisplayValueAsText(Data.LifeState).ToString());
	UE_LOG(LogOneBrahamUI, Log, TEXT("   weapon '%s'  %d/%d + %d  spread %.2f deg"),
		*Data.WeaponId.ToString(), Data.AmmoInMagazine, Data.MagazineSize, Data.AmmoReserve, Data.SpreadDegrees);
	UE_LOG(LogOneBrahamUI, Log, TEXT("   floor %d  phase %s  alive %d  squads %d"),
		Data.CurrentFloorIndex, *Data.MatchPhaseTag.ToString(), Data.AlivePlayersRemaining, Data.SquadsRemaining);
	UE_LOG(LogOneBrahamUI, Log, TEXT("   squad rows %d  killfeed rows %d  pings %d"),
		Data.Squad.Num(), Data.KillFeed.Num(), Data.WorldPings.Num());
}
