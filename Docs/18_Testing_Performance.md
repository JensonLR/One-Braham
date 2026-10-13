# ONE BRAHAM — testing, performance and optimisation

The test plan, the automation suite, the performance budgets, and the manual QA
pass that no amount of automation replaces.

---

## 1. What is automated and what is not

Automation covers everything that can be expressed as an assertion: damage
numbers, replication behaviour, unlock timing, spawn safety, memory budgets. It
does not cover: whether a fight is fun, whether a corridor reads as dangerous,
whether the extraction feels earned.

The division is deliberate. A test suite that tries to assert fun is a test suite
that gets disabled after a month.

---

## 2. Automation tests

Run with `Scripts/RunTests.bat` (all of `OneBraham.*`) or
`Scripts/RunTests.bat <category>`.

| Category | What it asserts |
| --- | --- |
| `OneBraham.Core` | Game instance, game state and player state lifecycle; match phase transitions; squad assignment with 12 players and 3 squads |
| `OneBraham.Weapons` | Every weapon definition is valid; RPM to minimum fire interval matches; magazines, reserves and reload times are inside sane bounds; no sidearm out-damages a rifle; the registry validates |
| `OneBraham.Damage` | 100 HP + 50 armour; hit-zone multipliers; falloff at exactly the documented ranges; armour absorption and break; downed at 0 HP rather than dead; bleed-out timing |
| `OneBraham.Access` | Zone tier gating; the 12 s solo override and the 3 s paired override; override cancellation; breaching a wall opens it permanently; escalation opens the right zones |
| `OneBraham.Lockdown` | Stage timing (90 s stages, 15 s warning); floors below `HighestOccupiedFloor-1` seal; the purge damages floors ≤ 13 and never the Skydeck; no kill attribution on purge death |
| `OneBraham.Extraction` | Broadcast → Arriving → Holding → Complete/Failed; the 30 s hold; contested hold freezes; loiter expiry; refusal when the mode is not ASCENT, when a squad is dead, or below Z 5600 |
| `OneBraham.Input` | `ValidateAllActionsSet` returns true; every binding in the ini resolves to an action asset by name |
| `OneBraham.Voice` | Codec round-trip at 16 kHz; frame budget (≤ 1024 bytes); channel routing (proximity / squad / team); mute list enforcement |
| `OneBraham.Multiplayer` | A dedicated server + 3 clients perform a full ASCENT match in headless mode; the match completes with an extraction result |
| `OneBraham.Performance` | Actor, light, decal and particle counts stay inside the budgets in section 4 on the shipping map |
| `OneBraham.Validation` | Wraps `validate_project.py`: the build fails if the validator fails |
| `OneBraham.StaticChecks` | Runs the engine-free checks (declared-but-undefined members, UE reflection contracts, project includes that do not exist, brace balance, log categories, VFX reachability, surface coverage, and the naming contract for assets loaded by path) in-process, so a CI stage without an engine install still catches the class of error that only a linker, a missing `DOREPLIFETIME` or a null soft load would otherwise reveal |

The functional tests drive real matches through the game mode rather than calling
functions directly, because the bugs that matter live in the spaces between
systems: an unlock that works but does not replicate, an extraction that
completes but does not end the match, a kill that is credited to the wrong squad.

---

## 3. The 12-player smoke test

`Scripts/RunTests.bat smoke` is the test that gates a release. It:

1. Boots a dedicated server (`-server -NullRHI`).
2. Connects three simulated squads of four.
3. Plays a scripted ASCENT run: loot a Staff card, open the lift lobby, take the
   stairwell on floor 3, override the server door with two panels, hold the
   Skydeck pad for 30 seconds, extract.
4. Asserts a `MatchResult` naming the winning squad, with four players counted
   as `Extracted`.
5. Repeats with the losing path: all three squads eliminated → `NoSurvivors`.

It runs in about four minutes. If it fails, nothing else in the release checklist
matters.

---

## 4. Performance budgets

Targets are for a **12-player dedicated server and a mid-range PC client**:
i5-10400 / Ryzen 5 3600, 16 GB, GTX 1660 Super / RX 5600 XT at 1080p, and an
NVMe SSD. Everything below is measured on the shipping map with twelve players in
a firefight on the server floor — the worst case in the game.

### Client frame budget — 16.6 ms at 60 fps

| Pass | Budget | Notes |
| --- | --- | --- |
| Game thread | 5.0 ms | AI-free, so this is animation, replication and the HUD |
| Draw thread | 4.5 ms | ≤ 2,400 draw calls |
| GPU | 12.0 ms | Lumen GI at 1080p, quality tier "High" |
| Render | 1.6 M triangles visible, Nanite for architecture | |
| Shadows | VSM, cascades 3, 90 m | |

### Memory

| Item | Budget |
| --- | --- |
| Streaming pool | 2,048 MB at 1080p High, 3,072 MB at 1440p Epic |
| Texture memory | 1,400 MB |
| Mesh memory | 700 MB |
| Audio | 96 MB |
| Total process | ≤ 4.5 GB |

### Content caps (enforced by `OneBraham.Performance`)

| Item | Cap |
| --- | --- |
| Runtime actors in the persistent level | 40,000 |
| Static mesh actors | 60,000 |
| Lights (all types) | 900 |
| Live decals per client | 96 |
| Niagara particles per frame, worst case | 24,000 |
| Cloth assets simulating at once | 8 |

### Server tick budget — 60 Hz, 16.6 ms wall clock, 5 ms target

| Work | Budget |
| --- | --- |
| Character movement + replication | 1.2 ms for 12 players |
| Weapon fire validation | 0.4 ms per 10 shots/s |
| Lockdown director | 0.1 ms (4 Hz occupancy scan) |
| Extraction component | 0.1 ms (4 Hz) |
| Voice subsystem routing | 0.3 ms for 12 speakers at 20 ms frames |
| Replication graph (Unreal) | 2.0 ms |
| Headroom | ≥ 60% idle |

A dedicated server must sit at 60% idle under a full firefight. If it does not,
the first thing to fix is always the same: something is ticking at 60 Hz that
should be at 4 Hz.

---

## 5. Optimisation rules applied in this project

1. **Tick rates by intent.** Lean and anti-cheat at 2–4 Hz. The lockdown director
   and extraction at 4 Hz. Nothing ticks at 60 Hz except movement, weapons and
   the local player's own audio.
2. **Nanite for architecture, LODs for props.** Every wall, floor and ceiling is a
   Nanite candidate; every loose prop has LODs and is `Movable` only if Chaos
   needs it.
3. **No dynamic bounds on Niagara.** Fixed bounds on all 17 systems.
4. **No VFX or audio work on a dedicated server.**
   `ShouldSpawnForLocalViewer` returns false with no local viewer.
5. **Occlusion traces at 15 Hz per listener**, not per frame.
6. **Replication by distance.** Kill feed, scoreboard and objective state are
   replicated; per-bullet impact detail is multicast unreliably and culled by
   distance.
7. **One material master per purpose.** Fifteen surface instances of two masters,
   not fifteen masters.
8. **World Partition on the server** so the dedicated process loads only what it
   simulates.
9. **Audio concurrency limits** per cue type, and a per-frame budget on new
   one-shots.
10. **Shaders are cooked, not compiled on first use.** A shipping build must never
    hitch on the first shot fired.

---

## 6. Measuring

| Command | What it does |
| --- | --- |
| `stat unit` / `stat gpu` | frame breakdown |
| `stat scenerendering` | draw calls and triangles |
| `stat memory` | streaming pool |
| `stat net` | replication bandwidth per actor |
| `ob.Audio.Dump` | the whole mix state in one line per bus |
| `ob.NetDebug 1` | per-actor replication cost overlay |
| `OBBench.Skydeck` | the scripted benchmark flythrough on the release map |
| `Automation RunTests OneBraham.Performance` | the automated budget check |

Test hardware is recorded with every benchmark run. A budget measured on a
developer's overclocked machine is not a budget.

---

## 7. The manual QA pass

Before every release, on real hardware, with real people. Full checklist in
`Docs/20_Release_Checklists.md`; what follows is the part that must never be
skipped:

### Gameplay

- [ ] Play a full ASCENT match without a single console command.
- [ ] Win by extraction, and lose by being eliminated. Both endings feel earned.
- [ ] Open every one of the 10 access zones by card, by terminal, and where
      possible by breach. All three routes work.
- [ ] Survive the purge by climbing. The purge should feel like a decision, not a
      timer.
- [ ] Hold the Skydeck pad for the full 30 seconds while being contested.
- [ ] Play FIRE DRILL on all three points, and confirm respawns feel faster.

### Feel

- [ ] Fire every one of the 10 weapons. Recoil, sound, hit feedback and time-to-kill
      all read correctly, and no two weapons feel like the same gun.
- [ ] Throw every utility. Frag, flashbang, smoke and breach all do what the prompt
      says they do.
- [ ] Be downed and be revived. Bleed-out timing is tense, not annoying.
- [ ] Headshot at 40 m with the marksman rifle. It should feel like a decision that
      worked.

### Audio

- [ ] Play a match with headphones and no music: footsteps, lifts, shutters and
      gunfire through floors should all be locatable.
- [ ] Play a match and use proximity voice with someone on the other side of a
      wall.
- [ ] Stand on the Skydeck while a helicopter lands. This is the game's best
      30 seconds and it must land emotionally.

### Interface

- [ ] Complete a match without opening the menu.
- [ ] Play with subtitles on. Every voice line has one.
- [ ] Play one match with the HUD hidden. The game must still be readable.
- [ ] Rebind a key, restart, and confirm the rebind survived.

### Multiplayer

- [ ] 12 players, one dedicated server, 60 minutes, measured against the server
      tick budget. No rubber-banding at 100 ms simulated latency.
- [ ] A client joins mid-match, deploys, and plays without a hitch on arrival.
- [ ] A player disconnects mid-firefight. The match must be unaffected.
- [ ] Voice chat with four people talking at once. Everyone is still intelligible.

### Adversarial

- [ ] Fire-rate exploit: hold fire with a macro. Server must reject over-rate shots.
- [ ] Damage exploit: modify client damage values. Server must ignore them.
- [ ] Speed exploit: teleport. Anti-cheat must flag it and the server must correct it.
- [ ] Wall exploit: get out of the map and confirm you cannot shoot back in from
      outside the volume.
- [ ] Voice spam: hold push-to-talk for 60 seconds. The duck must not silence the
      match for everyone else.

---

## 8. Regression policy

Every bug fixed gets a test, in the category it belongs to, before the fix is
merged. A bug that cannot be expressed as a test gets a line in the manual QA
checklist instead — and the checklist has grown by about forty lines in a normal
production, which is what it is for.
