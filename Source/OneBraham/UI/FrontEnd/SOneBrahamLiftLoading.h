// Copyright (c) One Braham. All Rights Reserved.
//
// SOneBrahamLiftLoading.h
// ---------------------------------------------------------------------------
// The loading page, "THE LIFT": OLED black, a Doto floor counter in safety
// yellow counting up a floor every 0.8 s, the mode in Archivo, the tower in
// section on the right lighting floor by floor from the real loading
// progress (the async package queue draining; it never runs ahead of the
// loader), and one lift advert at a time in the building's serif.
//
// A plain Slate widget, because the engine's movie player paints it on its
// own thread while the game thread is busy loading the map: everything it
// draws with is resolved on the game thread when it is built (fonts,
// colours), so the paint touches no UObject that could be loading.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class SOneBrahamLiftLoading : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOneBrahamLiftLoading) {}
		/** "ASCENT", "FIRE DRILL". */
		SLATE_ARGUMENT(FText, ModeName)
		/** The map and mode line, and the status ("Entering One Braham..."). */
		SLATE_ARGUMENT(FText, Status)
		/** The squad (up to four names; the first is the player). */
		SLATE_ARGUMENT(TArray<FString>, Squad)
		/** The floor the lift is going to (the counter snaps to it on arrival). */
		SLATE_ARGUMENT(int32, TargetFloor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** The counter's floor at Seconds since the doors closed: 00 up to 03, then 03-08 round again. */
	static int32 CounterFloorAt(double Seconds);

private:
	FText ModeName;
	FText Status;
	TArray<FString> Squad;
	int32 TargetFloor = 15;
	double StartedAt = 0.0;

	/** Real progress: the most packages seen queued, and the fraction since drained. */
	mutable int32 PeakPackages = 0;
	mutable float Progress = 0.f;
};
