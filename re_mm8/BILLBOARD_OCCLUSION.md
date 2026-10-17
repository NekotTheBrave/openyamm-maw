# MM8 decoration placement and wall occlusion

Investigated 2026-10-02 with Rizin 0.10.0 and rz-ghidra. Target: `mm8_orig/MM8-Rel.exe`,
1,163,264 bytes, preferred image base `0x400000`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
This is the [GOG-verified executable](GOG_INSTALLER_VERIFICATION.md). All addresses below belong to that
executable. Important decompiler conclusions were checked against its instructions. Neither the executable
nor gameplay code was changed; the original game was not launched for a visual comparison.

**Ordinary MM8 decorations use a projected base anchor and a screen-facing rectangle with one depth for the
whole sprite. The indoor path has no separate wall-decoration depth bias or wall-aligned quad construction.**
Walls can therefore hide part of a sprite. This does not establish that the original game reproduces the exact
Goblinwatch screenshot, which uses an MM6 map and OpenYAMM's current assets and camera.

See [outdoor rendering](OUTDOOR_RENDERING.md#8-billboards-and-sprites-detailed-trace) for the shared queue,
texture metadata and software rasterizer. The [annotations](mm8_rel_5326d564_billboard_occlusion.rz) retain
the additional indoor addresses and verified depth states.

## Ordinary indoor decorations

Decoration producer `0x43d6fa` reads 32-byte decoration records at `0x6b1368`. After handling hidden,
non-drawable and particle-emitter cases, the ordinary branch reads XYZ directly from decoration
`+0x04/+0x08/+0x0c` at `0x43d880–0x43d886`. It selects an animation frame and one of eight art directions.
The facing field selects artwork; it does not orient a physical quad against a wall.

The unchanged XYZ anchor is passed to camera transform `0x434a5b` at `0x43d958` and screen projection
`0x434e6d` at `0x43d99d`. The transform applies camera position, yaw and pitch, returning fixed-point
depth, lateral and vertical coordinates. It transforms this one anchor rather than the top and bottom
of an upright world-space sprite. The ordinary producer makes no wall lookup or wall-normal offset.

The resulting 52-byte render record stores the projected anchor at `+0x26/+0x28` and packs depth/identity
at `+0x10` (`0x43db0a–0x43db1f`):

```text
packedDepthIdentity = (anchorDepth16_16 & 0xffff0000) + ((decorationIndex << 3) | 5)
```

There is no height-dependent depth correction in this assignment. The stationary torch light uses
decoration Z plus half the descriptor height, as documented in [static indoor lights](INDOOR_STATIC_LIGHTS.md),
but that light position is not the sprite's render anchor.

Dispatcher `0x43ea0c` passes this depth unchanged to the indoor hardware builder `0x4a42b7`, or to shared
software rasterizer `0x4ad16d`. Hardware sprite metadata retains native buffer dimensions and occupied-area
offsets. The quad's vertical bounds are constructed from the projected base anchor:

```text
topY    = anchorY - (originalHeight - occupiedOffsetY) * scaleY
bottomY = anchorY - (originalHeight - occupiedHeight - occupiedOffsetY) * scaleY
```

These are a behavioral description, not code copied into OpenYAMM. The builder also preserves horizontal
crop placement and mirroring. Every vertex receives the same anchor-derived depth and reciprocal W;
there is no per-pixel or per-vertex slope to follow a supporting wall.

## Depth testing against indoor walls

Indoor sprite builder `0x4a42b7` extracts integer anchor depth `d` from the packed record. At
`0x4a4435–0x4a4442`, it computes:

```text
z = 1 - 1 / (d * K)
rhw = 1 / d
K = 0.0617588945  // float stored at 0x4ea890
```

Indoor face rendering calls polygon submission `0x4a2e4f` from `0x4b12c3`. Its three vertex preparation
branches use the **same K and depth formula**, at `0x4a2f2a–0x4a2f47`, `0x4a3048–0x4a3065` and
`0x4a3302–0x4a331f`. The different depths of the wall vertices describe wall geometry; the sprite keeps
one anchor depth. Comparing these two paths rules out a separate numerical bias for ordinary indoor sprites.

Device initialization `0x49f545` / `0x49fb5a` explicitly enables Z testing and selects `D3DCMP_LESS`:

| State | First initialization path | Second initialization path |
| --- | --- | --- |
| `ZENABLE = 1` | `0x49f7ed` | `0x49fe2f` |
| `ZWRITEENABLE = 1` | `0x49f800` | `0x49fe42` |
| `ZFUNC = 2` (`LESS`) | `0x49f815` | `0x49fe57` |

Shared billboard queue drawing `0x4a16af` disables depth writes at `0x4a16f1`, draws entries back to front,
and restores depth writes at `0x4a17eb`. It does not replace `ZENABLE` or `ZFUNC`. This is ordinary
occlusion against the existing geometry depth buffer, not an always-visible sprite pass. The distinction
between depth tests and depth writes matters: changing writes alone does not prevent clipping by a wall.

The normal software branch also tests geometry occlusion: the nontransparent-pixel loop at
`0x4ad452–0x4ad468` accepts a sprite pixel when its unsigned packed depth/identity is less than or equal to
the existing pixel value, then updates color and the auxiliary buffer. The packed identity participates
in ties; this is not the same representation or exact equality rule as hardware floating-point Z.

Outdoors, ordinary decoration producer `0x47b61b` likewise starts with the native XYZ anchor at
`0x47b85e–0x47b881` and packs its projected depth at `0x47bc60–0x47bc7d`. Builder `0x4a3f0b` constructs
a cropped screen rectangle with constant depth, then uses the same shared queue. Its outdoor depth mapping
differs from the indoor mapping, as already recorded in the outdoor investigation.

## Comparison with OpenYAMM and the Goblinwatch report

At the time of this investigation:

| Behavior | Original MM8 | OpenYAMM |
| --- | --- | --- |
| Ordinary sprite plane | Screen-facing, one depth for the image | Camera-facing, one depth for the image |
| Indoor decoration placement | Project native base anchor, extend image upward on screen | Raise center by half the world height, then rotate quad around that center |
| Actors and ordinary outdoor sprites | Projected anchor with native crop placement | Shared camera-up base anchoring with crop offsets |
| Hardware sprite depth state | Tests geometry depth; queue disables writes | `LEQUAL` tests and depth writes in the ordinary sprite state |
| Wall-mounted decoration plane/bias | No special wall plane or offset in the traced ordinary path | No special wall plane or offset in the ordinary path |

OpenYAMM's indoor decoration center is constructed in
[IndoorRenderer.cpp](../game/indoor/IndoorRenderer.cpp)'s `renderDecorationBillboards`; its actor renderer
and the active combined outdoor sprite renderer instead use
[spriteBillboardCenter](../game/render/SpriteAtlasCache.h). For an uncropped height H, the indoor
decoration center is `anchor + worldUp * H/2`; base-anchored camera-facing construction is
`anchor + cameraUp * H/2`. Those agree at level pitch, but otherwise change placement and the depth used
for the whole image. The magnitude of their camera-depth difference is `H/2 * abs(sin(pitch))`.

An OpenYAMM desktop capture reproduced the reported Goblinwatch clipping at foot position
`(553.988, 6724.987, 96)`, yaw `4.519` radians, pitch `-26.471` degrees. At the same position and yaw
with zero pitch, the torch flames were clear. This is evidence of pitch-dependent occlusion in the
current renderer, not an original-MM8 visual test or proof that changing the anchor alone fixes it.

The first compatibility experiment should compare native base anchoring with the current midpoint
construction at both pitches. Correcting that difference might still leave flat-sprite occlusion
artifacts. Wall-aligned upright decoration planes would be a deliberate rendering improvement beyond
the original behavior. A global depth bias also changes which surrounding geometry can hide a sprite
and has no support in the traced ordinary MM8 indoor path.
