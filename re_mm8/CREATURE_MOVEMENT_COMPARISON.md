# Creature movement: original MM8 versus OpenYAMM

Compared 2026-09-14 against the current OpenYAMM working tree, including its existing uncommitted changes.
Original behavior is established in [CREATURE_MOVEMENT.md](CREATURE_MOVEMENT.md), using the verified original
`MM8-Rel.exe` with SHA-256 `5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
OpenEnroth is a local behavioral reference, not a source of implementation code.

**Recommendation: retain our architecture and fix specific movement invariants.** Replacing it with MM8's
solver would lose pathfinding and several useful collision protections. However, the comparison uncovered
reproducible outdoor defects and an important planner/body mismatch. These deserve attention before adding
more stuck-recovery behavior.

No production code was changed for this comparison. Temporary observational probes and their output are in
`work/creature_comparison/`; they reuse OpenYAMM's own synthetic test fixtures.

**Implementation follow-up:** the three reproduced outdoor defects have since been fixed. See
[Outdoor actor movement fixes](OUTDOOR_ACTOR_MOVEMENT_FIXES.md) for the changes, regression coverage and
before/after CPU measurements. The descriptions below preserve the pre-fix investigation.

## Outdoors

| Concern | Original MM8 | Current OpenYAMM | Assessment |
| --- | --- | --- | --- |
| Routing | Heading/state steering in the traced movement chain; collision redirects movement. | Shared waypoint planner for eligible pursuit; partial routes, retries, source recovery, and worker budgets. | Keep our planner. It can navigate around obstacles that simple steering cannot solve reliably. |
| Collision body | Two swept spheres; ground monster types use radius 40. | Actor-specific outdoor solver uses low/high spheres plus middle and face-center-height probes, and radius 40 for nonflying types. | Keep the additional body coverage; this is still an approximate body sweep. |
| Terrain/building support | Fresh terrain plus bmodel floor query; no persistent model attachment. | Tracks support kind, model, face and airborne state; prefers existing support around edges. | Useful for bridge seams, but support must remain geometrically valid. |
| Getting onto a bmodel | Floor selection plus actual face collision; no proven universal upward-step constant. | Default body step budget 128; conditional floor selection allows upward support changes within the current model, while collision can also raise the actor onto floors. | Keep explicit limits, but validate the actual raised body and transition. Do not interpret 128 as verified original MM8 behavior. |
| Leaving a bmodel | Lower support leaves the actor airborne; gravity handles descent. No indoor-style 100-unit drop avoidance in this outdoor loop. | Ground-settling helper can snap a previously grounded actor to arbitrarily lower selected support. | Confirmed defect: a large drop can become an instantaneous position change. |
| Falling while walking | Ground walkers preserve vertical velocity and gravity changes it. | Walking integration assigns zero to nonflyers' vertical velocity before the controller call. The separate passive physics path preserves it. | Confirmed state-flow defect: falling depends on whether movement intent is active. |
| Walls and retry behavior | Sweeps, earliest hit, plane response/pushout, damping, up to 100 attempts. | Similar actor response and 100-attempt ceiling, with a spatial grid for world faces. | Keep broad phase and sweep. A large retry cap does not repair an invalid step or floor snap. |
| Water | Terrain restrictions with exceptions for supported bmodels and appropriate monster types. | Water-aware path maps, support checks and movement restrictions; bridge cases have tests. | Keep. Verify water/bridge transitions when fixing grounding. |

Relevant source:

- [OutdoorMovementController.cpp](../game/outdoor/OutdoorMovementController.cpp):
  `queryPreferredSupportFloor`, `actorCanStepUpWithinCurrentBModel`, `queryFloorLevel`,
  `resolveOutdoorActorMove`.
- [OutdoorWorldRuntime.cpp](../game/outdoor/OutdoorWorldRuntime.cpp):
  `actorCollisionRadius`, `applyOutdoorActorMovementIntegration`, `applyOutdoorActorPhysicsStep`.
- [OutdoorPathfindingBuilderTests.cpp](../tests/OutdoorPathfindingBuilderTests.cpp):
  synthetic bridge seam, New Sorpigal terrain-to-ramp, support above water, and party step/ceiling cases.

### Confirmed: large drops snap down

`resolveOutdoorActorMove` determines that the actor is grounded on its starting support. Its
`settleActorToGround` helper subsequently snaps downward whenever `!flyingActive && actorGrounded`, without
limiting `dropToGround`. The previous grounded classification therefore authorizes snapping to a new, much
lower floor.

Probe: flat terrain at Z=0, a platform at Z=160, ground actor radius 40 and height 128, moving horizontally
at 1000 units/s with 1/128-second updates. The old face remains preferred slightly past its edge because of
the radius-based support footprint. When that support is lost:

```text
EXIT_TRANSITION tick=5 old_z=161 new_z=1 airborne=0
```

**Change:** restrict downward adhesion to a small, explicit snap distance. Beyond that distance, preserve
the actor's current height, mark it airborne, and integrate gravity. Keep support candidate identity separate
from the assertion that the body is actually grounded. Test ramp descent, bridge exits and terrain drops.

### Confirmed: walking discards accumulated gravity

`applyOutdoorActorMovementIntegration` sets:

```text
actor.velocityZ = pStats->canFly ? effectiveDesiredMoveZ * moveSpeed : 0.0f;
```

That value is passed into `resolveOutdoorActorMove`. Its initial actor velocity uses the supplied value,
not the previous move state's vertical velocity. In contrast, `applyOutdoorActorPhysicsStep` passes through
the actor's accumulated vertical velocity. The normal frame dispatch uses that passive path when movement
intent is absent; it does not compensate for the reset during ordinary movement integration.

Controller probe starting at Z=1000, moving horizontally for one second over flat terrain:

| Input policy | Z after one second | Final vertical velocity |
| --- | ---: | ---: |
| Pass zero each tick, matching ground walking integration | 990 | -10 |
| Preserve the preceding result's vertical velocity | 355 | -1280 |

These are synthetic integration results using our existing gravity, not measurements of native MM8 and
not a proposal to change gravity strength.

**Change:** steering should update horizontal velocity for ground actors; gravity should own their vertical
velocity until landing or another explicit physical event resets it. Test walking, standing, stunned and
corpse cases to ensure transitions do not accidentally reset or duplicate physics.

### Confirmed: actor step-up can bypass a ceiling

The existing test named `outdoor BModel movement cannot step through an overhead face` exercises
`resolveMoveForBody`, the party/body path. It does not exercise `resolveOutdoorActorMove`.

Running the same OpenYAMM-owned synthetic step geometry through the actor path gives:

```text
ACTOR_STEP ceiling=0 end=160,81
ACTOR_STEP ceiling=1 end=160,81
```

The fixture has an 80-unit raised floor, an optional overhead face at Z=40, and a body of radius 10 and
height 30 initially at Z=1. It fits below the ceiling initially. The actor ends above that ceiling in both
cases. The actor floor-selection/step adjustment changes Z without equivalent validation of the vertical
transition and raised-body clearance.

**Change:** make an upward support change a validated step operation: allowed rise, usable destination
support, free standing volume, and a collision-safe transition. Apply that to both floor-query-driven
raises and collision-driven raises. Do not solve it merely by reducing the 128-unit budget: a smaller
unchecked raise can still cross a thin blocker.

## Indoors

| Concern | Original MM8 | Current OpenYAMM | Assessment |
| --- | --- | --- | --- |
| Collision coverage | Two spheres, sector/near-portal candidates, face/edge tests. | Low/high spheres plus middle and face-center-height probes, swept bounds, sector/portal filtering. | Keep the additional coverage; it addresses obstacles between the original sphere heights. |
| Movement subdivision | Remaining-distance collision retries; up to 100 outer attempts. | Distance-based substeps at 24 units, capped at 64; up to eight sweep responses per substep. | Keep bounded substeps; do not replace them with the original loop count. |
| Floor/ceiling validity | Floor query, sector retry and floor collision. | Support face and grounded state, floor and eye sectors, clearance/steepness validation and moving-geometry cache updates. | Already stronger in several respects. Preserve these checks. |
| Upward steps | The original 50-unit check triggers sector recovery; not a universal step cap. | Explicit 50-unit controller rise limit; ordinary ground path requests use step height 40. | An intentional policy is useful, but planner and controller have different limits. |
| Dangerous drops | Grounded walking actors reject drops over 100 units. | 100-unit guard including a leading-footprint check; runtime disables this guard while a path is active. | Keep the guard, but explicitly define which path-authorized descents may bypass it. |
| Actor crowding | Collision and local reactions; already-overlapping indoor actors can separate without their overlap blocking movement. | Existing-overlap escape, smaller peer collision radius 24, shared sidestep/retreat/stand logic; some distant path-followers ignore peer collision. | More capable but intentionally more permissive. Audit doorway crowd behavior before making collision stricter. |
| Stuck/invalid position | Local collision responses and sector retry; no general persistent rescue system found. | Wall tangent/overlap recovery, unsupported-path recovery, batched invalid-position recovery and path progress tracking. | We already have recovery. The priority is ensuring it cannot circumvent geometry. |

Relevant source:

- [IndoorMovementController.cpp](../game/indoor/IndoorMovementController.cpp): constants, `resolveMove`,
  `resolveMoveSingleStep`, `tryRecoverFromWallOverlap`, `tryRecoverAlongWallTangent`,
  `tryRecoverUnsupportedTowardStep`, `collectSweptCollisionFaceCandidates`.
- [IndoorCollisionPrimitives.cpp](../game/indoor/IndoorCollisionPrimitives.cpp): body and face sweeps.
- [IndoorWorldRuntime.cpp](../game/indoor/IndoorWorldRuntime.cpp): `applyIndoorActorMovementIntegration`,
  `recoverInvalidIndoorActorPosition`, `actorMovementCollidersForActorMovement`.
- [GameplayRuleRegressionTests.cpp](../tests/GameplayRuleRegressionTests.cpp): indoor actor overlap,
  tangent recovery, head-on wall recovery, clearance, ledges and leading-footprint cases.

### Recovery needs stronger, uniform constraints

This is a **source-level risk assessment**, not a reproduced indoor wall-crossing bug:

- Normal-direction wall recovery tries offsets of 4 through 128 units in both signs and checks candidate
  positions while ignoring the original overlap face. Unlike tangent recovery, that helper does not call
  `recoveryTraceBlockedByFace` on the transition. A valid destination alone is insufficient proof of a safe
  movement segment.
- Invalid-position recovery can consider floors up to 8192 units vertically away when sector information is
  missing. Nearest-floor-center recovery does have a `traceCollisionIssues` check, so it would be incorrect
  to describe it as entirely unchecked teleportation. That trace filters wall-like blockers; the same-XY
  floor correction does not pass through this nearest-center trace branch.
- Ground path-followers enable unsupported recovery and disable the normal ledge guard. Ordinary planner
  segments have drop checks, but recovery movement can exceed the requested tick displacement and needs
  its own support/transition validation.

**Change:** share the physical acceptance rules between normal movement and local recovery. Keep recovery
bounded and on a verified side of a blocker; validate the whole body along the correction where applicable.
Separate initial placement repair from runtime rescue. For a runtime invalid state, prefer revalidated
recent safe support or a local connected candidate, rather than searching arbitrary floors. A stored safe
position must itself be checked against current doors/mechanisms before reuse.

Do not simply remove recovery: existing tests intentionally depend on it to escape wall contacts.
Add adversarial fixtures with parallel walls, thin separating walls, stacked rooms, closed doors and a
ledge beside the recovery target before changing this behavior.

### Peer collision is deliberately relaxed

When a path is active, movement intent is eligible, the actor is outside the party's sector, and target
distance is at least 768, `ignoreActorCollisionForMovement` becomes true. The controller skips colliders
whose `reportActorContact` flag is set; the separately flagged party collider remains blocking. This does
**not** turn off world-wall collision.

If overlapping enemies at doorways are a visible issue, replace this broad peer bypass with bounded local
avoidance/yielding while retaining initial-overlap escape. This is a behavior tradeoff, not an automatic
MM8-fidelity fix: turning full collision back on without crowd handling can reintroduce traffic jams.

## Shared planner and physics agreement

`PathObject` contains radius, step length, step height and flight capability, but **no body height**.
Ground segment checks in [PathMap.cpp](../game/pathfinding/PathMap.cpp) trace around the lower-body height
`floor + radius`; they cannot express the full standing clearance of a taller creature.

Probe with floor Z=0, a blocking overhead plane Z=80 and radius 40:

```text
PATH_CLEARANCE ceiling=80 radius=40 reachable=1 (actor height 128 cannot be expressed in PathObject)
```

This confirms that a route acceptance check can disagree with body clearance. A valid movement controller
may prevent penetration, but the actor can repeatedly pursue a route that it cannot physically execute.

There are additional policy differences:

- Indoor path requests use step height 40; the controller's upward limit is 50.
- Outdoor path requests and body defaults both use 128, but the planner lacks the controller's conditional
  model-support state and therefore does not express identical terrain/model transition rules.
- Drop limits in both `PathMap.cpp` and `PathPlanner.cpp` are derived from step height and radius:
  `max(stepHeight, min(radius, 2*stepHeight))`. For ordinary indoor requests this is 40–80, while the controller
  has a 100-unit avoidance guard. Outdoor requests with radius 40 derive a 128-unit planned drop limit.
- Planner segment sampling uses 2-unit spacing. That is spatial sampling, not the actor physics tick.

**Change:** give route queries the actual body height and explicit upward-step/downward-drop policy.
Use the same authoritative traversal rules for planning, direct checks, shortcutting and execution, while
retaining separate BLV/ODM geometry implementations. Do not introduce a forwarding layer that merely hides
two different sets of rules. Conservative planning margins can remain, but must be explicit and tested.

This does not require running the complete 128 Hz movement controller inside every search expansion.
Use inexpensive support/clearance checks and validate executed transitions; add planner-versus-controller
fixtures to ensure each accepted route segment is physically executable for its declared body.

## Performance and existing optimizations

We already have spatial indexing for outdoor faces/path geometry, indoor sector filtering and geometry
caches, a 1/128-second actor update, two path workers per runtime, 8000-node request limits, retry cooldowns,
and path scheduling. The scheduling constants include a two-request per-step budget and a shared 0.1-second
admission interval; the budget alone should not be described as 256 actual plans per second.

One concrete cleanup opportunity is outdoor dynamic collider construction:

- `buildNearbyActorMovementColliders` scans the map actor array, checks the active mask, copies collider
  data including names, and produces a vector.
- Active moving actors call it before their individual movement operation; `setActorColliders` copies
  that vector again. This repeats work each fixed update.
- Build the collider representation once per update and update moved actors' entries as movement commits,
  preserving sequential collision behavior. Add a local spatial query if profiling justifies it. Indoor
  code already exposes per-actor collider-position updates.
- Keep collision participation distinct from the full-AI budget: our outdoor builder currently excludes
  actors outside the active mask, whose selection is capped at 48. Original MM8's nearby-AI cap of 30 did
  not similarly restrict its full actor collision scans. Audit dense crowds before changing participation;
  enabling all map actors through full scans would be the wrong performance fix.

Do not copy MM8's fixed 20/50 floor candidate capacities or its numeric storage limitations. Do not raise
the outdoor 100-attempt cap to treat sticking; inspect exhausted attempts and fix the invalid transition.

## Suggested implementation order

| Priority | Change | Expected runtime cost | Main regression exposure |
| --- | --- | --- | --- |
| 1 | Preserve falling velocity during outdoor steering; bound downward ground snap. | Negligible arithmetic/state cost. | Ramp adhesion, ledge descent, stun/death and water transitions. |
| 1 | Validate outdoor actor step transitions and overhead clearance. | Extra local geometry checks when attempting a rise. | Stairs, ramps, bridges, low ceilings and unusual body sizes. |
| 2 | Add body height and explicit traversal limits to planner queries; align with execution. | More clearance checks during planning/direct checks; measure within existing budgets. | Routes previously accepted may be correctly rejected; false rejection could strand actors. |
| 2 | Constrain indoor recovery using full transition validity and current geometry. | Additional work on recovery paths. | Existing wall escape and invalid-spawn repair; moderate-to-high behavior risk without fixtures. |
| 3 | Reuse/update outdoor actor collider data; profile before further indexing or cap changes. | Expected CPU/allocation reduction; no measured percentage yet. | Stale collider positions, update ordering and active/inactive crowd boundaries. |

Keep our shared steering/path architecture, original-style physical checks where sound, indoor clearance
and ledge protection, authored Untouchable semantics, and bridge-support handling. Current OE can guide
individual design decisions, but neither OE nor MM8 should be adopted as a wholesale replacement.

## Validation and limits

- Built `openyamm_unit_tests` with the repository's prescribed build command.
- Ran 82 selected existing cases: **82 passed, 512 assertions passed**. Selection covered actor movement,
  indoor sweeps, actor path runtime, path builders, bridge paths, and BModel step tests.
- Ran four observational probes against the current compiled movement/path code; numeric results above
  are preserved in `work/creature_comparison/probe-results.txt`. These report existing behavior; their
  successful process exit does not mean the demonstrated behavior is correct.
- The probe runner substitutes a temporary test translation unit in a separate executable, preserving
  production source and the normal unit-test executable. Reproduce after building tests with
  `python3 re_mm8/work/creature_comparison/run_probes.py`.
- No performance benchmark, new full-game scenario, or original-MM8 runtime experiment was conducted for
  this comparison. The indoor recovery and dense-crowd concerns remain candidates for targeted reproduction.
