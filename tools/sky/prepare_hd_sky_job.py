#!/usr/bin/env python3
"""Prepare a Codex job that repaints original MM6-MM8 sky textures as HD tiling masters.

  prepare_hd_sky_job.py <job-name> [sky names...]   (no names: every sky derive_legacy_skies.py derives)

Writes level_generation/lighting/sky_art/hd_skies/<job-name>/ with the native inputs (PNG copies of the original
bitmaps, never x2 restorations) and CODEX_BRIEF.md, and prints the codex command. Codex saves its selections as
<name>_hd.png in the job folder; install_hd_sky_masters.py checks and installs them.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import derive_legacy_skies as derive  # noqa: E402

ROOT = derive.ROOT
JOBS = ROOT / 'level_generation' / 'lighting' / 'sky_art' / 'hd_skies'


def native_source(name):
    for world in derive.WORLD_SEARCH:
        folder = ROOT / 'assets_source' / 'worlds' / world / 'textures'
        for path in sorted(folder.glob('*')) if folder.exists() else []:
            if path.stem.lower() == name and path.suffix.lower() in ('.bmp', '.png'):
                return path
    return None


def hex_colour(rgb):
    return '#' + ''.join(f'{round(max(0.0, min(1.0, value)) * 255):02x}' for value in rgb)


def sky_entry(name, source, image, info):
    width, height = image.size
    lines = [f'### `{name}`', '', f'- Input: `inputs/{name}.png` ({width} × {height}, from `{source.relative_to(ROOT)}`).']
    if info['background'] is None:
        lines.append('- **Fully painted sky**: clouds cover the whole tile. Keep every area painted; there is no open '
                     'sky to flatten.')
    else:
        lines.append(f'- **Open sky with clouds**: paint every gap between clouds as one perfectly flat, even colour '
                     f'**{hex_colour(info["background"])}** (no gradient, noise, haze or texture in the open sky). '
                     f'Clouds keep their own soft edges against it.')
    clusters = ', '.join(hex_colour(cluster['rgb']) for cluster in reversed(info['clusters']))
    lines.append(f'- Main colours of the original, brightest first: {clusters}.')
    lines.append(f'- Save as `{name}_hd.png`.')
    return '\n'.join(lines)


BRIEF_HEADER = '''# Codex brief: HD sky masters ({job})

## Scope

Image generation only. Write files only into `{folder}`. Do not build, run the game, edit code or touch any other
folder. Use your built-in image generation tool. The attached images are the inputs listed per sky, in order.

## What these are

Each input is an original sky texture from Might and Magic 6–8 (1990s, 8-bit palette, dithered, 256–512 px). The game
tiles it across a curved sky plane, so each texel is magnified many times and the original looks pixelated and smeared.
Produce an **HD repaint of each input**: the same picture at high resolution with clean painterly detail.

## Rules for every sky

- **Faithful**: keep the input's exact composition. Every cloud keeps its position, size, shape and direction of
  flow; the colours, palette, lighting direction and mood stay the same. This is a restoration, not a new picture:
  no new clouds, no removed clouds, no change of weather or time of day.
- **Detail**: replace dithering, palette banding and blocky pixels with smooth painted gradients and fine cloud
  structure (soft wisps, billows, subtle internal shading), in the hand-painted style of the original.
- **Tiling**: the input tiles seamlessly (its left edge continues into its right edge, top into bottom). Keep that:
  clouds that leave one edge must continue on the opposite edge, exactly as in the input.
- **Size**: square, the largest size your tool produces (2048 × 2048 if possible, at least 1024 × 1024). Do not add
  borders or margins; the picture fills the whole canvas edge to edge, like the input.
- **Do not add**: sun, moon, stars, rays, lens flare, horizon, ground, mountains, birds, buildings, text, logos,
  frames, vignettes or watermarks. No photographic look.

## Skies

{skies}

## Per-image checks (run before selecting)

1. Square canvas of at least 1024 px, picture fills it edge to edge.
2. Side by side with the input: same cloud layout and colours; nothing added or removed.
3. Open-sky inputs: the gaps between clouds are one flat colour. Sample a few gap pixels: each channel within ±6 of
   the given colour is a pass (the engine keys it out; tiny variation is harmless, so do not spend attempts on it).
4. Tile the result 2 × 2 (for example with Python/PIL into a scratch file in this folder) and check that the edges
   continue without a visible seam or cut-off cloud.
5. None of the "Do not add" items appear.

Regenerate a failed image with one targeted correction (at most three attempts per sky). Keep rejected attempts as
`<name>_hd_try<N>.png`.

## Records

Write `generation_runs.json` listing every attempt: sky, file, prompt, selected flag and reason. Write
`selected_review.png`: each selected master beside its input (input scaled to the same size with nearest-neighbour).
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('job')
    parser.add_argument('names', nargs='*')
    args = parser.parse_args()
    names = [name.lower() for name in args.names] or derive.selectable_names()
    folder = JOBS / args.job
    (folder / 'inputs').mkdir(parents=True, exist_ok=True)
    entries = []
    inputs = []
    for name in names:
        source = native_source(name)
        if source is None:
            sys.exit(f'no native source for {name}')
        image = Image.open(source)
        info = derive.analyse(name, image)
        input_path = folder / 'inputs' / f'{name}.png'
        image.convert('RGB').save(input_path)
        inputs.append(input_path)
        entries.append(sky_entry(name, source, image, info))
    brief = folder / 'CODEX_BRIEF.md'
    brief.write_text(BRIEF_HEADER.format(job=args.job, folder=folder.relative_to(ROOT), skies='\n\n'.join(entries)))
    relative = folder.relative_to(ROOT)
    image_flags = ' '.join(f'-i {path.relative_to(ROOT)}' for path in inputs)
    print(f'codex exec "Follow the brief in {relative}/CODEX_BRIEF.md exactly. The attached images are its inputs in '
          f'order. Produce the HD sky masters with your built-in image generation tool and save them as specified." '
          f'-C {ROOT} -s workspace-write --add-dir ~/.codex/generated_images -o {relative}/codex_last_message.md '
          f'{image_flags} < /dev/null > {relative}/codex_run.log 2>&1')


if __name__ == '__main__':
    main()
