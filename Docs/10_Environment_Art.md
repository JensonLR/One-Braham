# ONE BRAHAM — environment art

The materials, the lighting, the dressing and the set-dressing principles that
turn eighteen storeys of correctly-scaled blockout into a building a player
believes in.

The materials in section 2 are **authored as real node graphs** by
`Tools/Python/onebraham/build_materials.py`. The graphs are reproduced here so
they can be reviewed, rebuilt by hand, or extended without reading Python.

---

## 1. The look, in one paragraph

A 2000s London telecoms tower, interior, in November: cold daylight from big
glazing, warm task lighting in the offices, a lot of light grey and deep navy,
brushed steel where the building is expensive and plain plasterboard where it is
not. Wet concrete on the ground floor. Lumen global illumination at quality
"High" with hardware ray tracing off by default, so nothing in the art direction
depends on a feature most players will not have enabled. The building is
*maintained*, not new: everything has four to eight years of wear on it, and
nothing has been cleaned this week.

**Nothing in the art direction is stylised.** No exaggerated proportions, no
saturated colour ramps, no "game" lighting. If a frame from this game could be
mistaken for an architectural photograph of an empty building with a fight
happening in it, the art direction is working.

---

## 2. Master materials

Six masters, authored at `/Game/OneBraham/Materials/`. Every instance in the
game is one of these with parameters, because fifteen masters is how a project
loses its shader cache and its frame time at the same time. Five of the six are
surface masters; the sixth (2.7, `M_OB_Highlight`) is a post-process material,
which is why the instance count is 28 and not 29.

### 2.1 `M_OB_Surface` — the lit opaque master

```
  [VectorParameter BaseColour (0.5,0.5,0.5,1)] ──────────────► BaseColor
  [ScalarParameter  Roughness  (0.50)] ─────────────────────► Roughness
  [ScalarParameter  Metallic   (0.00)] ─────────────────────► Metallic
  [ScalarParameter  Specular   (0.50)] ─────────────────────► Specular
  [ScalarParameter  AmbientOcclusion (1.0)] ────────────────► AmbientOcclusion

  [VectorParameter EmissiveColour (1,1,1,1)] ─┐
                                              ├─[Multiply]──► EmissiveColor
  [ScalarParameter EmissiveStrength (0.0)] ───┘

  [TextureCoordinate] ─┐
                       ├─[Multiply]──► UVs ─► [TextureSampleParameter2D NormalMap
  [ScalarParameter UVTiling] ─┘                     (sampler = Normal, default empty)]
                                                            │
  [Constant3Vector (0,0,1)] ─┐                              │
                             ├─[Lerp (A, B, Alpha)] ◄───────┘
  [ScalarParameter           │
   DetailNormalStrength] ────┘                              │
                                                            ▼
                                                         Normal

  [ScalarParameter SurfaceTypeIndex (2)]   (unconnected: read by tools and by the
                                            impact system for surface lookup)
```

**Why the normal lerp matters:** `NormalMap` defaults to empty, which samples as
black, which is an invalid normal. Lerping from a flat `(0,0,1)` by
`DetailNormalStrength` (default 0) means a fresh instance with no texture is
*correct* rather than subtly wrong. Every artist who has shipped a UE project has
lost an afternoon to that black normal.

Shading: default lit, opaque, two-sided off, `bUseEmissiveForDynamicAreaLighting`
on (the signage and screens must light their own alcoves).

### 2.2 `M_OB_Glass`

Same as above, plus:

```
  [ScalarParameter Opacity (0.15)] ─────────────────────────► Opacity
  [ScalarParameter Refraction (1.02)] ──────────────────────► Refraction
```

Blend mode `Translucent`, shading model default lit, two-sided off,
`TranslucencyLightingMode = Surface PerPixel`. Glass in this game is
**architectural**: it must be see-through and it must not cost a full scene read
per pane. Twelve panes of refraction is a frame-time conversation, so the master
caps `Refraction` at 1.4 and the map uses `M_OB_Glass_Office` (opacity 0.30, no
refraction) for anything seen in bulk.

### 2.3 `M_OB_Emissive`

Unlit. `EmissiveColour × EmissiveStrength → EmissiveColor`, `EmissiveStrength`
slider 0–60. Used for signs, exit signs, the reader LEDs, alarm beacons and desk
task lights. Default 3.0 so a plain instance reads as lit.

### 2.4 `M_OB_Screen`

Unlit, `ScreenTexture × ScreenTint × ScreenBrightness → EmissiveColor`, with
`ScreenBrightness` default 2.0 up to 20. The lift indicators, the NOC monitors
and the terminals. Screens that are *on* are how a building says it has power,
and during the purge the screens are the first thing to go.

### 2.5 `M_OB_Decal`

Deferred decal. `DecalTexture × BaseColour → BaseColor`, `Opacity`, and a normal
parameter for the plaster craters. Six instances; see
`Docs/11_Niagara_VFX.md` section 5.

### 2.6 The instance set

Fourteen surface instances (`MI_<Surface>`) plus fourteen brand instances
(`MI_OB_Brand_*`, `MI_OB_Signage_*`, `MI_OB_Glass_*`, `MI_OB_Puddle_Dirty`).
Parameter values per surface are the table in
`Tools/Python/onebraham/build_environment.py` (`SURFACES`): colour key,
roughness, and whether the surface is metal enough for reflections (metal 0.85
metallic on Metal, Grating, Decking, ServerRack).

Every instance also carries a **physical material**, and that is the part that
matters most: the physical material is what drives footsteps, bullet decals,
impact VFX, bounce audio and friction. Get it wrong and a marble floor sounds
like carpet.

### 2.7 `M_OB_Highlight` - the interaction outline

The one master that is not a surface. It is a **post-process material**
(`MD_POST_PROCESS`, unlit, opaque) applied as a blendable on every floor's
unbound `PostProcessVolume` by `build_environment.py`.

```
  SceneTexture:PostProcessInput0 ----------------.
                                                  Add --> EmissiveColor
  SceneTexture:CustomStencil  .                   |
      Subtract(StencilTarget) |                   |
      Abs                     +--> OneMinus --> Multiply(OutlineColour)
      Saturate                |
                              '--  (1 - saturate(|S - 2|)) is 1 on the object the
                                   player is looking at and 0 on everything else
```

Why it exists: **seven interactables already write this stencil and nothing read
it.** `AOneBrahamKeycard` sets custom-depth stencil 2 while highlighted and 1
while not; `SetInteractionHighlight_Implementation` on the access zone, the
breachable wall, the override terminal, the pickup, `AOneBrahamCharacter` and the
winch console on the Skydeck sets 2 on the highlighted case. `AOneBrahamPlayerController::UpdateInteractionTarget`
calls it as the trace target changes. All of that ran on a value that no material
consumed, which is why the outline was in the code, in three documents and in the
HUD, and nowhere on screen.

Two parameters, both on the volume's material instance rather than in the graph:

| Parameter | Default | What it is |
| --- | --- | --- |
| `OutlineColour` | Signal Blue `(0.078, 0.522, 1.000)` | the stencil-2 tint; brand colour, deliberately not a red enemy outline |
| `StencilTarget` | `2.0` | the stencil value that means "the player is looking at this". **Slider range 0-255.** If the outline ever covers ordinary geometry, the `CustomStencil` node is handing back a normalised value and this wants `0.0078` (2/255) instead - one number, changed on the instance, no rebuild |

The outline is a *tint over the object*, not a six-pixel shell outline: in a game
where the prompt already tells you the verb, the highlight's job is to say "that
one", and a tinted edge does that without drawing a shape that has to be art
directed per object type.

---

## 3. Lighting per area

| Area | Key light | Fill | Accent | Notes |
| --- | --- | --- | --- | --- |
| Ground Plaza | Directional, overcast, 3 lux eq | SkyLight, HDRI grey | Canopy downlights 3000 K | Wet-surface roughness 0.08 on the plaza |
| Lobby | Directional through glazing | SkyLight + 3 area fills | Reception fascia 4000 K backlit | The most beautiful room in the game; light it like it |
| Mezzanine | Directional, high | Rect lights over the seating | Coffee point 2700 K | Daylight from the atrium is the only strong source |
| Open Plan 3–8 | SkyLight + ceiling grid | Recessed grid 4000 K every 3 m | Task lamps 2700 K | The grid is the floor's identity: even, flat, corporate |
| Server 9–11 | No daylight at all | 4000 K strips | Rack LEDs (blue and green) | The only area with no exterior light, which is why it feels wrong |
| Executive 12 | Golden-hour directional through glazing | Low warm fill | Boardroom pendant | The one area where the light is *good* |
| Canteen 13–14 | Directional, two elevations of glazing | SkyLight | Servery 4000 K | Brightest room in the building; the hardest place to hide |
| Skydeck 15 | Dusk directional, low | SkyLight | 4 floodlight masts, 6000 K pools | Pools of light with hard shadows; the pad is half-lit by design |

Rules:

1. **No light without a source.** Every point or rect light is inside a fixture
   mesh. A floating light is the fastest way to make a realistic interior look
   like a level.
2. **Lumen at High, not Epic.** GI quality high, reflections high, no hardware
   ray tracing required. The game must look like itself on a GTX 1660.
3. **The emergency lighting is a separate layer.** During lockdown and purge, red
   comes up and white goes down, and the exit signs stay lit: 26 of them across
   the building, and they are the player's wayfinding when the lights fail.
4. **The Skydeck at dusk is the lighting showcase.** It is the only place the
   player is outside, it is the end of the match, and it is the frame that goes on
   the store page.

---

## 4. Dressing principles

Sixty-three prop kinds, 827 instances, catalogued with dimensions in
`Tools/Python/onebraham/build_props.py` and placed by the pipeline in
`Docs/17_Build_Automation.md`.

The rule that governs all of it: **props exist to break sightlines, provide
cover, or tell you whose floor this is — usually two of the three.**

| Principle | Example |
| --- | --- |
| Cover is furniture | A desk is crouch cover, a filing cabinet is full cover, a sofa is soft cover. The map's fight shape comes from the furniture, not from walls |
| Density by floor type | Open plan: high density, low objects (desks, chairs). Server: low density, tall objects (racks). Executive: low density, expensive objects |
| Nothing is tidy | Chairs turned out, one monitor still on, a jacket over a chair, a tray left on a desk. Tidiness reads as a game level |
| Wear on surfaces | Scuffed lino at the lift doors, cleaned-orange peel on the carpet, worn stair nosings, hand marks at 1.1 m on every push door |
| Specific paper | The noticeboard has real notices. The whiteboard has a real schedule. Nobody reads them, and everybody registers them |
| One recurring object | The lanyard. Every body has one, every archetype wears one, and it is the strongest silhouette in the game |

---

## 5. Wear, damage and storytelling

Damage is a storytelling layer, not a decoration layer. The building tells the
player what happened before they arrived:

* **Floors 3–8**: abandoned mid-morning. Coats on chairs, a printer mid-job, a
  meeting room frozen with the door open.
* **Floor 9**: somebody tried to lock it down properly. Racks pushed against the
  door from inside, a chair braced under the handle.
* **Floor 12**: the executive floor is untouched. That is the point, and it is the
  most political thing in the game.
* **Floor 13–14**: the canteen is mid-service. It is the only warm room in the
  building and the only one with food on the tables.
* **Floor 15**: the roof is a working plant deck. Nothing has been staged there.
  The only human trace is the winch control room, which is on: a lit console
  screen against an unlit deck. That screen is the single most saturated surface
  on the Skydeck and its colour is the game's clearest state read - standby blue,
  amber once the winch is called, green on the wire, red when the attempt is
  contested or aborted. It is deliberately the only thing up there that changes.
* **Ground floor**: the turnstiles are stuck in the open position. Someone
  released them.

During a match, damage accumulates: bullet holes in plasterboard, glass broken
out of partitions, fire doors that will not close again. It is all consequences
of the systems (see `Docs/11_Niagara_VFX.md`), never scripted.

---

## 6. Streaming and the floors

* `L_Persistent_OneBraham` holds the sky, the atmosphere, the facade, the lifts,
  the gameplay actors and Aldgate East around the tower (Docs/09 §2.9).
* One level per floor (`L_OneBraham_F00` … `F15`), built by the map builder and
  attached to the persistent level **always loaded**: eighteen small floors fit
  in memory together, and floors seen through the stairs and glazing must not
  pop in.
* The Skydeck level uses a **different directional light angle** — the same dusk,
  seen from above the cloud line of the rest of the building. It is the only way
  the roof reads as a different place without a loading screen.
* Every floor's level contains its own lighting, fog and audio volumes, so a
  streamed floor arriving is complete rather than half-lit for a frame.

## 7. Reference: the real building's interiors

Notes from public photographs of the real One Braham, Aldgate (October 2026),
for look and layout only. The game's building is fictional: no real company's
logo, signage or brand colour goes into it.

| Where | What the real building does | What it gives the game |
| --- | --- | --- |
| Office floors (1–14) | Exposed services ceiling painted dark, large ring pendant lights and linear LED strips, pale concrete-grey floors, column-free plate around the core, full-height curtain wall onto the City towers | `M_OB_Surface` ceiling in charcoal with visible ducts; ring-light props in open plan; the window line is the brightest thing on every floor, which is what silhouettes a player against it |
| Furniture | Light-oak islands and long benching, grey soft-seating clusters, meeting booths, planters everywhere | Island desks and booths are waist-high cover; planters are soft cover that a round passes through |
| Feature stairs | Open-tread steel stairs with glass balustrades, near the glass | A stair you can be seen on from the floor below: a fast route that is also exposed |
| Reception (ground) | Double-height, dark feature wall, pale stone floor, turnstiles, glazed front | The ground floor reads as stone and glass, not carpet; the turnstiles are the first choke |
| Breakout kitchens | Long timber servery counters under pendant lights | The canteen servery on 15: hard cover for its whole length |
| Roof terrace | Large dark steel planters with grasses, pale paving, glass balustrade, skyline | The Skydeck balcony: planters are the only cover on the open deck, the balustrade is glass so nothing hides at the edge |

### What the published case studies add

Facts from fit-out and supplier case studies (paraphrased; sources below). Where
sources disagree it is said so.

* **Reception.** Two storeys, entered from the north and the east through
  all-glass revolving doors, with a structural-glass balustrade gallery above
  and a large media wall. A line of black glass speed gates crosses the floor,
  with more gate banks in front of the lifts. The game's turnstile banks are the
  same choke, and the gallery is an overlook onto it.
* **Office floors.** Large column-free spans (steel frame, up to about 13.7 m),
  no suspended ceilings - services, beams and the underside of the deck left
  exposed - raised floors, carpet tiles, and furniture in "neighbourhoods":
  timber benching, booths, sofas and glass meeting rooms. Full-height glass
  behind vertical fins throws banded light and shadow across the floor.
* **The feature stair.** A staircase runs up through the heart of the building
  behind see-through balustrades, and is visible from outside through the
  facade. A fast vertical route that is also the most exposed one.
* **Canteen and community floors.** Some floors carry a café, a restaurant and
  social space; the servery is a set of separate food islands rather than one
  counter. Counters and dense tables are the cover.
* **Upper floors.** Open terraces on 15 and 17 (Severfield), so the tower
  steps back twice; an atrium through parts of 14–17; a rooftop Winter Garden
  bar and roof terrace (McLaughlin & Harvey). The terraces in photographs:
  pale large-format paving, long charcoal planters of grasses and multi-stem
  trees, frameless glass balustrades, the City cluster beyond.
* **Facade.** Fully glazed, unitised, with "soft corners" and dense vertical
  aluminium fins on the east, south and west elevations; a glass-finned stick
  curtain wall at ground level (Focchi, AGC).
* **Reception.** Double height. A black linear-baffle ceiling with sweeping
  LED light ribbons, some in a violet-to-blue gradient; pale terrazzo; a
  black-clad volume behind a white desk; full-height glazing with black
  mullions; structural glass balustrades (BA Systems, The Light Lab).
* **Below ground.** A basement of cycle stands, lockers and showers.
* **Height.** 18 storeys (19 counting differently). The game builds all of
  them: the ground floor, 1–14, the canteen and Skydeck on 15, and the crown,
  16 and 17. The owner's logo is deliberately not reproduced: the tower in the
  game is "OB".

Sources: [Severfield](https://www.severfield.com/projects/one-braham),
[Barbour product search](https://www.barbourproductsearch.info/one-braham-london-news120205.html),
[HDR](https://www.hdrinc.com/node/9564),
[RLB](https://www.rlb.com/africa/projects/bt-london-one-braham),
[McLaughlin & Harvey](https://mclh.co.uk/?p=1118),
[Focchi](https://www.focchi.it/us/projects/one-braham),
[AGC](https://www.agc-yourglass.com/en/project-case-studies/one-braham),
[NLA](https://nla.london/projects/one-braham),
[Buildington](https://www.buildington.co.uk/buildings/4813/england/london-e1/1-braham-street/one-braham).

### The ground floor, from photographs

* **Revolving doors**: three tall all-glass drums in the frontage, slim
  stainless frames, opening onto the plaza.
* **Reception**: a long desk in front of a dark feature wall; a pale speckled
  terrazzo floor throughout; planters along the glass.
* **Gate line**: black glass speed gates with lit lanes, set against warm timber
  panelling, the lifts beyond them.
* **Escalators**: behind the gates, in an enclosure lit in a deep violet - the
  one saturated colour in a monochrome floor. In the game the enclosure is dark
  metal and its light is the floor's accent.
* **Lift lobby**: timber-lined walls, metal lift doors, the glass lift at the
  back catching the light from above.
