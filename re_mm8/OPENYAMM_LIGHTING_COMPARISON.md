# MM8 lighting compared with current OpenYAMM

Reviewed 2026-09-14 against the **working tree**, with HEAD `504fb38f`. HEAD alone does not reproduce this snapshot.
Original-engine evidence comes from [the Rizin lighting investigation](LIGHTING.md), targeting the verified
`MM8-Rel.exe`, SHA-256 `5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
See also [the broader rendering comparison](OPENYAMM_RENDERING_COMPARISON.md).

The comparison below records the pre-change implementation. Directional outdoor sunlight has since been
[implemented and validated separately](OUTDOOR_SUNLIGHT_IMPLEMENTATION.md). Indoor gaps 2 and 3 below (room membership
of the static bake, per-room ambient) were addressed on 2026-10-16 by phase 5 of
[the lighting and materials plan](../docs/LIGHTING_AND_MATERIALS_ARCHITECTURE.md) (section 5.1): lights reach rooms
through portals, and each room keeps its own ambient.

This is a source/binary comparison, not a benchmark or a visual reproduction test. No engine behavior was changed.
The scope is ordinary MM6–MM8 BLV/ODM geometry and billboards. Authored/lightmapped `BModelWorld` content has a
separate path and must not be used as evidence that classic ODM terrain already has lightmaps or directional sun.

## Assessment

**Keep OpenYAMM's rendering architecture and improve its reproduction of MM8's lighting behavior.** Cached static
illumination, persistent GPU geometry, bounded shader light lists and cheap billboard sampling suit the project's
desktop/Android targets. Recreating MM8's CPU-projected additive light polygons would bring back surface processing,
extra submissions and blending work. That is an architectural judgment, not a measured speed comparison.

However, current OpenYAMM lighting is not uniformly more sophisticated. It has smooth RGB contributions and better
light prioritization, but its classic surfaces lack normal-dependent illumination. Its indoor static bake also
discards room light membership, while room ambient is collapsed into one value for the visible scene. These are
more useful fidelity improvements than increasing the number of shader lights.

## Approach and cost

| Area | Original MM8 | Current OpenYAMM | Assessment |
| --- | --- | --- | --- |
| Outdoor sun | CPU sun-direction/normal shading, fixed daily arc, cached daylight updates | Time/day fog and dimming; classic surface shader has no normal or sun direction | Restore directional shape cues within our existing shader/mesh path |
| Indoor static lights | Room-selected runtime lighting; D3D projected patches or software shade interpolation | RGB baked into mesh vertices, subdividing large lit triangles; updates when light state changes | Keep caching, but preserve room membership and receiving-surface behavior |
| Dynamic surfaces | CPU selection followed by additive light polygons in D3D; vertex/span sampling in software | Distance-based RGB contribution per fragment in the geometry shader | Appropriate modern tradeoff; GPU cost grows with shaded pixels and selected lights |
| Surface limits | First 20 accepted lights per face; separate 400-entry mobile/stationary pools | Up to 8 outdoor or 12 indoor shader lights per draw; vectors of source lights | Limits count different work; 8/12 does not mean fewer total indoor static lights |
| Selection | Source order, room lists, bounds and plane rejection | Sector/grid candidates, bounds rejection, ranking, indoor selection history, FX merging | Ours handles overload more deliberately, with spatial/temporal approximations |
| Billboards | Scalar world-position shade, approximate distance, 32 shade levels | CPU RGB world-position sample, applied to the textured billboard | Good inexpensive extension; still no detailed lighting across a large sprite |
| Occlusion | No light-to-receiver ray/shadow-map test found in inspected paths | No such test in the classic surface lighting paths inspected here | Neither provides general light occlusion; room filtering still matters |

The original's linear attenuation is verified in the point sampler, with additional type-dependent angular response
in software vertex lighting. It is not one universal equation for its D3D patches and every software path.
Our inspected surface, baked-static and point-sampling helpers use the smooth radial function
`max(0, 1 - distanceSquared / radiusSquared)^2`, with path-specific intensity scales and clamps. At half radius this
function is 0.5625, compared with 0.5 for linear falloff; at three-quarter radius it is about 0.1914 versus 0.25.
Changing falloff alone therefore changes the light footprint even before RGB, ambient and geometry differences.

## Existing optimizations worth retaining

**Indoors:** [IndoorLightingRuntime.cpp](../game/indoor/IndoorLightingRuntime.cpp) builds sector-associated candidate
lists, rejects lights outside draw bounds, ranks them and gives previously selected IDs a 20% score bonus to reduce
switching. The general selector reserves up to one torch, seven FX and four static lights, then fills spare slots
from the combined ranking. These are reservations, not independent hard caps when slots are unused.

The ordinary textured-geometry caller in `IndoorRenderer::render`'s `drawLightSetForBatch` lambda passes
`includeStaticLights = false`: static and decoration lights are already in vertex colors. Consequently the twelve
uniform slots do not limit the number of baked static sources. Omitted candidate contributions can be approximated
as an ambient addition, trading spatial accuracy for continuity. Selection uses the camera as its score reference,
with batch bounds for intersection and the omitted-light approximation; it is not exact per-fragment selection.

[IndoorRenderer.cpp](../game/indoor/IndoorRenderer.cpp) caches static RGB in vertex colors, subdivides affected
triangles to improve sampling, and selectively refreshes affected batches/vertices when source state changes.
This spends geometry/memory and rebuild work to avoid evaluating every static source in every fragment. Static
billboard samples are cached, with position/state checks and a short refresh interval; dynamic samples remain live.

**Outdoors:** [OutdoorLightingRuntime.cpp](../game/outdoor/OutdoorLightingRuntime.cpp) indexes light influence in a
512-unit XY grid, keeps very large lights separately, rejects by draw bounds and chooses the strongest eight by a
radius/intensity/distance score with an importance bonus. [OutdoorRenderer.cpp](../game/outdoor/OutdoorRenderer.cpp)
reuses the runtime when its inputs are unchanged, uses a global selection for simple cases, and selects lights for
terrain chunks when local sets are needed. Chunking trades more selections/draws for more appropriate local lights.

**Shared FX:** [FxLightClustering.h](../game/render/lighting/FxLightClustering.h) merges eligible nearby dynamic
projectile/impact/generic-FX sources by sector, spatial bucket, kind and approximate color, preserving sources marked
important. This is CPU source merging, not GPU clustered/Forward+ lighting. It reduces light count but can change
the shape and position of an effect's illumination. Our kind/importance/stable-ID metadata classifies source and
selection policy; it does not currently reproduce MM8's angular-response type bits.

## Three concrete fidelity gaps

1. **Classic surface lighting ignores normals.**
   [outdoor_textured_fog.sh](../game/shaders/outdoor_textured_fog.sh) computes RGB from distance to local sources and
   applies fog/dimming, but has no sun direction or surface normal. Its vertex shader supplies neither a normal nor
   a baked sun-shade attribute. Day/night presentation exists, including the curve in
   [OutdoorFogProfile.h](../game/outdoor/OutdoorFogProfile.h); what is missing is lighting that distinguishes slopes
   and differently oriented building faces. Similarly,
   [fs_indoor_textured_lit.sc](../game/shaders/fs_indoor_textured_lit.sc) and the static-bake helper are radial only.
   Original MM8 used normal-dependent sun shading and distinguished angular and angle-independent local responses.
   Adding Lambert shading indiscriminately to every source would also change that original behavior.

2. **Indoor geometry baking loses permanent-light room membership.**
   `IndoorLightingRuntime::buildStaticCache` correctly traverses `sector.lightIds`, but the geometry bake uses a
   separate `buildBakedStaticLightSources` that gathers all enabled map lights and decorations.
   [BakedStaticLightSource](../game/indoor/IndoorRenderer.h) carries position, radius and color, without room/source
   membership. `buildFaceTexturedVertices` samples that shared list by position; its computed face normal is used
   for texture-coordinate work, not illumination. Refresh filtering is spatial as well. A nearby light in another
   room can therefore illuminate geometry even when absent from the receiving room's authored light list.
   This is a code-derived consequence, not a captured example. Runtime sector selection for other consumers does
   not protect this bake. Original room lists were not complete occlusion either, but retained this useful boundary.

3. **Indoor ambient belongs to the frame rather than each receiving room.**
   `IndoorLightingRuntime::buildFrame` takes the maximum `minAmbientLightLevel` over visible sectors, converts it to
   brightness and stores one `frame.ambient`. Larger darkness levels mean lower brightness; the conversion has a
   0.18 floor. Draw packing and point sampling reuse that value. Thus revealing a darker neighboring room can lower
   the base ambient of an already visible room, subject to clamping and added illumination. MM8's inspected indoor
   paths retain room darkness. Front/back-sector draw data already exists in our renderer, so a room-based ambient
   policy fits the current structure.

View/portal visibility is not a light-to-surface visibility test. Likewise, contact-shadow blobs in the effects
system do not make these light evaluations occlusion-aware. Restoring room lists would reduce one source of leaks,
but would not automatically stop a light passing through an internal wall in the same room/sector.

## Recommended direction

1. Preserve authored receiving-room light lists in the static bake and use receiving-room ambient. Define the
   front/back-sector rule explicitly and retain source IDs for correct event-driven updates. Keep static caching.
2. Add a normal-dependent outdoor sun term tied to game time, retaining existing day/night and fog behavior.
   Validate terrain slopes and building faces at morning, noon and evening. Reproduce original angular versus
   angle-independent local-light behavior where fidelity requires it; do not assume all sources are diffuse lamps.
3. Keep RGB point sampling for ordinary sprites, the bounded dynamic shader loops and source prioritization.
   If strict classic appearance is desired, scalar sprite shade/falloff/quantization can be a deliberate presentation
   policy rather than forcing the engine back to palette rasterization.
4. Evaluate stronger static occlusion only after those corrections and visual comparison. A static bake with
   occlusion could improve internal-wall leaks but adds bake cost and invalidation requirements for doors/moving
   geometry. General dynamic shadows or GPU light-list infrastructure need a demonstrated scene requirement.

For performance, keep the current 8/12 caps initially. Measure representative release desktop and Android scenes:
quiet rooms, many permanent lights, dense spell effects, and outdoor terrain with overlapping emitters. Track GPU
lighting time, selection CPU time, static-refresh time, vertices/draws and selection discontinuities. More shader
lights increase fragment work; larger batches save submissions but make one selected list cover more space.
The binary investigation alone cannot establish the fastest balance on current hardware.
