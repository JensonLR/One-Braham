// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameplayTags.h
// ---------------------------------------------------------------------------
// Native GameplayTags. Declaring them natively means the ASCENT match flow has
// zero string lookups at runtime and typos fail at compile time. The same tags
// are mirrored in Config/DefaultGame.ini so designers can use them in Data
// Assets before the first compile.

#pragma once

#include "NativeGameplayTags.h"
#include "Core/OneBrahamTypes.h"

namespace OneBrahamTags
{
	// --- Character --------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_State_Alive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_State_Downed);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_State_Dead);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_State_Spectating);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_State_Extracting);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_State_Extracted);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_Archetype_BusinessAnalyst);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_Archetype_Graduate);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_Archetype_FinanceBroker);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_Archetype_ITOperations);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_Archetype_FacilitiesSecurity);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Character_Archetype_Consultant);

	// --- Teams ------------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Alpha);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Bravo);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Charlie);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Delta);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_InternalStaff);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_ExternalContractors);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Spectator);

	// --- Access -----------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Tier_Public);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Tier_Staff);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Tier_Secure);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Tier_Executive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Tier_Skydeck);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Tier_Override);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_Lobby);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_Mezzanine);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_Offices);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_ServerFloor);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_Executive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_Canteen);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Zone_Skydeck);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Method_Keycard);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Method_Override);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Method_Breach);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Access_Method_Escalation);

	// --- Interaction ------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Door);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Door_Secure);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Door_Breachable);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Terminal);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Lift);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Keycard);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Weapon);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Ammo);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Utility);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Medkit);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Revive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Execute);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Body);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Extraction);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_HoldToUnlock);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_TapToUse);

	// --- Weapons ----------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Pistol);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_CompactPistol);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_SuppressedPistol);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_SMG);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_PDW);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_PumpShotgun);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_SemiAutoShotgun);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Carbine556);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_CompactAssaultRifle);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_MarksmanRifle);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Category_Utility);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Fire_Single);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Fire_Burst);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Fire_Auto);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Fire_Pump);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Fire_Bolt);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Utility_Flashbang);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Utility_Smoke);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Utility_Frag);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Utility_BreachCharge);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Utility_Medkit);

	// --- Match ------------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Mode_Ascent);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Mode_FireDrill);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Mode_NightShift);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Phase_Warmup);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Phase_Active);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Phase_Extraction);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Phase_Complete);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Lockdown_Normal);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Lockdown_PreLockdown);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Lockdown_Lockdown);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_Lockdown_Purge);

	// --- Extraction -------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Extraction_Inactive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Extraction_Broadcast);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Extraction_Arriving);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Extraction_Holding);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Extraction_Complete);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Extraction_Failed);

	// --- Voice ------------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_RoundStart);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Movement);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Contact);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Combat);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Reload);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_LowAmmo);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Down);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Death);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Revive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Access);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Extraction);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Win);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Lose);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Reactive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Category_Banter);

	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Channel_Proximity);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Channel_Squad);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Channel_Team);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Voice_Channel_Radio);

	// --- Audio ------------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Rain_Light);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Rain_Heavy);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Exterior);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Lobby);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Mezzanine);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Offices);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Server);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Executive);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Canteen);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio_State_Floor_Skydeck);

	// --- Cosmetics --------------------------------------------------------
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Head);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Torso);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Legs);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Feet);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Outerwear);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Lanyard);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Badge);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_Backpack);
	ONEBRAHAM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cosmetic_Slot_WeaponSkin);

	// --- Utility helpers --------------------------------------------------
	/** Returns team tag for a team enum - used by the scoreboard and voice routing. */
	ONEBRAHAM_API FGameplayTag TeamTagFromTeam(EOneBrahamTeam Team);

	/** Returns the floor tag for the given floor index (0 plaza .. 15 skydeck). */
	ONEBRAHAM_API FGameplayTag FloorTagFromIndex(int32 FloorIndex);

	/** True for the two FIRE DRILL factions, false for ASCENT squads. */
	ONEBRAHAM_API bool IsFireDrillTeam(EOneBrahamTeam Team);

	/** Friend/foe test used by damage, outlines and voice routing. */
	ONEBRAHAM_API bool AreTeamsFriendly(EOneBrahamTeam A, EOneBrahamTeam B, EOneBrahamMatchMode Mode);
}
