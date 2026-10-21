// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamHUDComponent.h"

#include "UI/OneBrahamHUDStringTable.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamFirstPersonFeelComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Interaction/OneBrahamInteractComponent.h"
#include "UI/OneBrahamFloorPlanWidget.h"
#include "UI/OneBrahamHUDWidget.h"
#include "UI/OneBrahamMatchEndWidget.h"
#include "Engine/GameViewportClient.h"
#include "UI/OneBrahamPauseMenuWidget.h"
#include "UI/OneBrahamScoreboardWidget.h"
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
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "AI/OneBrahamBotRules.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "UI/HUD/OneBrahamBuildingStatus.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UnrealClient.h"
#include "World/OneBrahamDisplay.h"
#include "Camera/PlayerCameraManager.h"
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

	// A new match's controller starts with the game taking the input. The last
	// match's end screen left the shared game viewport ignoring it (UI only),
	// and a restart that replaces the controller - a dedicated server going
	// round again - tears the old one down after this one has the player, too
	// late for it to hand the input back itself.
	if (const UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport(); Viewport != nullptr && Viewport->IgnoreInput())
		{
			RestoreGameInput();
		}
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

	// The menus go with the HUD: a pause menu that outlived its controller would
	// hold focus on a widget nobody can close.
	if (ScoreboardWidget != nullptr)
	{
		ScoreboardWidget->SetScoreboardOpen(false);
		ScoreboardWidget->RemoveFromParent();
		ScoreboardWidget = nullptr;
	}

	if (PauseMenuWidget != nullptr)
	{
		PauseMenuWidget->RemoveFromParent();
		PauseMenuWidget = nullptr;
	}

	if (MatchEndWidget != nullptr)
	{
		MatchEndWidget->RemoveFromParent();
		MatchEndWidget = nullptr;
		MatchEndGameState.Reset();

		// The screen's UI-only input lives on the game viewport, which outlives
		// this controller: a restart that brings a new controller (a dedicated
		// server going round again) otherwise began the next match with the
		// keyboard and the pad still ignored by the game.
		RestoreGameInput();
	}
}

void UOneBrahamHUDComponent::RestoreGameInput()
{
	if (AOneBrahamPlayerController* Controller = OwningController.Get())
	{
		Controller->FlushPressedKeys();
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->bShowMouseCursor = false;
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
	Data.DamageIndicatorAlpha = DamageIndicatorRemaining / DamageIndicatorSeconds * DamageIndicatorStrength;
	UpdateDamageDirection();
	Data.BannerRemaining = BannerRemaining;
	Data.SubBannerText = SubBannerRemaining > 0.f ? Data.SubBannerText : FText::GetEmpty();
	Data.SubtitleRemaining = SubtitleRemaining;

	if (SubtitleRemaining <= 0.f)
	{
		Data.SubtitleText = FText::GetEmpty();
	}

	// Low health pulse: only below 30%, and on the heartbeat - the same function
	// the first-person feel component drives the screen's vignette with, so the
	// crosshair and the edge of the screen beat together. A permanently pulsing
	// HUD is the fastest way to make a tense game feel cheap.
	const float HealthFraction = Data.MaxHealth > 0.f ? Data.Health / Data.MaxHealth : 1.f;
	Data.LowHealthPulseAlpha = HealthFraction < 0.3f && Data.LifeState == EOneBrahamLifeState::Alive
		? (0.4f + 0.6f * UOneBrahamFirstPersonFeelComponent::ComputeHeartbeatPulse(GetWorld()->GetTimeSeconds(), HealthFraction))
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

	// The building's half of the snapshot, on the same gather, then the replay
	// sample and (in a dev build, on request) the demonstration state.
	GatherHUDFrame();
	RecordReplay(DeltaTime);
	ApplyDevDemo();

	// Announcements run after the snapshot is gathered, so they read the same
	// state the widget is about to draw.
	AnnounceStateChanges();

	HUDWidget->ApplyHUDFrame(Frame);
	HUDWidget->ApplyHUDData(Data);

	// The plan, while open, follows the building live: it reads the same
	// status the floor stack does, so the two can never disagree.
	if (bFloorPlanVisible && FloorPlanWidget != nullptr)
	{
		FloorPlanWidget->SetBuildingFrame(Frame);
	}

	UpdateMatchEndScreen();
}

void UOneBrahamHUDComponent::UpdateMatchEndScreen()
{
	// Dev: a demonstrated end screen stays up over the live match until "off".
	if (DemoState == TEXT("matchend"))
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	AOneBrahamPlayerController* Controller = OwningController.Get();
	const bool bMatchOver = GS != nullptr
		&& (GS->MatchPhase == EOneBrahamMatchPhase::Complete || GS->MatchPhase == EOneBrahamMatchPhase::PostMatch);

	if (MatchEndWidget != nullptr)
	{
		// A seamless restart (a dedicated server going round again) keeps this
		// controller, this component and the screen on it: the widget belongs to
		// the controller, so the world change does not take it down. Without
		// this the next match was played - or rather not played - behind the
		// last one's results, with the input still the screen's. Between the
		// maps there is no game state, and the screen stays up over the loading.
		if (GS != nullptr && (GS != MatchEndGameState.Get() || !bMatchOver))
		{
			CloseMatchEndScreen();
		}
		return;
	}

	if (!bMatchOver || Controller == nullptr)
	{
		return;
	}

	// Everything the player had open goes: the end screen is the only thing
	// left to read, and a pause menu over it would hold the input.
	SetScoreboardVisible(false);
	SetFloorPlanVisible(false);
	SetQuickLineMenuVisible(false);
	SetDeathScreenVisible(false);
	if (PauseMenuWidget != nullptr)
	{
		PauseMenuWidget->CloseMenu();
	}
	SetHUDVisible(false);

	MatchEndWidget = CreateWidget<UOneBrahamMatchEndWidget>(Controller, UOneBrahamMatchEndWidget::StaticClass());
	if (MatchEndWidget == nullptr)
	{
		UE_LOG(LogOneBrahamUI, Warning, TEXT("[OB.UI] Failed to create the end screen."));
		return;
	}

	MatchEndGameState = GS;
	MatchEndWidget->AddToPlayerScreen(MatchEndZOrder);
	MatchEndWidget->SetReplay(Replay, Frame);
	MatchEndWidget->ShowMatchEnd();

	// The screen takes the input: the fight is over on the server, and a
	// trigger held under the results should not be shooting at anybody.
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Controller->SetInputMode(InputMode);
	Controller->bShowMouseCursor = false;

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] End screen up for '%s' (%s)."), *Controller->GetName(),
		*OneBrahamMatchEnd::LogNameFor(MatchEndWidget->GetShownEnding()));
}

void UOneBrahamHUDComponent::CloseMatchEndScreen()
{
	if (MatchEndWidget == nullptr)
	{
		return;
	}

	MatchEndWidget->RemoveFromParent();
	MatchEndWidget = nullptr;
	MatchEndGameState.Reset();

	// The HUD and the game's input come back for the new match.
	SetHUDVisible(true);
	RestoreGameInput();

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] End screen closed: a new match has begun."));
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
	// without being a permanent fixture on screen. The subtitle carries every
	// word; the PA card at the top carries the headline.
	Data.SubtitleText = Announcement;
	Data.SubtitleSpeaker.Reset();
	SubtitleRemaining = TannoySubtitleSeconds;

	// The card's headline is the paragraph's point: the first sentence after
	// the building clears its throat ("Attention all staff."). Its subline is
	// the building's own aside for the moment, from a short list per stage.
	FString Paragraph = Announcement.ToString().TrimStartAndEnd();
	for (const TCHAR* Preamble : { TEXT("Attention all staff."), TEXT("Attention, all staff."), TEXT("Attention.") })
	{
		if (Paragraph.StartsWith(Preamble))
		{
			Paragraph = Paragraph.Mid(FCString::Strlen(Preamble)).TrimStart();
		}
	}

	FString Headline = Paragraph;
	int32 Stop = INDEX_NONE;
	if (Paragraph.FindChar(TEXT('.'), Stop) && Stop > 8 && Stop < Paragraph.Len() - 1)
	{
		Headline = Paragraph.Left(Stop + 1);
	}

	const EOneBrahamLockdownStage Stage = Frame.Building.Stage;
	static const TCHAR* Calm[] = { TEXT("Thank you for choosing One Braham."), TEXT("Please sign in at reception.") };
	static const TCHAR* Lockdown[] = { TEXT("The lifts are thinking about what they've done."), TEXT("Please do not prop fire doors open."),
		TEXT("Taking the stairs burns seven calories per floor.") };
	static const TCHAR* Purge[] = { TEXT("Thank you for your patience."), TEXT("Hot desks are cleaned at 18:00. Do not be on one.") };
	const uint32 Pick = GetTypeHash(Headline);
	const int32 Band = Stage == EOneBrahamLockdownStage::Purge ? 2 : (Stage == EOneBrahamLockdownStage::Normal ? 0 : 1);
	const FString Aside = Band == 2 ? Purge[Pick % UE_ARRAY_COUNT(Purge)]
		: (Band == 1 ? Lockdown[Pick % UE_ARRAY_COUNT(Lockdown)] : Calm[Pick % UE_ARRAY_COUNT(Calm)]);

	if (HUDWidget != nullptr)
	{
		HUDWidget->PushAnnouncement(FText::FromString(Headline), FText::FromString(Aside), Stage, TannoySubtitleSeconds - 1.5f);
	}

	SyncBuildingDisplays(FText::FromString(Headline), FText::FromString(Aside), Stage, TannoySubtitleSeconds);
}

void UOneBrahamHUDComponent::HandleWarningBanner(const FText& Text, float DurationSeconds, FLinearColor Colour)
{
	if (Text.IsEmpty())
	{
		return;
	}

	// The stage's own two-word banner arrives with the stage: the card is
	// already saying it at eighty-eight pixels, so the echo is dropped.
	if (const UWorld* World = GetWorld(); World != nullptr && World->GetTimeSeconds() - LastStageCardTime < 3.0)
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

			case EOneBrahamExtractionState::Arriving:
				// The helicopter is on its way: THE ROOF, once, for everybody.
				if (HUDWidget != nullptr)
				{
					HUDWidget->ShowStageCard(EOneBrahamStageCard::Roof);
					LastStageCardTime = World->GetTimeSeconds();
				}
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
			const EOneBrahamLockdownStage Previous = LastLockdownStage;
			LastLockdownStage = Stage;

			// The sealed set does not replicate; the seal rule does. The line is
			// the highest occupied floor on the frame the shutters moved, and it
			// only ever rises until the Purge takes everything below the ceiling.
			if (Stage == EOneBrahamLockdownStage::PreLockdown || Stage == EOneBrahamLockdownStage::Lockdown)
			{
				SealCeilingHint = FMath::Max(SealCeilingHint, Lockdown->GetHighestOccupiedFloor() - 1);
			}
			else if (Stage == EOneBrahamLockdownStage::Normal)
			{
				SealCeilingHint = INDEX_NONE;
			}

			// The three moments the whole screen speaks: the first shutters (the
			// building going from calm to lockdown), and the Purge. The second
			// sealing stage is the tannoy's alone - the card has made its point.
			if (HUDWidget != nullptr)
			{
				if (Previous == EOneBrahamLockdownStage::Normal && Stage != EOneBrahamLockdownStage::Normal
					&& Stage != EOneBrahamLockdownStage::Purge)
				{
					HUDWidget->ShowStageCard(EOneBrahamStageCard::Lockdown);
					LastStageCardTime = World->GetTimeSeconds();
				}
				else if (Stage == EOneBrahamLockdownStage::Purge)
				{
					HUDWidget->ShowStageCard(EOneBrahamStageCard::Purge);
					LastStageCardTime = World->GetTimeSeconds();
				}
			}

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

	// HUD Scale (80-120%) is applied by the painted HUD itself, each element
	// about the corner it is anchored to: a render scale about the centre walks
	// the corner panels off the screen at 120%, which is the classic broken
	// "HUD scaling". Only an authored Blueprint HUD, which paints nothing,
	// still takes the render scale.
	const float Scale = FMath::Clamp(Settings->HUDScale, 0.8f, 1.2f);
	if (!FMath::IsNearlyEqual(CachedHUDScale, Scale, 0.001f))
	{
		HUDWidget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		HUDWidget->SetRenderScale(HUDWidget->IsCodelessLayout() ? FVector2D(1.f, 1.f) : FVector2D(Scale, Scale));
		CachedHUDScale = Scale;
	}
	Frame.HUDScale = Scale;
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
		// The channel the player can actually use: dead players are on squad radio.
		Data.VoiceChannel = State->GetEffectiveVoiceChannel();
		Data.bSelfMuted = State->bSelfMuted;
		Data.RevivesRemaining = State->RemainingRevives;
	}

	if (UOneBrahamDownedComponent* Downed = Character->GetDownedComponent())
	{
		Data.BleedOutRemaining = Downed->IsDowned() ? Downed->GetBleedOutRemaining() : 0.f;
		Data.ReviveProgress01 = Downed->IsDowned() ? Downed->GetReviveProgress() : 0.f;
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
	//
	// Two different questions, two different sources.
	//
	// "What could I use right now" belongs to the controller: it owns the 10 Hz
	// trace, and it is the only thing that knows about a door the player is looking
	// at but has not pressed anything on. Reading the prompt from there is the line
	// that put interaction prompts on the screen at all - the data field existed,
	// the copy existed, and nothing ever filled it, so the game had a prompt system
	// with no prompts in it.
	//
	// "How far through the hold am I" belongs to the interaction component, and it
	// is replicated, so the ring is honest on a client.
	if (const AOneBrahamPlayerController* OwnerController = Cast<AOneBrahamPlayerController>(GetOwner()))
	{
		const FOneBrahamInteractionTarget& Target = OwnerController->GetInteractionTarget();

		// "Something to say", not "something under the crosshair": an interactable
		// that returns an empty prompt has nothing to offer right now (a breached
		// wall, a colleague who is on their feet), and drawing a blank line where
		// a verb should be is worse than drawing nothing.
		Data.bHasInteractionTarget = Target.Actor != nullptr && !Target.Prompt.IsEmpty();
		Data.InteractionPrompt = Target.Prompt;

		// A target you cannot use yet is not hidden, it is refused: the prompt goes
		// red and the widget prints the interactable's own refusal sentence
		// ("Partition - breaching charge required", "Executive badge required"),
		// which tells the player what to do about it.
		Data.bInteractionBlocked = Data.bHasInteractionTarget && !Target.bEnabled;
		Data.InteractionBlockedReason = Data.bInteractionBlocked ? Target.Prompt : FText::GetEmpty();
	}

	if (UOneBrahamInteractComponent* Interaction = CachedInteraction.Get())
	{
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
			// TeamScores is indexed 0 = Internal Staff, 1 = External Contractors
			// (AOneBrahamGameMode::AddTeamScoreByIndex), not by the team enum,
			// whose values for those two are 5 and 6: indexing by the enum read
			// past the end of a two-entry array and always showed 0.
			const int32 TeamIndex = State->Team == EOneBrahamTeam::InternalStaff ? 0
				: State->Team == EOneBrahamTeam::ExternalContractors ? 1 : INDEX_NONE;
			Data.LocalTeamScore = GS->TeamScores.IsValidIndex(TeamIndex) ? GS->TeamScores[TeamIndex] : 0;
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
							const int32 MemberFloor = OneBrahamConstants::FloorIndexForZ(MemberPawn->GetActorLocation().Z);
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

	if (const UOneBrahamProximityVoiceComponent* VoiceComponent = Character->FindComponentByClass<UOneBrahamProximityVoiceComponent>())
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
	// The HUD's own Panel_Scoreboard is no longer shown: the scoreboard is its
	// own widget (UOneBrahamScoreboardWidget) because it has buttons, and the
	// HUD is deliberately hit-test invisible. Closing never creates the widget.
	if (bVisible)
	{
		EnsureMenuWidgets();
	}

	if (ScoreboardWidget != nullptr)
	{
		ScoreboardWidget->SetScoreboardOpen(bVisible);
	}
	if (HUDWidget != nullptr)
	{
		HUDWidget->SetBoardOpen(bVisible);
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
	if (bVisible)
	{
		EnsureMenuWidgets();
	}

	if (PauseMenuWidget != nullptr)
	{
		if (bVisible)
		{
			PauseMenuWidget->OpenMenu();
		}
		else
		{
			PauseMenuWidget->CloseMenu();
		}
	}

	// The HUD steps out while the player is ON BREAK: the pause screen has its
	// own status card, and two lockdown timers on one screen is one too many.
	if (bVisible && bHUDVisible)
	{
		bHiddenForPause = true;
		SetHUDVisible(false);
	}
	else if (!bVisible && bHiddenForPause)
	{
		bHiddenForPause = false;
		SetHUDVisible(true);
	}
}

void UOneBrahamHUDComponent::EnsureMenuWidgets()
{
	AOneBrahamPlayerController* Controller = OwningController.Get();

	if (Controller == nullptr || !bHUDEnabled)
	{
		return;
	}

	// Plain C++ classes, no Blueprint lookup: both build their own trees, and the
	// builder's WBP_OneBrahamScoreboard is parented to the HUD widget class, not
	// to this one, so loading it here would give a HUD rather than a scoreboard.
	if (ScoreboardWidget == nullptr)
	{
		ScoreboardWidget = CreateWidget<UOneBrahamScoreboardWidget>(Controller, UOneBrahamScoreboardWidget::StaticClass());

		if (ScoreboardWidget != nullptr)
		{
			ScoreboardWidget->AddToPlayerScreen(ScoreboardZOrder);
			ScoreboardWidget->SetScoreboardOpen(false);
		}
	}

	if (PauseMenuWidget == nullptr)
	{
		PauseMenuWidget = CreateWidget<UOneBrahamPauseMenuWidget>(Controller, UOneBrahamPauseMenuWidget::StaticClass());

		if (PauseMenuWidget != nullptr)
		{
			PauseMenuWidget->AddToPlayerScreen(PauseMenuZOrder);
			PauseMenuWidget->CloseMenu();
		}
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

	// Damage sets the indicator strength for its whole lifetime: a 3-point graze
	// should whisper. (This used to be written once here and overwritten by the
	// tick on the very next frame, so every hit drew at full strength.)
	DamageIndicatorStrength = FMath::Clamp(Damage / 40.f, 0.25f, 1.f);
	Data.DamageIndicatorAlpha = DamageIndicatorStrength;

	// Keep the world position, not a bearing. A bearing frozen at the moment of
	// the hit is wrong the moment the player turns - and turning towards the
	// shooter is exactly what the indicator asks them to do. The zero vector is
	// the health component's "no instigator" (a fall, the building): no arc.
	bHasDamageInstigator = !InstigatorLocation.IsNearlyZero();
	DamageInstigatorLocation = InstigatorLocation;

	UpdateDamageDirection();

	// The painted HUD keeps its own arcs (several at once, each fading), and
	// the floor the shot came from: in a tower that is half of "where".
	if (HUDWidget != nullptr)
	{
		int32 FloorDelta = 0;
		if (bHasDamageInstigator)
		{
			const AOneBrahamCharacter* Character = BoundCharacter.Get();
			const int32 Here = Character != nullptr ? Character->GetCurrentFloorIndex() : Data.CurrentFloorIndex;
			FloorDelta = OneBrahamConstants::FloorIndexForZ(InstigatorLocation.Z - 90.f) - Here;
		}
		HUDWidget->PushDamage(Damage, InstigatorLocation, bHasDamageInstigator, FloorDelta);
	}
}

void UOneBrahamHUDComponent::UpdateDamageDirection()
{
	if (!bHasDamageInstigator || DamageIndicatorRemaining <= 0.f)
	{
		Data.DamageDirectionDegrees = -1000.f;
		return;
	}

	// Against the view, not the body: the control rotation is where the player is
	// looking, and the actor's facing lags it (and is meaningless while downed).
	// The camera manager has both, and still has them after the pawn has gone.
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	bool bHaveView = false;

	if (const APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		if (PC->PlayerCameraManager != nullptr)
		{
			ViewLocation = PC->PlayerCameraManager->GetCameraLocation();
			ViewRotation = PC->PlayerCameraManager->GetCameraRotation();
			bHaveView = true;
		}
		else if (const APawn* Pawn = PC->GetPawn())
		{
			ViewLocation = Pawn->GetActorLocation();
			ViewRotation = PC->GetControlRotation();
			bHaveView = true;
		}
	}

	if (!bHaveView)
	{
		if (const AOneBrahamCharacter* Character = BoundCharacter.Get())
		{
			ViewLocation = Character->GetEyeLocation();
			ViewRotation = Character->GetEyeRotation();
			bHaveView = true;
		}
	}

	// The horizontal plane only: an indicator that points up or down tells the
	// player nothing they can act on.
	const FVector ToInstigator = DamageInstigatorLocation - ViewLocation;
	const FVector Flat(ToInstigator.X, ToInstigator.Y, 0.f);

	if (!bHaveView || Flat.IsNearlyZero())
	{
		Data.DamageDirectionDegrees = -1000.f;
		return;
	}

	const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
	Data.DamageDirectionDegrees = FRotator::NormalizeAxis(TargetAngle - ViewRotation.Yaw);
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

	if (HUDWidget != nullptr)
	{
		HUDWidget->SetPlanOpen(bFloorPlanVisible);
	}

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
	FloorPlanWidget->SetBuildingFrame(Frame);
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
		FloorPlanWidget->SetBuildingFrame(Frame);
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

	if (const UOneBrahamProximityVoiceComponent* Voice = Character->FindComponentByClass<UOneBrahamProximityVoiceComponent>())
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

// ==========================================================================
//  The building's half of the snapshot
// ==========================================================================
namespace OneBrahamHUDFrameRules
{
	FString CategoryWord(EOneBrahamWeaponCategory Category)
	{
		switch (Category)
		{
		case EOneBrahamWeaponCategory::Pistol:
		case EOneBrahamWeaponCategory::CompactPistol:		return TEXT("PISTOL");
		case EOneBrahamWeaponCategory::SuppressedPistol:	return TEXT("SUPPRESSED");
		case EOneBrahamWeaponCategory::SMG:					return TEXT("SMG");
		case EOneBrahamWeaponCategory::PDW:					return TEXT("PDW");
		case EOneBrahamWeaponCategory::PumpShotgun:
		case EOneBrahamWeaponCategory::SemiAutoShotgun:		return TEXT("SHOTGUN");
		case EOneBrahamWeaponCategory::Carbine556:			return TEXT("CARBINE");
		case EOneBrahamWeaponCategory::CompactAssaultRifle:	return TEXT("RIFLE");
		case EOneBrahamWeaponCategory::MarksmanRifle:		return TEXT("MARKSMAN");
		default:											return FString();
		}
	}

	/** A weapon id as the sign-out log prints it: "MP7", "M4 CARBINE". */
	FString WeaponWord(FName WeaponId)
	{
		FString Id = WeaponId.ToString();
		Id.RemoveFromStart(TEXT("Weapon_"));
		Id.RemoveFromStart(TEXT("WPN_"));
		if (Id == TEXT("None") || Id.IsEmpty())
		{
			return FString();
		}
		// "M4Carbine" -> "M4 CARBINE": a space before a capital that follows a
		// lower-case letter, so model numbers stay whole.
		FString Out;
		for (int32 Index = 0; Index < Id.Len(); ++Index)
		{
			const TCHAR C = Id[Index];
			if (Index > 0 && FChar::IsUpper(C) && FChar::IsLower(Id[Index - 1]))
			{
				Out.AppendChar(TEXT(' '));
			}
			Out.AppendChar(C == TEXT('_') ? TEXT(' ') : C);
		}
		return Out.ToUpper();
	}
}

void UOneBrahamHUDComponent::GatherHUDFrame()
{
	using namespace OneBrahamHUDFrameRules;

	UWorld* World = GetWorld();
	AOneBrahamPlayerController* PC = OwningController.Get();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;

	Frame.bValid = GS != nullptr;
	if (GS == nullptr)
	{
		return;
	}

	Frame.Mode = GS->MatchMode;
	Frame.Phase = GS->MatchPhase;
	FOneBrahamBuildingStatus::Gather(World, PC, SealCeilingHint, Frame.Building);
	Frame.DuskClock = OneBrahamDuskClock::Now(World).ToString();
	Frame.TimeRemaining = GS->MatchTimeLimitSeconds > 0.f ? FMath::Max(0.f, GS->MatchTimeLimitSeconds - GS->PhaseElapsedSeconds) : 0.f;

	// --- settings --------------------------------------------------------------------
	if (const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Frame.bReduceMotion = Settings->bReduceMotion;
		Frame.HUDScale = FMath::Clamp(Settings->HUDScale, 0.8f, 1.2f);
		Frame.SubtitleScale = Settings->SubtitleScale;

		// "HUD rest dimming" is an accessibility switch the settings screen owns;
		// read by name so the HUD honours it from the moment it exists, and
		// rests by default until then.
		static const FBoolProperty* RestDimming = FindFProperty<FBoolProperty>(UOneBrahamGameUserSettings::StaticClass(), TEXT("bHUDRestDimming"));
		Frame.bRestDimming = RestDimming != nullptr ? RestDimming->GetPropertyValue_InContainer(Settings) : true;
	}

	// --- who the player is ----------------------------------------------------------------
	const AOneBrahamPlayerState* LocalState = PC != nullptr ? PC->GetPlayerState<AOneBrahamPlayerState>() : nullptr;
	if (LocalState != nullptr)
	{
		Frame.PlayerName = LocalState->GetPlayerName();
		Frame.RoleLabel = UOneBrahamScoreboardWidget::ArchetypeLabel(LocalState->ArchetypeTag).ToString();
		if (Frame.RoleLabel == TEXT("-"))
		{
			Frame.RoleLabel.Reset();
		}
	}

	// --- the weapon's family and its modes ------------------------------------------------
	Frame.WeaponCategory.Reset();
	Frame.FireModes.Reset();
	Frame.bAiming = false;
	if (UOneBrahamWeaponComponent* Weapons = CachedWeapons.Get())
	{
		Frame.bAiming = Weapons->IsAimingDownSights();
		if (const AOneBrahamWeaponBase* Weapon = Weapons->GetCurrentWeapon())
		{
			if (const UOneBrahamWeaponDefinition* Definition = Weapon->Definition)
			{
				Frame.WeaponCategory = CategoryWord(Definition->Category);
				Frame.FireModes = Definition->AvailableFireModes;
			}
		}
	}

	// --- voice ----------------------------------------------------------------------------
	switch (Data.VoiceChannel)
	{
	case EOneBrahamVoiceChannel::Squad:	Frame.VoiceChannelWord = OneBrahamHUDStrings::Get(TEXT("HUD_Voice_Squad")).ToString(); break;
	case EOneBrahamVoiceChannel::Team:	Frame.VoiceChannelWord = OneBrahamHUDStrings::Get(TEXT("HUD_Voice_Team")).ToString(); break;
	default:							Frame.VoiceChannelWord = OneBrahamHUDStrings::Get(TEXT("HUD_Voice_Proximity")).ToString(); break;
	}
	Frame.VoiceRangeMetres = 0;
	if (Data.VoiceChannel == EOneBrahamVoiceChannel::Proximity)
	{
		if (const UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(PC))
		{
			Frame.VoiceRangeMetres = FMath::RoundToInt(Voice->GetProximityRangeCm() / 100.f);
		}
	}

	// --- the extraction --------------------------------------------------------------------
	const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent();
	Frame.ExtractionState = Extraction != nullptr ? Extraction->GetExtractionState() : EOneBrahamExtractionState::Inactive;
	Frame.ExtractionSeconds = Extraction != nullptr ? Extraction->GetSecondsRemainingInState() : 0.f;
	Frame.bExtractionContested = Extraction != nullptr && Extraction->IsContested();
	Frame.bOwnSquadHolding = Extraction != nullptr && LocalState != nullptr && Extraction->IsSquadHoldingExtraction(LocalState->SquadId);
	Frame.ExtractionProgress01 = 0.f;
	if (Extraction != nullptr)
	{
		Frame.ExtractionProgress01 = Frame.ExtractionState == EOneBrahamExtractionState::Holding
			? Extraction->GetHoldProgress01() : Extraction->GetArrivalAlpha01();
	}

	// --- FIRE DRILL ---------------------------------------------------------------------------
	Frame.Points.Reset();
	if (Frame.Mode == EOneBrahamMatchMode::FireDrill)
	{
		const int32 TeamIndex = LocalState != nullptr && LocalState->Team == EOneBrahamTeam::ExternalContractors ? 1 : 0;
		Frame.ScoreOurs = GS->TeamScores.IsValidIndex(TeamIndex) ? GS->TeamScores[TeamIndex] : 0;
		Frame.ScoreTheirs = GS->TeamScores.IsValidIndex(1 - TeamIndex) ? GS->TeamScores[1 - TeamIndex] : 0;

		if (const UOneBrahamMatchSettingsAsset* Settings = GS->GetMatchSettings())
		{
			Frame.ScoreToWin = Settings->ScoreToWin;
			for (int32 Index = 0; Index < Settings->ControlPoints.Num(); ++Index)
			{
				const FOneBrahamControlPointDef& Def = Settings->ControlPoints[Index];
				FOneBrahamPointView& Point = Frame.Points.AddDefaulted_GetRef();
				Point.Letter = FString::Chr(TEXT('A') + Index);
				Point.Name = Def.DisplayName.ToString();
				Point.Floor = Def.FloorIndex;
				for (const FOneBrahamSquadMarker& Mate : Frame.Building.Squad)
				{
					Point.bYoursPresent |= Mate.Floor == Def.FloorIndex && !Mate.bDowned;
				}
			}
		}
	}

	// --- the in/out board ---------------------------------------------------------------------
	Frame.Squad.Reset();
	for (const FOneBrahamHUDSquadMember& Member : Data.Squad)
	{
		FOneBrahamSquadRowView& Row = Frame.Squad.AddDefaulted_GetRef();
		Row.PlayerId = Member.PlayerId;
		Row.Name = Member.PlayerName;
		Row.Floor = Member.FloorIndex;
		Row.LifeState = Member.LifeState;
		Row.HealthFraction = Member.HealthFraction;
		Row.BleedOutRemaining = Member.BleedOutFraction * OneBrahamConstants::BleedOutSeconds;
		Row.bTransmitting = Member.bIsTransmitting;
		Row.bLocal = Member.bIsLocalPlayer;
		Row.bKeycard = Member.bIsLocalPlayer && Data.HighestKeycardTier != EOneBrahamAccessTier::Public;
	}

	// --- the sign-out log ----------------------------------------------------------------------
	Frame.Kills.Reset();
	const float PurgeAt = OneBrahamDuskClock::PurgeAtSeconds(World);
	const double ServerNow = GS->GetServerWorldTimeSeconds();
	for (int32 Index = 0; Index < GS->KillFeed.Num(); ++Index)
	{
		const FOneBrahamKillFeedEntry& Entry = GS->KillFeed[Index];
		FOneBrahamKillRowView& Row = Frame.Kills.AddDefaulted_GetRef();
		const float Ago = Entry.Timestamp > 0.f ? FMath::Clamp(static_cast<float>(ServerNow - Entry.Timestamp), 0.f, 600.f) : 0.f;
		Row.Time = OneBrahamDuskClock::MatchSecondsToClock(GS->PhaseElapsedSeconds - Ago, PurgeAt).ToString();
		Row.Killer = Entry.KillerName;
		Row.Victim = Entry.VictimName;
		Row.Weapon = WeaponWord(Entry.WeaponId);
		Row.bHeadshot = Entry.Verb == EOneBrahamKillFeedVerb::Headshot;
		Row.bBuilding = Entry.Verb == EOneBrahamKillFeedVerb::Building;
		Row.Stage = Frame.Building.Stage;
		Row.EventTime = Data.KillFeed.IsValidIndex(Index) ? Data.KillFeed[Index].EventTime : World->GetTimeSeconds();
		for (const FOneBrahamSquadRowView& Mate : Frame.Squad)
		{
			Row.bKillerSquad |= Mate.Name == Entry.KillerName;
			Row.bVictimSquad |= Mate.Name == Entry.VictimName;
		}
		Row.bKillerYou = !Frame.PlayerName.IsEmpty() && Entry.KillerName == Frame.PlayerName;
		Row.bVictimYou = !Frame.PlayerName.IsEmpty() && Entry.VictimName == Frame.PlayerName;
	}

	// --- the compass's markers ---------------------------------------------------------------------
	Frame.Compass.Reset();
	const int32 LocalFloor = Frame.Building.LocalFloor;
	FVector Eye = FVector::ZeroVector;
	if (PC != nullptr && PC->PlayerCameraManager != nullptr)
	{
		Eye = PC->PlayerCameraManager->GetCameraLocation();
	}

	for (const FOneBrahamSquadMarker& Mate : Frame.Building.Squad)
	{
		if (Mate.bLocal || !Mate.bHasLocation)
		{
			continue;
		}
		FOneBrahamCompassMarkView& Mark = Frame.Compass.AddDefaulted_GetRef();
		Mark.Type = EOneBrahamCompassMark::Squad;
		Mark.WorldLocation = Mate.Location;
		FString Clean = Mate.Name;
		Clean.RemoveFromStart(TEXT("BOT "));
		Mark.Label = Clean.Left(2).ToUpper();
		Mark.FloorDelta = LocalFloor != INDEX_NONE && Mate.Floor != INDEX_NONE ? Mate.Floor - LocalFloor : 0;
		Mark.DistanceCm = FVector::Dist(Eye, Mate.Location);
		Mark.bAlert = Mate.bDowned;
	}

	for (const FOneBrahamHUDWorldPing& Ping : Data.WorldPings)
	{
		FOneBrahamCompassMarkView& Mark = Frame.Compass.AddDefaulted_GetRef();
		Mark.Type = EOneBrahamCompassMark::Ping;
		Mark.WorldLocation = Ping.Location;
		Mark.SpawnTime = Ping.SpawnTime;
		Mark.FloorDelta = LocalFloor != INDEX_NONE ? OneBrahamConstants::FloorIndexForZ(Ping.Location.Z) - LocalFloor : 0;
		Mark.DistanceCm = FVector::Dist(Eye, Ping.Location);
	}

	if (Frame.Mode == EOneBrahamMatchMode::Ascent && Extraction != nullptr && !Extraction->GetHelipadLocation().IsNearlyZero())
	{
		FOneBrahamCompassMarkView& Mark = Frame.Compass.AddDefaulted_GetRef();
		Mark.Type = EOneBrahamCompassMark::Extraction;
		Mark.WorldLocation = Extraction->GetHelipadLocation();
		Mark.FloorDelta = LocalFloor != INDEX_NONE ? Frame.Building.ExtractionFloor - LocalFloor : 0;
		Mark.DistanceCm = FVector::Dist(Eye, Mark.WorldLocation);
	}

	// In a lockdown, the nearest stair core: the way up is the only thing that
	// matters, and the lifts have stopped. A core on a sealed floor is struck.
	if (Frame.Mode == EOneBrahamMatchMode::Ascent && OneBrahamHUDStyle::StageBand(Frame.Building.Stage) > 0 && LocalFloor != INDEX_NONE
		&& LocalFloor < OneBrahamConstants::FloorTop)
	{
		using namespace OneBrahamBotRules;
		const FVector A = RouteDoorPoint(EClimbRoute::FireStairA, LocalFloor);
		const FVector B = RouteDoorPoint(EClimbRoute::SpiralB, LocalFloor);
		const bool bAIsNearer = FVector::DistSquared(Eye, A) <= FVector::DistSquared(Eye, B);
		const EOneBrahamFloorCondition Here = Frame.Building.ConditionOf(LocalFloor);

		FOneBrahamCompassMarkView& Mark = Frame.Compass.AddDefaulted_GetRef();
		Mark.Type = EOneBrahamCompassMark::Stair;
		Mark.WorldLocation = bAIsNearer ? A : B;
		Mark.Label = bAIsNearer ? TEXT("STAIR A") : TEXT("STAIR B");
		Mark.DistanceCm = FVector::Dist(Eye, Mark.WorldLocation);
		Mark.bStruck = Here == EOneBrahamFloorCondition::Sealed || Here == EOneBrahamFloorCondition::Purged;
	}

	// --- spectating ---------------------------------------------------------------------------------
	Frame.SpectatingName.Reset();
	Frame.SpectatingFloor = INDEX_NONE;
	if (PC != nullptr && LocalState != nullptr && LocalState->IsEliminated())
	{
		if (const APawn* Watched = Cast<APawn>(PC->GetViewTarget()))
		{
			if (const APlayerState* WatchedState = Watched->GetPlayerState())
			{
				Frame.SpectatingName = WatchedState->GetPlayerName();
				Frame.SpectatingFloor = OneBrahamConstants::FloorIndexForZ(Watched->GetActorLocation().Z - 90.f);
			}
		}
	}

	BuildObjective(GS);
}

void UOneBrahamHUDComponent::BuildObjective(const AOneBrahamGameState* GS)
{
	Frame.Objective = FText::GetEmpty();
	Frame.ObjectiveDetail.Reset();

	const FOneBrahamBuildingStatus& Building = Frame.Building;
	const int32 Here = Building.LocalFloor;
	const int32 Target = Building.ExtractionFloor;
	const int32 ToGo = Here != INDEX_NONE ? Target - Here : 0;
	auto FloorsToGo = [&]()
	{
		return ToGo > 0 ? FString::Printf(TEXT("%d %s"), ToGo, ToGo == 1 ? TEXT("FLOOR") : TEXT("FLOORS")) : FString();
	};

	if (Frame.Mode == EOneBrahamMatchMode::NightShift)
	{
		Frame.Objective = NSLOCTEXT("OneBraham", "HUD_Obj_NightShift", "Survive the night shift");
		Frame.ObjectiveDetail = Frame.DuskClock;
		return;
	}
	if (Frame.Mode != EOneBrahamMatchMode::Ascent)
	{
		return;
	}

	if (Frame.Phase == EOneBrahamMatchPhase::Warmup)
	{
		Frame.Objective = OneBrahamHUDStrings::Get(TEXT("HUD_Objective_SignIn"));
		return;
	}

	switch (Frame.ExtractionState)
	{
	case EOneBrahamExtractionState::Broadcast:
	case EOneBrahamExtractionState::Arriving:
		Frame.Objective = OneBrahamHUDStrings::Get(TEXT("HUD_Objective_Winch"));
		Frame.ObjectiveDetail = FloorsToGo();
		return;
	case EOneBrahamExtractionState::Holding:
		Frame.Objective = OneBrahamHUDStrings::Get(TEXT("HUD_Objective_Hold"));
		Frame.ObjectiveDetail = FloorsToGo();
		return;
	default:
		break;
	}

	switch (Building.Stage)
	{
	case EOneBrahamLockdownStage::PreLockdown:
	case EOneBrahamLockdownStage::Lockdown:
		Frame.Objective = NSLOCTEXT("OneBraham", "HUD_Obj_Lockdown", "The lift lobby is closed. Use the stairs.");
		break;
	case EOneBrahamLockdownStage::Purge:
		Frame.Objective = FText::Format(NSLOCTEXT("OneBraham", "HUD_Obj_Purge", "Get above Level {0}. Now."),
			FText::AsNumber(Building.PurgeCeiling));
		break;
	default:
		Frame.Objective = Here >= Target
			? NSLOCTEXT("OneBraham", "HUD_Obj_Arrived", "Call the extraction from the Skydeck terminal")
			: FText::Format(NSLOCTEXT("OneBraham", "HUD_Obj_Ascend", "{0}  ·  Level {1}"),
				OneBrahamHUDStrings::Get(TEXT("HUD_Objective_Ascend")), FText::AsNumber(Target));
		break;
	}
	Frame.ObjectiveDetail = FloorsToGo();
}

// ==========================================================================
//  The replay: what this machine saw, every two seconds
// ==========================================================================
void UOneBrahamHUDComponent::RecordReplay(float DeltaTime)
{
	const UWorld* World = GetWorld();
	const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}

	// A new game state is a new match.
	if (ReplayGameState.Get() != GS)
	{
		ReplayGameState = GS;
		Replay.Reset();
		ReplayKillKeys.Reset();
		ReplayAccumulator = FOneBrahamMatchReplay::SampleSeconds;
	}

	const bool bLive = GS->MatchPhase == EOneBrahamMatchPhase::Active || GS->MatchPhase == EOneBrahamMatchPhase::Extraction;
	if (!bLive)
	{
		return;
	}

	const float Now = GS->PhaseElapsedSeconds;
	Replay.DurationSeconds = FMath::Max(Replay.DurationSeconds, Now);
	Replay.PurgeCeiling = Frame.Building.PurgeCeiling;
	if (Frame.Building.Stage == EOneBrahamLockdownStage::Purge && Replay.PurgeAtSeconds < 0.f)
	{
		Replay.PurgeAtSeconds = Now;
	}

	// Kills by the local squad, each once, on the floor the killer was on.
	for (const FOneBrahamKillFeedEntry& Entry : GS->KillFeed)
	{
		const FString Key = FString::Printf(TEXT("%s>%s@%.1f"), *Entry.KillerName, *Entry.VictimName, Entry.Timestamp);
		if (ReplayKillKeys.Contains(Key))
		{
			continue;
		}
		ReplayKillKeys.Add(Key);
		for (const FOneBrahamSquadMarker& Mate : Frame.Building.Squad)
		{
			if (Mate.Name == Entry.KillerName && Mate.Floor != INDEX_NONE)
			{
				FOneBrahamReplayKill& Kill = Replay.Kills.AddDefaulted_GetRef();
				Kill.MatchSeconds = Now;
				Kill.KillerId = Mate.PlayerId;
				Kill.Floor = Mate.Floor;
			}
		}
	}

	ReplayAccumulator += DeltaTime;
	if (ReplayAccumulator < FOneBrahamMatchReplay::SampleSeconds)
	{
		return;
	}
	ReplayAccumulator = 0.f;

	FOneBrahamReplaySample& Sample = Replay.Samples.AddDefaulted_GetRef();
	Sample.MatchSeconds = Now;
	Sample.bPurge = Frame.Building.Stage == EOneBrahamLockdownStage::Purge;
	Sample.SealCeiling = Sample.bPurge ? Frame.Building.PurgeCeiling : Frame.Building.SealCeiling;

	const AOneBrahamPlayerController* PC = OwningController.Get();
	const AOneBrahamPlayerState* LocalState = PC != nullptr ? PC->GetPlayerState<AOneBrahamPlayerState>() : nullptr;

	for (const APlayerState* Base : GS->PlayerArray)
	{
		const AOneBrahamPlayerState* Other = Cast<AOneBrahamPlayerState>(Base);
		const AOneBrahamCharacter* Pawn = Other != nullptr ? Cast<AOneBrahamCharacter>(Other->GetPawn()) : nullptr;
		if (Other == nullptr || Pawn == nullptr || Other->IsEliminated())
		{
			continue;
		}

		Sample.Floors.Emplace(Other->GetPlayerId(), Pawn->GetCurrentFloorIndex());

		if (Replay.FindPerson(Other->GetPlayerId()) == nullptr)
		{
			FOneBrahamReplayPerson& Person = Replay.People.AddDefaulted_GetRef();
			Person.PlayerId = Other->GetPlayerId();
			Person.Name = Other->GetPlayerName();
			Person.SquadId = Other->SquadId;
			Person.bLocal = Other == LocalState;
			Person.bLocalSquad = LocalState != nullptr && LocalState->SquadId != INDEX_NONE && Other->SquadId == LocalState->SquadId;
		}
	}
}

// ==========================================================================
//  The building's screens say what the tannoy says
// ==========================================================================
void UOneBrahamHUDComponent::SyncBuildingDisplays(const FText& Headline, const FText& Subline, EOneBrahamLockdownStage Stage, float HoldSeconds)
{
	// Every escalator, ceiling and reception OLED in the building shows the
	// line on the same frame the card goes up. The displays are client-side
	// actors, so each machine drives its own copies; nothing replicates.
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FOneBrahamDisplaySlide Slide;
	Slide.Eyebrow = FText::FromString(FString::Printf(TEXT("ONE BRAHAM  ·  ALL FLOORS  ·  %s"), *Frame.DuskClock));
	Slide.Headline = Headline;
	Slide.Subline = Subline;
	// The screens take the HUD's colour rule: the same tone as the card.
	const bool bEvacuating = Frame.ExtractionState == EOneBrahamExtractionState::Broadcast
		|| Frame.ExtractionState == EOneBrahamExtractionState::Arriving
		|| Frame.ExtractionState == EOneBrahamExtractionState::Holding;
	UOneBrahamUIStyle::ToneField(UOneBrahamUIStyle::StageTone(Stage, bEvacuating), Slide.ColourA, Slide.ColourB);

	int32 Count = 0;
	for (TActorIterator<AOneBrahamDisplay> It(GetWorld()); It; ++It)
	{
		// AOneBrahamDisplay::PushOverride (the front end's title designer) is
		// the call; until it lands the displays keep their own slides.
		++Count;
	}
	(void)HoldSeconds;

	UE_LOG(LogOneBrahamUI, Verbose, TEXT("[OB.UI] Announcement on %d building displays: %s"), Count, *Headline.ToString());
}

// ==========================================================================
//  Dev: demonstration states, for screenshots of moments a run cannot reach
// ==========================================================================
void UOneBrahamHUDComponent::DevDemo(const FString& State)
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	DemoState = State.ToLower();
	DemoStartedAt = World != nullptr ? World->GetTimeSeconds() : 0.0;
	bDemoOneShotsDone = false;

	if (DemoState == TEXT("off"))
	{
		DemoState.Reset();
		if (MatchEndWidget != nullptr && (MatchEndGameState.Get() == nullptr || MatchEndGameState.Get() == World->GetGameState()))
		{
			CloseMatchEndScreen();
		}
		SetScoreboardVisible(false);
		SetFloorPlanVisible(false);
		SetDeathScreenVisible(false);
		if (AOneBrahamPlayerController* Controller = OwningController.Get())
		{
			Controller->SetPauseMenuOpen(false);
		}
		return;
	}

	// Each demonstration starts from a living player: the accident book from a
	// previous "death" would otherwise sit over every state that follows.
	if (DemoState != TEXT("death"))
	{
		SetDeathScreenVisible(false);
	}
	if (DemoState != TEXT("plan"))
	{
		SetFloorPlanVisible(false);
	}
	if (DemoState != TEXT("scoreboard"))
	{
		SetScoreboardVisible(false);
	}

	if (DemoState == TEXT("scoreboard"))
	{
		SetScoreboardVisible(true);
	}
	else if (DemoState == TEXT("pause"))
	{
		if (AOneBrahamPlayerController* Controller = OwningController.Get())
		{
			Controller->SetPauseMenuOpen(true);
		}
	}
	else if (DemoState == TEXT("plan"))
	{
		SetFloorPlanVisible(true);
	}
	else if (DemoState == TEXT("death"))
	{
		SetDeathScreenVisible(true, FText::FromString(TEXT(
			"Priya Shah - MP7, headshot\n\n"
			"Priya Shah - 4 hits, 112 damage (1 head, 3 body) at 18 m\n"
			"Dave Okafor - 1 hit, 22 damage (1 limb) at 31 m\n"
			"Priya Shah had 64 health left")));
	}
	else if (DemoState == TEXT("matchend"))
	{
		// The end screen over the live match, fed a fabricated day: three
		// squads climbing, the lockdown rising under them, a purge at the end.
		AOneBrahamPlayerController* Controller = OwningController.Get();
		if (Controller != nullptr && MatchEndWidget == nullptr)
		{
			FOneBrahamMatchReplay Demo;
			Demo.DurationSeconds = 600.f;
			Demo.PurgeAtSeconds = 470.f;
			Demo.PurgeCeiling = 14;
			for (int32 Person = 0; Person < 12; ++Person)
			{
				FOneBrahamReplayPerson& P = Demo.People.AddDefaulted_GetRef();
				P.PlayerId = 900 + Person;
				P.Name = FString::Printf(TEXT("Colleague %d"), Person + 1);
				P.SquadId = Person / 4;
				P.bLocalSquad = P.SquadId == 0;
				P.bLocal = Person == 0;
			}
			for (float T = 0.f; T <= 600.f; T += 2.f)
			{
				FOneBrahamReplaySample& Sample = Demo.Samples.AddDefaulted_GetRef();
				Sample.MatchSeconds = T;
				Sample.bPurge = T >= 470.f;
				Sample.SealCeiling = Sample.bPurge ? 14 : (T > 310.f ? 8 : (T > 160.f ? 3 : INDEX_NONE));
				for (const FOneBrahamReplayPerson& P : Demo.People)
				{
					const float Pace = 0.026f + 0.006f * P.SquadId + 0.0015f * (P.PlayerId % 4);
					const int32 Floor = FMath::Clamp(FMath::FloorToInt(T * Pace + FMath::Sin(T * 0.03f + P.PlayerId) * 0.8f), 0, 15);
					const bool bGone = (P.SquadId == 2 && T > 420.f) || (P.SquadId == 1 && P.PlayerId % 4 == 3 && T > 280.f);
					if (!bGone)
					{
						Sample.Floors.Emplace(P.PlayerId, Floor);
					}
				}
			}
			for (const float T : { 140.f, 251.f, 333.f, 402.f, 515.f, 560.f })
			{
				FOneBrahamReplayKill& Kill = Demo.Kills.AddDefaulted_GetRef();
				Kill.MatchSeconds = T;
				Kill.KillerId = 900 + static_cast<int32>(T) % 4;
				Kill.Floor = FMath::Clamp(FMath::FloorToInt(T * 0.027f), 0, 15);
			}

			SetHUDVisible(false);
			MatchEndWidget = CreateWidget<UOneBrahamMatchEndWidget>(Controller, UOneBrahamMatchEndWidget::StaticClass());
			if (MatchEndWidget != nullptr)
			{
				MatchEndWidget->AddToPlayerScreen(MatchEndZOrder);
				MatchEndWidget->SetReplay(Demo, Frame);
				MatchEndWidget->DevSetDemoEnding(true);
				MatchEndWidget->ShowMatchEnd();
			}
		}
	}

	UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD demo: %s"), *DemoState);
#else
	(void)State;
#endif
}

void UOneBrahamHUDComponent::ApplyDevDemo()
{
#if !UE_BUILD_SHIPPING
	if (DemoState.IsEmpty() || DemoState == TEXT("scoreboard") || DemoState == TEXT("pause") || DemoState == TEXT("matchend"))
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	const float Age = static_cast<float>(Now - DemoStartedAt);
	const AOneBrahamPlayerController* PC = OwningController.Get();
	const FVector Eye = PC != nullptr && PC->PlayerCameraManager != nullptr ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FRotator View = PC != nullptr && PC->PlayerCameraManager != nullptr ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
	const FVector Forward = FRotator(0.f, View.Yaw, 0.f).Vector();
	const FVector Right = FRotator(0.f, View.Yaw + 90.f, 0.f).Vector();

	// --- the stage, the floor, the seal line ---------------------------------------------
	FOneBrahamBuildingStatus& B = Frame.Building;
	int32 Local = 7;
	B.Mode = EOneBrahamMatchMode::Ascent;
	B.Stage = EOneBrahamLockdownStage::Normal;
	B.bDirectorRunning = true;
	B.SecondsToNextStage = FMath::Max(0.f, 161.f - Age);
	B.SealCeiling = INDEX_NONE;
	B.NextSealCeiling = 5;
	B.PurgeCeiling = 14;
	Frame.Mode = EOneBrahamMatchMode::Ascent;
	Frame.Phase = EOneBrahamMatchPhase::Active;
	Frame.ExtractionState = EOneBrahamExtractionState::Inactive;

	if (DemoState == TEXT("prelockdown"))
	{
		// The building's warning: nothing sealed yet, the first shutters coming.
		B.Stage = EOneBrahamLockdownStage::PreLockdown;
		B.SealCeiling = INDEX_NONE;
		B.NextSealCeiling = 3;
		B.SecondsToNextStage = FMath::Max(0.f, 28.f - Age * 0.25f);
		Local = 4;
	}
	else if (DemoState == TEXT("lockdown") || DemoState == TEXT("critical") || DemoState == TEXT("plan"))
	{
		B.Stage = EOneBrahamLockdownStage::Lockdown;
		B.SealCeiling = 4;
		B.NextSealCeiling = DemoState == TEXT("plan") ? 14 : 6;
		B.SecondsToNextStage = FMath::Max(0.f, 24.f - Age * 0.25f);
		Local = DemoState == TEXT("critical") ? 9 : 6;
	}
	else if (DemoState == TEXT("purge") || DemoState == TEXT("downed"))
	{
		B.Stage = EOneBrahamLockdownStage::Purge;
		B.SealCeiling = INDEX_NONE;
		B.NextSealCeiling = INDEX_NONE;
		B.SecondsToNextStage = 0.f;
		Local = 13;
	}
	else if (DemoState == TEXT("extraction"))
	{
		B.Stage = EOneBrahamLockdownStage::Lockdown;
		B.SealCeiling = 11;
		B.NextSealCeiling = 14;
		B.SecondsToNextStage = 48.f;
		Local = 15;
		Frame.ExtractionState = EOneBrahamExtractionState::Holding;
		Frame.ExtractionSeconds = FMath::Max(0.f, 27.f - Age);
		Frame.ExtractionProgress01 = FMath::Clamp(0.1f + Age / 30.f, 0.f, 1.f);
		Frame.bExtractionContested = true;
	}
	else if (DemoState == TEXT("firedrill"))
	{
		Frame.Mode = B.Mode = EOneBrahamMatchMode::FireDrill;
		B.Stage = EOneBrahamLockdownStage::Normal;
		B.bDirectorRunning = false;
		B.NextSealCeiling = INDEX_NONE;
		Local = 9;
		Frame.TimeRemaining = 372.f;
		Frame.ScoreOurs = 212;
		Frame.ScoreTheirs = 187;
		Frame.Points.Reset();
		const TCHAR* Names[3] = { TEXT("Reception"), TEXT("Server Floor 9"), TEXT("Skydeck") };
		const int32 Floors[3] = { 1, 9, 15 };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FOneBrahamPointView& Point = Frame.Points.AddDefaulted_GetRef();
			Point.Letter = FString::Chr(TEXT('A') + Index);
			Point.Name = Names[Index];
			Point.Floor = Floors[Index];
			Point.bYoursPresent = Floors[Index] == Local;
		}
	}

	// The plate's split-flap: the demo arrives on the floor below, then climbs.
	B.LocalFloor = Age < 1.2f ? Local - 1 : Local;
	B.HighestOccupiedFloor = Local;

	// --- the squad -------------------------------------------------------------------------------
	struct FMate { const TCHAR* Name; int32 Floor; EOneBrahamLifeState Life; float Health; bool bTalk; FVector Offset; };
	const bool bDrill = Frame.Mode == EOneBrahamMatchMode::FireDrill;
	TArray<FMate> Mates = {
		{ TEXT("Priya Shah"), Local, EOneBrahamLifeState::Alive, 0.82f, true, Forward * 900.f + Right * -300.f },
		{ TEXT("Dave Okafor"), Local + 2, DemoState == TEXT("lockdown") || DemoState == TEXT("plan") ? EOneBrahamLifeState::Downed : EOneBrahamLifeState::Alive, 0.35f, false, Forward * -400.f + Right * 1200.f },
		{ TEXT("BOT Brennan"), FMath::Max(0, Local - 3), DemoState == TEXT("purge") ? EOneBrahamLifeState::Dead : EOneBrahamLifeState::Alive, 1.f, false, Forward * 1500.f + Right * 700.f },
	};
	if (bDrill)
	{
		Mates.Add({ TEXT("BOT Kowalski"), 1, EOneBrahamLifeState::Alive, 0.6f, false, Forward * 2000.f });
		Mates.Add({ TEXT("BOT Lindqvist"), 15, EOneBrahamLifeState::Alive, 0.9f, false, Right * -1800.f });
	}

	Frame.Squad.Reset();
	B.Squad.Reset();
	{
		FOneBrahamSquadMarker& Me = B.Squad.AddDefaulted_GetRef();
		Me.Name = Frame.PlayerName;
		Me.Floor = B.LocalFloor;
		Me.bLocal = true;
		Me.PlayerId = -1;
		FOneBrahamSquadRowView& Row = Frame.Squad.AddDefaulted_GetRef();
		Row.Name = Frame.PlayerName;
		Row.bLocal = true;
		Row.Floor = B.LocalFloor;
	}
	for (int32 Index = 0; Index < Mates.Num(); ++Index)
	{
		const FMate& Mate = Mates[Index];
		FOneBrahamSquadRowView& Row = Frame.Squad.AddDefaulted_GetRef();
		Row.PlayerId = 800 + Index;
		Row.Name = Mate.Name;
		Row.Floor = Mate.Floor;
		Row.LifeState = Mate.Life;
		Row.HealthFraction = Mate.Health;
		Row.BleedOutRemaining = 24.f - Age;
		Row.bTransmitting = Mate.bTalk && FMath::Fmod(Age, 5.f) < 3.f;
		Row.bKeycard = Index == 0;

		if (Mate.Life == EOneBrahamLifeState::Dead)
		{
			continue;
		}
		FOneBrahamSquadMarker& Marker = B.Squad.AddDefaulted_GetRef();
		Marker.PlayerId = Row.PlayerId;
		Marker.Name = Mate.Name;
		Marker.Floor = Mate.Floor;
		Marker.bDowned = Mate.Life == EOneBrahamLifeState::Downed;
		Marker.Location = Eye + Mate.Offset + FVector(0.f, 0.f, (Mate.Floor - Local) * OneBrahamConstants::FloorHeightCm);
		Marker.bHasLocation = true;
	}

	// The floors, re-derived from the demo's line with the same rule.
	for (FOneBrahamFloorStatus& Floor : B.Floors)
	{
		Floor.Condition = bDrill ? EOneBrahamFloorCondition::Open
			: FOneBrahamBuildingStatus::ConditionFor(B.Stage, Floor.FloorIndex, B.SealCeiling, B.NextSealCeiling, B.PurgeCeiling);
		Floor.bLocal = Floor.FloorIndex == B.LocalFloor;
		Floor.bExtraction = !bDrill && Floor.FloorIndex == B.ExtractionFloor;
		Floor.Mates = 0;
		Floor.MatesDowned = 0;
		for (const FOneBrahamSquadMarker& Marker : B.Squad)
		{
			if (!Marker.bLocal && Marker.Floor == Floor.FloorIndex)
			{
				++Floor.Mates;
				Floor.MatesDowned += Marker.bDowned ? 1 : 0;
			}
		}
	}

	// --- the compass ------------------------------------------------------------------------------
	Frame.Compass.Reset();
	for (const FOneBrahamSquadMarker& Marker : B.Squad)
	{
		if (Marker.bLocal || !Marker.bHasLocation)
		{
			continue;
		}
		FOneBrahamCompassMarkView& Mark = Frame.Compass.AddDefaulted_GetRef();
		Mark.Type = EOneBrahamCompassMark::Squad;
		Mark.WorldLocation = Marker.Location;
		FString Clean = Marker.Name;
		Clean.RemoveFromStart(TEXT("BOT "));
		Mark.Label = Clean.Left(2).ToUpper();
		Mark.FloorDelta = Marker.Floor - B.LocalFloor;
		Mark.bAlert = Marker.bDowned;
		Mark.DistanceCm = (Marker.Location - Eye).Size();
	}
	if (!bDrill)
	{
		FOneBrahamCompassMarkView& Exit = Frame.Compass.AddDefaulted_GetRef();
		Exit.Type = EOneBrahamCompassMark::Extraction;
		Exit.WorldLocation = Eye + Forward * 3000.f + Right * 1600.f;
		Exit.FloorDelta = B.ExtractionFloor - B.LocalFloor;
		Exit.DistanceCm = 6200.f;
		FOneBrahamCompassMarkView& Ping = Frame.Compass.AddDefaulted_GetRef();
		Ping.Type = EOneBrahamCompassMark::Ping;
		Ping.WorldLocation = Eye + Forward * 1200.f + Right * -900.f;
		Ping.FloorDelta = 1;
		Ping.SpawnTime = static_cast<float>(Now) - FMath::Fmod(Age, 5.f);
		if (OneBrahamHUDStyle::StageBand(B.Stage) > 0)
		{
			FOneBrahamCompassMarkView& Stair = Frame.Compass.AddDefaulted_GetRef();
			Stair.Type = EOneBrahamCompassMark::Stair;
			Stair.WorldLocation = Eye + Forward * 800.f + Right * 500.f;
			Stair.Label = TEXT("STAIR A");
			Stair.bStruck = B.Stage == EOneBrahamLockdownStage::Purge;
		}
	}

	// --- the sign-out log -----------------------------------------------------------------------------
	Frame.Kills.Reset();
	auto AddDemoKill = [&](const TCHAR* Time, const TCHAR* Killer, const TCHAR* Victim, const TCHAR* Weapon, bool bHead, bool bBuilding, bool bKSquad, bool bVSquad, float Ago)
	{
		FOneBrahamKillRowView& Row = Frame.Kills.AddDefaulted_GetRef();
		Row.Time = Time;
		Row.Killer = Killer;
		Row.Victim = Victim;
		Row.Weapon = Weapon;
		Row.bHeadshot = bHead;
		Row.bBuilding = bBuilding;
		Row.bKillerSquad = bKSquad;
		Row.bVictimSquad = bVSquad;
		Row.Stage = B.Stage;
		Row.EventTime = static_cast<float>(Now) - FMath::Fmod(Ago + Age, 6.5f);
	};
	AddDemoKill(TEXT("18:02"), TEXT("Marcus Hale"), TEXT("Tom Reid"), TEXT("M4 CARBINE"), false, false, false, false, 5.f);
	AddDemoKill(TEXT("18:04"), TEXT("Priya Shah"), TEXT("Ana Lopes"), TEXT("MP7"), true, false, true, false, 3.f);
	if (B.Stage != EOneBrahamLockdownStage::Normal)
	{
		AddDemoKill(TEXT("18:06"), TEXT(""), TEXT("BOT Castell"), TEXT(""), false, true, false, false, 1.5f);
	}
	AddDemoKill(TEXT("18:06"), TEXT("Dave Okafor"), TEXT("BOT Patel"), TEXT("SPAS-12"), false, false, true, false, 0.2f);

	// --- the objective, the dusk ----------------------------------------------------------------------
	Frame.DuskClock = B.Stage == EOneBrahamLockdownStage::Purge ? TEXT("18:38") : (B.Stage == EOneBrahamLockdownStage::Normal ? TEXT("17:52") : TEXT("18:06"));
	if (Frame.RoleLabel.IsEmpty())
	{
		Frame.RoleLabel = TEXT("IT OPERATIONS");
	}
	if (Frame.WeaponCategory.IsEmpty())
	{
		Frame.WeaponCategory = TEXT("CARBINE");
	}
	if (Frame.FireModes.Num() < 2)
	{
		Frame.FireModes = { EOneBrahamFireMode::Single, EOneBrahamFireMode::Burst, EOneBrahamFireMode::Auto };
	}
	if (const AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr)
	{
		BuildObjective(GS);
		if (bDrill)
		{
			Frame.Objective = FText::GetEmpty();
		}
	}

	// --- the player ------------------------------------------------------------------------------------
	Data.bShowFloorIndicator = true;
	Data.FragCount = 2;
	Data.MedkitCount = 1;
	Data.FlashCount = DemoState == TEXT("calm") ? 1 : 0;
	Data.Armour = 50.f;
	Data.MaxArmour = 50.f;
	Data.Health = 82.f;
	Data.MaxHealth = 100.f;
	Data.LifeState = EOneBrahamLifeState::Alive;
	Data.bLocalTransmitting = DemoState == TEXT("calm") || DemoState == TEXT("firedrill");
	Data.VoiceChannel = EOneBrahamVoiceChannel::Proximity;
	Frame.VoiceRangeMetres = 35;
	Data.HighestKeycardTier = EOneBrahamAccessTier::Staff;

	if (DemoState == TEXT("critical"))
	{
		// A hit from 61 to 23: the ghost, the nudge, the critical breath.
		Data.Health = Age < 0.6f ? 61.f : 23.f;
		Data.Armour = Age < 0.6f ? 12.f : 0.f;
		Data.AmmoInMagazine = Age < 0.6f ? 9 : 7;
	}
	else if (DemoState == TEXT("downed"))
	{
		Data.LifeState = EOneBrahamLifeState::Downed;
		Data.Health = 0.f;
		Data.BleedOutRemaining = FMath::Max(0.f, 24.f - Age);
		Data.ReviveProgress01 = FMath::Clamp((Age - 2.f) / 4.5f, 0.f, 1.f);
	}

	if (DemoState == TEXT("extraction"))
	{
		Data.bHasInteractionTarget = true;
		Data.InteractionPrompt = FText::FromString(TEXT("Hold E - Hold the winch"));
		Data.InteractionHoldProgress01 = FMath::Fmod(Age * 0.35f, 1.f);
		Data.bInteractionBlocked = false;
	}
	else if (DemoState == TEXT("lockdown"))
	{
		Data.bHasInteractionTarget = true;
		Data.InteractionPrompt = FText::FromString(TEXT("Executive badge required"));
		Data.InteractionBlockedReason = Data.InteractionPrompt;
		Data.bInteractionBlocked = true;
	}
	else if (DemoState == TEXT("calm"))
	{
		Data.bHasInteractionTarget = true;
		Data.InteractionPrompt = FText::FromString(TEXT("Hold E - Executive Suite"));
		Data.InteractionHoldProgress01 = FMath::Clamp((Age - 1.f) * 0.3f, 0.f, 0.72f);
		Data.bInteractionBlocked = false;
	}

	// One-shot moments, on the demo's first tick.
	if (HUDWidget != nullptr && !bDemoOneShotsDone)
	{
		bDemoOneShotsDone = true;
		if (DemoState == TEXT("lockdown"))
		{
			HUDWidget->ShowStageCard(EOneBrahamStageCard::Lockdown);
			HUDWidget->PushAnnouncement(FText::FromString(TEXT("Fire shutters are closing below Level 6.")),
				FText::FromString(TEXT("The lifts are thinking about what they've done.")), EOneBrahamLockdownStage::Lockdown, 6.f);
			HUDWidget->ShowSubtitle(FText::FromString(TEXT("Fire shutters are closing on all floors below 6. Lifts are out of service. Please use the stairwell nearest to you.")),
				FString(), 7.f);
			// The pile-up that made the centre's priority queue: a stage card,
			// a PA card, a refused reader and the induction's step, all at
			// once (UI/HUD/OneBrahamCentreMessages.h decides who is shown).
			HUDWidget->ShowSubBanner(FText::FromString(TEXT("INDUCTION · Find the stairs - follow the exit signs")), 9.f);
		}
		else if (DemoState == TEXT("purge"))
		{
			HUDWidget->ShowStageCard(EOneBrahamStageCard::Purge);
		}
		else if (DemoState == TEXT("prelockdown"))
		{
			// The tannoy's paragraph and its card: the subtitle drops the
			// sentence the card is already showing.
			HUDWidget->PushAnnouncement(FText::FromString(TEXT("The building will enter lockdown shortly.")),
				FText::FromString(TEXT("Please do not prop fire doors open.")), EOneBrahamLockdownStage::PreLockdown, 6.f);
			HUDWidget->ShowSubtitle(FText::FromString(TEXT("Attention all staff. The building will enter lockdown shortly. Shutters will close on Levels 1 to 3.")),
				FString(), 7.f);
		}
		else if (DemoState == TEXT("extraction"))
		{
			HUDWidget->PushAnnouncement(FText::FromString(TEXT("The Skydeck winch is holding.")),
				FText::FromString(TEXT("Please mind the gap.")), EOneBrahamLockdownStage::Lockdown, 6.f);
		}
		else if (DemoState == TEXT("calm"))
		{
			HUDWidget->PushAnnouncement(FText::FromString(TEXT("The lift lobby is open. Ascend.")),
				FText::FromString(TEXT("Thank you for choosing One Braham.")), EOneBrahamLockdownStage::Normal, 6.f);
			HUDWidget->ShowSubtitle(FText::FromString(TEXT("Contact, two. Coming up the east stair.")), TEXT("Priya Shah"), 6.f);
		}
		else if (DemoState == TEXT("critical"))
		{
			HUDWidget->PushDamage(38.f, Eye + Right * 900.f + FVector(0.f, 0.f, OneBrahamConstants::FloorHeightCm), true, 1);
			HUDWidget->PushDamage(14.f, Eye + Forward * -800.f + Right * -400.f, true, 0);
		}
	}
	if (HUDWidget != nullptr && DemoState == TEXT("critical") && FMath::Fmod(Age, 1.1f) < 0.07f && Age > 0.5f)
	{
		HUDWidget->PushDamage(24.f, Eye + Right * 900.f + FVector(0.f, 0.f, OneBrahamConstants::FloorHeightCm), true, 1);
	}
#endif
}

#if !UE_BUILD_SHIPPING
namespace OneBrahamHUDDev
{
	UOneBrahamHUDComponent* FindHUD(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get(); PC != nullptr && PC->IsLocalController())
			{
				if (UOneBrahamHUDComponent* HUD = PC->FindComponentByClass<UOneBrahamHUDComponent>())
				{
					return HUD;
				}
			}
		}
		return nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs DemoCommand(
		TEXT("ob.hud.demo"),
		TEXT("Dev: puts the HUD in a demonstration state (calm, prelockdown, lockdown, purge, extraction, critical, downed, death, firedrill, scoreboard, pause, plan, matchend, off)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOneBrahamHUDComponent* HUD = FindHUD(World))
			{
				HUD->DevDemo(Args.Num() > 0 ? Args[0] : FString(TEXT("calm")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ShotCommand(
		TEXT("ob.hud.shot"),
		TEXT("Dev: saves a screenshot with the UI to Saved/Screenshots/<platform>/<name>.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FString Name = Args.Num() > 0 ? Args[0] : FString(TEXT("hud"));
			const FString Path = FPaths::ScreenShotDir() / (Name + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddUniqueSuffix*/ false);
			UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD screenshot requested: %s"), *Path);
		}));

	/** The local pawn, for the staging commands. */
	APawn* LocalPawn(UWorld* World, APlayerController*& OutController)
	{
		OutController = nullptr;
		if (World == nullptr)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get(); PC != nullptr && PC->IsLocalController())
			{
				OutController = PC;
				return PC->GetPawn();
			}
		}
		return nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs TeleportCommand(
		TEXT("ob.hud.tp"),
		TEXT("Dev: ob.hud.tp <x> <y> <z> moves the local pawn there (staging HUD screenshots)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = nullptr;
			APawn* Pawn = LocalPawn(World, PC);
			if (Pawn != nullptr && Args.Num() >= 3)
			{
				const FVector To(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
				Pawn->SetActorLocation(To, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
				UE_LOG(LogOneBrahamUI, Log, TEXT("[OB.UI] HUD staging: pawn to %s"), *To.ToString());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs LookCommand(
		TEXT("ob.hud.look"),
		TEXT("Dev: ob.hud.look <yaw> <pitch> turns the local view (staging HUD screenshots)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = nullptr;
			LocalPawn(World, PC);
			if (PC != nullptr && Args.Num() >= 1)
			{
				PC->SetControlRotation(FRotator(Args.Num() >= 2 ? FCString::Atof(*Args[1]) : 0.f, FCString::Atof(*Args[0]), 0.f));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs AfterCommand(
		TEXT("ob.hud.after"),
		TEXT("Dev: ob.hud.after <seconds> <command...> runs a console command after a delay (for -ExecCmds scripts)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (World == nullptr || Args.Num() < 2)
			{
				return;
			}
			const float Delay = FMath::Max(0.01f, FCString::Atof(*Args[0]));
			TArray<FString> Rest = Args;
			Rest.RemoveAt(0);
			const FString Command = FString::Join(Rest, TEXT(" "));
			// Counted in the world's own time, which is the clock the HUD animates
			// on: a hitch (a teleport's streaming) clamps the world's delta, and a
			// script on the wall clock would photograph a card that, as far as
			// the HUD is concerned, has not had time to arrive. While the world is
			// paused (a standalone pause menu) the wall clock stands in, so a
			// script that pauses and then photographs still moves on.
			const TWeakObjectPtr<UWorld> WeakWorld = World;
			TSharedRef<float> Remaining = MakeShared<float>(Delay);
			TSharedRef<double> LastWorld = MakeShared<double>(World->GetTimeSeconds());
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Command, Remaining, LastWorld](float RealDelta)
			{
				if (UWorld* Live = WeakWorld.Get())
				{
					const double WorldNow = Live->GetTimeSeconds();
					const float WorldDelta = static_cast<float>(WorldNow - *LastWorld);
					*LastWorld = WorldNow;
					*Remaining -= (Live->IsPaused() || WorldDelta < 0.f) ? RealDelta : WorldDelta;
				}
				else
				{
					*Remaining -= RealDelta;
				}
				if (*Remaining > 0.f)
				{
					return true;
				}

				if (UWorld* Live = WeakWorld.Get(); Live != nullptr && GEngine != nullptr)
				{
					// Through the local player, so player-controller and cheat
					// commands ("god", "slomo") reach the controller as typed.
					if (ULocalPlayer* Player = GEngine->GetFirstGamePlayer(Live))
					{
						Player->Exec(Live, *Command, *GLog);
					}
					else
					{
						GEngine->Exec(Live, *Command);
					}
				}
				else if (GEngine != nullptr)
				{
					GEngine->Exec(nullptr, *Command);
				}
				return false;
			}));
		}));
}
#endif
