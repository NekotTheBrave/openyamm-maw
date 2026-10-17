# Vita performance review

Reviewed 2026-09-14 against the supplied tester report, the current OpenYAMM working tree, and the existing
original-MM8 investigations. This is a prioritization checklist, not a benchmark of the Vita fork. Its patches,
exact dependency revisions, GPU captures and hardware were not supplied. The 00.35 inclusive timings overlap;
00.36 already contains changes whose gains remain unmeasured. The reported gameplay FPS and profiling callback
times are different observations and should not be combined into one timing model.

Indexed textures are promising for memory and streaming, particularly on Vita. The largest measured sustained
CPU opportunity is actor simulation, followed by rendering submission. Pixel-format changes alone cannot remove
those costs. Address CPU work and the resident texture working set together.

| Priority | Work | Reason and acceptance check |
| --- | --- | --- |
| First | Repeat the profile on 00.36 with identical save, pose, actor population and settings | Separate cold loading, warm stationary rendering, combat and travel. Record median/p95/p99 frame time, CPU work versus waits, and simulated seconds per wall second. |
| First | Review the existing port allocation-reuse patches and current upstream movement fixes | Collision and target preparation consume repeated work. Measure allocations and CPU time per actor step, then the complete frame; preserve collision order and gameplay. |
| First | Cull terrain chunks before lighting and bgfx submission | The current local-light chunk loop still lacks frustum rejection. Keep conservative bounds and the same draw distance; check hills, water and screen edges. |
| First | Remove synchronous sprite loading from picking and ordinary steady-state gameplay | Retain dimensions and opacity metadata independently of GPU residency; preload predictable UI. Verify first-use interaction still works when a texture is evicted. |
| First | Bound all streaming stages and actual GPU residency | Count queued source bytes, prepared pixels, upload staging and pending GPU retirement separately. The complete animation set exceeds the stated budget. |
| Next | Reduce draw/state work and repeated light selection | Their bgfx calls cost 49.2 ms inclusively. Batch compatible material/lighting ranges and reject invisible billboards before preparation. Verify alpha ordering and uniform state after submissions. |
| Next | Prepare compact GPU assets ahead of gameplay | Compare 16-bit color and offline BC1/BC3 where natively supported; prototype native P8 on Vita. Measure actual allocation, conversion work, upload stalls and visual quality. |
| Next | Separate expensive AI decisions from movement integration | Trial 32/64 Hz decisions with existing collision slices. Preserve elapsed combat/recovery time and immediate important reactions. Visibility alone must not stop simulation. |
| Later | Tune resolution, effects, draw distance and optional texture resolution | Use CPU/GPU measurements to select the setting. Keep a known baseline; do not label an unverified distance as vanilla MM8. |

The current source substantiates several report findings. `OutdoorBillboardRenderer.cpp` interaction candidate
construction calls `ensureSpriteBillboardTexture`; on a miss that function loads BGRA pixels, makes a mask and
creates a texture. The native warmup implementation parallelizes decoding but then joins futures and uploads the
collected results: worker threads alone do not establish asynchronous, bounded frame-time behavior.

`OutdoorWorldRuntime.cpp::buildNearbyActorMovementColliders` constructs a vector, scans actors, resolves stats
and copies display names. Movement paths call it repeatedly. `buildCombatTargetCandidates` also builds a new
vector and scans the map actors. Reusing capacity removes allocator traffic but does not remove repeated scans
or invariant policy construction. Prepare shared immutable facts once per simulation step, reuse scratch storage,
and consider a spatial neighborhood if the remaining scans are material. Collision positions and any mutable
target state must still reflect actors already processed in that step; a stale whole-step snapshot changes behavior.

Our recent [movement fixes](OUTDOOR_ACTOR_MOVEMENT_FIXES.md) already remove temporary floor-candidate allocation,
avoid repeated bmodel-ground classification within movement, and avoid a redundant final support query when the
validated pose is unchanged. Desktop controller microbenchmarks improved by 15–74% across the tested fixtures.
These are not Vita measurements and do not predict the improvement in the reported 48.3 ms collision total.
They also preserve falling velocity and validate actor step clearance; do not discard correctness checks to save CPU.

The ordinary ODM building path in `OutdoorRenderer.cpp` still uses the wireframe vertex buffer/line count in its
outer resource gate. Textured buildings should depend on their own resources. This is a useful shared correctness
patch. The newer BModelWorld branch already has a different gate.

The terrain implementation has changed since the port snapshot. Current upstream creates a 256-layer terrain
texture array; `buildBgraMipLevels` floor-halves dimensions already. Review the old atlas-region correction against
the path actually retained for GLES2 rather than transplanting its line numbers. The 1088-to-2048 backing expansion
is evidence from the reported vitaGL path, not a general bgfx allocation rule. For that path the reported reduction
from approximately 21.3 to 5.3 MiB is valuable. Any repacking must preserve tile gutters and mip isolation.

The timing numbers also merit investigation before celebrating higher FPS. At 128 Hz, 6.4 steps represent 50 ms
of simulation. If those are the complete steps for a 280.8 ms wall interval, actor simulation advances at about
18% of real time. That is conditional arithmetic, not proof of the cause: establish whether the counters share
an interval and log raw elapsed time, accepted time, executed steps and discarded backlog. Current outdoor and
indoor actor accumulators cap their backlog at 0.1 seconds. Removing a cap without reducing work can create an
ever-growing backlog. Likewise, fewer AI decisions will not proportionally reduce collision cost if collision
remains at 128 Hz. Validate movement distance, gravity, attack cadence, recovery, projectiles and events against
wall time, including sustained 7–13 FPS operation.

For texture storage, distinguish the source file, CPU representation and GPU representation. Keeping indexed
BMPs in the package saves neither GPU memory nor upload bandwidth if loading still expands them to 32-bit color.
Parsing rows, cropping, transparency and upload layout remains necessary even when the indices are retained.
Changing BGRA channel order to RGBA does not reduce storage.

| GPU pixel representation | Nominal bytes per pixel | Main tradeoff |
| --- | ---: | --- |
| BGRA8/RGBA8 | 4 | Full color and alpha; current native upload path |
| RGB565 / RGB5A1 / RGBA4 | 2 | Reduced color precision; choose alpha precision by content |
| Indexed P8 with palette | 1, plus palette | Preserves native palette colors; requires supported palette sampling |
| BC1 / DXT1 | 0.5 | Lossy block color and optional binary transparency |
| BC3 / DXT5 | 1 | Lossy block color with interpolated alpha |

These ratios exclude row alignment, texture backing, block padding, mip levels, descriptors and transient copies.
BC formats and 16-bit formats are documented by [bgfx's format reference](https://bkaradzic.github.io/bgfx/bgfx.html#textureformat).
If the reported 153.75 MiB were entirely comparable 32-bit pixel payload, quarter-sized indexed payload would still
be about 38.4 MiB before overhead, exceeding 28 MiB. Sharing indices across palette variants could save more, but
requires counting unique images. Whole-animation residency remains the wrong default for this budget.

There is a concrete Vita-specific opportunity: current [vitaGL texture source](https://github.com/Rinnegatamante/vitaGL/blob/master/source/textures.c)
maps `GL_COLOR_INDEX8_EXT` and palette8 OES uploads to native `SCE_GXM_TEXTURE_FORMAT_P8_ABGR`; DXT uploads map
to UBC formats. This establishes implementation paths, not successful integration with their bgfx fork. Test
per-texture palettes, filtering, mip upload, palette lifetime and actual residency. Avoid per-draw `glColorTable`
replacement: the inspected function frees and reallocates its table.

The [bgfx public texture API](https://github.com/bkaradzic/bgfx/blob/master/include/bgfx/bgfx.h) has R8 and compressed
formats but no ordinary P8-with-palette format. Native palette support therefore needs an explicit backend bridge.
A portable alternative samples an index texture and then a palette texture in a shader. Nearest sampling is simple;
linearly interpolating numeric indices gives incorrect colors. Correct bilinear filtering needs neighboring index
lookups followed by color interpolation, which adds shader work. Mips must also be generated in color space and
represented appropriately, not by averaging palette indices. Prototype native sprites first; enhanced art with
smooth alpha and more than 256 colors has different requirements. A single transparent palette entry also cannot
represent all the different RGB colors produced by our transparent-edge bleed preparation; check filtered edges
for halos and decide whether extra transparent entries or another format are needed. Retain a separate tiny mask
for picking.

For a portable low-memory profile, compare compact direct-color formats and precompressed assets before committing
the shared renderer to shader palette lookup. Select by backend capabilities, alpha needs and visual tests. Verify
the driver keeps the requested compact format instead of expanding or recompressing it. Do not runtime-compress
textures in the gameplay frame. [bgfx texturec](https://bkaradzic.github.io/bgfx/tools.html#texture-compiler-texturec)
already provides offline conversion machinery; the asset pipeline must still preserve palette overrides, cropping,
alpha coverage and filtering conventions. Android needs capability-appropriate formats, not a universal DXT setting.

A derived asset cache should key content, palette, scale tier, filtering/mip preparation version and target format.
It should persist expensive edge preparation and mips, not just decoded source pixels. Keep the original assets as
authoritative inputs. Bound worker output by bytes, prioritize visible/current frames and likely next frames, and
avoid starving the game/render threads with too many workers. Cache hits still incur storage reads and uploads;
load predictable UI before interaction and measure cold misses separately.

Avoid changing textures the GPU is reading where practical. Current water animation updates an array layer only
when its frame changes, but rebuilds its mip chain for that update. Precomputed mips remove CPU work. A small
resident animation page with frame/UV selection can remove writes altogether; compare its additional residency
against the reported replacement-copy cost. A forced GPU wait can lower peak memory while worsening frame time.
Log both and budget resources awaiting retirement, not only the cache's logical live textures.

The [original outdoor rendering investigation](OUTDOOR_RENDERING.md) establishes useful precedents. Software MM8
used indexed pixels, skipped transparent sprite row margins and selected palette shade banks. Its hardware path
read prepared 16-bit HWL images and reused uploaded textures. Sprite/ordinary HWL calls select A1RGB555 surfaces;
the surface creator also contains an RGB565 branch. The important shared lesson is small prepared resources and
reuse, not that the original hardware renderer sampled 8-bit BMPs directly.

The original executable hash was rechecked for this review:
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
A fresh Rizin inspection of `MM8-Rel.exe` at `0x49d911` confirms the 16-bit surface setup and A1RGB555 masks at
`0x49d9db–0x49d9fa`. No original executable or reference implementation was modified.

Original terrain rendering rejected work by view/distance and buildings had coarse visibility tests. Use that
principle with our persistent meshes and coarse bounds; reproducing its CPU polygon clipping is not automatically
beneficial. Original D3D sprites were also submitted individually, so its draw architecture is not an optimal
template for bgfx through a translated driver. Our modern atlas batches must account for frame rectangles,
palette/recolor state and transparency order; sharing a texture handle alone is insufficient to merge draws.

The [original creature investigation](CREATURE_MOVEMENT.md) found simple sphere collision, heightfield support,
bounded geometry neighborhoods, early exits and a selected nearby list of 30 actors. The physical loops still scan
the full actor array; 30 is not a moving-monster cap. Movement consumes a fixed-point timer delta. A 128-unit time
base does not establish 128 complete AI updates per second. Apply locality and avoid repeated decisions, without
copying original fixed-array truncation or assuming identical scheduling.

I would welcome focused upstream patches for resource gating, applicable mip arithmetic, no-load picking,
allocation reuse, conservative culling, measured state/batch reductions, configurable streaming budgets and a
derived asset pipeline. Maintained ARMv7 build coverage is useful infrastructure, but does not itself fix frame
time. Native palette plumbing, vitaGL fixes and loader/input/affinity choices belong primarily in the Vita port.
AI cadence/LOD changes need separate behavioral validation before becoming a default.

For acceptance, capture identical crowd/sea poses, indoor combat and outdoor ramps/platforms; rapid camera turns,
spell/FX bursts, opening UI, map transitions and repeated animation cycles should exercise the streaming budget.
Measure steady state and cold startup separately, actual GPU allocation peaks, cache misses/evictions, uploads,
bytes copied, retirement backlog and waits. Run a longer session for growth and thrashing. Diagnostic toggles for
frozen simulation, rendering without updates, lower resolution and disabled local lights can identify bottlenecks;
they are experiments, not evidence that gameplay may safely omit those systems.
