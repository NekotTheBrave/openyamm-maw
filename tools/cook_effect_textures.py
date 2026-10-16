#!/usr/bin/env python3
"""Cook authored effect sprites into GPU block-compressed atlases (.oytex), one per sprite.

An effect sprite descriptor (assets_dev/engine/effects/authored/sprites/<name>.yml) that names an `atlas` is cooked:
its frames, assets_source/engine/effects/authored/textures/<name>_NN.png in frame order, are packed row by row in
`atlasColumns` columns as the effect renderer would pack them at load, edge-bled like its Billboard upload, without
mips, and encoded per profile:

  desktop  BC7       assets_dev/engine/effects/authored/textures/<name>.oytex
  android  ASTC 4x4  assets_cooked/android/engine/effects/authored/textures/<name>.oytex

The cooked files are committed (their sources are local: tools/fx/generate_melee_effects.py writes them), so a fresh
checkout runs and packages without them; the packager swaps in the android files for that profile.

  tools/cook_effect_textures.py                    cook new and changed sprites for both profiles, remove orphans
  tools/cook_effect_textures.py --profile desktop  one profile
  tools/cook_effect_textures.py --plan             what would be cooked; writes nothing
  tools/cook_effect_textures.py --verify           fail on a missing, invalid, stale (when the sources are present)
                                                   or orphaned cooked atlas
  tools/cook_effect_textures.py --force            re-cook everything
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import yaml
from PIL import Image

from cook_surface_textures import file_stat, header_source_hash, sha256

ROOT = Path(__file__).resolve().parent.parent
SPRITES = "engine/effects/authored/sprites"
TEXTURES = "engine/effects/authored/textures"
PROFILES = {"desktop": ("bc7", ROOT / "assets_dev"), "android": ("astc_4x4", ROOT / "assets_cooked/android")}
# Bump when the cooker's output for the same inputs changes.
COOK_REVISION = 1


def sprites(assets):
    """[(name, descriptor)] of the sprites that are cooked as atlases."""
    result = []
    for path in sorted((assets / SPRITES).glob("*.yml")):
        descriptor = yaml.safe_load(path.read_text())
        if descriptor.get("atlas"):
            result.append((path.stem, descriptor))
    return result


def frame_sources(name):
    return sorted((ROOT / "assets_source" / TEXTURES).glob(f"{name}_[0-9][0-9].png"))


def combined_hash(sources):
    digest = hashlib.sha256()
    for source in sources:
        digest.update(sha256(source).encode())
    return digest.hexdigest()


def check_sources(name, descriptor, sources):
    if len(sources) != descriptor["frameCount"]:
        raise SystemExit(f"{name}: {len(sources)} source frames in assets_source/{TEXTURES}, "
                         f"descriptor names {descriptor['frameCount']}; run tools/fx/generate_melee_effects.py")
    size = (descriptor["frameWidth"], descriptor["frameHeight"])
    for source in sources:
        if Image.open(source).size != size:
            raise SystemExit(f"{source}: not {size[0]}x{size[1]} as the descriptor says")


def receipts_path(profile):
    return ROOT / "assets_source/.cook-cache/effect_textures" / f"{profile}.json"


def cook(profile, cooker, force, plan, assets):
    codec, output_root = PROFILES[profile]
    receipts_file = receipts_path(profile)
    receipts = {} if force or not receipts_file.is_file() else json.loads(receipts_file.read_text())
    pending, expected = [], set()
    for name, descriptor in sprites(assets):
        output = output_root / TEXTURES / f"{name}.oytex"
        expected.add(output)
        sources = frame_sources(name)
        check_sources(name, descriptor, sources)
        settings = {"codec": codec, "revision": COOK_REVISION, "columns": descriptor["atlasColumns"],
                    "sources": [source.name for source in sources]}
        stats = [file_stat(source) for source in sources]
        receipt = receipts.get(name)
        if receipt and output.is_file() and receipt["settings"] == settings:
            if receipt["stat"] == [stats, file_stat(output)]:
                continue
            if receipt["source_sha256"] == combined_hash(sources) and receipt["output"] == sha256(output):
                receipt["stat"] = [stats, file_stat(output)]
                continue
        pending.append((name, descriptor, output, sources, settings))
    managed = set((output_root / TEXTURES).glob("*.oytex"))
    orphans = sorted(managed - expected)
    print(f"{profile}: {len(expected)} effect atlases, {len(pending)} to cook, {len(orphans)} orphaned", flush=True)
    if plan:
        for name, _, output, sources, _ in pending:
            print(f"  {output.relative_to(ROOT)} from {len(sources)} frame(s)")
        return
    for path in orphans:
        path.unlink()
        receipts.pop(path.stem, None)
    failed = 0
    if pending:
        with tempfile.TemporaryDirectory() as scratch:
            lines = []
            for name, descriptor, output, sources, _ in pending:
                listing = Path(scratch) / f"{name}.frames"
                listing.write_text(f"columns\t{descriptor['atlasColumns']}\n" + "".join(f"{path}\n" for path in sources))
                lines.append("\t".join((str(listing), str(output), codec, "linear", "0", "0", "8192", "0",
                                        combined_hash(sources)[:16], "effect_atlas")) + "\n")
            job_file = Path(scratch) / "jobs.tsv"
            job_file.write_text("".join(lines))
            result = subprocess.run([str(cooker), str(job_file)], text=True, capture_output=True)
        sys.stderr.write(result.stderr)
        done = {Path(line.split("\t")[1]) for line in result.stdout.splitlines() if line.startswith("cooked\t")}
        for name, _, output, sources, settings in pending:
            if output not in done:
                failed += 1
                continue
            output.chmod(0o644)
            receipts[name] = {"settings": settings, "source_sha256": combined_hash(sources), "output": sha256(output),
                              "stat": [[file_stat(source) for source in sources], file_stat(output)]}
    receipts_file.parent.mkdir(parents=True, exist_ok=True)
    temporary = receipts_file.with_suffix(".tmp")
    temporary.write_text(json.dumps(receipts, indent=1, sort_keys=True) + "\n")
    os.replace(temporary, receipts_file)
    if failed:
        raise SystemExit(f"{failed} of {len(pending)} effect atlases failed to cook for {profile}")


def verify(profile, assets):
    _, output_root = PROFILES[profile]
    problems, expected, unchecked = [], set(), 0
    for name, descriptor in sprites(assets):
        output = output_root / TEXTURES / f"{name}.oytex"
        expected.add(output)
        if not output.is_file():
            problems.append(f"missing {output.relative_to(ROOT)}")
            continue
        try:
            source_hash = header_source_hash(output)
        except ValueError as error:
            problems.append(f"invalid {output.relative_to(ROOT)}: {error}")
            continue
        sources = frame_sources(name)
        if not sources:
            unchecked += 1
        elif len(sources) != descriptor["frameCount"] or source_hash != int(combined_hash(sources)[:16], 16):
            problems.append(f"stale {output.relative_to(ROOT)}")
    problems += [f"orphaned {path.relative_to(ROOT)}"
                 for path in sorted(set((output_root / TEXTURES).glob("*.oytex")) - expected)]
    for problem in problems[:40]:
        print(problem)
    note = f" ({unchecked} without local sources, freshness not checked)" if unchecked else ""
    print(f"{profile}: {len(expected)} effect atlases, {len(problems)} problems{note}")
    return not problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--profile", choices=sorted(PROFILES), action="append")
    parser.add_argument("--assets", type=Path, default=ROOT / "assets_dev", help="where the sprite descriptors live")
    parser.add_argument("--cooker", type=Path, default=ROOT / "build/game/openyamm_model_texture_cook")
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    profiles = args.profile or sorted(PROFILES)
    if args.verify:
        if not all([verify(profile, args.assets) for profile in profiles]):
            raise SystemExit("run tools/cook_effect_textures.py")
        return
    lock_path = receipts_path("lock").with_suffix(".lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for profile in profiles:
            cook(profile, args.cooker, args.force, args.plan, args.assets)


if __name__ == "__main__":
    main()
