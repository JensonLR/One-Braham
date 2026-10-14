// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamLoadingScreen.h"

#include "OneBraham.h"
#include "Styling/CoreStyle.h"
#include "UI/OneBrahamUIStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SOneBrahamLoadingScreen::Construct(const FArguments& InArgs)
{
	// The building, top floor first: one bar per floor, lit up to the player's
	// best, the Skydeck in safety yellow.
	TSharedRef<SVerticalBox> Building = SNew(SVerticalBox);
	for (int32 Floor = OneBrahamConstants::FloorSkydeck; Floor >= OneBrahamConstants::FloorLobby; --Floor)
	{
		const bool bClimbed = Floor <= InArgs._HighestFloor;
		const FLinearColor Colour = Floor == OneBrahamConstants::FloorSkydeck
			? (bClimbed ? UOneBrahamUIStyle::SafetyYellow() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::SafetyYellow()))
			: (bClimbed ? UOneBrahamUIStyle::SignalBlue() : UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey(), 0.85f));

		Building->AddSlot()
			.AutoHeight()
			.Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(28.f)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(Floor))
						.Font(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Micro))
						.ColorAndOpacity(FSlateColor(UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey())))
						.Justification(ETextJustify::Right)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(180.f)
					.HeightOverride(10.f)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor(Colour))
					]
				]
			];
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(UOneBrahamUIStyle::Navy()))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(96.f, 0.f, 0.f, 96.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(ONE_BRAHAM_DISPLAY_NAME))
				.Font(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Display, true))
				.ColorAndOpacity(FSlateColor(UOneBrahamUIStyle::LightGrey()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(UOneBrahamUIStyle::HeaderLabel(InArgs._ModeName))
				.Font(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Header, true))
				.ColorAndOpacity(FSlateColor(UOneBrahamUIStyle::SafetyYellow()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(720.f)
				[
					SNew(STextBlock)
					.Text(InArgs._Briefing)
					.AutoWrapText(true)
					.Font(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Medium))
					.ColorAndOpacity(FSlateColor(UOneBrahamUIStyle::LightGrey()))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 24.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(InArgs._Status)
				.Font(UOneBrahamUIStyle::Font(EOneBrahamTextSize::Small))
				.ColorAndOpacity(FSlateColor(UOneBrahamUIStyle::Muted(UOneBrahamUIStyle::LightGrey())))
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 120.f, 0.f)
		[
			Building
		]
	];
}
