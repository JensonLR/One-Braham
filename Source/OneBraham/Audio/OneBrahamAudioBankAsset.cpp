// Copyright (c) One Braham. All Rights Reserved.

#include "Audio/OneBrahamAudioTypes.h"

#include "OneBraham.h"
#include "Audio/OneBrahamProceduralAudio.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	/** Compact row builder so the core table below stays readable. */
	FOneBrahamSoundBankEntry Row(FName Id, EOneBrahamAudioBus Bus, EOneBrahamAudioCategory Category,
		float Volume = 1.f, float MaxDistanceCm = 4500.f, float CooldownSeconds = 0.f,
		int32 MaxConcurrent = 6, bool bLooping = false, bool bSurfaceSensitive = false)
	{
		FOneBrahamSoundBankEntry Entry;
		Entry.SoundId = Id;
		Entry.Bus = Bus;
		Entry.Category = Category;
		Entry.Volume = Volume;
		Entry.MaxDistanceCm = MaxDistanceCm;
		Entry.CooldownSeconds = CooldownSeconds;
		Entry.MaxConcurrent = MaxConcurrent;
		Entry.bLooping = bLooping;
		Entry.bSurfaceSensitive = bSurfaceSensitive;
		Entry.bGainsIndoors = Category == EOneBrahamAudioCategory::Impact
			|| Category == EOneBrahamAudioCategory::Weapon;
		return Entry;
	}
}

FName UOneBrahamAudioBankAsset::GetMusicCueForState(EOneBrahamMusicState State) const
{
	const FName* Found = MusicCueByState.Find(State);

	return Found != nullptr ? *Found : FName(TEXT("MUS_Lobby"));
}

FName UOneBrahamAudioBankAsset::GetAmbienceCueForZone(FName ZoneId) const
{
	const FName* Found = AmbienceCueByZone.Find(ZoneId);

	if (Found == nullptr)
	{
		// The generic city bed rather than silence: an unknown zone is a bug, but
		// a bug the player hears as "outside", not as a dead room.
		return FName(TEXT("AMB_London_Bed"));
	}

	return *Found;
}

const FOneBrahamSoundBankEntry* UOneBrahamAudioBankAsset::FindEntry(FName SoundId) const
{
	return Entries.FindByPredicate([SoundId](const FOneBrahamSoundBankEntry& Entry)
	{
		return Entry.SoundId == SoundId;
	});
}

void UOneBrahamAudioBankAsset::EnsureCoreEntries()
{
	// -------------------------------------------------------------------
	//  The core table.
	//
	//  This is the shipping cue list. Every id here either has an authored
	//  asset slot in the content pipeline (Docs/12_Audio.md lists the exact
	//  filenames and recording briefs) or a procedural implementation in
	//  OneBrahamProceduralAudio.cpp. Nothing here is allowed to be silent.
	// -------------------------------------------------------------------
	static const FOneBrahamSoundBankEntry Core[] =
	{
		// --- Footsteps and player foley ---------------------------------
		Row(TEXT("SFX_Footstep_Carpet"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.55f, 2600.f, 0.05f, 6, false, true),
		Row(TEXT("SFX_Footstep_Hard"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.62f, 3000.f, 0.05f, 6, false, true),
		Row(TEXT("SFX_Footstep_Metal"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.70f, 3600.f, 0.05f, 6, false, true),
		Row(TEXT("SFX_Footstep_Water"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.66f, 3000.f, 0.05f, 6, false, true),
		Row(TEXT("SFX_Land_Soft"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.75f, 3200.f, 0.10f, 3),
		Row(TEXT("SFX_Land_Hard"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.95f, 4000.f, 0.10f, 3),
		Row(TEXT("SFX_Mantle"),					EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.70f, 2600.f, 0.15f, 2),
		Row(TEXT("SFX_Slide"),					EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.72f, 3200.f, 0.20f, 2),
		Row(TEXT("SFX_Crouch_Down"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.40f, 1600.f, 0.12f, 2),
		Row(TEXT("SFX_Crouch_Up"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.40f, 1600.f, 0.12f, 2),
		Row(TEXT("SFX_Cloth_Move"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.30f, 1400.f, 0.35f, 3),
		Row(TEXT("SFX_Gear_Rattle"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.45f, 1800.f, 0.50f, 3),
		Row(TEXT("SFX_Downed_Heartbeat"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Loop, 0.55f, 0.f, 0.f, 1, true),
		Row(TEXT("SFX_Downed_Tinnitus"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Loop, 0.35f, 0.f, 0.f, 1, true),

		// --- Weapons -----------------------------------------------------
		Row(TEXT("SFX_Gunshot_Pistol"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.0f, 9000.f, 0.02f, 12),
		Row(TEXT("SFX_Gunshot_SMG"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.0f, 9000.f, 0.02f, 12),
		Row(TEXT("SFX_Gunshot_Shotgun"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.1f, 12000.f, 0.02f, 8),
		Row(TEXT("SFX_Gunshot_Rifle"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.05f, 14000.f, 0.02f, 12),
		Row(TEXT("SFX_Gunshot_Marksman"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.15f, 16000.f, 0.02f, 8),
		Row(TEXT("SFX_Gunshot_Suppressed"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 0.45f, 2600.f, 0.02f, 12),
		// The layers PlayWeaponShot puts around the close report: the action
		// (close range only), the distant report (dominant past ~40 m), and the
		// room's answer - one tail per reverb profile, played 2D because a
		// diffuse tail comes from everywhere at once.
		Row(TEXT("SFX_Gunshot_Mech"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.80f, 2200.f, 0.00f, 10),
		Row(TEXT("SFX_Gunshot_Distant_Light"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.0f, 40000.f, 0.02f, 10),
		Row(TEXT("SFX_Gunshot_Distant_Heavy"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Weapon, 1.0f, 50000.f, 0.02f, 10),
		Row(TEXT("SFX_Gunshot_Tail_Lobby"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_Mezzanine"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_OpenPlan"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_ServerComms"), EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_Executive"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_Canteen"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_Skydeck"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_Plaza"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_Gunshot_Tail_Stairwell"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Interface, 1.0f, 0.f, 0.05f, 6),
		Row(TEXT("SFX_DryFire"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.55f, 1500.f, 0.10f, 2),
		Row(TEXT("SFX_Reload_Mag_Out"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.75f, 2600.f, 0.05f, 3),
		Row(TEXT("SFX_Reload_Mag_In"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.80f, 2600.f, 0.05f, 3),
		Row(TEXT("SFX_Reload_Bolt"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.78f, 2600.f, 0.05f, 3),
		Row(TEXT("SFX_Weapon_Draw"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.60f, 2000.f, 0.15f, 3),
		Row(TEXT("SFX_Shell_Drop"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.45f, 1200.f, 0.03f, 6),
		Row(TEXT("SFX_Shell_Drop_Carpet"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.35f, 800.f, 0.03f, 6),
		Row(TEXT("SFX_Shell_Drop_Metal"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.55f, 1800.f, 0.03f, 6),
		Row(TEXT("SFX_FireMode_Switch"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.55f, 1400.f, 0.10f, 2),
		Row(TEXT("SFX_Laser_On"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.40f, 1200.f, 0.10f, 2),
		Row(TEXT("SFX_Laser_Off"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.40f, 1200.f, 0.10f, 2),
		Row(TEXT("SFX_Light_Click"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.45f, 1200.f, 0.10f, 2),
		Row(TEXT("SFX_Melee_Swing"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.55f, 1800.f, 0.20f, 3),
		Row(TEXT("SFX_Melee_Thud"),				EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Foley, 0.85f, 2400.f, 0.10f, 3, false, true),
		Row(TEXT("SFX_Bullet_Impact_Concrete"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.80f, 6000.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Metal"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.85f, 7000.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Wood"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.78f, 5200.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Glass"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.90f, 9000.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Carpet"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.55f, 3000.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Flesh"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.85f, 3600.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Plaster"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.70f, 4500.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_Water"),	EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.70f, 4000.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Impact_ServerRack"), EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.85f, 7000.f, 0.02f, 10, false, true),
		Row(TEXT("SFX_Bullet_Whizz"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.70f, 2000.f, 0.08f, 6),
		Row(TEXT("SFX_Bullet_Crack"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.90f, 2000.f, 0.05f, 6),
		Row(TEXT("SFX_Body_Fall"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.85f, 3200.f, 0.05f, 4, false, true),
		Row(TEXT("SFX_Helmet_Ping"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.0f, 2400.f, 0.10f, 2),

		// --- Utility -----------------------------------------------------
		Row(TEXT("SFX_FuseBeep"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Objective, 0.55f, 4000.f, 0.f, 4),
		Row(TEXT("SFX_FuseBeep_Close"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Interface, 0.75f, 0.f, 0.f, 2),
		Row(TEXT("SFX_Utility_Throw"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.55f, 2200.f, 0.10f, 3),
		Row(TEXT("SFX_UtilityBounce"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.60f, 3200.f, 0.03f, 6, false, true),
		Row(TEXT("SFX_Breach_Plant"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.85f, 3600.f, 0.00f, 4),
		Row(TEXT("SFX_Frag_Explosion"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.15f, 18000.f, 0.00f, 6),
		Row(TEXT("SFX_Breach_Explosion"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.25f, 22000.f, 0.00f, 4),
		// PlayExplosion's layers around the blast itself.
		Row(TEXT("SFX_Explosion_Debris"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.90f, 8000.f, 0.00f, 4),
		Row(TEXT("SFX_Explosion_Distant"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.10f, 60000.f, 0.00f, 4),
		Row(TEXT("SFX_Flashbang_Bang"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.20f, 20000.f, 0.00f, 4),
		Row(TEXT("SFX_Flashbang_Ring"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Interface, 0.60f, 0.f, 0.f, 1),
		Row(TEXT("SFX_Smoke_Deploy"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 0.75f, 5000.f, 0.00f, 2),
		Row(TEXT("SFX_Medkit_Use"),				EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.65f, 2000.f, 0.50f, 1),
		Row(TEXT("SFX_Medkit_Complete"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Interface, 0.55f, 0.f, 0.00f, 1),
		Row(TEXT("SFX_Revive_Start"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Foley, 0.70f, 2200.f, 0.00f, 2),
		Row(TEXT("SFX_Revive_Complete"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Objective, 0.80f, 3000.f, 0.00f, 2),
		Row(TEXT("SFX_Bandage_Loop"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Loop, 0.55f, 1600.f, 0.00f, 1, true),

		// --- World / objective -------------------------------------------
		Row(TEXT("SFX_Door_Open"),				EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.65f, 4000.f, 0.10f, 4),
		// The wall coming down, from a charge. Two seconds of weight, and the
		// loudest single event in the building after the helicopter.
		Row(TEXT("SFX_Wall_Collapse"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.15f, 26000.f, 0.00f, 3),
		// Fire shutters closing through the lockdown, one after another.
		Row(TEXT("SFX_Shutter_Close"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.95f, 12000.f, 0.05f, 6, false, true),
		Row(TEXT("SFX_Door_Close"),				EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.60f, 3800.f, 0.10f, 4),
		Row(TEXT("SFX_Door_Locked"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.70f, 2000.f, 0.20f, 3),
		Row(TEXT("SFX_Keycard_Use"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.75f, 2400.f, 0.20f, 3),
		Row(TEXT("SFX_Terminal_Accept"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.80f, 2600.f, 0.20f, 3),
		Row(TEXT("SFX_Terminal_Deny"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Mechanical, 0.80f, 2600.f, 0.20f, 3),
		Row(TEXT("SFX_Terminal_Hum"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.35f, 900.f, 0.00f, 4, true),
		Row(TEXT("SFX_Terminal_Progress"),		EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Loop, 0.60f, 2600.f, 0.00f, 2, true),
		Row(TEXT("SFX_Override_Hum"),			EOneBrahamAudioBus::Gameplay, EOneBrahamAudioCategory::Loop, 0.50f, 2600.f, 0.00f, 2, true),
		Row(TEXT("SFX_Lift_Arrive"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.85f, 6000.f, 0.00f, 3),
		Row(TEXT("SFX_Lift_Doors"),				EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 0.70f, 4500.f, 0.00f, 3),
		Row(TEXT("SFX_Lift_Motor"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.55f, 3000.f, 0.00f, 2, true),
		Row(TEXT("SFX_Shutter_Drop"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Mechanical, 1.0f, 9000.f, 0.00f, 4),
		Row(TEXT("SFX_Lockdown_Siren"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.75f, 0.f, 0.00f, 1, true),
		Row(TEXT("SFX_FireAlarm"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.70f, 0.f, 0.00f, 1, true),
		Row(TEXT("SFX_Security_Purge"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Objective, 0.85f, 0.f, 0.00f, 1),
		Row(TEXT("SFX_Power_Down"),				EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Objective, 0.80f, 8000.f, 0.00f, 2),
		Row(TEXT("SFX_Window_Break"),			EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Impact, 1.0f, 14000.f, 0.00f, 4),
		Row(TEXT("SFX_Extraction_Start"),		EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Objective, 0.85f, 0.f, 0.00f, 1),
		Row(TEXT("SFX_Extraction_Winch"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Loop, 0.80f, 12000.f, 0.00f, 1, true),
		Row(TEXT("SFX_Helicopter_Rotor"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Loop, 0.90f, 30000.f, 0.00f, 2, true),
		Row(TEXT("SFX_Helicopter_Leave"),		EOneBrahamAudioBus::SFX, EOneBrahamAudioCategory::Objective, 0.90f, 30000.f, 0.00f, 1),

		// --- Combat feedback and UI ---------------------------------------
		Row(TEXT("UI_HitMarker"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.65f, 0.f, 0.02f, 6),
		Row(TEXT("UI_HitMarker_Headshot"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.80f, 0.f, 0.02f, 4),
		Row(TEXT("UI_HitMarker_Armour"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.70f, 0.f, 0.02f, 4),
		Row(TEXT("UI_KillConfirm"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.70f, 0.f, 0.05f, 3),
		Row(TEXT("UI_Damage_Taken"),			EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.60f, 0.f, 0.10f, 3),
		Row(TEXT("UI_LowHealth"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.55f, 0.f, 2.00f, 1),
		Row(TEXT("UI_LowAmmo_Click"),			EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.55f, 0.f, 0.15f, 1),
		Row(TEXT("UI_Click"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.60f, 0.f, 0.02f, 4),
		Row(TEXT("UI_Hover"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.45f, 0.f, 0.02f, 4),
		Row(TEXT("UI_Accept"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.65f, 0.f, 0.05f, 3),
		Row(TEXT("UI_Deny"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.65f, 0.f, 0.05f, 3),
		Row(TEXT("UI_ScoreTick"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.50f, 0.f, 0.05f, 4),
		Row(TEXT("UI_Banner"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.60f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Countdown"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.65f, 0.f, 0.00f, 1),
		Row(TEXT("UI_Objective_Capture"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.70f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Objective_Lost"),			EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.70f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Match_Win"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.85f, 0.f, 0.00f, 1),
		Row(TEXT("UI_Match_Lose"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.85f, 0.f, 0.00f, 1),
		Row(TEXT("UI_LevelUp"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.80f, 0.f, 0.00f, 1),
		Row(TEXT("UI_Unlock"),					EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.80f, 0.f, 0.00f, 1),
		Row(TEXT("UI_Voice_Connected"),			EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.50f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Voice_Disconnected"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.50f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Voice_ChannelSwitch"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.45f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Voice_Muted"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.45f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Voice_Radio_Open"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.45f, 0.f, 0.00f, 2),
		Row(TEXT("UI_Voice_Radio_Close"),		EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.45f, 0.f, 0.00f, 2),
		Row(TEXT("UI_TeamPing"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.60f, 0.f, 0.02f, 4),
		Row(TEXT("UI_Ping_Enemy"),				EOneBrahamAudioBus::UI, EOneBrahamAudioCategory::Interface, 0.70f, 0.f, 0.02f, 4),

		// --- Ambience -----------------------------------------------------
		Row(TEXT("AMB_Plaza_Street"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.75f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Plaza_Rain"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.55f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Lobby"),					EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.70f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Lift_Lobby"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.65f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Office_Hum"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.60f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Server_Room"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.85f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Executive"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.55f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Canteen"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.60f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Skydeck_Wind"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.90f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_London_Bed"),				EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.65f, 0.f, 0.00f, 1, true),
		Row(TEXT("AMB_Stairwell_Door"),			EOneBrahamAudioBus::Ambience, EOneBrahamAudioCategory::Loop, 0.55f, 0.f, 0.00f, 1, true),

		// --- Music --------------------------------------------------------
		Row(TEXT("MUS_Lobby"),					EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.55f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_MatchEarly"),				EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.50f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_MatchMid"),				EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.55f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_MatchLate"),				EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.60f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_Lockdown"),				EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.65f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_Extraction"),				EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.70f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_Downed"),					EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.45f, 0.f, 0.00f, 1, true),
		Row(TEXT("MUS_Results"),				EOneBrahamAudioBus::Music, EOneBrahamAudioCategory::Loop, 0.60f, 0.f, 0.00f, 1, true)
	};

	int32 Added = 0;
	for (const FOneBrahamSoundBankEntry& Entry : Core)
	{
		if (FindEntry(Entry.SoundId) == nullptr)
		{
			Entries.Add(Entry);
			++Added;
		}
	}

	// Ambience by zone: the building layout's own area ids, plus the stairwell.
	//
	// This used to be keyed Zone_Plaza, Zone_Office, ... - a third vocabulary
	// that neither the map nor the layout nor the audio manager used. The manager
	// had a fourth (bare "Plaza", "Offices", "ServerRoom"), nothing called the
	// setter that read either, and the beds themselves were briefs with no
	// synthesis, so the building had no ambience at all. One vocabulary now, and
	// it is the layout's.
	if (AmbienceCueByZone.Num() == 0)
	{
		const TPair<FName, const TCHAR*> ZoneCues[] = {
			{ TEXT("Ground_Plaza"), TEXT("AMB_Plaza_Street") },
			{ TEXT("Lobby"),        TEXT("AMB_Lobby") },
			{ TEXT("Mezzanine"),    TEXT("AMB_Lift_Lobby") },
			{ TEXT("OpenPlan"),     TEXT("AMB_Office_Hum") },
			{ TEXT("ServerComms"),  TEXT("AMB_Server_Room") },
			{ TEXT("Executive"),    TEXT("AMB_Executive") },
			{ TEXT("Skydeck"),      TEXT("AMB_Skydeck_Wind") },
			{ TEXT("Summit"),       TEXT("AMB_Executive") },	// the management suite: carpet hush
			{ TEXT("WinterGarden"), TEXT("AMB_London_Bed") },	// the city, heard from the top of it
			{ TEXT("Stairwell"),    TEXT("AMB_Stairwell_Door") }
		};

		for (const TPair<FName, const TCHAR*>& Pair : ZoneCues)
		{
			AmbienceCueByZone.Add(Pair.Key, FName(Pair.Value));
		}
	}

	// The stairwell is not a floor: it is the concrete between two of them, so
	// the layout has no area id for it and the director uses it whenever a floor
	// has no area at all.
	AmbienceFallbackZone = FName(TEXT("Stairwell"));

	// Music bed wiring: the director states point at the minted cue ids, so the
	// asset works the moment a designer presses "Ensure Core Entries".
	if (MusicCueByState.Num() == 0)
	{
		MusicCueByState.Add(EOneBrahamMusicState::Lobby,      FName(TEXT("MUS_Lobby")));
		MusicCueByState.Add(EOneBrahamMusicState::MatchEarly, FName(TEXT("MUS_MatchEarly")));
		MusicCueByState.Add(EOneBrahamMusicState::MatchMid,   FName(TEXT("MUS_MatchMid")));
		MusicCueByState.Add(EOneBrahamMusicState::MatchLate,  FName(TEXT("MUS_MatchLate")));
		MusicCueByState.Add(EOneBrahamMusicState::Lockdown,   FName(TEXT("MUS_Lockdown")));
		MusicCueByState.Add(EOneBrahamMusicState::Extraction, FName(TEXT("MUS_Extraction")));
		MusicCueByState.Add(EOneBrahamMusicState::Downed,     FName(TEXT("MUS_Downed")));
		MusicCueByState.Add(EOneBrahamMusicState::Results,    FName(TEXT("MUS_Results")));
	}

	// Resolve the mix assets the audio manager expects. These are created by the
	// audio bootstrap Python script; EnsureCoreEntries only records the intent.
	if (MasterSoundClass.IsNull())
	{
		MasterSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/OneBraham/Audio/Mix/SC_OneBraham_Master.SC_OneBraham_Master")));
	}
	if (BaseSoundMix.IsNull())
	{
		BaseSoundMix = TSoftObjectPtr<USoundMix>(FSoftObjectPath(TEXT("/Game/OneBraham/Audio/Mix/SM_OneBraham.SM_OneBraham")));
	}
	if (VoiceDuckSoundMix.IsNull())
	{
		VoiceDuckSoundMix = TSoftObjectPtr<USoundMix>(FSoftObjectPath(TEXT("/Game/OneBraham/Audio/Mix/SM_OneBraham_VoiceDuck.SM_OneBraham_VoiceDuck")));
	}

	UE_LOG(LogOneBrahamAudio, Log, TEXT("[OB] Audio bank core table ensured: %d row(s) added, %d total."), Added, Entries.Num());
}

bool UOneBrahamAudioBankAsset::ValidateBank(TArray<FString>& OutProblems) const
{
	OutProblems.Reset();

	TSet<FName> Seen;
	for (const FOneBrahamSoundBankEntry& Entry : Entries)
	{
		if (Entry.SoundId.IsNone())
		{
			OutProblems.Add(TEXT("Bank row with an empty SoundId."));
			continue;
		}

		if (Seen.Contains(Entry.SoundId))
		{
			OutProblems.Add(FString::Printf(TEXT("Duplicate SoundId '%s'."), *Entry.SoundId.ToString()));
			continue;
		}
		Seen.Add(Entry.SoundId);

		if (Entry.Volume <= 0.f)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s' has volume %.2f - it will never be heard."),
				*Entry.SoundId.ToString(), Entry.Volume));
		}

		if (Entry.Sound.IsNull() && !OneBrahamProceduralAudio::HasSynthesisedSound(Entry.SoundId))
		{
			// Not fatal: an authored asset may still be assigned before ship, but it
			// is exactly the sort of silence that ships by accident.
			OutProblems.Add(FString::Printf(
				TEXT("'%s' has no asset and no procedural fallback - this cue is silent."),
				*Entry.SoundId.ToString()));
		}

		// A 2D cue has no distance by design. Interface and Loop are the two 2D
		// categories - UI is heard in the head, a bed is the room - so this rule
		// used to report all eleven ambience beds and all eight music beds as
		// broken, on top of the silent-cue report for the same rows.
		const bool bIs2D = Entry.Category == EOneBrahamAudioCategory::Interface
			|| Entry.Category == EOneBrahamAudioCategory::Loop;

		if (!bIs2D && Entry.MaxDistanceCm <= 0.f)
		{
			OutProblems.Add(FString::Printf(TEXT("'%s' is positional but has MaxDistanceCm = 0."),
				*Entry.SoundId.ToString()));
		}
	}

	// Every music state must name a cue that exists, otherwise the director
	// silently keeps whatever is already playing and the match never changes
	// mood. This used to ask for a soft object path in a second map with no
	// reader; the cue is what actually gets played, so the cue is what is checked.
	for (uint8 StateIndex = 0; StateIndex <= static_cast<uint8>(EOneBrahamMusicState::Results); ++StateIndex)
	{
		const EOneBrahamMusicState State = static_cast<EOneBrahamMusicState>(StateIndex);
		const FName* Found = MusicCueByState.Find(State);

		if (Found == nullptr || Found->IsNone())
		{
			OutProblems.Add(FString::Printf(TEXT("Music state '%s' has no cue id."),
				*UEnum::GetDisplayValueAsText(State).ToString()));
		}
		else if (FindEntry(*Found) == nullptr)
		{
			OutProblems.Add(FString::Printf(TEXT("Music state '%s' plays '%s', which is not a bank row."),
				*UEnum::GetDisplayValueAsText(State).ToString(), *Found->ToString()));
		}
	}

	// Every zone must name a cue that exists, for the same reason.
	for (const TPair<FName, FName>& Pair : AmbienceCueByZone)
	{
		if (FindEntry(Pair.Value) == nullptr)
		{
			OutProblems.Add(FString::Printf(TEXT("Zone '%s' plays '%s', which is not a bank row."),
				*Pair.Key.ToString(), *Pair.Value.ToString()));
		}
	}

	UE_LOG(LogOneBrahamAudio, Log, TEXT("[OB] Audio bank validation: %d row(s), %d problem(s)."),
		Entries.Num(), OutProblems.Num());

	return OutProblems.Num() == 0;
}
