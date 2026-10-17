"""Validate the MM7/MM8 x2 export artifacts against native and saved x4 sources."""
import hashlib,json,zipfile
from pathlib import Path
from PIL import Image
P=Path(__file__).resolve().parent
ROOT=P.parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    reports=[]
    for world in ('mm7','mm8'):
        output=P/('out_prod_bmodel_'+world+'_x2')
        manifest=json.loads((output/'manifest.json').read_text())
        originals={p.stem.lower():p for p in (ROOT/'assets_dev/worlds'/world/'textures').glob('*.bmp')}
        names={r['file'] for r in manifest['files']}
        assert len(names)==manifest['count']
        assert names=={p.name for p in output.glob('*.png')}
        assert not any(Path(n).stem.lower() in {Path(x).stem.lower() for x in manifest['excluded']+manifest['unused_source_copies']} for n in names)
        for record in manifest['files']:
            result=output/record['file'];source=originals[result.stem];saved=ROOT/record['esrgan_x4_source']
            assert sha(source)==record['original_sha256'],source
            assert sha(saved)==record['esrgan_x4_sha256'],saved
            assert sha(result)==record['sha256'],result
            assert result.name==result.name.lower() and result.stat().st_mode&0o777==0o644
            with Image.open(source) as a,Image.open(saved) as b,Image.open(result) as c:
                c.load()
                assert c.mode=='RGBA'
                assert b.size==(a.width*4,a.height*4)
                assert c.size==(a.width*2,a.height*2)==tuple(record['size'])
        for m in manifest['map_sources']:
            assert sha(ROOT/'assets_dev/worlds'/world/'maps'/m['file'])==m['sha256']
        archive=P/(world+'_surface_textures_x2.zip')
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            expected={manifest['runtime_directory']+'/'+n for n in names}|{'manifest.json'}
            assert set(z.namelist())==expected
            for r in manifest['files']:
                assert hashlib.sha256(z.read(manifest['runtime_directory']+'/'+r['file'])).hexdigest()==r['sha256']
        reports.append(dict(world=world,passed=True,textures=len(names),maps_scanned=len(manifest['map_sources']),referenced_surface_names=len(manifest['referenced_surface_textures']),unused_source_copies=manifest['unused_source_copies'],manifest_sha256=sha(output/'manifest.json'),zip_sha256=sha(archive),zip_bytes=archive.stat().st_size,checks=['Source/x4/x2 hashes','Native x2 dimensions and RGBA decode','Lowercase names and 0644 permissions','Exact manifest/directory/ZIP membership','ZIP CRC and content hashes','Scanned map source hashes'],visual_review='See review.jpg; original / saved x4 / runtime x2 samples.'))
        print(world,len(names),'textures verified',flush=True)
    (P/'mm7_mm8_x2_validation.json').write_text(json.dumps(dict(passed=True,worlds=reports),indent=2)+'\n')
if __name__=='__main__':main()
