// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.StaticChecks.*
//
// The project's engine-free checks are Python programs in Tools/Python/onebraham
// that find the class of error a compiler, a linker or a missing DOREPLIFETIME
// would otherwise reveal: a member function declared and never defined, a
// TSoftObjectPtr pointed at a path no sane packager would cook, a gameplay tag
// that does not exist, a voice line id that was invented, an input key bound to
// an action nobody generates, a floor named wrongly in the building layout, and a
// build report whose own numbers have gone stale.
//
// They run in CI and they run from Scripts/*.bat. This suite runs them from
// inside the automation framework, which is what makes `Scripts/RunTests.bat`
// cover them too - one command that checks the static shape of the project and
// the parts of it that can only be answered at runtime.
//
// The checks need Python 3. If it is not on the machine this test fails with the
// command to run instead rather than passing quietly: a green test that checked
// nothing is the failure mode this whole file exists to avoid.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** The engine-free checks, by module name under Tools/Python/onebraham. */
	struct FOneBrahamStaticCheck
	{
		const TCHAR* Module;
		const TCHAR* What;
	};

	const FOneBrahamStaticCheck EngineFreeChecks[] =
	{
		{ TEXT("check_cpp_symbols"),   TEXT("every declared member function has a definition") },
		{ TEXT("check_uht_contracts"), TEXT("UE reflection contracts: replicated properties, RPC flags, RepNotify, soft paths") },
		{ TEXT("check_soft_paths"),    TEXT("every soft object path names a conventional, cookable asset") },
		{ TEXT("check_calls"),         TEXT("every call site matches a declaration's arity and parameter types") },
		{ TEXT("check_gameplay_tags"), TEXT("every gameplay tag used in C++ is declared in DefaultGame.ini") },
		{ TEXT("check_building_layout"), TEXT("the building layout agrees with the map builder: floors, areas, anchors") },
		{ TEXT("check_voice_lines"),   TEXT("every voice line id exists, is documented, and has a full variant set") },
		{ TEXT("check_input_actions"), TEXT("the five lists of input actions are one list") },
		{ TEXT("check_doc_numbers"),   TEXT("the numbers the documentation quotes are the numbers the build produces") },
		{ TEXT("check_test_names"),    TEXT("every automation test the documentation lists exists, and every test that exists is listed") },
		{ TEXT("check_doc_files"),     TEXT("every script and tool the documentation tells the reader to run exists on disk") },
		{ TEXT("check_doc_types"),     TEXT("every type name the documentation uses is a type this project declares") }

		// Deliberately absent: validate_project.py, and check_soft_paths.py in its
		// full (non-config) form. Both shell out to UnrealBuildTool or UnrealPak,
		// and a check that cannot tell "the tool never ran" from "the project is
		// clean" does not belong in a suite whose failure has to mean something.
		// They run from Scripts/RunAll.bat, where a missing toolchain is loud.
	};

	/** The python launchers to try, in order. */
	const TCHAR* PythonLaunchers[] = { TEXT("python3"), TEXT("python"), TEXT("py") };

	/** Runs one check in a child process. Returns the exit code, or -1 if the
	 *  launcher could not be started at all. */
	int32 RunPythonModule(const FString& PythonRoot, const FString& Module, FString& OutOutput, FString& OutLauncher)
	{
		// sys.path is set explicitly rather than through PYTHONPATH: the child
		// inherits no environment from the editor that would help it, and the
		// checkers import each other from the onebraham package.
		const FString Script = FString::Printf(
			TEXT("import sys; sys.path.insert(0, r'%s'); from onebraham import %s; raise SystemExit(%s.main())"),
			*PythonRoot, *Module, *Module);

		for (const TCHAR* Launcher : PythonLaunchers)
		{
			FString StdOut;
			FString StdErr;
			int32 ReturnCode = -1;

			const FString Params = FString::Printf(TEXT("-c \"%s\""), *Script);

			if (!FPlatformProcess::ExecProcess(Launcher, *Params, &ReturnCode, &StdOut, &StdErr))
			{
				continue;
			}

			// On Windows the Microsoft Store ships a stub python3.exe that does
			// nothing, and Scripts/Setup.bat installs Python into the project
			// rather than onto the PATH. A launcher that produced no output at all
			// did not run the check, so try the next one.
			if (ReturnCode != 0 && StdOut.TrimStartAndEnd().IsEmpty() && StdErr.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}

			OutLauncher = Launcher;
			OutOutput = StdOut;
			return ReturnCode;
		}

		OutLauncher.Reset();
		return -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamEngineFreeChecksTest, "OneBraham.StaticChecks.EngineFreeChecks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamEngineFreeChecksTest::RunTest(const FString& Parameters)
{
	const FString PythonRoot = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Tools/Python"));

	if (!FPaths::DirectoryExists(PythonRoot))
	{
		AddError(FString::Printf(
			TEXT("Tools/Python is missing at %s. The engine-free checks live there; clone the repository, "
				 "do not copy the Source folder out of it."), *PythonRoot));
		return false;
	}

	int32 Checked = 0;
	FString Launcher;

	for (const FOneBrahamStaticCheck& Check : EngineFreeChecks)
	{
		FString Output;
		const int32 ReturnCode = RunPythonModule(PythonRoot, Check.Module, Output, Launcher);

		if (ReturnCode < 0)
		{
			// No interpreter anywhere: say so once, with the fix, and stop.
			AddError(FString::Printf(
				TEXT("Could not run %s: no Python 3 interpreter found on PATH (tried python3, python, py). ")
				TEXT("Install Python 3, or run Scripts\\RunAll.bat, which looks for one and says what to install."),
				Check.Module));
			return false;
		}

		++Checked;

		if (ReturnCode != 0)
		{
			AddError(FString::Printf(TEXT("%s failed (%s) - exit %d\n%s"),
				Check.Module, Check.What, ReturnCode, *Output.Left(4000)));
		}
		else
		{
			AddInfo(FString::Printf(TEXT("%s: OK - %s"), Check.Module, Check.What));
		}
	}

	TestEqual(TEXT("every engine-free check ran"), Checked, UE_ARRAY_COUNT(EngineFreeChecks));
	TestFalse(TEXT("and Python was found"), Launcher.IsEmpty());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
