#!/usr/bin/env python3
"""Package OpenYAMM with MAW for players: upstream's Windows release (openyamm_windows_release_zip) with MAW's
scripts, the Merge base, the port's scripts and MAW's converted assets beside it, and a mods.yml whose paths are
relative to it. Writes build/release/OpenYAMM-MAW-<date>-<commit>.zip and its .sha256.

Run from the fork's root in Git Bash, with the build's PATH (Strawberry's bin, Python):

  python tools/maw/package.py --maw C:/Users/.../Maw-Mod-MMMerge-main --merge C:/Users/.../mmmerge-Rodril

--folder writes an unzipped folder instead, to try a build before releasing it. --no-release reuses build/release/Release/OpenYAMM as it is (the release target takes a few minutes)."""

import argparse
import datetime
import hashlib
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Stage = Root / "build" / "release" / "Release" / "OpenYAMM"
PackageName = "OpenYAMM-MAW"

ModsYml = """\
# OpenYAMM's mod configuration: MAW, with the Merge base it is built on. Paths are relative to this file.
# Set enabled to false to play OpenYAMM without MAW.
enabled: true
merge_base: mods/maw/merge
mods:
  - id: maw
    # MAW's own scripts and data.
    path: mods/maw/source
    # MAW's tables, maps and art, converted for OpenYAMM.
    assets: mods/maw/assets
    # OpenYAMM versions of MAW files that cannot run as shipped.
    port: mods/maw/port
    settings:
      # The difficulty a new game starts at, until one is chosen in the game:
      # Easy | Normal | MAW | Hard | Hell | Nightmare
      difficulty: MAW
"""

Readme = """\
OpenYAMM - MAW port (unofficial)
================================

MAW (Might and Magic Merge's "MAW" mod) running on OpenYAMM, a modern reimplementation of Might and Magic VI,
VII and VIII. This is an unofficial fan port: it is not made by, or supported by, the OpenYAMM or MAW teams.
Report problems at https://github.com/NekotTheBrave/openyamm-maw/issues, not to them.

Version: {version}

Playing
-------
1. Unzip this folder anywhere (it needs about 6 GB).
2. Run openyamm.exe. Windows may warn that the program is unrecognised (it is not signed): choose
   "More info", then "Run anyway".
3. Start a new game. Character creation has MAW's difficulty and game mode (Normal, Doom, Road to Insanity,
   Beyond Madness, Austerity).

Nothing else needs installing: the game data is included.

Settings
--------
- Settings > MAW: MAW's own options (buff rework, loot filter, extra item tooltip, and the options of MAW's
  settings file). The four under "This game" belong to the game being played and can be changed in a game only.
- Settings > Gameplay: difficulty, homing projectiles, MM6 monster missiles, and OpenYAMM's own options.
- Settings > Video: sword swings (the multiplayer package's melee swing effect).
- MAW's settings file is mods/maw/files/MAWSettings.lua, created on the first start (its keys can be changed there).

Saves are kept in the saves folder. Saves of the original MAW (MMExtension) cannot be loaded, and these saves
cannot be loaded in the original.

To play OpenYAMM without MAW, set "enabled: false" in mods.yml.

Upgrading
---------
Close the game, then unzip the new version over the folder you have, replacing the files it asks about. Your saves
(saves), your settings (settings.ini, made from settings.default.ini on the first start) and MAW's settings file
(mods/maw/files) are not in the zip, so they are kept. mods.yml is replaced: set "enabled: false" again if you had.
To start from the default settings, delete settings.ini.

Known issues
------------
- Black spots in some water (Emerald Isle's stepped pools east of the town, for one): the smoothed terrain pokes up
  through raised water surfaces. This is an OpenYAMM issue, waiting for a fix there. Work-around: set
  "terrain_subdivision=1" in settings.ini (under [video]); the spots go, but the landscapes are blockier. Choosing
  a graphics quality preset in the game sets it back to 2 or 4.

Logs
----
If something goes wrong, logs/mod_runtime.log has what MAW's scripts did in the last session, with any errors.
Attach it to a report.

Credits
-------
- OpenYAMM by Petr Jasicek (pjasicek) and contributors, GPL-3.0-or-later (LICENSE, COPYRIGHT):
  https://github.com/pjasicek/openyamm
- MAW by Malekitsu and the MAW team, included with their permission:
  https://github.com/Malekitsu/Maw-Mod-MMMerge
- Might and Magic Merge (MMMerge) by Rodril and contributors.
- The sword swing art is from MAW's multiplayer package.
- The port, by NekotTheBrave, with the source of this build at
  https://github.com/NekotTheBrave/openyamm-maw (branch maw-port, commit {commit}).

Might and Magic and its game data belong to their rights holders. They are not licensed by OpenYAMM's licence
(see COPYRIGHT).
"""


def git(*args):
    return subprocess.run(["git", *args], cwd=Root, check=True, capture_output=True, text=True).stdout.strip()


def tree(source, skip):
    """(path, path relative to source) of each file under source, minus .git and the names in skip."""
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if path.is_file() and ".git" not in relative.parts and not set(relative.parts) & skip:
            yield path, relative.as_posix()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--maw", required=True, type=Path, help="MAW's source checkout (the pinned commit)")
    parser.add_argument("--merge", required=True, type=Path, help="the Merge base (mods.yml's merge_base)")
    parser.add_argument("--no-release", action="store_true", help="reuse the existing release stage")
    parser.add_argument("--folder", action="store_true",
                        help="write the package as a folder (build/release/OpenYAMM-MAW-<version>) instead of a zip")
    args = parser.parse_args()

    for folder, name in ((args.maw / "Scripts", "MAW"), (args.merge / "Scripts", "Merge base"),
                         (Root / "mods/maw/assets/worlds", "MAW's converted assets")):
        if not folder.is_dir():
            sys.exit(f"{name} not found: {folder}")

    if git("status", "--porcelain", "--untracked-files=no"):
        print("warning: the fork has uncommitted changes; the README names the last commit")

    if not args.no_release:
        subprocess.run(["cmake", "--build", "build", "--target", "openyamm_windows_release_zip"], cwd=Root,
                       check=True)

    if not (Stage / "openyamm.exe").is_file():
        sys.exit(f"No release stage at {Stage}: run without --no-release")

    commit = git("rev-parse", "--short", "HEAD")
    version = f"{datetime.date.today():%Y-%m-%d}-{commit}"
    output = Root / "build" / "release" / f"{PackageName}-{version}.zip"
    # The player's files are not in the package, so that a newer one unzipped over an installed one keeps them: the
    # saves, the mod's data folder (mods/maw/files: MAW's settings and its death counter) and the settings, which ship
    # as settings.default.ini and become settings.ini on the first start.
    entries = [(path, "settings.default.ini" if relative == "settings.ini" else relative)
               for path, relative in tree(Stage, set())]
    entries += [(path, "mods/maw/source/" + relative) for path, relative in tree(args.maw, set())]
    entries += [(path, "mods/maw/merge/" + relative) for path, relative in tree(args.merge, set())]
    entries += [(path, "mods/maw/port/" + relative) for path, relative in tree(Root / "tools/maw/port", set())]
    entries += [(path, "mods/maw/assets/" + relative)
                for path, relative in tree(Root / "mods/maw/assets", {"conversion_report.txt"})]

    readme = Readme.format(version=version, commit=commit)

    if args.folder:
        folder = output.with_suffix("")

        if folder.exists():
            sys.exit(f"{folder} exists already")

        for path, relative in entries:
            target = folder / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)

        (folder / "mods.yml").write_text(ModsYml, encoding="utf-8")
        (folder / "README-MAW.txt").write_text(readme, encoding="utf-8")
        print(f"{folder} ({len(entries) + 2} files)")
        return

    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, relative in entries:
            # The game data zips are compressed already.
            compression = zipfile.ZIP_STORED if path.suffix.lower() == ".zip" else zipfile.ZIP_DEFLATED
            archive.write(path, f"{PackageName}/{relative}", compress_type=compression)
        archive.writestr(f"{PackageName}/mods.yml", ModsYml)
        archive.writestr(f"{PackageName}/README-MAW.txt", readme)

    digest = hashlib.sha256()
    with output.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    output.with_suffix(".zip.sha256").write_text(f"{digest.hexdigest()}  {output.name}\n", encoding="utf-8")
    print(f"{output} ({output.stat().st_size / 1e9:.2f} GB, {len(entries) + 2} files)")


if __name__ == "__main__":
    main()
