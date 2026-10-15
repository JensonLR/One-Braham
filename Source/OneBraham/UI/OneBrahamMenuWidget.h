// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMenuWidget.h
// ---------------------------------------------------------------------------
// The shared base of the in-match menus: the scoreboard, the pause menu and
// the settings screen.
//
// WHY A BASE CLASS
//   The three screens are built in C++ for the same reason the HUD is (Docs/08
//   section 1): a clean checkout has to be able to mute a colleague, change the
//   field of view and leave the match before any widget asset exists. Building
//   UMG by hand is verbose, and three copies of "make a button in the brand
//   style" drift apart within a week, so the construction helpers live here.
//
// THE LOOK
//   Building signage, not a game menu. Navy panels, light-grey type, hairline
//   rules, and one accent - signal blue - for the thing that is yours or the
//   thing that has focus. No other colour appears on these screens; when a
//   state needs emphasis it gets weight or opacity, not a new hue.
//
// DYNAMIC DELEGATES
//   UButton::OnClicked and friends are dynamic delegates: they bind to a
//   UFUNCTION on a UObject and carry no context, so twelve mute buttons cannot
//   share one handler. UOneBrahamMenuBinder is the adaptor: one small UObject
//   per control, holding a C++ callback, kept alive by the owning widget.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/SlateTypes.h"
#include "UI/OneBrahamUIStyle.h"
#include "OneBrahamMenuWidget.generated.h"

class UBorder;
class UCheckBox;
class UImage;
class USizeBox;
class USlider;
class UTextBlock;
class UWidget;

/** Adapts a dynamic UMG delegate to a C++ callback. One per control. */
UCLASS(Transient)
class ONEBRAHAM_API UOneBrahamMenuBinder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Clicked;
	TFunction<void(float)> FloatChanged;
	TFunction<void(bool)> BoolChanged;
	TFunction<void(int32)> IndexChanged;

	/** The combo whose selection is reported as an index. */
	TWeakObjectPtr<UComboBoxString> Combo;

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleFloatChanged(float Value);

	UFUNCTION()
	void HandleBoolChanged(bool bValue);

	UFUNCTION()
	void HandleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
};

/**
 * A combo box that can be given the brand font. UComboBoxString's font and
 * foreground setters are protected "Init" functions meant for constructors;
 * this subclass exposes them for code that builds the widget by hand, before
 * its Slate widget exists.
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamComboBox : public UComboBoxString
{
	GENERATED_BODY()

public:
	void InitBrandStyle(const FSlateFontInfo& InFont, const FSlateColor& InForeground);
};

/**
 * A button whose focusability can be set from code. UButton's InitIsFocusable
 * is protected for the same reason as the combo's font: it has to happen before
 * the Slate widget exists, which is exactly when hand-built code calls it.
 */
UCLASS()
class ONEBRAHAM_API UOneBrahamButton : public UButton
{
	GENERATED_BODY()

public:
	void InitBrandFocusable(bool bInFocusable) { InitIsFocusable(bInFocusable); }
};

UCLASS(Abstract)
class ONEBRAHAM_API UOneBrahamMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** True when this instance built its own layout (no authored Blueprint tree). */
	UFUNCTION(BlueprintPure, Category = "One Braham|UI")
	bool IsCodelessLayout() const { return bBuiltLayoutInCode; }

	// --- colour tokens ------------------------------------------------------
	/** Primary type colour. */
	static FLinearColor Ink();
	/** Secondary type: labels, column headings. */
	static FLinearColor InkMuted();
	/** Tertiary: footers, rules, the eliminated. */
	static FLinearColor InkFaint();
	/** The one accent: yours, or focused. */
	static FLinearColor Accent();

	/**
	 * An identifier as a sign-writer would set it: words split on case changes
	 * and upper-cased. "BusinessAnalyst" -> "BUSINESS ANALYST", "ITOperations"
	 * -> "IT OPERATIONS", "IA_VoiceLineMenu" (with the prefix stripped by the
	 * caller) -> "VOICE LINE MENU". Underscores become spaces.
	 */
	static FText SignageCase(const FString& Identifier);

protected:
	/** Builds the tree into WidgetTree. Called once, only when there is no
	 *  authored tree. */
	virtual void BuildLayout() {}

	/** Builds the layout if nobody has yet. Safe to call repeatedly. */
	void EnsureLayout();

	// A menu is a solid object: a click or a wheel that lands on it and is not
	// handled by a control must not fall through to the game underneath, or a
	// click on the scoreboard's background fires the player's weapon.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// --- construction helpers ----------------------------------------------
	UTextBlock* MakeLabel(const FText& Text, EOneBrahamTextSize Size, const FLinearColor& Colour,
		bool bBold = false, ETextJustify::Type Justify = ETextJustify::Left);

	/**
	 * A flat signage button: hairline outline at rest, accent fill on hover and
	 * on keyboard/gamepad focus. bFocusable=false for buttons used while the game
	 * keeps the keyboard (the scoreboard's mute buttons), so a click does not
	 * move focus off the viewport and swallow the key release that closes it.
	 */
	UButton* MakeButton(const FText& Label, TFunction<void()> OnClicked, EOneBrahamTextSize Size = EOneBrahamTextSize::Medium,
		bool bFocusable = true, UTextBlock** OutLabel = nullptr, const FMargin& Padding = FMargin(18.f, 10.f));

	USlider* MakeSlider(float Min, float Max, float Step, TFunction<void(float)> OnChanged);

	UCheckBox* MakeCheckBox(TFunction<void(bool)> OnChanged);

	UOneBrahamComboBox* MakeCombo(const TArray<FText>& Options, TFunction<void(int32)> OnChanged);

	/** A 1 px hairline rule. */
	UImage* MakeRule(const FLinearColor& Colour, float Thickness = 1.f);

	/** A flat solid rectangle of a fixed size: mic lamps, accent bars. */
	UImage* MakeSwatch(const FLinearColor& Colour, const FVector2D& Size);

	/** A fixed-width cell around a widget. Columns line up because every cell
	 *  in a column has the same width, not because of a table widget. */
	USizeBox* MakeCell(UWidget* Content, float Width, float Height = 0.f);

	/** A navy panel with the brand padding. */
	UBorder* MakePanel(UWidget* Content, float Alpha, const FMargin& Padding);

	UOneBrahamMenuBinder* NewBinder();

	/** The brand button style. bFocused is the style swapped in while a button
	 *  holds focus: identical to hovered, so a gamepad user sees the same thing
	 *  a mouse user does. */
	static FButtonStyle BrandButtonStyle(bool bFocused);

	/** Buttons that should show focus. Registered by MakeButton when focusable. */
	TArray<TWeakObjectPtr<UButton>> FocusStyledButtons;

	/** The button currently drawn in its focused style. Paint swaps it. */
	mutable TWeakObjectPtr<UButton> FocusStyledButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UOneBrahamMenuBinder>> Binders;

	bool bBuiltLayoutInCode = false;
};
