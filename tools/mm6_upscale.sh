#!/usr/bin/env bash
# Toggle the local MM6 x4 experiment. Run with the game closed.
# Requires Python 3 and the existing tools/upscaling/out_prod_bmodel PNGs.
set -euo pipefail

if [[ $# -ne 1 || ( "$1" != "on" && "$1" != "off" ) ]]; then
    echo "Usage: $0 <on|off>" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 - "${script_dir}/.." "$1" <<'PY'
import configparser
from pathlib import Path
import re
import shutil
import sys

root = Path(sys.argv[1]).resolve()
action = sys.argv[2]
source = root / "tools/upscaling/out_prod_bmodel"
state = root / "tools/upscaling/.mm6-x4-state"
settings = root / "settings.ini"
world = root / "assets_dev/worlds/mm6"
targets = [world / "textures_x4"]
overrides = {
    ("assets", "root"): "assets_dev",
    ("video_quality", "texture"): "x4",
    ("startup", "start_in_main_menu"): "false",
    ("debug", "preseed_party"): "true",
    ("debug", "start_world"): "mm6",
    ("debug", "start_map_file"): "oute3.odm",
    ("debug", "override_start_position"): "false",
}
for category in ("sky", "sprites", "decorations", "icons", "ui", "effects", "fonts"):
    overrides[("video_quality", category)] = "x1"


def update_settings(text, values):
    """Change only managed keys, preserving other settings and formatting."""
    section = ""
    found = set()
    result = []
    for line in text.splitlines(keepends=True):
        header = re.match(r"\s*\[([^]]+)\]", line)
        if header:
            section = header[1].lower()
        entry = re.match(r"(\s*([^;#=\s]+)\s*=)([^\r\n]*)(\r?\n)?$", line)
        key = (section, entry[2].lower()) if entry else None
        if key in values:
            if key in found:
                raise RuntimeError(f"Duplicate setting: {key}")
            line = entry[1] + values[key] + (entry[4] or "")
            found.add(key)
        result.append(line)
    missing = values.keys() - found
    if missing:
        raise RuntimeError(f"Missing settings: {sorted(missing)}")
    return "".join(result)


def write_settings(text):
    temporary = settings.with_name("settings.ini.mm6-x4.tmp")
    temporary.write_text(text)
    temporary.chmod(settings.stat().st_mode & 0o777)
    temporary.replace(settings)


if action == "off":
    if not state.exists():
        print("MM6 x4 is already off; nothing changed.")
        sys.exit(0)
    previous = configparser.ConfigParser(interpolation=None)
    previous.read(state / "settings.ini")
    restored = update_settings(
        settings.read_text(),
        {key: previous[key[0]][key[1]] for key in overrides},
    )
    write_settings(restored)
    for target in targets:
        if target.exists():
            shutil.rmtree(target)
    shutil.rmtree(state)
    print("MM6 x4 disabled: restored previous managed settings and removed the copied x4 folders.")
    sys.exit(0)

if state.exists():
    sys.exit("MM6 x4 state already exists. Run off before enabling again (also after an interrupted copy).")
for target in targets:
    if target.exists() or target.is_symlink():
        sys.exit(f"Refusing to replace existing assets: {target}")

images = sorted(source.glob("*.png"))
if not images:
    sys.exit(f"No upscaled PNGs found in {source}")

enabled = update_settings(settings.read_text(), overrides)
state.mkdir()
shutil.copyfile(settings, state / "settings.ini")
# Save the previous settings before copying so off can undo an interrupted setup.
for target, selected in zip(targets, (images,)):
    target.mkdir()
    for image in selected:
        destination = target / image.name.lower()
        shutil.copyfile(image, destination)
        destination.chmod(0o644)
    print(f"Copied {len(selected)} images to {target.relative_to(root)}")
write_settings(enabled)
print("MM6 x4 enabled. From the repository root, run: ./build/game/openyamm")
print("Starts in New Sorpigal; animated water uses resized originals. Other asset categories remain x1.")
PY
