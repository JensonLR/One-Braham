// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMainMenuWidget.h
// ---------------------------------------------------------------------------
// The front end a Steam player meets: PLAY, HOST, JOIN, SETTINGS,
// CREDITS & LICENCES, QUIT. Built in C++ on UOneBrahamMenuWidget, like the
// pause menu and the settings, for the same reason (Docs/08 section 1): a
// clean checkout can start, host and join a match before any widget asset
// exists, and it looks like the rest of the building's signage.
//
// THE LAYOUT ("SIGN IN AT RECEPTION", the approved front-end spec)
//   The building is the interface. The camera (AOneBrahamFrontEndCamera) is a
//   director with a shot list; this widget is the screen half of it:
//
//     COLD OPEN  first launch per session: black, the place and the time,
//                the fins, the reception wall saying ON BREAK, the crown;
//                three serif supers; any key skips to HOME
//     HOME       the reception floor directory: a painted board over a
//                scrim (no panel), verbs joined to where-lines by dotted
//                leaders, the title in Archivo Expanded Black
//     PLAY       "Lift lobby": mode cards with a mini section each, the
//                section drawing of the focused mode, three lift buttons
//     HOST       "Book a meeting room": a ledger with an LED edge
//     JOIN       "Today's meetings": a departure board
//     SETTINGS   FACILITIES: the settings widget over a frosted pane
//     CREDITS    VISITORS' BOOK: a roll over the Skydeck at dusk
//     SIGN OUT   a visitor badge: STAY or SIGN OUT
//
//   Letterbox bars (2.39:1) frame every page and carry the top bar (place,
//   live clock, LAN meetings) and the bottom bar (key hints, build, the
//   OpenStreetMap credit). Page changes follow one grammar: content out
//   140 ms, the camera dips to the page's shot, content in 260 ms staggered
//   25 ms a row, the breadcrumb types on.
//
//   Every control is a UOneBrahamPaintedControl and every animation reads
//   the real clock in paint; nothing ticks (Docs/08).
//
// INPUT
//   Mouse, keyboard and pad are one state: hovering lights a row exactly as
//   the d-pad does. Enter / A selects, Escape / B steps back (on HOME it
//   focuses SIGN OUT rather than quitting), the shoulders or Q / E walk the
//   settings tabs, R / Y checks the JOIN board again.
//
// CREDITS
//   The list is compiled in (Credits()), the same rule as the HUD strings: a
//   cooked build has no Docs folder to read. build_frontend.check_credits
//   holds it to Docs/LICENCES.md on every RunAll, so a credit added there and
//   missing here fails the build rather than the legal review.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "Core/OneBrahamSessionSubsystem.h"
#include "Core/OneBrahamTypes.h"
#include "UI/FrontEnd/OneBrahamPaintedControl.h"
#include "OneBrahamMainMenuWidget.generated.h"

class AOneBrahamFrontEndCamera;
class UCanvasPanel;
class UEditableText;
class UOneBrahamCinemaOverlay;
class UOneBrahamPaintedControl;
class UOneBrahamSettingsWidget;
struct FOneBrahamReveal;
enum class EOneBrahamShotTransition : uint8;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetSwitcher;

/** One block of the credits page. */
struct FOneBrahamCreditBlock
{
	FText Heading;
	FText Body;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamMainMenuWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	enum class EPage : uint8
	{
		Home,
		Play,
		Host,
		Join,
		Settings,
		Credits,
		SignOut
	};

	/** "home", "play", "host", "join", "settings", "credits", "signout" (any case). */
	static bool ParsePage(const FString& Text, EPage& OutPage);

	/** The credits, in the order they are shown. The OpenStreetMap block first. */
	static const TArray<FOneBrahamCreditBlock>& Credits();

	/** "ASCENT", "FIRE DRILL", "NIGHT SHIFT". */
	static FText ModeLabel(EOneBrahamMatchMode Mode);

	/** One row of the JOIN list as text: "12 / 16", "38 ms". */
	static FText PlayersLabel(const FOneBrahamServerInfo& Info);
	static FText PingLabel(const FOneBrahamServerInfo& Info);

	/** Every public slot taken (an unknown capacity is never "full"). */
	static bool IsFull(const FOneBrahamServerInfo& Info);

	void ShowPage(EPage Page);
	EPage GetPage() const { return Page; }

	// --- the actions every sign ends in (also what ob.menu.* calls) ----------
	void PlaySolo(EOneBrahamMatchMode Mode);
	void Host(EOneBrahamMatchMode Mode, bool bLAN);
	void RefreshJoinList();
	void JoinRow(int32 RowIndex);

	/** Shows JOIN, searches, and joins row RowIndex when it appears; searches
	 *  again (up to Attempts times) while the list is shorter than that. */
	void JoinWhenFound(int32 RowIndex, int32 Attempts);

	/** A line under the signs: why the player is back, or what went wrong. */
	void ShowNotice(const FText& Notice);

	UWidget* GetDefaultFocusTarget() const;

	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// --- the camera half of the front end ------------------------------------
	/** The camera this menu directs (the controller hands it over). */
	void AttachCamera(AOneBrahamFrontEndCamera* InCamera);

	/** The cinema overlay's dip, for the camera's Dip transitions. */
	void DipToBlack(float OutS, float HoldS, float InS);

	/** Holds one shot (the reel stops): ob.menu.cut. */
	void HoldShot(FName Shot, EOneBrahamShotTransition Transition);

	/** Focus the mode card for Mode (PLAY's focus memory; ob.menu.mode). */
	void FocusMode(EOneBrahamMatchMode Mode);

	/** Ends the cold open now, on HOME's end state. */
	void SkipColdOpen();

	/** True while the cold open is playing. */
	bool IsColdOpenPlaying() const { return bColdOpen; }

	/** The camera shot a page plays ("crown" for HOME, none for SETTINGS). */
	static FName ShotForPage(EPage InPage);

	/** The breadcrumb a page shows in the top bar ("RECEPTION / LIFT LOBBY"). */
	static FText BreadcrumbForPage(EPage InPage);

	/** The departure board's ping cue: bars (3 good, 2 fair, 1 poor, 0 unknown). */
	static int32 PingBars(int32 PingMs);

protected:
	virtual void BuildLayout() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UWidget* BuildHomePage();
	UWidget* BuildPlayPage();
	UWidget* BuildHostPage();
	UWidget* BuildJoinPage();
	UWidget* BuildSettingsPage();
	UWidget* BuildCreditsPage();
	UWidget* BuildSignOutPage();

	/** The picture layer under the pages: scrims, frost, the page furniture. */
	void PaintBackdrop(const OneBrahamPaint::FPainter& C, double Now) const;
	void PaintHomeHeader(const OneBrahamPaint::FPainter& C, double Now, float Alpha) const;
	void PaintPlayPage(const OneBrahamPaint::FPainter& C, double Now, float Alpha) const;
	void PaintHostPage(const OneBrahamPaint::FPainter& C, double Now, float Alpha) const;
	void PaintJoinPage(const OneBrahamPaint::FPainter& C, double Now, float Alpha) const;
	void PaintCredits(const OneBrahamPaint::FPainter& C, double Now, float Alpha) const;
	void PaintSignOut(const OneBrahamPaint::FPainter& C, double Now, float Alpha) const;

	/** The bars' furniture: place, clock, breadcrumb, hints, build. */
	void PaintBars(const OneBrahamPaint::FPainter& C, double Now) const;

	/** A directory row (HOME, and the pause menu's rows look the same). */
	UOneBrahamPaintedControl* MakeDirectoryRow(const FText& Verb, const FText& Where, TFunction<void()> OnChosen,
		int32 Slot, const TSharedPtr<FOneBrahamReveal>& Reveal);

	/** A painted control, focusable or not, with its page's reveal. */
	UOneBrahamPaintedControl* NewControl(const FVector2D& Size, bool bFocusable, const TSharedPtr<FOneBrahamReveal>& Reveal, int32 Slot);

	/** Links Controls into a vertical list that wraps (up from the first is the last). */
	static void LinkVertical(const TArray<UOneBrahamPaintedControl*>& Controls, bool bWrap);

	/** Places a widget on a page canvas, by fractions of the screen and pixels. */
	void Place(UCanvasPanel* Canvas, UWidget* Widget, const FVector2D& Anchor01, const FVector2D& PixelOffset,
		const FVector2D& Size, float StretchToX01 = -1.f);

	/** The page-change grammar: out, dip, in. */
	void TransitionTo(EPage InPage);
	void CompleteTransition(EPage InPage, double InAt);

	/** Applies the cold open's timeline up to Now. Called from paint. */
	void AdvanceColdOpen(double Now);
	void BeginColdOpen();
	void BuildInHome(bool bFromColdOpen);

	/** The HOME reel: the next shot, every 14 s. Called from paint. */
	void AdvanceReel(double Now);

	/** The lift: SOLO's confirm, then PlaySolo. */
	void RideLift(EOneBrahamMatchMode Mode);


	/** The board's rows for the last search. */
	void RefreshJoinRowsFocus();

	/** JOIN's direct-IP line. */
	void DialExtension(const FString& Address);

	bool IsIdle(double Now) const;
	void NoteInput(bool bGamepad);

	float PageAlpha(EPage InPage, double Now) const;

	/** HOME's opacity: stepping aside after 90 s idle, back in 250 ms on input. */
	float HomeIdleAlpha(double Now) const;
	/** Fills the JOIN list from the session subsystem's last search. */
	void RebuildJoinRows(const TArray<FOneBrahamServerInfo>& Servers);

	void HandleSessionsFound(bool bWasSuccessful, const TArray<FOneBrahamServerInfo>& Servers);
	void HandleSessionJoined(bool bWasSuccessful, const FString& TravelURL, const FText& Reason);
	void HandleSessionCreated(bool bWasSuccessful, bool bLAN);

	void SetStatus(FText& Line, const FText& Text);

	/** One step back: settings' own back first, then HOME. */
	void GoBack();

	void Quit();

	UOneBrahamSessionSubsystem* GetSessions() const;

	/** The sign that opens a page: where focus returns when it closes. */
	UOneBrahamPaintedControl* SignFor(EPage InPage) const;

	EPage Page = EPage::Home;

	EOneBrahamMatchMode HostMode = EOneBrahamMatchMode::Ascent;
	bool bHostLAN = true;

	/** Set while a host or a join is underway: a second press does nothing. */
	bool bBusy = false;

	int32 AutoJoinRow = INDEX_NONE;
	int32 AutoJoinAttemptsLeft = 0;
	FTimerHandle AutoJoinRetry;

	FDelegateHandle FoundHandle;
	FDelegateHandle JoinedHandle;
	FDelegateHandle CreatedHandle;

	UPROPERTY(Transient) TObjectPtr<UOneBrahamCinemaOverlay> Cinema = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Backdrop = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamSettingsWidget> Settings = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> PageCanvases;
	TArray<TSharedPtr<FOneBrahamReveal>> PageReveals;

	TWeakObjectPtr<AOneBrahamFrontEndCamera> Camera;

	// --- HOME ------------------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Sign_Play = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Sign_Host = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Sign_Join = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Sign_Settings = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Sign_Credits = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Sign_Quit = nullptr;
	FText Text_Notice;
	FText Text_Build;
	double HomeBuiltAt = 0.0;

	// --- PLAY ------------------------------------------------------------------
	UPROPERTY(Transient) TArray<TObjectPtr<UOneBrahamPaintedControl>> ModeCards;
	TArray<EOneBrahamMatchMode> CardModes;
	UPROPERTY(Transient) TArray<TObjectPtr<UOneBrahamPaintedControl>> LiftButtons;
	EOneBrahamMatchMode FocusedMode = EOneBrahamMatchMode::Ascent;
	double ModeFocusedAt = 0.0;
	int32 PressedLift = INDEX_NONE;
	FText Text_PlayStatus;

	// --- HOST ------------------------------------------------------------------
	UPROPERTY(Transient) TArray<TObjectPtr<UOneBrahamPaintedControl>> HostRows;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_HostGo = nullptr;
	int32 HostAttendees = 12;
	FText Text_HostStatus;
	FText Text_HostNote;
	enum class EBooking : uint8 { Available, Booking, InUse, Failed };
	EBooking Booking = EBooking::Available;
	double BookingAt = 0.0;

	// --- JOIN ------------------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> JoinRows = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_JoinRefresh = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_JoinBook = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableText> DialText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> DialFrame = nullptr;
	FText Text_JoinStatus;
	FText Text_JoinBody;
	TArray<FOneBrahamServerInfo> BoardServers;
	TArray<FString> BoardTimes;
	double BoardArrivedAt = 0.0;
	bool bSearchedOnce = false;
	int32 JoiningRow = INDEX_NONE;
	int32 FailedRow = INDEX_NONE;
	double FailedAt = 0.0;

	/** One control per row of the JOIN board, in list order. */
	UPROPERTY(Transient) TArray<TObjectPtr<UOneBrahamPaintedControl>> JoinRowButtons;

	// --- VISITORS' BOOK ----------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> CreditsFocus = nullptr;
	double CreditsAt = 0.0;
	float CreditsOffset = 0.f;
	bool bCreditsFast = false;
	mutable double CreditsLastPaint = 0.0;
	mutable float CreditsRolled = 0.f;
	mutable double CreditsStoppedAt = 0.0;

	// --- SIGN OUT ------------------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_Stay = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Button_SignOut = nullptr;
	bool bSigningOut = false;

	// --- the director's state -------------------------------------------------
	bool bColdOpen = false;
	double ColdOpenAt = 0.0;
	int32 ColdOpenStage = 0;
	int32 ReelIndex = 0;
	double NextReelAt = 0.0;
	bool bHoldShot = false;
	bool bTransitioning = false;
	FTimerHandle TransitionTimer;
	FTimerHandle ActionTimer;
	double BreadcrumbAt = 0.0;
	double LastInputAt = 0.0;
	double IdleWakeAt = 0.0;
	bool bGamepadHints = false;
	bool bFlat = false;
	EPage PreviousPage = EPage::Home;

	UFUNCTION()
	void HandleDialCommitted(const FText& Text, ETextCommit::Type CommitMethod);
};
