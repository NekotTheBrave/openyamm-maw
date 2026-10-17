# Original MM8 precipitation and the OpenYAMM approach

Investigated 2026-09-21 using Rizin 0.10.0 and rz-ghidra. Target: `mm8_orig/MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`, preferred image base `0x400000`.
This is the [GOG-verified executable](GOG_INSTALLER_VERIFICATION.md). All addresses are virtual addresses in
that exact file. The executable was opened read-only. No game execution or performance measurement was made.

OpenYAMM source was reviewed in the current working tree, HEAD `fcd47b9a4`; HEAD alone does not describe all local
changes. Read [the renderer comparison](OPENYAMM_RENDERING_COMPARISON.md) for the broader rendering architecture.
Reusable binary labels are in [the weather annotation script](mm8_rel_5326d564_weather.rz).

## Recommendation

**Keep OpenYAMM's world-space particles and batched GPU rendering. Borrow MM8's bounded local workload and
recycling, but do not port its rain implementation literally or replace weather with a screen overlay.**

The original contains a substantial **rain implementation**, but its activation is unresolved: initialization
clears its enable byte and the examined references reveal no writer that enables it. The normal outdoor frame
calls its dispatcher, which returns while disabled. Treat it as retained, apparently dormant functionality,
not evidence that ordinary stock gameplay necessarily shows rain.

For **snow**, there is a stronger narrow result: the legacy `SetSnow` EVT command calls an empty function in this
executable. No functioning snow renderer was identified. MM7/OpenEnroth's screen-space snow must not be presented
as verified MM8-Rel behavior.

## Snow and activation evidence

The EVT dispatcher starts at `0x4446bd`. At `0x4447cb` it reads the command byte, subtracts one and indexes the
table at `0x446744`. Opcode `0x0a`, named `SetSnow` by the local MMExtension definition
(`reference/MMExtension/Scripts/Core/evt.lua:736`), selects table entry nine, `0x445e97`.
That handler loads the two parameter bytes into EDX and ECX and calls `0x450f22` at `0x445e9f`.
**`0x450f22` is a single `ret`.** This is a verified no-op, not merely a missing xref.

The native rain object is at `0x6f2568`. Its constructor body at `0x461d22` initializes the two embedded list
sentinels, clears the count and clears four bytes at offsets `0x93c` through `0x93f`.
The active-rain test is specifically a **byte** read at `+0x93c`, absolute `0x6f2ea4`.
The outdoor frame supplies that object to `0x4c2d34` at `0x47b48d–0x47b492`.

Exact-address searches for the enable byte found no encoded absolute references. Searching its field displacement
found initialization, the dispatch read, and a write of zero when the list becomes empty. Searches for the other
three field displacements also found no enable/start setter. Searching the object address found its static
constructor entry and outdoor-frame call. This supports an apparently dormant subsystem, but is not proof against
every possible computed pointer, indirect write or external runtime patch.

The local MMExtension `Weather` definition distinguishes MM6/MM7 `Snow` from MM8 `Rain`
(`reference/MMExtension/Scripts/Structs/01 common structs.lua:440`). Its absolute addresses refer to another
executable revision; the addresses above were independently established in MM8-Rel.
The adjacent `MapExtra` definition also names rain/snow bits, but names in a data layout do not prove that a
renderer consumes them. OpenYAMM's own atmosphere bits must not be substituted for native header bit meanings.

As a separate reference only, local OpenEnroth `src/Engine/Graphics/Weather.cpp` implements MM7-style screen
coordinates, white rectangles of sizes 1/2/4, random drift, edge wrapping and horizontal shifts on player turns.
That code was read for comparison, not copied, and does not establish an MM8 snow implementation.

## Recovered rain pipeline

| Address | Verified role |
| --- | --- |
| `0x4c2d34` | Enabled check, initialize/refill, update, project, draw; disabled cleanup |
| `0x4c2d8b` | Initialize doubly linked list using embedded sentinels |
| `0x4c2dcd` | Add a random 1–20 drops, subject to the insertion cap |
| `0x4c2f07` | Allocate/link one `0xb0`-byte drop; refuse at count 499 |
| `0x4c3460` | Seed/reseed relative position, velocity and cached floor |
| `0x4c2e1c` | Advance fall, clamp to cached floor, recycle/delete |
| `0x4c2fe5` | Reconstruct positions from party XYZ, transform and project endpoints |
| `0x4c307d` | Direct3D or software streak rendering |
| `0x4c2fd4` | Reinitialize an existing drop without reallocating |
| `0x4c2f94` | Unlink and free one drop |
| `0x4c351a` | Drain list, check sentinels and reset state |

### Population, memory and timing

The list has head/tail pointers at object `+0x930/+0x934`, count at `+0x938`, and per-node next/previous pointers
at `+0xa0/+0xa4`. Each dynamically allocated drop occupies **176 bytes**. At the verified **499** limit that is
87,824 bytes of drop payload, excluding allocator overhead and the object itself.

The start/refill helper requests `rand()%20 + 1` drops per invocation. The insertion routine enforces the final
499 limit even if the requested batch exceeds the remaining space. Filling therefore depends on frame calls,
not a particles-per-second accumulator. Already allocated drops are normally reseeded in place on contact.

Fall motion itself **is time-scaled**. Seeded velocity is `(0, 0, -6.5)` and update performs
`relativeZ += timerDelta * velocityZ` (`0x4c2ec0–0x4c2ecf`). The timer is `0x51d258`, delta at `+0x1c`.
`0x424f68` converts `timeGetTime()` milliseconds by `*128/1000`; `0x425008` stores the elapsed ticks and clamps
them to 32. Thus the nominal fall rate is **832 world units per second**, with the timer clamp limiting large steps.
Do not describe this as 6.5 units/second, or describe the entire effect as frame-rate independent.

### Local volume, movement and surface handling

Every seed/reseed uses radius argument **1000**:

- Relative X and Y: integer random values in `[-1000, 999]`.
- Relative Z: integer random values in `[0, 1999]`.
- Absolute position during projection: **current party XYZ plus the saved relative offsets**.

The effect is a small three-dimensional field that translates with the party. It is not a world-wide rain simulation,
and it does not preserve stationary world positions as the player walks through it. Rotation is handled by the
camera transform. The second streak endpoint is made by adding **30–59 units to camera-space vertical**, after
the first point's world-to-view transform, then projecting both points. This is a visual streak, not integration
of a second physical particle or a world-vertical segment under arbitrary pitch.

At seed/reseed, `0x4c34f2` calls the combined outdoor floor query **`0x46d6bb`** at the seeded world position.
It stores the returned integer floor Z at node `+0xa8`, plus an output-derived flag at `+0xac`.
The floor query handles terrain and eligible building floors; its selection rules are documented in
[Creature movement](CREATURE_MOVEMENT.md#terrain-and-bmodel-floor-selection).

Update compares the previous reconstructed world Z with the cached floor. Below the floor, it adjusts relative Z
so the next reconstructed position reaches it; at equality, it reseeds the drop, or deletes it if `+0x93f == 1`.
The D3D renderer has a contact branch that emits a **two-screen-pixel vertical mark** at equality
(`0x4c32ca–0x4c3314`). This is not evidence of expanding water rings or a splash-particle simulation.

Limitations visible in this implementation:

- The floor is queried on reseed, not continuously, although the relative field follows party translation.
  Consequently cached floor heights can become inappropriate over changing terrain or buildings.
- The reseed-distance check at `0x4c2e42–0x4c2e68` tests **absolute X separation only**, against 1000.
  It is not a complete radius/XY/XYZ containment test.
- A floor query below a seeded point is not a general overhead shelter test. Spawning below a roof can select
  the floor beneath it. No complete rain-versus-roof/wall swept-collision system was found in this pipeline.
- Turning rain off through the enable byte drains the list immediately; the separate drain-at-contact flag
  exists, but no normal activation/control path for these modes was established.

### Rendering

`0x4c307d` traverses the list without depth sorting. It rejects first-endpoint view depth below **8**, excessive
X separation, and drops whose projected endpoints are outside the viewport. The viewport checks reject the
whole streak rather than clipping a partially visible streak at the weather level.

**Direct3D:** the function binds no texture and submits a **two-vertex line list per accepted drop** through
the device vtable `+0x70`. Each vertex carries projected XY, Z and reciprocal depth. The falling streak uses
white `0x80ffffff` at one end and `0x00ffffff` at the other. Render states set alpha blending on, source blend 5
and destination blend 2: **source-alpha-weighted additive blending**. These enum meanings are corroborated by
[Microsoft's render-state definitions](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3drenderstatetype)
and [blend definitions](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblend).
The function does not explicitly set `ZENABLE` or `ZWRITEENABLE`; those states are inherited. Supplying depth
must not be reported as an independently verified local depth-state setup. Other changed states are restored
at the end, rather than queried/saved generically.

**Software:** it checks the packed scene-depth value at the first endpoint, then passes a constant packed drop
depth to `0x4a0414`. That line rasterizer also compares scene depth at individual pixels
(`0x4a06c6`, `0x4a07a7`) before writing. This is more than a final full-screen overlay. Its pixel/depth-buffer
work is specific to the old software renderer and has no useful direct port into bgfx.

## Current OpenYAMM comparison

The implementation already takes a different and appropriate route:

- [OutdoorSpatialFxRuntime.cpp:527](../game/outdoor/OutdoorSpatialFxRuntime.cpp#L527) spawns ordinary world-space
  particles. Snow prewarms 1,800 particles and emits 320/second; motion-related refill adds up to 420 per frame.
  The snow volume removes distant particles and refills in the direction of travel. Existing particles keep
  their world positions rather than translating with the party.
- Rain emits `110 * intensity` particles/second, intensity clamped to 0–3, into a region ahead of the camera.
  It falls at 2,200–2,900 units/second, lives 0.42–0.50 seconds and uses velocity-aligned stretched quads.
  This recipe is visually different from MM8's slower, additive line streaks. Its horizontal slant is derived
  from camera yaw, rather than a persistent world-space wind direction.
- [ParticleSystem.cpp](../game/fx/ParticleSystem.cpp) advances motion in seconds in a reserved contiguous vector.
  The weather spawn and particle update paths contain no ground/roof collision queries.
- [ParticleRenderer.cpp](../game/fx/ParticleRenderer.cpp) performs visibility rejection, builds six vertices per
  visible particle and batches by material/blend mode. Rain and snow normally use one batch each, possibly shared
  with compatible other FX. It uses alpha blending, `DEPTH_TEST_LEQUAL`, and **no depth writes** for these particles.
  This preserves occlusion by depth-writing scene geometry, but does not prevent particles spawning under roofs.
  It does not sort the alpha particles. The current particle shader has no atmospheric fog or scene-depth soft fade.

The most concrete budget concern is the shared **4,096-particle** cap in
[ParticleSystem.h](../game/fx/ParticleSystem.h#L43): `addParticle` rejects new particles once it is full, without
tag priorities or reservations. Weather can therefore contend with spell/impact/trail effects. Snow's nominal
emission rate times its 9.9–11.3-second lifetime is about **3,168–3,616 particles**, before movement additions,
prewarm transients and volume culling. This is a capacity estimate, not a measured live count or proof of an
observed missing combat effect.

## Focused next steps for OpenYAMM

1. **Keep the existing shared particle simulation and batching.** A few thousand simple CPU-updated particles
   do not, by themselves, justify a compute simulation, volumetric weather or a second renderer.
2. **Give weather an explicit budget within the particle system.** Reserve capacity for gameplay FX or enforce
   weather admission limits. Prefer bounded recycling/refill to movement bursts that can consume the shared pool.
   Preserve world-space positions for particles still inside the local volume; handle teleports and large camera
   moves explicitly rather than translating all existing drops with the player.
3. **Add surface/exposure information through world hooks.** A small cached set of overhead/floor samples for
   the local weather volume can suppress particles under cover and expire them at terrain/roofs. Query/update
   at emission or when cells change, rather than sweeping every flake against every face each frame. A simple
   height cache suits ordinary outdoor roofs; overhangs and multiple vertical layers need explicit treatment.
   Keep simulation/rendering shared and terrain/building queries world-specific.
4. **Tune rain continuity and appearance.** A volume surrounding the camera, with stable world-space wind,
   avoids relying only on births in the current forward direction after quick turns. Keep thin batched quads
   for controllable width across resolutions. Use near-surface splashes sparingly if desired. Atmospheric fog
   and a scene-depth soft intersection fade can be later visual improvements if required by captured results.
5. **Measure before escalating complexity.** Compare rain/snow off versus on, stationary versus fast travel,
   and weather plus combat. Record live/rejected counts by tag, CPU update/gather time, draws and GPU time on
   desktop and Android release builds. Check roof entry, terrain transitions, looking up/down, rapid turns,
   teleport, weather transitions and resolution scaling. A full-screen overlay is an optional deliberate
   retro presentation choice; it loses spatial depth/exposure behavior if used as the primary effect.

These are proposed improvements, not changes made by this investigation. No builds or runtime tests were needed
for this documentation-only result; performance and visual quality remain to be measured.

## Reproduction

Follow [RIZIN_GUIDE.md](RIZIN_GUIDE.md). A fresh project was generated in `work/weather/mm8-rel-weather.rzdb`;
raw disassembly exports are disposable files in `work/weather/`.

```bash
sha256sum mm8_orig/MM8-Rel.exe
../third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -c 'aaa;Ps work/weather/mm8-rel-weather.rzdb' mm8_orig/MM8-Rel.exe
../third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -p work/weather/mm8-rel-weather.rzdb -i mm8_rel_5326d564_weather.rz \
  -c 'pdf @ 0x4c2d34;pdf @ 0x4c3460;pdf @ 0x4c2e1c;pdf @ 0x4c2fe5;pdf @ 0x4c307d'
```

In this session the installed `~/.local/bin/rizin` was unavailable; the existing build-tree executable worked.
The existing build-tree `core_ghidra.so` was loaded with Rizin's `L` command. Compiled x86 Sleigh specifications
and matching metadata were staged under `work/weather/sleigh` for decompilation. Important conclusions above
were checked in instructions, not inferred solely from decompiler output or reference names.
