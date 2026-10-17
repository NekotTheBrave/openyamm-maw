# MM8 outdoor rendering compared with current OpenYAMM

Reviewed 2026-09-10 against the **working tree**, including staged renderer, sprite-atlas and terrain-decoration
changes, with HEAD `db142fb3`. HEAD alone does not reproduce this snapshot. Original MM8 evidence comes from
[the executable investigation](OUTDOOR_RENDERING.md), using the GOG-verified `MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.

This is a source and binary architecture comparison, not a new benchmark or an engine change. The historical
measurements cited below apply to their recorded scenes and hardware. Recheck the named functions as code evolves.

## Assessment

OpenYAMM already implements the important reusable principles from MM8: avoid repeated resource work, reject cheap
invisible cases early, reuse geometry and scratch storage, and bound lighting work. Its persistent GPU geometry,
material batching, texture arrays, sprite atlases and instanced ground cover address costs that the original's
CPU geometry processing and individual Direct3D submissions do not address in the inspected paths.

The strongest difference is **less CPU submission work versus more geometry submitted to the GPU**. Original MM8
carefully chooses terrain cells and model faces before transforming them on the CPU. Classic ODM rendering in
OpenYAMM generally submits persistent terrain and material-grouped model geometry, letting GPU transformation and
clipping handle the view. This is a reasonable modern tradeoff; the binary research does not establish that adding
MM8's per-cell/per-face work would improve OpenYAMM.

OpenYAMM also renders optional enhanced sprites, filtering, extra ground cover and richer lighting. Costs of those
features cannot be attributed to inefficient reproduction of original MM8 without holding the workload constant.

## Comparison at a glance

| Area | Original MM8, inspected paths | Current OpenYAMM | Implication |
| --- | --- | --- | --- |
| Terrain | CPU view wedge, radial mask, adjacent-row transforms, flat-cell fast path; individual D3D polygons | Persistent mesh and texture array; one terrain submission or multiple chunks for local lighting | Trading CPU selection/submission for GPU vertex work can be advantageous |
| Static ODM models | Whole-model visibility rejection, then face processing and polygon submissions | Persistent groups by resolved material/animation, rebuilt on visual revision | Avoids ordinary per-frame reconstruction and many small submissions |
| Large polygon worlds | Original ODM model path is the relevant binary evidence | Separate `BModelWorld` path has spatial chunks and view/distance rejection | Do not conflate this with classic MM6–MM8 ODM batching |
| Actors and decorations | Frame/octant selection; D3D queue insertion and one draw per sprite | Early range/depth rejection, combined depth sort, compatible-sprite batches | Existing batching targets CPU and driver overhead |
| Sprite pixels | Software sparse rows/palette transparency; D3D cropped quads and uploaded textures | Native textures or cropped atlas frames, alpha discard, compact CPU opacity masks | Preserves the benefit of avoiding empty margins without a CPU rasterizer |
| Transparency | D3D queue drawn back-to-front with depth writes disabled | Configurable depth slices, alpha blending and depth writes enabled | A real visual-behavior difference to evaluate separately from speed |
| Sky | Small projected D3D quads; software fills remaining spans | Small projected sky mesh, reused buffers and texture, optional lower fill | Similar inexpensive geometric representation |
| Textures and animation | Cached resources; software mips; ordinary hardware uploads typically one level | Texture cache, terrain array, profile-specific filtering/mips, changed-frame water-layer updates | Existing resource reuse plus a different quality/bandwidth tradeoff |
| Lighting and reuse | Cached normals, sun-related work and software geometry/spans | Cached light structures, bounded light selection, cached decoration visibility/light batches | Reuse already exists at boundaries suited to the current renderer |

## 1. Terrain: why drawing more triangles can still be appropriate

In [OutdoorRenderer.cpp](../game/outdoor/OutdoorRenderer.cpp), `buildTexturedTerrainVertices` emits two triangles
for each valid grid cell. A complete native 128×128 sample grid yields at most **32,258 terrain triangles and
96,774 unindexed vertices**. `initWorldRenderResources` uploads the terrain once into a persistent buffer; the
whole-terrain buffer uses bgfx's dynamic-buffer API, which does not imply rebuilding it every frame.

`renderWorldPasses` chooses:

- A single textured-terrain submission when the global light selection is sufficient.
- Persistent terrain chunks with separate light selection when local lighting is required. The inspected chunk
  loop submits every valid chunk; it does not apply a CPU camera-frustum test.

The current render texture is a mipmapped **256-layer texture array**, even though the asset preparation structures
still have `Atlas` in their names. Materials therefore do not require one terrain draw per tile. World vertices are
transformed on the GPU through [vs_outdoor_textured_fog.sc](../game/shaders/vs_outdoor_textured_fog.sc).

MM8's visible wedge avoids transforming distant and off-screen cells on the CPU. OpenYAMM avoids that CPU traversal
and pays for more GPU vertices instead. GPU clipping still prevents off-screen primitives from rasterizing; submitting
the mesh does not mean shading every terrain pixel regardless of visibility. Neither inspected terrain construction
path supplies distance-dependent mesh coarsening. MM8's flat quad also becomes two rasterized triangles: its saving
is in polygon preparation/submission, not a magical reduction to one GPU triangle.

**Conditional opportunity:** if terrain GPU work or local-light chunk selection is measurable, test coarse rejection
of the already separate lighting chunks first. Splitting the single-draw path adds submissions and requires its own
evidence. The original wedge algorithm alone is not justification for that change.

## 2. Models: distinguish static batching, mechanisms and large worlds

`rebuildResolvedBModelDrawGroups` in [OutdoorRenderer.cpp](../game/outdoor/OutdoorRenderer.cpp) groups classic ODM
static faces by resolved animation/material. It excludes hidden faces and runtime mechanism models. Groups have
persistent GPU buffers and are rebuilt when the surface visual revision changes, rather than reconstructed for
every normal render. Selecting an already uploaded animation frame does not require rebuilding their geometry.

The normal classic group submission loop does not test each original model against the camera. Grouping faces
across models improves submission efficiency while losing that original per-model culling granularity. Moving or
event-controlled geometry has a separate runtime draw path; its transient vertex handling must not be described as
the cost of drawing every static building.

The `BModelWorld` branch is different: `buildBModelWorldRenderChunks`, `refreshBModelWorldRenderChunks` and the
`chunkVisible` check implement persistent spatial groups with distance and view tests, plus separate opaque and
translucent passes. It addresses polygon-world content such as the MM9 integration. Its existence does not mean
classic ODM has the same culling, or that classic ODM should automatically adopt its draw granularity.

**Conditional opportunity:** measure submitted vertices and GPU cost on representative classic maps before breaking
material groups into spatial groups. The saved off-screen work must outweigh extra selection, uniforms and draws.

## 3. Billboards: current batching and the meaningful compatibility difference

The active combined actor/decoration path is **`renderActorPreviewBillboards`**, called by `renderWorldPasses`, despite
its historical name. Use that function in [OutdoorBillboardRenderer.cpp](../game/outdoor/OutdoorBillboardRenderer.cpp)
when reviewing ordinary world sprites. The separate `renderDecorationBillboards` helper's texture sort is not the
combined path's sorting policy.

The current combined path:

1. Rejects hidden objects, objects beyond cached squared draw distances, and objects behind/too near the camera
   before resolving their current frame texture and lighting.
2. Selects animation frames, view octants and mirroring, preserving placement metadata for enhanced cropped frames.
3. Sorts a reusable draw-item vector back-to-front with `std::sort`.
4. Builds six world-space vertices per billboard into compatible batches, reusing batch-vector capacity and using
   bgfx transient vertex buffers for submission. Ordinary actors/decorations are batched quads, not GPU instances.

The native D3D implementation instead inserts records into its sorted queue by searching and shifting entries,
then binds and submits individual billboards. It has fixed producer/draw capacities; those limits are not useful
optimization targets to reproduce in OpenYAMM.

`outdoorBillboardDepthSlice` has a **compiled default of 256 world units** in
[GameSettings.h](../game/app/GameSettings.h); a user's saved setting can override it. The combined path sorts first,
then groups compatible textures within successive depth slices. A value of **0** preserves exact sorted submission
order and batches only consecutive matching descriptors. Hovered outline draws form batch boundaries.

This is not exact original compositing even with slice size zero. `BillboardAlphaRenderState` enables both depth
testing and **depth writes**, whereas original MM8 disables depth writes for its D3D billboard queue.
[billboard_lit.sh](../game/shaders/billboard_lit.sh) discards alpha at or below 0.001, then blends surviving fragments.
With filtered or enhanced alpha edges, both depth writes and within-slice reordering can affect overlapping sprites.
This review identifies the difference; it does not establish a visible defect or justify changing the default.

The ordinary actor/decoration gathering loops do not have a full horizontal/vertical sprite-bounds rejection before
texture/light resolution. The nearby `billboardIntersectsView` calculation selects combat health-bar candidates;
it does not reject the sprite itself. **If gathering is expensive**, conservative sprite-frustum rejection is a
focused experiment. It must account for cropped-frame offsets, logical dimensions, mirroring and camera tilt.

## 4. Atlases, texture memory and animation

[SpriteAtlasCache.cpp](../game/render/SpriteAtlasCache.cpp) caches base/mask GPU pages and resolves frame rectangles,
placement and recoloring metadata. Its preload path walks animation/direction resources. The native sprite preload
path also decodes requests with a bounded worker pool, capped at eight workers. Compact
[opacity masks](../game/render/BillboardOpacityMask.h) support CPU picking without retaining an RGBA image per
frame/variant solely for opacity queries.

The enhanced atlas path has explicit mip generation and frame isolation. See
[SpriteAtlasMipmaps.h](../game/render/SpriteAtlasMipmaps.h) and
[SpriteAtlasMipmaps.cpp](../game/render/SpriteAtlasMipmaps.cpp): aligned cells, gutters and edge colors protect
sampling through mip level 4, and the shader clamps its LOD accordingly. Native billboard texture creation follows
a different filtering profile: `profileUsesMipmaps` in
[TextureFiltering.cpp](../game/render/TextureFiltering.cpp) enables ordinary mips for terrain, models and sky,
not native billboard textures. Do not infer that every sprite uses the enhanced-atlas mip path.

**Atlas sharing does not yet mean every frame on a page can be one draw.** The combined billboard batch key is a
texture descriptor pointer. `SpriteAtlasCache::bind` sets per-draw rectangle, texel/LOD and recoloring uniforms.
Different frames or variants sharing a GPU page may need different values. Batching solely by GPU texture handle
would be incorrect with this shader contract. Broader batching would require moving that varying metadata into
vertex/instance data or another indexed representation, and is justified only if current submission cost warrants it.

`updateAnimatedWaterTileTexture` skips repeated animation ticks and unchanged frames, then updates only the changed
terrain-array layer from cached frame pixels. It does not rebuild the terrain mesh or upload the whole terrain
texture collection each frame. Historical commits discussing atlas-region updates describe an earlier resource
layout; use the current array code for current cost analysis.

## 5. Sky, lighting and optional ground cover

`renderOutdoorSky` in [OutdoorRenderer.cpp](../game/outdoor/OutdoorRenderer.cpp) uses a cached sky texture, a small
reused dynamic vertex buffer, CPU camera/time-dependent UV preparation, and a projected sky draw. A second small
draw can supply gradient/lower fill. This is close in representation to MM8's projected D3D sky, not a large skydome
or volumetric simulation. Replacing it has little geometric work to remove; any measured sky cost would need to
distinguish pixel shading and coverage from these few vertices.

[OutdoorLightingRuntime.cpp](../game/outdoor/OutdoorLightingRuntime.cpp) builds spatial light data and selects bounded
sets. `OutdoorSelectedFxLights::MaxLights` is eight, rather than an unbounded per-draw list. `renderWorldPasses`
rebuilds its light runtimes when the inputs change and uses a global selection when appropriate; local bounds
selection is used when needed. Billboards receive a sampled light contribution in their vertex color; their fragment
shader combines that contribution with ambient light and fog without iterating the scene's lights per pixel.

Optional grass/stones are extra content, with `terrainDecorations` false in the compiled settings default.
[TerrainDecorationRenderer.cpp](../game/outdoor/TerrainDecorationRenderer.cpp) uses persistent instance data,
patch range/frustum rejection and GPU instancing. The main renderer joins contiguous visible ranges only when mesh
and selected light uniforms match. While stationary, visibility/light batches refresh at 20 Hz; camera/projection
changes and light-count changes refresh immediately. Wind, fog and submissions continue every rendered frame.
Existing moving/flickering light values can be up to 50 ms old while the camera is stationary.

This shares the original's principle of reusing unchanged work, at a different boundary from the software renderer's
geometry/span cache. There is no reason to port software span rasterization or its segmented perspective mapper
into this GPU pipeline merely because those techniques were effective in original MM8.

## 6. Existing measurements and how they constrain conclusions

The retained [terrain decoration performance report](../level_generation/terrain/mm6_ground_cover/review/performance/REPORT.md)
provides a concrete example of the optimization work already done. At its fixed New Sorpigal camera on an RTX 3060 Ti,
OpenGL, 1600×900, release build and VSync off, joining compatible ranges reduced **148 draws to 14 for the same
8,714 instances**. In the controlled batching comparison, decoration CPU work fell from 0.091 to 0.046 ms and
the whole-frame renderer/driver interval from 0.483 to 0.373 ms.

Those numbers are historical measurements, not a new run of this working tree. The report also documents varying
results across launches. Its final within-process cached on/off comparison found approximately **0.048 ms/frame**
added for ground cover, or 9.9% lower FPS at that extremely high baseline frame rate. CPU and GPU intervals overlap;
they must not be added. The earlier approximately 1.5% result must not be quoted as a universal final feature cost.

This evidence supports preserving batching as an established constraint. It does not prove that every renderer
path is optimal or that one original-engine technique can never help. It also does not support ranking MM8 and
OpenYAMM's overall performance without comparable scenes, asset quality, settings and hardware.

## 7. Priorities if another optimization pass is requested

| Measured symptom | Focused experiment | Preserve or verify |
| --- | --- | --- |
| Terrain vertex work or local-light selection is costly | Reject off-screen/distant existing terrain lighting chunks | Visible bounds, camera movement, lighting; retain the one-draw path unless splitting it wins |
| Classic model GPU geometry dominates | Compare coarse spatial/material groups with current material-only groups | Submission cost, moving mechanisms and event invalidation |
| Sprite gathering dominates | Conservative full-view rejection before costly frame/light work where metadata permits | Enhanced crop/pivot placement, wide/tall sprites, near-plane behavior |
| Sprite submission dominates with many atlas frames | Prototype frame/variant metadata suitable for broader batching | Atlas mips, recoloring, transparency order and selection outlines |
| Overlapping sprites differ visibly from MM8 | Compare slice 0 with the configured value; investigate depth writes separately | Original depth/blend behavior and enhanced alpha edges; slice 0 alone is not full MM8 emulation |

Use existing `performanceTrace` sprite gather/sort/submit counters, texture-load diagnostics, world-pass timings and
real GPU measurements to select an experiment. Compare the same moving and stationary cameras with the same assets
and settings, including the actual target device when evaluating Android. None of these experiments is an identified
speedup from this review, and none was implemented here.
