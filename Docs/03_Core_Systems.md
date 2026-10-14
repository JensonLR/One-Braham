# ONE BRAHAM — core systems

The foundation every other system stands on: types, tags, settings, the game
instance, the game state, the player state, the controller, the game mode and
spawn selection.

Implementation: `Source/OneBraham/Core/`.

---

## 1. `OneBrahamTypes.h`

Every shared enum and struct. The ones that matter:

| Type | Values / fields | Notes |
| --- | --- | --- |
| `EOneBrahamMatchMode` | `Ascent, FireDrill, NightShift` | `NightShift` is post-launch |
| `EOneBrahamMatchPhase` | `Warmup, Active, Extraction, Complete, PostMatch` | the top-level state machine |
| `EOneBrahamTeam` | `None, Alpha, Bravo, Charlie, Delta, InternalStaff, ExternalContractors, Spectator` | ASCENT squads use Alpha–Delta, FIRE DRILL the two staff teams |
| `EOneBrahamLifeState` | `Alive, Downed, Dead, Spectating, Extracted` | |
| `EOneBrahamAccessTier` | `Public, Staff, Secure, Executive, Skydeck, Override` | six tiers; `Override` is the security tool, never loot |
| `EOneBrahamUnlockMethod` | `None, Keycard, Override, Breach, Escalation, Cheat` | an access zone records how it opened |
| `EOneBrahamFireMode` | `Single, Burst, Auto, Pump, Bolt` | `Pump` is the shotgun, `Bolt` the marksman |
| `EOneBrahamKillFeedVerb` | `Killed, Headshot, Executed, Building, Left, OwnUtility` | one verb drives the feed and the death screen |
| `EOneBrahamLockdownStage` | `Normal, PreLockdown, Lockdown, Purge` | |
| `EOneBrahamExtractionState` | `Inactive, Broadcast, Arriving, Holding, Complete, Failed` | |
| `EOneBrahamExtractionFailReason` | `None, CallersEliminated, Contested, LoiterExpired, Purge` | the end-of-match copy |
| `EOneBrahamHitZone` | `Head, Neck, Torso, UpperArm, LowerArm, Hand, UpperLeg, LowerLeg, Foot` | multipliers 2.6 / 0.8 / 1.0 / 0.82 |
| `EOneBrahamWeaponCategory` | `Pistol, CompactPistol, SuppressedPistol, SMG, PDW, PumpShotgun, SemiAutoShotgun, Carbine556, CompactAssaultRifle, MarksmanRifle, Utility` | |
| `EOneBrahamWeaponSlot` | `Primary, Sidearm, Utility, Access, Melee` | |
| `FOneBrahamKillFeedEntry` | killer, victim, both teams, verb, `WeaponId`, `bWasDowned`, `bAssisted`, timestamp | replicated struct |
| `FOneBrahamSquadInfo` | `SquadId`, `Team`, `SquadName`, `Members`, `bEliminated`, `bHasExtractionClaim` | replicated struct |
| `FOneBrahamArchetypeDefinition` | `ArchetypeTag`, body, face, hair, clothing, animation, voice, cosmetics | archetypes are tags, not an enum — see §2 |

Health model: **100 HP + 50 armour**, and armour absorbs 60% of incoming damage
until it breaks. At 0 HP a player is **downed, not dead** — bleed-out is 60
seconds, a revive takes 4.5 seconds and returns you at 40 HP, an execution takes
2 seconds, and a downed player who takes lethal damage again dies outright, with
two revives per match. A headshot bypasses the downed state entirely, an
explosion of 120+ base damage does too, and the building never leaves anyone
alive to be revived. FIRE DRILL switches the state off in `MatchSettings` and
respawns instead. This is the whole reason ASCENT has tension: a fight that goes
badly leaves you alive and helpless, which is worse than dying.

---

## 2. Gameplay tags

`OneBrahamTags` namespace, one `FGameplayTag` per name, declared natively in
`OneBrahamGameplayTags.h` / `.cpp` and mirrored in `Config/DefaultGame.ini` so
designers can pick them before the first compile. All 147:

```
OB.Access.Method.{Breach, Escalation, Keycard, Override}
OB.Access.Tier.{Executive, Override, Public, Secure, Skydeck, Staff}
OB.Access.Zone.{Canteen, Executive, Lobby, Mezzanine, Offices, ServerFloor,
    Skydeck}
OB.Audio.Event.{Alarm, Announcement, Helicopter, Lift, Shutter}
OB.Audio.State.{Floor.Canteen, Floor.Executive, Floor.Exterior, Floor.Lobby,
    Floor.Mezzanine, Floor.Offices, Floor.Server, Floor.Skydeck, Rain.Heavy,
    Rain.Light}
OB.Character.Archetype.{BusinessAnalyst, Consultant, FacilitiesSecurity,
    FinanceBroker, Graduate, ITOperations}
OB.Character.State.{Alive, Dead, Downed, Extracted, Extracting, Spectating}
OB.Cosmetic.Rarity.{Rare, Standard, Uncommon}
OB.Cosmetic.Slot.{Backpack, Badge, Feet, Head, Lanyard, Legs, Outerwear,
    Torso, WeaponSkin}
OB.Cosmetic.Unlock.{Achievement.FireDrill50, Default, Milestone.Extract5,
    Milestone.NoDamageExtract, Milestone.ServerFloorWin}
OB.Extraction.Arriving
OB.Extraction.Broadcast
OB.Extraction.Complete
OB.Extraction.Failed
OB.Extraction.Holding
OB.Extraction.Inactive
OB.Interaction.Ammo
OB.Interaction.Body
OB.Interaction.Door
OB.Interaction.Door.{Breachable, Secure}
OB.Interaction.Execute
OB.Interaction.Extraction
OB.Interaction.HoldToUnlock
OB.Interaction.Keycard
OB.Interaction.Lift
OB.Interaction.Medkit
OB.Interaction.Revive
OB.Interaction.TapToUse
OB.Interaction.Terminal
OB.Interaction.Utility
OB.Interaction.Weapon
OB.Match.Lockdown.{Lockdown, Normal, PreLockdown, Purge}
OB.Match.Mode.{Ascent, FireDrill, NightShift}
OB.Match.Phase.{Active, Complete, Extraction, Warmup}
OB.Team.Alpha
OB.Team.Bravo
OB.Team.Charlie
OB.Team.Delta
OB.Team.ExternalContractors
OB.Team.InternalStaff
OB.Team.Spectator
OB.Voice.Category.{Access, Banter, Combat, Contact, Death, Down, Extraction,
    Lose, LowAmmo, Movement, Reactive, Reload, Revive, RoundStart, Win}
OB.Voice.Channel.{Proximity, Radio, Squad, Team}
OB.Weapon.Attachment.{Flashlight, Grip, Laser, Magazine, Muzzle, Optic}
OB.Weapon.Carbine.556
OB.Weapon.Fire.{Auto, Bolt, Burst, Pump, Single}
OB.Weapon.Marksman
OB.Weapon.PDW
OB.Weapon.Pistol
OB.Weapon.Pistol.{Compact, Suppressed}
OB.Weapon.Rifle.Compact
OB.Weapon.SMG
OB.Weapon.Shotgun.{Pump, SemiAuto}
OB.Weapon.Utility
OB.Weapon.Utility.{BreachCharge, Flashbang, Frag, Medkit, Smoke}
```

A tag is an interface. Blueprint, data assets and C++ all agree on the string,
and no code depends on an asset's name. `check_gameplay_tags.py` fails the build
if this list and `Config/DefaultGame.ini` ever disagree, in either direction.

---

## 3. Settings

Three layers, each with one job:

| Class | Stores | Persisted by |
| --- | --- | --- |
| `UOneBrahamGameSettings` | developer-tunable defaults: the seven UI colours, the interaction-distance tolerance, the voice occlusion, the default mode and tick rates | read from `DefaultGame.ini` `[OneBrahamGameSettings]` at startup |
| `UOneBrahamGameUserSettings` | player video/audio/gameplay/accessibility settings | `GameUserSettings.ini` (engine) |
| `UOneBrahamSaveSettings` | keybinds (per device), sensitivity, HUD scale, last-used loadout | `Saved/SaveGames/OneBrahamSettings.sav` |

`UOneBrahamInputConfig` is a data asset holding the 41 `UInputAction*` pointers
and the 4 mapping contexts, so input is data-driven and `ValidateAllActionsSet()`
can assert none is null at startup rather than crashing on the first keypress.

The palette lives in the ini rather than in code so a designer can retune the
brand without a compile, and so `build_ui.py` can verify the code and the ini
agree on every content build.

---

### The three interaction distances

They are three because they answer three different questions, and confusing them
is how a game ends up with prompts the server refuses:

| Number | Value | Where | The question it answers |
| --- | --- | --- | --- |
| `OneBrahamConstants::InteractRangeCm` | 220 cm | `OneBraham.h` | How far can the player reach? The client traces this far, the server validates against it, and an interactable that declares no range of its own gets it as a default. |
| `UOneBrahamGameSettings::MaxInteractDistanceTolerance` | 220 cm | project settings | How much slack does the server allow for latency? A player who pressed interact at 210 cm while backing away is further than that by the time the RPC lands. |
| `AOneBrahamCharacter::GetInteractRange()` | the constant | character | The single accessor the trace and the HUD prompt use, so the two cannot disagree. |

---

## 4. `UOneBrahamGameInstance`

Holds what survives a level load:

* the Steam session (`IOnlineSessionPtr`) and the lobby handle
* the player's chosen archetype and loadout
* mute list and voice settings
* the settings object and its save
* the server's advertised name and whether it is dedicated

It does **not** hold match state. Anything that belongs to a match lives on the
game state, because the game instance outlives the match and stale match data on
the game instance is the classic source of "my last game's keycard is still in my
inventory" bugs.

---

## 5. `AOneBrahamGameState`

| Member | Purpose |
| --- | --- |
| `MatchPhase`, `PhaseStartTime`, `PhaseDuration` | the top-level clock; every countdown derives from these |
| `Mode`, `ModeName` | |
| `Squads` (`TArray<FOneBrahamSquadInfo>`) | 3 for ASCENT, 2 for FIRE DRILL |
| `HighestOccupiedFloor` | drives the lockdown's sealing rule |
| `KillFeed` (`TArray<FOneBrahamKillFeedEntry>`) | 7-second entries, batched |
| `bExtractionAvailable` | |
| `LockdownComponent` | the stage director (PART 6) |
| `ExtractionComponent` | the winch state machine (PART 6) |
| `MulticastAnnounce(FText, bool)` | one announcement path for every system |
| `OnMatchPhaseChanged`, `OnKillFeedUpdated`, `OnSquadUpdated` | delegates |

Two components rather than one because they have different lifetimes: the
lockdown runs from the second stage of a match to the end, and the extraction only
exists once a squad calls it. A single "match component" holding both would carry
an inactive extraction machine through 20 minutes of every match.

---

## 6. `AOneBrahamPlayerState`

Identity (`Archetype`, `SquadId`, `Team`, `bIsSquadLeader`), state
(`bIsDowned`, `bIsEliminated`, `bHasExtracted`), statistics (kills, downs,
assists, damage, objectives, extractor role), and the keycard inventory
(`HeldKeycards`, replicated). `IsEliminated()` is the authoritative liveness
check for every other system: the controller is not the authority on whether a
player is alive, because the controller exists before a pawn does and after the
pawn is gone.

---

## 7. `AOneBrahamPlayerController`

Possesses the pawn, drives Enhanced Input, owns the HUD, owns the interact
component, and relays intents that can be spoofed by a client to the server for
validation. It never applies a gameplay effect locally: everything a client
wants to do is a `Server*` RPC and the server decides.

Input is routed through the mapping contexts on the input config; a controller in
a menu swaps to `IMC_OneBraham_UI` and the gameplay contexts are removed, which is
how the game avoids "I could still shoot while typing in the settings".

---

## 8. `AOneBrahamGameMode`

The match director. Responsibilities:

1. Assign teams and squads on login.
2. Drive the phase machine: `Warmup → Deploy → Playing → Lockdown → Extraction → Results`.
3. Start the lockdown as the match matures, and the extraction when a squad
   claims it.
4. Decide the ending: one squad extracts, or the last squad standing, or
   **nobody** — if every squad is eliminated, the match ends with
   `NoSurvivors=true` and every player is listed as lost.
5. Award and record statistics, then hand to the results screen.

The `NoSurvivors` outcome is not an edge case to be handled; it is a designed
ending, and the results screen treats it as the worst possible result rather than
as a null. A player's last memory of a losing match should be the building
winning.

---

## 9. Spawn selection

`AOneBrahamSpawnPoint` carries a location, team/squad tags, an area name, a floor
and a `bEnabled` flag.

`ScoreSpawnPoint` ranks candidates by:

| Factor | Weight |
| --- | --- |
| nearest visible enemy | heavy penalty, hard reject under 1,500 cm |
| nearest enemy, any state | penalty under 2,000 cm |
| distance to the squad's centroid | penalty over 1,200 cm, so a squad arrives together |
| recent deaths within 800 cm | penalty (a spawn point where somebody just died is a spawn point about to be watched) |
| floor above the lockdown seal | large bonus |
| line of sight from a contested objective | penalty |
| random jitter | small, so it is not deterministic across matches |

The game mode retries up to 8 candidates and, if every candidate is rejected,
falls back to the best-scoring point anyway and logs it — a spawn is never
refused, because refusing to spawn a player is a worse bug than an imperfect one.
