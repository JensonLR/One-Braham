// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"

UOneBrahamMenuWidget::UOneBrahamMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

TSharedRef<SWidget> UOneBrahamMenuWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildFrame();
	}
	return Super::RebuildWidget();
}

void UOneBrahamMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ShowPage();
}

FText UOneBrahamMenuWidget::GetFooterText() const
{
	return NSLOCTEXT("OneBraham", "Menu_Footer", "ESC / B  BACK");
}

void UOneBrahamMenuWidget::BuildFrame()
{
	// Backdrop, then a centred column: title, a scroll box with the page, footer.
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Menu_Root"));
	WidgetTree->RootWidget = Root;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Menu_Backdrop"));
	Backdrop->SetBrush(UOneBrahamUIStyle::SolidBrush(UOneBrahamUIStyle::Navy().CopyWithNewOpacity(BackdropOpacity)));
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Menu_Width"));
	Width->SetWidthOverride(ColumnWidth);
	if (UOverlaySlot* WidthSlot = Root->AddChildToOverlay(Width))
	{
		WidthSlot->SetHorizontalAlignment(HAlign_Center);
		WidthSlot->SetVerticalAlignment(VAlign_Fill);
		WidthSlot->SetPadding(FMargin(0.f, 64.f));
	}

	UVerticalBox* Frame = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Menu_Frame"));
	Width->AddChild(Frame);

	TitleText = MakeText(FText::GetEmpty(), EOneBrahamTextSize::Header, UOneBrahamUIStyle::LightGrey(), true);
	if (UVerticalBoxSlot* TitleSlot = Frame->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
	}

	Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Menu_Scroll"));
	if (UVerticalBoxSlot* ScrollSlot = Frame->AddChildToVerticalBox(Scroll))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Menu_Column"));
	Scroll->AddChild(Column);

	FooterText = MakeText(FText::GetEmpty(), EOneBrahamTextSize::Micro, UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()));
	if (UVerticalBoxSlot* FooterSlot = Frame->AddChildToVerticalBox(FooterText))
	{
		FooterSlot->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));
	}

	bBuiltLayoutInCode = true;
}

void UOneBrahamMenuWidget::ShowPage()
{
	if (Column == nullptr)
	{
		return;	// an authored Blueprint lays itself out
	}

	Column->ClearChildren();
	Binders.Reset();
	FirstButton = nullptr;

	if (TitleText != nullptr)
	{
		TitleText->SetText(UOneBrahamUIStyle::HeaderLabel(GetPageTitle()));
	}
	if (FooterText != nullptr)
	{
		FooterText->SetText(GetFooterText());
	}

	BuildPage();

	if (Scroll != nullptr)
	{
		Scroll->ScrollToStart();
	}

	// A pad has no cursor: the first button takes focus, so the page is usable
	// from the moment it appears.
	if (FirstButton != nullptr)
	{
		FirstButton->SetKeyboardFocus();
	}
	else
	{
		SetKeyboardFocus();
	}
}

FReply UOneBrahamMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		GoBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UOneBrahamMenuWidget::GoBack()
{
	CloseMenu();
}

void UOneBrahamMenuWidget::CloseMenu()
{
	RemoveFromParent();
	OnMenuClosed.Broadcast();
}

// ==========================================================================
//  Controls
// ==========================================================================
UOneBrahamMenuBinder* UOneBrahamMenuWidget::MakeBinder()
{
	UOneBrahamMenuBinder* Binder = NewObject<UOneBrahamMenuBinder>(this);
	Binders.Add(Binder);
	return Binder;
}

UTextBlock* UOneBrahamMenuWidget::MakeText(const FText& Text, EOneBrahamTextSize Size, const FLinearColor& Colour, bool bBold)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = UOneBrahamUIStyle::Font(Size, bBold);
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Colour));
	Block->SetAutoWrapText(true);
	Block->SetText(Text);
	return Block;
}

UButton* UOneBrahamMenuWidget::MakeButton(const FText& Label, TFunction<void()> OnClick, bool bEnabled)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

	// The pause menu's three states: outline at rest, signal blue under the
	// cursor or a pad's focus, navy while held.
	FButtonStyle Style;
	Style.SetNormal(UOneBrahamUIStyle::OutlineBrush(UOneBrahamUIStyle::LightGrey(), 1.f, 4.f));
	Style.SetHovered(UOneBrahamUIStyle::SolidBrush(UOneBrahamUIStyle::SignalBlue()));
	Style.SetPressed(UOneBrahamUIStyle::SolidBrush(UOneBrahamUIStyle::Navy()));
	Style.SetDisabled(UOneBrahamUIStyle::OutlineBrush(UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()), 1.f, 4.f));
	Style.SetNormalPadding(FMargin(16.f, 10.f));
	Style.SetPressedPadding(FMargin(16.f, 10.f));
	Button->SetStyle(Style);

	UTextBlock* Text = MakeText(UOneBrahamUIStyle::HeaderLabel(Label), EOneBrahamTextSize::Medium,
		bEnabled ? UOneBrahamUIStyle::LightGrey() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()), true);
	Text->SetJustification(ETextJustify::Center);
	Button->AddChild(Text);
	Button->SetIsEnabled(bEnabled);

	UOneBrahamMenuBinder* Binder = MakeBinder();
	Binder->OnClick = MoveTemp(OnClick);
	Button->OnClicked.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleClick);

	if (FirstButton == nullptr && bEnabled)
	{
		FirstButton = Button;
	}
	return Button;
}

UTextBlock* UOneBrahamMenuWidget::AddHeading(const FText& Text)
{
	UTextBlock* Block = MakeText(UOneBrahamUIStyle::HeaderLabel(Text), EOneBrahamTextSize::Medium, UOneBrahamUIStyle::SafetyYellow(), true);
	if (UVerticalBoxSlot* HeadingSlot = Column->AddChildToVerticalBox(Block))
	{
		HeadingSlot->SetPadding(FMargin(0.f, 18.f, 0.f, 8.f));
	}
	return Block;
}

UTextBlock* UOneBrahamMenuWidget::AddText(const FText& Text, EOneBrahamTextSize Size, const FLinearColor& Colour, UPanelWidget* Parent)
{
	UTextBlock* Block = MakeText(Text, Size, Colour);
	UPanelWidget* Target = Parent != nullptr ? Parent : Column.Get();
	if (UPanelSlot* TextSlot = Target->AddChild(Block))
	{
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(TextSlot))
		{
			VerticalSlot->SetPadding(FMargin(0.f, 2.f));
		}
	}
	return Block;
}

UButton* UOneBrahamMenuWidget::AddButton(const FText& Label, TFunction<void()> OnClick, UPanelWidget* Parent, bool bEnabled)
{
	UButton* Button = MakeButton(Label, MoveTemp(OnClick), bEnabled);
	UPanelWidget* Target = Parent != nullptr ? Parent : Column.Get();
	if (UPanelSlot* ButtonSlot = Target->AddChild(Button))
	{
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(ButtonSlot))
		{
			VerticalSlot->SetPadding(FMargin(0.f, 4.f));
		}
		else if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(ButtonSlot))
		{
			HorizontalSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			HorizontalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
	return Button;
}

UHorizontalBox* UOneBrahamMenuWidget::AddRow()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Row))
	{
		RowSlot->SetPadding(FMargin(0.f, 4.f));
	}
	return Row;
}

UHorizontalBox* UOneBrahamMenuWidget::AddLabelledRow(const FText& Label, UWidget* Value)
{
	UHorizontalBox* Row = AddRow();
	UTextBlock* LabelText = MakeText(Label, EOneBrahamTextSize::Small, UOneBrahamUIStyle::LightGrey());
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (Value != nullptr)
	{
		USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ValueBox->SetWidthOverride(ColumnWidth * 0.45f);
		ValueBox->AddChild(Value);
		if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueBox))
		{
			ValueSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	return Row;
}

USlider* UOneBrahamMenuWidget::AddSlider(const FText& Label, float Min, float Max, float Step, float Value,
	TFunction<void(float)> OnChanged, TFunction<FText(float)> Format)
{
	UHorizontalBox* Value_Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetStepSize(Step);
	Slider->SetValue(Value);
	Slider->SetSliderBarColor(UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey()));
	Slider->SetSliderHandleColor(UOneBrahamUIStyle::SignalBlue());
	if (UHorizontalBoxSlot* SliderSlot = Value_Row->AddChildToHorizontalBox(Slider))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
	}

	const auto Describe = [Format](float V)
	{
		return Format ? Format(V) : FText::AsNumber(V, &FNumberFormattingOptions::DefaultNoGrouping());
	};
	UTextBlock* Readout = MakeText(Describe(Value), EOneBrahamTextSize::Small, UOneBrahamUIStyle::LightGrey());
	if (UHorizontalBoxSlot* ReadoutSlot = Value_Row->AddChildToHorizontalBox(Readout))
	{
		ReadoutSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		ReadoutSlot->SetVerticalAlignment(VAlign_Center);
	}

	UOneBrahamMenuBinder* Binder = MakeBinder();
	TWeakObjectPtr<UTextBlock> WeakReadout = Readout;
	Binder->OnFloat = [OnChanged = MoveTemp(OnChanged), Describe, WeakReadout](float NewValue)
	{
		if (UTextBlock* Text = WeakReadout.Get())
		{
			Text->SetText(Describe(NewValue));
		}
		if (OnChanged)
		{
			OnChanged(NewValue);
		}
	};
	Slider->OnValueChanged.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleFloat);

	AddLabelledRow(Label, Value_Row);
	return Slider;
}

UCheckBox* UOneBrahamMenuWidget::AddToggle(const FText& Label, bool bValue, TFunction<void(bool)> OnChanged)
{
	UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
	Check->SetIsChecked(bValue);

	UOneBrahamMenuBinder* Binder = MakeBinder();
	Binder->OnBool = MoveTemp(OnChanged);
	Check->OnCheckStateChanged.AddDynamic(Binder, &UOneBrahamMenuBinder::HandleBool);

	AddLabelledRow(Label, Check);
	return Check;
}

UButton* UOneBrahamMenuWidget::AddCycle(const FText& Label, const TArray<FText>& Options, int32 Index, TFunction<void(int32)> OnChanged)
{
	if (Options.Num() == 0)
	{
		return nullptr;
	}

	// The index lives in a shared cell: the closure outlives this call.
	TSharedRef<int32> Current = MakeShared<int32>(FMath::Clamp(Index, 0, Options.Num() - 1));
	UButton* Button = MakeButton(Options[*Current], nullptr);
	UTextBlock* ButtonLabel = Cast<UTextBlock>(Button->GetChildAt(0));
	TWeakObjectPtr<UTextBlock> WeakLabel = ButtonLabel;

	UOneBrahamMenuBinder* Binder = Binders.Last();
	Binder->OnClick = [Options, Current, WeakLabel, OnChanged = MoveTemp(OnChanged)]()
	{
		*Current = (*Current + 1) % Options.Num();
		if (UTextBlock* Text = WeakLabel.Get())
		{
			Text->SetText(UOneBrahamUIStyle::HeaderLabel(Options[*Current]));
		}
		if (OnChanged)
		{
			OnChanged(*Current);
		}
	};

	AddLabelledRow(Label, Button);
	return Button;
}
