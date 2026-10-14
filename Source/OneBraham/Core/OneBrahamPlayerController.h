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

	/**
	 * The local player is attached to this controller: on a client when it joins,
	 * on a listen host for the host. This machine's profile is the only place a
	 * player's choices exist, so this is where they leave for the server.
	 */
	virtual void ReceivedPlayer() override;

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

	/** An authored pause screen. Empty uses the HUD's own panel (resume, settings, leave). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<UUserWidget> PauseMenuWidgetClass;

	/** WBP_Settings, opened over the pause menu. Defaults to the pipeline's path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSoftClassPtr<UUserWidget> SettingsWidgetClass;

	/** WBP_Results, shown when the match result arrives. Defaults to the pipeline's path. */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|UI")
	TSoftClassPtr<UUserWidget> ResultsWidgetClass;

	/** Shows the results screen. Bound to the game state's OnMatchResultReady on a local controller. */
	UFUNCTION()
	void HandleMatchResult(const FOneBrahamMatchResult& Result);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<UUserWidget> DeathScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "One Braham|UI")
	TSubclassOf<UUserWidget> ScoreboardWidgetClass;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|UI")
	TObjectPtr<class UOneBrahamHUDWidget> HUDWidget;

	/**
	 * Opens or closes the pause menu. It never pauses the world: this is a PvP
	 * match with eleven other people in it, and SetPause on a listen host froze
	 * all of them. While it is open the gameplay keys are swapped out for the
	 * menu bindings (pause and push to talk), so a stick nudge or a trigger
	 * cannot move or fire the body standing in the corridor.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void TogglePauseMenu();

	/** WBP_Settings over the pause menu; closed with it. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void OpenSettingsMenu();

	/**
	 * Back to the main menu. On a listen host that ends the match for everyone,
	 * which is what leaving a match you are hosting means.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void LeaveMatch();

	UFUNCTION(BlueprintCallable, Category = "One Braham|UI")
	void SetScoreboardVisible(bool bVisible);

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
	void ClientDamageTaken(float Damage, FVector_NetQuantize InstigatorLocation, bool bHeadshot, bool bArmourAbsorbed);

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

	/**
	 * Local: one camera shake, through the player's screen-shake setting (off, or
	 * an intensity). Every shake in the game goes through here so the
	 * accessibility switch cannot be missed by the next one somebody adds.
	 */
	class UCameraShakeBase* PlayCameraShakeLocally(TSubclassOf<class UCameraShakeBase> ShakeClass, float Scale);

	/** Remaining flash duration on this client (0 = not flashed). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Effects")
	float GetFlashRemaining() const { return FlashRemaining; }

	/**
	 * Server: enters or leaves spectating. Replicates bIsSpectating, and runs the
	 * local half (view, deafened mix) at once on a listen host, whose own
	 * controller never receives its own RepNotify. The respawn path used to
	 * write the protected flag directly from the character, which does not
	 * compile, and would have skipped the local half if it had.
	 */
	void SetSpectating(bool bSpectating);

protected:
	/** Per-tick flash decay, called from PlayerTick. */
	void TickFlash(float DeltaTime);

	// ======================================================================
	//  Spectating
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_Spectating, BlueprintReadOnly, Category = "One Braham|Spectator")
	bool bIsSpectating = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Spectator")
	int32 SpectatedPlayerId = INDEX_NONE;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Spectator")
	FOneBrahamSpectatorTargetChanged OnSpectatorTargetChanged;

	/** The player state being watched, or null (free camera, or nobody left). */
	const AOneBrahamPlayerState* GetSpectatedPlayerState() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Spectator")
	bool IsFreeCameraActive() const { return bFreeCamActive; }

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
	UFUNCTION(BlueprintCallable, Category = "One Braham|Security")
	bool CheckRPCBudget(const TCHAR* RPCLabel);

	/**
	 * The budget check from anywhere a Server RPC lands: a controller, a pawn,
	 * a player state or a component on any of them. Resolves the owning player's
	 * controller and spends from its budget; true (allowed) when there is no
	 * player behind the call - a bot, or the server calling its own handler.
	 */
	static bool CheckRPCBudgetFor(const UObject* Context, const TCHAR* RPCLabel);

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

	/**
	 * Client -> server: who this player is wearing. The archetype by id
	 * ("BusinessAnalyst"), the outfit as parallel slot/option arrays (an RPC
	 * cannot carry a TMap), and the clan tag. Every value is checked against the
	 * character registry on the server: an unknown archetype is ignored, a
	 * cosmetic is kept only if it is a real option of the slot it claims and is
	 * not on the competitive ban list, and the clan tag is cut to four letters
	 * and digits. The wardrobe and the deploy screen call it again on a change.
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "One Braham|Controller|Loadout")
	void ServerSetAppearance(FName ArchetypeId, const TArray<FName>& CosmeticSlots,
		const TArray<FName>& CosmeticOptions, const FString& InClanTag);

	/** Local: sends this machine's saved profile - name, loadout, appearance. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Controller|Loadout")
	void SendProfileToServer();

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

	/**
	 * Installs the mapping context that carries the keys, replacing the previous
	 * one. Local controllers only. Called at BeginPlay and again whenever the
	 * settings change, so a rebind is live without a level load.
	 */
	void RefreshBindingsContext();

public:
	/**
	 * The prompt with {Interact} replaced by the key that interacts on the device
	 * the player last looked with: "Hold F - override" on a keyboard, "Hold
	 * Gamepad Face Button Top" on a pad, and whatever they rebound it to. Prompts
	 * used to say "Hold E", which was the lean key.
	 */
	FText ResolvePromptKeys(const FText& Prompt) const;

protected:

	/**
	 * Builds that context from the [OneBraham.Input] section: the shipped defaults
	 * in DefaultInput.ini, with the player's GameUserSettings.ini taking an action's
	 * place device by device. The IMC_ assets the pipeline generates carry no keys
	 * by design (keys are data, rebindable; shape is design), so without this
	 * context no key in the game would do anything.
	 */
	UInputMappingContext* BuildBindingsContext(const TSet<FName>* OnlyActions, const TCHAR* ContextName);

	/** Swaps the gameplay contexts for the menu bindings, or back. */
	void SetMenuInput(bool bMenu);

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

	UPROPERTY()
	TObjectPtr<UOneBrahamHUDComponent> HUDComponent = nullptr;

	/** Rain outdoors and the Tube underneath, for the local player only. */
	UPROPERTY()
	TObjectPtr<class UOneBrahamWorldEffectsComponent> WorldEffects = nullptr;

	UPROPERTY()
	TObjectPtr<UOneBrahamAudioManagerComponent> AudioManager = nullptr;

	/** Rolling RPC budget: timestamps of the last N RPCs inside the window. */
	TArray<float> RPCWindowTimestamps;
	static constexpr float RPCWindowSeconds = 1.f;

	/** Escalating anti-cheat score; QA automation asserts this stays at 0. */
	int32 ViolationScore = 0;

	float InteractionTraceAccumulator = 0.f;

	/** The context RefreshBindingsContext last installed; transient, rebuilt on rebind. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> BindingsMappingContext;

	/** The same section filtered to what works in a menu: pause, and push to talk. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MenuBindingsMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PauseMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SettingsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ResultsWidget;

	/**
	 * GFrameCounter of the last interaction begin. Tap and hold share a key by
	 * design, so one press fires both actions; the second begin in the same frame
	 * is dropped, or a door would open and close again on a single press.
	 */
	uint64 LastInteractionStartFrame = 0;

	/** Which device's key the prompts name: whichever the player last looked with. */
	bool bPromptsShowGamepad = false;
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
};
