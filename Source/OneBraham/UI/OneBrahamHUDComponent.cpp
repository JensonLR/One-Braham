// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamHUDComponent.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Interaction/OneBrahamInteractComponent.h"
#include "UI/OneBrahamHUDWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "Voice/OneBrahamProximityVoiceComponent.h"
#include "Voice/OneBrahamVoiceSubsystem.h"
#include "Extraction/OneBrahamExtractionComponent.h"
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

	HUDWidget->ApplyHUDData(Data);
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
