// Copyright (c) One Braham. All Rights Reserved.
//
// On Apple platforms UnrealBuildTool compiles .cpp files as Objective-C++, so
// the macOS branch below can talk to AVFoundation directly. AVFoundation is
// already linked through the Engine module on Mac, so no Build.cs change.

#include "Voice/OneBrahamMicrophonePermission.h"

#include "OneBraham.h"
#include "Async/Async.h"

#include <atomic>

#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"
#include "Apple/PreAppleSystemHeaders.h"
#include <AVFoundation/AVFoundation.h>
#include "Apple/PostAppleSystemHeaders.h"
#endif

namespace
{
	/** One prompt at a time: a second PTT press while the dialog is up must not
	 *  queue a second request (macOS ignores it, but we would log it twice). */
	std::atomic<bool> GRequestInFlight { false };
}

EOneBrahamMicrophonePermission OneBrahamMicrophonePermission::GetStatus()
{
#if PLATFORM_MAC
	@autoreleasepool
	{
		// Without the usage string, macOS terminates the process the moment the
		// input device is touched. Checking the bundle is what keeps a missing
		// plist entry a log line instead of a crash report.
		NSDictionary<NSString*, id>* Info = [NSBundle mainBundle].infoDictionary;
		if (Info == nil || Info[@"NSMicrophoneUsageDescription"] == nil)
		{
			return EOneBrahamMicrophonePermission::MissingUsageDescription;
		}

		switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
		{
		case AVAuthorizationStatusAuthorized:		return EOneBrahamMicrophonePermission::Authorised;
		case AVAuthorizationStatusNotDetermined:	return EOneBrahamMicrophonePermission::NotDetermined;
		case AVAuthorizationStatusRestricted:		return EOneBrahamMicrophonePermission::Restricted;
		case AVAuthorizationStatusDenied:
		default:									return EOneBrahamMicrophonePermission::Denied;
		}
	}
#else
	return EOneBrahamMicrophonePermission::Authorised;
#endif
}

void OneBrahamMicrophonePermission::Request(TFunction<void(EOneBrahamMicrophonePermission)> OnComplete)
{
	const EOneBrahamMicrophonePermission Current = GetStatus();

	if (Current != EOneBrahamMicrophonePermission::NotDetermined)
	{
		if (OnComplete)
		{
			OnComplete(Current);
		}
		return;
	}

#if PLATFORM_MAC
	if (GRequestInFlight.exchange(true))
	{
		return;
	}

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Asking macOS for microphone access."));

	// The completion handler runs on an arbitrary queue; everything the game
	// touches afterwards lives on the game thread, so hop there before calling out.
	TFunction<void(EOneBrahamMicrophonePermission)> Callback = MoveTemp(OnComplete);
	[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL bGranted)
	{
		const EOneBrahamMicrophonePermission Result = bGranted
			? EOneBrahamMicrophonePermission::Authorised
			: EOneBrahamMicrophonePermission::Denied;

		AsyncTask(ENamedThreads::GameThread, [Callback, Result]()
		{
			GRequestInFlight = false;

			UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Microphone access %s."),
				Result == EOneBrahamMicrophonePermission::Authorised ? TEXT("granted") : TEXT("denied"));

			if (Callback)
			{
				Callback(Result);
			}
		});
	}];
#else
	if (OnComplete)
	{
		OnComplete(EOneBrahamMicrophonePermission::Authorised);
	}
#endif
}

const TCHAR* OneBrahamMicrophonePermission::ToString(EOneBrahamMicrophonePermission Status)
{
	switch (Status)
	{
	case EOneBrahamMicrophonePermission::Authorised:				return TEXT("authorised");
	case EOneBrahamMicrophonePermission::NotDetermined:				return TEXT("not yet asked");
	case EOneBrahamMicrophonePermission::Denied:					return TEXT("denied");
	case EOneBrahamMicrophonePermission::Restricted:				return TEXT("restricted");
	case EOneBrahamMicrophonePermission::MissingUsageDescription:	return TEXT("missing NSMicrophoneUsageDescription");
	default:														return TEXT("unknown");
	}
}
