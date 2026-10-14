# ONE BRAHAM — your first day on a Mac

From a Mac with nothing installed to the game open in the editor. Each step says
what you should see, and what to do when you see something else. The first
compile **will** produce errors: the C++ has never been through a compiler. Step 5
says how to send them back so they can be fixed in bulk.

Requirements for UE 5.5, from Epic's
[macOS development requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine):
macOS Ventura 13.5 or newer, Xcode 15.2 or newer (15.4+ recommended), Apple
Silicon, 16 GB of memory at minimum (32 GB recommended). The engine and Xcode
together take well over 100 GB of disk.

---

## 1. Xcode

1. Install Xcode from the App Store.
2. Open it once, accept the licence, and let it install its components.
3. In Terminal:

   ```
   sudo xcode-select -s /Applications/Xcode.app
   xcodebuild -runFirstLaunch
   ```

**You should see:** `xcode-select -p` prints `/Applications/Xcode.app/Contents/Developer`.

## 2. Unreal Engine 5.5

1. Install the Epic Games Launcher from epicgames.com and sign in.
2. Unreal Engine → Library → the `+` next to Engine Versions → choose **5.5** (the
   newest 5.5.x) → Install. Keep the default location,
   `/Users/Shared/Epic Games/UE_5.5`; the scripts look there.

A newer engine (5.6 and up) also opens the project. Check that version's row in
Epic's table first, because each release pins its own Xcode.

## 3. The code

```
git clone https://github.com/JensonLR/One-Braham.git
cd One-Braham
git checkout claude/charming-sagan-tkhb3f
```

That branch is pull request #4. Once it is merged, use `main` instead.

## 4. The checks that need no engine

```
Scripts/RunAll.sh
```

**You should see:** `BUILD OK: 19 step(s) succeeded`, then the validator with 2
warnings. The warnings say the map has not been built, which is true until step 6.

## 5. Compile the editor

```
Scripts/BuildEditor.sh --generate 2>&1 | tee build.log
```

This writes the Xcode project and compiles `OneBrahamEditor`. The first run takes a
while.

**You should see, the first time:** errors. Collect the first forty with

```
grep -m 40 -E "error:|error [A-Z]+[0-9]+" build.log
```

and paste them into the session. Errors repeat in families, so fixing the first
forty usually clears hundreds. Repeat until the build ends with
`OneBrahamEditor built.`

## 6. Build the content

```
Scripts/BuildContent.sh
```

This runs the content builders inside the editor: materials, the fifteen floors,
weapons, the cast, audio, input, the interface and the effects. The report lands
in `Saved/Build/`.

**Read the report, not just the exit code.** Every line marked failed or skipped
names the asset and the reason. Paste the failed lines into the session.

## 7. Play

1. Double-click `OneBraham.uproject`, or open it from the Launcher's Library.
2. Open `Content/OneBraham/Maps/L_MainMenu`.
3. Press Play. The project is set to start three networked clients in one process
   (Play In Editor, net mode Client), so you see a real multiplayer session.

Without the Steam client running, online features are off. Practice matches
(bots) and LAN games still work.

## What a Mac cannot do

- **Package the shipped game.** The Windows client is packaged on Windows, and the
  server on Windows or Linux.
- **Build the dedicated server.** That needs Unreal built from source, on any
  platform (README_BUILD.md). You do not need it until you test real
  multiplayer servers.
- **Nanite and virtual shadow maps on an M1.** Both need an M2 or newer. On an M1
  the engine draws the fallback meshes and ordinary shadows; the game still runs.

## When something else happens

| You see | Do |
| --- | --- |
| `Unreal Engine was not found` | `export UE_ROOT="/Users/Shared/Epic Games/UE_5.5"` (or wherever you installed it) and run the script again |
| `Xcode is not set up` | step 1 again, including `xcode-select` |
| The editor asks to rebuild modules when you open the project | say yes once; if it fails, run step 5 from the terminal instead and send the errors |
| The editor says a plugin is missing | send the plugin's name: the project's plugin list is checked against Epic's, so this would be news |
| `RunAll.sh` fails | send the failing step's lines: those checks need no engine, so a failure there is a fault in the repository |
