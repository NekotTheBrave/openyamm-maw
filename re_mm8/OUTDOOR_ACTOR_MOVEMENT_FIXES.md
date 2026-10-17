# Outdoor actor grounding, gravity and step clearance

Implemented and checked 2026-09-14, following the reproduced defects in
[CREATURE_MOVEMENT_COMPARISON.md](CREATURE_MOVEMENT_COMPARISON.md).

## Behavior

- Grounded actors adhere to downward changes of at most **8 units**. Losing elevated support beyond that
  distance leaves their height intact and starts falling instead of snapping to the lower floor.
- Ground walking changes horizontal velocity while preserving accumulated vertical velocity. Landing
  still cancels downward velocity; flying steering continues to supply its own vertical velocity.
- Actor floor-query corrections and collision-driven step-ups check the vertical lift and the standing
  body's height and radius against nearby geometry. The conservative clearance check covers sloped and
  off-center obstructions, preserves Untouchable semantics, and excludes the destination tread and
  adjoining support surfaces. An obstructed upper floor does not hide usable support beneath it.

The existing step-height limits, route planner, indoor controller, and gravity strength were not changed.
This fixes upward support changes; it is not a replacement for the general outdoor collision solver.

Sources: [OutdoorMovementController.cpp](../game/outdoor/OutdoorMovementController.cpp),
[OutdoorWorldRuntime.cpp](../game/outdoor/OutdoorWorldRuntime.cpp).

## Avoiding additional movement cost

Clearance checks run only for upward corrections and use the already collected nearby-face indices.
They allocate no additional candidate lists. Three reductions in repeated work offset those checks:

1. Reuse the last validated support result unless a subsequent collision response changes the position.
2. Compute terrain-versus-bmodel ground classification once per movement operation, passing it through
   floor queries. In bmodel-ground scenes, this avoids repeatedly scanning the height map.
3. Select the highest eligible floor directly instead of allocating and then scanning a temporary vector.
   The rise filter, terrain minimum, equal-height ordering and preferred-support rules remain in place.

## Validation

Both `openyamm` and `openyamm_unit_tests` built successfully with `-j25`.

**102 focused unit cases passed, with 718 assertions.** These include new actor tests for platform exits
and landing, small downward steps, walking/stationary gravity, step limits, obstructed lifts, standing
clearance, sloped/offset overhead faces, vertical obstructions and Untouchable faces. Existing New Sorpigal
bridge entry/seam, party movement, indoor movement, pathfinding and bmodel-world cases also passed.

**Six headless checks passed:** the new walking-gravity scenario, four existing ship-support/nearby-wall
checks selected by `actor_51_`, and the existing Jeric-under-tent support check. The new scenario follows
a walking ground actor through the actual world/AI update for half a second. Replacing only the runtime
object with its pre-fix version makes that scenario fail with a 5-unit fall and vertical velocity -10,
confirming coverage of the steering reset rather than just the isolated controller.

Permanent coverage is in [OutdoorPathfindingBuilderTests.cpp](../tests/OutdoorPathfindingBuilderTests.cpp)
and [HeadlessOutdoorDiagnostics.cpp](../game/outdoor/HeadlessOutdoorDiagnostics.cpp).

## CPU timing

Local optimized (`-O3 -DNDEBUG`) controller microbenchmarks compared the saved pre-change controller
object with the final controller, using identical fixtures and toolchain. Four sequential runs used
before/after/after/before order, with no concurrent builds or tests. Each run took nine timing batches per
fixture; values below average the two run medians for each version. Normal/terrain fixtures used 200,000
calls per batch, and bmodel-ground step fixtures used 5,000. Setup and map loading were outside timing.

| Fixture | Before, ns/call | After, ns/call | Less CPU time |
| --- | ---: | ---: | ---: |
| Flat terrain | 308.7 | 196.2 | 36.4% |
| Flat bridge support | 492.9 | 316.8 | 35.7% |
| Ascending ramp | 802.1 | 589.1 | 26.6% |
| Clear step, bmodel-ground scene | 14,925.3 | 3,944.6 | 73.6% |
| Obstructed step, bmodel-ground scene | 14,981.1 | 4,136.7 | 72.4% |
| Clear step, terrain scene | 643.0 | 416.0 | 35.3% |
| Obstructed step, terrain scene | 659.6 | 558.9 | 15.3% |

All seven measured controller paths improved. Obstructed-step results deliberately differ in position:
the old version accepted the invalid lift, while the fixed version rejects it. These are CPU microbenchmarks,
not GPU or whole-game FPS measurements. Benchmark sources, saved baseline objects and raw logs from this
session are under `/tmp/openyamm-outdoor-movement-fix/`.
