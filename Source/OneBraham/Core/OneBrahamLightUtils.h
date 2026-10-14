// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLightUtils.h
// ---------------------------------------------------------------------------
// Easing a light toward a colour and an intensity, touching it only when it
// would change. SetIntensity and SetLightColor mark render state dirty even
// when the value is the one already set, and the building has a card reader
// on every door, a screen on every terminal and a strobe on every shutter,
// all ticking: idle, they should cost nothing.

#pragma once

#include "CoreMinimal.h"
#include "Components/LightComponent.h"

namespace OneBrahamLights
{
	/** Eases Light toward Target intensity at Speed, and sets Colour if it differs. */
	inline void Ease(ULightComponent* Light, const FLinearColor& Colour, float Target, float DeltaSeconds, float Speed)
	{
		if (Light == nullptr)
		{
			return;
		}

		if (!Light->GetLightColor().Equals(Colour, 0.001f))
		{
			Light->SetLightColor(Colour);
		}

		const float Current = Light->Intensity;
		if (FMath::IsNearlyEqual(Current, Target, 0.5f))
		{
			if (Current != Target)
			{
				Light->SetIntensity(Target);
			}
			return;
		}
		Light->SetIntensity(FMath::FInterpTo(Current, Target, DeltaSeconds, Speed));
	}
}
