"""Count icon images and exact decoded-pixel duplicates for restoration planning."""
from collections import Counter,defaultdict
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
P=Path(__file__).resolve().parent

def main():
    folders=[ROOT/'assets_dev/engine/icons']+[ROOT/'assets_dev/worlds'/w/'icons' for w in ('mm6','mm7','mm8','mm9')]
    groups=defaultdict(list);records=[];stats=[];errors=[]
    for folder in folders:
        local=[];extensions=Counter();pixels=0
        for path in sorted(folder.rglob('*')):
            if not path.is_file() or path.suffix.lower() not in ('.bmp','.pcx','.png','.jpg','.jpeg','.tga','.webp','.gif'):continue
            extensions[path.suffix.lower()]+=1
            try:
                with Image.open(path) as im:
                    mode=im.mode;rgba=im.convert('RGBA');size=list(rgba.size)
                    key=hashlib.sha256(str(size).encode()+rgba.tobytes()).hexdigest()
                    pixels+=rgba.width*rgba.height
                    record=dict(path=str(path.relative_to(ROOT)),size=size,mode=mode,pixel_sha256=key,file_bytes=path.stat().st_size,frames=getattr(im,'n_frames',1))
                    records.append(record);local.append(key);groups[key].append(record['path'])
            except Exception as e:errors.append(dict(path=str(path.relative_to(ROOT)),error=str(e)))
        stats.append(dict(folder=str(folder.relative_to(ROOT)),image_files=sum(extensions.values()),decoded_images=len(local),distinct_pixels=len(set(local)),extensions=dict(extensions),pixels=pixels))
        print(stats[-1],flush=True)
    legacy=[r for r in records if not r['path'].startswith('assets_dev/worlds/mm9/')]
    legacy_keys={r['pixel_sha256'] for r in legacy}
    summary=dict(scope='Shared engine icons and MM6/MM7/MM8 world icons; MM9 counted separately. Font FNTs outside these roots excluded.',legacy_image_files=sum(s['image_files'] for s in stats[:4]),legacy_decoded_images=len(legacy),legacy_distinct_images=len(legacy_keys),legacy_redundant_copies=len(legacy)-len(legacy_keys),including_mm9_image_files=sum(s['image_files'] for s in stats),including_mm9_distinct_images=len(groups),folders=stats,decode_errors=errors,notes=['Exact deduplication uses dimensions and decoded RGBA pixels, not just names or file bytes.','Counts describe files on disk, not proven live runtime references.','Palette/colour-key runtime semantics and related animation states require a later grouping audit before sharing generated replacements.'])
    (P/'icons_inventory.json').write_text(json.dumps(dict(summary=summary,images=records,duplicate_groups={k:v for k,v in groups.items() if len(v)>1}),indent=2)+'\n')
    print(json.dumps({k:v for k,v in summary.items() if k not in ('folders','notes')},indent=2))
if __name__=='__main__':main()
