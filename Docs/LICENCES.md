# ONE BRAHAM — third-party licences

Every third-party asset in the project, its source, its licence, and the
permission that licence gives. **Nothing ships that is not on this page.**

Keep this file current. An entry added here is the difference between a released
game and a legal review that ends one.

---

## How to add an entry

1. Record the asset, the source, the purchase or download date, the licence type,
   and any credit requirement.
2. Save the licence text (PDF or the receipt page) to `Docs/Licences/<source>/`.
3. Check the four permissions an entry must have:
   * commercial use
   * redistribution **inside a cooked build**
   * modification
   * no mandatory on-screen credit (or note the credit — the credits screen is
     generated from this table)
4. If any of the four is unclear, do not ship the asset. Find a substitute in
   `Docs/16_Marketplace_Fallbacks.md`.

---

## Engine and plugins

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| Unreal Engine 5.5 | Epic Games | Epic Games EULA | Royalty terms apply above the revenue threshold. This project's budget model assumes them |
| Chaos Cloth, Hair Strands | Epic Games | Engine EULA | shipped with the engine |
| Enhanced Input, CommonUI, Niagara, Sound Utilities, Synthesis, Metasound | Epic Games | Engine EULA | |
| Online Subsystem Steam + Utils | Epic Games | Engine EULA | requires a Steamworks partner account |
| Modeling Tools Editor Mode, Editor Scripting Utilities | Epic Games | Engine EULA | editor-only |

## Engine default content

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| `/Engine/BasicShapes/Cube`, `Capsule`, etc. | Epic Games | Engine EULA | used as the blockout primitive **and** as the fallback geometry the pipeline places when an art asset is not yet imported. Not shipping content: it is replaced by real meshes before a release, and `validate_project.py` reports any surface still on it |

## Sound

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| Procedurally generated cues | this project | owned | `OneBrahamProceduralAudio.cpp` synthesises the whole bank: every cue in `OneBrahamAudioBankAsset.cpp` has a working fallback, so a build with no audio assets is still fully audible |
| Recorded weapon, footstep, building foley and ambience | licensed library or commissioned | **TBC at recording** | the brief and the priority order are in `Docs/12_Audio.md`. Fill in the library name, the date and the licence type before shipping |
| Music: 8 beds | commissioned or buyout library | **TBC at commissioning** | must include a perpetual, worldwide, commercial sync licence |

## Voice

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| Six actors, 161 lines | contracted performers | **TBC at recording** | the contract must cover worldwide, perpetual, commercial use including marketing; the casting sheet is in `Docs/14_Voice_Script.md` |
| No synthetic voice | — | — | not used, and not planned. A disclosed synthetic performance is a licensing question this project does not need |

## Art

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| Character bodies, faces, grooms | MetaHuman Creator | Epic Games content licence | export is permitted for use in Unreal projects |
| Environment, prop, weapon and animation packs | Unreal Engine Marketplace / Fab | **per pack** | record each pack separately. Watch for packs whose licence forbids redistribution |
| Textures (surfaces) | Quixel Megascans | free for use in Unreal Engine projects | the default answer for all ten surfaces |
| Wordmark and all signage | this project | owned | original by design; avoid any resemblance to a real telecom operator's mark |
| Building floor plans and layout | this project | owned | *inspired by* a London tower, not derived from any real floor plan |

## Fonts

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| `F_OneBrahamSans` faces | **TBC** | must be an open or commercially licensed grotesque | the store page must not use a font whose licence forbids commercial use. Check before the capsule art is cut, not after |

## Software

| Item | Source | Licence | Notes |
| --- | --- | --- | --- |
| Steamworks SDK and SteamCMD | Valve | Steamworks terms | requires the Steam Distribution Agreement |
| Python 3 | Python Software Foundation | PSF licence | used by the content pipeline only; not distributed |
| Blender, Substance, Houdini and any other DCC used | — | **per tool** | not distributed with the game, but record what was used to author shipped assets |

---

## What is never permitted

| Item | Why |
| --- | --- |
| An asset whose licence forbids redistribution in a build | a game *is* a redistribution |
| A font or texture pack licensed to a name rather than to a company | it cannot be transferred if the studio's structure changes |
| Marketplace audio that includes a recognisable brand, siren or broadcast | the sync licence does not cover the underlying right |
| Anything generated from a copyrighted image, logo or a real person's likeness | the output inherits the problem |
| Real weapon manufacturer names or logos | trademark, and this project's weapons are deliberately generic |
| Any real company's name, logo or tenant in the building | the building is inspired by an address, not a tenant list |
| An asset with no recorded source | if the source cannot be found now, it cannot be proven later |

---

## Credits

The end credits are generated from this table. An entry with a credit requirement
appears on screen automatically; an entry without one does not. If a credit is
missing from the build, the fix is in this file, not in the credits screen.
