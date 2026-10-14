# ONE BRAHAM — character system

Movement, health, damage, downing, reviving, ragdoll and the character data
asset. Implementation: `Source/OneBraham/Characters/`.

The design constraint that shapes all of it: **people carry ordinary weight.**
There is no sliding, no bhop, no wall-running, no jump-crouch exploit worth
learning. The skill in this game is positioning, patience and communication, and
the movement system exists to keep those three things in charge.

---

## 1. `AOneBrahamCharacter`

| Property | Value | Notes |
| --- | --- | --- |
| Capsule | radius 34 cm, half-height 88 cm | 176 cm standing; the capsule is authoritative for hits, never the mesh |
| Crouched capsule | radius 34 cm, half-height 56 cm | |
| Max health | 100 | |
| Max armour | 50 | |
| Archetype | `FGameplayTag ArchetypeTag` | `OB.Character.Archetype.*`, cosmetic only, plus the animation personality |
| `bIsDowned` / `bIsEliminated` | replicated | |
| Components | `HealthComponent`, `DownedComponent`, `RagdollComponent`, `WeaponComponent`, `InventoryComponent`, `InteractComponent`, `ProximityVoiceComponent` | |

Every life begins in `ServerOnRespawned`, and the order in it is the rule: the
health is reset, the inventory goes back to the standard-issue kit
(`ServerResetInventory`, which clears the keycards and the override tool with it),
and only then is the weapon component told to re-arm. Utility therefore does not
accumulate across lives — a player who respawns eight times does not eventually
carry twenty-four flashbangs — and the loadout is applied on top of one of each
rather than on top of whatever the last life left behind. The other end of a life
is `ServerOnDied`, which drops the kit as pickups and the keycard with it.

The character does not own game rules. It owns a body: a capsule, a camera, a
mesh and a set of components. Everything that can be argued about — damage, downed
state, extraction eligibility — lives in a component that can be tested on its
own.

The constructor loads `/Engine/BasicShapes/Capsule.Capsule` as a generation-time
placeholder and the archetype's skeletal mesh when one exists, so a fresh clone
has a walkable, shootable, audible person with no content at all. That is
deliberate: it means the *game* can be tested before the *art* exists, which is
the only order that ever works.

---

## 2. `UOneBrahamCharacterMovementComponent`

| Movement | Speed |
| --- | --- |
| Walk (slow walk key) | 190 cm/s (`UOneBrahamCharacterMovementComponent::WalkSpeed`) |
| Jog (default) | 400 cm/s (`UOneBrahamCharacterMovementComponent::JogSpeed`) |
| Sprint (forward only, weapon down) | 605 cm/s (`UOneBrahamCharacterMovementComponent::SprintSpeed`) |
| Crouch | 165 cm/s (`UOneBrahamCharacterMovementComponent::CrouchSpeed`) |
| Crouch and aim | 140 cm/s (`UOneBrahamCharacterMovementComponent::CrouchedAimingSpeed`) |
| Aim down sights | 250 cm/s (`UOneBrahamCharacterMovementComponent::AimingSpeed`) |
| Slide | decays from `SprintSpeed` to `WalkSpeed` over `SlideDuration` |
| Falling | whatever you left the edge with, up to `SprintSpeed * 0.98` |
| Downed crawl | 60 cm/s (`UOneBrahamCharacterMovementComponent::DownedCrawlSpeed`) |

Every row is a `UOneBrahamCharacterMovementComponent` property read by
`GetMaxSpeed()`, and there are no direction multipliers anywhere in the movement
path: a strafe and a jog are the same speed, because in a game where the fights
are in corridors, the moment strafing is punished is the moment everyone starts
walking forwards into rooms. This table used to say 220 / 380 / 620 for walk, jog
and sprint, "0.55× current" for aiming, and 0.90× / 0.78× for strafing and
backpedalling - five numbers that appear nowhere in the code, in the table a
designer reads before tuning movement. Crouch-and-aim used to be written as
"165, 140 crouched and aiming" in one cell; the 140 was `CrouchSpeed * 0.85f`
written inside `GetMaxSpeed()`, which is a number no tuning pass can find, so it
is now `CrouchedAimingSpeed` and the table quotes it like every other row.

**Every speed in this table is quoted with the name of the property that defines
it, and `check_interaction.py` reads these three tables and fails if a number's
unit is not followed by a source or if the source disagrees with the code.** That
is the rule the old 4.0 s revive in section 4 needed and did not have.

| Physics | Value |
| --- | --- |
| Gravity | the engine's own `DefaultGravityZ` (Config/DefaultEngine.ini); no scale |
| Jump Z velocity | 380 cm/s (`UOneBrahamCharacterMovementComponent::JumpZVelocity`) |
| Air control | 0.14 (`UOneBrahamCharacterMovementComponent::GroundAirControl`) |
| Braking deceleration, walking | 2000 cm/s² (`UOneBrahamCharacterMovementComponent::GroundBrakingDeceleration`) |
| Max step height | 45 cm (`UOneBrahamCharacterMovementComponent::MaxStepHeight`) |
| Walkable floor angle | 46° (`UOneBrahamCharacterMovementComponent::SetWalkableFloorAngle`) |
| Ledge tolerance | 12 cm (`UOneBrahamCharacterMovementComponent::PerchRadiusThreshold`) |
| Perch additional height | 8 cm (`UOneBrahamCharacterMovementComponent::PerchAdditionalHeight`) |

Two rows here are deliberately unitless: gravity (a config value the project
never scales) and air control (a dimensionless multiplier). The checker covers
durations, distances and speeds - the units a wrong number *crashes into* - and
says so in its docstring rather than pretending to check everything.

The gravity row used to claim a scale of 1.75, "weight, not float". Nothing in
the project has ever set `GravityScale`, so the game has always fallen at the
engine's gravity and the jump was tuned against it. The 1.75 was a design note
that never reached the code, which makes it the same kind of sentence as a
weapon that is in the design document and not in the game.

The numbers are chosen for one feel: **committing to a movement decision should
be hard to undo.** Braking at 2,000 cm/s² means a full-speed sprint takes about
0.30 s to stop — long enough that a player who runs into a room has already
decided to be in it.

Sliding and mantling **are** in the game, bound to their own input actions and
implemented as movement states, not as animations: a slide is 0.85 s
(`SlideDuration`) off a sprint and drains it, and a mantle reaches 95 cm
(`MantleReach`) over 0.62 s (`MantleDuration`) with the weapon down. This
paragraph claimed for most of the project's life that they were "deliberately
absent", which was a sentence rather than a fact about a build in which
`Input_Slide` and `Input_Mantle` were both bound. Dodging and vaulting are absent:
the stairwells are meant to be walked.

---

## 3. `UOneBrahamHealthComponent`

```cpp
float Health;               // 100
float MaxHealth;
float Armour;               // 50
float MaxArmour;
bool  bArmourBroken;
bool  bInvulnerable;

void ApplyDamage(float Amount, AActor* Causer, const FOneBrahamDamageReport& Report, AController* Instigator);
void  ApplyHeal(float Amount, AActor* Healer);
void  ApplyArmour(float Amount);
```

Rules:

1. **Armour absorbs 60%** of incoming damage and loses the absorbed amount until
   it breaks. A 30-damage shot against full armour takes 12 HP and 18 armour.
2. **No regeneration.** Health comes from a medkit or a colleague. A player who
   survives a fight at 14 HP is at 14 HP for the rest of it, and that is the
   design.
3. **Degenerate damage kills.** A downed player is at 1 HP, so any hit at all
   finishes them, and there is no damage multiplier to get wrong: the deliberate
   version is the execution (section 4), and the accidental version is the stray
   round that lands on the carpet.
4. **Friendly fire is on** in ASCENT (squad-level: you can shoot your own
   squadmate) at **0.5×** and it never downs a squadmate — it drops them to 1 HP,
   which is a punishment without a grief. In FIRE DRILL it is off.
5. **The purge is unattributed.** `Causer` is null, so the kill feed reads
   "Purge" and no squad is credited. The building does not get kills.

Every damage application runs through one function. There is no second path,
because a second path is how a shooter ends up with a weapon that ignores armour.

---

## 4. `UOneBrahamDownedComponent`

| State | Duration |
| --- | --- |
| Bleed-out | 60 s (`OneBrahamConstants::BleedOutSeconds`) |
| Revive | 4.5 s (`OneBrahamConstants::ReviveSeconds`), a hold |
| Execution | 2 s (`UOneBrahamDownedComponent::ExecuteSeconds`), a hold |
| Second down while bleeding out | immediate death |
| Crawl speed | 60 cm/s (`UOneBrahamCharacterMovementComponent::DownedCrawlSpeed`) |
| Revive outcome | 40 HP (`UOneBrahamDownedComponent::ReviveHealth`), no armour |
| Invulnerability on revive | 1.25 s (`UOneBrahamDownedComponent::ReviveInvulnerabilitySeconds`) |
| Movement allowed | crawl only; no firing, no weapon switching, no interaction |
| What you can do | talk, look, and call for help (`V_DOWNED_CALL_HELP`, then `V_DOWNED_QUIP`) |

This table described a different game for most of the project's life: a 45-second
bleed-out, a 4-second revive, a 45 cm/s crawl, a call for help with a line id that
has never existed, and 30 HP back on revive. All of it was written down somewhere
as if it were code, and none of it matched `OneBrahamConstants`, which is now
where three of the four numbers live. `check_interaction.py` compares the numbers
in the interaction table in Docs/13 against the constants; the constants are the
source and this table quotes them.

Reviving is a **hold**, not a tap, and the reviver is fully exposed while doing
it. That single rule is why revives in this game are dramatic: the person helping
cannot defend themselves, so a third squadmate has to hold the room. The design
goal is that reviving should be a decision about trust.

The hold is also **interruptible**, and that is the point rather than a
limitation: let go, walk away or get shot and the bar empties (it decays at twice
the rate it fills, so nobody pumps a revive), and the person on the floor watches
it fall back to nothing.

On revive: 40 HP (`UOneBrahamDownedComponent::ReviveHealth`), no armour, and a
1.25-second invulnerability window so that a revive in the open is not a free
kill for the enemy holding the corridor. Then the loudest voice line in the game:
`V_REVIVED` — "Up. Thank you. Let's move."

A downed player is **the target of an interaction, never the initiator**: you
cannot open a door, use a terminal or pick anything up from the carpet. Both the
client's trace and the server's validation check that, because a downed player is
at 1 HP and `IsAlive()` is perfectly happy about it.

Downed players can be **finished**: hold interact on an enemy for 2 s and they
are gone, credited to you rather than to whoever put them down. What you cannot
do is pick them up. Carrying a body is not in this game — somebody on the floor is
a problem you solve where they are. (The component used to carry a
`bCarryingBody` flag, a `IsCarryingBody()` getter and a 0.5× speed penalty for a
feature that does not exist; the flag could never be set by anything, so the
penalty could never apply, and all three were deleted.)

---

## 5. `UOneBrahamRagdollComponent`

Physics-driven, not animation-driven:

| Property | Value |
| --- | --- |
| Death impulse | from the killing shot's direction and energy |
| Blend to physics | 0.18 s |
| Bone collapse | 0.35 s, heavy on the knees |
| Sleep threshold | 1.2 s of near-zero velocity, then `PutRigidBodyToSleep` |
| Linger | 45 s, then fade at 8 m or immediately beyond |
| Maximum live ragdolls | 12 |

**Clip-friendly deaths** are a stated requirement, so ragdolls are tuned for
readability rather than comedy: no double-bounces, no spinning, no sticking to
geometry. The `CollisionProfile` is `OB_Ragdoll`: it collides with the world, with
cover and with other ragdolls, and is ignored by bullets, by the navmesh, and by
other players' movement. A body must never be able to block a doorway or eat a
shot meant for a person.

---

## 6. `UOneBrahamCharacterDataAsset`

One registry asset (`DA_CharacterRegistry`) holds all six, rather than one asset
per archetype - the wardrobe, the deploy screen and the character all read from
exactly one place, and there is no ordering to get wrong. Fields:

One `FOneBrahamArchetypeDefinition` per archetype, in the registry's
`Archetypes` array. The struct is the single source for the deploy screen, the
wardrobe and the character:

| Field | Type | Notes |
| --- | --- | --- |
| `ArchetypeTag` | `FGameplayTag` | `OB.Character.Archetype.*`, the identity used everywhere |
| `DisplayName`, `Biography` | `FText` | the deploy screen; one dry line of biography |
| `HomeFloor` | `int32` | flavour, and the floor the voice sets reference |
| `BodyMesh`, `FirstPersonMesh` | `TSoftObjectPtr<USkeletalMesh>` | third person and cuffed first-person arms |
| `AnimInstanceClass` | `TSubclassOf<UAnimInstance>` | one `ABP_OneBrahamCharacter` for all six |
| `BodyVariant`, `HeightOffsetCm` | `int32`, `float` | cosmetic, non-gameplay, clamped |
| `FaceMaterial`, `HairMaterial`, `HairPresetId` | soft pointers, `FName` | shared strand set, per-archetype preset |
| `DefaultCosmetics` | `TMap<FName, FName>` | the starting outfit, slot id to cosmetic id |
| `Personality` | `FOneBrahamAnimationPersonality` | the animation personality |
| `VoiceSetId`, `VoicePitchRange` | `FName`, `FVector2D` | `Vox_*_London`, pitched per player |
| `MovementFoleySet` | `TSoftObjectPtr<USoundBase>` | lanyard swing, bag rustle, ID rattle |
| `UnlockRuleId`, `RequiredLevel`, `UnlockAchievementId` | metadata | all six ship unlocked; cosmetics are the only monetised content |

**`FOneBrahamAnimationPersonality` must never touch recoil patterns, spread or
damage.** It changes how the weapon mesh sits in the hands - walk rate, arm
swing, idle lean, how far he hunches over a sprint. Anything more is a gameplay
advantage sold as a personality, and the validator refuses it.

Loaded softly on deploy and cached for the match, so a 12-player match does not
pay for six skeletal meshes it will never see.

---

## 7. Animation

One animation blueprint, `ABP_OneBrahamCharacter`, driven by the twelve
parameters in `Docs/13_Characters_Clothing.md` §2. It is layered:

```
Base locomotion (blend space: speed × direction, 8-way)
  └ Crouch layer          (weight: crouch alpha)
  └ ADS layer             (weight: aim alpha, spine + arms)
  └ Weapon layer          (upper body: fire, reload, inspect — from the weapon component)
  └ Downed layer          (replaces base at alpha 1)
  └ Additive archetype personality layer  (twelve parameters)
  └ Foot IK + hand IK     (traces, 2 per frame max)
```

Rules that keep it performant: no full-body IK, IK traces at most every other
frame and only for the local player and any character within 12 m, and no
anim-notify-driven gameplay. Gameplay never lives in an animation notify, because
a notify that does not fire on a dedicated server is a gameplay bug that only
appears in multiplayer.

---

## 8. Hit zones and feedback

| Zone | Bones | Multiplier |
| --- | --- | --- |
| Head | `head`, `neck` | 2.40× |
| Torso | `spine_01`…`spine_03`, `pelvis` | 1.00× |
| Arms | `upperarm_*`, `lowerarm_*`, `hand_*` | 0.75× |
| Legs | `thigh_*`, `calf_*`, `foot_*` | 0.70× |

The bone name decides the zone, so an artist who adds a bone cannot
accidentally create an un-hittable region: anything unmatched falls back to the
torso multiplier and logs once in development builds.

Feedback on hit: a directional hit marker, a short impact sound, a body flinch in
the mesh (cosmetic, never affecting aim), and a blood decal only on the surface
behind the hit — never a particle spray in front of the camera, because a
first-person blood effect that obscures the screen is a mechanic nobody asked for.
