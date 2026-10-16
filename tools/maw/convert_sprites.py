#!/usr/bin/env python3
"""Converts sprites from a Might and Magic sprites LOD into the engine's sprite images.

The engine keeps a sprite as <name>_p<palette>.png under engine/decorations_x2, drawn with the palette its frame
table names (worlds/<world>/textures/pal<NNN>.act). This decodes the LOD's sprites (MM6-8 format: a 32-byte header,
a span table of one entry per row, then zlib-packed palette indexes) and writes them that way.

    python tools/maw/convert_sprites.py --lod <file.sprites.lod> --palette 508 --out <assets>/engine/decorations_x2
        [--scale 2] [--names na1 na2 ...]

With --sft (Merge's Data/Tables/SFT.txt), --groups and --first-id it also writes those frame groups, numbered from
--first-id, to --frames-out: the yml the engine appends to its sprite frame table from a mod's assets
(engine/rendering/sprite_frame_data_mods.yml). MAW's sword swings:

    python tools/maw/convert_sprites.py --lod <MAW5>/Data/SwordSwings.sprites.lod --palette 508
        --out mods/maw/assets/engine/decorations_x2 --sft <MAW>/Data/Tables/SFT.txt
        --groups na1g nb1g nc1g nd1g --first-id 24000
        --frames-out mods/maw/assets/engine/rendering/sprite_frame_data_mods.yml
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from convert_icons import png_bytes  # noqa: E402
from named_monster_levels import read_lod  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
SPRITE_HEADER = struct.Struct("<12sihhhhhhI")


def find_palette(palette_id: int) -> bytes:
    for world in ("mm8", "mm7", "mm6"):
        path = REPO_ROOT / "assets_dev" / "worlds" / world / "textures" / f"pal{palette_id:03d}.act"

        if path.is_file():
            return path.read_bytes()[:768]

    raise FileNotFoundError(f"no palette {palette_id} in the engine's worlds")


def decode_sprite(raw: bytes) -> tuple[int, int, list[bytes]]:
    """The sprite's width, height and rows of palette indexes, 0 for transparent; top row first."""
    name, data_size, width, height, palette, _, y_skip, _, unpacked_size = SPRITE_HEADER.unpack_from(raw, 0)
    spans = [struct.unpack_from("<hhI", raw, SPRITE_HEADER.size + row * 8) for row in range(height)]
    packed = raw[SPRITE_HEADER.size + height * 8:]
    pixels = zlib.decompress(packed) if unpacked_size and unpacked_size != len(packed) else packed
    rows = []

    for left, right, offset in spans:
        row = bytearray(width)

        if left >= 0 and right >= left:
            row[left:right + 1] = pixels[offset:offset + right - left + 1]

        rows.append(bytes(row))

    return width, height, rows


def sprite_png(width: int, height: int, rows: list[bytes], palette: bytes, scale: int) -> bytes:
    rgba_rows = []

    for row in rows:
        out = bytearray()

        for index in row:
            if index == 0:
                pixel = b"\0\0\0\0"
            else:
                pixel = bytes(palette[index * 3:index * 3 + 3]) + b"\xff"

            out += pixel * scale

        rgba_rows.extend([bytes(out)] * scale)

    return png_bytes(width * scale, height * scale, rgba_rows)


# SFT.txt's flag columns, by the engine's frame flag names (GroupStart and HasMore the engine sets itself).
SFT_FLAG_COLUMNS = {"Luminous": "Lit", "Image1": "Image1", "Center": "Center", "Fidget": "Fidget",
                    "Mirror0": "Mirror0", "Mirror1": "Mirror1", "Mirror2": "Mirror2", "Mirror3": "Mirror3",
                    "Mirror4": "Mirror4", "Mirror5": "Mirror5", "Mirror6": "Mirror6", "Mirror7": "Mirror7",
                    "Images3": "Images3", "Glow": "Glowing", "Transparent": "Transparent"}


def frame_groups_yml(sft_path: Path, groups: list[str], first_id: int) -> str:
    """The frame groups as the engine's sprite frame yml, numbered from first_id, each taking an id per frame."""
    lines = sft_path.read_text(encoding="latin-1").splitlines()
    header = lines[0].lstrip("#").split("\t")
    column = {name.strip(): index for index, name in enumerate(header)}
    rows = [line.split("\t") for line in lines[1:]]
    wanted = [group.lower() for group in groups]
    out = ["# Sprite frame groups a mod adds (tools/maw/convert_sprites.py), appended to the engine's table.", "sprites:"]
    next_id = first_id

    for group in wanted:
        start = next((i for i, row in enumerate(rows) if row[column["GroupName"]].strip().lower() == group), None)

        if start is None:
            raise ValueError(f"no frame group {group} in {sft_path}")

        frames = [rows[start]]

        for row in rows[start + 1:]:
            if row[column["GroupName"]].strip():
                break

            frames.append(row)

        out += [f"  - sprite_id: {next_id}", f'    sprite_name: "{group}"',
                f"    animation_length_raw: {sum(int(row[column['Time']]) for row in frames)}", "    frames:"]

        for row in frames:
            flags = [name for key, name in SFT_FLAG_COLUMNS.items() if row[column[key]].strip() == "x"]
            out += [f'      - texture_name: "{row[column["SpriteName"]].strip().lower()}"',
                    f"        frame_length_raw: {int(row[column['Time']])}",
                    f"        flags: [{', '.join(flags)}]",
                    f"        scale: {int(row[column['Scale']]) / 65536:g}",
                    f"        glow_radius: {int(row[column['LightRadius']])}",
                    f"        palette_id: {int(row[column['PaletteId']])}"]

        next_id += len(frames)

    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lod", required=True, help="a sprites LOD (e.g. MAW5's Data/SwordSwings.sprites.lod)")
    parser.add_argument("--palette", required=True, type=int, help="the palette the frame table draws them with")
    parser.add_argument("--out", required=True, help="folder for <name>_p<palette>.png")
    parser.add_argument("--scale", type=int, default=2, help="pixel scale (decorations_x2 holds double size)")
    parser.add_argument("--names", nargs="*", help="sprites to convert (default: all)")
    parser.add_argument("--sft", help="Merge's SFT.txt, for --frames-out")
    parser.add_argument("--groups", nargs="*", help="frame groups of --sft to write")
    parser.add_argument("--first-id", type=int, default=24000, help="the first group's sprite id (mods: 24000 on)")
    parser.add_argument("--frames-out", help="the frame groups' yml")
    args = parser.parse_args()

    if args.frames_out:
        if not (args.sft and args.groups):
            parser.error("--frames-out needs --sft and --groups")

        frames_out = Path(args.frames_out)
        frames_out.parent.mkdir(parents=True, exist_ok=True)
        frames_out.write_text(frame_groups_yml(Path(args.sft), args.groups, args.first_id), encoding="utf-8")
        print(f"{len(args.groups)} frame groups -> {frames_out}")

    lod = read_lod(Path(args.lod))
    palette = find_palette(args.palette)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    wanted = {name.lower() for name in args.names} if args.names else None

    for name, raw in sorted(lod.items()):
        if wanted is not None and name.lower() not in wanted:
            continue

        width, height, rows = decode_sprite(raw)
        target = out / f"{name.lower()}_p{args.palette}.png"
        target.write_bytes(sprite_png(width, height, rows, palette, args.scale))
        print(f"{name}: {width}x{height} -> {target.name}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
