#!/usr/bin/env python3
"""Generate the procedural liquid art in assets_dev/engine/liquids.

  tools/liquids.py lava-pattern
      Write lava_pattern.png: a tileable 512x512 pattern the lava shader samples twice and drifts.
        R  crust plate borders: distance between the nearest and second-nearest plate centre (0 on a crack,
           1 at 0.25 plate spacing and beyond), so the shader picks the crack width.
        G  smooth tileable noise for crust roughness and heat variation.
        B  one random value per plate: plates cool and pulse at their own pace.
        A  a finer crack network (same encoding as R) for detail inside the plates.
      Our own art (generated, no source image).
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.spatial import cKDTree

ROOT = Path(__file__).resolve().parent.parent
LIQUIDS = ROOT / "assets_dev/engine/liquids"
SIZE = 512


def relaxed_points(random, count, iterations=3):
    """Random points on the unit torus, moved toward their cell centres so cells are similar in size."""
    points = random.random((count, 2))
    for _ in range(iterations):
        samples = random.random((count * 500, 2))
        _, nearest = cKDTree(points, boxsize=1.0).query(samples)
        for index in range(count):
            angles = samples[nearest == index] * 2.0 * np.pi
            if len(angles):
                # The mean on a torus: average the angles.
                points[index] = (np.arctan2(np.sin(angles).mean(0), np.cos(angles).mean(0)) / (2.0 * np.pi)) % 1.0
    return points


def plate_field(points, coordinates, size):
    """Per-pixel crack encoding (0 on a border, 1 at a quarter plate spacing inside) and nearest plate index."""
    distances, indices = cKDTree(points, boxsize=1.0).query(coordinates, k=2)
    spacing = 1.0 / np.sqrt(len(points))
    border = np.clip((distances[:, 1] - distances[:, 0]) / (0.25 * spacing), 0.0, 1.0)
    return border.reshape(size, size), indices[:, 0].reshape(size, size)


def tileable_noise(random, size, octaves):
    """Sum of bilinear value-noise octaves on integer lattices, so it wraps exactly."""
    result = np.zeros((size, size))
    amplitude, total = 1.0, 0.0
    for octave in range(octaves):
        cells = 4 << octave
        lattice = random.random((cells, cells))
        coordinates = (np.arange(size) + 0.5) * cells / size
        index = np.floor(coordinates).astype(int)
        fraction = coordinates - index
        fraction = fraction * fraction * (3.0 - 2.0 * fraction)
        y0, x0 = index[:, None] % cells, index[None, :] % cells
        y1, x1 = (y0 + 1) % cells, (x0 + 1) % cells
        fy, fx = fraction[:, None], fraction[None, :]
        top = lattice[y0, x0] * (1 - fx) + lattice[y0, x1] * fx
        bottom = lattice[y1, x0] * (1 - fx) + lattice[y1, x1] * fx
        result += (top * (1 - fy) + bottom * fy) * amplitude
        total += amplitude
        amplitude *= 0.5
    return result / total


def lava_pattern():
    random = np.random.default_rng(20261011)
    noise = tileable_noise(random, SIZE, 5)
    # Warp the lookups by the noise so plate borders wander instead of running straight.
    coordinates = (np.indices((SIZE, SIZE)).reshape(2, -1).T[:, ::-1] + 0.5) / SIZE
    warp = 0.035 * (noise - 0.5)
    warped = (coordinates + np.stack([warp.ravel(), np.roll(warp, SIZE // 3, axis=0).ravel()], 1)) % 1.0
    plates = relaxed_points(random, 36)
    border, nearest = plate_field(plates, warped, SIZE)
    plate_values = random.random(len(plates))[nearest]
    fine, _ = plate_field(relaxed_points(random, 220), warped, SIZE)
    channels = np.stack([border, noise, plate_values, fine], axis=-1)
    LIQUIDS.mkdir(parents=True, exist_ok=True)
    target = LIQUIDS / "lava_pattern.png"
    Image.fromarray(np.round(channels * 255.0).astype(np.uint8), "RGBA").save(target, optimize=True)
    print(f"wrote {target.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("lava-pattern")
    args = parser.parse_args()
    if args.command == "lava-pattern":
        lava_pattern()


if __name__ == "__main__":
    main()
