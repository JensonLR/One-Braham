# 21 — Development plan: from here to Steam and Xbox

The plan of record for finishing ONE BRAHAM. Every other document describes a
system; this one says what is left, in what order, what "done" means for each
step, and what cannot be done from the development Mac. It is updated at the
end of every working session, and nothing ships that is not ticked here and in
Docs/20.

---

## 1. Where the game stands (2026-10-15)

| Area | State | Evidence |
| --- | --- | --- |
| Code | Compiles on macOS (UE 5.8, Apple Silicon). Every system in Docs/03–08 implemented. | 37 automation tests, 43 validation checks, all engine-free checks passing |
| The tower | All 18 storeys to the brief and the references: revolving doors, double-height reception, gates with glass wings, escalators, coffee shop, 9 lifts (glass lift in steel), two stairs, 48 rooms, canteen, Skydeck balcony, Management Suite and atrium (16), Winter Garden and roof terrace (17) | Docs/09; rendered review captures |
| Lighting | Physically based: lumens and lux, EV100 exposure per floor, sun and sky, fill, stair bulkheads | Docs/09 §5.3 |
| Surroundings | Aldgate East from OpenStreetMap at 1:1: ~8,800 buildings, streets, traffic, parks, trees, the City skyline | Docs/09 §2.9 |
| Art | **Blockout.** Every surface is a flat material instance on engine cubes; no textures, no authored meshes, characters and weapons are placeholders | Docs/15, 16 |
| Audio | Procedural placeholders; the recording brief is written | Docs/12, 14 |
| Played | **Not yet by people.** Automation and captures only | Docs/18 §3 |

The honest summary: the systems and the space are there; the game does not yet
*look* or *sound* like a shipped game, and it has not been played. The plan
below closes those two gaps first, because they are what a player sees.

---

## 2. Milestones

Each milestone ends with a playable build, a capture review against the
reference photographs (Docs/10 §7), and every check green.

### M1 — The building, true to life *(in progress)*

The real One Braham, inside and out, from the research in Docs/09 and Docs/10 §7.

- [x] Ground floor to the brief: doors, reception ahead, gates right with open
      glass wings, escalators, coffee shop, nine lifts, glass lift, stairs, WCs.
- [x] Physically based lighting; fixed sun, sky, fog, exposure; stair lighting.
- [x] The surrounding district from OpenStreetMap; the City skyline.
- [x] **Floors 16 and 17**: the real building has 18 storeys, with terraces on
      15 and 17 and an atrium rising through the top floors. Add the
      Management Suite (16) round an atrium and the Winter Garden and roof
      terrace (17), stepped back over the canteen like the real crown.
- [x] **Facade**: dense silver aluminium fins on the east, south and west faces
      (not the entrance face); the stepped crown; soft rounded corners; the
      ground-floor base in black mullions with glass fins.
- [x] **Reception**: double height; black linear-baffle ceiling with sweeping
      LED light ribbons; pale terrazzo; black feature volume (no roundel - the
      game's tower is OB) with an OLED advert screen; white desk; the open
      feature stair. OLED screens at the top of the escalators too.
- [x] **Terraces**: pale large-format paving, long charcoal planters with
      grasses and multi-stem trees, frameless glass balustrades (17, and the
      atrium garden on 16).
- [x] **Sky**: a photographed CC0 sky on a dome, the sun aimed at the sun in
      it, horizon haze matched to the fog. *Still to do:* switch to the dusk
      preset for the Skydeck finale (built; needs a lockdown-stage hook).

**Done when** each reference photograph has a matching in-game capture a
stranger would recognise as the same building.

### M2 — Real surfaces and dressing *(started)*

Replace flat colours with physically based textures, and boxes with props.

- [ ] Texture sets for the 15 surfaces (Docs/10 §2) from the sources in §4.
      *Done:* terrazzo, concrete, timber, plaster, street paving, asphalt -
      CC0 sets on a world-aligned master, coloured by the measured albedo;
      carpet tile, brushed metal, terrace decking and the canteen's rubber
      tile (ambientCG / Poly Haven). *Next:* fabric, glass grime, signage.
- [ ] Office dressing: desks, chairs, monitors, planters, signage, kitchen kit,
      server racks, toilets - from §4, scaled to real dimensions. *Done:*
      open-plan benching with oak tops, spine screens, lit dual monitors,
      pedestals, task chairs and row-end planters; then, from the interior
      photographs: white benching and mesh chairs, cubby shelving, purple
      high-back booths and breakout kitchens, felt acoustic discs, suspended
      linear LEDs, the cream-panelled lift lobby on pale porcelain, the
      reception lounge (oak herringbone, curved sofa, figs in black pots),
      server racks with LEDs and cable trays, WC fittings, wayfinding signs
      and terrace picnic benches. Meeting rooms, the boardroom and
      offices as glass boxes in black frames with oak tables; the lift lobby's
      lit ceiling. The canteen servery: steel wells under a sneeze-guard, heat
      lamps, menu screens, coffee machine, lit chiller, extract canopy; oak
      tables with black chairs.
- [ ] Decals: wear, floor signage, wet-floor, blood (already authored as
      materials; needs textures).
- [ ] District facades: real window textures per family, roofscape plant,
      shopfronts at street level on the near streets.

**Done when** no surface within 50 m of a player reads as a flat colour.

### M3 — People, weapons, animation, sound

- [ ] Characters on the UE5 mannequin skeleton with the free Game Animation
      Sample motion-matching set; the six archetypes' clothing from Docs/13.
- [ ] Ten weapons with proper meshes, sounds and first-person animation.
- [ ] Recorded or licensed audio replacing the procedural placeholders:
      weapons, footsteps per surface, ambience per floor, the voice script.

**Done when** a 30-second clip of a fight could go in the trailer.

### M4 — Gameplay finish

The known gaps, from the end of Docs/18 and the session log:

- [x] Lifts pick up calls they pass in the travel direction (and the lockdown
      closes the Skydeck and the crown to them).
- [x] Microphone level meter in Settings: dBFS bar with peak hold and a tick
      where the voice-activation gate opens.
- [x] Death recap in place of a replay killcam: under the cause line, each
      attacker's hits, damage, zones and range, and what the killer had left.
- [x] First-time player path: a guided induction on the ground floor (move,
      look, sprint, crouch, keycard, gates, escalators, fire, reload), once
      per profile, never able to trap a player.
- [ ] Bots tuned against human play data from M5.

### M5 — Play it

- [ ] Two-machine LAN session on the real network stack (voice, replication,
      spawn safety, lockdown, extraction) - Docs/18 §3, the 12-player smoke run.
- [ ] Internal playtests with recorded sessions; bug triage in the tracker.
- [ ] Performance capture on the minimum-spec PC (Docs/18 §4 budgets).
- [ ] Steam Playtest (closed) for external players.

**Done when** two consecutive playtests produce no new A or B bugs.

### M6 — Ship

- [ ] Windows build packaged and uploaded to Steam (Docs/20 §11).
- [ ] Xbox build through ID@Xbox (§5).
- [ ] Store pages, trailer, capsule art (Docs/19); legal notices including the
      OpenStreetMap credit (Docs/LICENCES.md).

---

## 3. What cannot be done from the development Mac

These are not oversights; they need hardware, accounts or people:

| Step | Needs |
| --- | --- |
| The Windows (Steam) build | A Windows PC with Visual Studio; Unreal cannot cross-compile Win64 from macOS |
| Steam upload | A Steamworks partner account, the app fee, a real AppID (the project uses 480) |
| Xbox | ID@Xbox acceptance, the Microsoft GDK, Epic's console access, a dev kit, Windows |
| Multiplayer validation | Two or more machines and players |
| Human playtesting | Players |
| Voice recording | Actors and a booth (Docs/14) |

Everything else is in the plan above and can be done here.

---

## 4. Open and free assets: where from, and the bar they must clear

**Sources, in order of preference.** All are free for commercial use in an
Unreal game; record every asset used in Docs/LICENCES.md.

| Source | What | Licence |
| --- | --- | --- |
| Fab / Quixel Megascans (installed: Fab plugin, Quixel Bridge) | Photoscanned surfaces, plants, props, decals | Free for Unreal Engine projects |
| Poly Haven | HDRI skies, textures, a few props | CC0 |
| ambientCG | PBR texture sets | CC0 |
| Epic samples: Game Animation Sample, City Sample, Lyra | Animation, crowd/vehicle/street kit, gameplay reference | Free for Unreal Engine projects |
| Kenney | UI icons and simple props | CC0 |
| OpenStreetMap | The surrounding streets and buildings | ODbL, credit required |
| Freesound (CC0 only), Sonniss GDC bundles | Sound effects | CC0 / royalty-free |

**The bar.** One Braham's look is a real, recent London office building: glass,
aluminium, exposed steel and concrete, terrazzo, timber, planting, under
grey-blue London light. An asset is used only if it:

1. **Matches a reference photograph** of the real building or of Aldgate -
   not "an office", *this* office.
2. **Is physically based and real-scale**: proper albedo range, roughness, a
   100 cm door is 100 cm.
3. **Holds up at first-person distance**: no texture under 2K on anything a
   player can press their face against.
4. **Sits in the palette** (Docs/08, Docs/10 §1): muted corporate neutrals,
   BT's purple-to-blue only where the real building uses it.
5. **Is not recognisable** as a stock asset from another game.

If an asset needs forcing - re-colouring until it fits, hiding its seams with
props, scaling it until it stops looking wrong - it does not fit. Keep the
blockout and write the brief in Docs/15 instead. A clean blockout is better
than an ugly asset.

---

## 5. Xbox: the route

1. Apply to **ID@Xbox** (Microsoft's independent developer programme) with the
   pitch, the trailer and a playable PC build. Acceptance is Microsoft's call.
2. Once accepted: access to the **Microsoft GDK**, a development kit, and
   **Epic's console platform access** for the Xbox build of Unreal.
3. Platform work: controller-first UI (already supported by the input
   config), Xbox Live sign-in and multiplayer sessions in place of Steam's
   online subsystem, cross-play decision, achievements, save handling,
   suspend/resume, accessibility requirements.
4. **Certification** against Microsoft's Xbox Requirements, then release.

Plan for the Xbox port to follow the Steam launch by one milestone. The PC
build is the reference, and certification is easier on a game already patched
from live feedback.

---

## 6. How this plan is kept true

- Every session ends by updating §1 and ticking §2.
- A tick needs evidence: a capture in the review set, a passing test, or a
  playtest note. "Implemented" is not "done".
- New work found mid-milestone goes into the milestone it belongs to, not the
  current one, unless it blocks it.
