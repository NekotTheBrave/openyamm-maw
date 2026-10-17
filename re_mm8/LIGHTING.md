# Original MM8 lighting: indoor, outdoor, Direct3D and software

Investigated 2026-09-14 with Rizin 0.10.0 and its installed rz-ghidra decompiler.
Target: **`mm8_orig/MM8-Rel.exe`**, 1,163,264 bytes, preferred image base `0x400000`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
The hash was checked again for this investigation. This is the executable previously verified against the supplied
GOG installer; it is not the GrayFace-patched `mm8.exe`. No executable bytes or game content were changed.

For the source-based assessment of our current implementation, see
[MM8 lighting compared with OpenYAMM](OPENYAMM_LIGHTING_COMPARISON.md).

The [2026-10-01 static indoor follow-up](INDOOR_STATIC_LIGHTS.md) verifies loaded room-light associations,
load-time decoration sources and the repeated per-surface rendering work behind inexpensive dungeon lighting.

## Findings

MM8 uses several inexpensive lighting paths rather than one universal lighting equation:

| Target | Base illumination | Local illumination | Main cost reduction |
| --- | --- | --- | --- |
| Outdoor terrain and models, Direct3D | CPU sun/normal shading, time/depth-dependent color | Textured light polygons projected and clipped onto surfaces, additive passes | Cached normals, minute-gated sun update, bounded face light lists |
| Indoor faces, Direct3D | Room darkness and face/render shading state | Same projected-light machinery, using indoor face and room data | Room light lists, face bounds/plane rejection, bounded selection |
| Outdoor terrain and models, software | Compact sun shade | Local light values at polygon vertices, interpolated over edges and visible spans | Vertex sampling, span interpolation, reuse of unchanged geometry |
| Indoor faces, software | Room darkness plus face-specific shade/gradient data | Local illumination sampled along raster spans in surface coordinates | Selected face lights, cached reciprocal radii, segment endpoint sampling and palette lookup |
| Ordinary sprites/billboards | Room or outdoor base shade | Scalar brightness sampled at the billboard's world position | Cheap approximate distance; one shade for the sprite rather than surface lighting across it |

The Direct3D paths submit pretransformed, already colored vertices. They do not delegate this work to Direct3D's
fixed-function light objects. The inspected paths contain no shadow-map, stencil-shadow or light-to-receiver ray
test. Light containment comes primarily from candidate lists, distance, face orientation and clipping the light
polygon to its receiving face. This is a conclusion about the traced lighting paths, not an exhaustive claim about
every effect or asset in the game.

## 1. Classification and data limits

There are three distinct questions: where a light comes from, which rendering pass receives it, and which
attenuation calculation is selected. The binary does not use a single enum answering all three.

### Sources and storage

| Source | Verified representation | Evidence |
| --- | --- | --- |
| Mobile lights | Pool of 400, record stride 18 bytes, count at `0x52cd9c`, records at `0x52b17c` | Inserter `0x467750`; diagnostic identifies `MobileLightStack.cpp` |
| Stationary/sprite lights | Pool of 400, stride 12 bytes, count at `0x52b174`, records at `0x529eb4` | Inserter `0x4ad866`; diagnostic identifies `StationaryLightStack.cpp` |
| Permanent indoor map lights | Separate map array through `0x6f3bb4`, stride 20 bytes; room lists contain 16-bit indices | Consumers `0x43d28b`, `0x45b962`, `0x4aeaa2` |
| Outdoor sun/base state | Global direction and time-dependent shade values | `0x48a779`, `0x48a274`, `0x479d20` |

The shared 12-byte prefix is signed 16-bit XYZ, signed 16-bit radius, RGB bytes and a type byte. Mobile records also
contain additional 16-bit fields: the inserter computes a sign-dependent value of ±31, stores a room/sector argument,
and stores `(radius * radius) >> 5` in a 16-bit field. These fields are not evidence that every consumer uses
quadratic attenuation; the traced point sampler explicitly uses linear falloff.

Permanent indoor lights additionally carry flags (`+0x0c`, bit `0x08` means off), a brightness field (`+0x0e`), and
an MM8-specific ID at `+0x10`. Layout names were cross-checked against local MMExtension structures. Runtime uses
of the flags, radius and room lists were verified in MM8 instructions.

The 400-entry limits apply to the two runtime pools, **not to the total number of permanent lights in a map**.
Insertion at capacity reports “Too many mobile lights!” or “Too many stationary lights!” and returns failure.
It does not select the brightest replacement.

“Stationary” does not mean a baked lightmap. Decoration producers can refill that pool, and its lights are evaluated
at runtime. Mobile insertion callers include the outdoor monster and object producers (`0x47c1d2`, `0x47bca5`)
and indoor producers. Indoor frame preparation `0x43d049` also inserts a camera/party-position light unless its
disable flag is set. Exact spell power, flicker and producer timing formulas were not reconstructed here.

### Type bits

| Bit | Observed role | Qualification |
| --- | --- | --- |
| `0x01` | Selects the first light-polygon list, consumed by the per-surface additive draw path | Without it, the builder uses the second, later-drawn list |
| `0x04` | Selects a normal/direction-dependent response | Software vertex helper uses an absolute normal dot normalized light direction |
| `0x08` | Selects a direction-independent response | Still attenuated by distance; not a directional sun or a spotlight cone |

The small byte table at `0x4faf98` contains **5, 9, 6, 10**, consistent with combinations of pass and response bits.
It must not be confused with the permanent-map-light **off flag**, which also happens to be `0x08` but lives in a
different field.

At `0x45c181`, when both the D3D pointer and fog-active state `0xfc4ee0` are nonzero, a type containing `0x04`
is replaced with table byte 6. `0x45c1a5` scales RGB by **0.33** under the corresponding condition. This is a concrete
fog-related change to light routing and intensity, not a physically based scattering model. The indoor selector
does not call these outdoor type/color adjustment helpers.

## 2. Outdoor sunlight and day/night

`0x48a779` updates the sun state only for hours **05 through 20 inclusive**. It maps minutes since 05:00 over a
960-minute interval into the game's angle-table system, obtains two trigonometric components, sets the third axis
to zero, and stores both the direction and its negation. This is a fixed daily arc; no latitude, season or planetary
lighting model appears in this routine.

The main outdoor frame `0x47b25b`, at `0x47b381`, compares the current minute with the cached minute at `0x6f255c`
before calling the updater. The updater writes that cache only inside its daylight branch. Therefore “the sun is
updated once per minute” is a daylight optimization description, not proof that the call is skipped all night.
Initialization and other state changes can also call the updater.

The accompanying base shade at outdoor-object offset `0x84b0` is a triangular daily curve. With
`t = minutes since 05:00`, the instructions calculate approximately:

```text
baseShade = integer_conversion(20 * (1 - min(t, 960 - t) / 480))
```

The constants at `0x4ea810` and `0x4ea818` are 20 and 1/480. This produces its lowest dimming near 13:00.
Do not extend the equation outside the updater's daylight range.

`0x479d20` demonstrates the polygon sun term explicitly:

```text
sunAmount = max(0, dot(faceNormal, directionTowardSun))
shade ≈ clamp(31 - round(31 * sunAmount), 0, 31)
```

ODM model normals use fixed-point arithmetic; terrain uses indexed float normals. The terrain normal-index array
at `0x79b25c` selects entries through `0x7ab25c`, avoiding reconstruction of each triangle normal during drawing.
The same cached-polygon routine also refreshes animated texture references.

`0x48a274`, called through `0x48a854`, supplies a separate night/twilight scalar:

| Time | Scalar | Night flag `0x6f2ea8` |
| --- | --- | --- |
| Before 05:00 | 1 | Set |
| 05:00–05:59 | Falls from 1 towards 0 | Clear |
| 06:00–19:59 | 0 | Clear |
| 20:00–20:59 | Rises from 0 towards 1 | Clear |
| From 21:00 | 1 | Set |

The earlier outdoor report calls this wrapper an outdoor-fog update. Its traced callee establishes a more precise
role: **night/twilight dimming state**, also consumed by depth-dependent color calculation. Weather fog and this
day/night scalar should not be treated as synonymous.

`0x47d384`, reached through `0x4a1af1`, combines the compact shade with depth, twilight/night state and special
branches. It uses a dimming clamp of **216** in several D3D calculations, then forms RGB from `255 - dimming`.
Its indoor early branch forms grayscale from `(31 - shade) * 8`. Thus a 0–31 shade index is not a universal literal
RGB value, and the daytime, nighttime, indoor and special-color paths are not identical.

There is no terrain horizon/occluder lookup in the verified normal-dot-sun calculation. Facing away from the sun
produces darker faces, but it is not a cast shadow from another building or hill.

## 3. Indoor rooms and face selection

The runtime room stride is **0x78**. Relevant fields are light count `+0x58`, light-index pointer `+0x5c` and
darkness `+0x66`. Indoor face stride is **0x60**, with the owning room at `+0x4c`.

The hardware indoor selector `0x45b962` begins with room darkness plus the shared additional shade state,
converted to 16.16 fixed point in `0xffced4`. Its candidate order is:

1. Mobile pool.
2. Owning room's enabled permanent map lights.
3. Stationary pool.

Each loop stops once **20 accepted lights** have been collected. The checks are at `0x45b9b5`, `0x45b9f6` and
`0x45ba4a`. The corresponding outdoor-model selector `0x45bf2e` and terrain selector `0x45c1e9` use the same
20-entry capacity, considering mobile before stationary lights. This is first-accepted selection, not sorting by
distance, brightness or screen influence. A later, stronger light can lose to earlier accepted lights.

Indoor helper `0x45ba81` and outdoor-model helper `0x45bfc7` reject candidates using:

- Positive radius.
- Light center inside the face's XYZ bounding box expanded by the radius.
- Signed light-to-face-plane distance no greater than the radius.
- For ordinary stationary/permanent candidates, the light must also be on the nonnegative side of the plane.
  Mobile candidates bypass that lower-bound facing test.

Accepted records cache radius, fixed-point reciprocal radius, XYZ, normalized RGB, plane distance and type.
The terrain helper `0x45c288` has separate triangle/quad handling and comparable geometric selection.

The selectors contain no search through intervening walls, door polygons or other occluders. Room membership
limits the permanent candidate set, but mobile and stationary loops are not universally restricted to the receiver's
room. **Light leaking is a plausible consequence** where geometry/list organization does not otherwise contain the
effect. This is an inference from the traced selection and sampling, not a newly reproduced visual bug.

## 4. Direct3D: projected light polygons

Indoor hardware face drawing `0x4b0ef5`, outdoor model drawing `0x47893a`, and terrain drawing `0x4815f0` feed
`0x45ad85`, which prepares the receiving plane/basis and invokes `0x45affd` for each selected light.

The builder:

1. Projects the light center onto the receiving plane.
2. Calculates the light sphere's intersection radius with that plane:
   `patchRadius = sqrt(radius² - planeDistance²)`.
3. Creates a four-corner patch using a surface basis and UVs covering 0–1.
4. Projects the corners onto the plane, clips against the receiving polygon, then transforms/projects the result.
5. Stores color and intensity and appends the polygon to one of two light lists.

The square-root helper was checked through its wrapper: the actual `fsqrt` is at **`0x4dc3ec`**. Initial broad
analysis stopped the wrapper too early, illustrating why the C-like output alone is insufficient.

In the ordinary builder branch, the intensity is `patchRadius / radius`; a rendering-option branch instead uses
distance falloff and the type's angular response. Basis helper `0x434d08` distinguishes wall, floor/ceiling and
sloped-face classes, using a normal-Z threshold about **0.7081** to choose a stable construction basis.
These are geometric specializations, not material-based PBR classifications.

`0x45c8ba` binds a light texture, enables alpha blending, and sets source and destination blend to **ONE, ONE**
(`0x45c9d0`, `0x45c9f2`). `0x45cc44` builds pretransformed textured vertices, assigns a uniform RGB intensity for
each patch, and submits a triangle fan. The texture supplies the spatial pattern within the patch; the CPU does
not evaluate every illuminated pixel.

The late-list pass `0x45ce1e` also uses additive blending. It disables depth writes and draws with a **0.0005**
depth bias passed through `0x45cbb6` to `0x45cc44`, then restores the states it changed. Fog-enabled branches
temporarily disable and restore fog. Exact composition depends on the caller/material/fog path; do not assume
every light list is rendered with identical state or at the same moment.

The polygon draw uses FVF **0x1c4**: XYZRHW, diffuse, specular and one texture coordinate set. Outdoor flags include
`DONOTLIGHT`; indoor patch draws additionally use `DONOTCLIP | DONOTUPDATEEXTENTS` after CPU preparation.
Direct3D performs texture sampling, interpolation and blending, not the source-light evaluation.

The lists have storage for **512 and 768 records**, each `0xc1c` bytes. The builder only increments the respective
counts while below **511 and 767** (`0x45b62f`, `0x45b643`). A saturated final slot is scratch/overwritten rather
than increasing the draw count. These are polygon-list limits, not independent light-source limits. The first
count is reset for individual receiving surfaces; the second is reset during frame setup (`0x44d9c6`).

The binary's diagnostic “Lightpoly builder native indoor clipping not implemented” is real, but **does not mean
indoor lights are unclipped**. The ordinary indoor call passes a zero native clipping flag; receiving-face clipping
already occurs, and a separate caller-supplied clipping path exists. The diagnostic describes an unsupported
combination of flags in the builder.

The neighboring `0x49a89a`/`0x49aaf1` chain is a **decal polygon projector**, identified by its own diagnostics.
Although it shares geometric ideas and samples light for decals, it must not be counted as another light-source
pool merely because it is called beside light construction.

## 5. Software rendering

### Outdoors: vertex illumination and span interpolation

The software outdoor model/terrain emitters call `0x45bbf4`. It evaluates selected lights at polygon vertices
through `0x45bd7c` and accumulates a scalar in each vertex record. For positive radius and in-range samples, the
verified helper implements these response shapes, with `s` denoting the supplied strength:

```text
type & 4:  contribution = 2.0 * s * abs(dot(normalizedLightDirection, normal)) * (1 - d/r)
type & 8:  contribution = 1.3 * s * (1 - d/r)
```

`d` is the cheap approximate distance described below. The direction normalization uses a separate helper;
the angular term is **absolute**, not the sun's `max(0, dot)` rule. The selection-stage facing test remains relevant.
The pixel path does not repeat these calculations for every texel.

`0x45c53d` walks a polygon's visible spans. `0x45c59c` locates the matching left/right edges, evaluates their lighting,
interpolates the span endpoints and clamps the resulting contributions to **0–1**. This completes the lighting
role left unresolved in the earlier outdoor investigation. Texture mapping/shade-table selection then uses those
span values. It is an approximation across the polygon, so a small light in the middle of a large polygon can be
represented poorly when vertex samples miss its strongest region.

### Indoors: surface coordinates and segmented shading

Software indoor face setup `0x4aeaa2` uses room darkness plus the face-extra shade field at `+0x22` and gathers
mobile, enabled room and stationary lights in that order. Pointer bounds restrict it to **20 entries** as well.
It caches reciprocal radius and face-plane distance rather than dividing again at every shade sample.

The face rasterizer `0x4b0427` handles walls, horizontal faces and sloped faces differently when converting light
centers into surface coordinates. `0x4ae943` then samples scalar light there, using the two surface-coordinate
differences and cached plane distance. It applies local falloff and an optional face gradient, then clamps to
**0 .. 31<<16**. Thus this path has authored face/room shading data as well as runtime local lights.

The rasterizer samples at span/segment endpoints (`0x4b0891`, `0x4b09a2`). If both endpoint shades have the same
integer part, it selects one palette for the segment; otherwise it increments an interpolated fixed-point shade
across the pixels. The equal-shade test is at **`0x4b09b3–0x4b09c4`**, the constant-palette branch at **`0x4b0c0b`**, and
interpolated palette indexing at **`0x4b0a29`** onward. It does not run the full selected-light loop at every pixel.

These are runtime shade tables and projected/interpolated lighting calculations. The evidence does not support
describing the whole engine as a modern baked lightmap-atlas renderer. Permanent lights and authored face
gradients should also not be confused with freshly evaluated dynamic light polygons.

## 6. Sprite brightness and cheap distance

`0x43d224` chooses room darkness indoors or the billboard/supplied outdoor shade, then calls `0x43d28b` at the
billboard's world position. The latter visits mobile lights, indoor room lights when applicable, and stationary
lights. It first rejects by absolute X/Y/Z difference against radius.

It sorts the three nonnegative differences so that `a >= b >= c`, then approximates Euclidean distance as:

```text
d = a + ((11 * b) >> 5) + (c >> 2)
```

For `d < radius`, each light reduces dimming by approximately `30 * (1 - d/radius)`. The exact arithmetic first
forms a truncated 16.16 quotient, then multiplies by 30. Final shade is clamped to **0–31**, with **0 brightest**.
This helper does not read light RGB/type/brightness for its contribution, and does not apply the face-list
20-light limit. It is a scalar brightness sampler, even when nearby surface light patches are colored.

Software sprite selector `0x41f1bd` chooses among precomputed shade-palette banks: 32 shades per palette and
512 bytes per shade (256 packed 16-bit colors). Sprite rasterization then performs texture-index lookups rather
than a local-light loop per pixel. `0x43d1da` also has explicit full-bright/sector-zero bypasses.

Consequences include quantized brightness, limited spatial detail across large sprites, and a difference between
colored surface illumination and sprite illumination. The distance approximation also makes equal-intensity
contours imperfect spheres. These are observable implications of the algorithm; this session did not capture
runtime images to quantify them.

## 7. Optimization strategy and tradeoffs

| Technique | Verified place | Benefit / limitation |
| --- | --- | --- |
| Room-indexed permanent lights | `0x45b962`, `0x43d28b` | Avoids scanning every indoor map light; list correctness affects containment |
| Expanded bounds and plane rejection | `0x45ba81`, `0x45bfc7` | Cheap rejection before polygon building; does not establish occlusion |
| First 20 accepted surface lights | Indoor, model, terrain selectors | Bounded expensive work; order-dependent omission |
| Fixed 400-source pools | `0x467750`, `0x4ad866` | Predictable storage; insertion failure at capacity |
| Shared normals and time-gated sunlight | `0x479d20`, `0x48a779` | Avoids repeated normal/sun reconstruction |
| Reciprocal radii and fixed-point state | `0x45ba81`, `0x4aeaa2`, `0x4ae943` | Moves divides out of repeated software shade samples |
| Approximate distance | `0x43d28b`, `0x45bd7c`, `0x4ae943` | Avoids Euclidean square root for distance falloff; angular/polygon helpers can still use square roots |
| Vertex/segment sampling and palette lookup | `0x45bbf4`, `0x4b0427`, `0x41f1bd` | Reduces per-pixel arithmetic at the cost of interpolation/quantization artifacts |
| Projected light texture, additive pass | `0x45affd`, `0x45c8ba` | Localized light footprint without shaders; extra polygons, draws and overdraw |
| Reuse unchanged software outdoor geometry | `0x47b25b`, `0x479d20` | Saves traversal/transforms/span construction when valid |

The software outdoor cache has explicit lighting invalidation. `0x44d8ea` sets the rebuild bit when mobile lights
are present in software outdoor mode, and also on the following frame using its recorded frame counter. A change
in stationary-light count sets the bit as well (`0x44dab4`). This prevents describing cached geometry as unaffected
by dynamic lighting. These are verified triggers, not a complete inventory of all invalidation conditions.

There is no evidence in these paths of HDR accumulation, physically based inverse-square falloff, material roughness,
normal-map lighting, screen-space ambient occlusion, clustered/tiled light assignment or shadow-casting point lights.
Additive patches operate in the bounded output-color pipeline, while scalar paths explicitly clamp brightness.
They can wash out/saturate and do not model indirect light transport.

This is static architectural evidence, **not a performance benchmark**. It shows how work is reduced and where
hard limits exist; it does not establish which operation dominated an original machine or which technique would
speed up OpenYAMM. See [the existing renderer comparison](OPENYAMM_RENDERING_COMPARISON.md) before transferring
optimization ideas. No engine optimization is proposed or implemented by this report.

## 8. Reproduction, confidence and remaining scope

Use [RIZIN_GUIDE.md](RIZIN_GUIDE.md), the full hash above and
[the lighting annotations](mm8_rel_5326d564_lighting.rz). Labels are research names, not recovered original symbols.

```bash
sha256sum re_mm8/mm8_orig/MM8-Rel.exe
rizin -N -q -e scr.color=0 -p re_mm8/work/mm8-rel-5326d564-base.rzdb \
  -i re_mm8/mm8_rel_5326d564_lighting.rz -c 'pdf @ 0x45b962;pdf @ 0x43d28b'
```

Disposable exports and scripts from this session are under ignored `work/lighting/`. No project was saved over
the baseline. Several decompilations initially failed because inferred four-byte variables crossed the top of the
stack address space (`var_1h`). Clearing inferred variables throughout a separate project allowed decompilation;
it did not recover missing calling conventions. In particular, decompilation falsely rendered the indoor selected
count as zero and omitted its 20-entry checks: assembly verifies pointer mutation and the actual checks.
The `sqrt` wrapper also needs sequential disassembly beyond its automatically discovered function boundary.

High confidence: pool/record sizes, room indexing, selection order and caps, off/facing tests, point-sample formula,
day/night branches, sun caching, additive state, vertex format, light-patch geometry, and the inspected software
sampling/interpolation branches. Limitations and visual artifacts described as consequences are inferences.

Not fully reconstructed: every producer's flicker and spell-strength rules, exact default/debug option combinations,
all special materials and fog pass ordering, the complete shade-palette construction/gamma pipeline, upstream
map-compiler generation of room light lists and face gradients, and runtime costs or patched-executable differences.
The static indoor follow-up verifies that the loader reads and relocates the precompiled room light lists.
The absence of a shadow test in these routines does not prove that artwork lacks painted shadows.

Local behavioral/layout references, used to interpret MM8 rather than replace its evidence:

- `reference/MMExtension/Scripts/Structs/01 common structs.lua`: lights, indoor room/face layouts.
- `reference/OpenEnroth-git/src/Engine/Graphics/LightsStack.h` and `LightmapBuilder.cpp`: role/name cross-checks.
- `reference/Misc-MM8Patch-2.5.7/MMPatches/MM8Patch/Direct3D.pas`: API slots, FVF, blend and submission constants.
- [OUTDOOR_RENDERING.md](OUTDOOR_RENDERING.md): previously verified same-binary outdoor pipeline and palettes.

No reference implementation code was copied into OpenYAMM.
