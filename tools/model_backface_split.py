"""Back-face culling of hidden triangles, applied in place to uncooked runtime GLBs in assets_dev.

Until the switch to cooked models (ASSET_COOKING_PLAN.md section 12), assets_dev holds uncooked copies of the model
sources in assets_source. This runs the model cook's hidden-back-face pass (tools/cook_model_geometry.py; ray tests in
openyamm_model_texture_cook --geometry) on such copies so they render as cheaply as cooked ones. Never run it on a
source in assets_source or on a project export: the cook applies the pass itself, from the unprocessed source.

A processed file is marked `asset.extras.openyamm_backface_split` and is skipped later (the cook recognises the mark).
Files are rewritten in place, so hard links stay linked.

    python3 tools/model_backface_split.py [--dry-run] [--report report.json] model.glb...
(Also accepted under Blender's Python, as the first version of this tool required.)
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cook_model_geometry  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942


def read_glb(path):
    data = path.read_bytes()
    magic, version, _length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC or version != 2:
        raise ValueError(f"{path}: not a glTF 2 binary")
    document, binary, offset = None, b"", 12
    while offset < len(data):
        size, kind = struct.unpack_from("<II", data, offset)
        chunk = data[offset + 8:offset + 8 + size]
        if kind == CHUNK_JSON:
            document = json.loads(chunk)
        elif kind == CHUNK_BIN:
            binary = chunk
        offset += 8 + size
    return document, binary


def write_glb_in_place(path, document, binary):
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = bytes(binary) + b"\0" * (-len(binary) % 4)
    blob = struct.pack("<III", GLB_MAGIC, 2, 12 + 8 + len(text) + 8 + len(binary))
    blob += struct.pack("<II", len(text), CHUNK_JSON) + text + struct.pack("<II", len(binary), CHUNK_BIN) + binary
    with open(path, "r+b") as handle:   # the same inode, so hard links stay linked
        handle.write(blob)
        handle.truncate()


def process(path, cooker, dry_run):
    document, binary = read_glb(path)
    if document.get("asset", {}).get("extras", {}).get(cook_model_geometry.LEGACY_MARKER):
        return {"file": str(path), "skipped": "already processed"}
    writer = cook_model_geometry.Writer(document, binary)
    checked, culled, back_copies = cook_model_geometry.cull_hidden_back_faces(document, binary, writer, cooker)
    report = {"file": str(path), "triangles": checked, "culled": culled, "back_copies": back_copies}
    if checked and not dry_run:
        document.setdefault("asset", {}).setdefault("extras", {})[cook_model_geometry.LEGACY_MARKER] = {
            "rays": cook_model_geometry.RAY_COUNT, "max_ray_angle_degrees": cook_model_geometry.MAX_RAY_ANGLE_DEGREES,
            "culled_triangles": culled, "back_copies": back_copies, "checked_triangles": checked}
        write_glb_in_place(path, document, writer.compact())
    return report


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    parser = argparse.ArgumentParser(description="Back-face cull hidden triangles of uncooked runtime GLBs in place.")
    parser.add_argument("models", nargs="+", type=Path)
    parser.add_argument("--dry-run", action="store_true", help="classify and report without writing")
    parser.add_argument("--report", type=Path, help="write the per-file results as JSON")
    parser.add_argument("--cooker", type=Path, default=ROOT / "build/game/openyamm_model_texture_cook",
                        help="openyamm_model_texture_cook (cmake --build build --target openyamm_model_texture_cook)")
    args = parser.parse_args(argv)
    cooker = args.cooker.resolve(strict=True)
    reports = []
    for path in args.models:
        if "assets_source" in path.resolve().parts:
            raise SystemExit(f"{path}: a source; the cook applies this pass itself")
        started = time.time()
        report = process(path, cooker, args.dry_run)
        report["seconds"] = round(time.time() - started, 1)
        reports.append(report)
        if "skipped" in report:
            print(f"{path}: skipped ({report['skipped']})", flush=True)
        else:
            print(f"{path}: culled {report['culled']} of {report['triangles']} checked triangles, "
                  f"{report['back_copies']} back copies; {report['seconds']} s", flush=True)
    if args.report:
        args.report.write_text(json.dumps(reports, indent=1))


main()
