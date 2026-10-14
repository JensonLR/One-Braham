// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVoiceSubsystem.h
// ---------------------------------------------------------------------------
// Voice policy for one match. Two jobs, deliberately in one place:
//
//   1. BEHAVIOUR LINES (the character "barks" written in Docs/14_Voice_Script.md)
//      Resolves a LineId to a recorded line + subtitle, applies priority and
//      cooldowns so twelve characters never talk over each other, and broadcasts
//      the subtitle to the HUD. Speaking is server-authoritative and replicated
//      through UOneBrahamProximityVoiceComponent, so every machine hears the
//      same line from the same character at the same time.
//
//   2. LIVE VOICE CHAT POLICY
//      Who can hear whom (proximity radius, squad, team), how much a wall should
//      muffle them (VoiceOccluder trace), mute handling, and the per-talker
//      relay budget that stops a modified client from flooding the server.
//      The transport itself lives in UOneBrahamProximityVoiceComponent.
//
// The subsystem is a UWorldSubsystem so it exists on the server and on clients,
// and it dies with the match - no state survives a travel.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Voice/OneBrahamVoiceTypes.h"
#include "OneBrahamVoiceSubsystem.generated.h"

class AOneBrahamCharacter;
class AOneBrahamPlayerState;
class UOneBrahamVoiceLineSet;
class UOneBrahamProximityVoiceComponent;

/** Fired on every machine that can hear a line, so the HUD can subtitle it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOneBrahamVoiceLineSpoken,
	APlayerState*, Speaker, FName, LineId, const FText&, Subtitle, float, DurationSeconds);

UCLASS()
class ONEBRAHAM_API UOneBrahamVoiceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- lifecycle ---------------------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Resolves the subsystem for whatever context the caller has: a character, a
	 * controller, a player state or a component owner. This is the single entry
	 * point used from gameplay code, and it is null-safe on purpose.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice", meta = (WorldContext = "Context"))
	static UOneBrahamVoiceSubsystem* Get(const AActor* Context);

	// --- behaviour lines ---------------------------------------------------
	/**
	 * Requests that a character says a line. Server-authoritative: on a client
	 * this is ignored (the server decided the line already, and the multicast
	 * brings it back). Returns true when the line was accepted and spoken.
	 *
	 * @param Speaker  Character, PlayerState, Controller or a component owner.
	 * @param LineId   Id from the voice script, e.g. V_RELOADING.
	 * @param Priority 0..1 override; higher wins when two lines compete.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	bool SpeakContextualLine(AActor* Speaker, FName LineId, float Priority = 0.5f);

	/** Whether this character may speak right now (cooldowns, downed, silence). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool CanSpeak(AActor* Speaker) const;

	/** Called by the proximity voice component when a replicated line arrives. */
	void NotifyLineSpoken(AOneBrahamPlayerState* SpeakerState, AOneBrahamCharacter* Speaker,
		FName LineId, const FText& Subtitle, float DurationSeconds);

	/** The full line set (authored asset when present, C++ core table otherwise). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	UOneBrahamVoiceLineSet* GetLineSet() const { return LineSet; }

	// --- live voice chat policy -------------------------------------------
	/** Fired when the local player presses/uses the push-to-talk key. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void SetTransmitting(const AActor* Context, bool bTransmitting);

	/** True while the given player is transmitting. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	bool IsTransmitting(const AOneBrahamPlayerState* PlayerState) const;

	/** Server-side routing test: should Listener receive Speaker's voice? */
	bool ShouldHear(const AOneBrahamPlayerState* Listener, const AOneBrahamPlayerState* Speaker) const;

	/** 0 = clear line of sight, 1 = fully blocked. Uses the VoiceOccluder channel. */
	float ComputeOcclusionAlpha(const FVector& SpeakerLocation, const FVector& ListenerLocation) const;

	/** Proximity voice radius in cm for the current match. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	float GetProximityRangeCm() const;

	/** Transport tunables, resolved once from the match settings. */
	const FOneBrahamVoiceChatConfig& GetChatConfig() const { return ChatConfig; }

	/** Server-side frame budget. Returns false when the talker should be throttled. */
	bool ConsumeRelayBudget(APlayerState* Talker, int32 FrameBytes);

	/** Anti-cheat counter exposed to the audit log at match end. */
	int32 GetDroppedFrameCount() const { return TotalDroppedFrames; }

	/** Plays the local UI cue for a voice channel change. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Voice")
	void PlayChannelSwitchCue(EOneBrahamVoiceChannel Channel);

	/** Channel name for the HUD, localised: "PROXIMITY", "SQUAD", "TEAM", "OFF". */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	FText GetChannelDisplayName(EOneBrahamVoiceChannel Channel) const;

	// ======================================================================
	//  Quick-line queries
	// ======================================================================
	/** Seconds until this speaker may say this line again, or 0.
	 *
	 *  The comms wheel greys a line out until it is available, and it has to
	 *  agree with the server: the cooldown that matters is the one enforced when
	 *  the line is actually requested, a moment after the wheel closes. Reading
	 *  the same table keeps a player from picking a line that is silently
	 *  refused. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	float GetLineCooldownRemaining(const AActor* Speaker, FName LineId) const;

	/** The wheel's own labels for the eight quick lines, in the order the wheel
	 *  draws them. A label is not a subtitle: it fits a segment, so it is short,
	 *  and it is UI text, so it is not the same string the character says. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Voice")
	static void GetQuickLineWheel(FName& OutLineId, FText& OutLabel, int32 SegmentIndex);

	/** Announcement channel: used by the match director for building announcements. */
	void SpeakAnnouncement(FName LineId);

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Voice")
	FOneBrahamVoiceLineSpoken OnVoiceLineSpoken;

protected:
	/** Ambient muttering: gives empty floors a human presence without chatter. */
	void ScheduleNextAmbientLine();
	void HandleAmbientLineTimer();

	/** Per-player "last spoke" clock, used to keep characters from monologuing. */
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<APlayerState>, float> LastSpokenTime;

	/** Per-player cooldown per line id. Key is a stable id, so it never grows. */
	TMap<uint64, float> LineCooldowns;

	UPROPERTY(Transient)
	TObjectPtr<UOneBrahamVoiceLineSet> LineSet = nullptr;

	FOneBrahamVoiceChatConfig ChatConfig;

	TMap<TWeakObjectPtr<APlayerState>, FOneBrahamVoiceBudget> RelayBudgets;

	FTimerHandle AmbientLineTimerHandle;
	FRandomStream Random;

	float CurrentPriorityToken = 0.f;
	int32 TotalDroppedFrames = 0;

	/** Mix of "how long has this character been quiet" used by ambient lines. */
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<APlayerState>, int32> AmbientLineCounts;
};
