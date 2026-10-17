# Outdoor night darkness: screenshot 1309

Investigated 2026-09-14 from [test_img/1309.png](../test_img/1309.png): MM7 Harmondale (`7out02.odm`),
position `(-10750.869, 15485.513, 385)`, yaw `4.092` radians, pitch `-6.790` degrees, user-reported time 03:30.
The screenshot shows a dark sky with comparatively bright surfaces and especially bright foliage/billboards.

The diagnosis below records the pre-fix behavior; the implemented correction is documented at the end. Original MM8 was checked
with Rizin against `mm8_orig/MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`, reverified during this investigation.
Local OpenEnroth reference: commit `d98839ba8`. Its current renderer is not the original MM8 executable.

## Root cause in OpenYAMM before the fix

1. **The intended darkness overlay is entirely inside the near clipping plane.**
   `renderOutdoorDarknessOverlay` in [OutdoorRenderer.cpp](../game/outdoor/OutdoorRenderer.cpp) places a
   camera-facing quad at distance **1**. [OutdoorGameView.cpp](../game/outdoor/OutdoorGameView.cpp) sets
   `OutdoorCameraNearClip = 4` and supplies that projection and the same camera basis to the main view.
   Every overlay vertex therefore lies closer than the near plane. Disabling depth testing in the draw state does
   not disable geometric clipping. For an OpenGL projection with near=4 and far=24576, this quad has `z/w` about
   **-7.001**, outside the visible interval [-1,1]. Its CPU submission does not establish that it reaches the screen.
   The main view uses sequential submission, so this finding is not an inference about draw sorting.

2. **Time-dependent ambient is calculated but does not reach base surface/billboard illumination.**
   [OutdoorWorldRuntime.cpp](../game/outdoor/OutdoorWorldRuntime.cpp), `normalizedAmbientBrightness` and the
   atmosphere update, calculate a daily brightness curve. At 03:30 it yields **0.255634**. The atmosphere converts
   this to `darknessOverlayAlpha = 0.442409`, with night tint RGB `(16,24,52)`.
   Current geometry instead receives the fixed **0.65** ambient from
   [OutdoorSunlight.h](../game/outdoor/OutdoorSunlight.h). Direct sunlight correctly becomes zero at this time.
   Ordinary billboards use the fixed **0.85** base in
   [OutdoorBillboardRenderer.cpp](../game/outdoor/OutdoorBillboardRenderer.cpp), plus sampled local illumination.
   The shader multiplies texture RGB by these lighting values. Neither base uses `AtmosphereState::ambientBrightness`.

3. **Darkening the sky/fog is not sufficient to darken nearby objects.**
   `outdoorClearDistanceFogBrightness` returns **39** at night, and `computeOutdoorSkyTintAbgr` uses it. Classic
   clear-weather distance fog starts at 75% of the visible distance. Nearby terrain and buildings consequently do
   not receive the distant dark fog color. The runtime's field named `fogDensity` tracks night/twilight, but it is
   not automatically a near-surface brightness uniform. A dark sky alongside bright objects is the expected result
   of these disconnected paths, matching the screenshot.

The directional-sunlight change relied on the existing overlay for broader night darkness. Its validation checked
that direct sunlight disappeared and that the ambient floor stayed stable, but missed the clipped overlay. The
65% floor was a presentation choice, not a recovered original nighttime value. That validation was incomplete.

Simply making the overlay visible would restore a substantial missing effect: at 03:30 its source-color multiplier
would be about **0.5576**, followed by addition of the night tint. It would also tint local FX, sprites and the sky
because it is submitted after them. That is different from applying darkness to ambient before adding local lights.
The same quad helper also handles the separate gameplay overlay, so its clipping error is not confined to night.

## Original MM8: time state and actual darkening

The verified routine `0x48a274` sets the night flag at `0x6f2ea8` and returns the night/twilight scalar:

| Time | Night flag | Scalar |
| --- | --- | --- |
| Before 05:00 | Set | 1 |
| 05:00–06:00 | Clear | Falls from 1 to 0 |
| 06:00–20:00 | Clear | 0 |
| 20:00–21:00 | Clear | Rises from 0 to 1 |
| From 21:00 | Set | 1 |

Our ordinary clock classification agrees with this. 03:30 is unambiguously night; changing those boundaries would
not resolve the screenshot.

The Direct3D tint helper **`0x47d384`**, reached through wrapper **`0x4a1af1`**, explicitly consumes the flag.
Its night branch begins at `0x47d3e8`. It starts with a range factor of one, or reads the power of an active
party-buff record at `0xb21738`/`0xb21740`. The range is **1024 × factor**, verified by the float at `0x4ea7a8`.
For its ordinary depth-dependent branch, positive view depth within that range produces approximately
`dimming = round(216 * depth / range)`. Beyond the range, dimming is **216**. A separate mode and zero depth take
the maximum-dimming branch directly. Billboard sampling can subsequently reduce dimming in response to local lights.
The function clamps dimming and packs grayscale RGB as **255 minus dimming** (`0x47d49e`).

Thus a distant, unlit receiver in this traced D3D path gets **39/255 ≈ 15.3%** modulation. It is incorrect to describe
every original receiver as uniformly 15.3% bright: the near-camera range, local lights and special branches matter.
The original uses depth-dependent near visibility in addition to its other light sources. It does not depend on a
single final screen tint for all outdoor darkness.

The software polygon helper **`0x47cc91`** likewise consumes the night flag and range, producing a darkness level
up to **27** before its palette/shading path. Palette colors are not interchangeable with modern linear-light RGB.
Underwater and special-effect branches exist in these routines; the clear outdoor night case is the relevant one
for this comparison. See [LIGHTING.md](LIGHTING.md) for broader D3D/software and source-selection details.

## Local OpenEnroth comparison

[Outdoor.cpp](../reference/OpenEnroth-git/src/Engine/Graphics/Outdoor.cpp), `GetFogDensityByTime`, retains the same
05:00/06:00/20:00/21:00 classification.

The current [OpenGLRenderer.cpp](../reference/OpenEnroth-git/src/Engine/Graphics/Renderer/OpenGLRenderer.cpp), in both
terrain and outdoor-building drawing, sends a time-varying ambient directly to the sun-lighting uniforms. Its curve
ranges from **0.15 to 0.69** and gives **0.255634 at 03:30**. Direct sunlight is zero while the night flag is set.
Point lights are handled separately. The terrain/building fragment shaders consume this illumination before fog
and gamma correction. Our runtime already calculates this same daily ambient curve, but currently redirects its
effect into the clipped overlay instead of these base lighting inputs.

For ordinary sprite tint, [LightmapBuilder.cpp](../reference/OpenEnroth-git/src/Engine/Graphics/LightmapBuilder.cpp),
`GetActorTintColor`, starts night dimming at **216**, lets local billboard lights adjust it, then returns grayscale
`255 - dimming`. This is distinct from both OE's modern surface shader and original MM8's near-depth branch.

| At 03:30, before local lights/fog/gamma | Relevant lighting value |
| --- | --- |
| Original MM8 traced D3D tint, beyond near-light range | 39/255 ≈ 0.153 |
| Current OE terrain/building ambient | ≈ 0.256; direct sun off |
| Current OpenYAMM classic geometry | 0.65; direct sun off |
| Current OpenYAMM ordinary billboard base | 0.85 |
| OpenYAMM atmosphere's intended overlay | Alpha ≈ 0.442, but clipped away |

These are algorithm inputs/modulation factors, not measured display luminance or a gamma-corrected visual ratio.

## Recommended correction

Use one authoritative atmosphere/day-night state to drive base illumination for terrain, buildings, ground cover
and ordinary billboards. Keep local lights additive after that base term, so Torchlight and luminous effects remain
effective in darkness. Preserve authored-lighting, underwater, always-light/always-dark and enclosed-location rules.
The existing daily ambient curve is a useful modern baseline; exact original near-camera falloff would be a separate
fidelity choice rather than something to infer from OE's current shaders.

Render actual screen-tint effects in screen/clip space or a dedicated orthographic view so camera near distance
cannot clip them. Avoid simultaneously applying a full lighting-darkness overlay and a newly darkened ambient,
which would count night darkening twice and weaken luminous effects.

Validate at the supplied Harmondale camera at 03:30, midnight, dawn and noon, including tree/actor billboards and
Torchlight on/off. A render check is essential: the existing time/ambient unit checks can pass while the entire
darkening primitive is clipped.

## Implemented correction

The renderer now passes `AtmosphereState::ambientBrightness` and its sun direction/day-night state directly into
classic outdoor surface lighting. Direct sunlight is off at night; in daytime its intensity is ambient + 0.3,
with the existing twilight fade. The surface base is capped at 0.85 before local light contributions, following OE's
modern surface approach. Ordinary billboards share the horizontal-ground base response rather than retaining 0.85
at night. This deliberately differs from OE's legacy 39/255 sprite tint to keep our surfaces and sprites consistent.

The clipped lighting overlay was removed. Its serialized fields remain present but are reset to zero; there is no
save-format change or second darkness pass. Separate gameplay tints now use `vs_screen_color.sc`, which supplies
clip-space positions independently of the camera. Authored lighting, enclosed, underwater and BModelWorld paths
retain their previous surface/sprite treatment. Forced-light and forced-dark profiles select the corresponding
ambient endpoint and direct-light state.

At 03:30, base lighting is approximately 0.256 instead of 0.65 for surfaces and 0.85 for ordinary sprites.
These are texture modulation factors, not a display luminance measurement. Point-light loops, attenuation and
source selection remain unchanged. The fix adds no lights, per-pixel loops or geometry bandwidth; the sun term
still runs per vertex, and one clipped darkness submission is removed when it would previously have occurred.

Validation records are under `/tmp/openyamm-night-fix/`: focused unit checks, weather/runtime checks, GLES shader
outputs and Harmondale captures at 03:30, 05:30 and noon. The fixture advances into the next day because the scenario
clock cannot rewind the outdoor runtime; an initial morning capture named `harmondale_before` is therefore **not**
a valid nighttime before/after comparison. The corrected captures use the supplied pose and native textures, with
the daily fog selected by the normal weather simulation. Native desktop startup also passed on the NVIDIA GPU.
No hardware A/B benchmark, Android device run, or visual Torchlight on/off comparison was performed.
