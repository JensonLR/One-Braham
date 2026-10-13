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
| Business Analyst | `OB.Character.Archetype.Analyst` | 5 | 0 | +0 cm | `Vox_Analyst_London` | Measuring everything, out loud, because it helps | Brisk, upright, efficient. Walk 1.05, arms 0.90, lean 0.35 |
| Graduate / Intern | `OB.Character.Archetype.Graduate` | 4 | 3 | −3 cm | `Vox_Graduate_London` | Trying hard not to be the one who panics | Hurried, slightly loose. Walk 1.12, arms 1.15, lean 0.70 |
| Finance | `OB.Character.Archetype.Finance` | 8 | 1 | +4 cm | `Vox_Finance_London` | Unbothered. Has seen worse on a Tuesday | Economical, heavy. Walk 0.94, arms 0.80, lean 0.25 |
| IT / Operations | `OB.Character.Archetype.ITOps` | 9 | 2 | −1 cm | `Vox_ITOps_London` | Knows exactly what the building is doing and is annoyed about it | Precise, hunched at the screen. Walk 0.98, arms 0.85, hunch 0.55 |
| Facilities / Security | `OB.Character.Archetype.Facilities` | 15 | 4 | +2 cm | `Vox_Facilities_London` | Responsible for everyone, paid for none of it | Working gait, heavy boots. Walk 1.00, arms 0.95, composure 0.75 |
| Consultant | `OB.Character.Archetype.Consultant` | 7 | 5 | +6 cm | `Vox_Consultant_London` | Here for six weeks, noticeably better at this than expected | Relaxed, athletic, never rushed. Walk 0.96, arms 1.05, lean 0.40 |

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

## 7. Anti-cheat and appearance

Cosmetic state is server-authoritative. A client cannot grant itself a cosmetic,
and a modified client that renders a cosmetic it does not own sees only its own
screen change. `Docs/07_Multiplayer_Voice.md` covers the client-authority
boundary in full; the relevant rule here is that *appearance is validated the
same way as damage*, because in a game where a player's gear can be read off a
body at 20 m, appearance is gameplay information.
