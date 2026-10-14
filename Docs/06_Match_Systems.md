# ONE BRAHAM — match systems

Access, lockdown, extraction, the game modes, bots and the rules that decide how
a match ends.

Implementation: `Source/OneBraham/{Access,Lockdown,Extraction,Bots}/`,
`Core/OneBrahamGameMode.cpp`, `Data/OneBrahamMatchSettingsAsset.h`.

---

## 1. The three modes

### ASCENT — the primary mode

| Property | Value |
| --- | --- |
| Players | 12, in 3 squads of 4 |
| Teams | squads are free-for-all: there are three squads and one extraction |
| AI | none. Not "few": none |
| Respawn | no respawns. Downed → revive or bleed out → eliminated |
| Objective | reach the Skydeck, hold the pad 30 s, extract |
| Match length | 22–28 minutes typical; hard end at 35 |
| Endings | one squad extracts · one squad left standing with the pad claimed · **no survivors** |

**Why squads and not teams.** Three squads means the player is never sure where
the next threat is coming from, and it means the extraction is contested by more
than one opponent. The building is designed as a vertical maze; a 6v6 would turn
it into two sides pushing a line.

### FIRE DRILL — the secondary mode

| Property | Value |
| --- | --- |
| Players | 12 (6v6) or 16 (8v8) |
| Teams | Internal Staff vs External Contractors |
| Objective | TDM/control over three points: Lobby, Server Floor, Canteen–Skydeck |
| Respawn | wave respawn every 12 s, spawn protection 2 s, no downed state |
| Match length | 10 minutes, or first to 200 points |
| Why it exists | it is how a player learns the map, the weapons and the callouts without losing a 25-minute run to it |

### NIGHT SHIFT — post-launch

4-player co-op survival against the building's own systems: lockdowns, purge
waves, security shutters that close on you, no zombies and no AI enemies. The map
is the antagonist and the objective is to keep a floor alive long enough to walk
out of it. **Explicitly out of the launch scope** and listed here so it is not
mistaken for an unfinished feature.

---

## 2. Access — the spine of ASCENT

Ten gated zones, five tiers, three ways through each:

| Tier | Tag | Keycards in the world | Floors |
| --- | --- | --- | --- |
| Public | `OB.Access.Tier.Public` | none needed | ground, lobby, mezzanine |
| Staff | `OB.Access.Tier.Staff` | 14 spawns | office floors 3–8 |
| Secure | `OB.Access.Tier.Secure` | 6 spawns | server and comms 9–11 |
| Executive | `OB.Access.Tier.Executive` | 3 spawns | floor 12 |
| Skydeck | `OB.Access.Tier.Skydeck` | 2 spawns | canteen 13–14, Skydeck 15 |

### The three routes

| Route | How | Cost |
| --- | --- | --- |
| **Card** | walk up to the reader with the tier held | a card you had to find, and a card you have to keep |
| **Override** | hold the terminal: **12 s alone, 3 s with a second player on the paired panel** | the alarm: 4 s after the hold starts, a klaxon runs for 25 s and every squad learns which door is opening |
| **Breach** | a breaching charge on one of four partitions | `260` integrity at `0.6` resistance, 3 s, `V_BREACHING` audible at 8,500 cm, and a permanent hole that everybody can use |

The paired override is the game's designed co-op moment and it appears exactly
twice: on the server floor and on the executive floor. It is 3 seconds instead of
12, it requires two people who trust each other to stand still in the open, and it
is the single best story a squad will tell after a match.

### The door — `AOneBrahamAccessZone`

| Property | Value |
| --- | --- |
| Interaction range | 240 cm |
| Open duration | 1.15 s |
| Swing | 92° |
| Latch threshold | 12% of the swing |
| Breach integrity | 260, resistance 0.6 |
| States | `Locked, Unlocked, Opening, Open, Closing, Breached` |

A door left open stays open. That is a decision the player made and the building
remembers it.

---

## 3. Lockdown — the clock that is not a clock

`UOneBrahamLockdownComponent`. Four stages, all replicated as a stage plus a
start time:

| Stage | Duration | What happens |
| --- | --- | --- |
| `Normal` | until stage 2 | nothing |
| `PreLockdown` | 15 s warning | announcement, klaxon, exit signs flash, every screen shows the notice |
| `Lockdown` | 90 s | floors below `HighestOccupiedFloor - 1` seal. Fire shutters close, one at a time, 2.75 s each |
| `Purge` | until the match ends | every floor ≤ 13 takes escalating damage on a 2 Hz pulse. Floors 14 and 15 are safe |

Rules, all implemented:

1. **Sealing is relative, never absolute.** The seal line is
   `HighestOccupiedFloor - 1`, recomputed at 4 Hz. A squad that climbs is safe
   one floor below them; a squad that camps is standing on floors that are about
   to close.
2. **The Skydeck is never sealed.** There is no configuration in which the
   building removes the only way out. This is a fairness guarantee, not a
   difficulty setting.
3. **The purge is unattributed.** No kill credit, no squad points. The results
   screen lists purge deaths separately, because being killed by the building is
   not the same as being outplayed.
4. **A sealed floor traps nothing permanently.** Every sealed floor still has its
   stairwell: the shutters seal *rooms*, and the vertical routes stay open until
   the purge.
5. **A squad wiped by the purge ends the match as `NoSurvivors`** if no squad
   extracts. The building can win.

CVar `ob.Lockdown.ForceStage <0-3>` for testing, and `ob.Security.Verbose 1`
for the full trace of what sealed and when.

---

## 4. Extraction — the ending

`UOneBrahamExtractionComponent` on the game state. One extraction at a time.

```
Inactive ──call──► Broadcast (10 s) ──► Arriving (45 s) ──► Holding ──► Complete
                                                     │           │
                                                     └─── Failed ◄┘  (squad wiped,
                                                                      loiter 75 s,
                                                                      mode ≠ ASCENT)
```

| Rule | Value |
| --- | --- |
| Hold radius | 900 cm |
| Hold time | 30 s |
| Contested | a member of another squad inside the radius **freezes** the timer; it does not reset it |
| Prerequisites | mode is ASCENT · ≥ 1 squadmate alive · the claiming pawn's Z ≥ 5600 |
| Loiter | 75 s from the call; a squad that calls and does not come fails and the extraction is locked out for 60 s |
| Fast mode | CVar `ob.Extraction.Fast 1` halves the durations for testing |

**Contested freezes, it does not reset**, and that is the most important rule in
the mode. A reset would let a single defender deny an extraction forever by
dying on the pad; freezing means the fight is *over the pad*, and whoever
controls it when the other side breaks wins.

The 30 seconds are the loudest, brightest, most exposed 30 seconds in the game.
The pad is covered on one side (the AHU boxes) and open on three. The announcing
squad's voice line plays for everybody: the extraction is public information by
design.

---

## 5. Bot behaviour — FIRE DRILL and offline only

Bots are **not in ASCENT**. They exist so that FIRE DRILL, the offline mode and
the tutorial work, and so the game can be tested without twelve people.

| Sense | Value |
| --- | --- |
| Sight range | 5,200 cm (day), 6,200 cm with a clear sightline down a long room |
| Sight cone | 100° horizontal, 60° vertical |
| Hearing | 6,500 cm base, scaled by the weapon's own `AudibleRadius` |
| Reaction time | 0.85 s, down to 0.22 s at the highest difficulty |
| Accuracy | cone-based, 4° base tightening over 1.2 s of continuous sight |
| Memory | last known position, 8 s; a bot that has been shot at investigates |
| Grenade use | one flashbang per engagement, only if the player is behind cover |

Bots use the floor's navmesh, which is rebuilt when a door or shutter changes
state, so a bot never walks into a door a player sealed five seconds ago.

---

## 6. Match settings

`UOneBrahamMatchSettingsAsset` (`DA_MatchSettings_Ascent`,
`DA_MatchSettings_FireDrill`, `DA_MatchSettings_NightShift`):

| Field | ASCENT default | FIRE DRILL default |
| --- | --- | --- |
| `MaxPlayers` | 12 | 16 |
| `SquadSize` | 4 | 6 |
| `SquadCount` | 3 | 2 |
| `bAllowRespawn` | false | true |
| `RespawnDelaySeconds` | 0 | 12 |
| `bDownedStateEnabled` | true | false |
| `BleedOutSeconds` | 45 | 0 |
| `ReviveSeconds` | 4.0 | 0 |
| `LockdownEnabled` | true | false |
| `LockdownFirstStageDelaySeconds` | 600 | 0 |
| `PurgeDamagePerTick` | 4 | 0 |
| `ExtractionEnabled` | true | false |
| `ObjectiveCount` | 1 | 3 |
| `MatchTimeLimitSeconds` | 2100 | 600 |
| `PointsToWin` | 0 | 200 |
| `FriendlyFireScale` | 0.5 | 0.0 |
| `BotsEnabled` | false | true (filled to minimum) |
| `MinPlayersToStart` | 6 | 4 |

The values live in `Config/DefaultGame.ini` under `[OneBraham.Modes]` as well, so
a server operator can retune a mode without an editor.

---

## 7. The end of a match

Four endings, all designed, none of them a null:

| Ending | Condition | Results screen |
| --- | --- | --- |
| **Extracted** | a squad completes the 30 s hold | survivors named, damage, objectives, the winch replay as the backdrop |
| **Last squad standing** | all other squads eliminated, extraction not completed | "The building is yours. Nobody else's." |
| **No survivors** | every squad eliminated, including by the purge | every player listed as lost, the building named as the cause |
| **Time out** | the hard limit is reached | nobody extracts, everybody lost |

The last two are the reason the game has a tone. A match that goes wrong in this
building should end with nobody walking out of it, and the results screen should
say so plainly rather than pretending a losing squad "placed second".

### What a kill is worth

`AOneBrahamGameMode::RegisterKill` is the single place a kill is recorded, and it
does three things: it writes the feed entry, it credits the killer, and it credits
the assists.

- **The killer** gets `ServerRegisterKill(ScorePerKill)`, and in FIRE DRILL the
  same amount goes to their team (`AddTeamScoreByIndex`). In ASCENT there is no
  team score at all, because the win condition is the winch and a kill count is
  not a score.
- **The assists** are everyone who took **20% or more of the victim's maximum
  health**, inside the window the health component keeps and closes five seconds
  after the victim's last hit (`UOneBrahamHealthComponent::GetRecentAttackers`),
  and who is not the killer. `ServerRegisterAssist` gives each of them the count
  and a third of a kill's score. The killer is never their own assist, a
  self-kill has no assists, and the building has no player state to credit.
- **The feed entry** carries `bAssisted`, because "Okonkwo, assisted" and
  "Okonkwo" are different facts about the last ten seconds, and a squad that
  shoots together should be able to see that it did.
