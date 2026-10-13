// Copyright (c) One Braham. All Rights Reserved.

#include "Voice/OneBrahamVoiceTypes.h"

#include "OneBraham.h"
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
		Line(TEXT("V_SQUAD_WIPED"),					TEXT("That's the squad. It's on me now."), EOneBrahamVoicePriority::Critical, 4.f),
		Line(TEXT("V_LAST_MAN_STANDING"),			TEXT("Last one up. Fine."), EOneBrahamVoicePriority::Critical, 4.f),

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

	int32 Added = 0;
	for (const FOneBrahamVoiceLine& LineEntry : Core)
	{
		const bool bExists = Lines.ContainsByPredicate([&LineEntry](const FOneBrahamVoiceLine& Existing)
		{
			return Existing.LineId == LineEntry.LineId;
		});

		if (!bExists)
		{
			Lines.Add(LineEntry);
			++Added;
		}
	}

	UE_LOG(LogOneBrahamVoice, Log, TEXT("[OB.VOICE] Core line set ensured: %d added, %d total."), Added, Lines.Num());
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

	// Archetype filter first: a Facilities & Security character and a Graduate
	// intern do not deliver the same line the same way, and some lines are
	// written for one voice only.
	TArray<const FOneBrahamVoiceLine*> Filtered;
	Filtered.Reserve(Candidates.Num());

	for (const FOneBrahamVoiceLine* Candidate : Candidates)
	{
		if (Candidate->ArchetypeFilter.Num() == 0 || Candidate->ArchetypeFilter.Contains(Archetype))
		{
			Filtered.Add(Candidate);
		}
	}

	if (Filtered.Num() == 0)
	{
		return nullptr;
	}

	return Filtered[Stream.RandRange(0, Filtered.Num() - 1)];
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
