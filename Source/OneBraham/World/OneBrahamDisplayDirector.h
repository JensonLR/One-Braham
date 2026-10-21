// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDisplayDirector.h
// ---------------------------------------------------------------------------
// When the building speaks, its screens say it.
//
// Every announcement the match broadcasts - the game mode's, the lockdown's,
// the override terminals', a breached wall's - reaches each machine through
// AOneBrahamGameState::OnAnnouncement (MulticastAnnounce). This subsystem
// listens there and, on the frame it arrives, puts the same line on every
// OLED screen on the viewer's floor and near them (the escalator wall that
// spans two storeys, the screens across the atrium): an override slide in
// the building's voice -
//
//     ONE BRAHAM  ·  ALL FLOORS  ·  17:33           eyebrow, the dusk clock
//     Fire shutters are closing on all floors below 5.     the point
//     Lifts are out of service.                     the next sentence
//
// - on the stage's field by the HUD's own colour rule (UOneBrahamUIStyle::
// StageTone: calm violet-blue, pre-lockdown amber, lockdown yellow, purge red,
// the evacuation's green while the helicopter is called), held
// for as long as the tannoy takes to read it, then the adverts come back.
// A screen the viewer walks up to while the line still stands joins it.
//
// The match's end takes the screens too: the end screen's headline
// (OneBrahamMatchEnd::HeadlineFor), held through the post-match window, with
// the tannoy's last word under it.
//
// Client-side only, like the screens themselves: nothing replicates, each
// machine drives its own copies, and a dedicated server never creates this.
// The phase and the stage are read from the replicated game state each tick
// rather than through delegates, because the server's own phase change
// broadcasts nothing and a stage can replicate after its announcement.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/OneBrahamTypes.h"
#include "World/OneBrahamDisplay.h"
#include "OneBrahamDisplayDirector.generated.h"

class AOneBrahamGameState;
class ACameraActor;

UCLASS()
class ONEBRAHAM_API UOneBrahamDisplayDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UOneBrahamDisplayDirector* Get(const UObject* WorldContext);

	/**
	 * Puts the building's line on the screens near the viewer now, for
	 * HoldSeconds. The announcement path calls it; so do the dev hooks.
	 */
	void Speak(const FText& Headline, const FText& Subline, EOneBrahamLockdownStage Stage, float HoldSeconds);

	/** The match's end on the screens: Headline over the tannoy's last word, held. */
	void SpeakMatchEnd(const FText& Headline, EOneBrahamLockdownStage Stage);

	/** True while a line is standing on the screens. */
	bool IsSpeaking() const;

	/** The slide on the screens while IsSpeaking. */
	const FOneBrahamDisplaySlide& GetCurrentSlide() const { return Current; }

	/** Screens carrying the line now. */
	int32 GetLitCount() const;

	// ----------------------------------------------------------------------
	//  The rules, without a world, for tests
	// ----------------------------------------------------------------------

	/** "Attention all staff. Fire shutters are closing. Lifts..." -> "Fire shutters are closing." */
	static FString HeadlineFor(const FString& Paragraph);

	/** The sentence after the headline's, or empty. */
	static FString SublineFor(const FString& Paragraph);

	/** How long the screens hold a paragraph: the tannoy's reading time, 6-14 s. */
	static float HoldSecondsFor(const FString& Paragraph);

	/** "ONE BRAHAM  ·  ALL FLOORS  ·  17:33". */
	static FText EyebrowFor(const FText& Clock);

	/**
	 * The field for a stage, by the HUD's rule (StageTone -> ToneField), so a
	 * screen and the PA card over it are never different colours: PreLockdown
	 * is the warning amber, an evacuation the way-out green (the Purge outranks
	 * it), and the colour-blind remaps apply.
	 */
	static void StageColours(EOneBrahamLockdownStage Stage, FLinearColor& OutA, FLinearColor& OutB, bool bEvacuating = false);

	/**
	 * Whether a screen whose bounds are DisplayBounds carries the line for a
	 * viewer at Viewer: on the viewer's storey (its bottom no more than
	 * FloorBandCm above their eye, its top no more than half that below it),
	 * or within NearCm of them whatever the floor.
	 */
	static bool IsInEarshot(const FVector& Viewer, const FBox& DisplayBounds, float FloorBandCm, float NearCm);

	/** A storey is 400 cm: the eye is on the floor it stands on when the screen's span is within this. */
	static constexpr float FloorBandCm = 300.f;
	/** Anything this close is "near the player", up the atrium or down the escalators. */
	static constexpr float NearCm = 2500.f;
	/** The post-match window's hold. The travel ends it. */
	static constexpr float MatchEndHoldSeconds = 120.f;

#if !UE_BUILD_SHIPPING
	/** Dev: `ob.Displays.Tour` - photographs the screens, then each stage's announcement on them. */
	void StartTour(bool bQuitAfter);
#endif

private:
	UFUNCTION()
	void HandleAnnouncement(const FText& Announcement);

	void TryBind();
	void Unbind();

	/** The viewer's eye, if this machine has one. */
	bool GetViewer(FVector& OutLocation) const;

	/** Pushes Current to every screen in earshot that is not already carrying it. */
	void LightScreens(bool bEveryScreen);

	EOneBrahamLockdownStage CurrentStage() const;

	/** The helicopter is called and not yet gone: the HUD's "evacuating". */
	bool IsEvacuating() const;

	TWeakObjectPtr<AOneBrahamGameState> BoundGameState;

	FOneBrahamDisplaySlide Current;
	double SpeakStart = -1.0;
	double SpeakEnd = -1.0;
	EOneBrahamLockdownStage SpokenStage = EOneBrahamLockdownStage::Normal;
	bool bSpokenEvacuating = false;
	bool bMatchEndShown = false;

	/** The screens carrying the current line. */
	TArray<TWeakObjectPtr<AOneBrahamDisplay>> Lit;

	double NextScan = 0.0;

	/** The tannoy's last paragraph and when it came, for the match end's subline. */
	FText LastAnnouncement;
	double LastAnnouncementTime = -1.0;

	/** When the phase was first seen Complete; the ending replicates around it. */
	double MatchCompleteSeenAt = -1.0;

#if !UE_BUILD_SHIPPING
	void TickTour();

	int32 TourStep = INDEX_NONE;
	double TourStepAt = 0.0;
	bool bTourQuit = false;
	TWeakObjectPtr<ACameraActor> TourCamera;
#endif
};
