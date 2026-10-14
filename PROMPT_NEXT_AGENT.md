# Prompt — finishing ONE BRAHAM

Hand this whole file to Claude Code (Opus 5.5) with the repository checked out. It
is written to be self-contained: everything the agent needs to start is here or one
file deep in the repository.

---

## The mission

You are taking over **ONE BRAHAM**, an Unreal Engine 5.5 vertical multiplayer
extraction shooter set in a fifteen-floor telecoms tower in Aldgate, London. It is
not a prototype, not a design document and not a greybox: it is the game project —
C++, config, a Python content pipeline, twenty-one documents of specification, and
a build/verify/ship toolchain. Your job is to **finish it to completion. Nothing
missed, nothing left as "later", nothing stubbed with a TODO.**

Work in `/home/user/One-Braham` (or wherever the repository is checked out). Start
by reading, in this order:

1. `HANDOVER.md` — the state of the repository, what is verified, what is not, and
   the traps. Everything below assumes you have read it.
2. `Docs/00_README.md` — the documentation index, with a reading order per role.
3. `Docs/01_Project_Structure.md` — where everything lives and which PART of the
   original brief it belongs to.

Then run the verification loop in §"Verify as you go" before you change anything, so
you know what a clean tree looks like.

---

## What the game is (the specification, in one page)

* **ONE BRAHAM**, brand: telecom-inspired blue, white, deep navy, light grey, signal
  blue; safety yellow warnings, emergency red alarms; an original wordmark.
* **Setting**: the ground-floor entrance of One Braham, Aldgate, up to the
  fifteenth-floor Skydeck, with an extraction helicopter and winch on top.
* **ASCENT** (primary mode): pure PvP, 12 players, 3 squads of 4, no AI. Unlock the
  upper floors with looted keycards, hold-to-unlock override terminals, or breaching;
  lower floors become unsafe through lockdown, fire shutters and the security purge;
  one squad extracts by holding the Skydeck for 30 seconds, or the last squad
  standing wins. No survivors if everybody is eliminated.
* **FIRE DRILL**: 6v6/8v8 TDM and control, Internal Staff versus External Contractors,
  points at the Lobby, the Server Floor and the Canteen/Skydeck, faster respawn.
* **NIGHT SHIFT**: four-player co-op survival, post-launch, no zombies.
* **Eight map areas**: Ground Plaza and Entrance, Lobby, Mezzanine and Lift Lobby,
  Open Plan Office, Server and Comms, Executive, the fifteenth-floor Canteen, and
  the Skydeck. Each needs collision, nav, cover, sightlines, chokepoints, flanks,
  lighting, sound occlusion, environmental storytelling, streaming, spawn safety and
  clip-friendly death moments.
* **Ten weapons** in nine categories: pistol, compact pistol, suppressed pistol,
  SMG, PDW, pump shotgun, semi-auto shotgun, 5.56 carbine, compact AR, marksman
  rifle. Plus flashbang, smoke, frag, breaching charge, medkit/meal deal, keycard and
  the security override tool.
* **Six archetypes**: Business Analyst, Graduate/Intern, Finance, IT/Ops,
  Facilities/Security, Consultant — each with visuals, clothing, body/face/hair
  variation, animation and voice personality, ten voice lines, cosmetics and unlock
  rules, and no gameplay imbalance.
* **Tech mandate**: UE 5.5+, C++ and Blueprint, Enhanced Input, UMG (CommonUI where
  useful), GameplayTags, data assets, Chaos physics and Chaos Cloth, Niagara, Lumen,
  Nanite where appropriate, MetaHuman-class characters, Steam Online Subsystem, the
  Unreal voice module, dedicated server architecture, PC Windows first, listen server
  and dedicated server, keyboard-and-mouse first with controller support.
* **Tone, and these are hard**: serious gunplay; believable, tired London corporate
  characters; dry British humour that comes out of realism and nothing else; the
  game must feel cool, tense, funny through realism, and utterly beautiful.
  **Banned**: cartoon visuals, office-supply or pun weapons, exaggerated characters,
  forced jokes, American action-movie clichés, low-effort AI slop, placeholder
  content.
* **Commercial**: £4.99/$5.99, cosmetic-only monetisation, roadmap, anti-cheat
  basics, a full QA and automation plan, a Steam page, a trailer, release checklists.

Where the original brief asked for something a machine cannot produce — a material
node graph, a Niagara module stack, an animation, a recorded voice line, a
marketplace fallback — the requirement is an **exact creation instruction** in the
document that owns it: numbered nodes, parameter names and values, frame counts,
recording briefs with per-line direction, or a licence-safe substitute with its
price and licence. That is what `Docs/10`–`Docs/16` are. Never write "this would
require".

---

## The rules of engagement

1. **Do not ask clarifying questions.** Make the reasonable assumption, write down
   which assumption you made and why, and carry on.
2. **Do not stop at design.** Write the implementation. If a thing genuinely cannot
   be built without an asset that does not exist, build the pipeline that will
   create it and wire the code to it.
3. **Never claim something you did not verify.** "It compiles" and "the tests pass"
   are claims: run the command, paste the result. In this repository the engine-free
   ladder is checkable and the engine is not, and the handover says which is which.
4. **Work in batches.** One feature or one hole per batch: read the file first, make
   the edit with an asserted replacement count, run the full ladder, update the
   graded rows from the checker's own output, commit with a message that explains
   the *why*, push, verify the tree is clean. `HANDOVER.md` §5 has the detail.
5. **Keep the documentation true.** Every number in a document that the code
   contradicts is a build failure (`check_doc_numbers.py`). Every new tooling module
   must be named in `Docs/17`. Every new interaction, tag, cue, weapon id, HUD
   string, cosmetic or test must be added to the document that owns it *and* to its
   checker.
6. **No placeholders.** A stub with a TODO is a lie about progress. A documented
   instruction in the owning document is the honest form of "not yet".
7. **If you hit an output limit, write `[CONTINUE]` and carry on exactly where you
   stopped.** Never silently drop a requirement.
8. **Commit to a branch, push, and open a pull request per milestone.** Keep the
   history legible: the subject line says what changed about the game, the body says
   why and what it cost. `git log --oneline` in this repository is the build's
   changelog and it is worth reading.

---

## Verify as you go

```
cd One-Braham

python3 Tools/Python/run_all.py --text-only            # expect: BUILD OK: 18 step(s) succeeded
python3 Tools/Python/onebraham/validate_project.py     # expect: 31 pass, 2 warn, 0 fail
python3 Tools/Python/onebraham/check_doc_numbers.py    # expect: 89 numbers checked. agree. OK
python3 Tools/Python/onebraham/check_interaction.py    # expect: checks: 222
python3 Tools/Python/onebraham/report_uncalled.py      # expect: 83 member(s) with no call site
```

Any checker can be run alone (this is the only invocation that trips people up):

```
PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_cpp_symbols.py
```

Rules for the ladder: read the checker's own output instead of guessing a count;
when you add a checker rule, sabotage the thing it looks for and prove it fires
before you trust it; and if a `grep` returns zero, assume the grep is wrong until
you have proved otherwise.

---

## The work, in order

The order matters: each step makes the next one possible, and the first three are
what turn "a very well-documented project" into "a game".

**1. Make it compile.** Open `OneBraham.uproject` in UE 5.5, generate project files,
build the Editor target, and work the compiler's list. Commit in batches, run
`check_cpp_symbols.py` and `check_uht_contracts.py` after each. The module has never
been compiled: expect a real list on the first pass, a short one on the second, and
a different one again on first run.

**2. Work the uncalled-API triage.** `report_uncalled.py` lists 83 members that are
declared, defined and never called. `HANDOVER.md` §4.2 splits them into the 22 with
no caller at all (holes or deletions, each one a decision) and the 61 that are
`BlueprintCallable` (wire them in C++, or record them as a Blueprint contract in the
document that specifies the Blueprint). Re-run the report after every batch: the
number coming down *is* the proof of progress. Start with
`IsClaimingSquadAlive` (ASCENT's win cannot currently be lost),
`Input_LookGamepad` (controller support), `SetPauseMenuVisible` (the pause menu
cannot open), the main-menu and matchmaking cluster in `UOneBrahamGameInstance`
(the front end does nothing), and the nine audio cue wrappers.

**3. Build the content.** `Scripts/BuildContent.bat` runs the nine builders in order
and writes the manifest. This is where the tower, the armoury, the wardrobe, the HUD,
the Niagara systems and the audio bank come into existence. Read each builder's
report, fix what fails, and clear `validate_project.py`'s two warnings by running it
for real.

**4. Make it look, sound and move like the documents say.** `Docs/10` materials,
lighting and dressing; `Docs/11` Niagara with parameters and budgets; `Docs/12`
audio classes, mixes and the recording brief; `Docs/13` the six archetypes and the
wardrobe; `Docs/14` the 161 voice lines; `Docs/15` the briefs; `Docs/16` the
marketplace fallbacks and their licences. Every one of these documents is an
instruction set, not a wish list.

**5. Run and fix the game.** `Scripts/RunServer.bat` and `Scripts/RunClient.bat`,
`Docs/18` §3's 12-player smoke run, §4's frame and tick budgets, §7's manual QA
pass. This is where the honest list of "what is actually broken" appears, and
`Docs/08` is a very large "not yet" list waiting for that list.

**6. Ship it.** `Scripts/Package.bat`, `Scripts/SteamUpload.bat`, `Docs/19` (store
page, capsule art, the 96-second trailer), `Docs/20` (release checklists, top to
bottom). Steam integration, the dedicated-server build, the platform configs and
the pricing/roadmap copy already exist; what is missing is running them.

---

## Definition of done

It compiles and the content builds. ASCENT can be won by the winch and lost by
elimination; FIRE DRILL plays; the HUD, audio, settings and voice chat work; the
fifteen floors are the fifteen floors the documents describe; `Scripts/Package.bat`
produces a shipping build on a clean machine; every box in `Docs/20` is ticked with
evidence; `report_uncalled.py` reaches zero — or every remaining entry has a written
reason, in the document that specifies it, for why zero is correct by design.

Then, and only then, say it is finished.
