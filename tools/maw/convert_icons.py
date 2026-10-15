#!/usr/bin/env python3
"""Puts the icons MAW adds into the MAW mod's asset overlay.

MAW's icon archives (Data/zMaw.icons.lod, Data/zVex.icons.lod) hold pictures its scripts give items (its crafting
gems, Data/zzAlchemy.lua: vCraft1..., vEye1...). Each one OpenYAMM does not have already is written as
<out>/engine/icons_x2/<name>.png at twice the size, as OpenYAMM keeps its icons, colour 0 of the palette
transparent as the original draws icons. Icons OpenYAMM has its own copy of are left alone.

MAW's scripts also use a few of Merge's own icons that OpenYAMM has no copy of (its bag buttons, SlChar1U...): with
--merge, a Merge install's Data/mmmerge.icons.lod, those are written too.

    python tools/maw/convert_icons.py --maw <Maw-Mod-MMMerge-main> --merge <MAW or Merge install> --out mods/maw/assets
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from named_monster_levels import read_lod  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
ICON_ARCHIVES = ('zMaw.icons.lod', 'zVex.icons.lod')
HEADER_SIZE = 48
PALETTE_SIZE = 768
MERGE_ARCHIVE = 'mmmerge.icons.lod'
# Merge's icons MAW's scripts use: the bag buttons (General/zzMaw-MultiBag.lua), up and down.
MERGE_ICONS = {f'slchar{number}{state}' for number in range(1, 6) for state in 'ud'}


def engine_icon_names() -> set[str]:
    names = set()

    for folder in [REPO_ROOT / 'assets_dev' / 'engine' / 'icons_x2'] + list(
            (REPO_ROOT / 'assets_dev' / 'worlds').glob('*/icons*')):
        if folder.is_dir():
            names.update(path.stem.lower() for path in folder.iterdir() if path.is_file())

    return names


def decode_bitmap(raw: bytes) -> tuple[str, int, int, bytes, bytes] | None:
    """An archive's 8-bit picture: (name, width, height, pixels, palette), or None if it is not one."""
    if len(raw) < HEADER_SIZE:
        return None

    name = raw[:16].split(b'\0', 1)[0].decode('latin-1')
    bitmap_size, data_size, width, height = struct.unpack_from('<IIHH', raw, 16)
    unpacked_size = struct.unpack_from('<I', raw, 40)[0]
    data = raw[HEADER_SIZE:HEADER_SIZE + data_size]
    pixels = zlib.decompress(data) if unpacked_size else data
    palette = raw[HEADER_SIZE + data_size:HEADER_SIZE + data_size + PALETTE_SIZE]

    if width == 0 or height == 0 or len(pixels) < width * height or len(palette) < PALETTE_SIZE:
        return None

    return name, width, height, pixels[:width * height], palette


def png_bytes(width: int, height: int, rgba_rows: list[bytes]) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))

    raw = b''.join(b'\0' + row for row in rgba_rows)
    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


def icon_png(width: int, height: int, pixels: bytes, palette: bytes, scale: int = 2) -> bytes:
    colours = []

    for index in range(256):
        r, g, b = palette[index * 3:index * 3 + 3]
        colours.append(bytes((r, g, b, 0 if index == 0 else 255)))

    rows = []

    for y in range(height):
        row = b''.join(colours[pixels[y * width + x]] * scale for x in range(width))
        rows.extend([row] * scale)

    return png_bytes(width * scale, height * scale, rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--maw', required=True, help='MAW checkout (has Data/zVex.icons.lod)')
    parser.add_argument('--merge', help='a Merge install (has Data/mmmerge.icons.lod), for MERGE_ICONS')
    parser.add_argument('--out', required=True, help="the mod's asset overlay, e.g. mods/maw/assets")
    args = parser.parse_args()

    have = engine_icon_names()
    out_folder = Path(args.out) / 'engine' / 'icons_x2'
    out_folder.mkdir(parents=True, exist_ok=True)
    written, kept = [], []

    sources = [(Path(args.maw) / 'Data' / archive, None) for archive in ICON_ARCHIVES]

    if args.merge:
        sources.append((Path(args.merge) / 'Data' / MERGE_ARCHIVE, MERGE_ICONS))

    for path, wanted in sources:
        if not path.is_file():
            continue

        for raw in read_lod(path).values():
            bitmap = decode_bitmap(raw)

            if bitmap is None:
                continue

            name, width, height, pixels, palette = bitmap

            if wanted is not None and name.lower() not in wanted:
                continue

            if name.lower() in have:
                kept.append(name)
                continue

            (out_folder / f'{name}.png').write_bytes(icon_png(width, height, pixels, palette))
            written.append(name)

    print(f'{len(written)} icons written to {out_folder}; {len(kept)} OpenYAMM has its own of (left alone)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
