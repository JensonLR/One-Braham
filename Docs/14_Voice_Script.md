# ONE BRAHAM - voice script

**Recording script, casting sheet and delivery direction. 95 core lines, 66 archetype variants, 161 rows total.**

The machine-readable contract is `Source/OneBraham/Voice/OneBrahamVoiceLineSet.cpp`: every `V_*` id below exists there with its priority, cooldown and chance. This document is what a voice director and six actors work from.

---

## 1. The tone rule

Six ordinary people with office jobs, on the worst morning of their working lives, in a building they know. Nobody in this script is a soldier. Nobody says "hostiles", "tango", "alpha team", "let's do this" or anything else that belongs in an American action film. The humour, where it exists, is the humour of competent people being extremely calm about something absurd - and it lives in the delivery, not in the words.

Two rules that override everything else:

1. **Information first.** Every combat line tells a teammate something they did not know: how many, which direction, what the enemy is doing. A line that only expresses emotion is cut.
2. **Understated, never flat.** These people are frightened. It shows in the pace and in the breathing, not in volume. Shouting is reserved for the four lines where shouting is the correct thing to do.

### Who is speaking

| Archetype | Who they are | Delivery |
| --- | --- | --- |
| **Business Analyst** | Floors 5-8. Measuring everything. Says numbers out loud because it helps. | Documents what is happening as it happens. Precise, level, slightly too fast. Under stress the numbers get more specific, not louder. |
| **Graduate / Intern** | Floors 3-4. Trying very hard not to be the one who panics. | A beat behind and covering it. Fastest talker of the six, and audibly relieved when someone else takes charge. |
| **Finance** | Floor 8. Unbothered. Has seen worse on a Tuesday. | Economy of words. Short sentences, no filler. Does not raise their voice because raising your voice is not a plan. |
| **IT / Operations** | Floor 9. Knows exactly what the building is doing and is annoyed about it. | Technically exact and tired of explaining it. The only one who names the system that just locked them in. |
| **Facilities / Security** | Floor 15 and everywhere. Responsible for everyone, paid for none of it. | Working. Practical instructions in the imperative, and the only one who acknowledges a teammate's death aloud. |
| **Consultant** | Floor 7, hot-desking. Here for six weeks. Noticeably better at this than expected. | Friendly, competent, slightly detached, and quietly the most useful person in the room. Asks questions instead of giving orders. |

Six actors, recorded separately, one session each. Aim for people who sound like they have a commute: London and the South East, ages 24-52, four men and two women, mixed accents from the city and its orbit. No received-pronunciation villain voice, no estuary parody.

### Technical

| | |
| --- | --- |
| Sample rate | 48 kHz, 24-bit, mono |
| Level | peak -3 dBFS, dialogue-normalised to -23 LUFS |
| Room | treated, close-mic'd, no reverb. The building's reverb is added by the game |
| Format delivered | one WAV per line per archetype |
| Named | `Content/OneBraham/Voice/Waves/<Archetype>/<LineId>.wav` |
| Breath | leave it in. Slight breaths between sentences are what make a take sound alive |
| Ad-libs | welcome, but only inside the intent. Send them as `<LineId>_alt<NN>.wav` |

Each line is under four seconds. If it is not, it is two lines.

---

## 2. Core lines

Priority and cooldown are the runtime's, not the actor's. They are here so the whole table can be reviewed in one place - and so nobody records a line that will almost never play.


### Downed / revive

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_DOWNED` | "I'm down. I'm down." | critical - ducks everything, plays immediately | 2s | always |
| `V_DOWNED_CALL_HELP` | "Down, on the floor - someone get me up." | critical - ducks everything, plays immediately | 2s | always |
| `V_DOWNED_QUIP` | "This is not how I expected my Tuesday to go." | flavour - only if nothing else is talking and never twice in a minute | 20s | 50% |
| `V_DOWNED_FINAL` | "That's me. Go on without me." | critical - ducks everything, plays immediately | 2s | always |
| `V_REVIVING` | "Hold still, I've got you." | tactical - plays at the next natural break | 4s | always |
| `V_REVIVED` | "Up. Thank you. Let's move." | tactical - plays at the next natural break | 3s | always |
| `V_REVIVE_BLOCKED` | "I can't get to you - you're in the open." | tactical - plays at the next natural break | 6s | always |
| `V_EXECUTE_WARNING` | "He's coming to finish it - he's coming." | critical - ducks everything, plays immediately | 3s | always |

### Contact / combat

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_CONTACT_ENEMY` | "Contact. One." | critical - ducks everything, plays immediately | 3s | always |
| `V_CONTACT_MULTIPLE` | "Contact, two or more. Left of the lift." | critical - ducks everything, plays immediately | 3s | always |
| `V_ENEMY_SPOTTED` | "Got eyes on him." | tactical - plays at the next natural break | 4s | always |
| `V_ENEMY_DOWN` | "He's down." | tactical - plays at the next natural break | 2s | always |
| `V_ENEMY_FLANKING` | "Watch the far side - he's gone round." | critical - ducks everything, plays immediately | 4s | always |
| `V_ENEMY_RELOADING` | "He's reloading. Push him." | critical - ducks everything, plays immediately | 4s | always |
| `V_ENEMY_SNIPER` | "Marksman on the far window. Keep your head down." | critical - ducks everything, plays immediately | 5s | always |
| `V_TAKING_FIRE` | "Taking fire - I'm hit, I'm hit." | critical - ducks everything, plays immediately | 3s | always |
| `V_SUPPRESSED` | "I can't move. He's got the door." | tactical - plays at the next natural break | 5s | always |
| `V_RETREATING` | "Falling back - this isn't a fight I win." | tactical - plays at the next natural break | 5s | always |
| `V_CLEAR` | "Clear." | tactical - plays at the next natural break | 2s | always |
| `V_ROOM_CLEAR` | "Office is clear. Moving on." | tactical - plays at the next natural break | 3s | always |
| `V_LAST_MAG` | "Last magazine here." | critical - ducks everything, plays immediately | 4s | always |
| `V_KILL_CONFIRMED` | "He's not getting up from that." | flavour - only if nothing else is talking and never twice in a minute | 10s | 40% |

### Weapons and utility

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_RELOADING` | "Reloading - cover me." | critical - ducks everything, plays immediately | 3s | always |
| `V_RELOAD_URGENT` | "Reloading, reloading - I'm out." | critical - ducks everything, plays immediately | 2s | always |
| `V_LOW_AMMO` | "Running low here." | tactical - plays at the next natural break | 8s | always |
| `V_DRY_FIRE` | "Empty." | critical - ducks everything, plays immediately | 2s | always |
| `V_WEAPON_SWITCH` | "Switching to the pistol." | tactical - plays at the next natural break | 6s | always |
| `V_GRENADE_OUT` | "Frag out." | critical - ducks everything, plays immediately | 2s | always |
| `V_GRENADE_INCOMING` | "Grenade - get back, get back." | critical - ducks everything, plays immediately | 2s | always |
| `V_FRAG_HIT` | "That one landed." | flavour - only if nothing else is talking and never twice in a minute | 8s | 50% |
| `V_FLASH_OUT` | "Flash out." | tactical - plays at the next natural break | 3s | always |
| `V_FLASHED` | "I'm blind - I'm blind, don't move me." | critical - ducks everything, plays immediately | 4s | always |
| `V_SMOKE_OUT` | "Smoke going down." | tactical - plays at the next natural break | 3s | always |
| `V_BREACH_PLANTED` | "Charge set - stand clear." | critical - ducks everything, plays immediately | 2s | always |
| `V_BREACH_FIRE_IN_HOLE` | "Going in on the bang. Two, one -" | critical - ducks everything, plays immediately | 3s | always |
| `V_MEDKIT` | "Patching up. Give me a second." | tactical - plays at the next natural break | 6s | always |
| `V_HEAL_REQUEST` | "I need a kit if anyone's carrying." | tactical - plays at the next natural break | 8s | always |
| `V_HEALING` | "Hold still - this'll sting for a second." | tactical - plays at the next natural break | 5s | always |
| `V_HEAL_COMPLETE` | "Good as new. Roughly." | tactical - plays at the next natural break | 4s | always |
| `V_NO_MEDKIT` | "I've nothing left to give you." | tactical - plays at the next natural break | 6s | always |
| `V_MELEE_HIT` | "Get off me." | flavour - only if nothing else is talking and never twice in a minute | 6s | 60% |

### Keycards, doors and the override

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_KEYCARD_FOUND` | "Got a card. Floor nine, looking at the colour." | tactical - plays at the next natural break | 5s | always |
| `V_KEYCARD_NEEDED` | "We need a higher card for this one." | tactical - plays at the next natural break | 6s | always |
| `V_KEYCARD_USED` | "Card's good. Door's open." | tactical - plays at the next natural break | 4s | always |
| `V_DOOR_LOCKED` | "Locked. No card, no entry." | tactical - plays at the next natural break | 5s | always |
| `V_OVERRIDE_START` | "Starting the override - somebody watch the corridor." | critical - ducks everything, plays immediately | 4s | always |
| `V_OVERRIDE_IN_PROGRESS` | "Still going. Ten more seconds." | tactical - plays at the next natural break | 6s | always |
| `V_OVERRIDE_DENIED` | "It's refused. Card's not high enough." | tactical - plays at the next natural break | 5s | always |
| `V_OVERRIDE_COMPLETE` | "Override's done. Floors opening up." | critical - ducks everything, plays immediately | 3s | always |
| `V_LOCKDOWN_WARNING` | "Attention: the lower floors are closing. Please move up." | **the building's own announcement voice**, not an archetype - 2D, through the PA, ducks everything | 5s | announcement only |
| `V_LOCKDOWN_START` | "That's the alarms - they're shutting the floors down." | critical - ducks everything, plays immediately | 4s | always |
| `V_SHUTTERS_CLOSED` | "Shutters are down behind us. No way back." | critical - ducks everything, plays immediately | 5s | always |
| `V_FLOOR_SECURED` | "This floor's sealed. We're committed." | tactical - plays at the next natural break | 6s | always |
| `V_UP_WE_GO` | "Up. Fifteenth." | tactical - plays at the next natural break | 5s | always |
| `V_SECURITY_PURGE` | "They're venting the stairwell - don't be in it." | critical - ducks everything, plays immediately | 5s | always |

### Extraction

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_EXTRACTION_CALLED` | "Helicopter's inbound on the roof - winch is live." | critical - ducks everything, plays immediately | 4s | always |
| `V_EXTRACTION_HOLD` | "Hold the deck. Thirty seconds, that's all it is." | critical - ducks everything, plays immediately | 4s | always |
| `V_EXTRACTION_LEAVING` | "That's us. Somebody else's problem now." | critical - ducks everything, plays immediately | 3s | always |
| `V_EXTRACTION_LOST` | "They're on the winch - we lost it." | critical - ducks everything, plays immediately | 3s | always |
| `V_LAST_SQUAD_WARNING` | "One squad left up here besides us." | critical - ducks everything, plays immediately | 6s | always |

### Teamwork

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_PING_ENEMY` | "Enemy, marked." | tactical - plays at the next natural break | 3s | always |
| `V_PING_ITEM` | "Kit here - worth taking." | tactical - plays at the next natural break | 4s | always |
| `V_PING_MOVE` | "Move up." | tactical - plays at the next natural break | 3s | always |
| `V_COVER_ME` | "Cover me, I'm moving." | tactical - plays at the next natural break | 4s | always |
| `V_COVERING` | "I've got you covered." | tactical - plays at the next natural break | 4s | always |
| `V_GO_LEFT` | "I'll take the left." | tactical - plays at the next natural break | 4s | always |
| `V_GO_RIGHT` | "Going right. Don't follow too close." | tactical - plays at the next natural break | 4s | always |
| `V_HOLD_POSITION` | "Hold here. Let them come to us." | tactical - plays at the next natural break | 5s | always |
| `V_PUSH_NOW` | "Now. Go now." | critical - ducks everything, plays immediately | 4s | always |
| `V_FALL_BACK` | "Back. Back to the lifts." | critical - ducks everything, plays immediately | 4s | always |
| `V_QUIET` | "Quiet - I heard something in the ceiling." | tactical - plays at the next natural break | 10s | always |
| `V_WAIT` | "Wait. Not yet." | tactical - plays at the next natural break | 5s | always |
| `V_ACK` | "Understood." | tactical - plays at the next natural break | 3s | always |
| `V_ACK_NEGATIVE` | "No. Not that way." | tactical - plays at the next natural break | 3s | always |
| `V_GOOD_KILL` | "Nice one." | flavour - only if nothing else is talking and never twice in a minute | 12s | 35% |
| `V_BAD_LUCK` | "Hard lines. He was waiting for you." | flavour - only if nothing else is talking and never twice in a minute | 12s | 35% |
| `V_MAN_DOWN` | "Somebody's down - nearest one, get to him." | critical - ducks everything, plays immediately | 4s | always |
| `V_SQUAD_WIPED` | "That's the squad. It's on me now." | critical - ducks everything, plays immediately | 4s | always |
| `V_LAST_MAN_STANDING` | "Last one up. Fine." | critical - ducks everything, plays immediately | 4s | always |

### Ambience / corporate realism

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_AMBIENT_ELEVATOR` | "Nineteen years in this building and the lifts still sound like that." | ambient - no gameplay weight, fills a room | 60s | 25% |
| `V_AMBIENT_CANTEEN` | "They've still got the sandwich board out. Optimistic." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_SERVER_ROOM` | "It's warm in here. Always warm in here." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_RAIN` | "Rain's coming in sideways off the plaza." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_STAIRS` | "Fifteen floors. Nobody thought to fix the lifts." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_EXEC` | "The executive floor always smells like new carpet." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_TIRED` | "I've been on shift since six." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_COFFEE` | "Somebody's left a coffee on the third floor. It's gone cold." | ambient - no gameplay weight, fills a room | 60s | 15% |
| `V_AMBIENT_MEETING` | "Meeting room's booked till eight. Of course it is." | ambient - no gameplay weight, fills a room | 60s | 15% |
| `V_AMBIENT_SKYDECK` | "You can see the whole of the City from up here. Shame about the company." | ambient - no gameplay weight, fills a room | 60s | 20% |
| `V_AMBIENT_PLAZA` | "Plaza's quiet for this time. That's never a good sign." | ambient - no gameplay weight, fills a room | 60s | 20% |

### Match framing

| Line id | Line | Priority | Cooldown | Chance |
| --- | --- | --- | --- | --- |
| `V_MATCH_START` | "Right. In we go." | tactical - plays at the next natural break | 5s | always |
| `V_MATCH_HALFWAY` | "Halfway. Everyone's still standing." | flavour - only if nothing else is talking and never twice in a minute | 30s | 50% |
| `V_VICTORY` | "That's us on the deck. Somebody's going to have to explain this in the morning." | tactical - plays at the next natural break | 5s | always |
| `V_DEFEAT` | "We're done. Out." | tactical - plays at the next natural break | 5s | always |
| `V_SPECTATE_COMMENT` | "Watch the corner - he always takes the corner." | flavour - only if nothing else is talking and never twice in a minute | 20s | 40% |
| `V_STEP_GRATING` | "Careful - grating." | ambient - no gameplay weight, fills a room | 30s | 30% |
| `V_STEP_GLASS` | "Glass. Go round it." | ambient - no gameplay weight, fills a room | 30s | 30% |

---


---

## 3. The comms wheel

Eight lines the player says on purpose, mapped to the eight segments of the
in-game comms wheel (`Docs/08_UI.md`, "The three overlays"). Seven of these
already existed in the script; the wheel was designed by reading the table above,
not the other way round. `V_MAN_DOWN` and `V_LOCKDOWN_WARNING` were written
because the wheel exposed two genuine holes.

| # | Segment | Line id | Says | Why this one |
| --- | --- | --- | --- | --- |
| 1 | CONTACT | `V_CONTACT_ENEMY` | "Contact. One." | the line that keeps people alive |
| 2 | PUSH | `V_PUSH_NOW` | "Now. Go now." | the shortest thing in the script, deliberately |
| 3 | HOLD | `V_HOLD_POSITION` | "Hold here. Let them come to us." | the counter to PUSH, and the reason both exist |
| 4 | RELOADING | `V_RELOADING` | "Reloading - cover me." | the most-used line in any match |
| 5 | MOVING UP | `V_PING_MOVE` | "Move up." | the vertical game, in two words |
| 6 | MAN DOWN | `V_MAN_DOWN` | "Somebody's down - nearest one, get to him." | written for the wheel: a bystander's callout, which the script did not have |
| 7 | QUIET | `V_QUIET` | "Quiet - I heard something in the ceiling." | going silent, said out loud |
| 8 | NEGATIVE | `V_ACK_NEGATIVE` | "No. Not that way." | cancels the last callout |

**Rules the wheel follows**

1. **Every segment speaks an existing line.** A wheel that invents ids produces
   seven segments that do nothing and look fine in a screenshot, which is exactly
   what the first draft of it did.
2. **A segment never invents delivery.** The line is spoken the way the archetype
   speaks it (`§4`), at the priority in its own row, with its own cooldown
   enforced on the server. The wheel chooses *which* line, never *how*.
3. **Nothing is spoken locally.** Pressing a segment asks the server; the server
   resolves the line and multicasts it. A modified client can ask faster and
   nothing more.
4. **A line on cooldown is drawn greyed** with its remaining seconds, so a player
   is told before they commit rather than after. The numbers come from the same
   table the server enforces.

## 4. Archetype variants

Eleven lines, six times each. These are the lines a player hears most, and the reason two squads sound like two squads. Read the intent column, then say it the way your archetype would - not the way the row above says it.

| Line id | Intent |
| --- | --- |
| `V_DOWNED` | the down line, said as news rather than panic |
| `V_CONTACT_ENEMY` | first contact - the moment they decide how they sound for the match |
| `V_RELOADING` | reload call, the one every squad hears six times a match |
| `V_CONTACT_MULTIPLE` | two or more - the escalation in the voice is the information |
| `V_LAST_MAG` | ammunition state. Flat delivery, that is the point |
| `V_KEYCARD_FOUND` | picked up a card. This is the line that changes the match |
| `V_OVERRIDE_COMPLETE` | the override finished. Relief, immediately suppressed |
| `V_EXTRACTION_CALLED` | the winch is called - the loudest thing anyone says all match |
| `V_LOCKDOWN_WARNING` | **the building, not a person.** This is a fire-drill announcement read by whoever records the PA at Pinewood: pleasant, unhurried, mid-Atlantic corporate, entirely without irony. It should sound exactly like the lift. That is the joke, and it is also the threat |
| `V_MAN_DOWN` | a bystander, not the downed player, and the tone is practical rather than distressed: somebody has a problem and somebody else is nearest. It is the only line on the comms wheel that is a request for a specific person to do a specific thing |
| `V_LOCKDOWN_START` | shutters coming down. Understated: the building did it, not them |
| `V_VICTORY` | on the deck. No celebration, just breathing and one line |
| `V_AMBIENT_SKYDECK` | the idle line on the roof: ground level to 15th floor, done |

| Line | Archetype | Direction |
| --- | --- | --- |
| `V_DOWNED` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; said as news, not as panic - the report is the priority |
| `V_DOWNED` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; said as news, not as panic - the report is the priority |
| `V_DOWNED` | Finance | short sentences, no filler, does not raise their voice; said as news, not as panic - the report is the priority |
| `V_DOWNED` | IT / Operations | technically exact, tired of explaining it, names the system; said as news, not as panic - the report is the priority |
| `V_DOWNED` | Facilities / Security | working - imperative instructions, the only one who carries a body out; said as news, not as panic - the report is the priority |
| `V_DOWNED` | Consultant | friendly, detached, quietly the best person in the room; said as news, not as panic - the report is the priority |
| `V_CONTACT_ENEMY` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; the first thirty seconds decide how they sound for the whole match |
| `V_CONTACT_ENEMY` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; the first thirty seconds decide how they sound for the whole match |
| `V_CONTACT_ENEMY` | Finance | short sentences, no filler, does not raise their voice; the first thirty seconds decide how they sound for the whole match |
| `V_CONTACT_ENEMY` | IT / Operations | technically exact, tired of explaining it, names the system; the first thirty seconds decide how they sound for the whole match |
| `V_CONTACT_ENEMY` | Facilities / Security | working - imperative instructions, the only one who carries a body out; the first thirty seconds decide how they sound for the whole match |
| `V_CONTACT_ENEMY` | Consultant | friendly, detached, quietly the best person in the room; the first thirty seconds decide how they sound for the whole match |
| `V_RELOADING` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; flat and functional; everyone already looks after their own ammo |
| `V_RELOADING` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; flat and functional; everyone already looks after their own ammo |
| `V_RELOADING` | Finance | short sentences, no filler, does not raise their voice; flat and functional; everyone already looks after their own ammo |
| `V_RELOADING` | IT / Operations | technically exact, tired of explaining it, names the system; flat and functional; everyone already looks after their own ammo |
| `V_RELOADING` | Facilities / Security | working - imperative instructions, the only one who carries a body out; flat and functional; everyone already looks after their own ammo |
| `V_RELOADING` | Consultant | friendly, detached, quietly the best person in the room; flat and functional; everyone already looks after their own ammo |
| `V_CONTACT_MULTIPLE` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; the escalation carries the information, not the volume |
| `V_CONTACT_MULTIPLE` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; the escalation carries the information, not the volume |
| `V_CONTACT_MULTIPLE` | Finance | short sentences, no filler, does not raise their voice; the escalation carries the information, not the volume |
| `V_CONTACT_MULTIPLE` | IT / Operations | technically exact, tired of explaining it, names the system; the escalation carries the information, not the volume |
| `V_CONTACT_MULTIPLE` | Facilities / Security | working - imperative instructions, the only one who carries a body out; the escalation carries the information, not the volume |
| `V_CONTACT_MULTIPLE` | Consultant | friendly, detached, quietly the best person in the room; the escalation carries the information, not the volume |
| `V_LAST_MAG` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; no drama - this is administration in the middle of a firefight |
| `V_LAST_MAG` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; no drama - this is administration in the middle of a firefight |
| `V_LAST_MAG` | Finance | short sentences, no filler, does not raise their voice; no drama - this is administration in the middle of a firefight |
| `V_LAST_MAG` | IT / Operations | technically exact, tired of explaining it, names the system; no drama - this is administration in the middle of a firefight |
| `V_LAST_MAG` | Facilities / Security | working - imperative instructions, the only one who carries a body out; no drama - this is administration in the middle of a firefight |
| `V_LAST_MAG` | Consultant | friendly, detached, quietly the best person in the room; no drama - this is administration in the middle of a firefight |
| `V_KEYCARD_FOUND` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; a small, quick lift. Do not celebrate it |
| `V_KEYCARD_FOUND` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; a small, quick lift. Do not celebrate it |
| `V_KEYCARD_FOUND` | Finance | short sentences, no filler, does not raise their voice; a small, quick lift. Do not celebrate it |
| `V_KEYCARD_FOUND` | IT / Operations | technically exact, tired of explaining it, names the system; a small, quick lift. Do not celebrate it |
| `V_KEYCARD_FOUND` | Facilities / Security | working - imperative instructions, the only one who carries a body out; a small, quick lift. Do not celebrate it |
| `V_KEYCARD_FOUND` | Consultant | friendly, detached, quietly the best person in the room; a small, quick lift. Do not celebrate it |
| `V_OVERRIDE_COMPLETE` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; relief, immediately suppressed and put back in a channel |
| `V_OVERRIDE_COMPLETE` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; relief, immediately suppressed and put back in a channel |
| `V_OVERRIDE_COMPLETE` | Finance | short sentences, no filler, does not raise their voice; relief, immediately suppressed and put back in a channel |
| `V_OVERRIDE_COMPLETE` | IT / Operations | technically exact, tired of explaining it, names the system; relief, immediately suppressed and put back in a channel |
| `V_OVERRIDE_COMPLETE` | Facilities / Security | working - imperative instructions, the only one who carries a body out; relief, immediately suppressed and put back in a channel |
| `V_OVERRIDE_COMPLETE` | Consultant | friendly, detached, quietly the best person in the room; relief, immediately suppressed and put back in a channel |
| `V_EXTRACTION_CALLED` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; the loudest anyone gets all match, and still not shouted |
| `V_EXTRACTION_CALLED` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; the loudest anyone gets all match, and still not shouted |
| `V_EXTRACTION_CALLED` | Finance | short sentences, no filler, does not raise their voice; the loudest anyone gets all match, and still not shouted |
| `V_EXTRACTION_CALLED` | IT / Operations | technically exact, tired of explaining it, names the system; the loudest anyone gets all match, and still not shouted |
| `V_EXTRACTION_CALLED` | Facilities / Security | working - imperative instructions, the only one who carries a body out; the loudest anyone gets all match, and still not shouted |
| `V_EXTRACTION_CALLED` | Consultant | friendly, detached, quietly the best person in the room; the loudest anyone gets all match, and still not shouted |
| `V_LOCKDOWN_START` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; understated: the building did this, not them |
| `V_LOCKDOWN_START` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; understated: the building did this, not them |
| `V_LOCKDOWN_START` | Finance | short sentences, no filler, does not raise their voice; understated: the building did this, not them |
| `V_LOCKDOWN_START` | IT / Operations | technically exact, tired of explaining it, names the system; understated: the building did this, not them |
| `V_LOCKDOWN_START` | Facilities / Security | working - imperative instructions, the only one who carries a body out; understated: the building did this, not them |
| `V_LOCKDOWN_START` | Consultant | friendly, detached, quietly the best person in the room; understated: the building did this, not them |
| `V_VICTORY` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; breathing, then one line. No celebration |
| `V_VICTORY` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; breathing, then one line. No celebration |
| `V_VICTORY` | Finance | short sentences, no filler, does not raise their voice; breathing, then one line. No celebration |
| `V_VICTORY` | IT / Operations | technically exact, tired of explaining it, names the system; breathing, then one line. No celebration |
| `V_VICTORY` | Facilities / Security | working - imperative instructions, the only one who carries a body out; breathing, then one line. No celebration |
| `V_VICTORY` | Consultant | friendly, detached, quietly the best person in the room; breathing, then one line. No celebration |
| `V_AMBIENT_SKYDECK` | Business Analyst | level and precise, numbers on the front foot, slightly too fast; idle, on the roof, ground level to fifteenth floor, done |
| `V_AMBIENT_SKYDECK` | Graduate / Intern | a beat behind and covering it, audible relief when someone else takes over; idle, on the roof, ground level to fifteenth floor, done |
| `V_AMBIENT_SKYDECK` | Finance | short sentences, no filler, does not raise their voice; idle, on the roof, ground level to fifteenth floor, done |
| `V_AMBIENT_SKYDECK` | IT / Operations | technically exact, tired of explaining it, names the system; idle, on the roof, ground level to fifteenth floor, done |
| `V_AMBIENT_SKYDECK` | Facilities / Security | working - imperative instructions, the only one who carries a body out; idle, on the roof, ground level to fifteenth floor, done |
| `V_AMBIENT_SKYDECK` | Consultant | friendly, detached, quietly the best person in the room; idle, on the roof, ground level to fifteenth floor, done |

(The cell text is the direction of voice for that archetype; the words are the same for all six, and that is deliberate. Six people reading the same sentence six ways is what makes a squad read as six people.)

---

## 5. Things nobody says

Written down because the first take of a line is always the wrong one, and these are the wrong ones:

* Anything with "hostiles", "tango", "bogey" or a NATO phonetic alphabet.
* Any line about stationery, photocopiers, meetings or "I could murder a coffee".
* Any line that references a film, a game or a meme.
* Any line that explains the game to the player ("the extraction is on the roof!") - the HUD does that, and the character is not a tutorial.
* Any line that is funny on purpose. If it is funny, it is because the situation is, and the actor is playing it straight.

## 6. Line count and coverage

| Group | Lines |
| --- | --- |
| Core lines in the runtime table above | 97 |
| Archetype variants (11 lines x 6 archetypes) | 66 |
| **Total rows to record** | **163** |

The 97 is not a claim, it is a count: `OneBrahamVoiceLineSet.cpp` contains 97
`Line(...)` rows, every one of them is listed above, and
`check_voice_lines.py` fails the build if those two stop agreeing in either
direction. The two lines added for the comms wheel - `V_MAN_DOWN` and
`V_LOCKDOWN_WARNING` - are included.

Eleven of the core lines are recorded six times each; the other 86 are recorded once and used unpitched by every archetype, because a grenade warning does not need six readings and the radio channel is narrow.

Coverage: every gameplay event that a player must communicate has a line - 12 combat, 8 downed/revive, 13 weapons and utility, 6 access and lockdown, 7 objective and extraction, 9 social and team, 9 ambient, 6 match, plus the deaths and the objective calls. A squad that never uses a microphone can still play ASCENT properly, which is the actual design goal of the voice system.
