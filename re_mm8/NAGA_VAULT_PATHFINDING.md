# Naga Vault hallway (`NV_1`)

Investigated 2026-09-29 using `saves/save18.oysav`, map `d18.blv` (MM8 Naga Vault).
These findings describe OpenYAMM behavior, without making a claim about the original MM8 AI.

Save SHA-256: `ffb1833a42f347756ebda03491ce9f048f1ab6bbe6bfd768b8877f4a6a2009a9`.
The party is at `(876.416504, 1406.141602, -128)`, sector 7.

The two living actors in the circled southwestern room are:

| Actor | Creature | Saved position | Radius | Height |
| --- | --- | --- | --- | --- |
| 7 | Naga | `(-280.343292, -627.092651, -144)` | 90 | 182 |
| 17 | Serpentman Elder | `(-113.231079, -832.416809, -144)` | 110 | 222 |

Both are hostile, have detected the party, and are pursuing. Their original searches exhausted
the reachable nodes in the starting room: 494 nodes for actor 7 and 448 for actor 17. Partial
routes led back to the same wall, without entering the hallway.

## The 16-unit threshold was treated as a body obstruction

The room floor is face 13 at Z=-144. The entrance floor is at Z=-128, starting at Y=-512.
Vertical face 37 closes the 16-unit difference. The narrow neck beyond the arch spans X=-640
through X=-384 and has 256 units of vertical clearance; both bodies fit.

The movement controller crosses this entrance with both original body sizes. A direct walking
probe from `(-512, -700, -144)` to `(-512, -300, -128)` nevertheless failed in the planner:

- Naga: rejected at Y=-560 against face 37.
- Elder: rejected at Y=-564 against face 37.

The additional body-clearance probes touched the raised step edge while the feet were still on
the lower floor. They treated it as an ordinary blocking wall, despite the controller's existing
rule allowing wall faces below the step-up limit.

`PathMap::traceWalkBody` now applies that same rule to its additional clearance probes. Floor
sampling still enforces the allowed rise; taller walls and overhead geometry remain blocking.
The shared planner retains the creatures' full radius and height.

After the change, the saved-position probes produce 55- and 61-waypoint partial routes through
the hallway into the main chamber, rather than ending in the starting room. The saved party
position still produces partial/no-route results around the northern stair junction; this
investigation verifies passage through the circled entrance and the adjoining hallway.

## Nearby creatures also lost pathfinding while separated by a wall

Actors 15 and 22, near the party's stair junction, exposed a separate state mismatch. Combat
correctly chose pursuit because their attack line of sight was blocked. The movement intent
copied a distance-only `inMeleeRange` flag, causing the world integration to disable pathfinding.

The shared AI now requires attack line of sight when setting that movement flag, consistent
with its combat engagement decision. A nearby target behind a wall therefore remains eligible
for route planning. This applies to both indoor and outdoor movement.

## Validation

- A real-map regression follows both saved actor positions through the planner and movement
  controller, crosses the threshold, and reaches the hallway floor. Collision-recovery nudges
  are disabled in this test.
- Pure path tests accept 16- and 40-unit steps with both body sizes and reject a 41-unit step.
- The shared AI test supplies a nearby remembered target with blocked attack line of sight and
  checks that pursuit stays active without incorrectly marking movement as in melee range.
- The existing placement assertion accepts exact non-overlapping radius clearance: X=16 for a
  radius-16 body beside the X=0 wall. Positions with actual overlap still fail that assertion.

All 250 selected navigation, collision, and shared AI regression tests passed (36,018 assertions),
including the previous Goblinwatch, New Sorpigal, and Temple of Baa cases. The normal `openyamm`
build succeeded, and the touched files passed `git diff --check`.

The isolated desktop replay shows both circled actors leaving the room and advancing into the
main chamber. Actor 7 reached `(884.974, 941.06, -128)` and actor 17 reached
`(878.369, 941.221, -128)`. The renderer was OpenGL 3.3 on NVIDIA (`vendor=0x10de`) under Wayland.

Before run: `/tmp/openyamm-desktop-nv1-path-before-r1au97gp/run.json`.
After run: `/tmp/openyamm-desktop-nv1-path-after-90dw4ae3/run.json`.
Captures: `/tmp/nv1-path-before.png` and `/tmp/nv1-path-after.png`.

Temporary probes and verbose logs remain outside the source changes. The original save is unchanged.
