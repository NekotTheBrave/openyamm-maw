"""Explicit migration of installed inline palette tables to the existing runtime LUT schema.

Run once for packages exported before single-region/RGB table normalization was fixed.
PNG art, frame geometry, palettes' numeric values and animation bindings are unchanged.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'level_generation/creatures/mm6_remaining'))
from install_runtime import export_palette_lookups, normalize_palette_model, write


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('asset_root', type=Path)
    parser.add_argument('--receipt', required=True, type=Path)
    args = parser.parse_args()
    receipts = []
    for path in sorted((args.asset_root / 'engine/sprites_new').glob('*/manifest.json')):
        original = path.read_bytes()
        runtime = json.loads(original)
        normalized = copy.deepcopy(runtime)
        model = normalize_palette_model(normalized, normalized['recolor_model'])
        if model == runtime['recolor_model']:
            continue
        normalized['recolor_model'] = model
        files = export_palette_lookups(normalized, path.parent)
        write(path, normalized)
        receipts.append(dict(package=path.parent.name, before_sha256=hashlib.sha256(original).hexdigest(),
                             after_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), model=model,
                             lookups={f: hashlib.sha256((path.parent / f).read_bytes()).hexdigest() for f in files}))
    write(args.receipt, receipts)
    print(f'Migrated {len(receipts)} installed palette manifests')


if __name__ == '__main__':
    main()
