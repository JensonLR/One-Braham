// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameplayTags.h"

#include "Core/OneBrahamTypes.h"

namespace OneBrahamTags
{
	// --- Character --------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Character_State_Alive, "OB.Character.State.Alive");
	UE_DEFINE_GAMEPLAY_TAG(Character_State_Downed, "OB.Character.State.Downed");
	UE_DEFINE_GAMEPLAY_TAG(Character_State_Dead, "OB.Character.State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(Character_State_Spectating, "OB.Character.State.Spectating");
	UE_DEFINE_GAMEPLAY_TAG(Character_State_Extracting, "OB.Character.State.Extracting");
	UE_DEFINE_GAMEPLAY_TAG(Character_State_Extracted, "OB.Character.State.Extracted");

	UE_DEFINE_GAMEPLAY_TAG(Character_Archetype_BusinessAnalyst, "OB.Character.Archetype.BusinessAnalyst");
	UE_DEFINE_GAMEPLAY_TAG(Character_Archetype_Graduate, "OB.Character.Archetype.Graduate");
	UE_DEFINE_GAMEPLAY_TAG(Character_Archetype_FinanceBroker, "OB.Character.Archetype.FinanceBroker");
	UE_DEFINE_GAMEPLAY_TAG(Character_Archetype_ITOperations, "OB.Character.Archetype.ITOperations");
	UE_DEFINE_GAMEPLAY_TAG(Character_Archetype_FacilitiesSecurity, "OB.Character.Archetype.FacilitiesSecurity");
	UE_DEFINE_GAMEPLAY_TAG(Character_Archetype_Consultant, "OB.Character.Archetype.Consultant");

	// --- Teams ------------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Team_Alpha, "OB.Team.Alpha");
	UE_DEFINE_GAMEPLAY_TAG(Team_Bravo, "OB.Team.Bravo");
	UE_DEFINE_GAMEPLAY_TAG(Team_Charlie, "OB.Team.Charlie");
	UE_DEFINE_GAMEPLAY_TAG(Team_Delta, "OB.Team.Delta");
	UE_DEFINE_GAMEPLAY_TAG(Team_InternalStaff, "OB.Team.InternalStaff");
	UE_DEFINE_GAMEPLAY_TAG(Team_ExternalContractors, "OB.Team.ExternalContractors");
	UE_DEFINE_GAMEPLAY_TAG(Team_Spectator, "OB.Team.Spectator");

	// --- Access -----------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Access_Tier_Public, "OB.Access.Tier.Public");
	UE_DEFINE_GAMEPLAY_TAG(Access_Tier_Staff, "OB.Access.Tier.Staff");
	UE_DEFINE_GAMEPLAY_TAG(Access_Tier_Secure, "OB.Access.Tier.Secure");
	UE_DEFINE_GAMEPLAY_TAG(Access_Tier_Executive, "OB.Access.Tier.Executive");
	UE_DEFINE_GAMEPLAY_TAG(Access_Tier_Skydeck, "OB.Access.Tier.Skydeck");
	UE_DEFINE_GAMEPLAY_TAG(Access_Tier_Override, "OB.Access.Tier.Override");

	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_Lobby, "OB.Access.Zone.Lobby");
	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_Mezzanine, "OB.Access.Zone.Mezzanine");
	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_Offices, "OB.Access.Zone.Offices");
	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_ServerFloor, "OB.Access.Zone.ServerFloor");
	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_Executive, "OB.Access.Zone.Executive");
	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_Canteen, "OB.Access.Zone.Canteen");
	UE_DEFINE_GAMEPLAY_TAG(Access_Zone_Skydeck, "OB.Access.Zone.Skydeck");

	UE_DEFINE_GAMEPLAY_TAG(Access_Method_Keycard, "OB.Access.Method.Keycard");
	UE_DEFINE_GAMEPLAY_TAG(Access_Method_Override, "OB.Access.Method.Override");
	UE_DEFINE_GAMEPLAY_TAG(Access_Method_Breach, "OB.Access.Method.Breach");
	UE_DEFINE_GAMEPLAY_TAG(Access_Method_Escalation, "OB.Access.Method.Escalation");

	// --- Interaction ------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Door, "OB.Interaction.Door");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Door_Secure, "OB.Interaction.Door.Secure");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Door_Breachable, "OB.Interaction.Door.Breachable");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Terminal, "OB.Interaction.Terminal");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Lift, "OB.Interaction.Lift");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Keycard, "OB.Interaction.Keycard");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Weapon, "OB.Interaction.Weapon");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Ammo, "OB.Interaction.Ammo");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Utility, "OB.Interaction.Utility");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Medkit, "OB.Interaction.Medkit");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Revive, "OB.Interaction.Revive");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Body, "OB.Interaction.Body");

	// --- Weapons ----------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_Pistol, "OB.Weapon.Pistol");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_CompactPistol, "OB.Weapon.Pistol.Compact");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_SuppressedPistol, "OB.Weapon.Pistol.Suppressed");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_SMG, "OB.Weapon.SMG");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_PDW, "OB.Weapon.PDW");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_PumpShotgun, "OB.Weapon.Shotgun.Pump");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_SemiAutoShotgun, "OB.Weapon.Shotgun.SemiAuto");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_Carbine556, "OB.Weapon.Carbine.556");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_CompactAssaultRifle, "OB.Weapon.Rifle.Compact");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_MarksmanRifle, "OB.Weapon.Marksman");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Category_Utility, "OB.Weapon.Utility");

	UE_DEFINE_GAMEPLAY_TAG(Weapon_Fire_Single, "OB.Weapon.Fire.Single");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Fire_Burst, "OB.Weapon.Fire.Burst");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Fire_Auto, "OB.Weapon.Fire.Auto");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Fire_Pump, "OB.Weapon.Fire.Pump");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Fire_Bolt, "OB.Weapon.Fire.Bolt");

	UE_DEFINE_GAMEPLAY_TAG(Weapon_Utility_Flashbang, "OB.Weapon.Utility.Flashbang");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Utility_Smoke, "OB.Weapon.Utility.Smoke");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Utility_Frag, "OB.Weapon.Utility.Frag");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Utility_BreachCharge, "OB.Weapon.Utility.BreachCharge");
	UE_DEFINE_GAMEPLAY_TAG(Weapon_Utility_Medkit, "OB.Weapon.Utility.Medkit");

	// --- Match ------------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Match_Mode_Ascent, "OB.Match.Mode.Ascent");
	UE_DEFINE_GAMEPLAY_TAG(Match_Mode_FireDrill, "OB.Match.Mode.FireDrill");
	UE_DEFINE_GAMEPLAY_TAG(Match_Mode_NightShift, "OB.Match.Mode.NightShift");

	UE_DEFINE_GAMEPLAY_TAG(Match_Phase_Warmup, "OB.Match.Phase.Warmup");
	UE_DEFINE_GAMEPLAY_TAG(Match_Phase_Active, "OB.Match.Phase.Active");
	UE_DEFINE_GAMEPLAY_TAG(Match_Phase_Extraction, "OB.Match.Phase.Extraction");
	UE_DEFINE_GAMEPLAY_TAG(Match_Phase_Complete, "OB.Match.Phase.Complete");

	UE_DEFINE_GAMEPLAY_TAG(Match_Lockdown_Normal, "OB.Match.Lockdown.Normal");
	UE_DEFINE_GAMEPLAY_TAG(Match_Lockdown_PreLockdown, "OB.Match.Lockdown.PreLockdown");
	UE_DEFINE_GAMEPLAY_TAG(Match_Lockdown_Lockdown, "OB.Match.Lockdown.Lockdown");
	UE_DEFINE_GAMEPLAY_TAG(Match_Lockdown_Purge, "OB.Match.Lockdown.Purge");

	// --- Extraction -------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Extraction_Inactive, "OB.Extraction.Inactive");
	UE_DEFINE_GAMEPLAY_TAG(Extraction_Broadcast, "OB.Extraction.Broadcast");
	UE_DEFINE_GAMEPLAY_TAG(Extraction_Arriving, "OB.Extraction.Arriving");
	UE_DEFINE_GAMEPLAY_TAG(Extraction_Holding, "OB.Extraction.Holding");
	UE_DEFINE_GAMEPLAY_TAG(Extraction_Complete, "OB.Extraction.Complete");
	UE_DEFINE_GAMEPLAY_TAG(Extraction_Failed, "OB.Extraction.Failed");

	// --- Voice ------------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_RoundStart, "OB.Voice.Category.RoundStart");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Movement, "OB.Voice.Category.Movement");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Contact, "OB.Voice.Category.Contact");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Combat, "OB.Voice.Category.Combat");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Reload, "OB.Voice.Category.Reload");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_LowAmmo, "OB.Voice.Category.LowAmmo");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Down, "OB.Voice.Category.Down");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Death, "OB.Voice.Category.Death");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Revive, "OB.Voice.Category.Revive");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Access, "OB.Voice.Category.Access");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Extraction, "OB.Voice.Category.Extraction");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Win, "OB.Voice.Category.Win");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Lose, "OB.Voice.Category.Lose");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Reactive, "OB.Voice.Category.Reactive");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Category_Banter, "OB.Voice.Category.Banter");

	UE_DEFINE_GAMEPLAY_TAG(Voice_Channel_Proximity, "OB.Voice.Channel.Proximity");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Channel_Squad, "OB.Voice.Channel.Squad");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Channel_Team, "OB.Voice.Channel.Team");
	UE_DEFINE_GAMEPLAY_TAG(Voice_Channel_Radio, "OB.Voice.Channel.Radio");

	// --- Audio ------------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Rain_Light, "OB.Audio.State.Rain.Light");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Rain_Heavy, "OB.Audio.State.Rain.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Exterior, "OB.Audio.State.Floor.Exterior");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Lobby, "OB.Audio.State.Floor.Lobby");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Mezzanine, "OB.Audio.State.Floor.Mezzanine");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Offices, "OB.Audio.State.Floor.Offices");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Server, "OB.Audio.State.Floor.Server");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Executive, "OB.Audio.State.Floor.Executive");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Canteen, "OB.Audio.State.Floor.Canteen");
	UE_DEFINE_GAMEPLAY_TAG(Audio_State_Floor_Skydeck, "OB.Audio.State.Floor.Skydeck");

	// --- Cosmetics --------------------------------------------------------
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Head, "OB.Cosmetic.Slot.Head");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Torso, "OB.Cosmetic.Slot.Torso");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Legs, "OB.Cosmetic.Slot.Legs");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Feet, "OB.Cosmetic.Slot.Feet");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Outerwear, "OB.Cosmetic.Slot.Outerwear");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Lanyard, "OB.Cosmetic.Slot.Lanyard");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Badge, "OB.Cosmetic.Slot.Badge");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_Backpack, "OB.Cosmetic.Slot.Backpack");
	UE_DEFINE_GAMEPLAY_TAG(Cosmetic_Slot_WeaponSkin, "OB.Cosmetic.Slot.WeaponSkin");

	// ----------------------------------------------------------------------
	//  Utility helpers
	// ----------------------------------------------------------------------
	FGameplayTag TeamTagFromTeam(EOneBrahamTeam Team)
	{
		switch (Team)
		{
		case EOneBrahamTeam::Alpha:				return Team_Alpha;
		case EOneBrahamTeam::Bravo:				return Team_Bravo;
		case EOneBrahamTeam::Charlie:			return Team_Charlie;
		case EOneBrahamTeam::Delta:				return Team_Delta;
		case EOneBrahamTeam::InternalStaff:		return Team_InternalStaff;
		case EOneBrahamTeam::ExternalContractors: return Team_ExternalContractors;
		case EOneBrahamTeam::Spectator:			return Team_Spectator;
		default:								return FGameplayTag();
		}
	}

	FGameplayTag FloorTagFromIndex(int32 FloorIndex)
	{
		// Floor index 0 = plaza/undercroft, 1 = lobby, 2 = mezzanine,
		// 3..8 = open-plan offices, 9..11 = server/comms, 12..14 = executive,
		// 15 = canteen + Skydeck.
		if (FloorIndex <= 0)			{ return Audio_State_Floor_Exterior; }
		if (FloorIndex == 1)			{ return Audio_State_Floor_Lobby; }
		if (FloorIndex == 2)			{ return Audio_State_Floor_Mezzanine; }
		if (FloorIndex <= 8)			{ return Audio_State_Floor_Offices; }
		if (FloorIndex <= 11)			{ return Audio_State_Floor_Server; }
		if (FloorIndex <= 14)			{ return Audio_State_Floor_Executive; }
		if (FloorIndex == 15)			{ return Audio_State_Floor_Canteen; }
		return Audio_State_Floor_Skydeck;
	}

	bool IsFireDrillTeam(EOneBrahamTeam Team)
	{
		return Team == EOneBrahamTeam::InternalStaff || Team == EOneBrahamTeam::ExternalContractors;
	}

	bool AreTeamsFriendly(EOneBrahamTeam A, EOneBrahamTeam B, EOneBrahamMatchMode Mode)
	{
		if (A == EOneBrahamTeam::None || B == EOneBrahamTeam::None)
		{
			return false;
		}
		if (A == EOneBrahamTeam::Spectator || B == EOneBrahamTeam::Spectator)
		{
			return false;
		}

		if (Mode == EOneBrahamMatchMode::FireDrill)
		{
			// Internal Staff vs External Contractors. Anything that is not a
			// FIRE DRILL faction counts as hostile (bots, spectators, QA pawns).
			return A == B;
		}

		// ASCENT / NIGHT SHIFT: squads are the unit of friendship.
		return A == B;
	}
}
