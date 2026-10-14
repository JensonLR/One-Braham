# ONE BRAHAM — marketplace fallbacks

Every binary asset this project cannot generate is listed here with a
licence-safe replacement that can be bought or downloaded today, named to the
path the code already expects. Nothing in this document is a placeholder: each
row is a real substitution that produces a shipping-quality build.

**The rule for every purchase: check the licence for (a) commercial use, (b)
redistribution inside a cooked build, (c) modification, and (d) no requirement to
credit. If any of the four is unclear, do not buy it.** Resale-licence confusion
is the most expensive mistake an independent studio makes, and it is always found
by a publisher's legal review two weeks before launch.

---

## 1. How to substitute anything on this list

1. Buy or download the pack.
2. Import into `Content/ThirdParty/<PackName>/`. Never edit a third-party asset
   in place.
3. Rename (or create a redirector) so the asset lands at the path in the
   **Expected path** column.
4. Run `Scripts/BuildContent.bat` and then `--dress` to place the props.
5. Run `validate_project.py`. It will tell you what is still missing.

Renaming matters more than it sounds: the entire content pipeline finds assets by
convention. An asset at the wrong path is an asset the game cannot see, and no
error message will ever tell you that.

---

## 2. Characters

| Need | Expected path | Licence-safe source | Notes |
| --- | --- | --- | --- |
| Six body variants, MetaHuman-class | `Content/OneBraham/Characters/SK_OB_Body_<0-5>` | MetaHuman Creator (free, Epic EULA) | Export as a skeletal mesh set; keep the shared `SK_OB_Body_0_Skeleton` |
| Faces | MetaHuman face meshes | MetaHuman Creator | Six archetypes, ages 24–52, London-plausible |
| Grooms (hair) | `G_OB_Hair_<preset>` | MetaHuman grooms, or the engine's strand samples | Set `HairPresetId` per archetype |
| Office clothing | `SM_OB_Cosmetic_<Slot>_<Id>` | Unreal Marketplace: *Business / Office* character kits; Quixel Bridge clothing | Must be rigged to the MetaHuman skeleton; cloth assets come from the same pack |
| Shoes, bags, lanyards | `SM_OB_Cosmetic_<Slot>_<Id>` | Marketplace *Office / Corporate props* kits | Lanyard is a cloth asset — it is one of the six archetype silhouettes |
| First-person arms | `SK_OB_Arms_<archetype>` | Marketplace *FPS arms* packs | Sleeves are the archetype tell; cuff and colour per archetype |

If a clothing pack's licence forbids redistribution inside a build (some do), it
cannot be used, however good it looks. Read the licence, not the shop page.

---

## 3. Weapons

| Need | Expected path | Source | Notes |
| --- | --- | --- | --- |
| 10 weapon meshes, FP + world | `SK_OB_Weapon_<Name>` | Marketplace FPS weapon packs (see below) | Sockets are named in the data asset; a mesh without `Muzzle` needs a socket added |
| Attachment meshes | `SM_OB_Attachment_<Id>` | Same packs, or a modular attachment pack | Static meshes, not skeletal |
| Weapon animations | `AM_OB_Weapon_<Category>_<Anim>` | Marketplace *FPS animation* packs | Fire, reload (tactical/empty/per-shell), draw, holster, inspect, melee |
| Weapon sound | `SW_OB_Gunshot_<Calibre>` | Marketplace *weapon sound* libraries, or a recording session | Must be dry: the building's reverb is added by the game |

Category-to-pack mapping, so the armoury reads as one family rather than ten
borrowed guns:

| Category | Look | Notes |
| --- | --- | --- |
| Pistol, Compact, Suppressed | Compact service pistol, polymer frame, no decoration | The suppressed one needs a visible suppressor and a short barrel |
| SMG | 9 mm service SMG, straight magazine | The most-used primary in the game: it must look ordinary |
| PDW | Compact PDW, high-pressure round | Reads smaller than the SMG, hits harder |
| Pump / Semi shotgun | 12 bore, wooden or polymer furniture | The pump must have a visibly cycling forend |
| Carbine / Compact AR | 5.56 carbine, collapsible stock, optic rail | The default; keep it plain |
| Marksman | .308 semi-auto, long barrel, large scope | The only weapon with a big optic; the silhouette should be recognisable at 40 m |

Explicitly not needed: anything on the banned list. No staplers, no tape
dispensers, no foam dart guns, no "office weapons". The joke weapons are the
fastest way to make a serious game look cheap.

---

## 4. Environment and props

63 prop kinds are catalogued with dimensions in
`Tools/Python/payloads/prop_manifest.json`. Buy in category:

| Category | Marketplace search that covers it | Notes |
| --- | --- | --- |
| Office furniture | *Office props*, *Corporate interior* kits | Desks, chairs, cabinets, whiteboards, monitors |
| Server room | *Data centre / server room* kits | Racks, cable trays, CRAC units, patch panels |
| Lobby and reception | *Lobby*, *Hotel interior*, *Reception* kits | Sofas, planters, reception island, turnstiles |
| Canteen | *Cafeteria*, *Restaurant* kits | Tables, chairs, servery, holding units |
| Circulation | *Modular building*, *Stairwell* kits | Handrails, treads, lift doors, signage |
| Executive | *Boardroom*, *Executive office* kits | Boardroom table, credenza, art |
| Skydeck | *Rooftop*, *Industrial exterior* kits | AHU boxes, floodlight masts, railings, decking |
| Signage | *Signage and wayfinding* kits | Floor directories, exit signs, warning signs |

The single most important note for whoever dresses the building: **buy at real
scale.** The map is built at true dimensions (400 cm floor to floor, 110 cm
doors, 260 cm corridors). A pack authored at 1 unit = 1 cm will be right; a pack
authored for a 3 m hero character will be wrong everywhere, and fixing it means
rescaling 800 instances by hand.

---

## 5. Materials and textures

The master materials are authored as real node graphs by
`build_materials.py` — you do not need a material pack. You need textures:

| Need | Where | Notes |
| --- | --- | --- |
| Surface detail (albedo/normal/roughness) | Quixel Megascans (free with UE), or Substance Source | Marble, carpet, concrete, metal, glass, wood, grating, fabric, lino, decking |
| Plasterboard, painted wall | Megascans | The office interior at 10 cm detail |
| VFX sheets | Marketplace *VFX texture* packs, or the free Epic Niagara content | Smoke flipbook, sparks, dust, cracks, bullet holes |
| Decals | Same packs | Bullet holes, scuffs, glass cracks |
| Signage | Marketplace *office signage* packs, or author your own | Original wordmark only: see Docs/15 |

Megascans is free for use in Unreal Engine projects under the Quixel terms, which
makes it the default answer for all ten surfaces. Two hours of Megascans beats
two weeks of hand-painted texture work on a project of this size.

---

## 6. Animation

| Need | Source | Notes |
| --- | --- | --- |
| Locomotion set (idle, walk, jog, sprint, crouch, strafe) | Marketplace *motion-capture* packs; Mixamo for blockout | Must be retargeted to the MetaHuman skeleton |
| Archetype personality overlays | Author from the locomotion set | The six personalities are *additive* — see Docs/13 |
| Weapon handling | Marketplace FPS animation packs | Fire, reload, ADS in/out, inspect, melee |
| Downed / revive | Author, or a small mocap session | The two most character-defining animations in the game |
| Ragdoll poses | Physics, not animation | Chaos ragdoll with the cycle in `OneBrahamRagdollComponent` |
| Extraction winch | Author | 30 seconds of being winched is the game's single most cinematic moment and cannot be bought |

Mixamo output is fine for blockout and must not ship: its licence permits use in
projects but the animation quality will read as exactly what it is next to
hand-keyed or mocapped work.

---

## 7. Audio

Recorded audio is the one category where buying is genuinely second-best, because
the building's specificity is the game's identity. Still, for a build that must
ship:

| Need | Source | Notes |
| --- | --- | --- |
| Weapon foley (close, distant, suppressed) | Marketplace *weapon sound* libraries | Buy a pack with separated close/distant/mechanism layers |
| Footsteps per surface | Marketplace *footstep* libraries | 15 surfaces; look for a pack with metal, carpet, concrete, glass, water |
| Building tones (lift, shutter, terminal, alarm) | Marketplace *industrial* / *building* libraries | Or record in a real office: 30 minutes with a recorder and a lift door |
| Ambience beds | Marketplace ambience packs, or record | The lobby, the server room and the stairwell are the three that must be right |
| Voice | Six actors | `Docs/14_Voice_Script.md` is the script and the casting sheet |
| Music | Commissioned, or a buyout library | 8 beds, dry and patient. A library track with a drop will not work |

Budget note: £2,000–£4,000 of licensed audio plus two days of foley gets a build
to 90% of the audio in this document. The last 10% — the suppressed pistol, the
shutter, the winch — must be recorded, and those three are the three a player
would notice if they were wrong.

---

## 8. What must never be bought

| Item | Why |
| --- | --- |
| The wordmark | The brand is original by design. See Docs/15 for the brief and the constraint |
| The building's floor plans | Any real building's plans are somebody's property and this one is inspired by, not copied from, a real address |
| Weapon manufacturer names | The weapons are generic by category. Naming a real manufacturer without a licence is a trademark problem |
| Voice lines | Recorded by actors under contract, or not at all. Synthetic voices are a disclosure and a licensing question, not a shortcut |
| The extraction sequence | It is the game's ending. It must be authored, not assembled |

---

## 9. Reconciliation checklist

After any substitution, before it is considered done:

- [ ] Asset is at the exact expected path (run `validate_project.py`).
- [ ] Named to convention: `SM_OB_`, `SK_OB_`, `MI_`, `NS_OB_`, `SW_` (self-check:
      `Tools/Python/onebraham/common.py` holds the prefix list).
- [ ] Licence recorded in `Docs/LICENCES.md` with the source, the date, the
      licence type and the order reference.
- [ ] Scale checked in the level against a 180 cm reference.
- [ ] LODs and Nanite setting set per the asset class (`Docs/18_Testing_Performance.md`).
- [ ] Collision set: `BlockAll` for cover, `BlockAllDynamic` for loose props,
      `NoCollision` for decoration.
- [ ] Physical material assigned (this is what makes footsteps and impacts right).
- [ ] Draw-call and texture-memory cost measured before it is used in bulk.
