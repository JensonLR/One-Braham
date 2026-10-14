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

```bat
Scripts\RunTests.bat                    all OneBraham.* tests
Scripts\RunTests.bat OneBraham.Weapons   one category
Scripts\RunTests.bat smoke               the match-boot composition check
```

The suites are in `Source/OneBraham/Tests/`, one file per category, entirely inside
`#if WITH_AUTOMATION_TESTS` so a shipping build compiles them out. They live in the
game module rather than in a plugin: `RunTests.bat` then finds them with no extra
configuration, and the engine-free checks run inside the same command.

They are written against the framework's own shape, which the network of C++-first
tutorials and the engine header agree on:

```cpp
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamCoreTest, "OneBraham.Core.AscentRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamCoreTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("ASCENT squads"), Settings->TeamCount, 3);
    return true;
}

#endif // WITH_AUTOMATION_TESTS
```

The macro takes no semicolon and declares a class the next line defines. The flags
are a pair: exactly one context (`EditorContext`) and exactly one filter
(`EngineFilter`), which the framework's own `static_assert` enforces - a missing
filter is a compile error, and a missing context is a test that never runs.
`check_test_names.py` holds both halves, plus the declaration-to-`RunTest` pairing
that is otherwise a link error.

### 2.1 The suites

| Category | Tests | What they assert |
| --- | --- | --- |
| `OneBraham.Core` | `AscentRules`, `FireDrillRules`, `Constants`, `BuildingLayout` | ASCENT is 3 squads of 4 = 12 with a 30 s hold, a 45 s winch flight, 90 s lockdown stages, a purge ceiling below the Skydeck and a 12 s / 3 s override; FIRE DRILL is 2 even teams of 6–8 with respawns, no extraction and control points inside the building; the compiled-in constants agree with each other and with the documentation (400 cm storeys, 220 cm reach, 16 kHz voice); the building's sixteen floor rows are complete, ascending, area-tagged, four-breachable, three-endgame, and their extraction gate is at 5600 cm |
| `OneBraham.Brand` | `Palette` | the seven brand colours match the published values in both the class defaults and `Config/DefaultEngine.ini`, `UOneBrahamUIStyle` reads them from the settings rather than from literals, and alarm red is distinguishable from warning yellow |
| `OneBraham.Weapons` | `FireRate`, `Falloff`, `Recoil`, `LootGating` | RPM to fire interval; damage never rises with range, never exceeds the base value and never goes negative out to 200 m; one-pull shotgun totals; recoil steps are finite, upward, non-constant, and a negative shot index reads nothing off the end of the pattern; floor gating (a floor 6 marksman rifle never spawns on the ground floor) and the registry's own validator |
| `OneBraham.Damage` | `HealthModel`, `Armour`, `Zone` | 100 health + 50 armour, armour absorbing 60 % of each hit until it breaks (the worked example in `Docs/04`: 30 damage → 12 health and 18 armour), four shots to break the plate and a fifth to kill, head/neck/torso/limb multipliers in the right order, and the shots-to-kill a carbine and a marksman rifle need through a full plate |
| `OneBraham.Voice` | `CodecShape`, `LineSet`, `Wheel` | 16 kHz mono 20 ms frames inside a 1024-byte transport ceiling and under 64 kB/s worst case; the line set fills itself from the compiled-in table (105 core + 66 variants, at least 120 of them different words), idempotently, with no duplicate ids, no empty subtitles, and no line that can never be said; the comms wheel's eight segments are unique, labelled, and every one resolves against the script; out-of-range segments return nothing |
| `OneBraham.Input` | `AllActionsBound`, `MappingContexts`, `Rebind` | `ValidateAllActionsSet` reports every one of the 41 reflected action fields on an empty config, reports nothing on a fully assigned one, names the three overlay actions explicitly, and the mapping contexts are soft references of the right type; a rebind replaces one key and keeps the rest, taking another action's key swaps the two so nothing is left doing two things, rebinding one key of Move writes all four with their modifiers, a pad button cannot be a keyboard binding, the look axes are not offered, and reset restores the defaults (the player's own rows are put back afterwards) |
| `OneBraham.Multiplayer` | `ReplicationContract`, `Smoke` | every server and client RPC is reliable, every `ReplicatedUsing` property names a function that exists on its own class, there are zero `WithValidation` RPCs, and the network surface has the shape a server-authoritative game should; then the match-boot composition check: the classes the config names load and derive from the right bases, ASCENT is the default mode, and the building and mode data agree on a twelve-player match |
| `OneBraham.Animation` | `PoseAndDirection` | every armed weapon category holds a weapon pose and utility holds none; a body's movement direction is measured against where it faces (forwards 0, strafing right 90, backwards 180), and standing still or falling straight down reads 0 rather than a NaN |
| `OneBraham.Online` | `CareerRecord`, `AchievementTable` | a finished match folds into the career record from the local player's own row, found by player id; an unfinished result records nothing; the best extraction time only improves; Hold the Floor needs a last-squad-standing win on floors 9-11, not 12 and not a loss; FIRE DRILL counts wins only; achievements are earned once; every achievement id is unique, starts `ACH_` and is unearned on a fresh profile; every cosmetic an achievement grants is locked on a fresh profile and opens with its tag; every rich presence token the code sends is in `Config/Steam/rich_presence_english.vdf` |
| `OneBraham.Extraction` | `HelicopterPath` | the helicopter's approach starts at the approach point and ends exactly over the pad when the arrival timer reaches zero, never dips below the hover height on the way in, and the hold hovers over the pad with less than half a metre of bob |
| `OneBraham.KillFeed` | `Verb`, `DeathCause`, `Wiring` | the kill verb's branch order (the building outranks a self-kill, a self-kill outranks an execution, an execution outranks a headshot, and no combination of circumstances produces the disconnect verb); the death screen's wording for every verb, named and unnamed, never empty and never leaking an unformatted placeholder; and that the three files that carry a kill - the character's death path, the game mode's single kill entry point, the controller's Client RPC - are still wired to each other |
| `OneBraham.StaticChecks` | `EngineFreeChecks` | runs the seventeen engine-free checkers in `Tools/Python/onebraham` as child processes and fails on any non-zero exit, so `RunTests.bat` covers the static shape of the project as well as the parts that need an engine - including whether the suites in this table exist |

The suites never call `ApplyDamage` or drive a match: that path is authority-gated
by design (a client that calls it is a bug or a cheat, and it refuses), so it needs
a spawned, authoritative actor in a real world. What is asserted is the arithmetic
and the data those calls are measured against.

### 2.2 What is not automated yet, and what covers it

Five categories that an earlier draft of this document promised are **not** in the
suite, because each needs something that does not exist in a fresh clone. They are
listed here rather than quietly dropped, with what does cover them today:

| Category | Why it cannot be a unit test | Covered by |
| --- | --- | --- |
| `OneBraham.Access` | zone tiers, doors and terminals are actors on the built map; their state lives on the game state and moves in a world with players | the data they run on is asserted (`Core.AscentRules`, `Core.BuildingLayout`), the layout table is checked against the map builder by `check_building_layout.py`, and the behaviour is exercised by the Q2 checklist row "unlock every route" in `Docs/20` |
| `OneBraham.Lockdown` | the seal decision depends on the highest occupied floor, i.e. on live player positions | `Core.AscentRules` asserts the stage timing, the warning and the purge ceiling; the escalation itself is watched in the Q2 "lockdown escalation" checklist row |
| `OneBraham.Extraction` | the winch advances on a timer with players on the pad, and its refusals depend on match state | `Core.AscentRules` asserts the hold, the flight, the radius and the override times; the state machine is driven by the smoke run in section 3 |
| `OneBraham.Performance` | the budgets in section 4 are counts of the actual level: actors, lights, decals, particles | `Scripts\Package.bat` plus the `stat` pass in section 4, recorded per milestone; `Multiplayer.Smoke` asserts the match composition the budget has to hold for |
| `OneBraham.Validation` | `validate_project.py`'s packaging checks need UnrealBuildTool and UnrealPak | `StaticChecks.EngineFreeChecks` runs the seventeen checks that need no engine inside the test run; the full validator runs in `Scripts\RunAll.bat`, where a missing toolchain is loud |

The functional half of a real project's suite - a match that starts, escalates and
extracts - is worth writing, and it is worth writing once there is a level to run
it on. Until then, a test with a mocked match would assert the mock.

One class of bug is invisible to every check above, and this project shipped four
of them: **the method that exists, is defined, is documented, and that nothing
calls.** It is not a link error and not a reflection error, so no compiler and no
checker in the table above reports it. A missing caller has cost this build more
than any other single category - the anti-cheat speed report (the client was never
asked to send it), the assist credits (the victim's attackers were tracked and
never paid), the spawn loadout (the method a respawn called did not exist, and
nothing else gave a player a weapon), and the extraction state machine (ASCENT's
win condition, four callers short of working).

`Tools/Python/onebraham/report_uncalled.py` is the answer, and it is deliberately a
report rather than a gate: it renders every declared-and-defined member with no call
site anywhere in the module, tags the Blueprint-callable and RPC ones so they can be
triaged rather than triaged for you, and exits 0. Run it before and after a wiring
batch. The number coming down is the proof of progress; the number standing still is
either a Blueprint contract that needs recording in the document that specifies it,
or a hole nobody has noticed yet.

---

## 3. The 12-player smoke run

`Scripts\RunTests.bat smoke` runs `OneBraham.Multiplayer.Smoke` in the headless
editor (it is an editor-context test; a Shipping server has no automation
framework to run it in). The automatic part is the composition check: the classes
the config names exist, the mode's squad maths gives twelve players, and the
building table backs it.

The release gate is the bot-driven run below, which needs the packaged server and
the built map. It replaces four players' worth of decision-making with eleven bots
and a human, which is enough to catch everything that only appears with twelve
people in the building:

```bat
Build\WindowsServer\OneBrahamServer.exe OneBrahamServer-L_Persistent_OneBraham?Mode=Ascent?Bots=11 ^
    -server -log -MaxPlayers=16 -NullRHI
```

1. Bots spawn, loot and push up; the log prints squad assignments for three squads.
2. Watch for a squad reaching the Skydeck: `LogOneBrahamExtraction` prints
   broadcast, arrival and hold progress.
3. The run passes when a `MatchResult` names a winning squad after a completed
   hold, or - the other path worth running - when all three squads are eliminated
   and it prints `NoSurvivors`.
4. A failed run is never "flaky": a squad that cannot find the Skydeck, a door that
   does not open and an extraction that does not end are all bugs in the level,
   the navmesh or the access chain.

It takes a few minutes with `-NullRHI` and about as long again with rendering on,
which is the version to run before a build goes to Steam.

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

### Content caps (checked when the level is built, not by a unit test)

These are counts of the shipping level, so they are read off the built map - by
`Scripts\\Package.bat`'s cook warnings and by an audit pass at each milestone -
rather than asserted in `Tests/`. Section 2.2 records why that half of the suite
is not automated yet and what stands in for it.

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
3. **No dynamic bounds on Niagara.** Fixed bounds on all 18 systems.
4. **No VFX or audio work on a dedicated server.**
   `ShouldSpawnForLocalViewer` returns false with no local viewer.
5. **Occlusion traces at 15 Hz per listener**, not per frame.
6. **Replication by distance.** Kill feed, scoreboard and objective state are
   replicated; per-bullet impact detail is multicast unreliably and culled by
   distance.
7. **One material master per purpose.** Fifteen surface instances of two masters,
   not fifteen masters.
8. **The whole tower stays loaded on the server.** It is one persistent level
   and sixteen always-loaded floor sublevels of blockout, small enough to hold
   at once, and the server simulates every floor a player is on anyway.
9. **Audio concurrency limits** per cue type, and a per-frame budget on new
   one-shots.
10. **Shaders are cooked, not compiled on first use.** A shipping build must never
    hitch on the first shot fired.

---

## 6. Measuring

Engine commands first, because most questions are answered by one of them:

| Command | What it does |
| --- | --- |
| `stat unit` / `stat gpu` | frame breakdown: game, draw, GPU |
| `stat scenerendering` | draw calls and triangles |
| `stat memory` | streaming pool and texture budget |
| `stat net` | replication bandwidth |
| `stat niagara` | particle counts per system |
| `Automation RunTests OneBraham.Multiplayer.Smoke` | the match composition the budget has to hold for |

Then the project's own readouts, all routed by `UOneBrahamConsole` and all
documented here rather than discovered in the source:

| Command | What it does |
| --- | --- |
| `ob.Audio.Dump` | the local mix state, one line per bus, plus the voice duck and the occlusion filter. Needs a player: a dedicated server has no mix. |
| `ob.NetDebug 1` | the per-actor replication cost overlay: the replicated actors near the local view, in the order the replication scheduler will spend bandwidth on them, from `AActor::GetNetPriority`, with update rate, cull distance and designed dormancy. `ob.NetDebug.Lines` (default 12) sets how many and `ob.NetDebug.Radius` (default 4000 cm) how far it looks. |
| `ob.Bench.Skydeck [seconds]` | the scripted flythrough benchmark. One leg per floor of the building layout, default 6 s each, camera panning and yawing across each floor, frame time recorded every frame. Writes `Saved/Benchmarks/ob_bench_<stamp>.md` with overall and per-floor mean, median, p95, p99 and worst, plus the three blanks a real record needs: hardware, resolution and scalability, build. Needs a local player |
| `ob.Bench.Skydeck.Stop` | ends a run early and writes the same report for the frames captured so far. |
| `ob.bots.dump` | one line per practice bot: state, skill, team, floor, location, target, line of sight and reaction time remaining. Server only; bots exist only in bot practice and the QA modes, never in ASCENT. |
| `ob.netbudget` | the replication budget report: what the game state counts per second against `RpcBudgetPerSecond`. Server only. |
| `ob.give <WeaponId>` | puts a weapon in the local player's hands. Cheat, development builds only. |
| `ob.weapon.remove` | destroys the local player's held weapon without dropping it. For the unarmed cases: the first-person arms with nothing in them, the player who has just been relieved of a rifle, the locker preview. |
| `ob.lockdown [stage]` | forces the lockdown to a stage, or with no argument advances it one. Server only. |
| `ob.endmatch [team]` | ends the match with a result. Server only. |
| `ob.extraction.fast` | calls the winch for the squad of whoever typed it (if nobody has called one yet) and jumps the sequence to the 30 s hold. Server only, ASCENT only. The winch is a 55 s sequence behind a console on the Skydeck; this is how a QA pass reaches the last 30 seconds of it. |
| `ob.extraction.abort` | aborts the live attempt as `CONTESTED`, which is the failure path with the end-of-match copy on it. Server only. |

Test hardware is recorded with every benchmark run - the report has blank lines
for it - because a budget measured on a developer's overclocked machine is not a
budget.

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

The movement report is the one anti-cheat channel that is deliberately two-sided.
Every `OneBrahamConstants::SpeedReportIntervalSeconds` (250 ms, 4 Hz) the machine
that is actually simulating a pawn sends its own velocity; the server compares
that number against the authoritative cap it derives from the movement component
(`GetAuthoritativeMaxSpeed()` times `MaxMovementSpeedTolerance`), and compares the
clock against the last report it heard. `MaxSpeedReportGapSeconds` (2 s, eight
missed intervals) of silence is itself a violation: a modified client can lie
about its speed, and it can stop reporting, but it cannot cheaply do neither. The
server takes the report as corroboration and never as evidence - a sustained
overspeed in its *own* simulation snaps the pawn back to legal velocity and logs
the violation, which is what the teleport and speed checkboxes below are really
testing.

---

## 8. Regression policy

Every bug fixed gets a test, in the category it belongs to, before the fix is
merged. A bug that cannot be expressed as a test gets a line in the manual QA
checklist instead — and the checklist has grown by about forty lines in a normal
production, which is what it is for.
