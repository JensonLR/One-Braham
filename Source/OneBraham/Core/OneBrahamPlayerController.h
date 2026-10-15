// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPlayerController.h
// ---------------------------------------------------------------------------
// First-person controller. Responsibilities:
//   * Enhanced Input: builds mapping contexts from UOneBrahamInputConfig,
//     applies the player's sensitivity/crouch/lean preferences every frame.
//   * Camera: 1P view, ADS FOV blending, hit-direction, death cam, spectator.
//   * UI: owns the CommonUI root widget stack (HUD, scoreboard, pause, death).
//   * Anti-cheat: RPC budget accounting and server-side validation helpers so
//     every Server_* RPC in the project has one obvious place to rate-limit.
//   * Voice: forwards push-to-talk state to the voice subsystem.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamPlayerController.generated.h"

class UOneBrahamInputConfig;
class UOneBrahamHUDComponent;
class UOneBrahamAudioManagerComponent;
class UOneBrahamProximityVoiceComponent;
class UInputMappingContext;
class UInputAction;
class UCommonActivatableWidget;
class AOneBrahamCharacter;
class USoundBase;
class UUserWidget;
class UMaterialParameterCollection;

/** What the player is currently looking at, for the interaction prompt. */
USTRUCT(BlueprintType)
struct FOneBrahamInteractionTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	FText Prompt;

	/** OB.Interaction.* tag on the target, used to pick the right input verb. */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	FGameplayTag InteractionTag;

	/** False when the player cannot use it yet ("Locked - Executive badge required"). */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	float Distance = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamInteractionTargetChanged, const FOneBrahamInteractionTarget&, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamSpectatorTargetChanged, AOneBrahamCharacter*, Target);

UCLASS(BlueprintType)
class ONEBRAHAM_API AOneBrahamPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AOneBrahamPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void InitPlayerState() override;

	// ======================================================================
	//  Components
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Controller")
	UOneBrahamHUDComponent* GetHUDComponent() const { return HUDComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Controller")
	UOneBrahamAudioManagerComponent* GetAudioManager() const { return AudioManager; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Controller")
	AOneBrahamCharacter* GetOneBrahamCharacter() const;

	// ======================================================================
	//  Input configuration
	// ======================================================================
	/** Assigned in BP_OneBrahamPlayerController; see Tools/Python/onebraham/build_input.py. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Input")
	TObjectPtr<UOneBrahamInputConfig> InputConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Input")
	TObjectPtr<UInputMappingContext> GameplayMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Input")
	TObjectPtr<UInputMappingContext> SpectatorMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Input")
	TObjectPtr<UInputMappingContext> CommonMappingContext;

	/** Runtime sensitivity multiplier, refreshed from settings each tick. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Input")
	float EffectiveSensitivity = 0.07f;

	UFUNCTION(BlueprintCallable, Category = "One Braham|Input")
	void PushMappingContext(UInputMappingContext* Context, int32 Priority = 0);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Input")
	void PopMappingContext(UInputMappingContext* Context);

	// ======================================================================
	//  Interaction
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Interaction")
	FOneBrahamInteractionTargetChanged OnInteractionTargetChanged;

	UFUNCTION(BlueprintPure, Category = "One Braham|Interaction")
	const FOneBrahamInteractionTarget& GetInteractionTarget() const { return CurrentInteractionTarget; }

	/** Performs the trace that fills CurrentInteractionTarget. Called at 10 Hz. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	void UpdateInteractionTarget();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	void NotifyInteractionStarted();   // the player pressed Interact

	UFUNCTION(BlueprintCallable, Category = "One Braham|Interaction")
	void NotifyInteractionReleased();  // hold actions (terminals, revives) end here

	// ======================================================================
	//  UI
	// ======================================================================
	/** Widget classes are set on BP_OneBrahamPlayerController so art can swap them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<class UOneBrahamHUDWidget> HUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<UUserWidget> PauseMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<UUserWidget> DeathScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<UUserWidget> ScoreboardWidgetClass;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|UI")
	TObjectPtr<class UOneBrahamHUDWidget> HUDWidget;

	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void TogglePauseMenu();

	/** Opens or closes the pause menu; a no-op when it is already in that state.
	 *  The menu's own RESUME and back key call this. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void SetPauseMenuOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "One Braham|UI")
	bool IsPauseMenuOpen() const { return bPauseMenuOpen; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void SetScoreboardVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "One Braham|UI")
	bool IsScoreboardVisible() const { return bScoreboardOpen; }

	/**
	 * Pushed when the player dies; the death screen doubles as the spectate HUD.
	 *
	 * A Client RPC, and that is the whole point of it being one: the death is
	 * detected on the server, in AOneBrahamCharacter::ServerOnDied, and a plain
	 * function called on the server's copy of a remote player's controller draws
	 * a death screen on a machine with no player sitting at it. Every remote
	 * client died to a screen that never appeared, and the server quietly built
	 * the widget for nobody.
	 *
	 * CauseText is what the death screen prints under the title: who did it, and
	 * how, from the same kill-feed entry the feed gets.
	 */
	UFUNCTION(BlueprintCallable, Client, Reliable, Category = "One Braham|UI")
	void ClientShowDeathScreen(bool bShow, const FText& CauseText);

	/**
	 * The death recap: who hit you, how often, where, from how far, and what the
	 * person who finished you had left. Sent just before the death screen, and
	 * set beneath its cause line.
	 */
	UFUNCTION(Client, Reliable)
	void ClientSetDeathRecap(const FString& RecapLines);

	/**
	 * Starts the death camera on the machine of the player who died: the view
	 * leaves the body, sags, and turns to the killer with a slow push-in and a
	 * draining grade, for DeathCamSeconds, before the spectate view takes over.
	 *
	 * Reliable: it is once per life, and a death that skipped it would drop the
	 * player straight into somebody else's eyes, which is the bug it replaces.
	 * Killer may be null (the building, a fall, a pawn the client cannot
	 * resolve); FocusLocation is used then, when bHasFocus says it means anything.
	 */
	UFUNCTION(Client, Reliable, Category = "One Braham|UI")
	void ClientBeginDeathCam(AOneBrahamCharacter* Victim, APawn* Killer, FVector_NetQuantize FocusLocation, bool bHasFocus);

	/** Server: begins spectating once the death camera has had its few seconds. */
	void ScheduleSpectatingAfterDeathCam();

	/** Server: forgets a scheduled spectate (a respawn arrived first). */
	void CancelScheduledSpectating();

	/** How long the death camera holds before spectating begins. Read on both ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Death Cam")
	float DeathCamSeconds = 3.5f;

	/** The death screen waits this long into the death camera, so the moment is seen before it is captioned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Death Cam")
	float DeathScreenDelaySeconds = 1.25f;

	/** Two shakes for the same explosion closer together than this are the same shake. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Effects")
	float ExplosionShakeDedupeSeconds = 0.4f;

	/** Explosion shakes fade to nothing at this distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|Effects")
	float ExplosionShakeRadiusCm = 4000.f;

	/** Shows a transient banner ("THE BUILDING IS NOW IN LOCKDOWN"). */
	UFUNCTION(BlueprintCallable, Client, Reliable, Category = "One Braham|UI")
	void ClientShowBanner(const FText& Text, float DurationSeconds, FLinearColor Colour);

	/**
	 * This client just took damage, and from where. A Client RPC for the same
	 * reason the death screen is one: `UOneBrahamHealthComponent::ApplyDamage`
	 * returns early on every machine except the authority, so the directional
	 * damage indicator - the arc on the screen edge, the only way a player learns
	 * they are being shot from behind - was broadcast into a delegate that exists
	 * on the server's copy of a remote player's pawn and nowhere else. The row in
	 * Docs/08 says the indicator is always on. This is what makes that true.
	 *
	 * Unreliable on purpose: a dropped arc on one bullet is invisible next to the
	 * bullet that follows it, and this fires on every hit that lands.
	 */
	UFUNCTION(BlueprintCallable, Client, Unreliable, Category = "One Braham|UI")
	void ClientDamageTaken(float Damage, FVector_NetQuantize InstigatorLocation);

	/** Blinds this client with a flashbang: white-out, tinnitus, temporary deafness. */
	UFUNCTION(BlueprintCallable, Client, Reliable, Category = "One Braham|Effects")
	void ClientApplyFlash(float DurationSeconds, bool bDirectHit);

	/** Applies camera shake from an explosion at a distance. */
	UFUNCTION(BlueprintCallable, Client, Unreliable, Category = "One Braham|Effects")
	void ClientApplyExplosionShake(FVector_NetQuantize Origin, float Magnitude);

	/** Local implementation of a flash - also used directly on a listen server. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Effects")
	void ApplyFlashLocally(float DurationSeconds, bool bDirectHit);

	/** Local implementation of an explosion shake - also used directly on a listen server. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Effects")
	void ApplyExplosionShakeLocally(const FVector& Origin, float Magnitude);

	/** Remaining flash duration on this client (0 = not flashed). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Effects")
	float GetFlashRemaining() const { return FlashRemaining; }

protected:
	/** What the 10 Hz interaction trace last found; read by GetInteractionTarget. */
	FOneBrahamInteractionTarget CurrentInteractionTarget;

	/** Per-tick flash decay, called from PlayerTick. */
	void TickFlash(float DeltaTime);

public:
	// ======================================================================
	//  Spectating
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_Spectating, BlueprintReadOnly, Category = "One Braham|Spectator")
	bool bIsSpectating = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Spectator")
	int32 SpectatedPlayerId = INDEX_NONE;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Spectator")
	FOneBrahamSpectatorTargetChanged OnSpectatorTargetChanged;

	UFUNCTION(BlueprintCallable, Category = "One Braham|Spectator")
	void SpectateNextPlayer();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Spectator")
	void SpectatePreviousPlayer();

	UFUNCTION(BlueprintCallable, Category = "One Braham|Spectator")
	void SpectatePlayer(int32 PlayerId);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Spectator")
	void ToggleFreeCamera();

	/** Server: begin spectating after death. */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "One Braham|Spectator")
	void ServerBeginSpectating(int32 PreferredTargetPlayerId);

	// ======================================================================
	//  Ping / comms rose markers
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Ping")
	void PingLookDirection();

	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "One Braham|Ping")
	void ServerPingLocation(FVector_NetQuantize Location);

	UFUNCTION(NetMulticast, Unreliable, Category = "One Braham|Ping")
	void MulticastPingLocation(FVector_NetQuantize Location, EOneBrahamTeam PingingTeam, const FString& PingingName);

	// ======================================================================
	//  Clip marker (for the viral clip pipeline in Docs/19)
	// ======================================================================
	/** Drops a timestamped marker into Saved/Clips/<match>/markers.csv. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Clip")
	void MarkClipMoment(const FString& Label);

	// ======================================================================
	//  Anti-cheat / RPC budget
	// ======================================================================
	/** Returns false (and logs) when this client has exceeded its RPC budget. */
	bool CheckRPCBudget(const TCHAR* RPCLabel);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Security")
	void ReportViolation(const FString& Reason, float Severity = 1.f);

	UFUNCTION(BlueprintPure, Category = "One Braham|Security")
	int32 GetViolationScore() const { return ViolationScore; }

	// ======================================================================
	//  Quick lines
	// ======================================================================
	/** Asks the server to have this character say a comms-wheel line.
	 *
	 *  Client-side entry point for the wheel. Nothing is spoken here: the server
	 *  resolves the line against the voice script, the archetype filter and the
	 *  cooldowns, then multicasts it to whoever can hear it. A client that
	 *  modified this call can only ask faster - the answer is still the
	 *  server's. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Controller|Voice")
	void RequestQuickLine(FName LineId);

	/** Server: validate and deliver a comms-wheel line.
	 *
	 *  Server-authoritative because the alternative is a client that can set its
	 *  own cooldowns, decide which archetype filter applies, and speak into a
	 *  proximity channel that other players paid for. The line set already knows
	 *  how to refuse a line - wrong archetype, on cooldown, no recorded asset -
	 *  so this forwards and lets it answer. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "One Braham|Controller|Voice")
	void ServerRequestQuickLine(FName LineId);

	/**
	 * Client -> server: the loadout this player chose in the locker.
	 *
	 * Names only; every id is resolved against the weapon registry (and, for
	 * utility, against the inventory's own whitelist) when it is granted, and an
	 * id that does not resolve is refused there. The server keeps the result on
	 * the player state and re-arms from it on every spawn.
	 *
	 * It is stored, not obeyed: a second call during a life changes what the
	 * player will carry *next* life and nothing else, so a client cannot swap
	 * weapons in the middle of a fight.
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "One Braham|Controller|Loadout")
	void ServerSetLoadout(FName PrimaryWeaponId, FName SidearmWeaponId,
		const TArray<FName>& UtilityIds, FName WeaponSkinId);

	/** True while the comms wheel is open on this client. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Controller|Voice")
	bool IsQuickLineMenuOpen() const { return bQuickLineMenuOpen; }

protected:
	// ======================================================================
	//  Enhanced Input handlers. One function per action; all of them call into
	//  the character, which owns the authoritative movement/weapon logic.
	// ======================================================================
	void Input_Move(const struct FInputActionValue& Value);
	void Input_Look(const struct FInputActionValue& Value);
	void Input_LookGamepad(const struct FInputActionValue& Value);
	void Input_JumpStarted(const struct FInputActionValue& Value);
	void Input_JumpCompleted(const struct FInputActionValue& Value);
	void Input_SprintStarted(const struct FInputActionValue& Value);
	void Input_SprintCompleted(const struct FInputActionValue& Value);
	void Input_WalkToggle(const struct FInputActionValue& Value);
	void Input_CrouchToggle(const struct FInputActionValue& Value);
	void Input_Lean(const struct FInputActionValue& Value);
	void Input_Mantle(const struct FInputActionValue& Value);
	void Input_Slide(const struct FInputActionValue& Value);
	void Input_FireStarted(const struct FInputActionValue& Value);
	void Input_FireCompleted(const struct FInputActionValue& Value);
	void Input_AimStarted(const struct FInputActionValue& Value);
	void Input_AimCompleted(const struct FInputActionValue& Value);
	void Input_Reload(const struct FInputActionValue& Value);
	void Input_FireModeSwitch(const struct FInputActionValue& Value);
	void Input_Melee(const struct FInputActionValue& Value);
	void Input_Inspect(const struct FInputActionValue& Value);
	void Input_WeaponSlotPrimary(const struct FInputActionValue& Value);
	void Input_WeaponSlotSidearm(const struct FInputActionValue& Value);
	void Input_WeaponCycleNext(const struct FInputActionValue& Value);
	void Input_WeaponCyclePrev(const struct FInputActionValue& Value);
	void Input_ThrowGrenade(const struct FInputActionValue& Value);
	void Input_UseUtility(const struct FInputActionValue& Value);
	void Input_UseMedkit(const struct FInputActionValue& Value);
	void Input_DropWeapon(const struct FInputActionValue& Value);
	void Input_Interact(const struct FInputActionValue& Value);
	void Input_InteractHoldStarted(const struct FInputActionValue& Value);
	void Input_InteractHoldCompleted(const struct FInputActionValue& Value);
	void Input_ToggleFlashlight(const struct FInputActionValue& Value);
	void Input_ToggleLaser(const struct FInputActionValue& Value);
	void Input_PushToTalkStarted(const struct FInputActionValue& Value);
	void Input_PushToTalkCompleted(const struct FInputActionValue& Value);
	void Input_VoiceChannelCycle(const struct FInputActionValue& Value);
	void Input_VoiceMuteSelf(const struct FInputActionValue& Value);
	void Input_ScoreboardStarted(const struct FInputActionValue& Value);
	void Input_ScoreboardCompleted(const struct FInputActionValue& Value);
	void Input_PauseMenu(const struct FInputActionValue& Value);
	void Input_Ping(const struct FInputActionValue& Value);

	// -----------------------------------------------------------------------
	//  Overlays (IA_ToggleHUD, IA_Map, IA_VoiceLineMenu)
	// -----------------------------------------------------------------------
	/** Hides or shows the interface. Session-only, and bound to a key that is
	 *  deliberately obscure: this exists for screenshots and for clips. */
	void Input_ToggleHUD(const struct FInputActionValue& Value);

	/** Opens or closes the floor plan. */
	void Input_MapToggle(const struct FInputActionValue& Value);

	/** Press to open the comms wheel, aim, release to say it.
	 *
	 *  Press-and-release rather than a toggle, because a wheel that stays open
	 *  until it is dismissed is a menu, and a menu in a firefight is a death. */
	void Input_VoiceLineMenuStarted(const struct FInputActionValue& Value);
	void Input_VoiceLineMenuCompleted(const struct FInputActionValue& Value);

	void Input_SpectateNext(const struct FInputActionValue& Value);
	void Input_SpectatePrev(const struct FInputActionValue& Value);
	void Input_SpectateFreeCam(const struct FInputActionValue& Value);
	void Input_ClipMarker(const struct FInputActionValue& Value);

	/** Binds every action in InputConfig to the handlers above. */
	void BindInputActions();

	UFUNCTION()
	void OnRep_Spectating();

	/** True while the local player is holding the comms wheel open. A press
	 *  opens it and a release commits: press, aim, release, which is the only
	 *  wheel interaction that survives a firefight. */
	bool bQuickLineMenuOpen = false;

	/** Rebuilds the spectator view target from SpectatedPlayerId. */
	void ApplySpectatorView();

	UFUNCTION()
	void HandleSettingsChanged();

	/**
	 * Sets the input mode and cursor for whichever menu is up. One function for
	 * both menus, because they interact: the pause menu wins over the scoreboard,
	 * and closing either must hand back exactly the mode the other still needs.
	 *
	 *   pause menu open  -> UI only, cursor on, focus on the menu. The game gets
	 *                       no input at all, so nothing fires, reloads or walks
	 *                       while a player is in Settings; the menu handles its
	 *                       own back key, so Escape still closes it.
	 *   scoreboard open  -> game and UI, cursor on, focus left on the viewport,
	 *                       so the player keeps moving and the Tab release still
	 *                       reaches Enhanced Input and closes the board.
	 *   neither          -> game only, cursor off.
	 */
	void RefreshMenuInputMode();

	/** An authored pause widget (PauseMenuWidgetClass), removed again on close. */
	TWeakObjectPtr<UUserWidget> AuthoredPauseMenu;

	UPROPERTY()
	TObjectPtr<UOneBrahamHUDComponent> HUDComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UOneBrahamAudioManagerComponent> AudioManager = nullptr;

	/** Rolling RPC budget: timestamps of the last N RPCs inside the window. */
	TArray<float> RPCWindowTimestamps;
	static constexpr float RPCWindowSeconds = 1.f;

	/** Escalating anti-cheat score; QA automation asserts this stays at 0. */
	int32 ViolationScore = 0;

	float InteractionTraceAccumulator = 0.f;
	static constexpr float InteractionTraceInterval = 0.1f;

	/** Whether the pause menu is currently up (blocks gameplay input). */
	bool bPauseMenuOpen = false;
	bool bScoreboardOpen = false;
	bool bFreeCamActive = false;

	/** Cached between ticks so the crosshair can grow/shrink predictably. */
	float CurrentCrosshairSpread01 = 0.f;

	/** Flashbang blindness remaining, seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Effects")
	float FlashRemaining = 0.f;

	/** Strength of the current flash (1 = a direct hit at point blank). */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Effects")
	float FlashStrength01 = 0.f;

	/** Latched so the post-process is cleared exactly once when the flash ends. */
	bool bFlashActiveLastFrame = false;

	/** Cached global parameter collection used for the flash white-out. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UMaterialParameterCollection> CachedFlashCollection;

	/** The last explosion this client shook for, so a duplicate delivery is ignored. */
	FVector LastExplosionShakeOrigin = FVector::ZeroVector;
	float LastExplosionShakeTime = -1000.f;

	/** Server: the pending "start spectating" after the death camera. */
	FTimerHandle DeathCamSpectateTimer;

	/** Client: the death screen waiting for the death camera's opening beat. */
	FTimerHandle DeathScreenDelayTimer;
	FText PendingDeathCauseText;
	FString PendingDeathRecap;

	/** The cause line with the recap beneath it. */
	FText ComposeDeathText(const FText& CauseText) const;

	/** Client: the body whose camera the death camera borrowed. */
	TWeakObjectPtr<AOneBrahamCharacter> DeathCamCharacter;

	/** Client: puts the deferred death screen up. */
	void ShowPendingDeathScreen();
};
