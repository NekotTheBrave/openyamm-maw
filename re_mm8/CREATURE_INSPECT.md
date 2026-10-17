# Original MM8 creature inspect portrait

Investigated 2026-10-04. The original popup displays the selected animation's **front sprite at native
pixel size, shifted by a manually assigned family offset and clipped to a 128×128 square**. It runs
a separate standing/fidget/attack demonstration instead of reproducing the actor's live animation.

## Evidence and scope

- Target: `mm8_orig/MM8-Rel.exe`, preferred image base `0x400000`.
- SHA-256: `5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
- Primary evidence: local Rizin 0.10.0 disassembly. Local rz-ghidra provided address-linked pseudocode;
  conditions, field offsets, constants and draw arguments were checked against instructions.
- [Reusable Rizin annotations](mm8_rel_5326d564_creature_inspect.rz).
- [GOG executable verification](GOG_INSTALLER_VERIFICATION.md) establishes executable provenance.
- This is static research of original disk code. The original game was not run and GrayFace/MMerge
  runtime hooks were not evaluated. The data files in `mm8_orig/` are not independently authenticated
  by the executable verification. Family names below are labels from the local 198-entry `dmonlist.bin`;
  every offset and native ID range comes from the verified executable itself.
- Local OpenEnroth and MMExtension were structural leads only. MM7's popup table and peasant test
  must not be substituted for the MM8 instructions. No reference-project code was copied.
- Disposable decompilation and offset exports are in ignored `work/creature_inspect/`.

### Entry points and fields

| Original VA | Meaning / evidence |
| --- | --- |
| `0x41697c` | Right-click popup dispatcher; pauses the logic timer at `0x416a15`. |
| `0x416f6e`, `0x416f91` | Calls creature popup with actor index in ECX and window pointer in EDX. |
| `0x41e0bc` | Creature popup drawing and portrait animation controller. |
| `0x517aa0` | Persistent portrait actor copy, size `0x3cc`. |
| `0x517b0a` | Copied monster type ID, actor offset `+0x6a`. |
| `0x517b48` | Copied action duration, signed 16-bit, actor offset `+0xa8`. |
| `0x517b58` | Copied AI state, signed 16-bit, actor offset `+0xb8`. |
| `0x517b5a` | Portrait graphic state, signed 16-bit, actor offset `+0xba`. |
| `0x517b60` | Portrait elapsed time, 32-bit, actor offset `+0xc0`. |
| `0x4f5a70` | 66 signed-byte family Y offsets. |
| `0x44c26c` | Shared SFT animation-frame selector. |
| `0x424f68`, `0x425008` | Real-time tick conversion and per-frame timer update. |
| `0x51d274` | View/miscellaneous timer delta, used to advance the portrait. |
| `0x4ad16d` | Software sprite rasterizer, also used by world sprites. |
| `0x4a4f76` | Hardware sprite loader; retains original canvas and occupied-area metadata. |

Reference-derived field names are corroborated by the local
[MapMonster and sprite structures](../reference/MMExtension/Scripts/Structs/01%20common%20structs.lua).
Their MM8 absolute addresses describe the other executable; use the original addresses above here.

## Sprite selection and presentation

1. The controller chooses graphic state **0 = standing, 2 = melee attack, 7 = fidget**.
2. At `0x41e20a–0x41e225`, it reads the corresponding 16-bit animation-group ID from the **currently
   inspected map actor**, `actor + 0xc4 + 2*graphicState`, then selects the SFT frame using portrait time.
3. At `0x41e232`, it reads **SFT sprite index 0**, frame offset `+0x18`. This is the loaded front view;
   camera bearing, actor yaw, distance and the sprite currently visible in the world do not select the view.
4. The software draw uses the selected frame's palette index at `+0x34`, through `0x41f1bd` with
   shade/depth input zero and mode argument one. Hardware uses that sprite's loaded surface.
5. The popup initializes X and Y scales to **`0x10000` = 1.0** at `0x41e29d–0x41e2a8`.
   It does not read SFT scale at `+0x28`, actor body height/radius, perspective scale or Shrink strength.
6. The popup supplies zero software drawing flags. It does not propagate SFT mirror/center flags or
   world billboard effects. There is no portrait death, petrification or paralysis animation branch;
   the copied AI state has only the action-choice effect described below.

The inspected actor's name, HP and identification detail are drawn separately. Their changes do not
select a portrait animation or alter the portrait's scale/crop.

## Viewport, placement and crop

The popup window's top-left is `(wx, wy)`. At `0x41e26d–0x41e2b7`:

```text
L = wx + 13                 T = wy + 52
R = L + 128                 B = T + 128
centerX = L + 64
family = (monsterTypeId - 1) / 3
deltaY = signedByteTable[family] - 40
```

The three A/B/C types in a family share one offset. The index uses the original type ID at actor
`+0x6a`, not an MMerge ID, a name lookup or a dynamically measured head/alpha bounding box.
The border is drawn just outside the square (`L-1`, `T-1`, `R+1`, `B+1`).

Conceptually, an untrimmed native frame of width `W` and height `H` is placed at:

```text
spriteLeft = centerX - floor(W / 2)
spriteTop = T + deltaY
visibleRectangle = intersection(spriteRectangle, portraitSquare)
```

This shows native canvas rows starting near `-deltaY`, rather than fitting the complete creature into
the portrait. A 200×300 canvas with `deltaY = -60`, for example, exposes approximately canvas columns
36–163 and rows 60–187. Transparent areas still occupy their native positions. There is no per-frame
zoom or automatic crop around the head. The selected frame's dimensions/crop metadata are read each
draw, so frames retain their original placement behavior rather than being individually fitted.

### Direct3D path: original canvas versus occupied area

At `0x41e362–0x41e46b`, the selected sprite indexes 40-byte hardware records via `0x72db70`.
The record retains original canvas `W/H` at `+0x18/+0x1c`, occupied-area origin `Ax/Ay` at
`+0x10/+0x14`, and occupied-area size `Aw/Ah` at `+0x20/+0x24`. The loader at `0x4a4f76`
copies these values from HWL image metadata; texture surface dimensions are a separate quantity.

The exact pre-clip hardware bounds are:

```text
x0 = centerX + Ax - trunc(W / 2)
x1 = centerX + Ax - W + trunc(W / 2) + Aw
y0 = T + deltaY + Ay
y1 = y0 + Ah
```

For odd `W`, the original `x1-x0` is `Aw-1`, an original integer-rounding detail.
It clips these bounds against `[L,R) × [T,B)`, retaining skipped source X/Y distances when left/top
are clipped. It locks the sprite surface and copies/resamples pixels into the UI framebuffer.
Sampling maps occupied-area coordinates into the actual surface width/height by integer division
(`0x41e590–0x41e5c6`, with a second pixel-format branch at `0x41e67d–0x41e6b3`).

Consequently a hardware texture can be cropped, padded or stored at another size while still being
displayed at its **original logical size**. This is not a scale-to-fit portrait operation.
The background/transparent color is black. This path does not use the world billboard queue.

### Software path

At `0x41e6f3–0x41e729`, the square is cleared to black and the shared sprite rasterizer is called
with scale 1.0, X anchor `centerX`, Y anchor `T + H + deltaY`, and the portrait clip bounds.
The rasterizer subtracts height from the supplied bottom anchor. Its inclusive pixel convention
introduces a one-pixel difference from the conceptual placement:

```text
firstDestinationX = centerX - floor(W / 2) + 1
firstDestinationY = T + deltaY + 1
lastDestinationX  = centerX - floor(W / 2) + W
lastDestinationY  = T + deltaY + H
```

These bounds are clipped by the rasterizer at `0x4ad247–0x4ad286` using the supplied L/T/R/B.
It uses the sprite's native per-row occupied intervals and leaves palette-index-zero pixels untouched.
The popup gives depth zero, so this is a UI draw without world occlusion/depth testing.

## Animation controller and timing

The persistent copy is refreshed only when the **full monster type ID changes**, at `0x41e11d`.
On a change, `rep movsd` copies all `0x3cc` actor bytes, then overrides graphic state, elapsed time
and duration. Different actors with the same type ID share the existing demonstration state.
A/B/C changes do reset it, even though they use the same family offset.

| Transition | New graphic state | Duration |
| --- | --- | --- |
| First inspect of a different type ID | Fidget (7), elapsed zero | `128 + rand() % 256` ticks. |
| Fidget (7) or attack (2) expires | Standing (0), elapsed zero | `128 + rand() % 128` ticks. |
| Standing expires, copied AI state is 1 | Fidget (7), elapsed zero | Fidget SFT group `TotalTime << 3` ticks. |
| Standing expires, copied AI state is anything else | Melee attack (2), elapsed zero | Attack SFT group `TotalTime << 3` ticks. |

State **1** is named `Active` by MMExtension and corresponds to the moving/active actor state.
The test at `0x41e196` reads **the copied AI state at `0x517b58`**, not a peasant flag, hostility,
monster family or the current actor's live AI state. The choice retains that snapshot until a type change.

There is an actual random-condition oddity: after initially choosing fidget, the non-state-1 branch
evaluates **`rand() % 30 < 100`**, at `0x41e1ae–0x41e1b7`. The CRT random function at `0x4dc832`
returns 0–32767, so this comparison always succeeds. This is not a 30% attack chance.
The branch also consumes a discarded `rand()` result at `0x41e191` before making that test.

The normal sequence is therefore:

```text
initial fidget → standing pause → attack or fidget → standing pause → repeat
```

- `0x424f68` computes `floor(timeGetTimeMilliseconds * 128 / 1000)`: **128 ticks per second**.
- Initial fidget lasts **1 to 383/128 seconds** (approximately 1–3 seconds); standing lasts
  **1 to 255/128 seconds** (approximately 1–2 seconds). These are tick-granular random durations,
  not a choice between whole seconds.
- Expiry at `0x41e16a–0x41e173` is strictly **elapsed > duration**. At most one transition is
  performed per popup draw, and overshoot is discarded by setting elapsed to zero.
- After drawing, `0x41e7bc–0x41e7c1` adds the view/misc timer delta at `0x51d274`.
  The logic timer is paused by the right-click dispatcher; the portrait's delta still advances.
- Timer update `0x425008` caps each delta to **32 ticks = 0.25 seconds**. Long stalls therefore
  need not advance the demonstration by the complete wall-clock stall duration.
- Closing the popup stops calls that advance the portrait. No close/reset write to these controller
  fields was found. Reopening the same type resumes its retained state, without adding the closed interval.
  Switching type refreshes the copy and begins initial fidget again.
- No attack/fidget actor sound helper is called by this portrait controller. Identification speech
  elsewhere in the popup is separate from the silent demonstration.

### SFT frame timing

The selector `0x44c26c` checks the group's continuation flag and nonzero total duration. For an animated
group, it computes **`(elapsedTicks >> 3) % TotalTime`** and advances over frames while the remainder
is **greater than** the current frame's `Time`, subtracting that `Time` each step. Otherwise it returns
the initial frame. This retains the original inclusive boundary comparison.

With the verified 128-tick timer, one raw SFT time unit is **8 ticks = 1/16 second** in this path.
MMExtension's structure comment saying 1/32 second is not the observed conversion here.
The initial randomly timed fidget can loop its SFT group until its independent action timer expires;
later attack/fidget actions use the group's own total duration. Standing can also animate if its SFT
group has multiple frames. There is no reverse, random-frame, walking, ranged-attack, hit or dying
selection in this controller.

## OpenYAMM comparison before implementation

The initial working tree had three relevant differences:

- Both indoor/outdoor `monsterInspectPreviewYOffset` helpers use a table derived from **MM7/OpenEnroth**.
  The original MM8 table below has a different family order and values. A merged content system needs
  explicit original-world ID mapping; original `(id-1)/3` cannot be applied directly to merged IDs.
- Both `advanceActorInspectPreviewAnimation` implementations classify peasants and choose random
  **whole-second** initial/standing durations. Original MM8 uses the copied AI-state test and the
  tick-granular ranges above. Their absolute update timestamps also differ from advancing only on
  popup draws, especially across closing/reopening.
- In [the shared inspect renderer](../game/ui/GameplayPartyOverlayRenderer.cpp), the BMP branch
  approximately follows native-size centering/Y-offset clipping. The **atlas branch** instead calls
  [renderSpriteAtlasPreview](../game/ui/GameplayUiRuntime.cpp) with a six-unit inset; that helper fits
  and vertically centers the native canvas in the supplied rectangle and does not receive the
  inspect Y offset. Atlas portraits therefore do not reproduce the original MM8 crop.

These are findings, not implementation changes. For restored artwork, reproducing the original crop
requires native logical canvas/placement metadata and the artwork's resolution multiplier, instead
of using the bitmap/atlas pixel dimensions as the original creature size.

## Complete original MM8 Y-offset table

`raw` is the signed byte at `0x4f5a70 + (id-1)/3`; `deltaY` includes the popup's additional `-40`.
All values are native 640×480 UI pixels. Names are local descriptor labels, as scoped above.

| Native monster IDs | Family | raw | deltaY |
| --- | --- | ---: | ---: |
| 1–3 | Lizardmen Male (Peasant) | -15 | -55 |
| 4–6 | Lizardmen Warrior | -50 | -90 |
| 7–9 | Couatl (winged snake) | -90 | -130 |
| 10–12 | Pirate Warrior Male | -20 | -60 |
| 13–15 | Pirate Crossbowman (Female) | -10 | -50 |
| 16–18 | Pirate Mage Male | -5 | -45 |
| 19–21 | Dark Elf Pesant Male | -15 | -55 |
| 22–24 | Dark Elf Pesant Female | -10 | -50 |
| 25–27 | Dark Elf Warrior | -30 | -70 |
| 28–30 | Ogre Pesant Male | -30 | -70 |
| 31–33 | Ogre Warrior | -30 | -70 |
| 34–36 | Wererat Human Form Male | -30 | -70 |
| 37–39 | Wererat Ratman Form Male | -30 | -70 |
| 40–42 | Wererat Rat Form Male | -90 | -130 |
| 43–45 | Dragon Hunter | -30 | -70 |
| 46–48 | Cleric (human male) | -20 | -60 |
| 49–51 | Vampire Female Peasant | 0 | -40 |
| 52–54 | Vampire (monster) | -20 | -60 |
| 55–57 | Necromancer (monster) | -20 | -60 |
| 58–60 | Troll | -50 | -90 |
| 61–63 | Troll Pesant | 0 | -40 |
| 64–66 | Minotaur | 0 | -40 |
| 67–69 | Minotaur Pesant | -30 | -70 |
| 70–72 | Dragon | 0 | -40 |
| 73–75 | Fire Elemental | 0 | -40 |
| 76–78 | Water Elemental | -30 | -70 |
| 79–81 | Earth Elemental | 0 | -40 |
| 82–84 | Air Elemental | 10 | -30 |
| 85–87 | Dire Wolf | -90 | -130 |
| 88–90 | Thunder Lizard | -40 | -80 |
| 91–93 | Cyclops | 0 | -40 |
| 94–96 | Serpentmen | -90 | -130 |
| 97–99 | Will ' O Wisp | 40 | 0 |
| 100–102 | Centaur | 0 | -40 |
| 103–105 | Efreeti | 20 | -20 |
| 106–108 | Ogre Mage | -20 | -60 |
| 109–111 | Unicorn | -40 | -80 |
| 112–114 | Crystal Dragon | -80 | -120 |
| 115–117 | Thunderbird | -60 | -100 |
| 118–120 | Wasp Warrior | 0 | -40 |
| 121–123 | Wyvern | -40 | -80 |
| 124–126 | Gorgon | -90 | -130 |
| 127–129 | Salamander | -10 | -50 |
| 130–132 | Phoenix | -50 | -90 |
| 133–135 | Triton | -20 | -60 |
| 136–138 | Dragon Turtle | -40 | -80 |
| 139–141 | Raven Man | 0 | -40 |
| 142–144 | Malevolent Boulder | -40 | -80 |
| 145–147 | Plane Guardian | -60 | -100 |
| 148–150 | Crystal Walker | -90 | -130 |
| 151–153 | Nightmare | -20 | -60 |
| 154–156 | Ether Knight | 0 | -40 |
| 157–159 | Juggernaut | -10 | -50 |
| 160–162 | Gog | 0 | -40 |
| 163–165 | Bone Dragon | -15 | -55 |
| 166–168 | Bedraggled Human (peasant) | -30 | -70 |
| 169–171 | Naga | -30 | -70 |
| 172–174 | Skeletons Archer | 0 | -40 |
| 175–177 | Dark Dwarf | -40 | -80 |
| 178–180 | School of Fish (non-combatant) | 0 | -40 |
| 181–183 | Wimpy Pirate Warrior Male | -20 | -60 |
| 184–186 | Wimpy Pirate Crossbowman (Female) | -10 | -50 |
| 187–189 | Wimpy Pirate Mage Male | 0 | -40 |
| 190–192 | Wimpy Dragon | 0 | -40 |
| 193–195 | Human Mercenary | -20 | -60 |
| 196–198 | Wimpy Plane Guardian | 0 | -40 |

## Reproduce

Run from the repository root, using the checked executable and original-executable project:

```bash
sha256sum re_mm8/mm8_orig/MM8-Rel.exe
third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -p re_mm8/work/mm8-rel-5326d564-base.rzdb \
  -i re_mm8/mm8_rel_5326d564_creature_inspect.rz \
  -c 'pdf @ 0x41e0bc;pdf @ 0x44c26c;pdf @ 0x424f68;pdf @ 0x425008;px 66 @ 0x4f5a70'
```

For a decompiler session, load `third_party/rz-ghidra/build/core_ghidra.so` with Rizin's `L`
command and set `ghidra.sleighhome` to a directory containing matching x86 `.ldefs`, `.pspec`,
`.cspec` and compiled `.sla` files. In this session, source language metadata plus the existing
compiled x86 specifications were copied to `/tmp/mm8-inspect-sleigh`; no system installation or
original-game files were changed. The guide's former `~/.local/bin/rizin` path was absent, so the
existing build-tree executable was used.

Verification: the annotation script loads without errors, the exported offset table contains all
66 families, documentation links resolve, and the target executable retains the SHA-256 above.
No OpenYAMM build or runtime test is needed for this read-only gameplay investigation.


## Shared OpenYAMM implementation (2026-10-04)

The inspector now uses one native-logical-size placement/crop calculation for both restored atlases and
ordinary bitmaps. The mounted 128×128 portrait square and Obsidian chrome are retained. Atlas draw sizes,
crop offsets and logical canvas/pivot come from the existing sprite metadata; bitmap dimensions are divided
by the **loaded** asset scale tier. Increasing physical resolution therefore does not change the portrait's
zoom. World sprite scale, body height, facing and Shrink do not affect this preview.

Three optional presentation columns were added to the authoritative base
[`monster_descriptors.txt`](../assets_dev/engine/data_tables/monster_descriptors.txt):

| Column | Meaning | Default for newly authored content |
| --- | --- | --- |
| `InspectYOffset` | Native logical vertical shift relative to the portrait top | 0 |
| `InspectAttackChance` | Percentage chance of melee attack after standing; otherwise fidget | 100 |
| `InspectFidgetWhenMoving` | Use the initially inspected actor's moving-state snapshot to force fidget | 0 |

Imported content explicitly sets all three fields. MM8 IDs 1–198 retain the verified table above and
moving-state rule. MM7 native IDs 1–264 map to merged IDs 199–462; native peasant ranges 115–186 and
232–249 have attack chance zero. MM6 descriptor rows 1–178 map to merged IDs 475–652 and have attack chance
30. Runtime code does not identify worlds or restored artwork to decide portrait placement/animation.
The additional merge-owned MM7 animal entries 463–474 retain the ordinary -40 shift because they are
outside the original 88-family table.

The MM6/MM7 offsets and animation rules were checked with Rizin against the available local patch
installation executables (disk code, **not authenticated original GOG executables or runtime hooks**):

| Target | SHA-256 | Signed-byte table VA | Drawing adjustment |
| --- | --- | --- | --- |
| `reference/Misc-MM8Patch-2.5.7/MMPatches/Install/MM6patch/Files/MM6.exe` | `fe3adb9ed62cb300f979ef337a9c093a0115d656ebf9ed758408532699ebf4d7` | `0x4bd10c`, 60 families | Table value directly |
| `reference/Misc-MM8Patch-2.5.7/MMPatches/Install/MM7patch/Files7/mm7.exe` | `673c9805ab4a7b7440aa1be937d0a508ec029f94c7e42be750673a0f977c678c` | `0x4e2b50`, 88 families | Table value minus 40 |

MM6 uses the zero-based actor type byte divided by three (`0x41ce9b–0x41cec8`) to read the offset; its action choice at `0x41cf4a–0x41cf6a` tests `rand()%100 < 30`.
MM7 subtracts 40 at `0x41e38c` and tests its native peasant ID ranges at `0x41e417–0x41e42f`.
The shared modern popup uses the MM8-style square for all content rather than reproducing MM6's older
whole-window clip bounds. The historical software/D3D one-pixel rounding differences are not emulated.

[`ActorInspectPreviewAnimation`](../game/gameplay/ActorInspectPreviewAnimation.h) is shared by indoor
and outdoor runtimes. It uses tick-granular initial/standing pauses, strict expiry, one transition per draw,
SFT clip durations and the original eight-tick inclusive frame boundaries. The UI supplies a clock that
advances only while the inspector is visible and caps stalls at 32 ticks. Same-type reopening retains the
demonstration; a different full type ID resets it. Preview actions remain silent and separate from live AI.


### Implementation checks

- Normal OpenGL development build: `cmake --build build --target openyamm -j25` passed.
- Unit target built; 31 focused inspect, monster-parser and sprite-atlas tests passed with 4,666 assertions.
  The broader wildcard sprite sweep ran 54 tests; four unrelated tests failed on missing projectile/FX
  bitmap files, unloaded shared spell fixtures and sprite-object preload expectations. These paths do not
  call the inspect renderer/controller; no original descriptor gameplay/sprite field was changed.
- Headless map/actor queries passed for MM6 `6d07.blv`, MM7 `7out01.odm` and MM8 `out01.odm`.
- Native desktop captures used the normal OpenGL renderer, NVIDIA vendor `0x10de`, SDL Wayland;
  all three final runs exited successfully. Checked initial and later animated portraits:
  [restored MM6 monk](../output/creature_inspect_20261004/mm6/initial.png),
  [legacy MM7 peasant](../output/creature_inspect_20261004/mm7/initial.png),
  [legacy MM8 lizardman](../output/creature_inspect_20261004/mm8/initial.png).
  Later and reopened captures are alongside these images. The MM8 standing capture shows the action
  change while retaining the same square, native scale and content offset.
- Original MM8 executable hash remains unchanged.
