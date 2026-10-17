# Directional sunlight in OpenYAMM

Implemented 2026-09-14 following [the original MM8 lighting investigation](LIGHTING.md).
This addresses the outdoor-normal finding in [the earlier implementation comparison](OPENYAMM_LIGHTING_COMPARISON.md).
Indoor lighting, dynamic-light selection and local-light attenuation are separate work.

**Night correction:** [the Harmondale investigation](NIGHT_DARKNESS.md) found the old darkness overlay was
inside the near clipping plane. Base illumination now consumes the existing atmosphere directly; ordinary sprites
share that day/night response. The earlier fixed 65% ambient floor has been removed.

## Behavior

- Classic exterior ODM terrain and buildings receive normal-dependent base illumination.
- The atmosphere owns the clock and X/Z sun arc, from 05:00 to 21:00 with its highest point at 13:00.
- Ambient follows the existing OE-style daily curve, from 0.15 to 0.69, giving about 0.256 at 03:30.
  Direct intensity is ambient + 0.3, faded through 05:00–06:00 and 20:00–21:00 and zero at night.
  Base illumination is capped at 0.85 before adding local lights, following the local OE surface behavior.
- Terrain decorations use their existing terrain normals. Ordinary billboards use the horizontal-ground sunlight
  response, capped at 0.85; at night their base equals ambient. This keeps surface and sprite darkness consistent,
  rather than copying OE's separate legacy sprite tint, which reaches 39/255 at night.
- Local FX contributions remain additive after base illumination. Night darkness adds no screen-wide draw pass.
- Enclosed locations, underwater views, `BModelWorld` scenes and authored lighting data retain their previous
  lighting path. The atmosphere respects always-light/always-dark profiles independently of the clock.
- Gameplay screen tints use a clip-space shader, so the camera near plane cannot clip them.
- No occlusion or cast shadows are added.

## Implementation and cost

[OutdoorSunlight.h](../game/outdoor/OutdoorSunlight.h) computes one direction/strength uniform per world render.
[OutdoorRenderer.cpp](../game/outdoor/OutdoorRenderer.cpp) uploads it alongside existing surface uniforms for each
relevant draw. The shader computes the orientation term per vertex; the fragment shader applies the interpolated
scalar only to base illumination, without a new light loop or draw pass.

Terrain normals match the rendered triangle diagonal using the existing terrain-normal helper. They are calculated
when geometry is built and carried unchanged through chunking. Building normals use the authored face plane where
available and the existing geometry helper otherwise. Runtime mechanism batches rotate their face normal once per
batch, excluding pivot and translation, alongside their existing transformed positions.

The textured vertex gains three floats (12 bytes), increasing its stride from 40 to 52 bytes. A full 32,258-triangle
unindexed terrain buffer gains 1,161,288 bytes; the existing separate chunk buffers also carry these normals.
There is no per-frame terrain rebuild. This trades modest memory/vertex bandwidth and shader arithmetic for surface
shape cues. Ground-cover instances gain no additional data. No measured FPS improvement or overhead is claimed.

## Validation

The original geometry/normal implementation was checked on MM6 New Sorpigal, MM7 Emerald Island and MM8 Dagger
Wound Island, with desktop GLSL and Android GLES shader compilation. Its original night validation was incomplete;
see the diagnosis linked above.

The subsequent darkness correction passed the development build, 17 focused unit cases (116 assertions), and
13 weather/runtime regressions covering 03:30, dawn/dusk, noon and forced lighting. Three existing Torchlight
unit cases (16 assertions) also pass. Six relevant shaders compile for
Android GLES 3.0. Harmondale captures at the reported camera show darker terrain/buildings/sprites at 03:30 and
progressively brighter dawn/noon. Records live in `/tmp/openyamm-night-fix/`; the native NVIDIA desktop smoke run
exited successfully (`/tmp/openyamm-desktop-harmondale_night_fix-l1fg1_co/run.json`).

These are correctness checks, not A/B performance measurements. No Android device performance claim is made.
