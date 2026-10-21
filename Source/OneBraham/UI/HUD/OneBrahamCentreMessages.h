// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCentreMessages.h
// ---------------------------------------------------------------------------
// Who may speak in the middle of the screen, and how loudly.
//
// THE PROBLEM
//   The centre column can be asked for a stage card, a PA card, a notice
//   banner, a refused card reader ("EXECUTIVE BADGE REQUIRED") and the
//   induction's next step all in the same second - five plates stacked down
//   the crosshair's column, each one correct and the whole unreadable.
//
// THE RULE
//   One card at a time. Everything that wants the centre has a rank:
//
//       Downed          the player's own medallion: nothing outranks your life
//       StageCard       LOCKDOWN, PURGE, THE ROOF
//       Announcement    the PA card
//       Banner          short notices from the game
//       BlockedPrompt   the card reader saying no
//       Induction       the next step of the induction (the sub-banner)
//
//   The highest-ranked request is the CARD. The next one that can shrink
//   becomes the TICKER - one line, no plate, in its own place below the
//   crosshair band - and everything else WAITS (its clock stops, so nothing
//   is missed, only delayed). The PA card never shrinks: its queue waits.
//
//   The subtitle line is not a card, but it must never repeat one: any
//   sentence of it that a card on screen is already showing is dropped, and
//   a subtitle with nothing left is not drawn.
//
//   The crosshair band (40% to 55% of the height) is kept clear of all of
//   it. The layout constants below are the only positions the centre uses.
//
// Plain C++ and pure, for the tests (OneBraham.UI.CentreMessages).

#pragma once

#include "CoreMinimal.h"

/** The centre's occupants, loudest first (the enum order is the rank). */
enum class EOneBrahamCentreSlot : uint8
{
	Downed,
	StageCard,
	Announcement,
	Banner,
	BlockedPrompt,
	Induction,
	Count
};

/** How an occupant is shown this frame. */
enum class EOneBrahamCentreShow : uint8
{
	/** Not asking. */
	Off,
	/** Asking, but held back: drawn nowhere, its clock stopped. */
	Wait,
	/** One line, no plate. */
	Ticker,
	/** The full card. */
	Card
};

struct ONEBRAHAM_API FOneBrahamCentreLayout
{
	EOneBrahamCentreShow Show[static_cast<int32>(EOneBrahamCentreSlot::Count)] = {};

	EOneBrahamCentreShow Get(EOneBrahamCentreSlot Slot) const { return Show[static_cast<int32>(Slot)]; }
	bool IsCard(EOneBrahamCentreSlot Slot) const { return Get(Slot) == EOneBrahamCentreShow::Card; }
	bool IsTicker(EOneBrahamCentreSlot Slot) const { return Get(Slot) == EOneBrahamCentreShow::Ticker; }
	bool IsWaiting(EOneBrahamCentreSlot Slot) const { return Get(Slot) == EOneBrahamCentreShow::Wait; }
	/** Drawn at all (card or ticker). */
	bool IsShown(EOneBrahamCentreSlot Slot) const { return IsCard(Slot) || IsTicker(Slot); }

	int32 Count(EOneBrahamCentreShow What) const;
};

namespace OneBrahamCentre
{
	inline uint32 Bit(EOneBrahamCentreSlot Slot) { return 1u << static_cast<uint32>(Slot); }

	/** Whether Slot may shrink to the ticker while Card holds the centre. */
	ONEBRAHAM_API bool CanTicker(EOneBrahamCentreSlot Slot, EOneBrahamCentreSlot Card);

	/** The layout for a set of requests (a mask of Bit()s). */
	ONEBRAHAM_API FOneBrahamCentreLayout Resolve(uint32 ActiveMask);

	// --- the subtitle never repeats a card ------------------------------------------
	/** Lower case, letters and digits only, single spaces: "Use the stairs." -> "use the stairs". */
	ONEBRAHAM_API FString Words(const FString& Text);

	/** The subtitle with every sentence a showing card already says removed;
	 *  empty when nothing is left worth a line. */
	ONEBRAHAM_API FString SubtitleRemainder(const FString& Subtitle, const TArray<FString>& CardLines);

	// --- the layout: fractions of the screen height ------------------------------------
	/** The crosshair's band: nothing in the centre column is drawn inside it. */
	constexpr float CrosshairBandTop = 0.40f;
	constexpr float CrosshairBandBottom = 0.55f;

	/** The PA card's top (it slides down under a tall objective, never into the band). */
	constexpr float AnnouncementTop = 0.13f;
	/** The PA card's height in px at 1080p. */
	constexpr float AnnouncementHeightPx = 78.f;

	/** The stage card's stack: eyebrow, 88 px title, serif line. */
	constexpr float StageCardTop = 0.22f;
	/** Its tallest extent in px at 1080p (eyebrow + title + line). */
	constexpr float StageCardHeightPx = 150.f;

	/** The notice banner's centre line. */
	constexpr float BannerY = 0.36f;
	/** Its plate's height in px at 1080p. */
	constexpr float BannerHeightPx = 34.f;

	/** The card reader: the ring's centre. */
	constexpr float PromptY = 0.58f;
	/** The reader's plate: 48 px tall, centred on PromptY. */
	constexpr float PromptHalfHeightPx = 24.f;

	/** The induction's card, and the ticker line for whatever has shrunk. */
	constexpr float InductionY = 0.665f;
	constexpr float TickerY = 0.665f;

	/** The downed medallion's centre and its radius in px at 1080p. */
	constexpr float DownedY = 0.30f;
	constexpr float DownedRadiusPx = 84.f;
}
