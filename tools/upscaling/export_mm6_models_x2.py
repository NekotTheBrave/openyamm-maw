"""Export MM6/MM7/MM8 surface textures at x2 from saved x4 ESRGAN batches.

The historical filename and default MM6 invocation are retained.
"""

import argparse
import csv
import sys
import zipfile
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
PROJECT = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(world='mm6'):
    SOURCE = ROOT / 'assets_dev/worlds' / world / 'textures'
    UPSCALED = PROJECT / ('out_prod_bmodel' if world == 'mm6' else 'out_prod_bmodel_' + world)
    OUTPUT = PROJECT / ('out_prod_bmodel_' + world + '_x2')
    saved_index = {p.stem.lower(): p for p in UPSCALED.glob('*.png')}
    sys.path.insert(0, str(ROOT / 'util'))
    from map_export import parse_odm_file, parse_blv_file
    references = set()
    maps = []
    def collect(value):
        if isinstance(value, dict):
            if 'texture_name' in value:
                references.add(value['texture_name'].strip().lower())
            for item in value.values(): collect(item)
        elif isinstance(value, list):
            for item in value: collect(item)
    for path in sorted((SOURCE.parent / 'maps').glob('*')):
        if path.suffix.lower() not in ('.odm', '.blv'): continue
        collect((parse_odm_file if path.suffix.lower() == '.odm' else parse_blv_file)(path))
        maps.append({'file': path.name, 'sha256': digest(path)})
    terrain = set()
    for table in (ROOT / 'assets_dev/engine/data_tables').glob('terrain_tile_data*.txt'):
        with table.open() as stream:
            terrain.update(row['texture_name'].lower() for row in csv.DictReader(stream, delimiter='\t'))
    assert terrain, 'Missing terrain registry'
    originals = sorted(SOURCE.glob('*.bmp'))
    selected, excluded, unused_copies = [], [], []
    for path in originals:
        name = path.stem.lower()
        if name in terrain or 'sky' in name or 'hdwtr' in name:
            excluded.append(path.name)
        elif ' ' in name and name not in references:
            unused_copies.append(path.name)
        else:
            selected.append(path)
    # Validate the entire source set before writing any exports.
    for path in selected:
        with Image.open(path) as original, Image.open(saved_index[path.stem.lower()]) as restored:
            assert restored.size == (original.width * 4, original.height * 4), path.name
    OUTPUT.mkdir(exist_ok=True)
    records = []
    for path in selected:
        saved = saved_index[path.stem.lower()]
        destination = OUTPUT / (path.stem.lower() + '.png')
        with Image.open(path) as original, Image.open(saved) as restored:
            size = (original.width * 2, original.height * 2)
            result = restored.convert('RGBA').resize(size, Image.Resampling.LANCZOS)
            result.save(destination)
        destination.chmod(0o644)
        records.append({'file': destination.name, 'size': size, 'original_sha256': digest(path),
                        'esrgan_x4_source': str(saved.relative_to(ROOT)), 'esrgan_x4_sha256': digest(saved), 'sha256': digest(destination)})
    report = {
        'world': world, 'runtime_directory': 'worlds/' + world + '/textures_x2',
        'scope': world.upper() + ' surface-texture pool: outdoor bmodels and indoor surfaces; excludes terrain, sky and water animation',
        'method': 'Existing x4 ESRGAN production output downsampled with Pillow Lanczos to exactly x2 native dimensions',
        'saved_script_defaults': {'model': 'realesrgan-x4plus', 'scale': 4,
                                  'unsharp': '0x0.7+0.5+0.02', 'enable_colorkey': 0},
        'provenance_limit': 'Original batch invocation logs are absent; defaults are from the saved script, not verified historical environment overrides.',
        'script_sha256': digest(PROJECT / 'upscale_model.sh'),
        'count': len(records), 'excluded': excluded, 'unused_source_copies': unused_copies,
        'map_sources': maps, 'referenced_surface_textures': sorted(references & {p.stem.lower() for p in selected}),
        'x4_directory': str(UPSCALED.relative_to(ROOT)), 'files': records,
    }
    (OUTPUT / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    samples = {'mm6': ['rh128', 'rhdr2', 'cb3side', 'ch1tople', 'cbrdbrck', 'mswinr1'],
               'mm7': ['hem1erw', '7hhr1bwall', 'hwm1bwall', 'hwm2wall', 'hwp1bwall', 'wall'],
               'mm8': ['gcstroof', 'gcstwind1', 'bwooda1', 'bwoodb1', 't66roof', 'bwoodb4']}[world]
    original_index = {p.stem.lower(): p for p in selected}
    sheet = Image.new('RGB', (960, len(samples) * 280), '#202933')
    draw = ImageDraw.Draw(sheet)
    for row, name in enumerate(samples):
        for col, (label, path) in enumerate([
            ('Original', original_index[name]),
            ('Saved ESRGAN x4', saved_index[name]),
            ('Runtime x2', OUTPUT / (name + '.png')),
        ]):
            with Image.open(path) as image:
                image = image.convert('RGB')
                image.thumbnail((300, 240), Image.Resampling.NEAREST if col == 0 else Image.Resampling.LANCZOS)
                sheet.paste(image, (col * 320, row * 280 + 30))
            draw.text((col * 320 + 6, row * 280 + 8), name + ' / ' + label, fill='white')
    sheet.save(OUTPUT / 'review.jpg')
    with zipfile.ZipFile(PROJECT / (world + '_surface_textures_x2.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.write(OUTPUT / 'manifest.json', 'manifest.json')
        for record in records:
            archive.write(OUTPUT / record['file'], report['runtime_directory'] + '/' + record['file'])
    print(f'{world}: exported {len(records)} x2 textures; excluded {len(excluded)} terrain/sky/water textures and {len(unused_copies)} unused source copies: {OUTPUT}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world', nargs='+', choices=['mm6', 'mm7', 'mm8'], default=['mm6'])
    args = parser.parse_args()
    for world in args.world:
        main(world)
