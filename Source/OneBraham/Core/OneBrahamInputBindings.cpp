// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamInputBindings.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* const InputSection = TEXT("OneBraham.Input");

	/** Axes and the look actions: what a key cannot sensibly replace. */
	const TSet<FName>& FixedActions()
	{
		static const TSet<FName> Actions = { FName(TEXT("IA_Look")), FName(TEXT("IA_LookGamepad")) };
		return Actions;
	}
}

bool UOneBrahamInputBindings::ParseBinding(const FString& Value, FOneBrahamKeyBinding& Out)
{
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(","), /*bCullEmpty*/ true);

	if (Parts.Num() < 2)
	{
		return false;
	}

	Out.Action = FName(*Parts[0].TrimStartAndEnd());
	Out.Key = FKey(FName(*Parts[1].TrimStartAndEnd()));
	Out.Modifiers.Reset();

	if (Parts.Num() > 2)
	{
		Parts[2].TrimStartAndEnd().ParseIntoArray(Out.Modifiers, TEXT("+"), /*bCullEmpty*/ true);
	}

	return Out.Key.IsValid();
}

FString UOneBrahamInputBindings::FormatBinding(const FOneBrahamKeyBinding& Binding)
{
	FString Value = FString::Printf(TEXT("%s,%s"), *Binding.Action.ToString(), *Binding.Key.GetFName().ToString());
	if (Binding.Modifiers.Num() > 0)
	{
		Value += TEXT(",") + FString::Join(Binding.Modifiers, TEXT("+"));
	}
	return Value;
}

TArray<FOneBrahamKeyBinding> UOneBrahamInputBindings::ReadRebinds(bool bGamepad)
{
	TArray<FOneBrahamKeyBinding> Rows;
	if (GConfig == nullptr)
	{
		return Rows;
	}

	TArray<FString> Values;
	GConfig->GetArray(InputSection, ArrayKey(bGamepad), Values, GGameUserSettingsIni);
	for (const FString& Value : Values)
	{
		FOneBrahamKeyBinding Binding;
		if (ParseBinding(Value, Binding))
		{
			Binding.bRebound = true;
			Rows.Add(MoveTemp(Binding));
		}
	}
	return Rows;
}

TArray<FOneBrahamKeyBinding> UOneBrahamInputBindings::GetBindings(bool bGamepad)
{
	TArray<FOneBrahamKeyBinding> Out;
	if (GConfig == nullptr)
	{
		return Out;
	}

	TArray<FOneBrahamKeyBinding> Player = ReadRebinds(bGamepad);
	TSet<FName> Rebound;
	for (const FOneBrahamKeyBinding& Binding : Player)
	{
		Rebound.Add(Binding.Action);
	}

	TArray<FString> Defaults;
	GConfig->GetArray(InputSection, ArrayKey(bGamepad), Defaults, GInputIni);
	for (const FString& Value : Defaults)
	{
		FOneBrahamKeyBinding Binding;
		if (ParseBinding(Value, Binding))
		{
			if (!Rebound.Contains(Binding.Action))
			{
				Out.Add(MoveTemp(Binding));
			}
		}
		else
		{
			OB_WARN("[OneBraham.Input] %s=%s names a key this engine does not have; skipped.", ArrayKey(bGamepad), *Value);
		}
	}

	Out.Append(MoveTemp(Player));
	return Out;
}

TArray<FName> UOneBrahamInputBindings::GetRebindableActions()
{
	TArray<FName> Actions;
	if (GConfig == nullptr)
	{
		return Actions;
	}

	TArray<FString> Prompts;
	GConfig->GetArray(InputSection, TEXT("ActionPrompt"), Prompts, GInputIni);
	for (const FString& Prompt : Prompts)
	{
		FString Action;
		FString Description;
		if (Prompt.Split(TEXT("="), &Action, &Description))
		{
			const FName ActionName(*Action.TrimStartAndEnd());
			if (!FixedActions().Contains(ActionName))
			{
				Actions.AddUnique(ActionName);
			}
		}
	}
	return Actions;
}

FText UOneBrahamInputBindings::GetActionDescription(FName Action)
{
	if (GConfig != nullptr)
	{
		TArray<FString> Prompts;
		GConfig->GetArray(InputSection, TEXT("ActionPrompt"), Prompts, GInputIni);
		const FString Prefix = Action.ToString() + TEXT("=");
		for (const FString& Prompt : Prompts)
		{
			if (Prompt.StartsWith(Prefix))
			{
				return FText::FromString(Prompt.RightChop(Prefix.Len()));
			}
		}
	}
	return FText::FromName(Action);
}

bool UOneBrahamInputBindings::IsKeyBindable(const FKey& Key, bool bGamepad)
{
	if (!Key.IsValid() || Key.IsGamepadKey() != bGamepad)
	{
		return false;
	}

	// The axes are not keys to press, and the console key is the console's.
	if (Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || Key == EKeys::Tilde)
	{
		return false;
	}
	return true;
}

FName UOneBrahamInputBindings::FindConflict(const FKey& Key, bool bGamepad, FName IgnoreAction)
{
	for (const FOneBrahamKeyBinding& Binding : GetBindings(bGamepad))
	{
		if (Binding.Key == Key && Binding.Action != IgnoreAction && !FixedActions().Contains(Binding.Action))
		{
			return Binding.Action;
		}
	}
	return NAME_None;
}

bool UOneBrahamInputBindings::Rebind(FName Action, const FKey& OldKey, const FKey& NewKey, bool bGamepad, FName& OutSwappedAction)
{
	OutSwappedAction = NAME_None;
	if (Action.IsNone() || FixedActions().Contains(Action) || !IsKeyBindable(NewKey, bGamepad) || OldKey == NewKey)
	{
		return false;
	}

	const TArray<FOneBrahamKeyBinding> Effective = GetBindings(bGamepad);
	const FName Conflict = FindConflict(NewKey, bGamepad, Action);

	// The player's file after the change: every row it already has for other
	// actions, then the whole of each action this rebind touches.
	TArray<FOneBrahamKeyBinding> Rows;
	for (const FOneBrahamKeyBinding& Row : ReadRebinds(bGamepad))
	{
		if (Row.Action != Action && Row.Action != Conflict)
		{
			Rows.Add(Row);
		}
	}

	bool bReplaced = false;
	for (const FOneBrahamKeyBinding& Row : Effective)
	{
		if (Row.Action == Action)
		{
			FOneBrahamKeyBinding Copy = Row;
			if (!bReplaced && OldKey.IsValid() && Copy.Key == OldKey)
			{
				Copy.Key = NewKey;
				bReplaced = true;
			}
			Rows.Add(Copy);
		}
		else if (!Conflict.IsNone() && Row.Action == Conflict)
		{
			FOneBrahamKeyBinding Copy = Row;
			if (Copy.Key == NewKey)
			{
				if (!OldKey.IsValid())
				{
					continue;	// a new key, nothing to give back: the other action loses it
				}
				Copy.Key = OldKey;
			}
			Rows.Add(Copy);
		}
	}

	if (!bReplaced)
	{
		// No old key, or the old key is not the action's: add a row.
		FOneBrahamKeyBinding Added;
		Added.Action = Action;
		Added.Key = NewKey;
		Rows.Add(Added);
	}

	OutSwappedAction = Conflict;
	WriteRebinds(bGamepad, Rows);
	OB_LOG("Rebound %s: %s -> %s%s", *Action.ToString(), *OldKey.GetFName().ToString(), *NewKey.GetFName().ToString(),
		Conflict.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (swapped with %s)"), *Conflict.ToString()));
	return true;
}

void UOneBrahamInputBindings::ResetBindings(bool bGamepad)
{
	WriteRebinds(bGamepad, TArray<FOneBrahamKeyBinding>());
}

void UOneBrahamInputBindings::WriteRebinds(bool bGamepad, const TArray<FOneBrahamKeyBinding>& Rows)
{
	if (GConfig == nullptr)
	{
		return;
	}

	TArray<FString> Values;
	for (const FOneBrahamKeyBinding& Row : Rows)
	{
		Values.Add(FormatBinding(Row));
	}

	if (Values.Num() > 0)
	{
		GConfig->SetArray(InputSection, ArrayKey(bGamepad), Values, GGameUserSettingsIni);
	}
	else
	{
		GConfig->RemoveKey(InputSection, ArrayKey(bGamepad), GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);

	// Every local controller listens for this and rebuilds its context.
	if (UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings())
	{
		Settings->OnSettingsChanged.Broadcast();
	}
}
