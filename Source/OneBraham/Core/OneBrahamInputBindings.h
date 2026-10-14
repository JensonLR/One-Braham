// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInputBindings.h
// ---------------------------------------------------------------------------
// The key bindings, read and written.
//
// The defaults are [OneBraham.Input] in DefaultInput.ini, generated from
// build_input.py. A player's rebinds are the same section in
// GameUserSettings.ini. An action that appears there is taken wholly from it,
// so a rebind always writes every key of the action it changes: the four keys
// of Move included, or a rebound W would leave the old W still pressing
// forward.
//
// Writing a rebind saves the file and broadcasts the settings change, and
// every local controller rebuilds its mapping context from here
// (AOneBrahamPlayerController::RefreshBindingsContext). The new key works
// immediately, in a menu or mid-match.
//
// CONFLICTS
//   A key belongs to one action per device. Binding a key that another action
//   holds swaps them: the other action takes the key being replaced. Nothing
//   is ever left silently doing two things, and nothing is left unbound that
//   was bound before.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OneBrahamInputBindings.generated.h"

/** One [OneBraham.Input] row: `IA_Move,S,Swizzle+Negate`. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamKeyBinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Input")
	FName Action = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Input")
	FKey Key;

	/** Swizzle, Negate: how a key feeds a 2D action. Kept through a rebind. */
	UPROPERTY(BlueprintReadOnly, Category = "Input")
	TArray<FString> Modifiers;

	/** True when this row comes from the player's file, not the defaults. */
	UPROPERTY(BlueprintReadOnly, Category = "Input")
	bool bRebound = false;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamInputBindings : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static bool ParseBinding(const FString& Value, FOneBrahamKeyBinding& Out);
	static FString FormatBinding(const FOneBrahamKeyBinding& Binding);

	/** Every effective binding for one device: the player's rows, then the defaults they do not replace. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Input")
	static TArray<FOneBrahamKeyBinding> GetBindings(bool bGamepad);

	/** Every action the controls screen lists, in the order of the generated prompts. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Input")
	static TArray<FName> GetRebindableActions();

	/** The one-line description build_input.py wrote for an action. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Input")
	static FText GetActionDescription(FName Action);

	/** True when a key may be bound on a device at all. */
	/**
	 * What a prompt or the rebind screen calls a key. Pad buttons get the names
	 * printed on an Xbox controller (A, RB, LT, View); the engine's own names
	 * ("Gamepad Face Button Bottom") mean nothing to a player. Everything else
	 * is the engine's short display name.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|Input")
	static FText GetKeyLabel(const FKey& Key);

	UFUNCTION(BlueprintPure, Category = "One Braham|Input")
	static bool IsKeyBindable(const FKey& Key, bool bGamepad);

	/** The other action holding Key on a device, or NAME_None. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Input")
	static FName FindConflict(const FKey& Key, bool bGamepad, FName IgnoreAction);

	/**
	 * Replaces OldKey with NewKey on Action (OldKey invalid adds NewKey as a new
	 * row). A conflicting action takes OldKey. Writes and applies. Returns the
	 * action that was swapped, NAME_None when none was, or fails (false) for a
	 * key that cannot be bound.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Input")
	static bool Rebind(FName Action, const FKey& OldKey, const FKey& NewKey, bool bGamepad, FName& OutSwappedAction);

	/** Removes every rebind for a device and applies the defaults. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Input")
	static void ResetBindings(bool bGamepad);

	static const TCHAR* ArrayKey(bool bGamepad) { return bGamepad ? TEXT("GamepadBinding") : TEXT("KeyboardBinding"); }

private:
	/** Writes the player's rows for a device, saves the file and tells every controller. */
	static void WriteRebinds(bool bGamepad, const TArray<FOneBrahamKeyBinding>& Rows);

	/** The player's rows only. */
	static TArray<FOneBrahamKeyBinding> ReadRebinds(bool bGamepad);
};
