"""Native bake texture lookup using the runtime's merged development-package order."""
from pathlib import Path


def dependency_file(asset_root, name):
    """A bake dependency named relative to the runtime tree: a runtime file, or source art at the same relative path
    in assets_source beside it (the original world texture BMPs, which the runtime no longer carries). A model (.glb)
    is its source in assets_source when there is one: the runtime copy may be the cooked one."""
    path = asset_root / name
    source = asset_root.parent / 'assets_source' / name
    if name.endswith('.glb') and source.exists():
        return source
    return path if path.exists() else source


def texture_paths(asset_root, world, category):
    # AssetFileSystem::mountMergedWorldPackageRoots: active world, engine, sorted other worlds. The original texture
    # and terrain BMPs are source art in assets_source, under each package's same relative path.
    source_root = asset_root.parent / 'assets_source'
    worlds = sorted(path.name for path in (asset_root / 'worlds').iterdir() if path.is_dir())
    packages = [Path('worlds') / world, Path('engine')] + [Path('worlds') / name for name in worlds if name != world]
    roots = [root / package / category for package in packages for root in (asset_root, source_root)]
    if category == 'sprites':
        # Approved non-creature BMPs moved here; baked shadows retain their native-source provenance.
        roots.append(asset_root / '_legacy/sprites_original')
    paths = {}
    for root in roots:
        for path in sorted(root.glob('*.bmp')):
            paths.setdefault(path.stem.lower(), path)
    return paths


def bake_texture_paths(asset_root, world):
    textures = texture_paths(asset_root, world, 'textures')
    terrain = texture_paths(asset_root, world, 'terrain')
    # Terrain's native path falls back to textures; bmodels may use terrain liquid materials.
    return dict(terrain, **textures), dict(textures, **terrain)
