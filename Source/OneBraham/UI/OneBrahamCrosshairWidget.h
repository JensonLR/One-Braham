// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCrosshairWidget.h
// ---------------------------------------------------------------------------
// The crosshair, drawn entirely in code.
//
// WHY DRAWN, NOT ARTED
//   A dynamic crosshair has to react to spread, hit markers, reloads and damage
//   direction at 60+ Hz. Drawing it from primitives is cheaper than any texture
//   atlas, scales losslessly to 4K, and lets the gap track the real weapon
//   spread in degrees rather than an artist's guess at four pose states.
//
// DESIGN
//   Four ticks around a 1 px centre dot. The gap is the projection of the actual
//   bullet cone onto the screen at the current aim distance, so the crosshair
//   tells the truth about where a bullet will go - at 10 m a 1.4 degree cone is
//   visibly tighter than at 50 m. Hit markers are four diagonal ticks that snap
//   inward; a kill replaces them with a crossed tick pair.
//
//   Nothing here is animated with a curve asset: every value is derived from the
//   HUD snapshot, so the widget is correct on its very first frame.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OneBrahamCrosshairWidget.generated.h"

class UOneBrahamHUDComponent;

UCLASS()
class ONEBRAHAM_API UOneBrahamCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UOneBrahamCrosshairWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// -----------------------------------------------------------------------
	//  Data (pushed by UOneBrahamHUDWidget::ApplyHUDData)
	// -----------------------------------------------------------------------
	/** Spread in degrees. Drives the tick gap. */
	void SetSpreadDegrees(float Degrees) { SpreadDegrees = FMath::Max(0.f, Degrees); }

	/** 1 = full spread (weapon raised, sprinting), 0 = settled. */
	void SetSpreadAlpha(float Alpha01) { SpreadAlpha = FMath::Clamp(Alpha01, 0.f, 1.f); }

	/** Hit-marker timer: 1 immediately after a confirmed hit, 0 when idle. */
	void SetHitMarker(float Alpha01, bool bHeadshot, bool bKill)
	{
		HitMarkerAlpha = FMath::Clamp(Alpha01, 0.f, 1.f);
		bHitMarkerHeadshot = bHeadshot;
		bHitMarkerKill = bKill;
	}

	/**
	 * Degrees clockwise from where the player is looking; below -999 = no
	 * indicator. Recomputed by the HUD component every frame from the attacker's
	 * world position and the current view, so it tracks a turn.
	 */
	void SetDamageDirection(float Degrees)
	{
		DamageDirectionDegrees = Degrees;
	}

	void SetDamageAlpha(float Alpha01) { DamageAlpha = FMath::Clamp(Alpha01, 0.f, 1.f); }

	/** Reload ring: 0..1, drawn as an arc so the player can time the re-peek. */
	void SetReloadProgress(float Progress01) { ReloadProgress = FMath::Clamp(Progress01, 0.f, 1.f); }

	/** Interaction ring: 0..1, drawn inside the crosshair. */
	void SetInteractionProgress(float Progress01) { InteractionProgress = FMath::Clamp(Progress01, 0.f, 1.f); }

	/** True when the weapon is aimed down sights (ticks shrink and brighten). */
	void SetAimingDownSights(bool bAiming) { bAimingDownSights = bAiming; }

	/** Blocked interaction (no keycard, no line of sight): ticks turn red. */
	void SetBlocked(bool bBlocked) { bBlockedInteraction = bBlocked; }

	/** Global visibility, honouring the settings toggle. */
	void SetCrosshairVisible(bool bVisible) { bCrosshairVisible = bVisible; }

	/** Low-health pulse alpha, 0 when healthy. */
	void SetLowHealthPulse(float Alpha01) { LowHealthPulse = FMath::Clamp(Alpha01, 0.f, 1.f); }

protected:
	/** Projects the weapon's cone onto the screen at the reference distance. */
	float ComputeTickGapPixels(float WidgetScale) const;

	/** Screen radius for the damage-direction arc. */
	float ComputeDamageRadiusPixels() const;

	float SpreadDegrees = 0.35f;
	float SpreadAlpha = 0.f;
	float HitMarkerAlpha = 0.f;
	bool bHitMarkerHeadshot = false;
	bool bHitMarkerKill = false;
	float DamageDirectionDegrees = -1000.f;
	float DamageAlpha = 0.f;
	float ReloadProgress = 0.f;
	float InteractionProgress = 0.f;
	bool bAimingDownSights = false;
	bool bBlockedInteraction = false;
	bool bCrosshairVisible = true;
	float LowHealthPulse = 0.f;

	// --- tuning (a designer can change all of this from the Blueprint) -----
	/** Distance in cm at which the spread cone is projected onto the screen. */
	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float ReferenceDistanceCm = 1200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float TickLengthPixels = 7.f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float TickThicknessPixels = 1.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float BaseGapPixels = 3.f;

	/** Multiplies the projected spread - the only fudge factor in the drawing. */
	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float SpreadScale = 1.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float MaxGapPixels = 64.f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float HitMarkerRadiusPixels = 16.f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float DamageArcRadiusPixels = 92.f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float DamageArcThicknessPixels = 4.f;
};
