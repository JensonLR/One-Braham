# ONE BRAHAM — environment art

The materials, the lighting, the dressing and the set-dressing principles that
turn fifteen floors of correctly-scaled blockout into a building a player
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

Why it exists: **six interactables already write this stencil and nothing read
it.** `AOneBrahamKeycard` sets custom-depth stencil 2 while highlighted and 1
while not; `SetInteractionHighlight_Implementation` on the access zone, the
breachable wall, the override terminal, the pickup and `AOneBrahamCharacter` sets
2 on the highlighted case. `AOneBrahamPlayerController::UpdateInteractionTarget`
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
  The only human trace is the winch control room, which is on.
* **Ground floor**: the turnstiles are stuck in the open position. Someone
  released them.

During a match, damage accumulates: bullet holes in plasterboard, glass broken
out of partitions, fire doors that will not close again. It is all consequences
of the systems (see `Docs/11_Niagara_VFX.md`), never scripted.

---

## 6. Streaming and the floors

* `L_Persistent_OneBraham` holds the ground plaza, the sky, the atmosphere and the
  lift core.
* One streaming level per floor (`L_OneBraham_F00` … `F15`), built by the map
  builder, streamed by distance and by the vertical axis.
* The Skydeck level uses a **different directional light angle** — the same dusk,
  seen from above the cloud line of the rest of the building. It is the only way
  the roof reads as a different place without a loading screen.
* Every floor's level contains its own lighting, fog and audio volumes, so a
  streamed floor arriving is complete rather than half-lit for a frame.
