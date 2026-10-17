// Copyright (c) One Braham. All Rights Reserved.

#include "UI/OneBrahamCompassWidget.h"

#include "OneBraham.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/HUD/OneBrahamTween.h"

using namespace OneBrahamHUDStyle;

namespace OneBrahamCompass
{
	/** The cardinals, and the part of London each one faces from Aldgate East. */
	struct FBearing
	{
		float Degrees;
		const TCHAR* Letter;
		const TCHAR* Place;
	};

	const FBearing Bearings[] = {
		{ 0.f,   TEXT("N"), TEXT("SHOREDITCH") },
		{ 90.f,  TEXT("E"), TEXT("WHITECHAPEL") },
		{ 180.f, TEXT("S"), TEXT("TOWER") },
		{ 270.f, TEXT("W"), TEXT("CITY") },
	};
}

UOneBrahamCompassWidget::UOneBrahamCompassWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UOneBrahamCompassWidget::ResolveView(float& OutYaw, FVector& OutLocation) const
{
	OutYaw = HeadingYaw;
	OutLocation = ViewerLocation;

	// The camera, not the pawn: the control rotation is where the player is
	// looking, and the camera manager still has it while downed or spectating.
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (PC->PlayerCameraManager != nullptr)
		{
			if (!bHeadingOverride)
			{
				OutYaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
			}
			if (!bViewerOverride)
			{
				OutLocation = PC->PlayerCameraManager->GetCameraLocation();
			}
		}
	}
}

int32 UOneBrahamCompassWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace OneBrahamCompass;

	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	const FVector2D Full = AllottedGeometry.GetLocalSize();
	if (!bCompassVisible || Full.X < 40.f || Full.Y < 20.f)
	{
		return Layer;
	}

	float Yaw = 0.f;
	FVector Viewer = FVector::ZeroVector;
	ResolveView(Yaw, Viewer);
	const float Heading = BearingForYaw(Yaw);
	const int32 ViewerFloor = OneBrahamConstants::FloorIndexForZ(Viewer.Z - 120.f);

	// HUD scale about the strip's top centre: the strip stays centred on the
	// screen whatever its size.
	FOneBrahamPainter Root(OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint().A);
	FOneBrahamPainter P = Root.Child(FVector2D(Full.X * (1.f - Scale) * 0.5f, 0.f), Scale);
	const float W = Full.X / Scale;
	const float CentreX = W * 0.5f;
	const float HalfField = VisibleFieldDegrees * 0.5f;
	const float PxPerDeg = W / VisibleFieldDegrees;
	const float Base = BaselineY;
	const double Now = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;

	// Slats fade out toward the ends, so the strip has no hard edge to read as
	// a bar.
	auto EdgeFade = [HalfField](float Delta) { return FMath::Clamp(1.f - FMath::Pow(FMath::Abs(Delta) / HalfField, 4.f), 0.f, 1.f); };

	// --- the backing: a soft shadow band, so a slat reads against a white wall --
	// Not a bar: it fades to nothing at both ends and is barely there in the
	// middle, which is where the slats need it most.
	{
		const FLinearColor Shade = WithAlpha(Navy(), 0.42f);
		const FLinearColor Clear = WithAlpha(Navy(), 0.f);
		P.Gradient(FVector2D(W * 0.08f, Base - 4.f), FVector2D(W * 0.42f, 38.f), Clear, Shade);
		P.Gradient(FVector2D(W * 0.5f, Base - 4.f), FVector2D(W * 0.42f, 38.f), Shade, Clear);
	}

	// --- the ribbon glow under the notch ----------------------------------------------
	const FLinearColor Glow = WithAlpha(SignalBlue(), 0.16f);
	P.Gradient(FVector2D(CentreX - 120.f, Base), FVector2D(120.f, 14.f), WithAlpha(SignalBlue(), 0.f), Glow);
	P.Gradient(FVector2D(CentreX, Base), FVector2D(120.f, 14.f), Glow, WithAlpha(SignalBlue(), 0.f));

	// --- the hairline the slats hang from ---------------------------------------------
	P.Gradient(FVector2D(0.f, Base - 1.f), FVector2D(CentreX, 1.f), WithAlpha(LightGrey(), 0.f), WithAlpha(LightGrey(), 0.35f));
	P.Gradient(FVector2D(CentreX, Base - 1.f), FVector2D(CentreX, 1.f), WithAlpha(LightGrey(), 0.35f), WithAlpha(LightGrey(), 0.f));

	// --- slats -----------------------------------------------------------------------------
	const FSlateFontInfo LetterFont = HUDFont(EOneBrahamHUDType::Label, 13.f);
	const FSlateFontInfo PlaceFont = OutlinedFont(EOneBrahamHUDType::Hint, 10.f, 140, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.45f));

	const float First = FMath::FloorToFloat((Heading - HalfField) / 5.f) * 5.f;
	for (float Angle = First; Angle <= Heading + HalfField + 0.01f; Angle += 5.f)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(Heading, Angle);
		if (FMath::Abs(Delta) > HalfField)
		{
			continue;
		}

		const float X = CentreX + Delta * PxPerDeg;
		const int32 Whole = FMath::RoundToInt(FRotator::ClampAxis(Angle));
		const bool bMajor = Whole % 15 == 0;
		const bool bCardinal = Whole % 90 == 0;
		const float Fade = EdgeFade(Delta);

		// Each slat carries a one-pixel shadow: the outline every HUD string has.
		P.Rect(FVector2D(FMath::RoundToFloat(X) + 1.f, Base + 1.f), FVector2D(1.f, bMajor ? 14.f : 8.f), FLinearColor(0.f, 0.f, 0.f, 0.45f * Fade));
		P.Rect(FVector2D(FMath::RoundToFloat(X), Base), FVector2D(1.f, bMajor ? 14.f : 8.f),
			WithAlpha(LightGrey(), (bMajor ? 0.75f : 0.45f) * Fade));

		if (bCardinal)
		{
			for (const FBearing& Bearing : Bearings)
			{
				if (FMath::RoundToInt(Bearing.Degrees) % 360 == Whole % 360)
				{
					// The letter carries the compass; the place is decoration and is
					// set small and quiet enough to be ignored in a fight.
					const FVector2D Letter = P.Text(FVector2D(X, Base + 15.f), Bearing.Letter, LetterFont, WithAlpha(White(), Fade), EOneBrahamTextAnchor::Centre);
					const float PlaceH = Measure(Bearing.Place, PlaceFont).Y;
					P.Text(FVector2D(X + Letter.X * 0.5f + 5.f, Base + 15.f + (Letter.Y - PlaceH) * 0.5f + 1.f), Bearing.Place, PlaceFont,
						WithAlpha(LightGrey(), 0.5f * Fade));
				}
			}
		}
	}

	// --- markers -------------------------------------------------------------------------------
	const FSlateFontInfo TagFont = HUDFont(EOneBrahamHUDType::Telemetry, 11.f);
	const FSlateFontInfo SmallTag = HUDFont(EOneBrahamHUDType::Telemetry, 10.f, 40);
	const float Imminent = OneBrahamPulse::Blink(Now, OneBrahamPulse::ImminentHz, bReduceMotion);
	const float Teammate = OneBrahamPulse::Breath(Now, OneBrahamPulse::TeammateHz, 0.4f, 1.f);

	auto Project = [&](const FVector& World, float& OutDelta) -> float
	{
		const FVector To = World - Viewer;
		const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
		OutDelta = FMath::FindDeltaAngleDegrees(Yaw, TargetYaw);
		return CentreX + FMath::Clamp(OutDelta, -HalfField, HalfField) * PxPerDeg;
	};

	// Labels are laid out left to right in up to three rows above the strip,
	// so two colleagues standing together read "PR +1" over "OK +1" rather
	// than "PROK+1".
	TArray<FOneBrahamCompassMarkView> Sorted = Marks;
	Sorted.Sort([&](const FOneBrahamCompassMarkView& A, const FOneBrahamCompassMarkView& B)
	{
		float DA = 0.f, DB = 0.f;
		return Project(A.WorldLocation, DA) < Project(B.WorldLocation, DB);
	});
	float RowEnd[3] = { -1000.f, -1000.f, -1000.f };
	auto LabelRow = [&RowEnd](float X, float Width) -> float
	{
		for (int32 Row = 0; Row < 3; ++Row)
		{
			if (X - Width * 0.5f > RowEnd[Row] + 4.f)
			{
				RowEnd[Row] = X + Width * 0.5f;
				return -13.f * Row;
			}
		}
		return 0.f;
	};

	for (const FOneBrahamCompassMarkView& Mark : Sorted)
	{
		float Delta = 0.f;
		const float X = Project(Mark.WorldLocation, Delta);
		const bool bClamped = FMath::Abs(Delta) > HalfField;
		const float Side = Delta < 0.f ? -1.f : 1.f;
		const FVector2D At(bClamped ? CentreX + Side * (CentreX - 6.f) : X, Base - 7.f);

		FLinearColor Colour = SignalBlue();
		FString Label = Mark.Label;
		const int32 Floors = Mark.FloorDelta;
		const FString Delta3 = Floors == 0 ? FString() : FString::Printf(TEXT("%+d"), Floors);

		switch (Mark.Type)
		{
		case EOneBrahamCompassMark::Squad:
		{
			Colour = Mark.bAlert ? WithAlpha(EmergencyRed(), Teammate) : SignalBlue();
			if (bClamped)
			{
				// Behind you: half a chevron at the end of the strip, pointing out.
				P.Segment(At + FVector2D(-Side * 3.f, -4.f), At + FVector2D(Side * 3.f, 0.f), Colour, 2.f);
				P.Segment(At + FVector2D(Side * 3.f, 0.f), At + FVector2D(-Side * 3.f, 4.f), Colour, 2.f);
			}
			else
			{
				P.Chevron(At + FVector2D(0.f, 1.f), FVector2D(0.f, 1.f), 9.f, Colour, 2.f);
			}
			Label = Delta3.IsEmpty() ? Mark.Label : FString::Printf(TEXT("%s %s"), *Mark.Label, *Delta3);
			P.Text(At + FVector2D(0.f, -20.f + LabelRow(At.X, Measure(Label, TagFont).X)), Label, TagFont, Colour, EOneBrahamTextAnchor::Centre);
			break;
		}

		case EOneBrahamCompassMark::Ping:
		{
			// Six seconds, the last one fading.
			const float Age = static_cast<float>(Now - Mark.SpawnTime);
			const float Life = FMath::Clamp(6.f - Age, 0.f, 1.f);
			Colour = WithAlpha(SignalBlue(), Life);
			P.Diamond(At, 8.f, Colour, true);
			if (!Delta3.IsEmpty())
			{
				P.Text(At + FVector2D(0.f, -21.f + LabelRow(At.X, Measure(Delta3, TagFont).X)), Delta3, TagFont, Colour, EOneBrahamTextAnchor::Centre);
			}
			break;
		}

		case EOneBrahamCompassMark::Extraction:
		{
			Colour = SafetyYellow();
			P.Rect(At - FVector2D(5.f, 5.f), FVector2D(10.f, 10.f), Colour);
			P.Outline(At - FVector2D(6.f, 6.f), FVector2D(12.f, 12.f), WithAlpha(Navy(), 0.8f), 1.f);
			const FString Range = FString::Printf(TEXT("%dm%s"), FMath::RoundToInt(Mark.DistanceCm / 100.f),
				Floors > 0 ? *FString::Printf(TEXT(" ↑%d"), Floors) : (Floors < 0 ? *FString::Printf(TEXT(" ↓%d"), -Floors) : TEXT("")));
			P.Text(At + FVector2D(0.f, -22.f + LabelRow(At.X, Measure(Range, TagFont).X)), Range, TagFont, Colour, EOneBrahamTextAnchor::Centre);
			break;
		}

		case EOneBrahamCompassMark::Stair:
		{
			// A struck tag is a stair core the shutters have taken: red and
			// crossed through, the shape saying what the colour says.
			Colour = Mark.bStruck ? EmergencyRed() : SafetyYellow();
			const FVector2D Size = Measure(Label, SmallTag) + FVector2D(10.f, 2.f);
			const FVector2D TagPos(At.X - Size.X * 0.5f, Base - 4.f - Size.Y);
			P.RoundRect(TagPos, Size, WithAlpha(Navy(), 0.78f), 2.f);
			P.Outline(TagPos, Size, Colour, 1.f, 2.f);
			P.Text(FVector2D(At.X, TagPos.Y + 1.f), Label, SmallTag, Colour, EOneBrahamTextAnchor::Centre);
			if (Mark.bStruck)
			{
				P.Rect(FVector2D(TagPos.X + 3.f, TagPos.Y + Size.Y * 0.5f - 1.f), FVector2D(Size.X - 6.f, 2.f), Colour);
			}
			(void)Imminent;
			break;
		}

		case EOneBrahamCompassMark::Point:
		default:
		{
			P.Circle(At, 6.f, WithAlpha(Navy(), 0.85f));
			P.Ring(At, 6.f, WithAlpha(LightGrey(), 0.7f), 1.f);
			const FSlateFontInfo PointFont = HUDFont(EOneBrahamHUDType::Label, 9.f);
			P.Text(At - FVector2D(0.f, Measure(Label, PointFont).Y * 0.5f), Label, PointFont, White(), EOneBrahamTextAnchor::Centre);
			if (!Delta3.IsEmpty())
			{
				P.Text(At + FVector2D(0.f, -22.f), Delta3, TagFont, WithAlpha(LightGrey(), 0.8f), EOneBrahamTextAnchor::Centre);
			}
			break;
		}
		}
	}

	// The snapshot's generic markers, for an authored HUD that fills those.
	for (const FOneBrahamHUDCompassMarker& Marker : Markers)
	{
		float Delta = 0.f;
		const float X = Project(Marker.WorldLocation, Delta);
		const FLinearColor Colour = Marker.Colour.A > 0.f ? Marker.Colour : LightGrey();
		P.Triangle(FVector2D(X - 5.f, Base - 10.f), FVector2D(X + 5.f, Base - 10.f), FVector2D(X, Base - 3.f), Colour);
	}

	// --- the centre notch: straight ahead ------------------------------------------------------
	P.Rect(FVector2D(CentreX - 1.f, Base - 4.f), FVector2D(2.f, 18.f), SignalBlue());

	(void)ViewerFloor;
	return Layer;
}
