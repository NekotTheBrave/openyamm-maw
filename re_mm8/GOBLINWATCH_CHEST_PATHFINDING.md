# Goblinwatch chest descent (`GW_1`, screenshot 2141)

Investigated 2026-09-29 against `saves/save13.oysav`, map `6d01.blv`.

## Reproduction and cause

Actor 133, Goblin Shaman, starts at approximately `(-1146.81, 3980.99, 96)`, grounded on chest
face 1821. Its collision radius is 56 and height is 145. The chest top is at Z=96, its side platforms
at Z=32, its front tread (face 1831) at Z=16, and the room floor (face 1834) at Z=0.
The party stands near `(-1148.23, 4538.85, 0)`.

Three checks combined to prevent escape:

1. `IndoorMovementController` accepted the floor within its 100-unit ledge limit, but then rejected
   the move as `LostGroundSupport` because a drop exceeding the eight-unit snap slack starts a fall.
   Direct pursuit could therefore never begin that descent. An unmodified 25-second desktop replay
   repeatedly reported the same position and rejection.
2. `PathMap` and `PathPlanner` derived maximum descent from step-up height and radius. This goblin's
   result was 56 units, too small for either the 64-unit side descent or the 80-unit front descent.
3. Raising that limit alone still returned `NoRoute`: the walk trace interpolated diagonally from
   the upper support to the lower support over a two-unit sample. That cut through the chest's
   front wall, face 1820, although the actor can leave the top and then fall beside that wall.

## Original MM8 evidence

Read-only Rizin analysis used `re_mm8/mm8_orig/MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`, matching the verified original
described in [the Rizin guide](RIZIN_GUIDE.md). Rizin executable:
`third_party/rizin/build/binrz/rizin/rizin` (0.10.0).

At `0x47025f–0x47029f`, the indoor actor update rejects a candidate floor **strictly more than
100 units below the current feet** for grounded, nonflying walking actors. The comparison subtracts
`0x64` from feet Z and accepts equality. Rejection turns the actor by ±100 angle units and skips
the movement commit; it does not teleport the actor or compute a route.

Floor correction, airborne gravity, and floor collision are separate parts of the update
(`0x46fe56–0x46feb5`, `0x46ffa2–0x46ffe8`, `0x47036a–0x4703eb`). See
[creature movement research](CREATURE_MOVEMENT.md#preventing-unwanted-drops) for the surrounding analysis.
Thus a 96-unit descent passes MM8's drop policy. This is a verified MM8 engine rule, not a replay
of this MM6 save in the original engine. The 100-unit value is a downward limit, not a step-up limit.

## Implemented behavior

- Permit an accepted downward step to become airborne and let the movement controller apply gravity.
  Keep both floor and leading-footprint checks against missing floors and excessive drops.
- Give `PathObject` an explicit `dropHeight`. Indoor requests share the movement controller's
  100-unit limit. Outdoor requests retain their previous descent policy.
- At a descending support-face transition, trace horizontally off the upper support and then
  vertically toward the lower support. Keep ordinary traces within a sloped face and collision
  checks against other walls.

Regression coverage includes 96- and 100-unit descents, rejection at 101 units, independent upward
limits, a solid wall beyond a step, and the actual Goblinwatch chest with both direct pursuit and
planned movement through the controller. The earlier New Sorpigal balcony regression remains included.

Validation: 183 selected tests passed (18,768 assertions), and the normal `openyamm` target built
successfully. An isolated desktop replay on the NVIDIA OpenGL renderer shows the shaman on the chest
at launch and approaching the party on the room floor in the six-second capture. Run records:
`/tmp/openyamm-desktop-goblin-chest-visual-36vpnd4h`; captures:
`/tmp/goblin-chest-captures/01-initial.png` and `/tmp/goblin-chest-captures/after-six-seconds.png`.

The source save remains unchanged, SHA-256
`47cc3d9bf0823368fa31976ad7f2d98c03764216143e5170a27db4a0c5d82454`.
