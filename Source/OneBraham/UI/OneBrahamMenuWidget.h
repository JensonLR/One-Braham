// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMenuWidget.h
// ---------------------------------------------------------------------------
// The base of every code-built menu: main menu, settings, results.
//
// Like the HUD, a menu builds its own layout when its Blueprint has none, so a
// clean checkout has a complete front end. A page is a title, a scrolling
// column of controls and a footer; a subclass fills the column in BuildPage
// and calls ShowPage again whenever what it shows has changed.
//
// CONTROLS AND CLOSURES
//   UMG's delegates are dynamic and carry no context, so a column of twenty
//   sliders cannot tell which one moved. Each control gets a small binder
//   object holding a C++ closure, and the binder is what the delegate calls.
//   The binders live as long as the page.
//
// BACK
//   Escape and the pad's B (FaceButton_Right) call GoBack. The default closes
//   the menu; a menu with pages goes up one.
//
// Menus do not rely on NativeTick (see UOneBrahamHUDWidget::NativePaint): what
// changes on a timer is pushed in by a delegate or a timer instead.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "OneBrahamMenuWidget.generated.h"

class UButton;
class UCheckBox;
class UHorizontalBox;
class UPanelWidget;
class UScrollBox;
class USlider;
class UTextBlock;
class UVerticalBox;

/** Holds one control's closure; the control's dynamic delegate calls it. */
UCLASS()
class ONEBRAHAM_API UOneBrahamMenuBinder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> OnClick;
	TFunction<void(float)> OnFloat;
	TFunction<void(bool)> OnBool;

	UFUNCTION()
	void HandleClick() { if (OnClick) { OnClick(); } }

	UFUNCTION()
	void HandleFloat(float Value) { if (OnFloat) { OnFloat(Value); } }

	UFUNCTION()
	void HandleBool(bool bValue) { if (OnBool) { OnBool(bValue); } }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOneBrahamMenuClosed);

UCLASS()
class ONEBRAHAM_API UOneBrahamMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Rebuilds the column from BuildPage. Safe to call at any time. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Menu")
	void ShowPage();

	/** Escape / B. Closes the menu unless a subclass has somewhere to go back to. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Menu")
	virtual void GoBack();

	/** Removes the menu and tells whoever opened it. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Menu")
	void CloseMenu();

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Menu")
	FOneBrahamMenuClosed OnMenuClosed;

	/** True when this instance built its own layout (no authored Blueprint). */
	bool IsCodelessLayout() const { return bBuiltLayoutInCode; }

protected:
	/** Fills the column. The base shows nothing. */
	virtual void BuildPage() {}

	/** The page title, upper-cased on screen. */
	virtual FText GetPageTitle() const { return FText::GetEmpty(); }

	/** The line along the bottom. */
	virtual FText GetFooterText() const;

	// --- controls, added to the column (or to Parent) ----------------------
	UTextBlock* AddHeading(const FText& Text);
	UTextBlock* AddText(const FText& Text, EOneBrahamTextSize Size = EOneBrahamTextSize::Small,
		const FLinearColor& Colour = UOneBrahamUIStyle::LightGrey(), UPanelWidget* Parent = nullptr);
	UButton* AddButton(const FText& Label, TFunction<void()> OnClick, UPanelWidget* Parent = nullptr, bool bEnabled = true);
	USlider* AddSlider(const FText& Label, float Min, float Max, float Step, float Value,
		TFunction<void(float)> OnChanged, TFunction<FText(float)> Format = nullptr);
	UCheckBox* AddToggle(const FText& Label, bool bValue, TFunction<void(bool)> OnChanged);

	/** A button that steps through Options; OnChanged gets the new index. */
	UButton* AddCycle(const FText& Label, const TArray<FText>& Options, int32 Index, TFunction<void(int32)> OnChanged);

	/** A horizontal strip to put buttons side by side. */
	UHorizontalBox* AddRow();

	/** A label on the left and Value on the right. */
	UHorizontalBox* AddLabelledRow(const FText& Label, UWidget* Value);

	UTextBlock* MakeText(const FText& Text, EOneBrahamTextSize Size, const FLinearColor& Colour, bool bBold = false);
	UButton* MakeButton(const FText& Label, TFunction<void()> OnClick, bool bEnabled = true);

	UOneBrahamMenuBinder* MakeBinder();

	/** The column of the current page. */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Column = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> Scroll = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterText = nullptr;

	/** The binders of the page on screen; replaced on every ShowPage. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UOneBrahamMenuBinder>> Binders;

	/** The first button of the page, focused for a pad. */
	UPROPERTY(Transient)
	TObjectPtr<UButton> FirstButton = nullptr;

	/** Width of the column, px at 1080p. */
	float ColumnWidth = 820.f;

	/** Background opacity: opaque for the front end, see-through over a match. */
	float BackdropOpacity = 0.92f;

private:
	void BuildFrame();

	bool bBuiltLayoutInCode = false;
};
