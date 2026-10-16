# Melee hit sounds

Might and Magic IX surface impact sounds, copied unchanged (16-bit PCM WAV) from the MM9 world package
(`worlds/mm9/audio/weapons/`) into engine scope so every world's melee hits can use them. MM9 picks them from
`DATA/MATERIALS.txt` by struck surface and blow strength (glancing, normal, crushing); OpenYAMM keeps that
structure in `engine/data_tables/melee_hit_materials.txt`; each monster names its material in the `HitMaterial`
column of `engine/data_tables/monster_descriptors.txt`. Design: `MELEE_HIT_FEEDBACK_DESIGN.md` at the repository root.

| Files | MM9 source (glancing, normal, crushing) |
| --- | --- |
| `flesh_*.wav` | `gflesh`, `nflesh`, `cflesh` |
| `armor_leather_*.wav` | `garmorleather`, `narmorleather`, `carmorleather` |
| `armor_chain_*.wav` | `garmorchain`, `namorchain`, `carmorchain` |
| `armor_plate_*.wav` | `gamorplate`, `narmorplate`, `carmorplate` |
| `metal_solid_*.wav` | `gmetalsolid`, `nmetalsolid`, `cmetalsolid` |
| `metal_hollow_*.wav` | `gmetalhollow`, `nmetalhollow`, `cmetalhollow` |
| `wood_solid_*.wav` | `gwoodsolid`, `nwoodsolid`, `cwoodsolid` |
| `wood_hollow_*.wav` | `gwoodhollow`, `nwoodhollow`, `cwoodhollow` |
| `stone_*.wav` | `gstone`, `nstone`, `cstone` |
| `earth_*.wav` | `gearth`, `nearth`, `cearth` |
| `glass_*.wav` | `gglass`, `nglass`, `cglass` |
| `liquid_*.wav` | `gliquid`, `nliquid`, `cliquid` |

MM9's table points the metal rows at the glancing file for all three strengths; the normal and crushing metal files
ship with MM9 as well and are used here. The `namorchain` and `gamorplate` spellings are MM9's own file names.
