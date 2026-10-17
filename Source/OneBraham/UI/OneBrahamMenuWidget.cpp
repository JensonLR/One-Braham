// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMenuWidget.h"

#include "OneBraham.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"

// ==========================================================================
//  Binder
// ==========================================================================
void UOneBrahamMenuBinder::HandleClicked()
{
	if (Clicked)
	{
		Clicked();
	}
}

void UOneBrahamMenuBinder::HandleFloatChanged(float Value)
{
	if (FloatChanged)
	{
		FloatChanged(Value);
	}
}

void UOneBrahamMenuBinder::HandleBoolChanged(bool bValue)
{
	if (BoolChanged)
	{
		BoolChanged(bValue);
	}
}

void UOneBrahamMenuBinder::HandleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Direct is the menu itself writing the current value back into the control
	// when it opens. Reporting that as a change would mark every screen dirty
	// the moment it was looked at.
	if (SelectionType == ESelectInfo::Direct || !IndexChanged)
	{
		return;
	}

	if (UComboBoxString* Owner = Combo.Get())
	{
		IndexChanged(Owner->FindOptionIndex(SelectedItem));
	}
}

// ==========================================================================
//  Combo box
// ==========================================================================
void UOneBrahamComboBox::InitBrandStyle(const FSlateFontInfo& InFont, const FSlateColor& InForeground)
{
	InitFont(InFont);
	InitForegroundColor(InForeground);
}

// ==========================================================================
//  Menu widget
// ==========================================================================
UOneBrahamMenuWidget::UOneBrahamMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Menus take focus (the pause menu has to, for a gamepad to drive it); the
	// scoreboard turns this off for itself because it is read with the game
	// still holding the keyboard.
	SetIsFocusable(true);
}

TSharedRef<SWidget> UOneBrahamMenuWidget::RebuildWidget()
{
	EnsureLayout();
	return Super::RebuildWidget();
}

void UOneBrahamMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Built here as well as in RebuildWidget so the owner can push data into the
	// controls the moment the widget is created, before it is ever on screen.
	EnsureLayout();
}

void UOneBrahamMenuWidget::EnsureLayout()
{
	// A designer's Blueprint supplies its own tree, exactly as with the HUD.
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	bBuiltLayoutInCode = true;
	BuildLayout();
}

int32 UOneBrahamMenuWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// UButton has no focused brush, so a gamepad user moving down the menu would
	// see nothing change except the engine's dotted focus rectangle. Paint is the
	// one callback a visible widget is guaranteed (Docs/08 section 3), so the
	// focused button is restyled here: only on a change, so the cost is a loop
	// over a handful of weak pointers.
	UButton* Focused = nullptr;

	for (const TWeakObjectPtr<UButton>& Weak : FocusStyledButtons)
	{
		UButton* Button = Weak.Get();
		if (Button != nullptr && Button->HasKeyboardFocus())
		{
			Focused = Button;
			break;
		}
	}

	if (Focused != FocusStyledButton.Get())
	{
		if (UButton* Previous = FocusStyledButton.Get())
		{
			Previous->SetStyle(BrandButtonStyle(false));
		}

		if (Focused != nullptr)
		{
			Focused->SetStyle(BrandButtonStyle(true));
		}

		FocusStyledButton = Focused;
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
}

FReply UOneBrahamMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	return FReply::Handled();
}

FReply UOneBrahamMenuWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
	return FReply::Handled();
}

FReply UOneBrahamMenuWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// A scroll box under the cursor has already had the wheel; anything that
	// reaches here would otherwise change the player's weapon.
	Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
	return FReply::Handled();
}

// ==========================================================================
//  Colour tokens
// ==========================================================================
// The display-correct tokens (UOneBrahamUIStyle::Colour): the brand floats
// read as linear washed every menu out.
FLinearColor UOneBrahamMenuWidget::Ink()		{ return UOneBrahamUIStyle::Colour(EOneBrahamColour::LightGrey); }
FLinearColor UOneBrahamMenuWidget::Accent()		{ return UOneBrahamUIStyle::Colour(EOneBrahamColour::SignalBlue); }
FLinearColor UOneBrahamMenuWidget::InkMuted()	{ return UOneBrahamUIStyle::Colour(EOneBrahamColour::LightGrey, 0.62f); }
FLinearColor UOneBrahamMenuWidget::InkFaint()	{ return UOneBrahamUIStyle::Colour(EOneBrahamColour::LightGrey, 0.32f); }

FText UOneBrahamMenuWidget::SignageCase(const FString& Identifier)
{
	// A capital that follows a lower-case letter starts a word, and so does the
	// last capital of an acronym when a lower-case letter follows it, which is
	// what keeps "IT" together in "ITOperations".
	FString Spaced;
	Spaced.Reserve(Identifier.Len() + 4);

	for (int32 Index = 0; Index < Identifier.Len(); ++Index)
	{
		const TCHAR Char = Identifier[Index];

		if (Char == TEXT('_'))
		{
			if (!Spaced.IsEmpty() && !Spaced.EndsWith(TEXT(" ")))
			{
				Spaced.AppendChar(TEXT(' '));
			}
			continue;
		}

		if (Index > 0 && FChar::IsUpper(Char))
		{
			const TCHAR Previous = Identifier[Index - 1];
			const bool bNextLower = Index + 1 < Identifier.Len() && FChar::IsLower(Identifier[Index + 1]);

			if ((FChar::IsLower(Previous) || FChar::IsDigit(Previous) || (FChar::IsUpper(Previous) && bNextLower))
				&& !Spaced.EndsWith(TEXT(" ")))
			{
				Spaced.AppendChar(TEXT(' '));
			}
		}

		Spaced.AppendChar(Char);
	}

	Spaced.TrimStartAndEndInline();
	return FText::FromString(Spaced.ToUpper());
}

// ==========================================================================
//  Construction helpers
// ==========================================================================
FButtonStyle UOneBrahamMenuWidget::BrandButtonStyle(bool bFocused)
{
	const FLinearColor Hairline(Ink().R, Ink().G, Ink().B, 0.22f);
	const FLinearColor AccentFill = Accent();
	const FLinearColor AccentPressed(AccentFill.R * 0.75f, AccentFill.G * 0.75f, AccentFill.B * 0.75f, 1.f);

	// Buttons are 2 px radius (Docs/08 section 2): square enough to read as a
	// sign, soft enough not to look like a spreadsheet.
	const FSlateBrush Rest = UOneBrahamUIStyle::OutlineBrush(Hairline, 1.f, 2.f);
	FSlateBrush Lit = UOneBrahamUIStyle::OutlineBrush(AccentFill, 1.f, 2.f);
	Lit.TintColor = FSlateColor(AccentFill);
	FSlateBrush Down = UOneBrahamUIStyle::OutlineBrush(AccentPressed, 1.f, 2.f);
	Down.TintColor = FSlateColor(AccentPressed);

	// The outline brush fills with its outline colour; the rest state wants the
	// hairline only, so its fill is made transparent.
	FSlateBrush RestHollow = Rest;
	RestHollow.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));

	const FLinearColor White(1.f, 1.f, 1.f, 1.f);

	FButtonStyle Style;
	Style.SetNormal(bFocused ? Lit : RestHollow)
		.SetHovered(Lit)
		.SetPressed(Down)
		.SetDisabled(RestHollow)
		.SetNormalForeground(FSlateColor(bFocused ? White : Ink()))
		.SetHoveredForeground(FSlateColor(White))
		.SetPressedForeground(FSlateColor(White))
		.SetDisabledForeground(FSlateColor(InkFaint()))
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f));

	return Style;
}

UTextBlock* UOneBrahamMenuWidget::MakeLabel(const FText& Text, EOneBrahamTextSize Size, const FLinearColor& Colour,
	bool bBold, ETextJustify::Type Justify)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

	// No outline here, unlike the HUD: these screens sit on an opaque panel, not
	// on a sunlit Skydeck, and outlined type on a flat panel looks like a game.
	Label->SetFont(UOneBrahamUIStyle::Font(Size, bBold));
	Label->SetColorAndOpacity(FSlateColor(Colour));
	Label->SetJustification(Justify);
	Label->SetText(Text);
	Label->SetVisibility(ESlateVisibility::HitTestInvisible);

	return Label;
}

UButton* UOneBrahamMenuWidget::MakeButton(const FText& Label, TFunction<void()> OnClicked, EOneBrahamTextSize Size,
	bool bFocusable, UTextBlock** OutLabel, const FMargin& Padding)
{
	UOneBrahamButton* Button = WidgetTree->ConstructWidget<UOneBrahamButton>(UOneBrahamButton::StaticClass());
	Button->SetStyle(BrandButtonStyle(false));
	Button->InitBrandFocusable(bFocusable);

	UTextBlock* Text = MakeLabel(Label, Size, Ink(), true);
	// The button's foreground drives the text, so hover and focus recolour it
	// without a second set of handlers.
	Text->SetColorAndOpacity(FSlateColor::UseForeground());

	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		ButtonSlot->SetPadding(Padding);
		ButtonSlot->SetHorizontalAlignment(HAlign_Left);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}

	UOneBrahamMenuBinder* Binder = NewBinder();
	Binder->Clicked = MoveTemp(OnClicked);
	Button->OnClicked.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleClicked);

	if (bFocusable)
	{
		FocusStyledButtons.Add(Button);
	}

	if (OutLabel != nullptr)
	{
		*OutLabel = Text;
	}

	return Button;
}

USlider* UOneBrahamMenuWidget::MakeSlider(float Min, float Max, float Step, TFunction<void(float)> OnChanged)
{
	USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetStepSize(Step);

	FSlateBrush Bar = UOneBrahamUIStyle::SolidBrush(InkFaint());
	FSlateBrush Thumb = UOneBrahamUIStyle::SolidBrush(Ink());
	Thumb.ImageSize = FVector2D(6.f, 18.f);
	FSlateBrush ThumbLit = UOneBrahamUIStyle::SolidBrush(Accent());
	ThumbLit.ImageSize = FVector2D(6.f, 18.f);

	FSliderStyle Style;
	Style.SetNormalBarImage(Bar)
		.SetHoveredBarImage(Bar)
		.SetDisabledBarImage(Bar)
		.SetNormalThumbImage(Thumb)
		.SetHoveredThumbImage(ThumbLit)
		.SetDisabledThumbImage(Thumb)
		.SetBarThickness(2.f);
	Slider->SetWidgetStyle(Style);

	UOneBrahamMenuBinder* Binder = NewBinder();
	Binder->FloatChanged = MoveTemp(OnChanged);
	Slider->OnValueChanged.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleFloatChanged);

	return Slider;
}

UCheckBox* UOneBrahamMenuWidget::MakeCheckBox(TFunction<void(bool)> OnChanged)
{
	UCheckBox* Box = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());

	// A square: hollow when off, filled with the accent when on. No tick glyph,
	// because the fill already says it and a tick is the one thing every game
	// menu has.
	const FLinearColor Hairline(Ink().R, Ink().G, Ink().B, 0.45f);

	FSlateBrush Off = UOneBrahamUIStyle::OutlineBrush(Hairline, 1.f, 0.f);
	Off.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	Off.ImageSize = FVector2D(18.f, 18.f);

	FSlateBrush OffHover = UOneBrahamUIStyle::OutlineBrush(Accent(), 1.f, 0.f);
	OffHover.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	OffHover.ImageSize = FVector2D(18.f, 18.f);

	FSlateBrush On = UOneBrahamUIStyle::OutlineBrush(Accent(), 1.f, 0.f);
	On.ImageSize = FVector2D(18.f, 18.f);

	FCheckBoxStyle Style = Box->GetWidgetStyle();
	Style.SetCheckBoxType(ESlateCheckBoxType::CheckBox)
		.SetUncheckedImage(Off)
		.SetUncheckedHoveredImage(OffHover)
		.SetUncheckedPressedImage(OffHover)
		.SetCheckedImage(On)
		.SetCheckedHoveredImage(On)
		.SetCheckedPressedImage(On)
		.SetUndeterminedImage(Off)
		.SetUndeterminedHoveredImage(OffHover)
		.SetUndeterminedPressedImage(OffHover)
		.SetPadding(FMargin(0.f));
	Box->SetWidgetStyle(Style);

	UOneBrahamMenuBinder* Binder = NewBinder();
	Binder->BoolChanged = MoveTemp(OnChanged);
	Box->OnCheckStateChanged.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleBoolChanged);

	return Box;
}

UOneBrahamComboBox* UOneBrahamMenuWidget::MakeCombo(const TArray<FText>& Options, TFunction<void(int32)> OnChanged)
{
	UOneBrahamComboBox* Combo = WidgetTree->ConstructWidget<UOneBrahamComboBox>(UOneBrahamComboBox::StaticClass());
	Combo->InitBrandStyle(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small, true), FSlateColor(Ink()));

	for (const FText& Option : Options)
	{
		Combo->AddOption(Option.ToString());
	}

	// The closed combo looks like a button; the open list is a navy panel with an
	// accent row under the cursor, so it reads as part of the same sign.
	FComboBoxStyle Style = Combo->GetWidgetStyle();
	FComboButtonStyle ButtonStyle = Style.ComboButtonStyle;
	ButtonStyle.SetButtonStyle(BrandButtonStyle(false));
	ButtonStyle.SetMenuBorderBrush(UOneBrahamUIStyle::PanelBrush(0.98f, 0.f));
	Style.SetComboButtonStyle(ButtonStyle);
	Combo->SetWidgetStyle(Style);

	const FSlateBrush Clear = UOneBrahamUIStyle::SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
	const FSlateBrush Lit = UOneBrahamUIStyle::SolidBrush(Accent());

	FTableRowStyle ItemStyle = Combo->GetItemStyle();
	ItemStyle.SetEvenRowBackgroundBrush(Clear)
		.SetOddRowBackgroundBrush(Clear)
		.SetEvenRowBackgroundHoveredBrush(Lit)
		.SetOddRowBackgroundHoveredBrush(Lit)
		.SetActiveBrush(Lit)
		.SetActiveHoveredBrush(Lit)
		.SetInactiveBrush(Clear)
		.SetInactiveHoveredBrush(Lit)
		.SetTextColor(FSlateColor(Ink()))
		.SetSelectedTextColor(FSlateColor(FLinearColor::White));
	Combo->SetItemStyle(ItemStyle);
	Combo->SetContentPadding(FMargin(12.f, 6.f));

	UOneBrahamMenuBinder* Binder = NewBinder();
	Binder->Combo = Combo;
	Binder->IndexChanged = MoveTemp(OnChanged);
	Combo->OnSelectionChanged.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleSelectionChanged);

	return Combo;
}

UImage* UOneBrahamMenuWidget::MakeRule(const FLinearColor& Colour, float Thickness)
{
	UImage* Rule = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	FSlateBrush Brush = UOneBrahamUIStyle::SolidBrush(Colour);
	Brush.ImageSize = FVector2D(4.f, Thickness);
	Rule->SetBrush(Brush);
	Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Rule;
}

UImage* UOneBrahamMenuWidget::MakeSwatch(const FLinearColor& Colour, const FVector2D& Size)
{
	UImage* Swatch = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	FSlateBrush Brush = UOneBrahamUIStyle::SolidBrush(FLinearColor::White);
	Brush.ImageSize = Size;
	Swatch->SetBrush(Brush);
	Swatch->SetColorAndOpacity(Colour);
	Swatch->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Swatch;
}

USizeBox* UOneBrahamMenuWidget::MakeCell(UWidget* Content, float Width, float Height)
{
	USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Cell->SetWidthOverride(Width);

	if (Height > 0.f)
	{
		Cell->SetHeightOverride(Height);
	}

	if (Content != nullptr)
	{
		Cell->AddChild(Content);
	}

	return Cell;
}

UBorder* UOneBrahamMenuWidget::MakePanel(UWidget* Content, float Alpha, const FMargin& Padding)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrush(UOneBrahamUIStyle::PanelBrush(Alpha, 4.f));
	Panel->SetPadding(Padding);

	if (Content != nullptr)
	{
		Panel->SetContent(Content);
	}

	return Panel;
}

UOneBrahamMenuBinder* UOneBrahamMenuWidget::NewBinder()
{
	UOneBrahamMenuBinder* Binder = NewObject<UOneBrahamMenuBinder>(this);
	Binders.Add(Binder);
	return Binder;
}
