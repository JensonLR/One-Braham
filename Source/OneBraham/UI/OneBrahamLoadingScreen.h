// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLoadingScreen.h
// ---------------------------------------------------------------------------
// The loading screen (Docs/08 §4): the name, the mode and its briefing, what
// is happening, and a flat diagram of the building with the floors this
// player has climbed so far lit.
//
// Plain Slate, not UMG: the movie player draws it on its own thread while the
// map loads, when UObjects and the UMG tick are not to be trusted.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ONEBRAHAM_API SOneBrahamLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOneBrahamLoadingScreen)
		: _HighestFloor(0)
	{}
		SLATE_ARGUMENT(FText, ModeName)
		SLATE_ARGUMENT(FText, Briefing)
		SLATE_ARGUMENT(FText, Status)
		/** The highest ASCENT floor this player has reached; floors up to it are lit. */
		SLATE_ARGUMENT(int32, HighestFloor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
