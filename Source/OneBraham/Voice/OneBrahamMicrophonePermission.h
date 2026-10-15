// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMicrophonePermission.h
// ---------------------------------------------------------------------------
// The operating system's say on whether the game may open the microphone.
//
// WHY THIS EXISTS
//   On macOS the microphone is a privacy-protected device. Opening it from a
//   bundle with no NSMicrophoneUsageDescription gets the process killed by TCC,
//   and opening it after the player said "Don't Allow" succeeds but delivers
//   silence - the worst failure there is, because the HUD says you are talking.
//   So capture asks here first, and only opens the device when the answer is
//   a clear yes. Every other platform answers Authorised.
//
//   The usage string itself lives in Build/Mac/Resources/Info.Template.plist.

#pragma once

#include "CoreMinimal.h"

enum class EOneBrahamMicrophonePermission : uint8
{
	/** Granted, or a platform with no permission model. */
	Authorised,
	/** Never asked. Request() shows the system prompt. */
	NotDetermined,
	/** The player said no (System Settings -> Privacy -> Microphone). */
	Denied,
	/** Blocked by policy (parental controls, MDM). */
	Restricted,
	/** The bundle has no NSMicrophoneUsageDescription: asking would crash. */
	MissingUsageDescription
};

namespace OneBrahamMicrophonePermission
{
	/** Current status. Cheap; safe to call whenever capture is about to start. */
	ONEBRAHAM_API EOneBrahamMicrophonePermission GetStatus();

	/**
	 * Shows the system prompt when the status is NotDetermined. OnComplete runs on
	 * the game thread with the resulting status; it runs immediately when there
	 * is nothing to ask. A request already in flight is not repeated.
	 */
	ONEBRAHAM_API void Request(TFunction<void(EOneBrahamMicrophonePermission)> OnComplete);

	/** True when capture may open the device now. */
	inline bool IsUsable(EOneBrahamMicrophonePermission Status) { return Status == EOneBrahamMicrophonePermission::Authorised; }

	/** For logs and the settings menu. */
	ONEBRAHAM_API const TCHAR* ToString(EOneBrahamMicrophonePermission Status);
}
