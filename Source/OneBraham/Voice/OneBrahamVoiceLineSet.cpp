// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceTypes.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Sound/SoundBase.h"

namespace
{
	FOneBrahamVoiceLine Line(FName Id, const TCHAR* Subtitle, EOneBrahamVoicePriority Priority,
		float CooldownSeconds = 8.f, float Chance = 1.f)
	{
		FOneBrahamVoiceLine Result;
		Result.LineId = Id;
		Result.Subtitle = FText::FromString(Subtitle);
		Result.Priority = Priority;
		Result.CooldownSeconds = CooldownSeconds;
		Result.Chance = Chance;
		return Result;
	}

	/**
	 * A line recorded for one archetype.
	 *
	 * The same moment as the generic row, in that archetype's own words (Docs/14
	 * section 4): its own row, its own Sound and a filter naming the voice it
	 * was recorded in. Eleven lines are recorded six times each (Docs/14, section
	 * 4); these rows are where those recordings live, and PickLine prefers them
	 * over the generic one so the money that was spent on them is audible.
	 *
	 * The filter is a gameplay tag constant rather than a string: a typo here
	 * would mean the variant silently never plays, which is the same class of
	 * failure as an invented line id.
	 */
	FOneBrahamVoiceLine LineFor(FName Id, const FGameplayTag& Archetype, const TCHAR* Subtitle,
		EOneBrahamVoicePriority Priority, float CooldownSeconds = 8.f, float Chance = 1.f)
	{
		FOneBrahamVoiceLine Result = Line(Id, Subtitle, Priority, CooldownSeconds, Chance);
		Result.ArchetypeFilter.Add(Archetype);
		return Result;
	}
}

void UOneBrahamVoiceLineSet::EnsureCoreLines()
{
	// -------------------------------------------------------------------
	//  CORE BEHAVIOUR LINES
	//
	//  The full script - every variant, every archetype, with delivery
	//  notes - is Docs/14_Voice_Script.md. This table is the machine-
	//  readable contract: every LineId the gameplay code can request exists
	//  here, so a missing recorded asset degrades to a subtitle rather than
	//  a silent bug.
	//
	//  TONE: dry, tired, British, understated. Nobody shouts "LET'S GO!",
	//  nobody makes an office joke, and nobody ever says "hostiles".
	// -------------------------------------------------------------------
	static const FOneBrahamVoiceLine Core[] =
	{
		// --- Downed / revive -------------------------------------------------
		Line(TEXT("V_DOWNED"),						TEXT("I'm down. I'm down."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_DOWNED_CALL_HELP"),			TEXT("Down, on the floor - someone get me up."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_DOWNED_QUIP"),					TEXT("This is not how I expected my Tuesday to go."), EOneBrahamVoicePriority::Flavour, 20.f, 0.5f),
		Line(TEXT("V_DOWNED_FINAL"),				TEXT("That's me. Go on without me."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_REVIVING"),					TEXT("Hold still, I've got you."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_REVIVED"),						TEXT("Up. Thank you. Let's move."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_REVIVE_BLOCKED"),				TEXT("I can't get to you - you're in the open."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_EXECUTE_WARNING"),				TEXT("He's coming to finish it - he's coming."), EOneBrahamVoicePriority::Critical, 3.f),

		// --- Contact / combat -------------------------------------------------
		Line(TEXT("V_CONTACT_ENEMY"),				TEXT("Contact. One."), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_CONTACT_MULTIPLE"),			TEXT("Contact, two or more. Left of the lift."), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_ENEMY_SPOTTED"),				TEXT("Got eyes on him."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_ENEMY_DOWN"),					TEXT("He's down."), EOneBrahamVoicePriority::Tactical, 2.f),
		Line(TEXT("V_ENEMY_FLANKING"),				TEXT("Watch the far side - he's gone round."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_ENEMY_RELOADING"),				TEXT("He's reloading. Push him."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_ENEMY_SNIPER"),				TEXT("Marksman on the far window. Keep your head down."), EOneBrahamVoicePriority::Critical, 5.f),
		Line(TEXT("V_TAKING_FIRE"),					TEXT("Taking fire - I'm hit, I'm hit."), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_SUPPRESSED"),					TEXT("I can't move. He's got the door."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_RETREATING"),					TEXT("Falling back - this isn't a fight I win."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_CLEAR"),						TEXT("Clear."), EOneBrahamVoicePriority::Tactical, 2.f),
		Line(TEXT("V_ROOM_CLEAR"),					TEXT("Office is clear. Moving on."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_LAST_MAG"),					TEXT("Last magazine here."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_KILL_CONFIRMED"),				TEXT("He's not getting up from that."), EOneBrahamVoicePriority::Flavour, 10.f, 0.4f),

		// --- Weapons and utility ---------------------------------------------
		Line(TEXT("V_RELOADING"),					TEXT("Reloading - cover me."), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_RELOAD_URGENT"),				TEXT("Reloading, reloading - I'm out."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_LOW_AMMO"),					TEXT("Running low here."), EOneBrahamVoicePriority::Tactical, 8.f),
		Line(TEXT("V_DRY_FIRE"),					TEXT("Empty."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_WEAPON_SWITCH"),				TEXT("Switching to the pistol."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_GRENADE_OUT"),					TEXT("Frag out."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_GRENADE_INCOMING"),			TEXT("Grenade - get back, get back."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_FRAG_HIT"),					TEXT("That one landed."), EOneBrahamVoicePriority::Flavour, 8.f, 0.5f),
		Line(TEXT("V_FLASH_OUT"),					TEXT("Flash out."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_FLASHED"),						TEXT("I'm blind - I'm blind, don't move me."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_SMOKE_OUT"),					TEXT("Smoke going down."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_BREACH_PLANTED"),				TEXT("Charge set - stand clear."), EOneBrahamVoicePriority::Critical, 2.f),
		Line(TEXT("V_BREACH_FIRE_IN_HOLE"),			TEXT("Going in on the bang. Two, one -"), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_MEDKIT"),						TEXT("Patching up. Give me a second."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_HEAL_REQUEST"),				TEXT("I need a kit if anyone's carrying."), EOneBrahamVoicePriority::Tactical, 8.f),
		Line(TEXT("V_HEALING"),						TEXT("Hold still - this'll sting for a second."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_HEAL_COMPLETE"),				TEXT("Good as new. Roughly."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_NO_MEDKIT"),					TEXT("I've nothing left to give you."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_MELEE_HIT"),					TEXT("Get off me."), EOneBrahamVoicePriority::Flavour, 6.f, 0.6f),

		// --- Keycards, doors and the override --------------------------------
		Line(TEXT("V_KEYCARD_FOUND"),				TEXT("Got a card. Floor nine, looking at the colour."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_KEYCARD_NEEDED"),				TEXT("We need a higher card for this one."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_KEYCARD_USED"),				TEXT("Card's good. Door's open."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_DOOR_LOCKED"),					TEXT("Locked. No card, no entry."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_OVERRIDE_START"),				TEXT("Starting the override - somebody watch the corridor."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_OVERRIDE_IN_PROGRESS"),		TEXT("Still going. Ten more seconds."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_OVERRIDE_DENIED"),				TEXT("It's refused. Card's not high enough."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_OVERRIDE_COMPLETE"),			TEXT("Override's done. Floors opening up."), EOneBrahamVoicePriority::Critical, 3.f),
		// The building's own announcement, not a character's: fifteen seconds before
		// each lockdown stage, delivered 2D through the PA. The wording is the
		// building's, which is to say the wording of a fire drill - the horror of
		// this game is that the tower locks down politely and exactly as designed,
		// and a line that broke register to sound dramatic would lose that.
		Line(TEXT("V_LOCKDOWN_WARNING"),			TEXT("Attention: the lower floors are closing. Please move up."), EOneBrahamVoicePriority::Critical, 5.f),
		Line(TEXT("V_LOCKDOWN_START"),				TEXT("That's the alarms - they're shutting the floors down."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_SHUTTERS_CLOSED"),				TEXT("Shutters are down behind us. No way back."), EOneBrahamVoicePriority::Critical, 5.f),
		Line(TEXT("V_FLOOR_SECURED"),				TEXT("This floor's sealed. We're committed."), EOneBrahamVoicePriority::Tactical, 6.f),
		Line(TEXT("V_UP_WE_GO"),					TEXT("Up. Fifteenth."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_SECURITY_PURGE"),				TEXT("They're venting the stairwell - don't be in it."), EOneBrahamVoicePriority::Critical, 5.f),

		// --- Extraction --------------------------------------------------------
		Line(TEXT("V_EXTRACTION_CALLED"),			TEXT("Helicopter's inbound on the roof - winch is live."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_EXTRACTION_HOLD"),				TEXT("Hold the deck. Thirty seconds, that's all it is."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_EXTRACTION_LEAVING"),			TEXT("That's us. Somebody else's problem now."), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_EXTRACTION_LOST"),				TEXT("They're on the winch - we lost it."), EOneBrahamVoicePriority::Critical, 3.f),
		Line(TEXT("V_LAST_SQUAD_WARNING"),			TEXT("One squad left up here besides us."), EOneBrahamVoicePriority::Critical, 6.f),

		// --- Teamwork ----------------------------------------------------------
		Line(TEXT("V_PING_ENEMY"),					TEXT("Enemy, marked."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_PING_ITEM"),					TEXT("Kit here - worth taking."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_PING_MOVE"),					TEXT("Move up."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_COVER_ME"),					TEXT("Cover me, I'm moving."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_COVERING"),					TEXT("I've got you covered."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_GO_LEFT"),						TEXT("I'll take the left."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_GO_RIGHT"),					TEXT("Going right. Don't follow too close."), EOneBrahamVoicePriority::Tactical, 4.f),
		Line(TEXT("V_HOLD_POSITION"),				TEXT("Hold here. Let them come to us."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_PUSH_NOW"),					TEXT("Now. Go now."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_FALL_BACK"),					TEXT("Back. Back to the lifts."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_QUIET"),						TEXT("Quiet - I heard something in the ceiling."), EOneBrahamVoicePriority::Tactical, 10.f),
		Line(TEXT("V_WAIT"),						TEXT("Wait. Not yet."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_ACK"),							TEXT("Understood."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_ACK_NEGATIVE"),				TEXT("No. Not that way."), EOneBrahamVoicePriority::Tactical, 3.f),
		Line(TEXT("V_GOOD_KILL"),					TEXT("Nice one."), EOneBrahamVoicePriority::Flavour, 12.f, 0.35f),
		Line(TEXT("V_BAD_LUCK"),					TEXT("Hard lines. He was waiting for you."), EOneBrahamVoicePriority::Flavour, 12.f, 0.35f),
		// Spoken by a bystander, not by the downed player: the wheel's "man down"
		// segment needs somebody to say it, and before this line existed the only
		// way to tell a squadmate was to hope they noticed the panel.
		Line(TEXT("V_MAN_DOWN"),					TEXT("Somebody's down - nearest one, get to him."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_SQUAD_WIPED"),					TEXT("That's the squad. It's on me now."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_LAST_MAN_STANDING"),			TEXT("Last one up. Fine."), EOneBrahamVoicePriority::Critical, 4.f),

		// --- The building's machinery: lifts, loot, glass, the helicopter -----
		Line(TEXT("V_LIFT_CALLED"),					TEXT("Lift's coming. Everyone on this floor heard that too."), EOneBrahamVoicePriority::Tactical, 10.f),
		Line(TEXT("V_LIFT_DOORS"),					TEXT("Mind the doors - they don't stop for anyone."), EOneBrahamVoicePriority::Critical, 6.f),
		Line(TEXT("V_LIFTS_DOWN"),					TEXT("Lifts are dead. Stairs."), EOneBrahamVoicePriority::Tactical, 12.f),
		Line(TEXT("V_WEAPON_FOUND"),				TEXT("Better than what I had. Taking it."), EOneBrahamVoicePriority::Tactical, 10.f),
		Line(TEXT("V_AMMO_FOUND"),					TEXT("Rounds here. Take what you need."), EOneBrahamVoicePriority::Tactical, 10.f),
		Line(TEXT("V_GLASS_GONE"),					TEXT("The glass is out on the deck - watch the edge."), EOneBrahamVoicePriority::Critical, 8.f),
		Line(TEXT("V_HELI_SPOTTED"),				TEXT("There - over the City. That's the helicopter."), EOneBrahamVoicePriority::Tactical, 30.f),
		Line(TEXT("V_PROP_SHORT"),					TEXT("That's shorting out - stay off it."), EOneBrahamVoicePriority::Tactical, 10.f),

		// --- Ambience / corporate realism -------------------------------------
		Line(TEXT("V_AMBIENT_ELEVATOR"),			TEXT("Nineteen years in this building and the lifts still sound like that."), EOneBrahamVoicePriority::Ambient, 60.f, 0.25f),
		Line(TEXT("V_AMBIENT_CANTEEN"),				TEXT("They've still got the sandwich board out. Optimistic."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_SERVER_ROOM"),			TEXT("It's warm in here. Always warm in here."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_RAIN"),				TEXT("Rain's coming in sideways off the plaza."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_STAIRS"),				TEXT("Fifteen floors. Nobody thought to fix the lifts."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_EXEC"),				TEXT("The executive floor always smells like new carpet."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_TIRED"),				TEXT("I've been on shift since six."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_COFFEE"),				TEXT("Somebody's left a coffee on the third floor. It's gone cold."), EOneBrahamVoicePriority::Ambient, 60.f, 0.15f),
		Line(TEXT("V_AMBIENT_MEETING"),				TEXT("Meeting room's booked till eight. Of course it is."), EOneBrahamVoicePriority::Ambient, 60.f, 0.15f),
		Line(TEXT("V_AMBIENT_SKYDECK"),				TEXT("You can see the whole of the City from up here. Shame about the company."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		Line(TEXT("V_AMBIENT_PLAZA"),				TEXT("Plaza's quiet for this time. That's never a good sign."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),

		// --- Match framing -----------------------------------------------------
		Line(TEXT("V_MATCH_START"),					TEXT("Right. In we go."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_MATCH_HALFWAY"),				TEXT("Halfway. Everyone's still standing."), EOneBrahamVoicePriority::Flavour, 30.f, 0.5f),
		Line(TEXT("V_VICTORY"),						TEXT("That's us on the deck. Somebody's going to have to explain this in the morning."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_DEFEAT"),						TEXT("We're done. Out."), EOneBrahamVoicePriority::Tactical, 5.f),
		Line(TEXT("V_SPECTATE_COMMENT"),			TEXT("Watch the corner - he always takes the corner."), EOneBrahamVoicePriority::Flavour, 20.f, 0.4f),
		Line(TEXT("V_STEP_GRATING"),				TEXT("Careful - grating."), EOneBrahamVoicePriority::Ambient, 30.f, 0.3f),
		Line(TEXT("V_STEP_GLASS"),					TEXT("Glass. Go round it."), EOneBrahamVoicePriority::Ambient, 30.f, 0.3f)
	};


	// ===================================================================
	//  ARCHETYPE VARIANT RECORDINGS (66)
	//
	//  Eleven lines, six performances each, and six sets of words: the analyst
	//  counts, the graduate apologises, Finance says as little as possible, IT
	//  names the system, Facilities gives instructions and the consultant asks.
	//  The core row is the fallback a voice with no recording of its own says.
	//  PickLine prefers the speaker's own row.
	//
	//  The delivery notes for each of these are the table in Docs/14, section 4.
	//  If you are looking for the recording sheet, that is it: this block is the
	//  machine-readable half and that table is the brief the actor reads.
	// ===================================================================
	static const FOneBrahamVoiceLine Variants[] =
	{
		// --- V_DOWNED --------------------------------------------------
		LineFor(TEXT("V_DOWNED"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("I'm down. Two hits, left side. I'm down."), EOneBrahamVoicePriority::Critical, 2.f),
		LineFor(TEXT("V_DOWNED"), OneBrahamTags::Character_Archetype_Graduate, TEXT("I'm down - sorry - I'm down."), EOneBrahamVoicePriority::Critical, 2.f),
		LineFor(TEXT("V_DOWNED"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Down."), EOneBrahamVoicePriority::Critical, 2.f),
		LineFor(TEXT("V_DOWNED"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("I'm down. Don't come the obvious way."), EOneBrahamVoicePriority::Critical, 2.f),
		LineFor(TEXT("V_DOWNED"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("I'm down. Stay where you are, I'll talk you in."), EOneBrahamVoicePriority::Critical, 2.f),
		LineFor(TEXT("V_DOWNED"), OneBrahamTags::Character_Archetype_Consultant, TEXT("I'm down. Can somebody get to me?"), EOneBrahamVoicePriority::Critical, 2.f),

		// --- V_CONTACT_ENEMY --------------------------------------------------
		LineFor(TEXT("V_CONTACT_ENEMY"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Contact. One. Twenty metres, by the lifts."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_ENEMY"), OneBrahamTags::Character_Archetype_Graduate, TEXT("There's - one. There's one. Contact."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_ENEMY"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("One. Ahead."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_ENEMY"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Contact. One, and he knows we're here."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_ENEMY"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("One of them. Everyone stop."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_ENEMY"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Contact - one. Are you seeing him?"), EOneBrahamVoicePriority::Critical, 3.f),

		// --- V_RELOADING --------------------------------------------------
		LineFor(TEXT("V_RELOADING"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Reloading. Four seconds."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_RELOADING"), OneBrahamTags::Character_Archetype_Graduate, TEXT("Reloading - sorry - reloading."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_RELOADING"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Changing magazine."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_RELOADING"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Reloading. Watch the door for me."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_RELOADING"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("Reloading. Hold that corner."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_RELOADING"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Reloading - can you take this one?"), EOneBrahamVoicePriority::Critical, 3.f),

		// --- V_CONTACT_MULTIPLE --------------------------------------------------
		LineFor(TEXT("V_CONTACT_MULTIPLE"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Two - no, three. Left of the lift."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_MULTIPLE"), OneBrahamTags::Character_Archetype_Graduate, TEXT("There's more than one. There's definitely more than one."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_MULTIPLE"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Several. Left."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_MULTIPLE"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Two or more, coming off the stairwell."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_MULTIPLE"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("More than one. Everyone back from the glass."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_CONTACT_MULTIPLE"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Two of them at least. Shall we pull back?"), EOneBrahamVoicePriority::Critical, 3.f),

		// --- V_LAST_MAG --------------------------------------------------
		LineFor(TEXT("V_LAST_MAG"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Last magazine. Thirty rounds, then nothing."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LAST_MAG"), OneBrahamTags::Character_Archetype_Graduate, TEXT("This is my last one. Is that bad?"), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LAST_MAG"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Last magazine."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LAST_MAG"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Last mag. Don't make me waste it on doors."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LAST_MAG"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("On my last one. Anyone carrying?"), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LAST_MAG"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Last magazine here - anyone got spare?"), EOneBrahamVoicePriority::Critical, 4.f),

		// --- V_KEYCARD_FOUND --------------------------------------------------
		LineFor(TEXT("V_KEYCARD_FOUND"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Card. Blue stripe. That's floor nine."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_KEYCARD_FOUND"), OneBrahamTags::Character_Archetype_Graduate, TEXT("I've got a card! Sorry. I've got a card."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_KEYCARD_FOUND"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Card. Taking it."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_KEYCARD_FOUND"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Got a card. Same badge system as the server floor."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_KEYCARD_FOUND"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("That's a staff card. I know whose it is."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_KEYCARD_FOUND"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Found a card. Who's going up first?"), EOneBrahamVoicePriority::Tactical, 5.f),

		// --- V_OVERRIDE_COMPLETE --------------------------------------------------
		LineFor(TEXT("V_OVERRIDE_COMPLETE"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Override done. Twelve seconds, as advertised."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_OVERRIDE_COMPLETE"), OneBrahamTags::Character_Archetype_Graduate, TEXT("It worked. It actually worked."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_OVERRIDE_COMPLETE"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Done. Move."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_OVERRIDE_COMPLETE"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Override's through. It'll have logged that."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_OVERRIDE_COMPLETE"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("Doors are released. Everyone through, quickly."), EOneBrahamVoicePriority::Critical, 3.f),
		LineFor(TEXT("V_OVERRIDE_COMPLETE"), OneBrahamTags::Character_Archetype_Consultant, TEXT("That's open. Shall we?"), EOneBrahamVoicePriority::Critical, 3.f),

		// --- V_EXTRACTION_CALLED --------------------------------------------------
		LineFor(TEXT("V_EXTRACTION_CALLED"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Winch is called. Forty-five seconds out."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_EXTRACTION_CALLED"), OneBrahamTags::Character_Archetype_Graduate, TEXT("I called it - it's coming - it's actually coming."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_EXTRACTION_CALLED"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Helicopter's called. Hold the deck."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_EXTRACTION_CALLED"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Winch is live. Every screen in the building just told them."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_EXTRACTION_CALLED"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("Called it in. Everyone to the pad, now."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_EXTRACTION_CALLED"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Helicopter's on its way. Where do you want me?"), EOneBrahamVoicePriority::Critical, 4.f),

		// --- V_LOCKDOWN_START --------------------------------------------------
		LineFor(TEXT("V_LOCKDOWN_START"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Lockdown. Stage two, lower floors first."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LOCKDOWN_START"), OneBrahamTags::Character_Archetype_Graduate, TEXT("Are they - they're shutting the floors?"), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LOCKDOWN_START"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Lockdown. Keep climbing."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LOCKDOWN_START"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("That's the building management system. It's locking us out."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LOCKDOWN_START"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("That's the lockdown. Nobody goes back down."), EOneBrahamVoicePriority::Critical, 4.f),
		LineFor(TEXT("V_LOCKDOWN_START"), OneBrahamTags::Character_Archetype_Consultant, TEXT("They've started the lockdown. Upwards, I think."), EOneBrahamVoicePriority::Critical, 4.f),

		// --- V_VICTORY --------------------------------------------------
		LineFor(TEXT("V_VICTORY"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("On the winch. That's - that's all of us."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_VICTORY"), OneBrahamTags::Character_Archetype_Graduate, TEXT("We made it. We actually made it."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_VICTORY"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Done."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_VICTORY"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("We're off the building. Someone else can file the incident report."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_VICTORY"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("Everyone's on. Everyone. Right."), EOneBrahamVoicePriority::Tactical, 5.f),
		LineFor(TEXT("V_VICTORY"), OneBrahamTags::Character_Archetype_Consultant, TEXT("Well. That went better than expected."), EOneBrahamVoicePriority::Tactical, 5.f),

		// --- V_AMBIENT_SKYDECK --------------------------------------------------
		LineFor(TEXT("V_AMBIENT_SKYDECK"), OneBrahamTags::Character_Archetype_BusinessAnalyst, TEXT("Fifty-six metres. I always wondered."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		LineFor(TEXT("V_AMBIENT_SKYDECK"), OneBrahamTags::Character_Archetype_Graduate, TEXT("I've never been up here. Is it always this windy?"), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		LineFor(TEXT("V_AMBIENT_SKYDECK"), OneBrahamTags::Character_Archetype_FinanceBroker, TEXT("Good view. Shame about everything else."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		LineFor(TEXT("V_AMBIENT_SKYDECK"), OneBrahamTags::Character_Archetype_ITOperations, TEXT("Comms mast's still up. That's something."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		LineFor(TEXT("V_AMBIENT_SKYDECK"), OneBrahamTags::Character_Archetype_FacilitiesSecurity, TEXT("Nobody's meant to be up here without a harness."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
		LineFor(TEXT("V_AMBIENT_SKYDECK"), OneBrahamTags::Character_Archetype_Consultant, TEXT("You can see my old office from here."), EOneBrahamVoicePriority::Ambient, 60.f, 0.2f),
	};

	// De-duplication is on the id AND the archetype filter, not the id alone. An
	// archetype variant shares its id with the core line by design, so keying on
	// the id would have dropped all 66 of them on the floor - and the failure
	// would have been invisible, because the generic recording plays instead.
	auto AlreadyPresent = [this](const FOneBrahamVoiceLine& Candidate)
	{
		return Lines.ContainsByPredicate([&Candidate](const FOneBrahamVoiceLine& Existing)
		{
			if (Existing.LineId != Candidate.LineId)
			{
				return false;
			}

			// Same id: the two rows are the same line only if they are recorded
			// for the same voices.
			if (Existing.ArchetypeFilter.Num() != Candidate.ArchetypeFilter.Num())
			{
				return false;
			}

			for (const FGameplayTag& Tag : Candidate.ArchetypeFilter)
			{
				if (!Existing.ArchetypeFilter.Contains(Tag))
				{
					return false;
				}
			}

			return true;
		});
	};

	int32 Added = 0;

	for (const FOneBrahamVoiceLine& LineEntry : Core)
	{
		if (!AlreadyPresent(LineEntry))
		{
			Lines.Add(LineEntry);
			++Added;
		}
	}

	int32 VariantsAdded = 0;

	for (const FOneBrahamVoiceLine& VariantEntry : Variants)
	{
		if (!AlreadyPresent(VariantEntry))
		{
			Lines.Add(VariantEntry);
			++VariantsAdded;
		}
	}

	UE_LOG(LogOneBrahamVoice, Log,
		TEXT("[OB.VOICE] Core line set ensured: %d core and %d variant rows added, %d total. "
			 "%d lines are recorded six times each."),
		Added, VariantsAdded, Lines.Num(), static_cast<int32>(UE_ARRAY_COUNT(Variants)) / 6);
}

void UOneBrahamVoiceLineSet::FindLines(FName LineId, TArray<const FOneBrahamVoiceLine*>& OutLines) const
{
	OutLines.Reset();

	for (const FOneBrahamVoiceLine& LineEntry : Lines)
	{
		if (LineEntry.LineId == LineId)
		{
			OutLines.Add(&LineEntry);
		}
	}
}

const FOneBrahamVoiceLine* UOneBrahamVoiceLineSet::PickLine(FName LineId, FGameplayTag Archetype,
	FRandomStream& Stream) const
{
	TArray<const FOneBrahamVoiceLine*> Candidates;
	FindLines(LineId, Candidates);

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	// Two tiers, in this order, and the order is the whole point:
	//
	//   1. lines recorded for this archetype, if any exist;
	//   2. otherwise the generic line, whose recording is used unpitched.
	//
	// The obvious implementation - one pool of everything that is not filtered
	// away - is wrong in a way that is invisible until somebody counts the
	// recordings. Eleven lines are recorded six times each (Docs/14, section 4),
	// so with a single pool a Business Analyst speaking V_DOWNED would have seven
	// candidates: their own recording and the six-times-generic one, picked at
	// random. The archetype-specific performance - the reason those 66 recordings
	// exist and cost money - would play one time in seven.
	TArray<const FOneBrahamVoiceLine*> ArchetypeLines;
	ArchetypeLines.Reserve(Candidates.Num());

	TArray<const FOneBrahamVoiceLine*> GenericLines;
	GenericLines.Reserve(Candidates.Num());

	for (const FOneBrahamVoiceLine* Candidate : Candidates)
	{
		if (Candidate->ArchetypeFilter.Num() == 0)
		{
			GenericLines.Add(Candidate);
		}
		else if (Archetype.IsValid() && Candidate->ArchetypeFilter.Contains(Archetype))
		{
			ArchetypeLines.Add(Candidate);
		}
	}

	// A line written for one voice only leaves every other archetype silent
	// rather than falling back to a recording that does not exist: that is what
	// the filter is for.
	if (GenericLines.Num() == 0 && ArchetypeLines.Num() == 0)
	{
		return nullptr;
	}

	const TArray<const FOneBrahamVoiceLine*>& Pool =
		ArchetypeLines.Num() > 0 ? ArchetypeLines : GenericLines;

	return Pool[Stream.RandRange(0, Pool.Num() - 1)];
}

bool FOneBrahamVoiceBudget::TryConsume(float Now, int32 FrameBytes, int32 MaxFramesPerSecond, int32 MaxBytesPerSecond)
{
	// Sliding one-second window. Cheap and exact enough: frames arrive at a fixed
	// cadence, so a simple expiry sweep behaves like a leaky bucket.
	for (int32 Index = RecentFrameTimes.Num() - 1; Index >= 0; --Index)
	{
		if (Now - RecentFrameTimes[Index] > 1.f)
		{
			RecentFrameTimes.RemoveAt(Index, 1, EAllowShrinking::No);
			RecentFrameSizes.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}

	int32 BytesInWindow = 0;
	for (const int32 Size : RecentFrameSizes)
	{
		BytesInWindow += Size;
	}

	if (RecentFrameTimes.Num() >= MaxFramesPerSecond || (BytesInWindow + FrameBytes) > MaxBytesPerSecond)
	{
		++DroppedFrames;
		return false;
	}

	RecentFrameTimes.Add(Now);
	RecentFrameSizes.Add(FrameBytes);
	return true;
}

void FOneBrahamVoiceBudget::Reset()
{
	RecentFrameTimes.Reset();
	RecentFrameSizes.Reset();
	DroppedFrames = 0;
}
