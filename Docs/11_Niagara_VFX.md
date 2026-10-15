# ONE BRAHAM — Niagara and VFX

The 17 systems, 3 camera shakes and 6 decal materials the game asks for, what each
one has to do, and the budget it has to do it in — plus the procedural fallback
that stands in for any system that is not authored yet (section 7) and the blood
and gore system (section 9).

Everything here is created as an asset by
`Tools/Python/onebraham/build_vfx.py`. The emitter stacks are the one part that is
authored by hand inside Niagara — that is what Niagara is for — so every system
below has its stack specified rather than left to taste. The machine-readable
version of this document is
`Tools/Python/payloads/vfx_authoring_sheet.txt`, which the build writes next to
the scripts so it is always in front of whoever is filling the systems in.

---

## 1. Where the systems are, and how the code finds them

`UOneBrahamVFXStatics::GetEffectAssetPath()` builds the path from a fixed stem
per effect:

```
/Game/OneBraham/VFX/NS_OB_<Stem>.NS_OB_<Stem>
```

Do not rename a system. A rename is not a compile error and not a crash: it is a
silent fallback to the procedural effect, logged once per effect. That is the
correct behaviour for a build in progress and the wrong behaviour for a shipping
build, which is exactly why `validate_project.py` asserts all 17 exist.

**An empty system counts as missing.** `build_vfx.py` creates all 17 with their
names and user parameters and *no emitters*. An empty system loads, spawns,
activates and draws nothing — for a long time that made every impact above the
Low preset invisible. `UOneBrahamVFXStatics::IsNiagaraSystemUsable` now requires
at least one enabled emitter; `LoadEffectSystem` returns null for an empty
system, and every caller (impacts, explosions, flash, smoke, the weapon's muzzle
flash, brass and tracer, the grenade's detonation component) plays the
procedural fallback instead. Adding the first emitter to a system is what
switches the authored effect on — no code change, no flag.

| Effect enum | Asset stem | Spawned by | Cull (cm) | Max concurrent |
| --- | --- | --- | --- | --- |
| `BulletImpact` | `NS_OB_Impact_Concrete` | `SpawnImpact` | 12000 | 64 |
| `MetalImpact` | `NS_OB_Impact_Metal` | `SpawnImpact` | 12000 | 64 |
| `GlassImpact` | `NS_OB_Impact_Glass` | `SpawnImpact` | 9000 | 48 |
| `FleshImpact` | `NS_OB_Impact_Flesh` | `SpawnImpact` | 9000 | 48 |
| `MuzzleFlash` | `NS_OB_MuzzleFlash` | `SpawnEffect` (weapon) | 6000 | 12 |
| `Tracer` | `NS_OB_Tracer` | `SpawnEffect` (weapon) | 25000 | 40 |
| `ShellEject` | `NS_OB_ShellEject` | `SpawnEffect` (weapon) | 5000 | 20 |
| `FragExplosion` | `NS_OB_Explosion_Frag` | `SpawnExplosion` | 25000 | 4 |
| `BreachExplosion` | `NS_OB_Explosion_Breach` | `SpawnExplosion` | 25000 | 4 |
| `FlashbangBurst` | `NS_OB_Flashbang` | `SpawnFlashbang` | 20000 | 6 |
| `SmokeCloud` | `NS_OB_Smoke` | `SpawnSmokeCloud` | 20000 | 8 |
| `BloodMist` | `NS_OB_BloodMist` | `SpawnEffect` | 4000 | 12 |
| `WaterSpray` | `NS_OB_SprinklerSpray` | `SpawnEffect` | 8000 | 24 |
| `SparkShower` | `NS_OB_SparkShower` | `SpawnEffect` | 9000 | 10 |
| `DustPuff` | `NS_OB_Dust` | `SpawnEffect` | 7000 | 16 |
| `BreachDebris` | `NS_OB_BreachDebris` | `SpawnBreachDebris` | 9000 | 12 |
| `CorpseImpact` | `NS_OB_CorpseImpact` | `SpawnEffect` | 5000 | 8 |

---

## 2. User parameters every system declares

Declared in all 17, set by the spawner, so a single system can serve a whole
class of events without a second asset:

| Parameter | Type | Default | Set by |
| --- | --- | --- | --- |
| `Scale` | float | 1.0 | the spawn call (a shotgun breach is bigger than a pistol hole) |
| `TintColour` | LinearColor | white | surface colour, team colour, alarm state |
| `Intensity` | float | 1.0 | the local VFX quality setting: 0.25 / 0.55 / 0.8 / 1.0 |
| `LifetimeScale` | float | 1.0 | the player's effect-duration preference |

Plus per-system parameters: `ShellVelocity` on `NS_OB_ShellEject`,
`ShotDistance` on `NS_OB_Tracer`, `Radius` on all four explosion/smoke systems,
`ImpactNormal` on both breach systems.

`Intensity` is the one that matters operationally. It is read from
`UOneBrahamVFXStatics::GetVFXIntensity()`, which reads the user's quality
setting. Below 0.35 the game does not spawn a system at all: it spawns
`AOneBrahamImpactFallbackActor`, a camera-facing sprite that costs a few
draw calls and reads clearly on a 1080p screen. That is the low-spec path and it
is a supported configuration, not a degraded one.

---

## 3. The systems, one at a time

### 3.1 `NS_OB_Impact_Concrete` — 12000 cm, 64 concurrent

The most-spawned system in the game; a twelve-player firefight on the open-plan
floor produces forty of them in six seconds.

| Emitter | Type | Detail |
| --- | --- | --- |
| Sparks | CPU sprite, burst 12–18 | 0.4–1.2 cm, velocity 220–650 cm/s along the surface plane, gravity −980, drag 2.2, lifetime 0.25–0.5 s |
| Dust | CPU sprite, burst 6–10 | 3–9 cm, velocity 40–130 cm/s, drag 6, lifetime 0.45–0.9 s, colour = `TintColour` × 0.6 |
| Decal | renderer-target only | single plane, alignment locked to `ImpactNormal` |

Fixed bounds on. Spawn normal-aligned. No collision module: impact VFX never
queries physics, because a hit result already exists.

### 3.2 `NS_OB_Impact_Metal`

Additive sparks, 18–26, with a 0.3 s gravity ramp and a 6 cm trail, plus 2–4 hot
flecks at emissive 6–14. Also drives the light flicker hook the map binds to when
a rack is shot.

### 3.3 `NS_OB_Impact_Glass`

Hybrid: 8–14 GPU flat quads with refraction 0.02, plus 10 dust motes and one long
crack decal (`M_OB_Decal_GlassCrack`). Shards inherit 150–400 cm/s of the
projectile's velocity, which is what makes an office partition breaking read as
*breaking* rather than as a puff.

### 3.4 `NS_OB_Impact_Flesh`

10–16 red-mist sprites, 4–7 dark flecks with gravity, one 0.35 s directional cone.
No additive, no screen effect, deliberately restrained: this is a serious game
about ordinary people, and a horror effect would be tonally wrong. Only spawned
for hits that pass through a body.

### 3.5 `NS_OB_MuzzleFlash` — 12 concurrent

GPU, camera-facing, 0.045 s. Three layers: a white core (emissive 30), petals
shaped by calibre (emissive 12) and a 0.25 s smoke wisp drifting up at 40 cm/s.
Calibre is a user parameter, not a second system: ten systems for ten weapons is
how a project hits its particle budget before it has any gameplay.

### 3.6 `NS_OB_Tracer`

Ribbon, 0.06 s, 3 cm wide, additive emissive 8. Length comes from the shot
distance so a hit at 2 m does not draw a 40 m line. One round in five carries a
tracer (`bHideTracer` on the marksman rifle is false, on the pistols true).

### 3.7 `NS_OB_ShellEject`

CPU mesh casings on ballistic arcs, 2.5 s life, 3 bounces, 2 s rest, fade. The
ejection vector is a parameter, so the pump shotgun throws differently from the
pistol without a second asset. Brass on lino is one of the best sound-and-vision
pairs in the game; keep it.

### 3.8 `NS_OB_Explosion_Frag` and `NS_OB_Explosion_Breach`

Four layers, all driven by one `Radius` parameter:

| Layer | Duration | Detail |
| --- | --- | --- |
| Flash | 0.12 s | white, emissive 60 |
| Fireball | 0.6 s | 3 turbulence sprites, orange ramp |
| Smoke | 1.6 s | black, rising |
| Debris | 1.2 s | 40 meshes, radial impulse, gravity, 2 bounces |
| Floor ring | 0.9 s | dust, projected onto the first hit below |

`NS_OB_Explosion_Breach` is the directional variant: a 60° cone along the wall
normal, 60 plaster chunks, 2.2 s of heavy dust, and no fireball colour ramp.
The hole itself is geometry — the wall opens — so the VFX only has to sell the
dust, which is what actually sells it.

### 3.9 `NS_OB_Flashbang`

0.08 s pure white sphere (emissive 120) plus a 0.25 s afterglow. The screen
effect is **not** this system: it is the post-process blend driven by
`AOneBrahamCharacter`, and the two are tuned together so that the white of the
sprite and the white of the screen agree. If they disagree, the effect reads as a
bug in the game rather than as an injury to the player.

### 3.10 `NS_OB_Smoke`

GPU sprites, 22 s lifetime, 4–6 m radius, growth-then-hold, with a wind vector
taken from the level's draught so smoke in the stairwell drifts. Two colours:
white for concealment, grey-green for the stairwell during a fire drill. Depth
fade at 40 cm so a player standing inside it is not blinded by their own screen.

### 3.11 `NS_OB_BloodMist`

Small, directional, 0.3 s, capped at 12 concurrent. The easiest effect in the
project to overspend on, so it is capped hard and only spawned for pass-through
hits.

### 3.12 `NS_OB_SprinklerSpray`

GPU cone, 40 cm fan, plus a 0.3 s splash ring. Emitted from the sprinkler heads
the map places. During the security purge these are on, on every floor being
cleared — the water and the red emergency lighting are the two things that make
the purge legible without any UI at all.

### 3.13 `NS_OB_SparkShower`

30–60 sparks falling from a rack or a lift panel over 1.4 s, with a light flicker
hook. Electrical failure is the visual language of the building giving up.

### 3.14 `NS_OB_Dust`

Low-velocity dust puff. Used when a door first moves, when a shutter lands, when
a body hits the floor, and when a charge goes off in a plasterboard wall. It is
the cheapest system in the project and the one that does the most work: it is
what makes the building feel like it has been sitting there since 1998.

### 3.15 `NS_OB_BreachDebris` and `NS_OB_CorpseImpact`

Breach debris: 20 wall chunks along the breach normal, 1.2 s, spawned on the
first charge impact and again, larger, when the wall opens.

Corpse impact: a ground puff sized from the ragdoll's velocity at first contact
plus a 0.15 s cloth ripple. Bodies are the most-read object on a floor — a player
learns more from one body than from any HUD marker — so this system is small but
it is not optional.

---

## 4. Camera shakes

| Blueprint | Trigger | Amplitude | Duration | Notes |
| --- | --- | --- | --- | --- |
| `BP_OBCameraShake_Explosion` | any explosion within its radius | 0.55 | 0.42 s | Perlin 26 Hz, plus a 0.1 s rotational pop on the first frame |
| `BP_OBCameraShake_Landing` | landing hard | 0.18 | 0.20 s | amplitude scaled by impact speed in the character's landing code |
| `BP_OBCameraShake_Extraction` | while the rotor is at the pad | 0.90 | continuous | 11 Hz sine with a 0.35 s wobble every 2.2 s as the blades pass the deck edge |

Parent class is `UOneBrahamCameraShake`, which is a `UCameraShakeBase` with the
amplitude/scale parameters that `AOneBrahamCharacter` sets when it plays them.
The extraction shake is the reason a player physically wants to get on the
helicopter rather than stand next to it.

---

## 5. Decals

| Material | Surface | Size | Notes |
| --- | --- | --- | --- |
| `M_OB_Decal_BulletHole_Concrete` | Concrete, Marble, Lino | 6 cm | dust ring, dark core |
| `M_OB_Decal_BulletHole_Metal` | Metal, Grating, ServerRack, Decking | 5 cm | bright bare-metal lip |
| `M_OB_Decal_BulletHole_Carpet` | Carpet, Fabric | 7 cm | dark, matte, ragged |
| `M_OB_Decal_BulletHole_Plaster` | Plasterboard | 9 cm | large pale crater with a shadowed rim |
| `M_OB_Decal_GlassCrack` | Glass, BallisticGlass | 24 cm | radial sheet with a 0.4 s crack-through |
| `M_OB_Decal_Scuff` | Concrete | 30 cm | where a body slid; texture is CC0 ambientCG Smear001 (mask from its roughness map), on `M_OB_DecalTextured` |
| `M_OB_Decal_Blood` | any surface | 3–64 cm | procedural splatter/pool; see section 9 |

Bullet holes are spawned by `SpawnImpact` with a **25 second** life and a hard
cap of **96 live holes** per client (`UOneBrahamVFXSubsystem::MaxBulletHoles`);
the oldest is recycled. Bullet holes are the fastest way to prove a firefight
happened, and the fastest way to lose 15 fps, so both numbers are in the code
rather than in a designer's head.

**Orientation.** A decal projects along its local +X, so +X points *into* the
surface (minus the hit normal), with a random roll about it. All decals go
through `UOneBrahamVFXSubsystem::DecalRotationForSurface`, which is pinned by the
`OneBraham.VFX.DecalOrientation` test. It replaced a rotation that kept only the
yaw, which projected every hole on a floor or a ceiling sideways into nothing.

---

## 6. Budgets and rules

| Budget | Value | Where it is enforced |
| --- | --- | --- |
| GPU particles per frame, worst case | 24,000 | the concurrency caps above |
| Sprite texture memory | 18 MB | the flipbooks listed in the authoring sheet |
| Mesh particle memory | 6 MB | casings, debris, shards share three meshes |
| Live bullet holes per client | 96 | `UOneBrahamVFXSubsystem::MaxBulletHoles` |
| Live blood decals per client | 64 | `UOneBrahamVFXSubsystem::MaxBloodDecals` |
| Active fallback effects per client | 48 | `UOneBrahamVFXSubsystem::MaxActiveFallbackActors` |
| Particles per fallback effect | 40 | `AOneBrahamImpactFallbackActor::MaxParticles` |
| Cull pass | per-viewer, by distance | `ShouldSpawnForLocalViewer`, `IsCulledByViewer` |

Rules that come out of shipping games and are cheap now and expensive later:

1. **Fixed bounds on every system.** Dynamic bounds on a twelve-player server
   with forty simultaneous impacts is the single most common cause of hitching in
   this genre.
2. **No collision queries in a system.** A hit result already exists when the VFX
   is spawned; asking physics again doubles the cost of every bullet.
3. **Nothing spawns per-frame from a tick.** Every system here is spawn-on-event.
4. **Scale, never duplicate.** If a new effect needs a bigger flashbang, it passes
   a bigger `Radius`.
5. **No VFX on the dedicated server.** `IsCulledByViewer` culls everything when
   the net mode is a dedicated server, the VFX subsystem is never created there,
   and the hit-reaction and blood paths return early, so a headless server
   allocates nothing.

---

## 7. Fallback behaviour

If a system is missing *or empty* — the content build's empty asset, a deleted
asset, a cook list wrong, a designer renaming something at 2 a.m. — the game does
this:

1. `LoadEffectSystem` logs once per effect, then remembers it is missing.
2. The caller plays the procedural stand-in, `AOneBrahamImpactFallbackActor`:
   engine primitives (`/Engine/BasicShapes` sphere, cube, cylinder) with dynamic
   material instances whose colour, emissive strength and opacity are driven over
   each particle's life. Particles are simulated on the CPU (velocity, gravity,
   drag, buoyancy, one floor plane per effect found by a single trace); brass and
   blast debris use real physics against world geometry only.

| Family | What the fallback draws |
| --- | --- |
| Concrete, lino | fast grey puff, hard chips that skitter, the odd aggregate spark |
| Marble | paler puff, bright chips |
| Metal, grating, racks, decking | 5–14 stretched emissive sparks with gravity and bounce, a warm flash |
| Glass | flat tumbling shards, half thrown along the shot, a cold flash |
| Plasterboard | a large pale cloud and chalky chips |
| Carpet, fabric | a small dark fibre puff |
| Puddle | a crown of droplets and a spreading ring |
| Flesh | dark droplets sprayed along the shot (stretched along their velocity, falling with gravity), a little back-spatter, a 0.3 s dark mist; headshots larger. With `ob.Gore 0`, a grey fabric puff |
| Muzzle flash | an elongated two-frame emissive flare, four petals, a point light, a smoke wisp |
| Tracer | a thin stretched emissive streak travelling the path at up to 450 m/s, one round in three, stopped at the first wall |
| Shell ejection | a brass cylinder thrown right-up-back with a physics arc and bounce (shotgun hulls twice the size) |
| Frag / breach | flash light, three expanding cooling fireball spheres (frag only), rolling smoke or plaster dust, 5–12 physics debris chunks, sparks |
| Flashbang | a very bright, very short white light and core |
| Smoke | nine overlapping spheres that bloom out and hold for the grenade's lifetime — opaque grey if the FX materials are missing, which still breaks line of sight |
| Ricochet | a tight spark fan and a short streak along the reflected path, plus a pitched-up whizz |

Materials, each with an engine fallback so a clean checkout still draws:
`M_OB_FX_Glow` (unlit additive) → `M_OB_Emissive` → `BasicShapeMaterial`;
`M_OB_FX_Haze` (lit translucent with a Fresnel-softened edge) →
`BasicShapeMaterial`, where an opaque particle shrinks instead of fading.
`build_vfx.py`'s `build_fx_materials` authors both, and the blood decal.

**Performance.** Every fallback actor is pooled by `UOneBrahamVFXSubsystem`:
finished actors are hidden and parked (and keep their components and material
instances), at most 48 are active at once, and the oldest is cut short when a
49th is asked for. Brass is culled at 25 m and skipped on Low. A steady
firefight creates no new objects at all.

This is a development convenience and the minimum-spec path, not a substitute
for the authored systems. `validate_project.py` fails the build if any of the 17
systems is missing from the content, because "it works without VFX" is a
statement about development builds only.

---

## 8. What spawns each effect, and the two ways a system goes unused

An effect can be perfect and never appear: declared in the enum, authored,
budgeted, culled correctly, and spawned by nothing. That failure is invisible to
every asset check because there is nothing wrong with the asset. There are
exactly two ways it happens, and this project closes both.

**1. The effect is only reachable from a Blueprint.** A weapon's muzzle flash
comes from `UOneBrahamWeaponDefinition::MuzzleFlash`, which is a soft pointer a
designer fills in. The content build creates weapon definitions before it creates
Niagara systems, so on a fresh checkout that pointer is empty and the gun fires
with no flash, no brass and no tracer — while looking perfectly correct in the
editor. The fix is a fallback in C++: `AOneBrahamWeaponBase::ResolveFxFallbacks`
loads the conventional `NS_OB_*` system when the definition does not name one,
caches it on the actor, and never writes to the data asset. Authored content
always wins; generated content always works.

**2. The effect is reached through a helper that nobody calls.** An impact effect
is chosen by `ImpactEffectForSurface`, and a utility detonation by
`GetDetonationEffect`; both are only as alive as their callers. The same fallback
pattern applies — `AOneBrahamProjectileBase::ResolveDetonationFX` points the
detonation component at `NS_OB_Explosion_Frag`, `NS_OB_Flashbang` or
`NS_OB_Smoke` when the Blueprint assigned nothing — and the check in
`validate_project.py` follows the call graph from gameplay code through the VFX
library to prove that nothing is orphaned.

### The table the build enforces

| Effect | Spawned by | Triggered by, in the building |
| --- | --- | --- |
| `BulletImpact` | `SpawnImpact` | any hard surface not otherwise mapped |
| `MetalImpact` | `SpawnImpact` → `ImpactEffectForSurface` | lift doors, handrails, riser cupboards, decking |
| `GlassImpact` | `SpawnImpact` → `ImpactEffectForSurface` | the perimeter glazing, screens, glass partitions |
| `FleshImpact` | `SpawnImpact` (character hit) | any shot that lands on a player |
| `WaterSpray` | `SpawnImpact` → `ImpactEffectForSurface` | the kitchen floor, the bike store, the terrace after rain |
| `DustPuff` | `SpawnImpact`, `SpawnExplosion`, `AOneBrahamFireShutter`, `AOneBrahamAccessZone` | plasterboard partitions, a shutter coming down, a breached door |
| `SparkShower` | `SpawnImpact` → `SurfaceSparks`, `SpawnExplosion` | server racks and grating on floors 3–11; every explosion |
| `BloodMist` | `SpawnImpact` | a character hit above half intensity, with `ob.Gore 1` |
| `MuzzleFlash` | `AOneBrahamWeaponBase::MulticastPlayFireFX` | draw-and-fire on every weapon except the suppressed P99-S |
| `ShellEject` | `AOneBrahamWeaponBase::MulticastPlayFireFX` | every shot, including suppressed ones |
| `Tracer` | `PlayTracerLocal` | every third or fourth round of the carbines and the marksman rifle |
| `FragExplosion` | `SpawnExplosion`, `AOneBrahamProjectileBase` | a frag detonating; a breached access zone |
| `BreachExplosion` | `SpawnExplosion`, `AOneBrahamBreachableWall` | a breaching charge, a breached partition |
| `BreachDebris` | `SpawnBreachDebris` | all four breachable walls and both breached-door cases |
| `FlashbangBurst` | `SpawnFlashbang`, `AOneBrahamProjectileBase` | a flashbang, anywhere in the server room or the lift lobby |
| `SmokeCloud` | `SpawnSmokeCloud`, `AOneBrahamProjectileBase` | smoke on the Skydeck pad, in the canteen, on the mezzanine bridge |
| `CorpseImpact` | `UOneBrahamRagdollComponent::TickCorpseLanding` | a body landing after a fall of more than a storey |

**The rule this table encodes.** If a system is authored in this project, some
line of C++ outside `OneBrahamVFXStatics.cpp` must be able to reach it, and
`validate_project.py`'s *vfx reachability* check follows the call graph to prove
it. Adding an eighteenth effect means adding its enum value, its `EffectStem`
case, its system in `build_vfx.py` — and a caller.

---

## 9. Blood, gore and hit reactions

ONE BRAHAM is serious, not gratuitous: blood is dark (it only reads red where the
light catches it), it is where the physics says it should be, and it stays long
enough to tell the next squad up the stairs what happened here.

### 9.1 The console variable

`ob.Gore` (registered in `OneBrahamVFXStatics.cpp`): **1** (default) on, **0** off.
Off means no blood spray, no blood decals, no pools, no wound shading — and
switching it off mid-match clears what is already on the walls. A character hit
with gore off still shows a neutral grey fabric puff, because hit feedback is
gameplay. The settings screen drives it with the console command; a platform or
rating requirement can force it from `[ConsoleVariables]` in `DefaultEngine.ini`.

### 9.2 What happens when a round hits a person

The server traces the shot and collects one `FOneBrahamCharacterHitFX` (victim,
bone, location, direction, damage, headshot, killed) per pellet that did damage,
then sends them in one unreliable multicast,
`AOneBrahamWeaponBase::MulticastPlayCharacterHitFX`. Damage, death and the hit
marker travel on their own reliable paths; this one is cosmetic. On every
non-dedicated machine:

1. **Spray** — from the impact multicast: the flesh effect, sprayed along the
   shot (the direction comes from the weapon's muzzle, which every machine has).
2. **Splatter** — `UOneBrahamVFXStatics::SpawnBloodForHit` traces along the shot
   up to 2.5 m past the victim (3 m for a headshot) against the building only and
   places a blood decal on whatever it finds: a wall, a floor, a ceiling, a
   desk, a door (attached, so it swings with the door). Size scales with damage
   and grows with distance travelled; the mark streaks along the shot and is
   longer the shallower it lands. Heavy hits and headshots add one or two
   smaller satellite marks.
3. **Drips** — one to four small round decals on the floor beneath the wound.
4. **Flinch** — `UOneBrahamRagdollComponent::PlayHitReaction`: a short partial
   physics blend below the hit bone (a shot to the arm moves the arm). At 40
   damage or more, or on any headshot, it is a **stagger**: everything above the
   upper spine rocks back for 0.65 s. On the victim's own machine it is a camera
   flinch instead, scaled by damage and the camera-shake settings. Hits through
   cover react exactly as clean hits do.
5. **Wound shading** — `BloodAmount` (0–1) accumulates on the body's materials.
   Skeletal meshes cannot take decals, so the character master material has to
   read this parameter to darken and wet the clothing; until it does, the
   parameter is ignored harmlessly.

### 9.3 Pools

A body does not arrive with a puddle under it. 1.6 s after the ragdoll starts
(when the body has settled), a pool decal appears under the pelvis and spreads
from a fifth of its size to 42–64 cm over 15 s, then stays for the corpse's
lifetime (capped at 240 s) and fades over 8 s. A downed player gets a smaller
pool after 1.5 s in one place, another if they stay put somewhere new (up to
three), and a trail of drops while they crawl.

### 9.4 Budget

64 blood decals per client, recycled oldest first — splatters before pools, so
the pool under a body outlives the spray from two minutes ago. Splatters hold
for 45 s and fade over 4 s (drying from glossy to matte as they go). Blood is
culled beyond 50 m from the camera. `M_OB_Decal_Blood` is procedural — a radial
falloff broken up by world-space noise, `Seed` per decal, `Splatter` 0 for a pool
and 1 for a spray — so no two marks tile. If it has not been built yet there are
no blood decals at all (logged once): a square red decal is worse than none.

### 9.5 Near misses and ricochets

A round that passes within 1.5 m of an enemy player's head without hitting them
tells that player's machine: `AOneBrahamWeaponBase::NotifyNearMisses` calls
`UOneBrahamRagdollComponent::ClientNearMiss` on the victim's own character,
which plays the whizz and a very small suppression flinch. Rounds that stopped
in a wall short of the player, rounds from teammates, and bots are excluded; it
is throttled to four a second per victim on the server, which is why it can be
reliable.

A round arriving within about 22° of a metal or concrete surface may ricochet:
sparks and a short streak along the reflected path and a pitched-up whizz
(`RicochetChance`, pinned by `OneBraham.VFX.Ricochet`). It is decided on each
machine, carries no damage, and costs no bandwidth.
