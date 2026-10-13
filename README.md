# ONE BRAHAM

A vertical multiplayer extraction shooter set in a fifteen-floor telecoms tower in
Aldgate, London. Twelve players, three squads of four, no AI, no respawns. Find a
keycard, climb the building, and hold the Skydeck for thirty seconds while
everybody else comes up the stairs behind you.

Unreal Engine 5.5 · C++ and Blueprint · Windows PC · dedicated server · Steam

---

## What is in this repository

This is the complete game project, not a design document and not a prototype:
the source, the configuration, the content pipeline, the tooling and the
documentation needed to build, package and ship ONE BRAHAM.

```
One-Braham/
├── OneBraham.uproject       UE 5.5, 11 plugins
├── Config/                  every ini, hand-authored and commented
├── Source/                  the C++ module, 14 systems
├── Content/OneBraham/       everything this project authors
├── Tools/Python/            the content pipeline (16 scripts)
├── Scripts/                 build, run, test, package, ship
├── Docs/                    21 documents
└── Saved/                   build and validation reports (not committed)
```

Everything that can be generated **is** generated, by
`Tools/Python/onebraham/*`. One command builds the whole game's content.

---

## Building it from a clone

Prerequisites: **Unreal Engine 5.5 or newer**, **Visual Studio 2022** with the
"Game development with C++" workload, **Python 3.9+**, and the **Steam client**
running if you intend to build the platform configuration.

```
1.  Run Setup.bat                     verifies the engine, the plugins and the toolchain
2.  Right-click OneBraham.uproject    Generate Visual Studio project files
3.  Build the Editor target           Development | Editor | Win64
4.  Run BuildContent.bat              builds every generated asset and the map
5.  RunTests.bat                      the automation suite
6.  Play                              open L_Persistent_OneBraham and press Play
```

`Scripts/README.md` is the full script index, with the switches each one takes
and the order they must run in.

### Running a match

```
Scripts\RunServer.bat                          dedicated server, 12 players, ASCENT
Scripts\RunClient.bat 127.0.0.1                one client
Scripts\RunClient.bat 127.0.0.1 -Squad 2       a client on a specific squad
```

`RunTests.bat smoke` boots a server, connects three squads and plays a complete
ASCENT run through to a `MatchResult`. It is the test that gates a release.

---

## The game

**ASCENT** — the primary mode. 12 players, 3 squads of 4, no AI, no respawns.
Three ways up the tower: a looted keycard, a hold-to-unlock override terminal
(twelve seconds alone, **three seconds with a colleague on the paired panel**),
or a breaching charge. Lower floors become unsafe as the match goes on: the
lockdown seals everything below the highest occupied floor, and then the purge
climbs the stairwell and does not care whose squad you are in. One squad extracts
from the Skydeck. If every squad is eliminated, nobody walks out, and the results
screen says so.

**FIRE DRILL** — 6v6 or 8v8, Internal Staff against External Contractors, three
control points (Lobby / Server Floor / Canteen–Skydeck), faster respawns. The way
to learn the building without losing a 25-minute run to it.

**NIGHT SHIFT** — 4-player co-op survival against the building's own systems.
Post-launch.

### The eight areas

Ground Plaza · Lobby · Mezzanine and Lift Lobby · Open Plan Office (floors 3–8) ·
Server and Comms (9–11) · Executive (12) · Canteen (13–14) · Skydeck (15).

All 8 areas are built at true dimensions: 400 cm floor to floor, 110 cm doors,
real desk rows, real server aisles, a canteen with eighteen tables and a boardroom
with a table you have to walk around.

### The armoury

Ten weapons, no joke weapons and no office supplies among them: a service pistol,
a machine pistol, a suppressed pistol, an SMG, a PDW, a pump shotgun, a
semi-automatic shotgun, a 5.56 carbine, a compact AR and a marksman rifle. Twelve
utilities, including a breach charge, a flashbang and a meal deal. Six archetypes
— Analyst, Graduate, Finance, IT Operations, Facilities, Consultant — who all
wear lanyards, because that is what people in this building wear.

**Cosmetics only, at £4.99 / $5.99.** No battle pass, no paid advantage, and every
cosmetic is earnable.

---

## The technology

| Area | What it uses |
| --- | --- |
| Engine | UE 5.5, World Partition, level streaming per floor |
| Rendering | Lumen GI and reflections, Virtual Shadow Maps, Nanite on architecture, TSR |
| Language | C++20 for every system; Blueprint for authoring and animation glue |
| Input | Enhanced Input, 41 actions, four mapping contexts — all generated |
| UI | UMG with CommonUI for the menu stack; the HUD builds itself in C++ |
| Physics | Chaos, ragdolls and Chaos Cloth |
| Effects | Niagara — 17 systems, fixed bounds, hard particle budgets |
| Audio | Unreal Audio Mixer, 12 sound classes, four mixes, spatialised voice |
| Voice | The Unreal Voice module with this project's own coder and mixer |
| Network | Dedicated and listen servers, 60 Hz, server-authoritative everything |
| Platform | Steam (Online Subsystem Steam, Steamworks, SteamCMD upload) |

---

## The documentation

| Document | What it covers |
| --- | --- |
| `Docs/00_README.md` | index and reading order |
| `Docs/01`–`06` | structure, configuration, core systems, characters, weapons, match systems |
| `Docs/07`–`08` | multiplayer and voice, interface |
| `Docs/09`–`13` | the map, the art, VFX, audio, characters and clothing |
| `Docs/14` | the voice script: 161 lines, six actors, per-line direction |
| `Docs/15`–`16` | asset generation prompts, marketplace fallbacks |
| `Docs/17`–`18` | build automation, testing and performance |
| `Docs/19`–`20` | Steam page, marketing and trailer, release checklists |
| `Docs/LICENCES.md` | every third-party asset and its licence |

---

## Contributing rules that are not negotiable

1. **No cartoon visuals, no office-supply weapons, no meme characters, no forced
   jokes.** The comedy in this game comes from the building being real. Anything
   that winks at the player undoes it.
2. **Nothing hand-placed that a script can place.** A hand-edited level and a
   generated manifest disagree silently, and that is how a two-week bug hunt
   starts.
3. **Server authority for anything that matters.** Damage, unlocks, breaches,
   timers and cosmetics.
4. **`validate_project.py` must report zero failures before a build goes out.**
   It runs 23 checks, and six of them need no engine at all — so a broken
   include, a member function with no definition, an RPC with no body, a
   replicated property with no `DOREPLIFETIME`, a log category with no verbosity,
   a VFX system nothing spawns, an asset path nothing creates, a call with the
   wrong number of arguments, or a gameplay tag that does not exist is found on
   any machine, in about a second, before anybody waits for a compile:

   ```
   PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/validate_project.py --config-only
   PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/run_all.py --text-only
   ```
