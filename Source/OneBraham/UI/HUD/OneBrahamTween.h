// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamTween.h
// ---------------------------------------------------------------------------
// The interface's one motion library: four curves, one tween, one pulse.
//
// WHY SO FEW
//   A HUD that animates with twenty curves reads as twenty designers. This one
//   has four motion characters and every element uses one of them:
//
//     Lift    ease-in-out cubic. Things that travel: camera blends, panels
//             opening (the lockdown module, the board).
//     Ribbon  ease-out expo, cubic-bezier(0.16, 1, 0.3, 1). Things that
//             arrive: entries, underlines, wipes, the letterbox bars.
//     Exit    ease-in cubic, cubic-bezier(0.32, 0, 0.67, 0). Things that
//             leave, at about 0.6x the length of their entry.
//     Reader  ease-out back (1.2). The card reader accepting a badge - and
//             nothing else, because an overshoot is a sound effect for the eye.
//     Linear  counters, stage cross-fades, data rolls.
//
// NO TICK
//   A tween is a start time, a length and a curve; it is evaluated in
//   NativePaint against a clock the caller chooses (FPlatformTime for menus,
//   world time for the HUD). A C++ UUserWidget's NativeTick is never called
//   (Docs/08, "Nothing in the HUD ticks"), so there is nothing to forget to
//   advance: the value is a function of the clock.
//
// PULSES
//   The only loops allowed, and each one means something (Docs/08):
//     0.5 Hz   a teammate needs you (downed)
//     1 Hz     something is about to change (next seal, CONTESTED, sealing)
//     2 Hz     act now (RELOAD)
//     0.71 Hz  breath: critical health only
//     0.25 Hz  the Lockdown beacon
//   No strobe frames anywhere: every blink is a smoothed square wave.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamUIStyle.h"

namespace OneBrahamEase
{
	/** One curve, 0..1 in, 0..1 out (Reader overshoots past 1 on the way).
	 *  The curves themselves live in UOneBrahamUIStyle::Ease, so the menus
	 *  and the HUD cannot drift apart. */
	inline float Evaluate(EOneBrahamEase Curve, float T)
	{
		return UOneBrahamUIStyle::Ease(Curve, T);
	}

	/** Durations, in seconds, named as the spec names them. */
	constexpr float Micro = 0.09f;
	constexpr float Small = 0.14f;
	constexpr float Panel = 0.28f;
	constexpr float Wipe = 0.26f;
	constexpr float RibbonDraw = 0.40f;
	constexpr float StageCrossFade = 0.40f;
}

/**
 * A value moving from A to B over a time window.
 *
 * Plain data: a start, a length, a curve, two ends. Evaluated against whatever
 * clock the owner keeps; a tween that has not started reads A, one that has
 * finished reads B, so there is never an "is it running" flag to get wrong.
 */
struct FOneBrahamTween
{
	double StartTime = -1.0;
	float Duration = 0.f;
	EOneBrahamEase Curve = EOneBrahamEase::Ribbon;
	float From = 0.f;
	float To = 1.f;

	FOneBrahamTween() = default;

	FOneBrahamTween(double InStart, float InDuration, EOneBrahamEase InCurve, float InFrom = 0.f, float InTo = 1.f)
		: StartTime(InStart), Duration(InDuration), Curve(InCurve), From(InFrom), To(InTo)
	{
	}

	/** True once Start has been called at least once. */
	bool HasStarted() const { return StartTime >= 0.0; }

	/** Restarts from the current value to a new target, so an interrupted
	 *  animation never jumps. */
	void Retarget(double Now, float NewTarget, float InDuration, EOneBrahamEase InCurve)
	{
		From = Value(Now);
		To = NewTarget;
		StartTime = Now;
		Duration = InDuration;
		Curve = InCurve;
	}

	/** 0..1 linear progress through the window. */
	float Progress(double Now) const
	{
		if (!HasStarted())
		{
			return 0.f;
		}
		if (Duration <= KINDA_SMALL_NUMBER)
		{
			return Now >= StartTime ? 1.f : 0.f;
		}
		return FMath::Clamp(static_cast<float>((Now - StartTime) / Duration), 0.f, 1.f);
	}

	/** The eased value now. */
	float Value(double Now) const
	{
		if (!HasStarted())
		{
			return From;
		}
		return FMath::Lerp(From, To, OneBrahamEase::Evaluate(Curve, Progress(Now)));
	}

	bool HasFinished(double Now) const { return HasStarted() && Progress(Now) >= 1.f; }
};

namespace OneBrahamPulse
{
	/** The named rates. */
	constexpr float TeammateHz = 0.5f;
	constexpr float ImminentHz = 1.f;
	constexpr float ActNowHz = 2.f;
	constexpr float BreathHz = 0.71f;
	constexpr float BeaconHz = 0.25f;

	/**
	 * A blink with soft edges: 1 for the first half of each cycle, 0 for the
	 * second, with 60 ms ramps so it is never a single-frame strobe. With
	 * Reduce Motion on, anything above 1 Hz holds steady (returns 1).
	 */
	inline float Blink(double Time, float Hz, bool bReduceMotion = false)
	{
		if (Hz <= 0.f || (bReduceMotion && Hz > 1.f))
		{
			return 1.f;
		}
		const double Period = 1.0 / Hz;
		const float Phase = static_cast<float>(FMath::Fmod(Time, Period) / Period);
		const float Ramp = FMath::Clamp(0.06f * Hz, 0.01f, 0.2f);
		if (Phase < Ramp)			{ return Phase / Ramp; }
		if (Phase < 0.5f)			{ return 1.f; }
		if (Phase < 0.5f + Ramp)	{ return 1.f - (Phase - 0.5f) / Ramp; }
		return 0.f;
	}

	/** A sine breath between Low and High. */
	inline float Breath(double Time, float Hz, float Low, float High)
	{
		const float S = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time * Hz * 2.0 * PI));
		return FMath::Lerp(Low, High, S);
	}
}
