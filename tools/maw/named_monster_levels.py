#!/usr/bin/env python3
"""Read the level stored on each named monster out of a Merge install's map archives.

A monster placed in a map carries a whole record of its own in the map's state file (.ddm, .dlv). OpenYAMM keeps
only part of it and fills the rest from the monster table. MAW takes the level of a named monster from that record
and not from the table, so the port needs the stored levels. This writes them as a Lua table for the port:

    python named_monster_levels.py <install>/Data <fork>/assets_dev/worlds <output.lua>

The actor list of a state file is found by matching it against the scene file OpenYAMM already has for the map
(same number of actors, same monster kinds in the same order), so nothing here depends on the rest of the layout.
"""

from __future__ import annotations

import re
import struct
import sys
import zlib
from pathlib import Path

ACTOR_RECORD_SIZE = 0x3CC
ACTOR_LEVEL_OFFSET = 0x34
ACTOR_MONSTER_INFO_ID_OFFSET = 0x6A
ACTOR_UNIQUE_NAME_OFFSET = 0x3BC
PACKED_SIGNATURE = (0x16741, 0x6969766D)


def read_lod(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()

    if data[:4] != b"LOD\0":
        return {}

    version = data[4:84].split(b"\0", 1)[0]
    table_offset, _, _, count = struct.unpack_from("<IIIH", data, 0x110)
    entry_size = 0x4C if version == b"MMVIII" else 0x20
    name_size = entry_size - 0x0C if version == b"MMVIII" else 0x10
    files: dict[str, bytes] = {}

    for index in range(count):
        entry = table_offset + index * entry_size
        name = data[entry:entry + name_size].split(b"\0", 1)[0].decode("latin-1").lower()
        offset, size = struct.unpack_from("<II", data, entry + name_size)
        files[name] = data[table_offset + offset:table_offset + offset + size]

    return files


def unpack_map_file(raw: bytes) -> bytes:
    if len(raw) >= 16 and struct.unpack_from("<II", raw, 0) == PACKED_SIGNATURE:
        packed_size, unpacked_size = struct.unpack_from("<II", raw, 8)
        body = raw[16:16 + packed_size]
        return zlib.decompress(body) if unpacked_size != 0 else body

    if len(raw) >= 8:
        packed_size, unpacked_size = struct.unpack_from("<II", raw, 0)

        if packed_size == len(raw) - 8:
            body = raw[8:]
            return zlib.decompress(body) if unpacked_size != 0 else body

    return raw


def scene_actors(scene_path: Path) -> list[tuple[int, int]]:
    """(monster kind, unique name) of each actor of a scene file, in order."""
    text = scene_path.read_text(encoding="utf-8")
    actors: list[tuple[int, int]] = []

    for block in re.split(r"^\s*- actor_index: \d+\s*$", text, flags=re.MULTILINE)[1:]:
        kind = re.search(r"^\s*monster_info_id: (-?\d+)", block, flags=re.MULTILINE)
        unique = re.search(r"^\s*unique_name_index: (-?\d+)", block, flags=re.MULTILINE)

        if kind is None or unique is None:
            break

        actors.append((int(kind.group(1)), int(unique.group(1))))

    return actors


def find_actor_list(data: bytes, actors: list[tuple[int, int]]) -> int | None:
    count = len(actors)
    needle = struct.pack("<I", count)
    start = data.find(needle)

    while start != -1:
        base = start + 4

        if base + count * ACTOR_RECORD_SIZE <= len(data) and all(
            struct.unpack_from("<h", data, base + index * ACTOR_RECORD_SIZE + ACTOR_MONSTER_INFO_ID_OFFSET)[0] == kind
            and struct.unpack_from("<i", data, base + index * ACTOR_RECORD_SIZE + ACTOR_UNIQUE_NAME_OFFSET)[0] == unique
            for index, (kind, unique) in enumerate(actors)
        ):
            return base

        start = data.find(needle, start + 1)

    return None


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2

    data_dir, worlds_dir, output_path = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    state_files: dict[str, bytes] = {}

    # Later archives replace the files of earlier ones, as in the game.
    for lod_path in sorted(data_dir.glob("*games.lod"), key=lambda path: path.name.lower(), reverse=True):
        for name, raw in read_lod(lod_path).items():
            if name.endswith((".ddm", ".dlv")):
                state_files[name] = raw

    levels: dict[str, dict[int, int]] = {}
    unmatched: list[str] = []

    for scene_path in sorted(worlds_dir.glob("*/maps/*.scene.yml")):
        stem = scene_path.name[:-len(".scene.yml")].lower()
        actors = scene_actors(scene_path)

        if not any(unique > 0 for _, unique in actors):
            continue

        raw = state_files.get(stem + ".ddm") or state_files.get(stem + ".dlv")
        data = unpack_map_file(raw) if raw else b""
        base = find_actor_list(data, actors) if data else None

        if base is None:
            unmatched.append(stem)
            continue

        map_name = stem + (".odm" if stem + ".ddm" in state_files else ".blv")
        levels[map_name] = {
            index: data[base + index * ACTOR_RECORD_SIZE + ACTOR_LEVEL_OFFSET]
            for index, (_, unique) in enumerate(actors) if unique > 0
        }

    lines = [
        "-- The level stored on each named monster in its map, by map file and monster index.",
        "-- Written by tools/maw/named_monster_levels.py from a Merge install; do not edit by hand.",
        "return {",
    ]

    for map_name in sorted(levels):
        entries = ", ".join(f"[{index}] = {level}" for index, level in sorted(levels[map_name].items()))
        lines.append(f'\t["{map_name}"] = {{{entries}}},')

    lines.append("}")
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(levels)} maps written, {sum(len(v) for v in levels.values())} named monsters")

    if unmatched:
        print("no matching actor list for: " + ", ".join(unmatched))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
