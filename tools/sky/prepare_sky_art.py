#!/usr/bin/env python3
"""Turn the Codex sky sources in level_generation/lighting/sky_art/codex/ into sky texture sources
(assets_source/engine/rendering/sky; tools/cook_sky_textures.py cooks them for the runtime).

  horizon_ring.png  4096x512 RGBA: grey = cloud lighting (0 shadow .. 1 lit), alpha = coverage; wraps horizontally
  night_sky.png     4096x2048 RGB equirect Milky Way on black, poles faded to black
  moon.png          512x512 RGBA neutral disc, alpha = disc mask

  prepare_sky_art.py [--source DIR] [--output DIR]
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
SKY = ROOT / 'assets_source/engine/rendering/sky'


def save(array, mode, path):
    Image.fromarray(np.clip(np.round(array), 0, 255).astype(np.uint8), mode).save(path, optimize=True)
    path.chmod(0o644)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wrap_blend(image, width):
    """Cross-fade the right edge into the left so the panorama wraps without a seam."""
    blended = image[:, :-width].copy()
    ramp = np.linspace(0.0, 1.0, width)[None, :, None] if image.ndim == 3 else np.linspace(0.0, 1.0, width)[None, :]
    blended[:, :width] = image[:, -width:] * (1.0 - ramp) + image[:, :width] * ramp
    return blended


def horizon_ring(source):
    grey = np.asarray(Image.open(source).convert('L'), dtype=np.float32)
    height = grey.shape[0]
    background = np.median(grey[: height // 4])
    # Cloud coverage is anything that departs from the flat upper background; the bottom haze is fully covered.
    alpha = np.clip(np.abs(grey - background) / 28.0, 0.0, 1.0)
    rows = np.linspace(0.0, 1.0, height)[:, None]
    alpha = np.maximum(alpha, np.clip((rows - 0.82) / 0.1, 0.0, 1.0))
    alpha = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)),
                       dtype=np.float32) / 255.0
    lit = np.clip((grey - 80.0) / (245.0 - 80.0), 0.0, 1.0)
    pixels = np.dstack([lit * 255.0] * 3 + [alpha * 255.0])
    pixels = wrap_blend(pixels, pixels.shape[1] // 12)
    # Two repeats around the horizon keep the cloud banks at a believable angular size.
    pixels = np.concatenate([pixels, pixels], axis=1)
    image = Image.fromarray(pixels.astype(np.uint8), 'RGBA').resize((4096, 512), Image.LANCZOS)
    return np.asarray(image, dtype=np.float32)


def night_sky(source):
    rgb = np.asarray(Image.open(source).convert('RGB').resize((4096, 2048), Image.LANCZOS), dtype=np.float32)
    floor = np.percentile(rgb, 20.0)
    rgb = np.clip((rgb - floor) / max(255.0 - floor, 1.0), 0.0, 1.0) * 255.0
    rows = np.linspace(0.0, 1.0, rgb.shape[0])[:, None, None]
    pole_fade = np.clip(np.minimum(rows, 1.0 - rows) / 0.12, 0.0, 1.0)
    rgb = wrap_blend(rgb * pole_fade, 128)
    return np.asarray(Image.fromarray(rgb.astype(np.uint8), 'RGB').resize((4096, 2048), Image.LANCZOS),
                      dtype=np.float32)


def moon(source):
    grey = np.asarray(Image.open(source).convert('L'), dtype=np.float32)
    ys, xs = np.nonzero(grey > 24.0)
    centre_x = (xs.min() + xs.max()) / 2.0
    centre_y = (ys.min() + ys.max()) / 2.0
    radius = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2.0
    box = (int(centre_x - radius), int(centre_y - radius), int(centre_x + radius), int(centre_y + radius))
    disc = np.asarray(Image.fromarray(grey.astype(np.uint8)).crop(box).resize((512, 512), Image.LANCZOS),
                      dtype=np.float32)
    coords = (np.arange(512) + 0.5) / 256.0 - 1.0
    distance = np.sqrt(coords[None, :] ** 2 + coords[:, None] ** 2)
    alpha = np.clip((1.0 - distance) / 0.01, 0.0, 1.0)
    surface = disc / max(np.percentile(disc[distance < 0.9], 98.0), 1.0) * 255.0
    return np.dstack([surface] * 3 + [alpha * 255.0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', type=Path, default=ROOT / 'level_generation/lighting/sky_art/codex')
    parser.add_argument('--output', type=Path, default=SKY)
    args = parser.parse_args()
    records = {}
    for name, build, mode, source in (
            ('horizon_ring', horizon_ring, 'RGBA', 'horizon_ring_src.png'),
            ('night_sky', night_sky, 'RGB', 'night_sky_src.png'),
            ('moon', moon, 'RGBA', 'moon_src.png')):
        source_path = args.source / source
        output_path = args.output / f'{name}.png'
        records[name] = {
            'source': str(source_path.relative_to(ROOT)),
            'source_sha256': hashlib.sha256(source_path.read_bytes()).hexdigest(),
            'sha256': save(build(source_path), mode, output_path),
        }
        print(output_path)
    (args.output / 'sky_art.json').write_text(json.dumps(
        {'generator': 'tools/sky/prepare_sky_art.py', 'art': records}, indent=2) + '\n')


if __name__ == '__main__':
    main()
