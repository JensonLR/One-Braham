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
| Archetype | `EOneBrahamArchetype` | cosmetic only, plus the animation personality |
| `bIsDowned` / `bIsEliminated` | replicated | |
| Components | `HealthComponent`, `DownedComponent`, `RagdollComponent`, `WeaponComponent`, `InventoryComponent`, `InteractComponent`, `ProximityVoiceComponent` | |

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

| Movement | Speed (cm/s) |
| --- | --- |
| Walk | 220 |
| Jog (default) | 380 |
| Sprint (forward only, weapon down) | 620 |
| Crouch | 165 |
| ADS | 0.55× current |
| Strafe | 0.90× base |
| Backpedal | 0.78× base |
| Downed crawl | 45 |

| Physics | Value |
| --- | --- |
| Gravity scale | 1.75 (weight, not float) |
| Jump Z velocity | 380 |
| Air control | 0.22 |
| Braking deceleration, walking | 1,800 |
| Max step height | 42 cm |
| Walkable floor angle | 46° |
| Ledge tolerance | 12 cm |

The numbers are chosen for one feel: **committing to a movement decision should
be hard to undo.** Braking at 1,800 cm/s² means a full-speed sprint takes about
0.34 s to stop — long enough that a player who runs into a room has already
decided to be in it.

Sliding, dodging, vaulting and mantling are deliberately absent. The stairwells
are meant to be walked.

---

## 3. `UOneBrahamHealthComponent`

```cpp
float Health;               // 100
float MaxHealth;
float Armour;               // 50
float MaxArmour;
bool  bArmourBroken;
bool  bInvulnerable;

float ApplyDamage(float Amount, EOneBrahamDamageType, AActor* Causer, FName HitBone, bool bHeadshot);
void  ApplyHeal(float Amount, AActor* Healer);
void  ApplyArmour(float Amount);
```

Rules:

1. **Armour absorbs 60%** of incoming damage and loses the absorbed amount until
   it breaks. A 30-damage shot against full armour takes 12 HP and 18 armour.
2. **No regeneration.** Health comes from a medkit or a colleague. A player who
   survives a fight at 14 HP is at 14 HP for the rest of it, and that is the
   design.
3. **Degenerate damage kills.** Downed players take 2.5× damage, so finishing
   somebody is quick and unambiguous.
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
| Bleed-out | 45 s, ticking `HealthComponent` to zero |
| Revive | 4.0 s of uninterruptible interaction |
| Second down while bleeding out | immediate death |
| Crawl speed | 45 cm/s |
| Movement allowed | crawl only; no firing, no weapon switching, no interaction |
| What you can do | talk, look, and call for help (`V_HELP_CALL`) |

Reviving is a **hold**, not a tap, and the reviver is fully exposed while doing
it. That single rule is why revives in this game are dramatic: the person helping
cannot defend themselves, so a third squadmate has to hold the room. The design
goal is that reviving should be a decision about trust.

On revive: 30 HP, no armour, a 1.5-second grogginess window with reduced
movement, and the loudest voice line in the game.

Downed players can be **finished** — 2.5× damage in a 6-second bleed-out window —
and can be **carried**? No. Carrying a body is not in this game. Somebody on the
floor is a problem you solve where they are.

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

One asset per archetype (`DA_Character_<Archetype>`), and the cast registry
(`DA_CharacterRegistry`) holds all six. Fields:

| Field | Type | Notes |
| --- | --- | --- |
| `Archetype` | `EOneBrahamArchetype` | |
| `DisplayName` / `Description` | `FText` | the deploy screen |
| `Tag` | `FGameplayTag` | `OB.Character.Archetype.*` |
| `BodyMesh` | `TSoftObjectPtr<USkeletalMesh>` | |
| `FaceMesh`, `HairGroom` | soft pointers | |
| `AnimBlueprint` | soft class pointer | one `ABP_OneBrahamCharacter` for all six |
| `FirstPersonArms` | soft pointer | archetype-specific |
| `VoiceSetId` | `FName` | `Vox_*_London` |
| `HomeFloor` | int | flavour, shown on the deploy screen |
| `BodyVariantIndex`, `HeightOffsetCm` | int / float | cosmetic, non-gameplay |
| `WalkRateScale`, `ArmSwingScale`, `IdleLeanScale`, `SprintHunch` | float | the animation personality |
| `WeaponHandlingScale` | float | cosmetic *feel* only: it does not touch recoil |
| `SignatureLineIds` | `TArray<FName>` | the archetype's variant voice lines |
| `CosmeticSlotDefaults` | map | the starting outfit per slot |

**`WeaponHandlingScale` must never touch recoil patterns, spread or damage.** It
changes how quickly the weapon mesh settles in the hands. Anything more is a
gameplay advantage sold as a personality, and the validator refuses it.

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
