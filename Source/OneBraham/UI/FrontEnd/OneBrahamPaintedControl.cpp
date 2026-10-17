// Copyright (c) One Braham. All Rights Reserved.

#include "UI/FrontEnd/OneBrahamPaintedControl.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

// ==========================================================================
//  Reveal
// ==========================================================================
void FOneBrahamReveal::Evaluate(double Now, int32 Slot, float& OutAlpha, float& OutOffsetX) const
{
	OutAlpha = 1.f;
	OutOffsetX = 0.f;

	if (OutAt > 0.0)
	{
		// Out: 140 ms, ease-in cubic, drifting 8 px left.
		const float Duration = bFlat ? 0.07f : 0.14f;
		const float T = UOneBrahamUIStyle::Tween(Now, OutAt, Duration, EOneBrahamEase::Exit);
		OutAlpha = 1.f - T;
		OutOffsetX = bFlat ? 0.f : -8.f * T;
		return;
	}

	if (InAt > 0.0)
	{
		// In: 260 ms, ease-out expo, from 12 px right, rows 25 ms apart.
		const float Duration = bFlat ? 0.13f : 0.26f;
		const double Start = InAt + (bFlat ? 0.0 : 0.025 * Slot);
		const float T = UOneBrahamUIStyle::Tween(Now, Start, Duration, EOneBrahamEase::Ribbon);
		OutAlpha = Now < Start ? 0.f : T;
		OutOffsetX = bFlat ? 0.f : 12.f * (1.f - T);
	}
}

// ==========================================================================
//  Control
// ==========================================================================
UOneBrahamPaintedControl::UOneBrahamPaintedControl(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

void UOneBrahamPaintedControl::Setup(const FVector2D& Size, bool bFocusable)
{
	bTakesFocus = bFocusable;
	SetIsFocusable(bFocusable);
	SetVisibility(bFocusable ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);

	if (WidgetTree != nullptr && SizeRoot == nullptr)
	{
		SizeRoot = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		WidgetTree->RootWidget = SizeRoot;
	}
	if (SizeRoot != nullptr)
	{
		if (Size.X > 0.f)
		{
			SizeRoot->SetWidthOverride(Size.X);
		}
		if (Size.Y > 0.f)
		{
			SizeRoot->SetHeightOverride(Size.Y);
		}
	}
}

void UOneBrahamPaintedControl::MarkPressed()
{
	PressedAt = FPlatformTime::Seconds();
}

void UOneBrahamPaintedControl::Activate()
{
	MarkPressed();
	if (OnActivate)
	{
		OnActivate();
	}
}

int32 UOneBrahamPaintedControl::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!PaintFn)
	{
		return Layer;
	}

	const double Now = FPlatformTime::Seconds();
	float Alpha = 1.f;
	float OffsetX = 0.f;
	if (Reveal.IsValid())
	{
		Reveal->Evaluate(Now, RevealSlot, Alpha, OffsetX);
	}
	Alpha *= InWidgetStyle.GetColorAndOpacityTint().A;
	if (Alpha <= 0.001f)
	{
		return Layer;
	}

	const FGeometry Shifted = OffsetX == 0.f
		? AllottedGeometry
		: AllottedGeometry.MakeChild(AllottedGeometry.GetLocalSize(), FSlateLayoutTransform(FVector2f(OffsetX, 0.f)));

	FOneBrahamControlState State;
	State.bLit = bLit;
	State.SinceLit = static_cast<float>(Now - LitChangedAt);
	State.SincePress = static_cast<float>(Now - PressedAt);
	State.Now = Now;

	++Layer;
	const OneBrahamPaint::FPainter Canvas(OutDrawElements, Shifted, Layer, Alpha);
	PaintFn(Canvas, State);
	return Canvas.TopLayer();
}

void UOneBrahamPaintedControl::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	// Hover is focus: one lit state for the mouse and the pad alike. Only a
	// moving mouse takes it, so a page building in under a resting cursor
	// does not steal focus from where the pad left it.
	if (bTakesFocus && !HasAnyUserFocus() && !InMouseEvent.GetCursorDelta().IsNearlyZero()
		&& (!Reveal.IsValid() || !Reveal->IsLeaving()))
	{
		SetFocus();
	}
}

FReply UOneBrahamPaintedControl::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bTakesFocus || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	SetFocus();
	if (OnPointer)
	{
		const FVector2D Local(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
		OnPointer(Local.X, InGeometry.GetLocalSize().X);
	}
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UOneBrahamPaintedControl::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (OnPointer && HasMouseCapture())
	{
		const FVector2D Local(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
		OnPointer(Local.X, InGeometry.GetLocalSize().X);
		return FReply::Handled();
	}
	if (bTakesFocus && !HasAnyUserFocus() && !InMouseEvent.GetCursorDelta().IsNearlyZero()
		&& (!Reveal.IsValid() || !Reveal->IsLeaving()))
	{
		SetFocus();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UOneBrahamPaintedControl::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bTakesFocus || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	}
	FReply Reply = FReply::Handled().ReleaseMouseCapture();
	if (!OnPointer && InGeometry.IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
	{
		Activate();
	}
	return Reply;
}

FReply UOneBrahamPaintedControl::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (bTakesFocus && !InKeyEvent.IsRepeat()
		&& (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Virtual_Accept))
	{
		if (OnActivate)
		{
			Activate();
			return FReply::Handled();
		}
	}
	if (OnKey && OnKey(Key))
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FNavigationReply UOneBrahamPaintedControl::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent,
	const FNavigationReply& InDefaultReply)
{
	if (OnNavigate)
	{
		if (UWidget* Target = OnNavigate(InNavigationEvent.GetNavigationType()))
		{
			return FNavigationReply::Explicit(Target->TakeWidget());
		}
	}
	return Super::NativeOnNavigation(MyGeometry, InNavigationEvent, InDefaultReply);
}

void UOneBrahamPaintedControl::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnAddedToFocusPath(InFocusEvent);
	if (!bLit)
	{
		bLit = true;
		LitChangedAt = FPlatformTime::Seconds();
		if (OnLitChanged)
		{
			OnLitChanged(true);
		}
	}
}

void UOneBrahamPaintedControl::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
	if (bLit)
	{
		bLit = false;
		LitChangedAt = FPlatformTime::Seconds();
		if (OnLitChanged)
		{
			OnLitChanged(false);
		}
	}
}
