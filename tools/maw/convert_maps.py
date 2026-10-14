#!/usr/bin/env python3
"""Puts MAW's edited maps into the MAW mod's asset overlay.

MAW ships 12 maps of its own in Data/zMaw.games.lod (geometry and the companion file with the map's starting
monsters, chests and spawn points). For each one this writes MAW's geometry file next to a scene file exported from
MAW's pair, under <out>/worlds/<world>/maps/, where the overlay puts them over the engine's copies. The world is the
one whose maps folder holds the map in the engine's assets.

An outdoor map's baked lighting names the geometry file it was baked for by a hash of the whole file, spawn points
included. Where MAW's map lights the same as the engine's (same terrain, models and decorations; MAW moved spawn
points or set texture animation flags), the engine's bake is copied with the hash of MAW's file. Otherwise the map
needs baking for MAW's geometry (tools/lighting/bake_outdoor.py), and is reported.

    python tools/maw/convert_maps.py --maw <Maw-Mod-MMMerge-main> --out mods/maw/assets [--maps d08 out05]
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from named_monster_levels import read_lod, unpack_map_file  # noqa: E402
from export_outdoor_scene_yml import parse_outdoor_odm  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
ENGINE_WORLDS = REPO_ROOT / "assets_dev" / "worlds"
WORLD_IDS = ("mm6", "mm7", "mm8")
TERRAIN_OFFSET = 0xB4
TERRAIN_BYTES = 3 * 128 * 128 + 0x20000 + 0x10000
LIGHTING_GEOMETRY_HASH_OFFSET = 16
# Face attribute bits that do not change how a face is lit: the texture's animation (texture frame table).
UNLIT_FACE_ATTRIBUTES = 0x4000


def fnv1a64(data: bytes) -> int:
    value = 14695981039346656037

    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF

    return value


def restamp_dependency(baked: bytearray, virtual_path: str, source: Path) -> None:
    """A bake lists the files it was baked from (length, hash, path); the game calls it stale when one no longer
    hashes the same. Gives the record for virtual_path the hash of source."""
    path_bytes = virtual_path.encode("utf-8")
    at = baked.find(path_bytes)

    while at != -1:
        if at >= 12 and struct.unpack_from("<I", baked, at - 12)[0] == len(path_bytes):
            struct.pack_into("<Q", baked, at - 8, fnv1a64(source.read_bytes()))
            return

        at = baked.find(path_bytes, at + 1)


def engine_world(map_file: str) -> str | None:
    for world_id in WORLD_IDS:
        if (ENGINE_WORLDS / world_id / "maps" / map_file).is_file():
            return world_id

    return None


def outdoor_geometry_changed(engine_odm: Path, maw_odm: Path) -> list[str]:
    """What of an outdoor map's lit geometry (terrain, models, decorations) MAW changed; a face's cached bitmap index
    and its texture animation are not it."""
    changes = []
    engine_bytes = engine_odm.read_bytes()
    maw_bytes = maw_odm.read_bytes()
    terrain = slice(TERRAIN_OFFSET, TERRAIN_OFFSET + TERRAIN_BYTES)

    if engine_bytes[terrain] != maw_bytes[terrain]:
        changes.append("terrain")

    def models(path: Path) -> list[object]:
        result = []

        for model in parse_outdoor_odm(path)["bmodels"]:
            model = dict(model)
            faces = []

            for face in model["faces"]:
                face = {k: v for k, v in face.items() if k != "bitmap_index"}
                face["attributes"] = face["attributes"] & ~UNLIT_FACE_ATTRIBUTES
                faces.append(face)

            model["faces"] = faces
            result.append(model)

        return result

    if models(engine_odm) != models(maw_odm):
        changes.append("models")

    if parse_outdoor_odm(engine_odm)["entities"] != parse_outdoor_odm(maw_odm)["entities"]:
        changes.append("decorations")

    return changes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--maw", required=True, help="MAW checkout (has Data/zMaw.games.lod)")
    parser.add_argument("--out", required=True, help="the mod's asset overlay, e.g. mods/maw/assets")
    parser.add_argument("--maps", nargs="*", help="map names to convert (default: every map MAW ships)")
    args = parser.parse_args()

    lod = read_lod(Path(args.maw) / "Data" / "zMaw.games.lod")
    stems = sorted({name.rsplit(".", 1)[0] for name in lod if name.endswith((".odm", ".blv"))})

    if args.maps:
        wanted = {name.lower() for name in args.maps}
        stems = [stem for stem in stems if stem in wanted]

    out_root = Path(args.out)
    work_dir = tempfile.TemporaryDirectory()
    failures = 0

    for stem in stems:
        outdoor = f"{stem}.odm" in lod
        geometry = f"{stem}.odm" if outdoor else f"{stem}.blv"
        companion = f"{stem}.ddm" if outdoor else f"{stem}.dlv"
        world_id = engine_world(geometry)

        if world_id is None or companion not in lod:
            print(f"{stem}: skipped ({'not an engine map' if world_id is None else 'no companion file'})")
            failures += 1
            continue

        maps_dir = out_root / "worlds" / world_id / "maps"
        maps_dir.mkdir(parents=True, exist_ok=True)
        companion_dir = Path(work_dir.name)
        geometry_path = maps_dir / geometry
        companion_path = companion_dir / companion
        geometry_path.write_bytes(unpack_map_file(lod[geometry]))
        companion_path.write_bytes(unpack_map_file(lod[companion]))

        exporter = "export_outdoor_scene_yml.py" if outdoor else "export_indoor_scene_yml.py"
        command = [sys.executable, str(REPO_ROOT / "tools" / exporter)]
        command += ["--odm", str(geometry_path), "--ddm", str(companion_path)] if outdoor else [
            "--blv", str(geometry_path), "--dlv", str(companion_path), "--world", world_id]
        command += ["--output", str(maps_dir / f"{stem}.scene.yml")]
        result = subprocess.run(command, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"{stem}: scene export failed: {result.stderr.strip() or result.stdout.strip()}")
            failures += 1
            continue

        note = ""

        if outdoor:
            engine_maps = ENGINE_WORLDS / world_id / "maps"
            changed = outdoor_geometry_changed(engine_maps / geometry, geometry_path)
            lighting = engine_maps / f"{stem}.lighting"

            if changed:
                note = f"; needs a lighting bake ({', '.join(changed)} changed)"
                failures += 1
            elif lighting.is_file():
                baked = bytearray(lighting.read_bytes())
                struct.pack_into("<Q", baked, LIGHTING_GEOMETRY_HASH_OFFSET, fnv1a64(geometry_path.read_bytes()))
                restamp_dependency(baked, f"worlds/{world_id}/maps/{geometry}", geometry_path)
                restamp_dependency(baked, f"worlds/{world_id}/maps/{stem}.scene.yml", maps_dir / f"{stem}.scene.yml")
                (maps_dir / lighting.name).write_bytes(bytes(baked))
                note = "; the engine's lighting, for MAW's file"

        print(f"{stem}: {world_id}/maps/{geometry} and {stem}.scene.yml{note}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
