#!/usr/bin/env python3
"""Convert the pinned CC0 research archives into shared FX sprite resources.

Pass any of the archives; sprites from archives not passed keep their existing files and bindings.
Requires Pillow and PyYAML. Effect libraries are not touched.
"""
import argparse
import hashlib
from io import BytesIO
from pathlib import Path
import zipfile

from PIL import Image
import yaml

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "assets_dev/engine/effects/authored"
SOURCES = {
    "kenney": ("b631d4b07f7002549fdcf155f01141ad482f79f3440e4e301eed49ce5f1d8958",
               "https://kenney.nl/assets/particle-pack", "Kenney", "Particle Pack v1.1"),
    "fireball": ("f60de3f2ce56f941d6f089ea4ea93e3ae51c63fac84c1aa0d6615022f8647010",
                 "https://unity.com/blog/engine-platform/free-vfx-image-sequences-flipbooks",
                 "Unity Labs / Thomas ICHÉ", "FireBall01"),
    "arcane": ("e404dd03289e41c87e64a67a781d4c5dc2b91cb1a45eb54883314231f1fd7b8d",
               "https://opengameart.org/content/arcane-magic-effect", "Cethiel", "Arcane Magic Effect"),
    "picca": ("ba2c321c25dcf3dcab25ba3769ce700e498d90daf481868acca935a3ae48b5f7",
              "https://github.com/RPicster/Godot-particle-and-vfx-textures/archive/refs/heads/main.zip",
              "Raffaele Picca", "Particle and VFX textures"),
}
# Unity Labs flipbooks (CC0, https://unity.com/blog/engine-platform/free-vfx-image-sequences-flipbooks), one archive
# each from https://unity3d.com/files/labs/downloads/vfx/assets01/<Name>/<Name>-flipbooks.zip.
UNITY_LABS = {
    "explosion00": "5931820e3e8f5062446b059bf4b0489a8fc56df5de35170721fe9cd4139d6d42",
    "explosion02": "1426be2dcfc9008b8b8f0c7d54a241da3f611725aa3c84fc33f919f56036f538",
    "flame02": "f37b12cdae090591d537ed3bb50396f03833a49864362bf665d6817b7282c954",
    "wispysmoke01": "f9bdc88948ff18a7d3e6332b68f2f2ff17e9a64c6feb821e05388f4f23dbcff0",
    "cloud02": "99ce5ed3ec21646dd4a0644c2149978ed4f2fd9ab2ef269b5cfcc89a7a9cc036",
}
for _key, _digest in UNITY_LABS.items():
    _title = {"explosion00": "Explosion00", "explosion02": "Explosion02", "flame02": "Flame02",
              "wispysmoke01": "WispySmoke01", "cloud02": "Cloud02"}[_key]
    SOURCES[_key] = (_digest, f"https://unity3d.com/files/labs/downloads/vfx/assets01/{_title}/{_title}-flipbooks.zip",
                     "Unity Labs / Thomas ICHÉ", _title)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in SOURCES:
        parser.add_argument("--" + name.replace("_", "-"), dest=name, type=Path)
    args = parser.parse_args()
    archives = {}
    for name, (digest, *_rest) in SOURCES.items():
        if getattr(args, name) is None:
            continue
        data = getattr(args, name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"Unexpected {name} archive; inspect and update provenance before importing")
        archives[name] = zipfile.ZipFile(BytesIO(data))
    bindings, conversions = [], []
    (DEST / "textures").mkdir(parents=True, exist_ok=True)
    (DEST / "sprites").mkdir(exist_ok=True)

    def read(source, member):
        return Image.open(BytesIO(archives[source].read(member))).convert("RGBA")

    def cells(sheet, columns, rows, keep, size):
        """Cells of a row-major flipbook sheet (fractional cell edges rounded), every keep-th, resized."""
        width, height = sheet.size
        frames = []
        for index in range(0, columns * rows, keep):
            column, row = index % columns, index // columns
            box = (round(column * width / columns), round(row * height / rows),
                   round((column + 1) * width / columns), round((row + 1) * height / rows))
            frames.append(sheet.crop(box).resize(size, Image.Resampling.LANCZOS))
        return frames

    def save(name, frames, fps, source, recipe, logical=(128, 128)):
        frame_ids = []
        for index, frame in enumerate(frames):
            frame_id = f"openyamm:texture/fx/{name}_{index:02d}"
            path = DEST / "textures" / f"{name}_{index:02d}.png"
            frame.save(path)
            path.chmod(0o644)
            frame_ids.append(frame_id)
            bindings.append(dict(resource=frame_id, asset=path.relative_to(ROOT / "assets_dev").as_posix(),
                                 status="bound", kind="texture"))
        resource = f"openyamm:effect-sprite/{name}"
        descriptor = DEST / "sprites" / f"{name}.yml"
        descriptor.write_text(yaml.safe_dump(dict(schema="openyamm.effectSprite.v2", id=resource,
            framesPerSecond=fps, logicalWidth=logical[0], logicalHeight=logical[1], frames=frame_ids),
            sort_keys=False))
        descriptor.chmod(0o644)
        bindings.append(dict(resource=resource, asset=descriptor.relative_to(ROOT / "assets_dev").as_posix(),
                             status="bound", kind="sprite"))
        conversions.append(dict(sprite=resource, source=source, conversion=recipe))

    if "kenney" in archives:
        # Showcase sprites, then the projectile set: glints and bursts (light, spirit), a soft four-point star,
        # a twirl (mind and spirit wisps), an electric blob, debris (earth), wispy smoke, a streak (arrows, blades,
        # water), a flame lick, a wobbly ring (mind) and a crescent slash (blades).
        for name, member in [("ring", "circle_01"), ("spark", "star_01"), ("smoke", "smoke_01"),
                             ("glint", "star_07"), ("starburst", "star_09"), ("soft_star", "magic_05"),
                             ("wisp", "twirl_02"), ("electric", "spark_02"), ("debris", "dirt_02"),
                             ("wisp_smoke", "smoke_05"), ("streak", "trace_02"), ("flame_lick", "flame_06"),
                             ("ripple", "light_02"), ("slash", "slash_02")]:
            frame = read("kenney", f"PNG (Transparent)/{member}.png")
            recipe = f"PNG (Transparent)/{member}.png; RGBA decode and Lanczos 512 to 128; retain soft alpha"
            if name == "streak":
                # Velocity-aligned sprites lay their width along the travel direction; the source streak is vertical.
                frame = frame.transpose(Image.Transpose.ROTATE_90)
                recipe += "; rotated 90 degrees so the streak runs along the width"
            save(name, [frame.resize((128, 128), Image.Resampling.LANCZOS)], 1, "kenney", recipe)
        (DEST / "KENNEY_LICENSE.txt").write_bytes(archives["kenney"].read("License.txt"))
    if "fireball" in archives:
        sheet = read("fireball", "FireBall01_8x8.tga")
        # RGB source is intentionally opaque: additive blending uses its black background as zero light.
        indices = list(range(0, 64, 4))
        frames = [sheet.crop(((i % 8)*128, (i // 8)*128, (i % 8+1)*128, (i // 8+1)*128)) for i in indices]
        save("fireball", frames, 16, "fireball",
             "FireBall01_8x8.tga; row-major cells 0,4,...,60; 16 FPS authored timing; RGB with opaque alpha; "
             "additive")
    if "arcane" in archives:
        # A single reviewed variant avoids inventing animation ordering or registration for this pack.
        arcane = read("arcane", "04/Arcane_Effect_5.png")
        canvas = Image.new("RGBA", (128, 128))
        canvas.paste(arcane, ((128-arcane.width)//2, (128-arcane.height)//2))
        save("arcane", [canvas], 1, "arcane", "04/Arcane_Effect_5.png; centered on 128 RGBA canvas; static variant")
    if "picca" in archives:
        # Flares and ray bursts for cores, glints and impact flashes (white with soft alpha, made to be tinted).
        for name, member in [("flare", "spotlight_4"), ("flare_star", "spotlight_5"), ("flare_cross", "spotlight_8"),
                             ("flare_wide", "spotlight_2"), ("rays", "effect_2"), ("rays_thin", "effect_3"),
                             ("swirl", "effect_4")]:
            path = f"Godot-particle-and-vfx-textures-main/textures/256/alpha/{member}.png"
            save(name, [read("picca", path)], 1, "picca", f"textures/256/alpha/{member}.png; RGBA, unchanged")
        (DEST / "PICCA_LICENSE.txt").write_bytes(
            archives["picca"].read("Godot-particle-and-vfx-textures-main/LICENSE"))
    # Unity Labs flipbooks: straight-alpha colour (explosions, flame) or grey (smoke, cloud) for tinting.
    for name, member, grid, keep, size, fps, logical in [
            ("explosion", "Explosion02_5x5.tga", (5, 5), 1, (192, 192), 25, (128, 128)),
            ("fire_burst", "Explosion00_5x5.tga", (5, 5), 1, (192, 192), 25, (128, 128)),
            ("flame_column", "Flame02_16x4.tga", (16, 4), 2, (128, 256), 24, (128, 256)),
            ("smoke_puff", "WispySmoke01_8x8.tga", (8, 8), 2, (128, 128), 24, (128, 128)),
            ("dust_cloud", "Cloud02_8x8.tga", (8, 8), 4, (128, 128), 12, (128, 128))]:
        source = {"explosion": "explosion02", "fire_burst": "explosion00", "flame_column": "flame02",
                  "smoke_puff": "wispysmoke01", "dust_cloud": "cloud02"}[name]
        if source not in archives:
            continue
        frames = cells(read(source, member), *grid, keep, size)
        save(name, frames, fps, source,
             f"{member}; row-major cells, every {keep}; Lanczos to {size[0]}x{size[1]}; {fps} FPS; straight alpha",
             logical)
    # Keep entries not imported in this run (other archives, generate_projectile_textures.py) after the imported ones.
    imported = {binding["resource"] for binding in bindings}
    existing_bindings = yaml.safe_load((DEST / "resource_bindings.yml").read_text())["bindings"] \
        if (DEST / "resource_bindings.yml").exists() else []
    bindings += [binding for binding in existing_bindings if binding["resource"] not in imported]
    existing_provenance = yaml.safe_load((DEST / "provenance.yml").read_text()) \
        if (DEST / "provenance.yml").exists() else {}
    conversions += [conversion for conversion in existing_provenance.get("conversions", [])
                    if conversion["sprite"] not in imported]
    (DEST / "resource_bindings.yml").write_text(yaml.safe_dump(
        dict(schema="openyamm.effectResourceBindings.v1", bindings=bindings), sort_keys=False))
    provenance = dict(license="CC0-1.0", sources=[dict(key=name, sha256=values[0], url=values[1],
        author=values[2], title=values[3]) for name, values in SOURCES.items()] + [source for source in
        existing_provenance.get("sources", []) if source["key"] not in SOURCES], conversions=conversions)
    (DEST / "provenance.yml").write_text(yaml.safe_dump(provenance, sort_keys=False, allow_unicode=True))
    print(f"Imported {len(conversions)} sprites, {sum(b['kind'] == 'texture' for b in bindings)} PNGs into {DEST}")


if __name__ == "__main__":
    main()
