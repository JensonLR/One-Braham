# ONE BRAHAM — characters and clothing

Six archetypes, twelve cosmetic slots, one shared skeleton. The rules, the
numbers, the animation and the unwritten law that cosmetics never change how the
game plays.

Cast table and payload: `Tools/Python/onebraham/build_characters.py`, applied to
`DA_CharacterRegistry` by `apply_cast_payload.py`.

---

## 1. The six archetypes

| Archetype | Tag | Home floor | Body | Height | Voice set | Who they are | Animation personality |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Business Analyst | `OB.Character.Archetype.BusinessAnalyst` | 5 | 0 | -2.0 cm | `Vox_BA_London` | Measuring everything, out loud, because it helps | Brisk, upright, efficient. Walk 1.00, arms 0.90, handling 1.00, lean 0.45, hunch 0.10, crouch 0.60 |
| Graduate / Intern | `OB.Character.Archetype.Graduate` | 4 | 3 | +1.5 cm | `Vox_Grad_London` | Trying hard not to be the one who panics | Hurried, slightly loose. Walk 1.16, arms 1.30, handling 0.92, lean 0.75, hunch 0.35, crouch 0.35 |
| Finance | `OB.Character.Archetype.FinanceBroker` | 8 | 1 | +3.0 cm | `Vox_Finance_London` | Unbothered. Has seen worse on a Tuesday | Economical, heavy. Walk 1.05, arms 1.05, handling 1.03, lean 0.35, hunch -0.05, crouch 0.70 |
| IT / Operations | `OB.Character.Archetype.ITOperations` | 9 | 2 | +0.5 cm | `Vox_IT_London` | Knows exactly what the building is doing and is annoyed about it | Precise, hunched at the screen. Walk 0.97, arms 0.85, handling 1.08, lean 0.50, hunch 0.40, crouch 0.75 |
| Facilities / Security | `OB.Character.Archetype.FacilitiesSecurity` | 15 | 4 | +2.0 cm | `Vox_Facilities_London` | Responsible for everyone, paid for none of it | Working gait, heavy boots. Walk 0.94, arms 0.80, handling 1.05, lean 0.30, hunch 0.05, crouch 0.85 |
| Consultant | `OB.Character.Archetype.Consultant` | 7 | 5 | -0.5 cm | `Vox_Consultant_London` | Here for six weeks, noticeably better at this than expected | Relaxed, athletic, never rushed. Walk 1.08, arms 1.15, handling 0.96, lean 0.65, hunch 0.55, crouch 0.45 |

**No archetype has a gameplay advantage.** Different body variants change
nothing that damage, movement speed, hit volume, weapon handling or audio
touches. `validate_project.py` asserts it on every build, and the assertion is
the whole competitive argument for shipping cosmetic variety in a 12-player
extraction shooter.

---

## 2. The animation personality system

One `ABP_OneBrahamCharacter` for all six. The differences between people come
from twelve parameters, and that is deliberate: six animation blueprints is six
times the work and six times the bugs.

| Parameter | Range | Drives |
| --- | --- | --- |
| `WalkRateScale` | 0.90–1.15 | locomotion playback rate |
| `ArmSwingScale` | 0.75–1.20 | arm swing amplitude |
| `WeaponHandlingScale` | 0.90–1.10 | how quickly the weapon settles after a shot |
| `IdleLeanScale` | 0.2–0.8 | how much weight they put on one leg |
| `SprintHunch` | 0.0–0.6 | how much they fold when sprinting |
| `CrouchComposure` | 0.4–0.8 | how steady the crouch aim pose is |
| `ArchetypeIndex` | 0–5 | additive layer selection |
| `bIsDowned` | bool | the downed pose set |
| `bIsAiming` | bool | ADS pose and IK weight |
| `bIsSprinting` | bool | sprint pose and weapon-down pose |
| `Speed` | 0–1000 | blend space |
| `Lean` | −1–1 | the peek layer, driven by input |

The parameters are set once on deploy from the character data asset. Two
Business Analysts walk the same; a Business Analyst and a Consultant in the same
corridor look like two people, which is the entire point.

### The four moments that matter

1. **Downed.** The single most character-defining animation in the game. A person
   on the floor of an office, not a ragdoll pose: they roll onto a hip, they keep
   one hand on the wound, they look at whoever is coming.
2. **Revive.** Both characters on the floor, one working. It should be the most
   human thing in the game and it takes 4 seconds, which is a long time.
3. **The first sprint after a firefight.** The stumble is in the blend, not in an
   animation: `SprintHunch` comes up over 0.4 s and the weapon lowers.
4. **Extraction winch.** 30 seconds of being lifted. Hands on the cable, feet off
   the deck, and the body reacting to the rotor wash.

---

## 3. Cosmetic slots and options

Twelve slots, 51 shipped options. Every option is cosmetic-only, and every one is
an *office* object: this is a building full of people at work, and the fantasy is
that a normal person walked out of a lift into a warzone wearing exactly what
they came in with.

| Slot | Options | Notes |
| --- | --- | --- |
| Jacket | 8 | wool overcoat, padded, softshell, blazer, none. The strongest silhouette read at 20 m |
| Fleece | 3 | company-branded top layer: the internal-branding joke played entirely straight |
| Hi-vis | 3 | vest, full jacket, none. Facilities-flavoured and extremely visible |
| Trousers | 4 | suit, chinos, cargo, jeans |
| Shoes | 4 | good boots, trainers, formal, work boots. Determines the *sound* of your footsteps? No: the surface does. Shoes is visual only |
| Lanyard | 6 | the building's identity, and the strongest close-range tell |
| Badge | 5 | the card in the lanyard, with the tier printed on it |
| Bag | 5 | messenger, backpack, laptop bag, gym bag, none |
| Headwear | 4 | nothing, cap, hood up, hard hat on the executive floor (a real joke, played straight) |
| Gloves | 3 | none, work gloves, leather |
| Watch | 3 | and a tan line for the one without a watch |
| Bag tag | 3 | company travel tag |

These twelve slots and 51 options are `COSMETIC_SLOTS` and `COSMETIC_OPTIONS`
in `Tools/Python/onebraham/build_characters.py`; the registry asset is generated
from them, the compiled-in default in `UOneBrahamCharacterDataAsset` carries the
same geometry, and `check_cosmetic_ids.py` compares all of it against this table
on every run. The starter outfit a new profile unlocks is drawn from these
options too — it once was not, and a fresh player owned seven cosmetics the
wardrobe has never contained.

Unlock rules: eleven slots are unlocked by playing (level, challenges, mode
completions). Nothing is purchasable that is not also earnable, and the wardrobe
says so on screen:

> *Cosmetics are cosmetic. Nothing here changes how you play.*

---

## 4. Competitive integrity

This is a PvP extraction shooter with no respawns in its primary mode. Cosmetic
variety is therefore a *fairness* feature and it is governed by hard rules:

| Rule | Enforcement |
| --- | --- |
| No cosmetic changes hit volume, hit zone size or visibility of a hit zone | `CompetitiveBannedCosmeticIds` in the registry, checked by `validate_project.py` |
| No cosmetic changes movement, speed, stance or noise | same check, plus a per-archetype review before each release |
| No cosmetic is a "black-on-black" silhouette advantage | a value-range check on every cosmetic's albedo in the FX review pass |
| Cosmetic keys are never gameplay-critical | the lanyard, the tier badge and the hi-vis vest are informational only, and both teams can see all of them |
| No cosmetic is region- or event-locked in a way that blocks a player from reading an enemy | event cosmetics are always the same silhouette family as something earnable |

The middle rule is the one that gets argued about in every shooter that ships:
the answer here is a value check rather than a debate, and the check is in the
validator.

---

## 5. Cloth

Chaos Cloth, one asset per garment, on the jacket, trousers and lanyard.

| Rule | Value |
| --- | --- |
| Cloth assets simulating at once | 8 |
| Max distance for simulation | 25 m (beyond that, cached pose) |
| Iterations | 2, solver frequency 30 Hz |
| Lanyard | worst-case simulation in the game: tiny, fast, high-frequency |
| Hi-vis vest | simulated at half rate, because it is flat and it does not matter |

The lanyard is the deliberate flourish. It moves when a person moves, it hangs
when they are still, and it is the single clearest indicator that the thing
running towards you is a person rather than a pattern.

---

## 6. Body, face and hair

* **Bodies:** six variants sharing `SK_OB_Body_0_Skeleton`, MetaHuman-class,
  heights 165–190 cm. `HeightOffsetCm` (±8 cm) is applied as a cosmetic skeletal
  scale and must not change the hit volume — the capsule is authoritative.
* **Faces:** MetaHuman-based, six archetypes, ages 24–52, mixed heritage reading
  as a real London office. Rest expressions only: a face that looks "characterful"
  in the character creator looks like a mask at 15 m.
* **Hair:** grooms via strands, one shared strand set and six presets. Hair is the
  second-most expensive cosmetic system in the game; the strand count and
  simulation settings are in `Docs/18_Testing_Performance.md`.
* **First-person arms:** separate meshes per archetype, cuffed and sleeved
  individually, because a first-person view of a rolled-up cuff is how the player
  knows who they are.

---

## 7. Interaction montages

Eight montages, loaded by name at runtime by
`UOneBrahamInteractComponent::PlayInteractionMontage`. They live in
`/Game/OneBraham/Characters/Shared/Anims` (created by `build_characters.py`, named
by `common.CHARACTER_ANIMS`), they are played on the upper-body slot so the
player keeps their lower-body locomotion, and each one is also played on the
first-person arms mesh so the hands visibly reach for the button.

**Table 4 — the interaction montage contract**

| Asset | Used for | Length | Notes |
| --- | --- | --- | --- |
| `AM_OB_Interact_Door` | every `OB.Interaction.Door.*` tag: opening, closing, breaching | 1.1 s | starts at the handle, ends with the arm back at the weapon. The most-played montage in the game; it is worth the extra polish. |
| `AM_OB_Interact_Terminal` | override terminals, the extraction console, keycard readers with a screen | 12 s | loops while the hold runs, so it must loop cleanly. The 12 s figure is the solo override time; the paired 3 s override plays the first second and blends out. |
| `AM_OB_Interact_LiftPanel` | lift call panels only | 0.9 s | the only montage with a real button press: a deliberate, single, index-finger stab, because calling a lift is the one interaction nobody rushes. |
| `AM_OB_Interact_Revive` | reviving a downed squadmate | 4.5 s | loops for the revive duration (see `OneBrahamConstants::ReviveSeconds`). Needs a clear "pulling them up" read from first person, because the player is blind to their own surroundings while it plays. |
| `AM_OB_Interact_Execute` | finishing a downed enemy | 2 s | the smallest montage in the game and the coldest: a single step in, the weapon already level, and no flourish at all. It is held for `UOneBrahamDownedComponent::ExecuteSeconds`, and the victim is watching it, so the read has to be legible from the floor. |
| `AM_OB_Interact_Keycard` | badge readers, the turnstiles, the Skydeck gate | 0.8 s | a swipe, not a press. Left hand, so the weapon hand never leaves the grip. |
| `AM_OB_Interact_Loot` | searchable bodies and dropped bags | 2.2 s | a crouch-and-reach: the interaction takes a full second longer than any other, and the animation is where the player feels that cost. |
| `AM_OB_Interact_Radio` | the extraction beacon, the winch control | 1.4 s | the hand comes off the weapon to key a radio. Deliberately the most exposed pose in the game. |

**A missing montage is not an error.** The interaction still happens — the
animation is presentation, the state change is the gameplay — and the load
failure is silent by design, because a red error in a shipping build tells the
player something they cannot act on. What catches a missing montage is the
content checklist in `Docs/20_Release_Checklists.md`, not the runtime.

**Rules for the animation team**

1. Every montage starts and ends at the same pose as the idle so the blend does
   not pop. The blend-out is 0.25 s, hard-coded in the interact component.
2. The upper-body slot only: legs are owned by locomotion and by the lean system.
3. No montage may move the root. Position comes from the capsule, always.
4. Left hand wherever possible. A montage that takes the right hand off the
   weapon reads as a player who has stopped fighting, and in a PvP game that
   reads as an invitation.

---

## 8. Anti-cheat and appearance

Cosmetic state is server-authoritative. A client cannot grant itself a cosmetic,
and a modified client that renders a cosmetic it does not own sees only its own
screen change. `Docs/07_Multiplayer_Voice.md` covers the client-authority
boundary in full; the relevant rule here is that *appearance is validated the
same way as damage*, because in a game where a player's gear can be read off a
body at 20 m, appearance is gameplay information.

---

## 9. What you can interact with

Every interactable in ONE BRAHAM goes through one interface
(`IOneBrahamInteractionInterface`) and one trace, and this section is the list.
It exists because the list was previously only in the code, and the code had a
hole in it the size of a person: revive and execute were implemented, documented
in three places, and impossible to reach, because the downed player did not
implement the interface and the interact ray could not see a body on the floor.
Two tables, both checked by `check_interaction.py` against the classes, the
gameplay tags, the hold constants, the montage table above and each class's own
declaration of the interface's eleven events. The last of those is the check that
matters most here: an interface method spelled slightly differently is not an
override, it is a virtual function nothing calls, and there is no compiler in
this loop to say so. The winch console
is the row that shows what the check is for: `ServerCallExtraction` and the whole
five-state extraction sequence had no caller anywhere in the project until the
console was placed on the Skydeck, so the win condition of ASCENT was unreachable
and the only way a match could end was elimination.

**Table 5 — the interaction table**

| Actor | Prompt | Tags | Hold | Reach | Montage |
| --- | --- | --- | --- | --- | --- |
| `AOneBrahamAccessZone` | "Open {zone}", "Use badge - {zone}", "Hold E - override {zone}", or the refusal ("Staff badge required") | `OB.Interaction.Door`, `OB.Interaction.Door.Secure`, `OB.Interaction.Terminal` | tap with the badge, override 12 s (`UOneBrahamAccessZoneComponent::OverrideSeconds`), 3 s (`UOneBrahamAccessZoneComponent::OverrideTwoPersonSeconds`) at the paired panel | 240 cm (`AOneBrahamAccessZone::OBDoorInteractionRange`) | `AM_OB_Interact_Door`, `AM_OB_Interact_Terminal` |
| `AOneBrahamBreachableWall` | "Plant breaching charge", or "Partition - breaching charge required" | `OB.Interaction.Door.Breachable` | 1.6 s (`AOneBrahamBreachableWall::PlantSeconds`) | 260 cm (`AOneBrahamBreachableWall::OBBreachInteractionRange`) | `AM_OB_Interact_Door` |
| `AOneBrahamKeycard` | "Take {tier} badge - issued floor {n}", or "Take {tier} ({name})" for a dropped card | `OB.Interaction.Keycard` | tap | 210 cm (`AOneBrahamKeycard::CardPickupRangeCm`) | `AM_OB_Interact_Keycard` |
| `AOneBrahamOverrideTerminal` | "Hold E - override", "Already released", "Access panel offline", "Unlinked panel" | `OB.Interaction.Terminal` | 12 s (`UOneBrahamAccessZoneComponent::OverrideSeconds`) solo, 3 s (`UOneBrahamAccessZoneComponent::OverrideTwoPersonSeconds`) paired | 200 cm (`AOneBrahamOverrideTerminal::OBTerminalInteractionRange`) | `AM_OB_Interact_Terminal` |
| `AOneBrahamPickupBase` | "Take {weapon}" | `OB.Interaction.Weapon` | tap | 220 cm (`AOneBrahamPickupBase::PickupRangeCm`) | `AM_OB_Interact_Door` |
| `AOneBrahamExtractionTerminal` | "Hold E - call the winch", or the state of the winch: "Called - helicopter inbound", "Winch live - hold the pad", "Winch aborted - stands by" | `OB.Interaction.Extraction` | 1.4 s (`AOneBrahamExtractionTerminal::CallHoldSeconds`) | 220 cm (`AOneBrahamExtractionTerminal::InteractionRangeCm`) | `AM_OB_Interact_Radio` |
| `AOneBrahamCharacter` | "Hold E - revive {name}" for a squadmate, "Hold E - finish {name}" for an enemy | `OB.Interaction.Revive`, `OB.Interaction.Execute` | 4.5 s (`OneBrahamConstants::ReviveSeconds`), 2 s (`UOneBrahamDownedComponent::ExecuteSeconds`) | 220 cm (`OneBrahamConstants::InteractRangeCm`) | `AM_OB_Interact_Revive`, `AM_OB_Interact_Execute` |

Three rules the table encodes, and the reasons they are rules rather than
conventions:

* **The verb is decided in one place.** `UOneBrahamDownedComponent::GetInteractionFor()`
  answers Revive, Execute or nothing; the prompt, the tag, the hold time and the
  server's own gate all read that one answer. A prompt the server would refuse is
  the worst bug an interaction system can have, because the player experiences it
  as lag.
* **A person is scenery until they are on the carpet.** The player capsule's
  response to the `Interactable` channel is `Overlap`, so a standing colleague
  never hides the door behind them, and the hitbox profile ignores the channel
  entirely so a body mesh never hides one either. In the downed state the capsule
  switches to `Block` (`AOneBrahamCharacter::ApplyLifeStateVisuals`), which is
  what makes a colleague aimable - and it switches back the moment they stand up.
* **The trace steps through one penetrable surface, and only one.** Cubicle
  walls, plasterboard and desk partitions are stepped through (the same
  `Penetrable` test the weapon trace uses for `bCanPenetrateCover`), so a
  squadmate can be revived through a partition, which is the best clip the game
  has. Doors, terminals and bodies are never stepped through: if what you are
  looking at already answers to the interface, it is what you are interacting
  with.

**Table 6 — declared, and not yet emitted**

Seven interaction tags are declared in the native registry and mirrored in
`Config/DefaultGame.ini`, and nothing in the game emits them yet. They are listed here rather than deleted because each is
the designed vocabulary of content that has not been built, and because the
montage contract in section 7 already reserves an animation family for several of
them. `check_interaction.py` fails if a tag is in neither this table nor Table 5,
if a tag is in both, or if this table's count changes without somebody deciding
which side of the line the tag belongs on.

| Tag | Why nothing emits it | What it is waiting on |
| --- | --- | --- |
| `OB.Interaction.Lift` | the two lift cores are access zones, so their doors return `OB.Interaction.Door.*` and the lift-panel montage is unreachable | a lift actor (call panel, car, floor indicator) rather than a door in the core |
| `OB.Interaction.Ammo` | there are no ammo pickups: resupply is the looted weapon and the magazine in it | an ammo box pickup actor |
| `OB.Interaction.Utility` | utility is granted by the loadout and found as a floor pickup, not picked up as utility | a utility pickup actor |
| `OB.Interaction.Medkit` | the meal deal is used from the inventory wheel, never off a surface | a placed consumable |
| `OB.Interaction.Body` | nothing is lootable: a corpse is a ragdoll, not a container | the death-bag pass (searchable bodies and dropped bags) |
| `OB.Interaction.HoldToUnlock` | the HUD verb category for a hold: `RequiresHold()` already answers this in C++, so nothing emits the tag | a decision - either the widget reads it or the tag goes |
| `OB.Interaction.TapToUse` | the same, for a tap | the same decision |

The two HUD rows are the interesting ones. They are the only entries here that
are not waiting on content: they are a second, redundant way of saying something
the interface already says, and the correct resolution is probably to delete
them. They are kept for now because they are in the copy the HUD's Blueprint
layer was briefed with (`Docs/08` section 3), and deleting a tag a widget author
was told to use is a conversation, not a commit.

