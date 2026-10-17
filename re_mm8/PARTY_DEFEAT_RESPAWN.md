# Original MM8 party-defeat destination

Checked on 2026-10-06 with Rizin 0.10.0 against the GOG-verified `mm8_orig/MM8-Rel.exe`:

```text
SHA-256: 5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b
Preferred image base: 0x400000
```

**Original MM8 selects the resurrection map from QBit 93, set on the first visit to Ravenshore.**
Before that visit, defeat returns the party to Dagger Wound Island. Afterward, defeat returns it to Ravenshore,
including when it dies back on Dagger Wound Island or inside its dungeons. Delivering Dadeross' letter (QBit 4)
is not the condition. The current map is compared only after the destination has been selected, to decide
whether the engine needs to load another map.

| QBit 93 | Destination | Native XYZ | Native direction (2048 units per turn) |
| --- | --- | --- | --- |
| Clear | Dagger Wound Island, `out01.odm` | `(3766, 7649, 545)` | 1248 |
| Set | Ravenshore, `out02.odm` | `(8315, -15562, 277)` | 0 |

## Binary evidence

The destination branch lies in the main game loop beginning at `0x46261d`.

- `0x462baf`: loads `DX = 0x5d` (93).
- `0x462bb3`: loads `ECX = 0xb2150f`, the original executable's party QBit storage.
- `0x462bb8`: calls the bit-test helper at `0x44836d`.
- `0x462bbd` / `0x462bc0`: tests the returned mask and jumps to `0x462bed` when the bit is clear.
- Set branch, `0x462bc2` through `0x462be6`: writes Ravenshore XYZ/direction and selects the `out02.odm`
  string at `0x4fb504`.
- Clear branch, `0x462bed` through `0x462c15`: writes Dagger Wound XYZ/direction and selects the `out01.odm`
  string at `0x4fb1b4`.
- `0x462c1f`: copies the chosen filename into a local buffer.
- `0x462c30` / `0x462c53`: compares that chosen filename with the current map name at `0x6f389c`;
  equality skips the map-change block via `0x462c5c`.

The helper at `0x44836d` subtracts one from the signed 16-bit bit number, computes its byte index and
MSB-first mask, reads the byte and returns the masked value. For 93, it reads byte `0xb2151a` with mask `0x08`.
Both branch instructions and coordinate constants were checked directly in the selected executable, without
depending on the cached project's names or decompiler output.

## Script and structure interpretation

[GrayFace's readable MM8 Ravenshore event listing](../reference/mmext-scripts/Decompiled_Scripts/mm8/txt/Out02.txt)
has event 1 as `OnLoadMap`: if QBit 93 is clear, it sets it. Its comment identifies this as entering
`out02.odm` and changing the starting place. The same event is retained in
[OpenYAMM's generated Ravenshore script](../assets_dev/worlds/mm8/events/maps/out02.lua).
This establishes the gameplay meaning of the numbered bit observed in the executable.

[MMExtension's party structure reference](../reference/MMExtension/Scripts/Structs/00%20structs.lua)
identifies party coordinates, direction and QBits. Its patched-MM8 absolute addresses differ from this original
executable by `0x100` for these globals: the original coordinate base is `0xb21454`, and its QBit base is
`0xb2150f`. The original addresses above were located independently; patched hook addresses were not reused.

## OpenYAMM comparison

The original investigation found that [the shared resurrection resolver](../game/app/GameApplication.cpp) used
current-map/area matching for MM8, following the local MMerge death-map reference. That allowed resurrection on
Dagger Wound after visiting Ravenshore, or resurrection in Ravenshore before visiting it when dying elsewhere.

The 2026-10-06 follow-up fixes MM8 destination selection to use QBit 93. The existing MM7 contest check (QBit 519)
and other worlds' rules are retained. The focused party-defeat regression checks the Ravenshore on-load flag,
Dagger Wound and its dungeon after visiting Ravenshore, Alvar before/after the visit, and MM6/MM7 destinations.
Landing coordinates remain owned by the merged
[continent settings](../assets_dev/engine/data_tables/continent_settings.txt), which differ from the native
constants listed above.

## Reproduce

From the repository root, using the existing source-tree Rizin build when it is absent from `PATH`:

```bash
sha256sum re_mm8/mm8_orig/MM8-Rel.exe
third_party/rizin/build/binrz/rizin/rizin -v
third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -c 'pd 24 @ 0x462baf;pd 19 @ 0x44836d;pd 9 @ 0x462c30;pd 8 @ 0x462c53;ps @ 0x4fb1b4;ps @ 0x4fb504' \
  re_mm8/mm8_orig/MM8-Rel.exe
```

Reusable flags/comments are in [mm8_rel_5326d564_party_defeat.rz](mm8_rel_5326d564_party_defeat.rz).
Raw focused output is retained under `work/party_defeat/`. The hash was checked before and after analysis;
`mm8_orig/` remains unchanged. This is static original-executable and script evidence; no original-game runtime
session or GrayFace/MMerge runtime-patch behavior was tested.
