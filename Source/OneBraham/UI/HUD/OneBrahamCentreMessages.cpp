// Copyright (c) One Braham. All Rights Reserved.

#include "UI/HUD/OneBrahamCentreMessages.h"

int32 FOneBrahamCentreLayout::Count(EOneBrahamCentreShow What) const
{
	int32 N = 0;
	for (const EOneBrahamCentreShow Each : Show)
	{
		N += Each == What ? 1 : 0;
	}
	return N;
}

bool OneBrahamCentre::CanTicker(EOneBrahamCentreSlot Slot, EOneBrahamCentreSlot Card)
{
	switch (Slot)
	{
	case EOneBrahamCentreSlot::StageCard:
		// Only the downed medallion outranks it: the card's word still reaches
		// the player, as a line under the medallion.
		return true;
	case EOneBrahamCentreSlot::Announcement:
		// A PA card is a paragraph: as one line it would be half a sentence.
		// Its queue waits instead, and the subtitle carries the words.
		return false;
	case EOneBrahamCentreSlot::Banner:
		return true;
	case EOneBrahamCentreSlot::BlockedPrompt:
	case EOneBrahamCentreSlot::Induction:
		// Down, they are instructions the player cannot follow.
		return Card != EOneBrahamCentreSlot::Downed;
	case EOneBrahamCentreSlot::Downed:
	default:
		return false;
	}
}

FOneBrahamCentreLayout OneBrahamCentre::Resolve(uint32 ActiveMask)
{
	FOneBrahamCentreLayout Layout;
	EOneBrahamCentreSlot Card = EOneBrahamCentreSlot::Count;
	bool bTickerTaken = false;

	for (int32 Index = 0; Index < static_cast<int32>(EOneBrahamCentreSlot::Count); ++Index)
	{
		const EOneBrahamCentreSlot Slot = static_cast<EOneBrahamCentreSlot>(Index);
		if ((ActiveMask & Bit(Slot)) == 0)
		{
			continue;
		}

		if (Card == EOneBrahamCentreSlot::Count)
		{
			Card = Slot;
			Layout.Show[Index] = EOneBrahamCentreShow::Card;
		}
		else if (!bTickerTaken && CanTicker(Slot, Card))
		{
			bTickerTaken = true;
			Layout.Show[Index] = EOneBrahamCentreShow::Ticker;
		}
		else
		{
			Layout.Show[Index] = EOneBrahamCentreShow::Wait;
		}
	}
	return Layout;
}

FString OneBrahamCentre::Words(const FString& Text)
{
	FString Out;
	Out.Reserve(Text.Len());
	bool bSpace = false;
	for (const TCHAR Char : Text)
	{
		if (FChar::IsAlnum(Char))
		{
			if (bSpace && !Out.IsEmpty())
			{
				Out.AppendChar(TEXT(' '));
			}
			bSpace = false;
			Out.AppendChar(FChar::ToLower(Char));
		}
		else if (Char != TEXT('\'') && Char != TEXT('’'))
		{
			// An apostrophe joins ("they've"); everything else separates.
			bSpace = true;
		}
	}
	return Out;
}

FString OneBrahamCentre::SubtitleRemainder(const FString& Subtitle, const TArray<FString>& CardLines)
{
	TArray<FString> Cards;
	for (const FString& Line : CardLines)
	{
		const FString W = Words(Line);
		if (!W.IsEmpty())
		{
			Cards.Add(TEXT(" ") + W + TEXT(" "));
		}
	}
	if (Cards.Num() == 0)
	{
		return Subtitle;
	}

	// The sentences, each with its own punctuation.
	TArray<FString> Sentences;
	FString Current;
	for (int32 Index = 0; Index < Subtitle.Len(); ++Index)
	{
		const TCHAR Char = Subtitle[Index];
		Current.AppendChar(Char);
		const bool bStop = Char == TEXT('.') || Char == TEXT('!') || Char == TEXT('?');
		const bool bEnd = Index + 1 >= Subtitle.Len() || FChar::IsWhitespace(Subtitle[Index + 1]);
		if (bStop && bEnd)
		{
			Sentences.Add(Current.TrimStartAndEnd());
			Current.Reset();
		}
	}
	if (!Current.TrimStartAndEnd().IsEmpty())
	{
		Sentences.Add(Current.TrimStartAndEnd());
	}

	TArray<FString> Kept;
	for (const FString& Sentence : Sentences)
	{
		const FString W = Words(Sentence);
		if (W.IsEmpty())
		{
			continue;
		}
		const FString Padded = TEXT(" ") + W + TEXT(" ");
		const bool bShown = Cards.ContainsByPredicate([&Padded](const FString& Card) { return Card.Contains(Padded); });
		if (!bShown)
		{
			Kept.Add(Sentence);
		}
	}

	// What is left of a PA paragraph once its point is on a card can be only
	// the building clearing its throat: that is not worth a line.
	static const TCHAR* Preambles[] = { TEXT("attention all staff"), TEXT("attention"), TEXT("all staff") };
	bool bOnlyPreamble = Kept.Num() > 0;
	for (const FString& Sentence : Kept)
	{
		const FString W = Words(Sentence);
		bool bPreamble = false;
		for (const TCHAR* P : Preambles)
		{
			bPreamble |= W.Equals(P);
		}
		bOnlyPreamble &= bPreamble;
	}
	if (bOnlyPreamble || Kept.Num() == 0)
	{
		return FString();
	}
	return Kept.Num() == Sentences.Num() ? Subtitle : FString::Join(Kept, TEXT(" "));
}
