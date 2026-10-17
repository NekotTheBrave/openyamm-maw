# Original MM8: indoor spawning and below-floor actors

Investigated 2026-09-29 after the TC_1/Tularean Caves report (`test_img/2146.png`). The original-binary
research below informed the subsequent placement fix. See [implementation and validation](#implementation-and-validation)
for the user's final scope: prevent bad fresh placements, with no save migration or new per-frame work.

## Binary and evidence

- Target: `re_mm8/mm8_orig/MM8-Rel.exe`, verified against the supplied GOG distribution.
- SHA-256, rechecked for this investigation:
  `5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
- Tool: Rizin 0.10.0, `third_party/rizin/build/binrz/rizin/rizin`, with the local rz-ghidra plugin as a
  reading aid. Important branches and stores were checked in x86 disassembly.
- Analysis project: `re_mm8/work/mm8-rel-5326d564-base.rzdb`. The EXE was opened read-only and is unchanged.
- [Address annotations](mm8_rel_5326d564_indoor_spawn_recovery.rz) are specific to this hash. Names are
  research labels, not recovered original symbols. No original or reference implementation was copied.

This examines original disk code, not GrayFace/MMMerge runtime patches. TC_1 is an OpenYAMM save of an
MM7 map; this investigation did not run that map inside original MM8. Applying the binary's conditions
to TC_1 geometry is an inference, distinguished below from verified instructions.

See also [the Rizin guide](RIZIN_GUIDE.md), [creature movement](CREATURE_MOVEMENT.md), and the
[TC_1 reproduction and geometry evidence](../output/tc1_underfloor_investigation/README.md).

## What the original actually does

### Floor selection requires a usable room

`RoomFromPoint`, at `0x499f5f`, rejects rooms outside the queried point's bounding range. The vertical
bounds test at `0x499fd0`–`0x499fea` permits **64 units below minZ and above maxZ**. It does not expand
its search to a distant floor merely because a caller would accept a large vertical correction.

`IndoorFloor`, at `0x46d0d0`, takes an explicit room and tests its floor polygons at the requested XY:

- One candidate: returns that floor, even if above query Z (`0x46d64a`–`0x46d662`).
- No candidate: returns `-30000` (`0x46d664`–`0x46d66d`).
- Multiple candidates: prefers the highest floor at/below query Z+5; if all are above, it chooses the
  lowest one (`0x46d683`–`0x46d6b2`). This is not a global highest-floor search.

`IndoorFloorWithSectorRetry`, at `0x46ef67`, first tries the supplied room. An invalid floor or a floor
above query Z+50 triggers a fresh `RoomFromPoint` lookup (`0x46ef8c`–`0x46efa3`). It writes the resulting
room back, and retries the floor query if that room is nonzero. An unresolved room returns `-30000`.
The 50-unit condition is a room-revalidation trigger, not a maximum correction or universal step height.

### Movement lifts a below-floor actor, including a flyer

`UpdateIndoorActors`, at `0x46fb6d`, performs floor resolution before collision integration:

1. Skips removed/disabled/summoned states and zero-speed actors before this movement path.
2. Queries the floor using the actor's feet and stored room (`0x46fbc9`–`0x46fbfc`).
3. If invalid, retries room/floor resolution. Failure branches to the next actor
   (`0x46fc52`–`0x46fc92`, destination `0x470774`).
4. If feet are below the resolved floor, assigns **floor+1** (`0x46fe56`–`0x46fe6b`).

The lift is not capped by actor height, and the branch does not exclude flying actors. The separate
ordinary-gravity branch checks the flying flag later (`0x46feb7`–`0x46fed4`). On a flat floor, negative
vertical velocity is also cleared after the lift. Collision integration follows the correction.

This explains the useful native rule: once support is trustworthy, a submerged flyer is still an
invalid body, not valid flight. It does **not** establish that the floor resolver can recover every
invalid actor. An actor whose room cannot be resolved is skipped instead.

### Encounter placement can omit requested monsters

`SpawnEncounter`, at `0x44dfbe`, uses RNG for encounter count/tier and a random angle for placement. The
indoor offset radius is 64 units. Its indoor candidate check:

- Resolves the marker's room (`0x44e147`).
- Resolves the offset candidate's room and requires it to match (`0x44e385`–`0x44e38d`).
- Requires a non-sentinel floor (`0x44e39d`–`0x44e3aa`).
- Requires the absolute floor/marker-Z difference to be at most 1024 (`0x44e3ac`–`0x44e3bb`).

Crucially, validation failures jump past the monster-count increment at `0x44e3ca`, but still consume
an encounter iteration at `0x44e3d0`. There is no retry-until-placed contract. The original can finish
with fewer monsters than the requested encounter count.

There is also an ordering limitation: current/home coordinates are stored at `0x44e2a8`–`0x44e2d1`,
before that iteration computes and validates its next offset. The new coordinates and corrected floor
are carried in locals to the following iteration. Therefore this routine must not be described as
atomically committing the just-validated position. Its checks also do not prove full-body clearance
or reachability through the room.

### Loading does not provide a general placement migration

The indoor load/setup function at `0x45f895` calls actor reset at `0x45fa47` unless the save-loading
skip-reset bit is set. Actor reset (`0x40928c`) copies stored home XYZ back to current XYZ for eligible
actors (`0x4092c9`–`0x4092d8`); it does not find a new floor. Fresh encounter generation is called at
`0x45fa8b`. The subsequent actor setup loop prepares sprites and actor state, without a general
floor/clearance repair pass.

Thus resetting an actor to a bad home is not a repair. These load and movement paths do not support a
claim that original MM8 guaranteed all encounter monsters would remain present and reachable.

## Application to TC_1

The prior OpenYAMM reproduction established:

| State | Feet/marker Z | Valid support at the same XY |
| --- | ---: | ---: |
| Troglodyte encounter marker | 256 | 448 |
| Wyvern encounter marker | 256 | 544 |
| Saved Queens 0 and 1 | 234.027344 | 448 |
| Saved Wyverns 2 and 3 | 302 | 448 |

The relevant room 26 has native minZ=443, making the room lookup's lower bound `443-64=379`.
The original MM8 bounding test would reject this room at all the invalid Z values above. Even supplying
room 26 to the floor-retry wrapper would not guarantee recovery: the floor is more than 50 units above
the query, causing room lookup again. Another overlapping room could change the result; merely copying
MM8's floor+1 assignment cannot solve this discovery failure.

OpenYAMM additionally exempts flyers with a stored room from recovery and limits the Queens' same-XY
correction by body height. Those restrictions block useful recovery after the correct floor is known.
A direct OpenYAMM probe placed all four saved actors at Z=448 without changing XY; all could move,
and both flyers could rise. This is a measured safe local correction for TC_1, not evidence for snapping
arbitrary actors to the highest nearby floor.

## Recommended OpenYAMM contract

This was the research recommendation. The implementation scope below supersedes its save-migration requirement.

Use native floor correction as a behavioral reference, with stronger placement and save invariants:

1. **Resolve placement before publishing an actor.** Use a placement-specific vertical search that
   enumerates rooms intersecting the requested search interval, then tests actual floor polygons.
   Leave ordinary movement's strict room query intact. Prefer validated authored room/support context;
   rank remaining candidates by a documented stable order, including distance and room/face IDs.
   Test the actor's radius and height against walls, ceilings and current mechanism geometry.
2. **Make recovery deterministic.** Try a valid correction at the same XY first, then a fixed ordered
   set of nearby placements within the intended room/area if needed. Placement retries must not consume
   random numbers. Existing encounter count/tier randomness can remain. Neither a raw invalid marker
   nor silently dropping the actor is an acceptable successful result.
3. **Preserve valid geometry relationships.** A lower room or genuine airborne position must stay
   intact. Apply floor correction to walkers and flyers only when the current body is demonstrably
   invalid and its intended support is established. Remove the body-height cap as a substitute for
   validation; do not replace it with an unrestricted highest-floor snap.
4. **Migrate older saves explicitly.** Validate and, where proven invalid, repair current position,
   stored room/support and home position before AI resumes. Keep identity, HP, inventory/loot and
   gameplay state. The migration must be versioned and idempotent; TC_1 retains all four actors.
5. **Check accessibility as well as fit.** Audit placements against the intended playable area and
   traversable connections, accounting for authored doors and mechanisms. A closed door does not make
   its monsters corrupt. A geometrically clear but isolated pocket is not a sufficient rescue target.
   Ambiguous stacked rooms or a map with no valid placement require an explicit content correction,
   not random teleportation or deletion. Surface unresolved placements as content-validation failures.

Validation should cover the two TC markers and four saved states, fresh spawning versus migrated save
loading, stacked rooms, valid flyers, low ceilings, doors and moving platforms. Assert unchanged actor
counts and gameplay state, repeatable results and migration idempotence. An audit across the mounted
MM6–MM8 indoor spawn data is needed to substantiate the no-inaccessible-monsters requirement for that
content; the original code alone cannot supply that guarantee.

## Implementation and validation

The user requested prevention for new saves, explicitly excluding repairs/migration of old saves, and asked for
no recurring runtime cost and minimal load cost. The implementation therefore changes initial placement only:

- A placement-specific floor query first preserves a resolvable room. Only a missing floor invokes the wider
  vertical-interval room search. Ordinary movement floor/room queries and AI updates are unchanged.
- Floor selection, offset attempts and local wall-overlap correction have a fixed ordering. Offset coordinates
  are quantized before validation; standing Z rounds upward so saving integer coordinates cannot lower feet into
  a slope. Encounter count/tier randomness is unchanged.
- Initial placement checks the lower, middle and upper collision spheres and polygon edges, and rejects local
  relocation through a dividing wall. This work uses the existing geometry cache, when creating actor state.
- Spawn actors receive corrected coordinates before their AI home positions are established. A missing floor
  is an explicit content error instead of a successful raw-marker fallback or a silently discarded monster.
- Three malformed source scene markers were corrected: `mm7/t01.blv` marker 35 was at Z=-608192 instead of its
  floor Z=480; `mm6/sewer.blv` markers 37 and 57 were inside solid space behind the room walls. Their scene
  positions now place them inside rooms 83 and 85, with room for the encounter offsets and rat body radius.

Support validity and full-body clearance remain separate results. The broader audit found independent native
body-size/room mismatches, particularly Lincoln robots. Those are recorded for follow-up; this change does not
shrink their collision bodies, delete them, or fail map loading for those clearance mismatches. It preserves
their resolved support when local clearance cannot be found. Consequently the validation establishes the
below-floor prevention fix, not a guarantee of full accessibility for every oversized legacy monster.

The [validation record](../output/tc1_spawn_fix/README.md) includes the 161-map floor audit, focused tests,
native movement replay and fresh-save round trip. No serialization format or existing save was changed.

## Reproduce the key binary checks

Run from the repository root after checking the full hash. No plugin or decompiler is required for
these instruction checks:

```bash
sha256sum re_mm8/mm8_orig/MM8-Rel.exe
third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -p re_mm8/work/mm8-rel-5326d564-base.rzdb \
  -i re_mm8/mm8_rel_5326d564_indoor_spawn_recovery.rz \
  -c 'pdf @ 0x44dfbe; pdf @ 0x46ef67; pd 38 @ 0x46fe56; pd 18 @ 0x499fd0; pdf @ 0x40928c'
```

Working exports are in ignored `re_mm8/work/indoor_spawn_recovery/`: `spawn_encounter.c.txt`,
`spawn_encounter.raw.asm.txt`, `indoor_floor.c.txt`, `indoor_floor_retry.c.txt`,
`indoor_actor_update.c.txt`, `indoor_load.c.txt`, `selection_and_xrefs.asm.txt`,
`floor_recovery.asm.txt` and `load_and_room.asm.txt`. Early failed decompiler experiments there are not
evidence. For the working local plugin, `ghidra.sleighhome` points at that directory's `sleigh/` staging
links to the existing local x86 language specs and compiled SLA files. Clearing inferred analysis
variables in memory avoids the plugin's overlapping-symbol error; assembly remains authoritative.
