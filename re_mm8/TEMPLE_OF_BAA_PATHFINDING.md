# Temple of Baa hallway (`BAA_1`, screenshot 2142)

Investigated 2026-09-29 using `saves/save14.oysav`, map `d04.blv`. These findings concern
OpenYAMM's collision and path search; they do not establish original MM8 behavior.

The party is at approximately `(2782.63, 7202.05, -1920)`, sector 29. The two marked actors
are already hostile and pursuing the party, but encounter different movement failures.

## Cleric of Baa: false floor-edge collision

Actor 12 starts at `(2085.705078, 5620.802734, -1408)`, radius 74, height 190, support face 616.
Its planner repeatedly finds a route. The movement controller nevertheless stays at the saved
position because the lower collision sphere reports a zero-distance hit on coplanar floor face 615.

The sphere is exactly tangent to the floor plane. Roundoff in the polygon-boundary sweep loses the
tiny lateral separation when combining it with the squared sphere radius. The resulting contact
normal points almost straight upward, so horizontal sliding makes no progress.

The sphere sweep now rejects this impossible boundary collision when the sphere is outside or
tangent to the face plane and remains parallel or separates from it. Partially overlapping spheres
still test polygon edges, preserving real doorway collisions. Regression coverage checks both
sides of a plane, genuine edge overlap, and motion into a wall.

## Venemous Spider: search grid misses usable corridor width

Actor 53 starts at `(1634.645264, 6144.69043, -1408)`, radius 127, height 256, support face 619.
Its original path requests exhaust the search with `NoRoute` and it repeatedly slides along the wall.

The passage between faces 907 and 908 spans X=1024 through X=1280: width 256. This spider's
diameter is 254, leaving valid center positions only from X=1151 through X=1153. Its original
40-unit search grid produces X=1154.645 instead. The body fits, but the grid misses the route.

Ground neighbors now adjust to nearby vertical-wall clearance by at most half a search step.
When opposing walls bound a passage, the adjustment chooses the middle of the available interval.
The planner retains the actual collision radius, rechecks floor/rise/drop limits, validates the
complete move, and updates the node key and cost for the adjusted position.

Climbable risers remain governed by floor-step validation. A valid descending transition can also
leave its upper support before gravity clears the side of that support. These distinctions preserve
the Goblinwatch chest descent regression while aligning ordinary hallway paths.

## Validation

- The actual `d04.blv` regression follows both saved actor positions through the planner and indoor
  movement controller at 128 updates per second, reaching the party's room and floor.
- A synthetic 256-unit corridor accepts the radius-127 body; a 252-unit corridor rejects it.
- All 186 selected pathfinding/collision tests passed, including the New Sorpigal balcony and
  Goblinwatch chest regressions: 23,032 assertions. The normal `openyamm` build succeeded.
- In the final isolated NVIDIA/OpenGL desktop replay, both actors descended into sector 29 and
  approached the party. The spider had one failed asynchronous request followed by a successful
  74-waypoint route; it no longer remained sliding in the hallway. The capture after approximately
  21 seconds shows both creatures at the party's stair entrance.

Run record: `/tmp/openyamm-desktop-baa-path-final-bnmy_3_z/run.json`.
Captures: `/tmp/baa-path-captures/01-initial.png` and `/tmp/baa-path-captures/after.png`.
Temporary probes and verbose diagnostic logs were kept outside the source changes.

The original save remains unchanged, SHA-256
`b1cf0173eb912e25b24eca3d1c19732fbf79f11f1c45f104411697783cf83c89`.

## BAA_2: High Priest at the stair side edge

The follow-up save is `saves/save16.oysav`. Actor 34, High Priest of Baa, starts at
`(3280.3515625, 7334.9423828125, -1920)`, radius 74, height 190, support face 1094.
The party is at `(2798.114014, 7245.755371, -1920)`.

An 18-second desktop replay reproduced continuous zero-distance hits on stair side face 1029,
despite successful path requests. The contact belongs to the upper sphere, whose center is
116 units above the feet. Its normal is approximately `(0.760559, 0.526249, -0.380279)`.

The edge-slide response subtracted the hit sphere's height offset from the contact point, but
compared that point against a center still raised by the lower sphere's radius. This mixed
coordinate systems and tilted the slide normal back into the stair edge. The response repeatedly
reported contact without actual progress.

`IndoorMovementController` now computes the slide using the actual contact sphere center and
the contact point in world coordinates. The test at the exact saved position failed before this
change with zero movement, with wall-recovery nudges disabled. It now slides within its requested
movement distance and stays on the room floor. A second subcase follows the full-height planned
route around the stairs to within 128 units of the party.

Validation: 191 focused tests passed, 33,307 assertions, including both Baa saves and the earlier
balcony/chest/cave regressions. The normal game build succeeded. In the final NVIDIA/OpenGL replay,
the priest moved on the first update, cleared the stair edge, and had no zero-progress moves among
the 63 logged movement updates. See `/tmp/baa2-path-captures/after.png`; run record:
`/tmp/openyamm-desktop-baa2-path-after-vobf7iw9/run.json`.

`BAA_2` remains unchanged, SHA-256
`2685be0895b04334d94aa430ee257c1460a4f334ecac999bef4dc0d9cb704984`.

## BAA_3: room doorway

The next follow-up uses `saves/save14.oysav`, now named `BAA_3`; it replaces the earlier
`BAA_1` slot described above. Its SHA-256 is
`cd5dc895911998ff8f67c3ddb724406768ed4cf64f743e1b25f79dc9af0fd7c6`.
The party stands at `(1025.653687, 3256.596436, -1120)`, sector 18. Actor 34 is the High Priest
at `(2058, 3153.330322, -1120)`, radius 74, height 190. Actor 53 is the Venemous Spider
at `(2111, 3368.097412, -1120)`, radius 127, height 256.

Three problems combine at the entrance:

1. **A useful route was discarded when the exact party position was unreachable.** The priest's
   search reached `(1058, 3262, -1120)` inside the room, then exhausted its node budget because
   its body could not occupy the party's exact position beside a bier. Indoor ground requests
   did not enable the planner's existing partial-route support. Recovery used a single best-point
   destination without the doorway waypoints and steered through the outside wall. Indoor ground
   actors now retain the complete partial route, as outdoor ground actors already do.
2. **The spider lined up too late.** The opening spans Y=2688 through Y=2944, leaving its 254-unit
   diameter only two units of center clearance. Its 40-unit search lattice generated Y=2808.097;
   Y=2816 fits. Wall-clearance adjustment now considers opposing doorway walls before the body
   overlaps their corners. Adjustments stay within half a search step and pass the complete
   collision/floor trace. An infeasible adjustment at one face does not suppress another valid
   corner exit; the New Sorpigal railing regression covers that distinction.
3. **Exact-height contact falsely pinned the spider.** Floor 708 is Z=-1120 and ceiling 710 is
   Z=-864, exactly 256 units apart. The controller reserved an extra ceiling unit and the planner
   traced one unit above the body. Both now accept exact contact while rejecting a 257-unit body.
   The lintels at faces 866 and 870 exposed another precision issue: adding a tiny horizontal
   edge clearance to radius squared in float precision erased that clearance. Edge/vertex sweep
   coefficients now use double precision. Boundary sliding uses the sweep's contact normal,
   rather than reconstructing it from a position displaced by collision backoff. This prevents
   artificial downward/upward slides into the opposite floor or ceiling.

The spider's planned partial route ends just inside the room before the stone bier. This is a
limit of the conservative route probes, not proof that the body cannot get around the bier:
ordinary collision sliding continued around it in the desktop replay. No body size or map
geometry was changed.

### Original MM8 height convention

Rizin inspection used the immutable original `re_mm8/mm8_orig/MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
The indoor actor update loads radius/height at `0x46ff5d`/`0x46ff64`; the upper collision center
is formed as actor Z plus height minus radius minus one at `0x46ffc4`–`0x46ffe8`.
Floor placement uses floor Z plus one at `0x4703bf`/`0x4703c1` and `0x4703e2`/`0x4703e4`.
Together these put the collision top at floor Z plus body height. This supports exact-height
contact without OpenYAMM's additional one-unit exclusion. It does not establish which route
original MM8 would choose in this MM6 map.

### BAA_3 validation

The map regression follows both original actor positions through shared path planning and indoor
movement. The priest reaches the room near the party; the spider completes the doorway approach.
Separate movement checks cross both lintels with heights 255 and 256 and reject height 257.
Synthetic narrow-passage checks include positive and negative search-grid offsets, a passage
that fits, and one that is genuinely too narrow. Edge/vertex tests preserve tiny positive
clearances and tangent normals.

All 201 selected navigation/collision/regression tests passed, with 34,755 assertions. This includes
`BAA_1`, `BAA_2`, the Goblinwatch chest/cave cases, the New Sorpigal balcony, and the new doorway
checks. The complete collision-primitives test file also passed (29 cases, 128 assertions).
The normal `openyamm` build succeeded; `git diff --check` was clean for the touched source/test files.

The final isolated desktop replay used OpenGL 3.3 on the NVIDIA adapter (`vendor=0x10de`), under
Wayland. Both actors crossed the room-side doorway plane X=1664 and continued on the room floor:
actor 34 reached X=1498.48 and actor 53 reached X=1500.87 during the observed approach.
Run record: `/tmp/openyamm-desktop-baa3-path-final-daafj0bd/run.json`.
Capture: `/tmp/baa3-path-final.png`. The original `BAA_3` save hash remains unchanged.
