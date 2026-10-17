# Original MM8 creature movement: outdoors and indoors

Investigated 2026-09-14 with Rizin 0.10.0 and rz-ghidra. Target:
`mm8_orig/MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
This is the GOG-verified original executable, opened read-only. Addresses below are preferred virtual addresses
in that executable, with image base `0x400000`. They are **not** the patched `mm8.exe` addresses in older notes.

The findings describe native actors, including walking town NPCs represented by monster records. These actors
use the same physical movement loops; hostility affects collision reactions, not whether buildings are solid.
This does not describe stationary house-dialogue portraits or the party movement controller.

**Main finding:** MM8 combines state-driven steering, two swept collision spheres, floor queries, gravity,
surface-specific collision response, and limited retries. It does not have a universal guarantee against sticking,
penetration or falling through malformed geometry. In particular, the traced original actor loops do **not** have
the explicit 128-unit upward-step rejection found in the current local OpenEnroth indoor actor solver.

## Evidence and confidence

- **Verified:** branch conditions, constants, structure accesses and call relationships listed with addresses.
- **Interpretation:** names such as “floor query,” “sphere sweep,” “flee” and “sector” combine those observations
  with local MMExtension structure definitions and local OpenEnroth behavior. Decompiled argument lists are not
  reliable calling-convention declarations; significant claims were checked against instructions and callers.
- **Not measured:** maximum traversable step for a particular mesh, recovery success rate, actual iteration counts
  per frame, or behavior after runtime patches. No original-MM8 gameplay session was executed for this study.

Reusable labels are in [mm8_rel_5326d564_creature_movement.rz](mm8_rel_5326d564_creature_movement.rz).
Raw disassembly/decompiler exports and the disposable analysis project are under `work/creature_movement/`.
Follow [RIZIN_GUIDE.md](RIZIN_GUIDE.md) to reproduce analysis.

[Friendly wandering in confined spaces](FRIENDLY_WANDERING.md) traces the home-biased wander routine, action
timers, random pauses, and the different responses to walls, friendly actors and balcony edges.

## Shared movement machinery

### Actor state and steering

Both actor loops traverse the monster array at `0x61c458`, bounded by count `0x692ec8`, with stride `0x3cc`.
They skip AI states 11, 19 and 17 (removed, invisible/disabled and summoned), and actors with zero movement-speed field.
Positions and velocities are signed 16-bit fields; calculations generally use 32-bit values and 16.16 fixed-point
products before storing back to the actor. Relevant offsets are:

| Offset | Meaning |
| --- | --- |
| `0x90`, `0x92` | Body radius and height |
| `0x94` | Movement speed |
| `0x96`, `0x98`, `0x9a` | Foot position X/Y/Z |
| `0x9c`, `0x9e`, `0xa0` | Velocity X/Y/Z |
| `0xa2`, `0xa4`, `0xa6` | Heading, pitch, indoor sector |
| `0xa8`, `0xb8`, `0xba`, `0xc0` | Action duration, AI state, graphic state, action elapsed time |

When the graphic state is walking, heading drives horizontal velocity through a trigonometric lookup.
Flying actors can also receive pitch-derived vertical velocity. The ability-to-act check can disable flight.
Slow effects reduce speed; pursue/flee double it; final walking speed is capped at **1000**. Nonwalking velocity
decays by **55000/65536 ≈ 0.83923**. Low-speed thresholds use the helper `0x46baaa`, returning **400**;
the indoor idle test includes XYZ velocity, whereas the outdoor idle test uses XY and has a steep-terrain exception.
Evidence: `0x46fc98–0x46ff26`, `0x470a93–0x470e36`.

Steering is a separate layer. For example, pursuit at `0x402c2f` computes a target heading with a small ±16-angle-unit
variation, sets pursue state 6 and an action duration. Flee at `0x402ab6` chooses a heading in a randomized away-side
range, caps its action duration at 256 and sets state 7. Stand/bored at `0x40424b` stops velocity and changes action.
These traced routines do not build a route around a building. Neither movement loop contains an A* search,
waypoint queue, navmesh query, or a persistent “stuck for N seconds, teleport to safety” mechanism.
That observation is scoped to the routines traced here, not a claim that every AI routine has been reconstructed.

### Collision shape and movement interval

The shared collision state starts at `0x75e218`. Actors are represented for geometry checks by two spheres:

- Lower center: `footZ + radius + 1`.
- Upper center: `footZ + height - radius - 1`, clamped upward to the lower center when necessary.
- Both centers have the actor's X/Y, and both sphere checks are enabled in these actor paths.

Indoors, radius comes from the actor's body-radius field. Outdoors, nonflying monster types use radius **40**;
types whose flying property is set use their body radius. That shape choice tests the monster-type flying property,
while the earlier gravity/velocity decision additionally tests whether the actor can act.
Evidence: `0x46ff5d–0x470026`, `0x470e36–0x470f02`.

This is a two-sphere approximation, not a skeletal collision mesh or a complete modern swept capsule.
Other actors, decorations and the party are handled by additional radius/height tests.

`0x47078b` derives speed, a fixed-point direction, intended travel and a swept bounding box covering old and new
sphere positions. Travel uses the fixed-point timer delta at `0x51d2a0`, minus travel already consumed by collision
responses. Remaining movement below one world unit exits early. The speed is ORed with 1 before division, avoiding
a zero divisor. Actual collisions shorten the allowed distance and store a packed hit identifier.

Thus the engine tests movement along a segment and uses the earliest accepted obstruction; it does not rely solely
on “is the final point inside a wall?” The retries are collision-response iterations, **not** a fixed number of
time substeps. There is no performance measurement in this investigation.

## Outdoor movement

Entry point: **`0x470945`**. Building collision: **`0x46ead9`**. Combined terrain/building floor query:
**`0x46d6bb`**. Terrain height: **`0x483842`**.

### Terrain and bmodel floor selection

The outdoor floor query starts with interpolated terrain height, then considers building faces beneath the query's
XY footprint. It rejects models and faces by XY bounds, accepts floor-like polygon types **3 and 4**, ignores
`Untouchable` faces, performs polygon containment, and calculates either horizontal-face height or sloped-plane Z.
It records the winning building/face identifier alongside the height.

Its candidate storage holds **20 entries including terrain**. Once filled, additional qualifying faces are not
added. This is a real limit, not a statement that only 20 faces or models exist in a map.
Evidence: `0x46d700–0x46da24`, especially `0x46d980`.

For overlapping candidates it normally chooses the highest floor at or below **queryZ + 5**. If its current
candidate is above that threshold, it can replace it with a lower candidate. Equal-height candidates favor the
later encountered candidate. The final returned height is never below the terrain candidate.
Evidence: `0x46da4f–0x46db00`.

For example, while an actor at Z=0 is under a roof at Z=200, terrain at Z=0 normally remains the selected support.
The actor is not automatically lifted to the roof just because its XY is inside the building's bounds. A building
floor at Z=4, however, can replace terrain at Z=0 under the five-unit tolerance. This tolerance is **floor selection**,
not a universal stair-climbing height.

The packed floor identifier uses a model index shifted by six and a face index. The caller uses zero to classify
terrain support. It recomputes this support from geometry; this path does not maintain a separate navigation link
or permanently attach the actor to a bmodel.

### Moving from terrain onto a building

There are two cooperating mechanisms:

1. The floor query changes support as the actor moves onto a sufficiently reachable floor or along a ramp.
   If the actor's feet are below the selected support at the beginning of an update, the outdoor loop raises them
   to that floor. It zeros vertical velocity for a walker, or assigns +20 for an active flyer.
   Evidence: `0x470bd0–0x470bec`.
2. Swept bmodel collision can hit a horizontal floor face. That response zeros vertical velocity and assigns
   **the collided face's first-vertex Z + 1** to the feet. This is a distinct snap, not simply reuse of terrain Z.
   Evidence: `0x471152–0x4711d2`.

Ramps use their plane heights and normals rather than a special “enter bmodel” command. Solid vertical sides are
obstacles. A raised platform does not become reachable merely because its top has a floor classification.
Whether a particular edge can be crossed depends on the sweep geometry, radius, approach, surrounding faces and
face collision flags.

**Authored face flags matter.** The original collision path honors bit `0x20000000` (`Untouchable`), and floor
queries exclude faces carrying it. MMExtension's local description specifically identifies nonblocking vertical
stair risers as a use of this flag. This allows visual risers to be excluded while treads remain physical floors.
The bit checks are verified in the executable; this study has not audited the flag placement on every original map.
Do not confuse this flag with ordinary invisibility: an invisible collision wall need not be nonblocking.

### Moving down from a building to terrain

When the actor leaves the building-floor footprint, the floor query falls back to terrain or another lower support.
The outdoor loop considers the actor airborne when `footZ > floorZ + 1`. A nonflying airborne actor receives gravity;
floor collision and the next floor-support correction handle landing. It is not immediately forced down to terrain
at every horizontal move, which would erase falling motion.

There is **no matching 100-unit ledge-avoidance branch in this outdoor actor loop**. The indoor rule described below
must not be applied to outdoors by inference. Terrain steepness and water transitions have their own handling.

### Wall collision, sliding and getting out of a building surface

The outdoor geometry pass scans model bounds against the swept actor bounds, then checks face bounds. For each
eligible face it evaluates lower and upper sphere distances against the face plane. It only proceeds from the
front side with movement toward/not farther from the plane and a sphere-radius intersection opportunity.
Sphere/plane/polygon tests and a ray/polygon fallback supply candidate distances; the nearest updates the hit.
Evidence: `0x46eb0f–0x46ef17`, `0x475fc0`, `0x476a4d`.

After a nonfloor hit, the actor changes velocity approximately as follows, with fixed-point rounding:

`velocity += normal * max(abs(dot(velocity, normal)), speed / 8)`

The tangential component remains, while the inward component is canceled or receives a minimum outward bias.
For faces other than type 4 slopes, it additionally computes how far the feet are within the desired radius of
the plane and pushes the actor outward along the normal when that correction is positive. Heading is then aligned
with the resulting horizontal velocity. This positional correction has a turn-based-stage exception.
Evidence: `0x4711d7–0x47132a`.

All velocity components are damped by **58500/65536 ≈ 0.89264** before retrying the remaining movement.
There are at most **100 outer collision attempts per actor update**. Earlier exits occur when movement is exhausted.
Evidence: `0x471495–0x471510`, `0x47164a–0x471677`.

This is why a shallow collision can slide along a building or be pushed out of its surface. It is not a general
inside-solid recovery algorithm: the collision pass has front-side tests, and the push-out runs after a selected
collision. An actor already far behind a face, inside malformed/open geometry, or oscillating between adjacent
faces is not guaranteed to recover.

### Steep terrain and water

Terrain is triangle-interpolated heightfield support, separate from the bmodel polygon sweep. The steepness helper
`0x4836bc` selects the terrain triangle and compares its vertex-height range: **max minus min greater than 512**
is steep. This is not a 512-unit step allowance and is not equivalent to applying one normal-angle limit to all
buildings. While grounded on steep terrain, the actor gets additional gravity and a normal-based velocity response.
Evidence: `0x4837b5–0x483835`, `0x470ca3–0x470d7c`.

The actor update also retains old XY and checks old/new terrain cells after movement. A forbidden terrain/water
transition can restore old XY, rotate heading by **−32 angle units**, clear elapsed action time, and enter flee
state for **128 time units**. Flight and the water-elemental category modify this check, and existing bmodel support
bypasses the terrain-cell restriction. This is a water/terrain transition rollback, not a generic bmodel-stuck
teleport. Evidence: `0x47151b–0x47162d`, helper `0x476c22`.

### What was the outdoor maximum step height?

**No single explicit maximum was found in the original actor movement/collision chain traced here.**
The verified numbers are 40 for the ordinary outdoor collision radius, +1 contact clearance, and +5 in floor
candidate selection. Horizontal-floor contact has its own height snap. None of these can honestly be relabeled
“the actor can climb every step up to X units.”

In particular, the `0x80` at `0x471624` is written to `actor + 0xa8`: an **action duration**, not a height comparison.
A maximum for a particular staircase needs a controlled original-game geometry test, including riser flags,
tread width, actor type, approach speed and angle. A radius-sized intuition alone is insufficient evidence.

## Indoor movement

Entry point: **`0x46fb6d`**. Floor query: **`0x46d0d0`** and retry wrapper **`0x46ef67`**.
Geometry candidate/sweep pass: **`0x46e663`**. Portal traversal: **`0x46f29b`**.

### Staying in the correct room and finding a floor

The actor starts with its stored sector and queries floor support there. The floor query uses the sector's floor
list, XY face bounds, polygon containment and flat/sloped height calculation. It has **50 candidate slots**,
ignores `Untouchable` faces, and uses the same +5 Z selection tolerance when choosing among multiple floors.
With a single candidate it returns that candidate directly. With none it returns **−30000**, an invalid-floor
sentinel. There is also a special sector/portal branch inserting a **−29000** candidate; that is not ordinary
walkable ground. Evidence: `0x46d0d0–0x46d6b8`.

The wrapper retries sector lookup when the result is invalid **or greater than queryZ + 50**. It calls room-from-point
at `0x499f5f`, updates the sector and repeats the floor query. The main actor loop also has a fallback for its initial
invalid floor result; if the room/floor still cannot be resolved, it skips that actor's movement.
Evidence: `0x46ef82–0x46efcd`, `0x46fbf1–0x46fc92`.

The **50** here is a suspicious-floor/sector-recovery threshold. It is not a hard upward-step limit: after finding
a new sector, the wrapper does not repeat an independent “reject any floor over 50” rule.

### Preventing falling through floors

Several protections cooperate:

- At the beginning of the update, feet below the selected floor are raised to **floorZ + 1**.
- For horizontal floor support, downward vertical velocity is canceled after that correction.
- The lower sphere is positioned one unit clear of the feet; the upper sphere checks head clearance.
- A swept collision against a flat floor assigns its vertex height **+1** and zeros vertical velocity.
- The proposed moved position is checked for valid floor support before the position is committed.
- Portal traversal changes which sector is used for further geometry/floor tests.

Evidence: `0x46fe56–0x46feb5`, `0x46ffa2–0x46ffe8`, `0x47014a–0x4702af`, `0x47036a–0x4703eb`.
The airborne nonflying gravity branch uses an eight-times tick/gravity term; a separate below-floor steep-plane
branch uses the normal-Z threshold **45000/65536**. These are original rules, not the current OE gravity refactor.

These checks explain recovery from small numerical penetration and stale sector assignment. They cannot prove
that the engine safely handles missing floors, incorrectly wound polygons, arbitrary spawn positions or any door
movement that might trap an actor. Retaining the old position after an unresolved floor can itself leave an actor
stationary rather than repairing the level.

### Preventing unwanted drops

A walking actor that was grounded and is not flying rejects a candidate floor **more than 100 units below** its
current feet. It changes heading by **+100 or −100 angle units**, approximately 17.6 degrees, instead of committing
that move. In the original executable, the sign is selected by the low bit of the actor's X coordinate; it is not
the random boolean used by the current OE implementation.
Evidence: `0x47025f–0x47029f`.

This is specifically a grounded-walker drop guard. It does not apply to flyers or actors already above their floor.
Sky-marked floor regions have additional handling, including refusing grounded entry in relevant branches and
removing dead actors. Evidence: `0x4701db–0x470254`.

### Preventing wall tunneling and handling doorways

Collision candidates start with the current sector. Nearby portal bounds and plane proximity can add adjacent
sectors, with **at most ten sector entries including the starting sector**. The geometry pass checks their
floor/wall/ceiling lists against the swept actor bounds, then tests the two spheres against eligible faces.
It skips portal faces as solid walls and remembers the last collided face to avoid immediately treating that
same face as a fresh obstruction within the response sequence.
Evidence: `0x46e663–0x46e860`.

The portal pass finds the earliest intersected portal along the movement segment. A solid collision before it
wins. Otherwise it switches to the opposite front/back sector, remembers the portal, resets the collision-distance
sentinel and lets geometry tests run again in the new sector. This avoids relying on the next frame to discover
that the actor has moved through a doorway into another room.
Evidence: `0x46f2d5–0x46f46f`; the nearest-portal comparison is confirmed in assembly at `0x46f3db–0x46f3f8`, where
the decompiler's recovered stack-variable names are misleading.

The indoor actor loop permits **100 geometry/portal passes per movement attempt** and **100 outer response
attempts**. These are upper bounds, not the normal number executed. Most unobstructed moves leave through the
full-movement branch. It is not OE's current five-attempt outer loop.
Evidence: `0x470124–0x47012b`, `0x470719–0x470720`.

### Sliding, overlap and crowded passages

For nonflat face hits, the original indoor solver uses the same normal-based velocity correction and 58500/65536
damping described outdoors. Nonfloor/nonslope faces also receive a radius-based positional push-out and heading
realignment. It does not use the current OE contact-point sliding-plane projection or its separate horizontal and
vertical movement calls. Evidence: `0x4703f0–0x470541`, `0x4706a3–0x470717`.

Other actors are checked through the nearby-actor list. Indoor code additionally skips the actor-cylinder test
when its approximate inter-actor distance is already less than the two original body radii combined.
Evidence: `0x470086–0x470100`. This avoids treating every preexisting overlap as another incoming obstruction,
but is not a full separation solver. Corpses and several inactive states are excluded by the actor-collision helper.

After actor/party/decor collisions, response can stop, face an object, flee, or turn away from a decoration.
Multiple counted actor collisions lead to a crowd response that can stand/bore the actor. Fleeing changes heading
for a future action; it does not compute an alternate route through a doorway. These choices reduce endless
head-on pushing but can leave crowded or geometrically trapped actors stalled.

### What was the indoor maximum step height?

Again, **the original actor loop does not contain the current OE 128-unit upward-step gate**. Its floor query,
sector retry, flat-floor snap, collision radius and map flags jointly determine what it traverses. The verified
100-unit comparison is a downward-drop guard; the 50-unit comparison triggers sector recovery; +5 belongs to
floor selection. Their purposes are different.

This distinction matters when implementing a replacement: a modern explicit step-height policy can be sensible,
but it should be documented as that policy, not cited as a recovered original-MM8 constant.

### Flying actors and the Hive pit investigation (2026-09-30)

Rechecked with Rizin against the same original `MM8-Rel.exe` SHA-256 above. This is disk-code research;
the reproduction below runs OpenYAMM with the MM6 Hive map, not the original game or a patched MM8 executable.

**Original flight and actor collisions:**

- `0x46fc03–0x46fc18` combines the monster's flying property (`actor + 0x3a`) with its ability to act.
  In the walking animation, `0x46fd9f–0x46fdd1` computes vertical velocity from movement speed and pitch
  (`actor + 0xa4`). The ordinary airborne gravity branch skips an active flyer at `0x46febd–0x46fec1`.
  Flight is steered movement, not a rule to stand on the highest floor beneath the actor's XY coordinates.
- Pursuit at `0x402c2f` writes direction pitch to the actor at `0x402de8`. When this routine must compute
  its own direction, a flyer targeting a grounded party requests a target-height offset of party height,
  or body radius + 512 when its first missile field is nonzero (`0x402c50–0x402c87`). The direction helper
  adds that offset to party Z (`0x4046ef–0x40470c`). A caller-supplied direction bypasses this calculation;
  **radius + 512 is not a universal hovering altitude for every flying action**.
- Indoor flight uses the two geometry spheres and a combined XYZ sweep described above. The geometry
  pass explicitly skips portal faces at `0x46e7de–0x46e7e2`; `0x46f29b` handles crossing into the next sector.
  The support query starts from the sector's floor list (`0x46d0e7–0x46d11d`), not all horizontal faces.
  Its special horizontal-portal branch inserts an invalid-height sentinel, not the portal's plane height.
- Flyers are still subject to actor collision. The indoor loop first skips preexisting overlaps using
  approximate XYZ distance and the sum of both body radii (`0x470086–0x4700ef`). For remaining candidates,
  the call to `0x46e143` supplies **40 as the other actor's radius override** (`0x4700f4–0x4700f7`);
  the moving actor's geometry radius remains its body radius.
- That actor-cylinder helper requires a positive horizontal forward projection (`0x46e257–0x46e271`).
  Exactly vertical movement has zero projection and cannot register an actor hit through this helper.
  Diagonal movement can still collide. Multiple accepted contacts set the crowd flag at `0x470133–0x47013a`;
  the response can flee, stand/bore, or face the other actor (`0x470644–0x4706a3`). There is no general
  collision response here that makes every blocked flyer climb over its neighbors.

**OpenYAMM reproduction and correction:**

At party `(712.484, 7147.493, 0)`, yaw `1.881`, pitch `-22.345°`, the fresh Hive run logged 28 devils
repeatedly stopping with feet around Z = **−330 to −331**. Across 42,925 diagnostic movement records there
were **zero actor contacts** and **42,324 `stacked_void_ceiling` rejections**. For actor 124, the actual
vertical collision pass accepted movement from Z = −331.247 to −328.620, then the path-volume guard restored
the old position. Its support query reported true floor 3082 at Z = −768 and **portal 2316 at Z = 0** as
an overhead floor. With height 225, radius 73 and guard slack 32, that false plane explains the −330 limit.

`IndoorPathfindingBuilder` had copied `geometry.isWalkable` to `PathFacet::walkableFloor` even for horizontal
portals. Such portals were already nonblocking for sweeps, but still supplied false floor/ceiling samples to
pathfinding and the flying safety guard. The builder now excludes portals from walkable support. This repairs
the shared geometry classification without removing protection against real floors or changing crowd reactions.
No additional per-frame query is introduced.

Validation:

- A single-devil runtime test, with no other actors, reproduced the old stall at Z = **−331.248** and now
  climbs above Z = −100. This isolates the cause from crowding.
- Synthetic stacked-floor tests retain real floor support and upward/downward solid-face obstruction while
  excluding the horizontal portal. Queries against the actual Hive face now keep the Z = −768 support on
  both sides of the portal and find no false overhead floor.
- Normal game build, **257 focused unit cases / 36,539 assertions** and **5 runtime cases / 814 assertions**
  pass. Coverage includes indoor movement, path maps/planning, actor AI, outdoor path conversion and the existing
  detection/faction runtime regressions.
- A second native OpenGL/NVIDIA run from the baseline startup save records **zero flying-volume rejections**;
  the devils climb out of the pit and reach the party. The 12-second capture shows them at the upper level.
  Detailed movement logging is conditional, so its reduced record/actor count is not a population measurement
  or a frame-time benchmark.

Small validation records and the native capture are in
[`output/hive_flying_20260930`](../output/hive_flying_20260930/). Full logs and Rizin exports remain under
`/tmp/hive-flying-mm8` and the recorded isolated-run directories. The change fixes this portal defect;
OpenYAMM still uses separate horizontal and vertical flying collision passes, unlike MM8's combined sweep.

## Limits and performance shortcuts

| Mechanism | Verified limit or shortcut | Consequence |
| --- | --- | --- |
| Geometry representation | Two spheres, fixed-point movement | Cheap shape and arithmetic; not mesh-accurate contact |
| Surface broad phase | Model/face swept AABBs outdoors; sectors/face AABBs indoors | Avoid most detailed polygon tests |
| Terrain support | Heightfield triangle interpolation | No terrain triangle soup sweep in these actor loops |
| Outdoor floor collection | 20 entries, terrain included | Overlapping building-floor candidates can be omitted |
| Indoor floor collection | 50 entries | Complex overlapping support can hit a fixed limit |
| Indoor geometry neighborhood | 10 sector entries | Not an unlimited connected-sector traversal per pass |
| Nearby actor list | 30 selected actors | Actor-to-actor collision is not all-pairs for the whole map |
| Collision responses | 100 attempts; indoor portal loop also bounded at 100 | Finite work, without guaranteed successful escape |
| Early exits | Inactive/zero-speed actors, tiny movement, bounds misses, exhausted travel | Typical cost is below the worst-case bounds |

The 30-entry list is built at `0x40151a` outdoors and `0x40172e` indoors. Outdoors it distance-sorts candidates
within a radius-adjusted approximate range below 5632, then truncates to 30 (`0x4015df–0x401703`). Indoors it uses
distance, visibility/reachability-related tests, cached bits and sector membership before the final 30 cap
(`0x40192d–0x401a80`). The physical update loops still traverse the full actor array. **Thirty is not a cap of thirty
moving monsters.** The list limits the actor candidates those loops test and participates in AI selection.

The movement routines use one-sided face-plane conditions before detailed collision. That is efficient, but
explains why prevention and recovery differ: a sweep from a valid location can hit a wall, while a point already
behind that wall may not trigger the same correction. Floor selection is also not a complete test for being inside
a closed bmodel. Face winding, sector membership, collision flags and correct spawn placement remain significant.

No persistent generic depenetration search, last-known-safe-position rescue or guaranteed path-around-obstacle
solver was found in the traced movement chain. This is a static finding with the stated scope, not proof that
original gameplay was free from such bugs or that every map-specific event has been audited.

## Why current OpenEnroth must be kept separate

Local reference: `reference/OpenEnroth-git/`, commit `d98839ba8` at investigation time. No reference code was copied
into OpenYAMM. Useful reference files are `src/Engine/Graphics/Indoor.cpp`, `Outdoor.cpp`, `Collisions.cpp`,
`Collisions.h`, and `src/Engine/Objects/Actor.cpp`.

Current OE's indoor actor path includes an explicit 128-unit upward-floor check, five outer collision attempts,
contact-point sliding, separate horizontal/vertical collision calls, a post-move heading/sector update and a random
choice for the ledge turn. Several differ directly from the verified MM8 loop above. OE is useful for naming and
design comparison, but its present implementation cannot serve as a transcript of the original MM8 executable.

Similarly, `reference/mmmerge/Scripts/Global/MonsterPathfinding.lua` explicitly loads a separate Pathfinder DLL and
maintains its own path/stuck state. Those are mod facilities, not evidence of an equivalent original engine system.

## Implications for a future OpenYAMM movement audit

The subsequent [OpenYAMM comparison](CREATURE_MOVEMENT_COMPARISON.md) checks the current implementation,
records synthetic reproductions, and prioritizes concrete changes.

The original's useful ideas are a common movement sweep across terrain/building boundaries, coherent floor and
wall queries, early sector transitions, contact clearance, and explicit handling of invalid support. The original's
fixed limits and incomplete overlap recovery should not be imported solely for historical fidelity.

A comparison or fix should separately verify:

1. Terrain-to-ramp, terrain-to-tread, and bmodel-to-terrain transitions; include solid and nonblocking risers.
2. A bridge over terrain: choose the correct support below/above the actor without snapping to its roof.
3. Glancing wall hits, inside corners, narrow doorways and overlapping actors; ensure progress or stable stopping.
4. Actor feet slightly below a floor, wrong/stale sector, missing support and genuinely invalid spawn positions.
5. Grounded ledges versus flyers/falling actors; distinguish deliberate drop avoidance from collision failure.
6. Long movement intervals and moving doors; the static investigation does not establish their worst-case robustness.

This research changes documentation and annotations only. It does not modify OpenYAMM movement or the original EXE.
