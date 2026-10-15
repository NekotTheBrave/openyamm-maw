#!/usr/bin/env python3
"""Generate the procedural projectile effect textures (original OpenYAMM artwork, CC0).

Writes into assets_dev/engine/effects/authored/textures/:
  glow_00.png            128x128  white, soft radial alpha with a dense centre (heads and halos)
  trail_noise_00.png     256x64   streaky fractal noise, tileable along U (flame and smoke ribbons)
  trail_electric_00.png  256x64   thin wandering filaments with a faint glow, tileable along U (lightning ribbons)

Ribbon textures are greyscale; the ribbon shader reads the red channel and colours it from the recipe.
The output is deterministic: rerun after changing a parameter here.
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "assets_dev/engine/effects/authored/textures"


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def periodic_value_noise(width, height, cells_u, cells_v, rng):
    """Value noise with smooth interpolation; periodic along U, free along V."""
    lattice = rng.random((cells_v + 1, cells_u))
    u = np.arange(width) / width * cells_u
    v = np.arange(height) / (height - 1) * cells_v
    u0 = np.floor(u).astype(int)
    v0 = np.minimum(np.floor(v).astype(int), cells_v - 1)
    fu = smoothstep(0.0, 1.0, u - u0)
    fv = smoothstep(0.0, 1.0, v - v0)
    u1 = (u0 + 1) % cells_u
    u0 = u0 % cells_u
    top = lattice[v0][:, u0] * (1.0 - fu) + lattice[v0][:, u1] * fu
    bottom = lattice[v0 + 1][:, u0] * (1.0 - fu) + lattice[v0 + 1][:, u1] * fu
    return top * (1.0 - fv)[:, None] + bottom * fv[:, None]


def periodic_curve(width, cells, rng, octaves=3):
    """A 1D periodic noise curve in [-1, 1]."""
    total = np.zeros(width)
    amplitude = 1.0
    norm = 0.0
    for octave in range(octaves):
        count = cells * (2 ** octave)
        lattice = rng.random(count) * 2.0 - 1.0
        x = np.arange(width) / width * count
        x0 = np.floor(x).astype(int)
        f = smoothstep(0.0, 1.0, x - x0)
        total += amplitude * (lattice[x0 % count] * (1.0 - f) + lattice[(x0 + 1) % count] * f)
        norm += amplitude
        amplitude *= 0.5
    return total / norm


def glow(size=128):
    coords = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    r = np.sqrt(coords[None, :] ** 2 + coords[:, None] ** 2)
    alpha = np.exp(-(r * 3.4) ** 2) + 0.28 * np.exp(-(r * 1.7) ** 2)
    alpha *= 1.0 - smoothstep(0.82, 1.0, r)
    alpha = np.clip(alpha / alpha.max(), 0.0, 1.0)
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = alpha
    return rgba


def trail_noise(width=256, height=64, seed=11):
    rng = np.random.default_rng(seed)
    total = np.zeros((height, width))
    amplitude = 1.0
    norm = 0.0
    for octave in range(5):
        total += amplitude * periodic_value_noise(width, height, 5 * 2 ** octave, 3 * 2 ** octave, rng)
        norm += amplitude
        amplitude *= 0.55
    noise = total / norm
    noise = smoothstep(0.25, 0.8, noise)
    rgba = np.ones((height, width, 4))
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = rgba[..., 3] = noise
    return rgba


def trail_electric(width=256, height=64, seed=23):
    rng = np.random.default_rng(seed)
    y = np.arange(height)[:, None].astype(float)
    image = np.zeros((height, width))
    for filament, (cells, spread, core_width, brightness) in enumerate(
            [(4, 0.32, 1.1, 1.0), (6, 0.36, 0.9, 0.8), (9, 0.30, 0.7, 0.55)]):
        curve = height * (0.5 + spread * periodic_curve(width, cells, rng))
        distance = np.abs(y - curve[None, :])
        line = np.exp(-(distance / core_width) ** 2) + 0.22 * np.exp(-(distance / (core_width * 5.0)) ** 2)
        image = np.maximum(image, brightness * line)
    image = np.clip(image, 0.0, 1.0)
    rgba = np.ones((height, width, 4))
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = rgba[..., 3] = image
    return rgba


def save(rgba, path):
    pixels = np.clip(np.round(rgba * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(pixels, "RGBA").save(path, optimize=True)
    path.chmod(0o644)
    print(f"wrote {path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    save(glow(), args.output / "glow_00.png")
    save(trail_noise(), args.output / "trail_noise_00.png")
    save(trail_electric(), args.output / "trail_electric_00.png")


if __name__ == "__main__":
    main()
