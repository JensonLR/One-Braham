// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSettingsTests.cpp
// ---------------------------------------------------------------------------
// The first-run graphics preset: which tier a machine lands on, and that the
// pick happens once per install. Every Mac used to start at Epic (~73 ms a
// frame on an M5 at 720p) because nothing ever ran the rule, and the rule
// itself read an Apple chip's unified memory as a 12 GB graphics card.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include "Core/OneBrahamGameUserSettings.h"

#if WITH_AUTOMATION_TESTS

namespace OneBrahamSettingsTests
{
	FOneBrahamHardwareProbe Probe(const TCHAR* Brand, int32 VramMb, float Benchmark = -1.f)
	{
		FOneBrahamHardwareProbe Out;
		Out.GpuBrand = Brand;
		Out.VramMb = VramMb;
		Out.GpuBenchmarkIndex = Benchmark;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSettingsHardwarePresetTest, "OneBraham.Settings.HardwarePreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSettingsHardwarePresetTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamSettingsTests;
	using EPreset = EOneBrahamQualityPreset;

	// Apple Silicon: Metal reports ~75% of system RAM as "VRAM", which used to
	// rate a fanless base M5 as an 8 GB-class card and hand it Epic.
	TestEqual(TEXT("base M5 (16 GB unified) is Medium, not Epic"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Apple M5"), 12288)), EPreset::Medium);
	TestEqual(TEXT("M4 Pro is Medium"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Apple M4 Pro"), 36864)), EPreset::Medium);
	TestEqual(TEXT("M3 Max is High"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Apple M3 Max"), 98304)), EPreset::High);
	TestEqual(TEXT("M2 Ultra is High"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Apple M2 Ultra"), 147456)), EPreset::High);

	// Metal's benchmark result is ignored rather than trusted: the M5 measured
	// ~346,000, which would otherwise mean "faster than any GPU".
	TestFalse(TEXT("Metal's benchmark index is implausible"), UOneBrahamGameUserSettings::IsPlausibleGpuBenchmark(346319.6f));
	TestFalse(TEXT("an unrun benchmark is not used"), UOneBrahamGameUserSettings::IsPlausibleGpuBenchmark(-1.f));
	TestTrue(TEXT("a desktop card's index is"), UOneBrahamGameUserSettings::IsPlausibleGpuBenchmark(420.f));
	TestEqual(TEXT("an implausible benchmark does not lift a base M5"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Apple M5"), 12288, 346319.6f)), EPreset::Medium);

	// Windows family / VRAM rule, unchanged.
	TestEqual(TEXT("Intel iGPU is Low"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Intel(R) UHD Graphics 770"), 2048)), EPreset::Low);
	TestEqual(TEXT("AMD APU graphics is Low"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("AMD Radeon(TM) Graphics"), 512)), EPreset::Low);
	TestEqual(TEXT("an Intel Arc card is not an iGPU"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Intel(R) Arc(TM) A770 Graphics"), 16384)), EPreset::Epic);
	TestEqual(TEXT("3 GB card is Medium"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce GTX 1060 3GB"), 3072)), EPreset::Medium);
	TestEqual(TEXT("6 GB non-RT card is High"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce GTX 1660 SUPER"), 6144)), EPreset::High);
	TestEqual(TEXT("8 GB RTX card is Epic"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce RTX 4060"), 8188)), EPreset::Epic);
	TestEqual(TEXT("12 GB RTX card is Ultra"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce RTX 4070"), 12282)), EPreset::Ultra);

	// A plausible benchmark only ever lowers the pick.
	TestEqual(TEXT("a slow benchmark caps an RTX card at Medium"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce RTX 3050 Laptop GPU"), 4096, 120.f)), EPreset::Medium);
	TestEqual(TEXT("a mid benchmark caps a 12 GB RTX card at High"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce RTX 3060"), 12288, 300.f)), EPreset::High);
	TestEqual(TEXT("a fast benchmark leaves Ultra alone"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("NVIDIA GeForce RTX 4080"), 16376, 1800.f)), EPreset::Ultra);
	TestEqual(TEXT("a fast benchmark never raises an iGPU"),
		UOneBrahamGameUserSettings::ChooseHardwarePreset(Probe(TEXT("Intel(R) Iris(R) Xe Graphics"), 2048, 1800.f)), EPreset::Low);

	// Resolution: dynamic where it pays, a fixed TSR scale where it does not.
	TestFalse(TEXT("no dynamic resolution on Apple Silicon (slower on Metal)"),
		UOneBrahamGameUserSettings::PrefersDynamicResolution(Probe(TEXT("Apple M5"), 12288)));
	TestTrue(TEXT("dynamic resolution elsewhere"),
		UOneBrahamGameUserSettings::PrefersDynamicResolution(Probe(TEXT("NVIDIA GeForce RTX 4060"), 8188)));
	const FOneBrahamHardwareProbe Desktop = Probe(TEXT("NVIDIA GeForce GTX 1660 SUPER"), 6144);
	TestNearlyEqual(TEXT("Medium renders at 71% without it"),
		UOneBrahamGameUserSettings::FixedResolutionScaleFor(EPreset::Medium, Desktop), 71.f, 0.01f);
	TestNearlyEqual(TEXT("Epic renders at 100%"),
		UOneBrahamGameUserSettings::FixedResolutionScaleFor(EPreset::Epic, Desktop), 100.f, 0.01f);
	TestNearlyEqual(TEXT("a base M5 renders Medium at the 50% floor (~15 ms GPU, 71% was ~24)"),
		UOneBrahamGameUserSettings::FixedResolutionScaleFor(EPreset::Medium, Probe(TEXT("Apple M5"), 12288)), 50.f, 0.01f);
	TestNearlyEqual(TEXT("an M4 Pro keeps Medium's 71%"),
		UOneBrahamGameUserSettings::FixedResolutionScaleFor(EPreset::Medium, Probe(TEXT("Apple M4 Pro"), 36864)), 71.f, 0.01f);
	TestNearlyEqual(TEXT("an M3 Max keeps High's 87%"),
		UOneBrahamGameUserSettings::FixedResolutionScaleFor(EPreset::High, Probe(TEXT("Apple M3 Max"), 98304)), 87.f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSettingsFirstRunOnceTest, "OneBraham.Settings.FirstRunOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSettingsFirstRunOnceTest::RunTest(const FString& Parameters)
{
	// "Remembers" means the flag is written with the rest of the user's
	// settings: a config property of the GameUserSettings class.
	const FBoolProperty* Flag = FindFProperty<FBoolProperty>(UOneBrahamGameUserSettings::StaticClass(), TEXT("bHardwarePresetApplied"));
	if (!TestNotNull(TEXT("bHardwarePresetApplied exists"), Flag))
	{
		return false;
	}
	TestTrue(TEXT("bHardwarePresetApplied is saved to GameUserSettings.ini"), Flag->HasAnyPropertyFlags(CPF_Config));

	// Transient objects: the gate itself never saves, so nothing reaches the
	// real GameUserSettings.ini. A new object copies the class defaults, which
	// carry this machine's saved flag, so each starts from a fresh install's
	// state explicitly.
	auto NewFreshInstall = [Flag]()
	{
		UOneBrahamGameUserSettings* Settings = NewObject<UOneBrahamGameUserSettings>(GetTransientPackage());
		Flag->SetPropertyValue_InContainer(Settings, false);
		return Settings;
	};

	UOneBrahamGameUserSettings* Fresh = NewFreshInstall();
	TestFalse(TEXT("a fresh install has not run the preset"), Fresh->HasAppliedHardwarePreset());
	TestTrue(TEXT("a fresh install runs it"), Fresh->ConsumeFirstRunPreset(/*bHasSavedUserSettings*/ false));
	TestTrue(TEXT("and remembers that it did"), Fresh->HasAppliedHardwarePreset());
	TestFalse(TEXT("never a second time"), Fresh->ConsumeFirstRunPreset(false));

	// An install that already has settings keeps them, and is marked so it is
	// not asked again.
	UOneBrahamGameUserSettings* Existing = NewFreshInstall();
	TestFalse(TEXT("an install with saved settings is not re-tiered"), Existing->ConsumeFirstRunPreset(/*bHasSavedUserSettings*/ true));
	TestTrue(TEXT("but is marked as done"), Existing->HasAppliedHardwarePreset());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
