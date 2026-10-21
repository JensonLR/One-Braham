// Copyright (c) One Braham. All Rights Reserved.

#include "World/OneBrahamDisplayDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameState.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "Extraction/OneBrahamMatchEnding.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "UI/HUD/OneBrahamBuildingStatus.h"
#include "UI/OneBrahamUIStyle.h"

namespace
{
	/** How often a standing line looks for screens the viewer has come into earshot of. */
	constexpr double OBDisplayScanSeconds = 0.5;

	/** A stage that replicates this soon after its announcement retints the line. */
	constexpr double OBDisplayRecolourWindow = 3.0;

	/** The phase seen Complete this long with no ending replicated: the end goes up anyway. */
	constexpr double OBDisplayEndingGrace = 1.5;

	/** A tannoy paragraph this recent is the match end's subline. */
	constexpr double OBDisplayLastWordWindow = 10.0;

	/** The tannoy reads at about two and a half words a second. */
	constexpr float OBDisplayWordsPerSecond = 2.5f;

	/** The building clears its throat before it says anything. */
	const TCHAR* const OBDisplayPreambles[] =
	{
		TEXT("Attention all staff."), TEXT("Attention, all staff."), TEXT("Attention please."), TEXT("Attention."),
	};

	/** Splits at the first sentence end (". ", "! ", "? ") that is not too early to be one. */
	int32 FirstSentenceEnd(const FString& Text)
	{
		for (int32 Index = 0; Index < Text.Len() - 1; ++Index)
		{
			const TCHAR Ch = Text[Index];
			if ((Ch == TEXT('.') || Ch == TEXT('!') || Ch == TEXT('?')) && FChar::IsWhitespace(Text[Index + 1]) && Index >= 8)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FString WithoutPreamble(const FString& Paragraph)
	{
		FString Text = Paragraph.TrimStartAndEnd();
		for (const TCHAR* Preamble : OBDisplayPreambles)
		{
			if (Text.StartsWith(Preamble))
			{
				Text = Text.Mid(FCString::Strlen(Preamble)).TrimStart();
			}
		}
		return Text;
	}
}

// ==========================================================================
//  Lifecycle
// ==========================================================================
bool UOneBrahamDisplayDirector::ShouldCreateSubsystem(UObject* Outer) const
{
	// The screens are drawn, never served: a dedicated server has no copy of
	// them to drive.
	if (IsRunningDedicatedServer())
	{
		return false;
	}
	return Super::ShouldCreateSubsystem(Outer);
}

bool UOneBrahamDisplayDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOneBrahamDisplayDirector::Deinitialize()
{
	Unbind();
	Lit.Reset();
	Super::Deinitialize();
}

TStatId UOneBrahamDisplayDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOneBrahamDisplayDirector, STATGROUP_Tickables);
}

UOneBrahamDisplayDirector* UOneBrahamDisplayDirector::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
	return World != nullptr ? World->GetSubsystem<UOneBrahamDisplayDirector>() : nullptr;
}

void UOneBrahamDisplayDirector::TryBind()
{
	if (BoundGameState.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	AOneBrahamGameState* GS = World != nullptr ? World->GetGameState<AOneBrahamGameState>() : nullptr;
	if (GS == nullptr)
	{
		return;
	}
	GS->OnAnnouncement.AddUniqueDynamic(this, &UOneBrahamDisplayDirector::HandleAnnouncement);
	BoundGameState = GS;
}

void UOneBrahamDisplayDirector::Unbind()
{
	if (AOneBrahamGameState* GS = BoundGameState.Get())
	{
		GS->OnAnnouncement.RemoveDynamic(this, &UOneBrahamDisplayDirector::HandleAnnouncement);
	}
	BoundGameState.Reset();
}

// ==========================================================================
//  The rules
// ==========================================================================
FString UOneBrahamDisplayDirector::HeadlineFor(const FString& Paragraph)
{
	const FString Text = WithoutPreamble(Paragraph);
	const int32 Stop = FirstSentenceEnd(Text);
	return Stop == INDEX_NONE ? Text : Text.Left(Stop + 1);
}

FString UOneBrahamDisplayDirector::SublineFor(const FString& Paragraph)
{
	const FString Text = WithoutPreamble(Paragraph);
	const int32 Stop = FirstSentenceEnd(Text);
	if (Stop == INDEX_NONE)
	{
		return FString();
	}
	const FString Rest = Text.Mid(Stop + 1).TrimStart();
	const int32 Next = FirstSentenceEnd(Rest);
	return Next == INDEX_NONE ? Rest : Rest.Left(Next + 1);
}

float UOneBrahamDisplayDirector::HoldSecondsFor(const FString& Paragraph)
{
	TArray<FString> Words;
	Paragraph.ParseIntoArrayWS(Words);
	// The tannoy's reading time plus a beat to look up at the screen; never
	// less than the HUD's subtitle (6 s), never a fixture.
	return FMath::Clamp(1.5f + static_cast<float>(Words.Num()) / OBDisplayWordsPerSecond, 6.f, 14.f);
}

FText UOneBrahamDisplayDirector::EyebrowFor(const FText& Clock)
{
	return Clock.IsEmpty()
		? NSLOCTEXT("OneBraham", "Display_Eyebrow_NoClock", "ONE BRAHAM  ·  ALL FLOORS")
		: FText::Format(NSLOCTEXT("OneBraham", "Display_Eyebrow", "ONE BRAHAM  ·  ALL FLOORS  ·  {0}"), Clock);
}

void UOneBrahamDisplayDirector::StageColours(EOneBrahamLockdownStage Stage, FLinearColor& OutA, FLinearColor& OutB, bool bEvacuating)
{
	// One colour rule for every surface: the PA card the HUD raises for this
	// same line takes ToneField(StageTone(...)), and so do the screens.
	UOneBrahamUIStyle::ToneField(UOneBrahamUIStyle::StageTone(Stage, bEvacuating), OutA, OutB);
}

bool UOneBrahamDisplayDirector::IsInEarshot(const FVector& Viewer, const FBox& DisplayBounds, float InFloorBandCm, float InNearCm)
{
	if (!DisplayBounds.IsValid)
	{
		return false;
	}
	// The screen's vertical span against the eye: the escalator wall rises
	// through two storeys and is on both of them.
	// The eye is ~170 cm over its own floor, so the band is lopsided: a
	// screen on this storey can start up to a storey's height above the eye,
	// but its top is never far under it - one whose top is more than half the
	// band below the eye is on the floor beneath, behind the slab.
	const double ScreenAboveEye = DisplayBounds.Min.Z - Viewer.Z;
	const double ScreenBelowEye = Viewer.Z - DisplayBounds.Max.Z;
	if (ScreenAboveEye <= InFloorBandCm && ScreenBelowEye <= InFloorBandCm * 0.5)
	{
		return true;
	}
	return DisplayBounds.ComputeSquaredDistanceToPoint(Viewer) <= FMath::Square(static_cast<double>(InNearCm));
}

// ==========================================================================
//  Speaking
// ==========================================================================
EOneBrahamLockdownStage UOneBrahamDisplayDirector::CurrentStage() const
{
	const AOneBrahamGameState* GS = BoundGameState.Get();
	const UOneBrahamLockdownComponent* Lockdown = GS != nullptr ? GS->GetLockdownComponent() : nullptr;
	return Lockdown != nullptr ? Lockdown->GetStage() : EOneBrahamLockdownStage::Normal;
}

bool UOneBrahamDisplayDirector::IsEvacuating() const
{
	const AOneBrahamGameState* GS = BoundGameState.Get();
	const UOneBrahamExtractionComponent* Extraction = GS != nullptr ? GS->GetExtractionComponent() : nullptr;
	if (Extraction == nullptr)
	{
		return false;
	}
	const EOneBrahamExtractionState State = Extraction->GetExtractionState();
	return State == EOneBrahamExtractionState::Broadcast || State == EOneBrahamExtractionState::Arriving
		|| State == EOneBrahamExtractionState::Holding;
}

bool UOneBrahamDisplayDirector::GetViewer(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr || !PC->IsLocalController())
	{
		return false;
	}
	FRotator Rotation;
	PC->GetPlayerViewPoint(OutLocation, Rotation);
	return true;
}

void UOneBrahamDisplayDirector::HandleAnnouncement(const FText& Announcement)
{
	if (Announcement.IsEmpty())
	{
		return;
	}
	const UWorld* World = GetWorld();
	LastAnnouncement = Announcement;
	LastAnnouncementTime = World != nullptr ? World->GetTimeSeconds() : 0.0;

	const FString Paragraph = Announcement.ToString();
	const FText Headline = FText::FromString(HeadlineFor(Paragraph));

	// At the match's end the end's headline holds the screens; the tannoy's
	// last word becomes the line under it.
	if (bMatchEndShown && IsSpeaking())
	{
		Current.Subline = Headline;
		for (const TWeakObjectPtr<AOneBrahamDisplay>& Display : Lit)
		{
			if (AOneBrahamDisplay* Screen = Display.Get())
			{
				Screen->PushOverride(Current, static_cast<float>(SpeakEnd - LastAnnouncementTime));
			}
		}
		return;
	}

	Speak(Headline, FText::FromString(SublineFor(Paragraph)), CurrentStage(), HoldSecondsFor(Paragraph));
}

void UOneBrahamDisplayDirector::Speak(const FText& Headline, const FText& Subline, EOneBrahamLockdownStage Stage, float HoldSeconds)
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	Current = FOneBrahamDisplaySlide();
	Current.Eyebrow = EyebrowFor(OneBrahamDuskClock::Now(World));
	Current.Headline = Headline;
	Current.Subline = Subline;
	bSpokenEvacuating = IsEvacuating();
	StageColours(Stage, Current.ColourA, Current.ColourB, bSpokenEvacuating);

	SpokenStage = Stage;
	SpeakStart = World->GetTimeSeconds();
	SpeakEnd = SpeakStart + FMath::Max(0.5f, HoldSeconds);
	Lit.Reset();
	LightScreens(/*bEveryScreen*/ false);
	NextScan = SpeakStart + OBDisplayScanSeconds;

	UE_LOG(LogOneBraham, Log, TEXT("[OB.DISPLAYS] %d screens: \"%s\" (%s, %.1fs)"), Lit.Num(), *Headline.ToString(),
		*UEnum::GetValueAsString(Stage), HoldSeconds);
}

void UOneBrahamDisplayDirector::SpeakMatchEnd(const FText& Headline, EOneBrahamLockdownStage Stage)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	const bool bRecentWord = LastAnnouncementTime >= 0.0 && Now - LastAnnouncementTime <= OBDisplayLastWordWindow;
	const FText Subline = bRecentWord
		? FText::FromString(HeadlineFor(LastAnnouncement.ToString()))
		: NSLOCTEXT("OneBraham", "Display_MatchEnd_Subline", "Thank you for your patience.");

	bMatchEndShown = true;
	Speak(Headline, Subline, Stage, MatchEndHoldSeconds);
}

bool UOneBrahamDisplayDirector::IsSpeaking() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && SpeakEnd >= 0.0 && World->GetTimeSeconds() < SpeakEnd;
}

int32 UOneBrahamDisplayDirector::GetLitCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AOneBrahamDisplay>& Display : Lit)
	{
		Count += Display.IsValid() ? 1 : 0;
	}
	return Count;
}

void UOneBrahamDisplayDirector::LightScreens(bool bEveryScreen)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const float Remaining = static_cast<float>(SpeakEnd - Now);
	if (Remaining <= 0.f)
	{
		return;
	}

	FVector Viewer = FVector::ZeroVector;
	const bool bHasViewer = !bEveryScreen && GetViewer(Viewer);

	for (TActorIterator<AOneBrahamDisplay> It(World); It; ++It)
	{
		AOneBrahamDisplay* Screen = *It;
		if (Lit.Contains(Screen))
		{
			continue;
		}
		if (bHasViewer)
		{
			FVector Origin;
			FVector Extent;
			Screen->GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent);
			if (!IsInEarshot(Viewer, FBox(Origin - Extent, Origin + Extent), FloorBandCm, NearCm))
			{
				continue;
			}
		}
		Screen->PushOverride(Current, Remaining);
		Lit.Add(Screen);
	}
}

// ==========================================================================
//  Tick
// ==========================================================================
void UOneBrahamDisplayDirector::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	TryBind();

	const double Now = World->GetTimeSeconds();

	if (IsSpeaking())
	{
		// The stage can replicate after the announcement that names it: the
		// line takes its colour when it lands.
		// So can the extraction state behind the helicopter's call.
		const EOneBrahamLockdownStage Stage = CurrentStage();
		const bool bEvacuating = IsEvacuating();
		if ((Stage != SpokenStage || bEvacuating != bSpokenEvacuating) && Now - SpeakStart <= OBDisplayRecolourWindow && !bMatchEndShown)
		{
			SpokenStage = Stage;
			bSpokenEvacuating = bEvacuating;
			StageColours(Stage, Current.ColourA, Current.ColourB, bEvacuating);
			for (const TWeakObjectPtr<AOneBrahamDisplay>& Display : Lit)
			{
				if (AOneBrahamDisplay* Screen = Display.Get())
				{
					Screen->RecolourOverride(Current.ColourA, Current.ColourB);
				}
			}
		}

		// Screens the viewer has come into earshot of join the line.
		if (Now >= NextScan)
		{
			NextScan = Now + OBDisplayScanSeconds;
			LightScreens(/*bEveryScreen*/ false);
		}
	}
	else if (!Lit.IsEmpty())
	{
		Lit.Reset();
	}

	// The match's end: the end screen's headline, once the ending is known.
	if (const AOneBrahamGameState* GS = BoundGameState.Get(); GS != nullptr && !bMatchEndShown)
	{
		if (GS->MatchPhase == EOneBrahamMatchPhase::Complete || GS->MatchPhase == EOneBrahamMatchPhase::PostMatch)
		{
			if (MatchCompleteSeenAt < 0.0)
			{
				MatchCompleteSeenAt = Now;
			}
			const UOneBrahamExtractionComponent* Extraction = GS->GetExtractionComponent();
			const EOneBrahamMatchEnding Ending = Extraction != nullptr ? Extraction->GetMatchEnding() : EOneBrahamMatchEnding::None;
			if (Ending != EOneBrahamMatchEnding::None || Now - MatchCompleteSeenAt >= OBDisplayEndingGrace)
			{
				SpeakMatchEnd(OneBrahamMatchEnd::HeadlineFor(Ending), CurrentStage());
			}
		}
	}

#if !UE_BUILD_SHIPPING
	TickTour();
#endif
}

// ==========================================================================
//  Dev
// ==========================================================================
#if !UE_BUILD_SHIPPING
namespace
{
	AOneBrahamDisplay* FindScreen(UWorld* World, const TCHAR* Name)
	{
		for (TActorIterator<AOneBrahamDisplay> It(World); It; ++It)
		{
			if (It->GetActorNameOrLabel().Contains(Name))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** A camera Distance in front of the screen, Drop below its centre, looking at it. */
	FTransform FacingScreen(const AOneBrahamDisplay* Screen, float Distance, float Drop, float Sideways = 0.f)
	{
		const FVector Centre = Screen->GetActorLocation();
		const FVector Eye = Centre + Screen->GetActorForwardVector() * Distance + Screen->GetActorRightVector() * Sideways
			- FVector(0.f, 0.f, Drop);
		return FTransform((Centre - Eye).Rotation(), Eye);
	}

	void Shot(const FString& Name, bool bShowUI)
	{
		const FString Path = FPaths::ScreenShotDir() / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, bShowUI, /*bAddUniqueSuffix*/ false);
		UE_LOG(LogOneBraham, Display, TEXT("[OB.DISPLAYS] shot %s"), *Path);
	}

	FAutoConsoleCommandWithWorldAndArgs GOBDisplaysTour(
		TEXT("ob.Displays.Tour"),
		TEXT("Dev: photographs the OLED screens close and far, then each stage's announcement on them "
			"(Saved/Screenshots/<platform>/obd_*.png). 'quit' exits when done."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOneBrahamDisplayDirector* Director = UOneBrahamDisplayDirector::Get(World))
			{
				Director->StartTour(Args.Contains(TEXT("quit")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOBDisplaysSay(
		TEXT("ob.Displays.Say"),
		TEXT("Dev: puts a line on this machine's screens as the building would. ob.Displays.Say <stage 0-3> <words...>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UOneBrahamDisplayDirector* Director = UOneBrahamDisplayDirector::Get(World);
			if (Director == nullptr || Args.Num() < 2)
			{
				return;
			}
			const int32 Stage = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 3);
			const FString Paragraph = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
			Director->Speak(FText::FromString(UOneBrahamDisplayDirector::HeadlineFor(Paragraph)),
				FText::FromString(UOneBrahamDisplayDirector::SublineFor(Paragraph)),
				static_cast<EOneBrahamLockdownStage>(Stage), UOneBrahamDisplayDirector::HoldSecondsFor(Paragraph));
		}));
}

void UOneBrahamDisplayDirector::StartTour(bool bQuitAfter)
{
	const UWorld* World = GetWorld();
	TourStep = 0;
	TourStepAt = World != nullptr ? World->GetTimeSeconds() : 0.0;
	bTourQuit = bQuitAfter;
	UE_LOG(LogOneBraham, Display, TEXT("[OB.DISPLAYS] tour started"));
}

void UOneBrahamDisplayDirector::TickTour()
{
	if (TourStep == INDEX_NONE)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	AOneBrahamGameState* GS = BoundGameState.Get();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	const double Since = Now - TourStepAt;

	auto Next = [this, Now]() { ++TourStep; TourStepAt = Now; };

	auto View = [this, World, PC](AOneBrahamDisplay* Screen, float Distance, float Drop, float Sideways = 0.f)
	{
		if (Screen == nullptr || PC == nullptr)
		{
			return;
		}
		const FTransform Where = FacingScreen(Screen, Distance, Drop, Sideways);
		ACameraActor* Camera = TourCamera.Get();
		if (Camera == nullptr)
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Where, Params);
			if (Camera != nullptr)
			{
				Camera->GetCameraComponent()->SetFieldOfView(80.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			}
			TourCamera = Camera;
		}
		if (Camera != nullptr)
		{
			Camera->SetActorTransform(Where);
			PC->SetViewTarget(Camera);
		}
	};

	auto Force = [GS](EOneBrahamLockdownStage Stage)
	{
		if (UOneBrahamLockdownComponent* Lockdown = GS != nullptr ? GS->GetLockdownComponent() : nullptr)
		{
			Lockdown->ServerForceStage(Stage);
		}
	};

	// A respawn or a possession hands the view back to the pawn: keep the tour's.
	if (ACameraActor* Camera = TourCamera.Get(); Camera != nullptr && PC != nullptr && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTarget(Camera);
	}

	AOneBrahamDisplay* Reception = World != nullptr ? FindScreen(World, TEXT("Display_Reception")) : nullptr;
	AOneBrahamDisplay* Wall = World != nullptr ? FindScreen(World, TEXT("Display_EscalatorWall")) : nullptr;
	AOneBrahamDisplay* Menu = World != nullptr ? FindScreen(World, TEXT("Display_Menu2")) : nullptr;

	// The views. A screenshot is taken from the next frame rendered, so a
	// step photographs and the following one moves the camera, never both in
	// one tick (the shot would show where the camera went).
	auto ViewReceptionFar = [&]() { View(Reception, 750.f, 330.f, 150.f); };
	auto ViewReceptionClose = [&]() { View(Reception, 450.f, 300.f); };
	auto ViewWall = [&]() { View(Wall, 650.f, 120.f, -100.f); };
	auto ViewMenuClose = [&]() { View(Menu, 280.f, 60.f); };
	auto ViewMenuFar = [&]() { View(Menu, 1200.f, 90.f, 300.f); };

	// Each pair: place the camera and give exposure and TAA a moment; photograph.
	constexpr double Settle = 2.5;
	constexpr double Moved = 0.3;
	switch (TourStep)
	{
	case 0:
		// Wait for the player and the game state, and for the level to settle.
		if (PC != nullptr && PC->GetPawn() != nullptr && GS != nullptr && Since > 5.0)
		{
			Next();
		}
		break;

	// The adverts, in the brand type: close up and from across the room.
	case 1: ViewReceptionFar(); Next(); break;
	case 2: if (Since > Settle) { Shot(TEXT("obd_ads_reception_far"), false); Next(); } break;
	case 3: if (Since > Moved) { ViewReceptionClose(); Next(); } break;
	case 4: if (Since > Settle) { Shot(TEXT("obd_ads_reception_close"), false); Next(); } break;
	case 5: if (Since > Moved) { ViewWall(); Next(); } break;
	case 6: if (Since > Settle) { Shot(TEXT("obd_ads_escalator_wall"), false); Next(); } break;
	case 7: if (Since > Moved) { ViewMenuClose(); Next(); } break;
	case 8: if (Since > Settle) { Shot(TEXT("obd_ads_canteen_close"), false); Next(); } break;
	case 9: if (Since > Moved) { ViewMenuFar(); Next(); } break;
	case 10: if (Since > Settle) { Shot(TEXT("obd_ads_canteen_far"), false); Next(); } break;
	case 11: if (Since > Moved) { ViewReceptionFar(); Next(); } break;

	// Calm: the game mode's own kind of line, down the real announcement path.
	case 12:
		if (Since > Settle && !IsSpeaking())
		{
			if (GS != nullptr && GS->HasAuthority())
			{
				GS->MulticastAnnounce(FText::FromString(TEXT("Attention all staff. The lift lobby is open. Ascend.")), false);
			}
			Next();
		}
		break;
	case 13: if (Since > 1.2) { Shot(TEXT("obd_announce_calm"), false); Next(); } break;
	case 14: if (Since > Moved) { ViewReceptionClose(); Next(); } break;
	case 15: if (Since > 1.2) { Shot(TEXT("obd_announce_calm_close"), true); Next(); } break;
	case 16: if (Since > Moved) { ViewReceptionFar(); Next(); } break;

	// Lockdown: the lockdown component's own announcement as the stage turns.
	case 17: if (!IsSpeaking() && Since > 1.5) { Force(EOneBrahamLockdownStage::Lockdown); Next(); } break;
	case 18: if (Since > 1.5) { Shot(TEXT("obd_announce_lockdown"), false); Next(); } break;
	case 19: if (Since > Moved) { ViewWall(); Next(); } break;
	case 20: if (Since > 1.2) { Shot(TEXT("obd_announce_lockdown_escalator"), false); Next(); } break;
	case 21: if (Since > Moved) { ViewReceptionClose(); Next(); } break;
	case 22: if (Since > 1.2) { Shot(TEXT("obd_announce_lockdown_close"), true); Next(); } break;
	case 23: if (Since > Moved) { ViewReceptionFar(); Next(); } break;

	// Purge.
	case 24: if (!IsSpeaking() && Since > 1.5) { Force(EOneBrahamLockdownStage::Purge); Next(); } break;
	case 25: if (Since > 1.5) { Shot(TEXT("obd_announce_purge"), false); Next(); } break;
	case 26: if (Since > Moved) { ViewReceptionClose(); Next(); } break;
	case 27: if (Since > 1.2) { Shot(TEXT("obd_announce_purge_close"), true); Next(); } break;
	case 28: if (Since > Moved) { ViewMenuFar(); Next(); } break;
	case 29: if (Since > 1.2) { Shot(TEXT("obd_announce_purge_canteen"), false); Next(); } break;
	case 30: if (Since > Moved) { ViewReceptionFar(); Next(); } break;

	// The match's end: the end screen's headline, through the call the phase drives.
	case 31:
		if (!IsSpeaking() && Since > 1.5)
		{
			SpeakMatchEnd(OneBrahamMatchEnd::HeadlineFor(EOneBrahamMatchEnding::Extracted), CurrentStage());
			Next();
		}
		break;
	case 32: if (Since > 1.5) { Shot(TEXT("obd_matchend"), false); Next(); } break;
	default:
		if (Since > 3.0)
		{
			UE_LOG(LogOneBraham, Display, TEXT("[OB.DISPLAYS] tour done"));
			TourStep = INDEX_NONE;
			if (bTourQuit && GEngine != nullptr)
			{
				GEngine->Exec(World, TEXT("quit"));
			}
		}
		break;
	}
}
#endif
