# ONE BRAHAM — release checklists

Every checklist that has to pass before a build goes out. Copy this into the
release issue and tick it there; a checklist that lives only in a document is a
checklist that gets skipped.

**Exit criteria for 1.0:** every item below is checked, `validate_project.py`
reports zero FAIL, the automation suite is green, and the 12-player smoke test
completes with a `MatchResult` on both the winning and losing paths.

---

## 1. Before the first content build (T-14 weeks)

- [ ] UE 5.5+ installed; `Scripts\Setup.bat` exits with `PROBLEMS 0`.
- [ ] Chaos Cloth and Hair Strands plugins enabled and probed by Setup.
- [ ] Steam client running and logged in for the platform build.
- [ ] `Source/*.Target.cs` all build: Editor, Game, Server, Client.
- [ ] `Tools/Python/onebraham/*` all compile: `python -m py_compile` clean.
- [ ] `run_all.py --text-only` exits 0.

## 2. Content build

- [ ] `Scripts\BuildContent.bat` completes; the report in `Saved/Build/` shows
      **0 failed steps**.
- [ ] Every `skipped_asset` in the report has a human assigned and a date.
- [ ] `Scripts\BuildContent.bat --only environment,props --dress` has been run
      since the last mesh import.
- [ ] `Tools/Python/onebraham_build_manifest.json` exists and lists all 9 areas.
- [ ] Map opens: `L_Persistent_OneBraham`, 18 floor levels, all present.
- [ ] The Aldgate district is present (`District_*` actors in the persistent
      level) and "Map data © OpenStreetMap contributors (ODbL)" is in the
      in-game credits and the store page's legal notices (Docs/LICENCES.md).

## 3. Validation

- [ ] `python3 Tools/Python/onebraham/validate_project.py` → **0 FAIL**.
- [ ] All 12 checks pass, and the 3 warnings that are expected (font weights,
      marketplace meshes, VFX stacks) are the *same three* as last release.
- [ ] No new `WARN` line since the previous release without a written reason.

## 4. Compile and package

- [ ] Editor target compiles with no warnings escalated to errors.
- [ ] Game, Server and Client targets compile.
- [ ] Non-unity build compiles (monthly; catches missing includes).
- [ ] `Scripts\Package.bat Shipping` completes for Windows.
- [ ] `Scripts\Package.bat Server` completes for Linux.
- [ ] Cooked build launches and reaches the main menu with **no** shader-compile
      hitch on the first firefight.

## 5. Automation

- [ ] `Scripts\RunTests.bat` → all `OneBraham.*` categories pass, including
      `OneBraham.StaticChecks`, which runs the engine-free checks in-process.
- [ ] `Scripts\RunTests.bat smoke` → the composition check passes on a dedicated
      server (see §2 of `Docs/18` for what that does and does not cover).
- [ ] The bot-driven 12-player run in `Docs/18` §3 completes with a `MatchResult`
      naming a winning squad, and the losing path reports `NoSurvivors`.
- [ ] The content caps in `Docs/18` §4 are inside budget on the release map
      (measured with `stat`, recorded with the hardware).

## 6. Multiplayer

- [ ] 12 players, dedicated server, 60 minutes: server tick ≤ 5 ms.
- [ ] 12 players at 100 ms latency and 2% loss: no rubber-banding above tolerance.
- [ ] A player joins mid-match and deploys without error.
- [ ] A player disconnects mid-firefight: no effect on the match.
- [ ] Listen server match with 8 players: host is flagged, match completes.
- [ ] Steam session: invite, browse, join, and a full match over Steam networking.
- [ ] Server list: the build appears, the mode filter works, ping is reported.

## 7. Every system, played

- [ ] All 10 weapons fired, reloaded, and used to kill.
- [ ] All 12 utility items used, including the breach charge and the meal deal.
- [ ] All 6 archetypes deployed, walked, sprinted, downed, revived and extracted.
- [ ] All 10 access zones opened by card, by terminal (solo and paired) and, where
      possible, by breach.
- [ ] All 4 breach walls breached.
- [ ] All 28 shutters closed by the lockdown and seen opening again.
- [ ] The purge survived by climbing, and the purge died to (deliberately).
- [ ] Extraction completed by a squad, and failed by a squad that loitered.
- [ ] Extraction contested: the timer froze and unfroze.
- [ ] FIRE DRILL played on all three points, and won on points.
- [ ] The offline mode played end to end with bots.
- [ ] Skydiving? No. There is no skydiving. Never mind.
- [ ] Every voice line triggered at least once, via `ob.VoiceLoopback 1`.

## 8. Interface and settings

- [ ] A full match played without opening the menu.
- [ ] Every setting changed, applied, and survived a restart.
- [ ] Every key rebound, including a conflict, and the conflict resolution works.
- [ ] Controller: a full match played on a pad, including the menus.
- [ ] All four colour-blind palettes checked in a real firefight.
- [ ] HUD at 80% and 120%: everything still legible.
- [ ] Subtitles on for a full match: every voice line has a line.
- [ ] Localisation: no string hard-coded outside the string table (spot-check 20).

## 9. Audio

- [ ] A match on headphones: gunfire through floors is locatable.
- [ ] Proximity voice through a wall: quieter and duller, as documented.
- [ ] Music transitions: match phases, lockdown, extraction, results — no clicks.
- [ ] The mix at -23 LUFS integrated; no true peak above -3 dBFS.
- [ ] The alarm and the extraction beacon are audible over a firefight.
- [ ] Accessibility: the audio visualisation for the alarm and the beacon works.

## 10. Performance

- [ ] 1080p High on the minimum GPU: 60 fps, 99th percentile above 45.
- [ ] 1440p Epic on the recommended GPU: 60 fps.
- [ ] Memory inside the budget on a 60-minute session.
- [ ] Server tick inside budget with 12 players in a firefight.
- [ ] No hitch above 30 ms in a full match (captured trace, not eyeballed).
- [ ] First-shot shader hitch: none, on a cooked Shipping build.

## 11. Steam

- [ ] Steampipe depot uploads: game content **and** the server tool depot.
- [ ] `steam_app_build.vdf`: AppID and both depot IDs replaced from the
      placeholders (480 / 480001 / 480002) with the real values.
- [ ] Steam Dev AppID replaced in `Config/DefaultEngine.ini`.
- [ ] Achievements and stats upload cleanly.
- [ ] Steam Input action manifest matches the four mapping contexts.
- [ ] Cloud saves: settings and keybinds survive a second machine.
- [ ] Store page assets all uploaded and free of HUD, of placeholder art, and of
      the words "work in progress".

## 12. Legal and licensing

- [ ] `Docs/LICENCES.md` lists every third-party asset with a licence type.
- [ ] No marketplace asset is redistributed in a form its licence forbids.
- [ ] The wordmark is original and has been checked against the trademark
      databases for the UK, EU and US.
- [ ] The building is *inspired by*, not a copy of, any real address: no real
      company name, logo or tenant appears in the game.
- [ ] No real weapon manufacturer is named in-game or in marketing.
- [ ] Voice actors have signed contracts covering worldwide, perpetual use.
- [ ] Music and SFX have commercial and sync licences on file.
- [ ] Privacy notice and the data-handling page are live (voice is not recorded
      or stored; state it plainly).
- [ ] Age rating submitted: PEGI 16 / ESRB M, and the content descriptors match
      what is actually in the build.

## 13. Marketing

- [ ] Capsules: five assets, correct sizes, brand palette, no HUD.
- [ ] Screenshots: ten, ordered, all from the shipping build.
- [ ] Trailer: 96 seconds, cut per `Docs/19_Steam_Marketing_Trailer.md` §4.
- [ ] Press kit live: logo pack, fact sheet, 6 clips, 12 screenshots.
- [ ] Launch build handed to the press list 72 hours before the embargo lifts.
- [ ] Launch post drafted for the community, written in the game's own voice.

## 14. Launch day

- [ ] Dedicated servers up in EU-West and EU-North, tick budget verified.
- [ ] A 6-hour soak: 12 players progressing through a full match repeatedly.
- [ ] Crash reporter wired and the first hour's reports on a screen somebody is
      watching.
- [ ] `ob.Security.Verbose` off, log retention set, and the server audit line's
      rotation confirmed.
- [ ] Rollback plan: the previous depot is still live and can be set as default in
      one Steamworks action.
- [ ] The launch tweet, the Steam announcement and the community post are all
      scheduled, all in the same voice, and none of them contain a pun.

## 15. Post-launch, first 72 hours

- [ ] Community thread triaged every 4 hours.
- [ ] Crashes grouped by signature, top three reproduced.
- [ ] The tick budget watched on live servers, not on a test server.
- [ ] Hotfix build staged, tested with the smoke suite, and *only* shipped if the
      smoke suite passes. A hotfix that breaks ASCENT is worse than the bug.
- [ ] Patch notes name every change, in plain language, including the ones that
      were a mistake.

---

## Appendix — the three checks that matter most

If a release is running late and something has to be dropped, these are the three
that cannot be:

1. **`validate_project.py` reports 0 FAIL.** Everything else depends on the
   content existing at the paths the code expects.
2. **The 12-player smoke test completes with a MatchResult.** It is the only test
   that proves the game is a game.
3. **The extracted ending and the `NoSurvivors` ending both work.** They are the
   two memories players take away, and a broken ending undoes a good match.
