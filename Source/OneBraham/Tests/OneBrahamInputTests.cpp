// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Input.*
//
// The control schema is five lists that have to be the same list: the Input
// Action assets the content pipeline generates, the fields on
// UOneBrahamInputConfig, the rows inside ValidateAllActionsSet, the bindings in
// Config/DefaultInput.ini, and the InputConfig-> reads in the player controller.
// They have drifted apart three times in this project's short life - three
// actions with no field and no handler (keys that did nothing), three fields read
// by the controller that were never declared, and a validator table missing
// eight of its own rows while a comment claimed a test would catch it.
//
// check_input_actions.py compares the five lists statically, and it is the tool
// that should be run on every commit. This suite is the half a static check
// cannot do: it asks the asset itself, at runtime, whether it is complete. That
// is the difference between "the table and the fields agree" and "this data
// asset will bind every key the game needs", and it is the assertion the comment
// in ValidateAllActionsSet has been promising.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamPlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** The number of Input Actions the shipped game binds. */
	constexpr int32 ExpectedActionCount = 42;

	/**
	 * Every reflected UInputAction property on the config class, in declaration
	 * order. Reflection rather than a list in the test: a test that repeats the
	 * names it is checking cannot notice a new field, which is the entire bug
	 * class this suite exists for.
	 */
	void GatherActionProperties(TArray<FObjectProperty*>& OutProperties)
	{
		OutProperties.Reset();

		for (TFieldIterator<FObjectProperty> It(UOneBrahamInputConfig::StaticClass()); It; ++It)
		{
			FObjectProperty* Property = *It;

			if (Property != nullptr && Property->PropertyClass != nullptr &&
				Property->PropertyClass->IsChildOf(UInputAction::StaticClass()))
			{
				OutProperties.Add(Property);
			}
		}
	}
}

// ===========================================================================
//  The schema is complete
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamInputAllActionsBoundTest, "OneBraham.Input.AllActionsBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamInputAllActionsBoundTest::RunTest(const FString& Parameters)
{
	UOneBrahamInputConfig* Config = NewObject<UOneBrahamInputConfig>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Config == nullptr)
	{
		AddError(TEXT("Could not create a transient input config."));
		return false;
	}

	// 42 actions: movement and stance (mouse and stick look apart), weapons,
	// interaction, voice, the UI, and the three overlays. The count is documentation - Docs/03 and the README
	// both quote it - and check_input_actions.py keeps the five lists at this
	// number, so a change here is a change to the control schema.
	TArray<FObjectProperty*> Properties;
	GatherActionProperties(Properties);

	TestEqual(TEXT("the config declares one field per action"), Properties.Num(), ExpectedActionCount);

	// A brand new config has nothing assigned, and the validator has to say so
	// about every single field. A validator that misses a field is a key that
	// does nothing at run time with no warning in the log.
	{
		TArray<FName> Missing;
		const bool bValid = Config->ValidateAllActionsSet(Missing);

		TestFalse(TEXT("an empty config is not valid"), bValid);
		TestEqual(TEXT("and every field is reported missing"), Missing.Num(), Properties.Num());

		// No duplicates: the report is read by a human at possession time.
		TSet<FName> Unique(Missing);
		TestEqual(TEXT("the missing list has no duplicates"), Unique.Num(), Missing.Num());

		// Every reflected field is named in the report, and - the direction the
		// eight-row drift went - every row in the table names a real field.
		for (const FObjectProperty* Property : Properties)
		{
			TestTrue(*FString::Printf(TEXT("%s is validated"), *Property->GetName()),
				Missing.Contains(Property->GetFName()));
		}
	}

	// Now fill every one of them, through reflection, and the same validator has
	// to come back clean.
	for (FObjectProperty* Property : Properties)
	{
		UInputAction* Action = NewObject<UInputAction>(Config, NAME_None, RF_Transient);

		if (Action != nullptr)
		{
			Property->SetObjectPropertyValue_InContainer(Config, Action);
		}
	}

	{
		TArray<FName> Missing;
		const bool bValid = Config->ValidateAllActionsSet(Missing);

		if (!bValid)
		{
			for (const FName& Name : Missing)
			{
				AddError(FString::Printf(TEXT("still missing after filling: %s"), *Name.ToString()));
			}
		}

		TestTrue(TEXT("a fully assigned config validates"), bValid);
		TestEqual(TEXT("with nothing missing"), Missing.Num(), 0);
	}

	// The three overlay actions are the ones this suite was written for. They are
	// asserted by name because their absence is what shipped the first time.
	const FName Overlays[] = { TEXT("ToggleHUD"), TEXT("Map"), TEXT("VoiceLineMenu") };

	for (const FName& Overlay : Overlays)
	{
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UOneBrahamInputConfig::StaticClass(), Overlay);

		TestNotNull(*FString::Printf(TEXT("%s is declared"), *Overlay.ToString()), Property);

		if (Property != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("%s is an input action field"), *Overlay.ToString()),
				Property->PropertyClass != nullptr && Property->PropertyClass->IsChildOf(UInputAction::StaticClass()));
			TestNotNull(*FString::Printf(TEXT("%s is assigned in the test config"), *Overlay.ToString()),
				Property->GetObjectPropertyValue_InContainer(Config));
		}
	}

	return true;
}

// ===========================================================================
//  The settings point at content that exists
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamInputContextsTest, "OneBraham.Input.MappingContexts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamInputContextsTest::RunTest(const FString& Parameters)
{
	const UOneBrahamGameSettings& Settings = UOneBrahamGameSettings::Get();

	// Two mapping contexts: one the game always has (Move, Look, Interact) and one
	// the game swaps in and out (weapons, the wheel, the map). Neither has a
	// compiled-in default and neither is written into the ini: the controller
	// resolves settings field, then conventional content path, then a runtime
	// default (Docs/02 section 1b), so the fields being absent on a fresh clone is
	// correct and the thing to hold still is their *type*. A hard reference here
	// would pull the context - and everything it points at - into memory at
	// startup, on a dedicated server, for a player who will never press a key.
	{
		const FSoftObjectProperty* Default = FindFProperty<FSoftObjectProperty>(
			UOneBrahamGameSettings::StaticClass(), TEXT("DefaultMappingContext"));
		const FSoftObjectProperty* Common = FindFProperty<FSoftObjectProperty>(
			UOneBrahamGameSettings::StaticClass(), TEXT("CommonMappingContext"));

		TestNotNull(TEXT("the default mapping context is a property"), Default);
		TestNotNull(TEXT("the common mapping context is a property"), Common);

		if (Default != nullptr && Default->PropertyClass != nullptr)
		{
			TestTrue(TEXT("the default mapping context is a soft reference to a context"),
				Default->PropertyClass->IsChildOf(UInputMappingContext::StaticClass()));
		}

		if (Common != nullptr && Common->PropertyClass != nullptr)
		{
			TestTrue(TEXT("the common mapping context is a soft reference to a context"),
				Common->PropertyClass->IsChildOf(UInputMappingContext::StaticClass()));
		}
	}

	// The interaction pair is two verbs, not two bindings of one: a tap opens a
	// door, a hold overrides a terminal for twelve seconds, and the player has to
	// be able to tell which one they are doing.
	const FObjectProperty* Interact = FindFProperty<FObjectProperty>(
		UOneBrahamInputConfig::StaticClass(), TEXT("Interact"));
	const FObjectProperty* InteractHold = FindFProperty<FObjectProperty>(
		UOneBrahamInputConfig::StaticClass(), TEXT("InteractHold"));

	TestNotNull(TEXT("Interact exists"), Interact);
	TestNotNull(TEXT("InteractHold exists"), InteractHold);
	TestTrue(TEXT("they are different fields"), Interact != InteractHold);

	// Push to talk is held, and it is the same key the voice subsystem listens
	// for: the two must not be separate settings, or a player rebinding one would
	// keep transmitting on the other.
	const FObjectProperty* PushToTalk = FindFProperty<FObjectProperty>(
		UOneBrahamInputConfig::StaticClass(), TEXT("PushToTalk"));
	TestNotNull(TEXT("PushToTalk exists"), PushToTalk);

	// The unbound-by-design action: clip marking is for the trailer and the QA
	// recordings, so it exists in the schema and has no default key. That is a
	// deliberate hole rather than a forgotten one, and this test is where that
	// gets written down.
	const FObjectProperty* ClipMarker = FindFProperty<FObjectProperty>(
		UOneBrahamInputConfig::StaticClass(), TEXT("ClipMarker"));
	TestNotNull(TEXT("ClipMarker exists"), ClipMarker);

	return true;
}

// ===========================================================================
//  The native controller ships with input
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamInputControllerDefaultsTest, "OneBraham.Input.ControllerDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamInputControllerDefaultsTest::RunTest(const FString& Parameters)
{
	// The game mode spawns AOneBrahamPlayerController itself - there is no
	// Blueprint subclass - so whatever the class default carries is what a
	// packaged client binds. The first packaged build carried nothing, and the
	// editor never said so because nobody plays the CDO.
	const AOneBrahamPlayerController* Controller = GetDefault<AOneBrahamPlayerController>();

	if (!TestNotNull(TEXT("the controller has a class default"), Controller))
	{
		return false;
	}

	const UOneBrahamInputConfig* Config = Controller->InputConfig;

	if (!TestNotNull(TEXT("the native controller has a default InputConfig"), Config))
	{
		return false;
	}

	TestEqual(TEXT("and it is the cooked DA_InputConfig"),
		Config->GetPathName(), FString(TEXT("/Game/OneBraham/Data/DA_InputConfig.DA_InputConfig")));

	{
		TArray<FName> Missing;
		const bool bValid = Config->ValidateAllActionsSet(Missing);

		for (const FName& Name : Missing)
		{
			AddError(FString::Printf(TEXT("DA_InputConfig has no action for %s"), *Name.ToString()));
		}

		TestTrue(TEXT("every action on the default config is set"), bValid);
	}

	// An action with nothing pressing it is as dead as an unset field. Both the
	// authored contexts the controller installs (Default + UI) and the ini-built
	// fallback have to reach every action - all of them except the clip marker,
	// which is unbound by design (see MappingContexts above).
	TArray<FObjectProperty*> Properties;
	GatherActionProperties(Properties);

	auto TestEveryActionMapped = [this, Config, &Properties](const TCHAR* Source, TArray<const UInputMappingContext*> Contexts)
	{
		TSet<const UInputAction*> Mapped;
		for (const UInputMappingContext* Each : Contexts)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Each->GetMappings())
			{
				TestTrue(*FString::Printf(TEXT("%s: every mapping has a valid key"), Source), Mapping.Key.IsValid());
				Mapped.Add(Mapping.Action);
			}
		}

		for (const FObjectProperty* Property : Properties)
		{
			const UInputAction* Action = Cast<UInputAction>(Property->GetObjectPropertyValue_InContainer(Config));

			if (Property->GetFName() == TEXT("ClipMarker"))
			{
				continue;
			}

			TestTrue(*FString::Printf(TEXT("%s: %s has a key"), Source, *Property->GetName()),
				Action != nullptr && Mapped.Contains(Action));
		}
	};

	// Mouse and stick look are two actions: Input_Look applies a mouse delta as
	// is, Input_LookGamepad scales a stick deflection by the frame time. A stick
	// on IA_Look turns faster the higher the frame rate, which is how it shipped.
	TestNotNull(TEXT("the default config has a LookGamepad action"), Config->LookGamepad.Get());

	auto TestLookSplit = [this, Config](const TCHAR* Source, const UInputMappingContext* Context)
	{
		bool bMouseLook = false;
		bool bStickLook = false;

		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Config->Look)
			{
				bMouseLook |= Mapping.Key == EKeys::Mouse2D;
				TestFalse(*FString::Printf(TEXT("%s: Look has no pad key (%s)"), Source, *Mapping.Key.ToString()),
					Mapping.Key.IsGamepadKey());
			}
			else if (Mapping.Action == Config->LookGamepad)
			{
				TestTrue(*FString::Printf(TEXT("%s: LookGamepad has only pad keys (%s)"), Source, *Mapping.Key.ToString()),
					Mapping.Key.IsGamepadKey());

				if (Mapping.Key == EKeys::Gamepad_Right2D)
				{
					bStickLook = true;
					TestTrue(*FString::Printf(TEXT("%s: the right stick has a dead zone"), Source),
						Mapping.Modifiers.ContainsByPredicate([](const UInputModifier* Modifier)
						{
							return Modifier != nullptr && Modifier->IsA<UInputModifierDeadZone>();
						}));
				}
			}
		}

		TestTrue(*FString::Printf(TEXT("%s: Look is on the mouse"), Source), bMouseLook);
		TestTrue(*FString::Printf(TEXT("%s: LookGamepad is on the right stick"), Source), bStickLook);
	};

	{
		const UInputMappingContext* Authored = LoadObject<UInputMappingContext>(nullptr,
			TEXT("/Game/OneBraham/Input/IMC_OneBraham_Default.IMC_OneBraham_Default"));
		const UInputMappingContext* AuthoredUI = LoadObject<UInputMappingContext>(nullptr,
			TEXT("/Game/OneBraham/Input/IMC_OneBraham_UI.IMC_OneBraham_UI"));

		if (TestNotNull(TEXT("IMC_OneBraham_Default exists"), Authored)
			&& TestNotNull(TEXT("IMC_OneBraham_UI exists"), AuthoredUI))
		{
			TestEveryActionMapped(TEXT("authored contexts"), { Authored, AuthoredUI });
			TestLookSplit(TEXT("IMC_OneBraham_Default"), Authored);
		}
	}

	UInputMappingContext* Context = Controller->BuildMappingContextFromIni(GetTransientPackage());

	if (!TestNotNull(TEXT("a mapping context builds from [OneBraham.Input]"), Context))
	{
		return false;
	}

	TestEveryActionMapped(TEXT("[OneBraham.Input]"), { Context });
	TestLookSplit(TEXT("[OneBraham.Input]"), Context);

	// Move has to work on a keyboard, not just a pad.
	int32 MoveKeys = 0;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (Mapping.Action == Config->Move && !Mapping.Key.IsGamepadKey())
		{
			++MoveKeys;
			TestTrue(TEXT("W, A and S carry a direction modifier"),
				Mapping.Key == EKeys::D || Mapping.Modifiers.Num() > 0);
		}
	}

	TestEqual(TEXT("Move has four keyboard keys"), MoveKeys, 4);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
