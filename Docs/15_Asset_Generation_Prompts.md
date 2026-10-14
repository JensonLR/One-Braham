# ONE BRAHAM — asset generation prompts and art briefs

Exact prompts, briefs and specifications for every asset that has to be made by
a person or a generative tool rather than by a script. Written so that two
different artists — or two different runs of the same tool — produce work that
belongs in the same game.

---

## 0. The rules that apply to everything in this document

**ONE BRAHAM is a realistic game.** Nothing cartoon, nothing stylised, nothing
"fun". A player should be able to look at any asset in this game and believe it
belongs in a telecoms building in Aldgate on a wet Tuesday, with people in it who
have jobs.

Three prohibitions, because they are the three ways this project could go wrong:

1. **No office-supply or pun weapons.** Not one. The weapons are weapons.
2. **No exaggerated characters.** No superhero proportions, no comedy faces, no
   "personality" that reads as a caricature of a Londoner.
3. **No American action-film iconography.** No tacticool skulls, no "OPERATOR"
   patches, no blood-spatter logos, no PUNISHER-style morale patches.

**Brand palette** — the only colours any asset may introduce:

| Name | RGB (0–1) | Use |
| --- | --- | --- |
| Primary blue | 0.000, 0.439, 0.753 | signage, wayfinding, UI accent |
| Deep navy | 0.016, 0.063, 0.145 | uniforms, carpet, furniture, UI dark |
| Signal blue | 0.078, 0.522, 1.000 | screens, indicators, calls to attention |
| Safety yellow | 1.000, 0.804, 0.000 | warnings, edge markings, hi-vis |
| Emergency red | 0.867, 0.118, 0.122 | alarms, exit signs, fire points |
| Light grey | 0.878, 0.898, 0.922 | office interiors, walls, floors |
| White | 1.000, 1.000, 1.000 | signage, lighting, highlights |

**Wordmark guidance.** The wordmark must be original. It may be a neutral
grotesque, uppercase, with a slight negative tracking — the visual language of
signage — but it must not imitate a specific telecom company's logo, and it must
not use a shape that reads as one. Deliver three candidate lockups (horizontal,
stacked, and a square badge for the Steam capsule) and record the constraint:
*audiences read signage, not brands; the mark should feel like a building's, not
a company's*.

---

## 1. Environment: the eight areas

For each area, the brief is the *feeling*; the geometry is already built at
correct scale by `build_environment.py`.

### Ground Plaza and Entrance — floor 0

> **Prompt (concepting):** *Photorealistic architectural reference: the ground
> floor entrance of a 2000s London office tower at 7:40 a.m. in November. Wet
> pavement reflecting the canopy lighting, granite and dark metal cladding, glass
> revolving door with a brushed steel frame, a bank of four security turnstiles
> with red LED indicators, a commissionaire's desk, two bikes locked to a rack,
> a loading bay entrance set back on the left, low winter light and a grey sky.
> Wide-angle, 24 mm, eye level. No people.*

Mood: the building before it knows anything is happening. Narratively, this is
the last normal 60 seconds. Materials: wet concrete, granite, dark metal, glass.
Lighting: overcast daylight, canopy downlights at 3000 K. Sound: traffic, rain,
the door.

### Lobby — floor 1

> **Prompt:** *Photorealistic interior of a corporate reception: polished marble
> floor with a subtle grey-blue veining, a curved reception island in light grey
> with a backlit fascia, four columns in brushed steel, a bank of three lifts in
> dark metal with illuminated floor indicators, a seating area in navy fabric,
> two large planters with mature plants. Daytime, mixed lighting: daylight from
> glazing on the right, warm downlights in the ceiling coffers. 35 mm, eye level,
> architectural photography.*

Mood: the room where the game's first fight happens, and it should be beautiful
and wrong. Materials: marble (roughness 0.15), steel, navy fabric, glass.
Reverb: 2.6 s — this room carries sound across it.

### Mezzanine and Lift Lobby — floor 2

> **Prompt:** *Photorealistic corporate mezzanine: carpet in deep navy with a
> subtle linear pattern, a low seating area, meeting pods in glass, a bridge
> across the atrium with a glass balustrade, brushed steel and light grey
> everywhere, a coffee point on the far wall. Overcast daylight through the
> atrium glazing. 35 mm.*

Mood: every route up from here is visible from somewhere else. This floor exists
to be a decision.

### Open Plan Office — floors 3–8

> **Prompt:** *Photorealistic empty open-plan office floor in a London tower:
> rows of workstations with light grey MFC desks, acoustic dividers, monitors and
> keyboards left mid-work, task chairs pushed back, a whiteboard still covered in
> a project schedule, a print alcove with a large multifunction printer, ceiling
> recessed lighting in a grid, one full wall of floor-to-ceiling glazing looking
> out over a grey London skyline. Late afternoon. 35 mm, eye level.*

Mood: 350 people's jobs, interrupted. This is the floor the game plays most of
its firefights on, so the *desk rows must form corridors and sightlines*.
Materials: carpet, MFC, plasterboard, glass, lino in the alcoves.

### Server and Comms — floors 9–11

> **Prompt:** *Photorealistic data centre: four rows of 42U racks in dark navy
> with perforated doors, cold-aisle containment above, cable trays overhead,
> a CRAC unit against the wall, white LED status lights across the racks, a NOC
> workstation with four monitors on a raised floor with visible tiles, harsh
> 4000 K lighting and no daylight. Wide-angle 20 mm.*

Mood: the loudest, coldest, most dangerous room in the building. It is also the
only room where a conversation is impossible, which is a gameplay feature.
Materials: metal, grating, lino, glass (rack doors).

### Executive — floor 12

> **Prompt:** *Photorealistic executive floor: a boardroom with a 3.2 m walnut
> table and twelve high-back chairs, a credenza, floor-to-ceiling glazing with
> the City of London beyond, a corner office with a desk and a personal bathroom
> door, thick carpet, art on the walls, an award shelf, a wine fridge. Golden
> hour light raking across the table. 35 mm.*

Mood: expensive, quiet, and the first place a player realises the building has a
top. Materials: walnut, glass, wool carpet, plasterboard.

### Canteen — floors 13–14

> **Prompt:** *Photorealistic staff canteen: eighteen six-seat tables in
> light wood and steel, stacking chairs, a long stainless servery with hot
> holding units, a menu board, a tray stack, a terrace door onto the roof
> edge, big glazing on two sides, daylight. 24 mm.*

Mood: the last room before the roof, and the densest cover in the game — which
makes it the most violent room in the game. Materials: wood, stainless, lino,
glass.

### Skydeck — floor 15

> **Prompt:** *Photorealistic rooftop plant deck of a London tower at dusk:
> three air-handling units in grey steel, a painted concrete pad with a white H
> and a circle in safety yellow, three floodlight masts, a steel balustrade at
> the parapet, decking with a visible grating walkway, a winch rig with a cable,
> City of London and the Thames beyond, low sun, wind-blown surface water. 24 mm.*

Mood: the ending. Thirty seconds of everyone in the building knowing where you
are. Materials: decking, grating, concrete, steel. Reverb: almost none — the wind
takes it.

---

## 2. Character prompts

Six archetypes. The rule for each: **a person you have sat next to**, not a
character. Ages, builds and ethnicities should read as a real London office
floor, and the differences between archetypes are posture, grooming and what they
are wearing — not costume.

| Archetype | Prompt fragment |
| --- | --- |
| Business Analyst | *29, mixed heritage, mid-length dark hair tied back, slim, glasses; grey wool overcoat over a navy crew-neck, dark chinos, clean white trainers, lanyard, leather messenger bag, two meeting invites still in the notebook* |
| Graduate / Intern | *23, white British, short light-brown hair, slight build, permanent slight uncertainty in the eyes; padded olive jacket over a check shirt, black jeans, scuffed trainers, lanyard, backpack with a badge dangling off it* |
| Finance | *41, South Asian, very short black hair, stocky, unbothered; dark navy suit jacket with no tie, white shirt rolled at the cuff, grey trousers, polished shoes, expensive watch, no bag* |
| IT / Operations | *34, Black British, close-cropped hair, wiry; company-branded softshell in navy with the collar up, plain t-shirt, cargo trousers, walking shoes, lanyard with a door fob and a USB stick, tool pouch* |
| Facilities / Security | *48, white British, grey stubble, heavy build; hi-vis vest over a work shirt, utility trousers with visible wear, boots, keyring on a belt reel, radio on the shoulder, clipboard* |
| Consultant | *37, East Asian, short hair, athletic, relaxed; navy unstructured blazer over a plain white t-shirt, dark jeans, good boots, small daypack, the only one whose clothes fit perfectly* |

Every one of them must:

* Be 165–190 cm with the height variation applied by `HeightOffsetCm`.
* Have a face that has a *rest* expression, not a game expression.
* Wear the lanyard. It is the building's identity and it is the strongest
  silhouette read at 20 m.
* Look slightly tired. It is 8:15 a.m. on a working day.

---

## 3. Weapon prompts

Weapons must read as **issued**, not as **chosen**. No decorative rails, no
custom paint, no anime-adjacent shapes.

> **Common prompt:** *Photorealistic studio product shot of a [category] on a
> neutral grey background, three-quarter view, soft key light, visible wear on
> contact surfaces, matte black polymer and grey phosphate steel, no markings, no
> branding, no decoration, industrial and slightly used.*

| Weapon | Distinguishing brief |
| --- | --- |
| P99 sidearm | compact polymer service pistol, no accessory rail clutter, worn grip |
| LCP machine pistol | same family, shorter barrel, extended 20-round magazine |
| P99-S suppressed | identical with a plain matte suppressor, visibly dirty at the muzzle |
| MP5 SMG | classic 9 mm service SMG, straight magazine, slim forend, worn safety |
| MP7 PDW | compact PDW, top-folding grip, short 4.6 mm magazine |
| M870 pump | 12 bore pump, wooden or matte black polymer furniture, visibly cycling forend |
| Semi-auto 12 | box-fed semi-auto shotgun with a 6-round magazine and a wide bead sight |
| M4 carbine | 5.56 carbine, collapsible stock, flat-top rail with a small optic, carry-handle removed |
| MCX compact | shorter carbine, folding stock, more visible gas wear |
| L129A1 marksman | .308 with a long barrel and a large optic, the only weapon with a big scope |

Attachment set (four per weapon maximum): optic, muzzle, grip, laser/light. All
static meshes, sockets named in the weapon data asset.

---

## 4. Texture and material prompts

| Surface | Prompt / source brief |
| --- | --- |
| Marble | *polished warm-white marble with a faint grey-blue vein, 2 m × 2 m seamless, 4K* |
| Carpet | *dark navy contract carpet, tight loop, faint linear pattern, 1 m tile, 2K* |
| Concrete | *smooth grey board-formed concrete, 2 m tile, 4K, with a rough variant* |
| Metal | *brushed steel, 1 m tile, 4K, anisotropic* |
| Glass | *clean architectural glazing, 1 m tile, with a dirt-at-the-edges variant* |
| Wood | *walnut veneer, 1 m tile, 4K, book-matched* |
| Puddle | *thin standing water on concrete, masking by a puddle generator* |
| Grating | *perforated steel walkway plate, 1 m tile, 2K, alpha-driven* |
| Fabric | *navy wool upholstery, 0.5 m tile, 2K* |
| ServerRack | *dark navy perforated rack door, 0.5 m tile, 2K, alpha-driven* |
| Plasterboard | *painted white plasterboard with a subtle roller texture, 2 m tile, 2K* |
| Lino | *speckled grey contract vinyl, 1 m tile, 2K* |
| Decking | *weathered steel deck plate with visible fixings, 1 m tile, 2K* |

Every one of them must be tileable, and every one must be readable at 2 m —
because that is the distance a player is from a wall when they are pressed
against it, and it is the distance at which texture quality is judged.

---

## 5. Signage and in-world text

All signage is original and all of it is specific to *this* building:

| Sign | Content |
| --- | --- |
| Floor directory | *Floors 1–15, area names from the map: Reception, Mezzanine, Open Plan 3–8, Operations 9–11, Executive 12, Canteen 13–14, Skydeck 15* |
| Exit signs | Standard UK emergency exit pictogram, illuminated, battery-backed |
| Fire point | *FIRE POINT — BREAK GLASS*, safety yellow cabinet, red signage |
| Lift indicators | Seven-segment floor readout, amber on black |
| Desk name plates | Insertable card holders, unreadable at gameplay distance (that is realistic and it saves texture budget) |
| Notices on the noticeboard | The building's actual notices: a lift maintenance window, a fire drill reminder for Thursday, a charity cake sale, a lost umbrella. These are the environmental storytelling the brief asks for and they must be **specific and boring** |
| Whiteboard | A project schedule that is two weeks behind, in a hand you can read |

The fire drill reminder on the noticeboard is the single best piece of
environmental storytelling in the game: it is four days stale, it is in the room
you spawn in, and it does the joke for you by simply existing.

---

## 6. UI and store art

| Asset | Brief |
| --- | --- |
| Steam capsule (616×353) | The Skydeck at dusk, seen from the stairwell door: a lit pad, a helicopter silhouette, one figure crossing. Title in the wordmark, small. No faces, no weapons in the frame |
| Library capsule (600×900) | Vertical: the building at night, fifteen lit floors, one dark. Wordmark low |
| Header (460×215) | The lobby at 7:40 a.m., empty, beautiful |
| Library hero (3840×1240) | The open-plan floor, desks, grey London light |
| Screenshots (10) | One per area, plus one firefight and one extraction. No HUD in the first four |
| Trailer stills | See `Docs/19_Steam_Marketing_Trailer.md` |

---

## 7. Music and sound prompts

| Cue | Prompt |
| --- | --- |
| `MUS_Lobby` | *warm analogue pad, 60 BPM, sustained, unprocessed, 90 seconds, loopable. Corporate but sincere* |
| `MUS_MatchEarly` | *sub pulse at 55 Hz, no percussion, sparse high textures, 2 minutes, loopable* |
| `MUS_MatchMid` | *as above with a single dry kick every four bars; no melody* |
| `MUS_MatchLate` | *the Lobby melody, played once, quietly, on a detuned electric piano* |
| `MUS_Lockdown` | *low strings, rising, alarm-adjacent, 90 seconds* |
| `MUS_Extraction` | *rotor rhythm as the time signature, building, no resolution until the winch engages* |
| `MUS_Downed` | *single note, heartbeat, no development* |
| `MUS_Results` | *the Lobby theme, two semitones lower, unresolved* |

Both music beds and ambience beds must be loopable without a click, and delivered
as 48 kHz stereo bed files with a mono-compatible mix (a player on a laptop
speaker must still hear the alarm).
