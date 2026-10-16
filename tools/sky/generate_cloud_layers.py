#!/usr/bin/env python3
"""Generate the Enhanced sky's tileable cloud density layers.

Every field is built from periodic noise (spectral fBm and toroidal Worley cells), so the textures tile exactly.
Output: RGBA PNG, R = density (histogram-equalised so `coverage` in sky.yml is the covered fraction of the sky),
G = thickness (low-frequency body; thick cores shade darker), B = 0, A = 255.

  generate_cloud_layers.py [--size 1024] [--seed 7] [--output assets_source/engine/rendering/sky]
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def spectral_noise(size, rng, beta, stretch=(1.0, 1.0), low_cut=1.0, high_cut=None):
    """Periodic Gaussian field with a power-law spectrum; stretch > 1 elongates features along that axis."""
    fy = np.fft.fftfreq(size)[:, None] * size
    fx = np.fft.fftfreq(size)[None, :] * size
    radius = np.sqrt((fx / stretch[0]) ** 2 + (fy / stretch[1]) ** 2)
    amplitude = np.where(radius >= low_cut, 1.0 / np.maximum(radius, 1e-6) ** (beta / 2.0), 0.0)
    if high_cut is not None:
        amplitude *= np.exp(-(radius / high_cut) ** 2)
    phase = rng.uniform(0.0, 2.0 * np.pi, (size, size))
    field = np.real(np.fft.ifft2(amplitude * np.exp(1j * phase)))
    return (field - field.mean()) / (field.std() + 1e-9)


def worley(size, rng, cells, stretch=(1.0, 1.0)):
    """Toroidal distance to the nearest of cells x cells jittered points, normalised to 0..1."""
    points = (np.stack(np.meshgrid(np.arange(cells), np.arange(cells), indexing='xy'), -1).reshape(-1, 2)
              + rng.uniform(0.0, 1.0, (cells * cells, 2))) / cells
    coords = (np.arange(size) + 0.5) / size
    nearest = np.full((size, size), np.inf)
    for px, py in points:
        dx = np.abs(coords[None, :] - px)
        dy = np.abs(coords[:, None] - py)
        dx = np.minimum(dx, 1.0 - dx) * stretch[0]
        dy = np.minimum(dy, 1.0 - dy) * stretch[1]
        nearest = np.minimum(nearest, np.sqrt(dx * dx + dy * dy))
    return nearest / nearest.max()


def warp(field, rng, strength, size):
    """Periodic domain warp for wispy, wind-sheared shapes."""
    offset_x = spectral_noise(size, rng, 3.0) * strength
    offset_y = spectral_noise(size, rng, 3.0) * strength
    yy, xx = np.mgrid[0:size, 0:size]
    return field[(yy + offset_y).astype(int) % size, (xx + offset_x).astype(int) % size]


def equalise(field):
    """Rank transform to a uniform 0..1 distribution."""
    order = np.argsort(field, axis=None)
    ranks = np.empty(field.size)
    ranks[order] = np.linspace(0.0, 1.0, field.size)
    return ranks.reshape(field.shape)


def blur(field, sigma):
    """Periodic Gaussian blur in the frequency domain."""
    size = field.shape[0]
    f = np.fft.fftfreq(size)
    kernel = np.exp(-2.0 * (np.pi * sigma) ** 2 * (f[:, None] ** 2 + f[None, :] ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(field) * kernel))


def normalise(field):
    return (field - field.min()) / (field.max() - field.min() + 1e-9)


def cumulus(size, rng):
    body = spectral_noise(size, rng, 3.2, low_cut=2.0)
    billows = 1.0 - worley(size, rng, 12)
    detail = spectral_noise(size, rng, 2.4, low_cut=12.0, high_cut=size / 6.0)
    density = body * 1.0 + billows * 1.6 + detail * 0.2
    thickness = normalise(blur(body + billows, size / 96.0))
    return equalise(density), thickness


def stratus(size, rng):
    body = spectral_noise(size, rng, 3.6, stretch=(2.5, 1.0), low_cut=1.0)
    ripple = spectral_noise(size, rng, 2.0, stretch=(3.0, 1.0), low_cut=10.0)
    density = body + ripple * 0.18
    thickness = normalise(blur(body, size / 48.0))
    return equalise(density), thickness


def cirrus(size, rng):
    streaks = spectral_noise(size, rng, 3.0, stretch=(9.0, 1.0), low_cut=3.0, high_cut=size / 8.0)
    streaks = warp(streaks, rng, size / 28.0, size)
    mask = spectral_noise(size, rng, 3.6, low_cut=1.0)
    density = blur(streaks, 1.5) + mask * 1.3
    thickness = normalise(blur(mask, size / 32.0)) * 0.4
    return equalise(density), thickness


def storm(size, rng):
    body = spectral_noise(size, rng, 3.6, low_cut=1.0)
    towers = 1.0 - worley(size, rng, 6)
    detail = spectral_noise(size, rng, 2.4, low_cut=8.0, high_cut=size / 6.0)
    density = body * 1.3 + towers * 1.4 + detail * 0.25
    thickness = np.clip(normalise(blur(body + towers * 1.2, size / 64.0)) * 1.3, 0.0, 1.0)
    return equalise(density), thickness


LAYERS = {
    'clouds_cumulus': cumulus,
    'clouds_stratus': stratus,
    'clouds_cirrus': cirrus,
    'clouds_storm': storm,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--size', type=int, default=1024)
    parser.add_argument('--seed', type=int, default=7)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets_source/engine/rendering/sky')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = {}
    for index, (name, build) in enumerate(LAYERS.items()):
        rng = np.random.default_rng(args.seed * 1000 + index)
        density, thickness = build(args.size, rng)
        pixels = np.zeros((args.size, args.size, 4), dtype=np.uint8)
        pixels[..., 0] = np.round(density * 255.0)
        pixels[..., 1] = np.round(np.clip(thickness, 0.0, 1.0) * 255.0)
        pixels[..., 3] = 255
        path = args.output / f'{name}.png'
        Image.fromarray(pixels, 'RGBA').save(path, optimize=True)
        path.chmod(0o644)
        records[name] = {'size': args.size, 'seed': args.seed * 1000 + index,
                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        print(f'{path}: {records[name]["sha256"][:12]}')
    (args.output / 'cloud_layers.json').write_text(json.dumps(
        {'generator': 'tools/sky/generate_cloud_layers.py', 'layers': records}, indent=2) + '\n')


if __name__ == '__main__':
    main()
