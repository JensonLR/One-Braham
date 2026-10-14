# ONE BRAHAM — weapon system

Ten weapons, one table. Every number in this document is read from
`Tools/Python/onebraham/build_weapons.py` — the same numbers that are written
into the data assets — because a specification that can drift from the
implementation is a specification nobody uses.

Implementation: `Source/OneBraham/Weapons/` (`AOneBrahamWeaponBase`,
`UOneBrahamWeaponComponent`, `UOneBrahamInventoryComponent`,
`AOneBrahamProjectileBase`), data in `UOneBrahamWeaponDefinition`.

---

## 1. The armoury

| # | Id | Weapon | Class | Slot | Damage | RPM | Mag | Reserve | TTK at 10 m vs 100 HP |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `W_Pistol` | P99 Sidearm | Pistol | Sidearm | 32.0 | 420 | 15 | 45 | 0.43 s (4 shots: 50 armour + 42 HP) |
| 2 | `W_CompactPistol` | LCP Machine Pistol | Compact Pistol | Sidearm | 21.0 | 900 | 20 | 60 | 0.40 s (8 shots) |
| 3 | `W_SuppressedPistol` | P99-S Suppressed | Suppressed Pistol | Sidearm | 26.0 | 380 | 15 | 45 | 0.63 s (5 shots) |
| 4 | `W_SMG` | MP5 9mm | SMG | Primary | 25.0 | 800 | 30 | 120 | 0.38 s (6 shots) |
| 5 | `W_PDW` | MP7 PDW 4.6mm | PDW | Primary | 22.0 | 950 | 40 | 160 | 0.38 s (7 shots, 6 through armour) |
| 6 | `W_PumpShotgun` | M870 Pump | Pump Shotgun | Primary | 17.0 × 8 | 75 | 7 | 28 | 0.00 s at 3 m; unusable at 25 m |
| 7 | `W_SemiShotgun` | Semi-Auto 12 Bore | Semi Shotgun | Primary | 12.5 × 9 | 190 | 6 | 30 | 0.32 s (one pattern at 8 m) |
| 8 | `W_Carbine556` | M4 Carbine | 5.56 Carbine | Primary | 33.0 | 740 | 30 | 120 | 0.32 s (5 shots) |
| 9 | `W_CompactAR` | MCX Compact 5.56 | Compact AR | Primary | 31.0 | 780 | 25 | 100 | 0.31 s (5 shots) |
| 10 | `W_MarksmanRifle` | L129A1 Marksman | Marksman Rifle | Primary | 78.0 | 170 | 10 | 40 | 0.35 s (2 shots, or 1 headshot) |

None of these is the best weapon. Each one is the best weapon **somewhere**:

* The **SMG** owns anything inside 15 m and tells the whole floor where you are.
* The **PDW** is the only thing that reliably defeats armour at range.
* The **carbines** are the only weapons that do not have a bad engagement.
* The **marksman rifle** owns the length of the open-plan floor and loses
  every fight in a stairwell.
* The **shotguns** own doorways and lose everything else.
* The **suppressed pistol** has the worst time-to-kill in the game and the best
  information profile — it is what you carry when you intend to be somewhere you
  should not be.

**There are no joke weapons, no office-supply weapons and no meme weapons.** This
is a hard rule of the project, not a preference.

---

## 2. Falloff, penetration, spread and recoil

### Falloff — `full_damage_range` → `min_damage_range`, down to `min_damage_multiplier`

| Weapon | Full | Min | Floor |
| --- | --- | --- | --- |
| P99 Sidearm | 1800 | 5000 | 0.62 |
| LCP Machine Pistol | 900 | 3000 | 0.45 |
| P99-S Suppressed | 1200 | 3600 | 0.50 |
| MP5 9mm | 1400 | 4200 | 0.55 |
| MP7 PDW | 1300 | 4000 | 0.52 |
| M870 Pump | 700 | 2200 | 0.22 |
| Semi-Auto 12 Bore | 600 | 1900 | 0.20 |
| M4 Carbine | 3200 | 9500 | 0.68 |
| MCX Compact | 2600 | 8000 | 0.62 |
| L129A1 Marksman | 6000 | 14000 | 0.85 |

Falloff is **linear between the two ranges**, and it is applied before hit-zone
multipliers. A pistol at 50 m does 20 damage to the chest, which is the correct
way to teach a player to close distance.

### Penetration — `penetration_power` and `penetration_damage_scale`

| Penetrating | Power | Damage retained | What it goes through |
| --- | --- | --- | --- |
| M4 Carbine | 9.0 | 0.78 | internal partitions, desk tops, most office furniture |
| MCX Compact | 9.0 | 0.75 | same |
| MP7 PDW | 6.0 | 0.72 | plasterboard partitions, screens, boxed plant |
| L129A1 Marksman | 16.0 | 0.88 | partitions, desks, **and** the server rack door |
| Everything else | 0.0 | — | nothing as a rule |

Penetration is **not** a "wall hack": the shot still has to be inside the
weapon's spread cone, it loses its listed damage, and every surface beyond the
first stops it. What it does is make cover honest — a plasterboard partition is
cover, and it is not safety.

### Spread — hip standing / crouching / moving / sprinting, ADS

| Weapon | Stand | Crouch | Move | Sprint | ADS | ADS move | Bloom/shot | Max bloom | Decay/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| P99 Sidearm | 2.6 | 1.8 | 3.8 | 6.27 | 0.28 | 1.12 | 0.28 | 3.0 | 5.0 |
| LCP Machine Pistol | 4.2 | 3.1 | 6.4 | 10.56 | 0.75 | 3.00 | 0.42 | 5.2 | 6.0 |
| P99-S Suppressed | 2.9 | 2.0 | 4.1 | 6.77 | 0.30 | 1.20 | 0.30 | 3.2 | 5.0 |
| MP5 9mm | 3.6 | 2.6 | 5.0 | 8.25 | 0.55 | 2.20 | 0.32 | 4.2 | 5.5 |
| MP7 PDW | 3.4 | 2.5 | 4.8 | 7.92 | 0.48 | 1.92 | 0.30 | 4.0 | 5.5 |
| M870 Pump | 6.5 | 5.2 | 7.8 | 12.87 | 5.60 | 22.40 | 0.0 | 0.0 | 0.0 |
| Semi-Auto 12 | 7.2 | 6.0 | 8.6 | 14.19 | 6.40 | 25.60 | 0.6 | 2.0 | 4.0 |
| M4 Carbine | 3.0 | 2.1 | 4.4 | 7.26 | 0.22 | 0.88 | 0.30 | 3.6 | 5.0 |
| MCX Compact | 3.3 | 2.3 | 4.7 | 7.76 | 0.30 | 1.20 | 0.34 | 4.2 | 5.0 |
| L129A1 Marksman | 4.4 | 3.2 | 6.0 | 9.90 | 0.08 | 0.32 | 1.4 | 6.0 | 3.2 |

Two facts jump out of that table and both are intentional:

1. **ADS is 8–20× tighter than the hip.** Aiming is not a preference in this
   game, it is the mechanic.
2. **Moving while aiming is 4× worse than standing still while aiming.** You
   cannot strafe-shoot accurately. You have to stop, and stopping is a decision.

The shotguns' ADS spread is deliberately enormous: a shotgun pattern does not
tighten, and a player who expects "aim down sights = slug" will learn otherwise
in the first minute.

### Recoil — vertical, horizontal, ADS multipliers, recovery

| Weapon | V | H | ADS V × | ADS H × | Recovery (deg/s) | Recovered | First shot × |
| --- | --- | --- | --- | --- | --- | --- | --- |
| P99 Sidearm | 0.85 | 0.35 | 0.85 | 0.90 | 30.0 | 0.95 | 0.70 |
| LCP | 1.15 | 0.55 | 1.35 | 1.05 | 26.0 | 0.90 | 0.75 |
| P99-S | 0.75 | 0.30 | 0.80 | 0.88 | 32.0 | 0.96 | 0.70 |
| MP5 | 0.95 | 0.42 | 1.00 | 0.95 | 30.0 | 0.94 | 0.72 |
| MP7 | 0.88 | 0.38 | 0.95 | 0.92 | 31.0 | 0.95 | 0.74 |
| M870 | 2.60 | 1.00 | 2.40 | 1.60 | 14.0 | 0.85 | 0.88 |
| Semi-12 | 2.10 | 0.85 | 2.00 | 1.35 | 16.0 | 0.88 | 0.85 |
| M4 | 1.10 | 0.45 | 1.10 | 0.90 | 29.0 | 0.94 | 0.68 |
| MCX | 1.28 | 0.52 | 1.25 | 0.95 | 27.0 | 0.92 | 0.70 |
| L129A1 | 2.35 | 0.95 | 2.20 | 0.75 | 20.0 | 0.90 | 0.55 |

The first-shot multiplier below 1.0 on the rifles is what makes tap-firing
correct: the first round of a burst is the accurate one, so a player who fires in
three-round groups is rewarded and a player who holds the trigger is punished.
Full-auto is never available on the marksman rifle.

---

## 3. Handling

| Weapon | ADS time (s) | Draw (s) | Holster (s) | Melee damage / range / speed × |
| --- | --- | --- | --- | --- |
| P99 Sidearm | 0.19 | 0.32 | 0.24 | 32 / 130 / 0.70 |
| LCP | 0.16 | 0.26 | 0.20 | 28 / 125 / 0.65 |
| P99-S | 0.21 | 0.34 | 0.26 | 32 / 130 / 0.70 |
| MP5 | 0.20 | 0.38 | 0.28 | 34 / 140 / 0.75 |
| MP7 | 0.19 | 0.36 | 0.27 | 34 / 140 / 0.75 |
| M870 | 0.30 | 0.55 | 0.40 | 40 / 150 / 0.90 |
| Semi-12 | 0.27 | 0.52 | 0.38 | 40 / 150 / 0.90 |
| M4 | 0.22 | 0.45 | 0.32 | 40 / 150 / 0.85 |
| MCX | 0.19 | 0.40 | 0.29 | 40 / 150 / 0.85 |
| L129A1 | 0.28 | 0.58 | 0.42 | 45 / 160 / 1.00 |

Reloads are **tactical** (round left in the chamber, faster) and **empty**
(magazine change plus a charging handle, slower) with different times — see the
`reload=(tactical, empty)` pair per weapon. The pump shotgun is **per-shell**
(0.62 s per round, `per_shell=True`), which is the only weapon in the game a
player can top up between firefights, and therefore the only one whose ammo
management is a real decision.

Melee is a last resort everywhere and a bad idea everywhere except a shotgun in a
doorway. It is timed, it is loud, and a player who relies on it will be shot.

---

## 4. Floors and loadout economy

`floors=(min, max)` gates a weapon to a floor range, applied only in FIRE DRILL
and NIGHT SHIFT — ASCENT has no floors-locked loadout, because its floors are
locked by *keycards*, not by a menu:

| Weapon | Id | Floors | Loot weight |
| --- | --- | --- | --- |
| P99, LCP, P99-S | `W_Pistol`, `W_CompactPistol`, `W_SuppressedPistol` | all | 1.0, 0.8, 0.9 |
| MP5 9mm | `W_SMG` | all | 1.1 |
| MP7 PDW | `W_PDW` | from 3 | 1.0 |
| M870 Pump | `W_PumpShotgun` | all | 1.4 |
| Semi-Auto 12 Bore | `W_SemiShotgun` | from 6 | 1.5 |
| M4 Carbine | `W_Carbine556` | all | 1.6 |
| MCX Compact | `W_CompactAR` | from 5 | 1.5 |
| L129A1 Marksman | `W_MarksmanRifle` | from 6 | 1.9 |

The same ids, weights and gates are the `WeaponSpawnTable` default in
`UOneBrahamMatchSettingsAsset` — the table the floor loot actually rolls — and
`check_weapon_ids.py` fails the build if either side moves without the other.

Weight (`weight=0.8…1.9`) is the **loot roll weight**: the marksman rifle is the
heaviest roll in the table and the least common thing on a floor, which is why
finding one changes how a squad plays the rest of the match. How a weapon
*handles* is not derived from it - `ads=` and `draw=` are their own numbers per
weapon, so the marksman rifle is not just slow to fire, it is slow to be holding
when somebody comes through the door behind you.

---

## 5. Sound as a weapon property

`audio=(AudibleRadius, InteriorAudibleRadius, bSuppressed)`, in cm:

| Weapon | Outside | Through a floor | Suppressed |
| --- | --- | --- | --- |
| P99 Sidearm | 7000 | 11000 | no |
| LCP | 6800 | 10500 | no |
| **P99-S** | **2600** | **3200** | **yes** |
| MP5 | 8600 | 13500 | no |
| MP7 | 8800 | 13000 | no |
| M870 | 10500 | 16000 | no |
| Semi-12 | 10000 | 15500 | no |
| M4 | 9200 | 14000 | no |
| MCX | 9000 | 13800 | no |
| L129A1 | 11000 | 17500 | no |

The interior radius is larger than the exterior radius for every weapon, because
the building carries sound through its structure. Firing an M4 on floor 6 is
audible **fourteen metres laterally and three floors vertically** — the sound
system in `Docs/12_Audio.md` applies the through-floor attenuation on top of
these radii, and the bot hearing model in `Docs/06_Match_Systems.md` uses the
same numbers. One number, three systems, no drift.

The suppressed pistol at 2600/3200 cm is the single most important line in this
table. It is the difference between a weapon and a playstyle.

---

## 6. `UOneBrahamWeaponComponent` and `UOneBrahamInventoryComponent`

| Component | Owns |
| --- | --- |
| `UOneBrahamWeaponComponent` | the current weapon, fire mode, ammo, bloom, recoil state, ADS, reload, per-weapon timers, and every server-validated `Server*` RPC |
| `UOneBrahamInventoryComponent` | two slots (primary, sidearm), three utility slots, keycards (`HasKeycardForTier`), loot, drop-on-death |

Server validation on fire: the server checks the weapon is the equipped one, that
the shot is not inside the minimum interval for the current fire mode, that the
shot origin is within 40 cm of the replicated weapon muzzle, and that the client's
claimed direction is within tolerance of the client's reported aim. A rejected
shot is dropped **silently** — no error, no feedback, nothing for a cheater to
learn from.

Fire modes are `Auto`, `Pump`, `Bolt` for the trigger logic and `Single`, `Burst`,
`Auto` for the selector; the distinction matters because a pump is not a
semi-automatic weapon with a slow rate, it is a weapon with a cycling action that
can be interrupted by sprinting.

On death the inventory drops one loot card carrying **what the player actually
had**: `AccessTier` (the highest tier keycard held), `bDroppedFromPlayer=true`,
and a `PrintedName` naming the archetype — so a squad that kills the IT Operations
player and reads "IT Operations — Staff" on the card learns something real. The
kit itself hits the floor as pickups, and the card goes with it
(`UOneBrahamInventoryComponent::ServerDropKeycardOnDeath`, next to
`DropAllOnDeath`): an escalation that survives its owner's death is not an
escalation, so a squad that dies on twelve does not keep the building.

### The spawn loadout

Nothing in the game gives a player a weapon except
`UOneBrahamWeaponComponent::ServerApplyLoadout`, and the only two callers are a
spawn and the arrival of a loadout that missed the spawn:

| Call | Called by | What it does |
| --- | --- | --- |
| `AOneBrahamPlayerController::ServerSetLoadout` | the client, once, when it joins | the locker's choice: a primary id, a sidearm id, up to four utility ids and a skin id. `AOneBrahamPlayerState::StoreLoadout` sanitises it (ids to 24 characters, four utility entries, nothing else) and keeps it. A second call during a life changes the *next* life and nothing else, so a client cannot swap weapons mid-fight. |
| `UOneBrahamWeaponComponent::ResetOnRespawn` | `AOneBrahamCharacter::ServerOnRespawned` | destroys what the previous life ended with, clears the reserve pools and the reload flag, then re-arms. |
| `ServerApplyLoadout` | the two above | registry-checked weapons (`ServerGiveWeapon` refuses an id it does not know), inventory-checked utility (`ServerAddUtility` has a whitelist), and the skin applied to both weapons. |
| `ApplyLoadoutForCurrentLife` | inside the component | the player state's loadout, or the standard-issue kit (`DefaultPrimaryWeaponId`, `DefaultSidearmWeaponId`, `DefaultUtilityIds`) when there is none — which is every AI bot, for its whole life, and any client whose loadout is still in flight. A player is never standing at a spawn point with nothing to shoot with. |

Three rules behind that table, each of which was wrong at some point:

- A respawn **destroys** the old kit rather than dropping it, because the death
  path has already dropped it as pickups. Dropping it twice would arm whoever is
  standing at the spawn point — including the enemy who just killed you.
- A reload belongs to the weapon, not to the player. Switching slots calls
  `AOneBrahamWeaponBase::ServerCancelReload` on the weapon being put away; without
  it, its timer kept running and the reload finished into a holstered weapon,
  moving the ammo and playing half an animation off-screen.
- Skins travel as an id and are painted locally. `SkinId` is
  `ReplicatedUsing = OnRep_SkinId` and the rep notify rebuilds the material
  instance, because a dynamic material instance only exists on the machine that
  created it: setting the skin on the server alone paints nobody's weapon but the
  server's own, which nobody can see.
