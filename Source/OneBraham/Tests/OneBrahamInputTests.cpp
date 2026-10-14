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
#include "Core/OneBrahamInputBindings.h"
#include "Core/OneBrahamAimAssist.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "InputAction.h"
#include "InputMappingContext.h"
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

	// 41 actions: movement and stance, weapons, interaction, voice, the UI, and
	// the three overlays. The count is documentation - Docs/03 and the README
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
//  Rebinding: the write path the controls screen uses
// ===========================================================================
namespace
{
	TArray<FKey> KeysOf(const TArray<FOneBrahamKeyBinding>& Bindings, const TCHAR* Action)
	{
		TArray<FKey> Keys;
		for (const FOneBrahamKeyBinding& Binding : Bindings)
		{
			if (Binding.Action == FName(Action))
			{
				Keys.Add(Binding.Key);
			}
		}
		return Keys;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamInputRebindTest, "OneBraham.Input.Rebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamInputRebindTest::RunTest(const FString& Parameters)
{
	// The player's own keyboard rows are put back afterwards, whatever happens.
	TArray<FString> Saved;
	GConfig->GetArray(TEXT("OneBraham.Input"), TEXT("KeyboardBinding"), Saved, GGameUserSettingsIni);
	ON_SCOPE_EXIT
	{
		if (Saved.Num() > 0)
		{
			GConfig->SetArray(TEXT("OneBraham.Input"), TEXT("KeyboardBinding"), Saved, GGameUserSettingsIni);
		}
		else
		{
			GConfig->RemoveKey(TEXT("OneBraham.Input"), TEXT("KeyboardBinding"), GGameUserSettingsIni);
		}
	};

	UOneBrahamInputBindings::ResetBindings(false);
	TestTrue(TEXT("defaults: Reload is R"), KeysOf(UOneBrahamInputBindings::GetBindings(false), TEXT("IA_Reload")) == TArray<FKey>{ EKeys::R });

	// A plain rebind replaces the key and nothing else.
	FName Swapped;
	TestTrue(TEXT("R -> T"), UOneBrahamInputBindings::Rebind(TEXT("IA_Reload"), EKeys::R, EKeys::T, false, Swapped));
	TestTrue(TEXT("nothing swapped"), Swapped.IsNone());
	TestTrue(TEXT("Reload is T"), KeysOf(UOneBrahamInputBindings::GetBindings(false), TEXT("IA_Reload")) == TArray<FKey>{ EKeys::T });

	// Taking another action's key swaps: Interact gets the key Reload gave up.
	TestTrue(TEXT("T -> F"), UOneBrahamInputBindings::Rebind(TEXT("IA_Reload"), EKeys::T, EKeys::F, false, Swapped));
	TestTrue(TEXT("swapped with Interact"), Swapped == FName(TEXT("IA_Interact")));
	const TArray<FOneBrahamKeyBinding> AfterSwap = UOneBrahamInputBindings::GetBindings(false);
	TestTrue(TEXT("Reload is F"), KeysOf(AfterSwap, TEXT("IA_Reload")) == TArray<FKey>{ EKeys::F });
	TestTrue(TEXT("Interact is T"), KeysOf(AfterSwap, TEXT("IA_Interact")) == TArray<FKey>{ EKeys::T });
	TestTrue(TEXT("nothing else holds F"), UOneBrahamInputBindings::FindConflict(EKeys::F, false, TEXT("IA_Reload")).IsNone());

	// Rebinding one key of Move writes all four, modifiers intact.
	TestTrue(TEXT("W -> Up"), UOneBrahamInputBindings::Rebind(TEXT("IA_Move"), EKeys::W, EKeys::Up, false, Swapped));
	int32 MoveRows = 0;
	for (const FOneBrahamKeyBinding& Binding : UOneBrahamInputBindings::GetBindings(false))
	{
		if (Binding.Action == FName(TEXT("IA_Move")))
		{
			++MoveRows;
			TestTrue(TEXT("W no longer moves"), Binding.Key != EKeys::W);
			if (Binding.Key == EKeys::Up)
			{
				TestTrue(TEXT("Up keeps W's swizzle"), Binding.Modifiers == TArray<FString>{ TEXT("Swizzle") });
			}
		}
	}
	TestEqual(TEXT("Move keeps four keys"), MoveRows, 4);

	// Keys a device cannot bind are refused, and the look axes are fixed.
	TestFalse(TEXT("a pad button is not a keyboard key"), UOneBrahamInputBindings::Rebind(TEXT("IA_Jump"), EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, false, Swapped));
	TestFalse(TEXT("the console key is the console's"), UOneBrahamInputBindings::IsKeyBindable(EKeys::Tilde, false));
	TestFalse(TEXT("look is not rebindable"), UOneBrahamInputBindings::GetRebindableActions().Contains(FName(TEXT("IA_Look"))));

	// Reset gives the defaults back.
	UOneBrahamInputBindings::ResetBindings(false);
	TestTrue(TEXT("reset: Interact is F"), KeysOf(UOneBrahamInputBindings::GetBindings(false), TEXT("IA_Interact")) == TArray<FKey>{ EKeys::F });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamAimAssistTest, "OneBraham.Input.AimAssist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamAimAssistTest::RunTest(const FString& Parameters)
{
	const FVector Eye = FVector::ZeroVector;
	const FRotator Ahead = FRotator::ZeroRotator; // looking down +X
	const float Dt = 1.f / 60.f;

	auto One = [](const FVector& Location, const FVector& Velocity = FVector::ZeroVector)
	{
		FOneBrahamAimAssistTarget Target;
		Target.Location = Location;
		Target.Velocity = Velocity;
		return TArray<FOneBrahamAimAssistTarget>{ Target };
	};

	// Dead ahead, standing still: full slowdown for the strength, no follow.
	FOneBrahamAimAssistResult R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(FVector(1000.f, 0.f, 0.f)), 0.6f, false, Dt);
	TestEqual(TEXT("ahead: target chosen"), R.TargetIndex, 0);
	TestEqual(TEXT("ahead: slowdown is strength x hip maximum"), R.LookScale, 1.f - FOneBrahamAimAssist::MaxSlowdownHip * 0.6f, 0.001f);
	TestTrue(TEXT("ahead, still: no follow"), R.Follow.IsNearlyZero());

	// Strength zero is off.
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(FVector(1000.f, 0.f, 0.f)), 0.f, false, Dt);
	TestEqual(TEXT("strength 0: nothing"), R.TargetIndex, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("strength 0: full speed"), R.LookScale, 1.f);

	// Twenty degrees off is outside every cone.
	const FVector Wide = FRotator(0.f, 20.f, 0.f).Vector() * 1000.f;
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(Wide), 1.f, true, Dt);
	TestEqual(TEXT("20 degrees off: nothing"), R.TargetIndex, static_cast<int32>(INDEX_NONE));

	// Cone: shrinks with distance, inside its floor and ceiling.
	TestEqual(TEXT("cone ceiling up close"), FOneBrahamAimAssist::ConeHalfAngleDegrees(100.f, false), FOneBrahamAimAssist::MaxConeDegrees);
	TestEqual(TEXT("cone floor far away"), FOneBrahamAimAssist::ConeHalfAngleDegrees(100000.f, false), FOneBrahamAimAssist::MinConeDegrees);
	TestTrue(TEXT("cone shrinks with distance"),
		FOneBrahamAimAssist::ConeHalfAngleDegrees(1500.f, false) < FOneBrahamAimAssist::ConeHalfAngleDegrees(900.f, false));

	// Range: 45 m is past hip range and inside ADS range.
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(FVector(4500.f, 0.f, 0.f)), 1.f, false, Dt);
	TestEqual(TEXT("45 m from the hip: nothing"), R.TargetIndex, static_cast<int32>(INDEX_NONE));
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(FVector(4500.f, 0.f, 0.f)), 1.f, true, Dt);
	TestEqual(TEXT("45 m down sights: assisted"), R.TargetIndex, 0);

	// A target running to the player's right pulls the view right, and no
	// faster than the cap however fast it runs.
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(FVector(1000.f, 0.f, 0.f), FVector(0.f, 400.f, 0.f)), 1.f, false, Dt);
	TestTrue(TEXT("strafing right: follow turns right"), R.Follow.Yaw > 0.f);
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, One(FVector(300.f, 0.f, 0.f), FVector(0.f, 100000.f, 0.f)), 1.f, true, Dt);
	TestTrue(TEXT("follow capped"), R.Follow.Yaw <= FOneBrahamAimAssist::MaxFollowDegreesPerSecond * Dt + KINDA_SMALL_NUMBER);

	// The player strafing with the target cancels the follow: it is relative motion.
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector(0.f, 400.f, 0.f), One(FVector(1000.f, 0.f, 0.f), FVector(0.f, 400.f, 0.f)), 1.f, false, Dt);
	TestTrue(TEXT("moving together: no follow"), R.Follow.IsNearlyZero());

	// Two targets: the one nearer the crosshair for its cone wins, and an
	// excluded one (sight line blocked) hands over to the other.
	TArray<FOneBrahamAimAssistTarget> Two;
	Two.Add(One(FRotator(0.f, 3.f, 0.f).Vector() * 800.f)[0]);
	Two.Add(One(FRotator(0.f, 0.5f, 0.f).Vector() * 2000.f)[0]);
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, Two, 1.f, false, Dt);
	TestEqual(TEXT("nearer the crosshair wins"), R.TargetIndex, 1);
	const TArray<int32> Blocked = { 1 };
	R = FOneBrahamAimAssist::Evaluate(Eye, Ahead, FVector::ZeroVector, Two, 1.f, false, Dt, Blocked);
	TestEqual(TEXT("blocked target skipped"), R.TargetIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamKeyLabelTest, "OneBraham.Input.KeyLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamKeyLabelTest::RunTest(const FString& Parameters)
{
	// Pad buttons read the way they are printed on the controller.
	TestEqual(TEXT("bottom face button is A"), UOneBrahamInputBindings::GetKeyLabel(EKeys::Gamepad_FaceButton_Bottom).ToString(), FString(TEXT("A")));
	TestEqual(TEXT("right shoulder is RB"), UOneBrahamInputBindings::GetKeyLabel(EKeys::Gamepad_RightShoulder).ToString(), FString(TEXT("RB")));
	TestEqual(TEXT("left trigger is LT"), UOneBrahamInputBindings::GetKeyLabel(EKeys::Gamepad_LeftTrigger).ToString(), FString(TEXT("LT")));
	TestEqual(TEXT("right special is Menu"), UOneBrahamInputBindings::GetKeyLabel(EKeys::Gamepad_Special_Right).ToString(), FString(TEXT("Menu")));

	// Everything else keeps the engine's short name.
	TestTrue(TEXT("keyboard keys are the engine's name"),
		UOneBrahamInputBindings::GetKeyLabel(EKeys::F).EqualTo(EKeys::F.GetDisplayName(false)));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
