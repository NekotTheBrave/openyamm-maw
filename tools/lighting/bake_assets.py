"""Native bake texture lookup using the runtime's merged development-package order."""
from pathlib import Path


def source_roots(asset_root, extra_sources=()):
    """Where source art lives beside a runtime tree: its sibling assets_source, then any extra roots (a staged
    world's gathered sources, such as the Hesper stage's sources/)."""
    return [asset_root.parent / 'assets_source'] + [Path(root) for root in extra_sources]


def dependency_file(asset_root, name, extra_sources=()):
    """A bake dependency named relative to the runtime tree: a runtime file, or source art at the same relative path
    in assets_source beside it (the original world texture BMPs, which the runtime no longer carries). A model (.glb)
    is its source in assets_source when there is one: the runtime copy may be the cooked one."""
    path = asset_root / name
    sources = [root / name for root in source_roots(asset_root, extra_sources)]
    if name.endswith('.glb'):
        for source in sources:
            if source.exists():
                return source
    if path.exists():
        return path
    return next((source for source in sources if source.exists()), sources[0])


def texture_paths(asset_root, world, category, extra_sources=(), pattern='*.bmp'):
    # AssetFileSystem::mountMergedWorldPackageRoots: active world, engine, sorted other worlds. The original texture
    # and terrain BMPs are source art in assets_source, under each package's same relative path.
    worlds = sorted(path.name for path in (asset_root / 'worlds').iterdir() if path.is_dir())
    packages = [Path('worlds') / world, Path('engine')] + [Path('worlds') / name for name in worlds if name != world]
    roots = [root / package / category for package in packages
             for root in [asset_root] + source_roots(asset_root, extra_sources)]
    if category == 'sprites':
        # Approved non-creature BMPs moved here; baked shadows retain their native-source provenance.
        roots.append(asset_root / '_legacy/sprites_original')
    paths = {}
    for root in roots:
        for path in sorted(root.glob(pattern)):
            paths.setdefault(path.stem.lower(), path)
    return paths


def bake_texture_paths(asset_root, world, extra_sources=()):
    textures = texture_paths(asset_root, world, 'textures', extra_sources)
    terrain = texture_paths(asset_root, world, 'terrain', extra_sources)
    # Faces of a world without native BMPs (Hesper) resolve to their 4x PNG masters (textures_x4 sources, a quarter
    # of their size in texels); a native BMP of the same name always wins.
    masters = texture_paths(asset_root, world, 'textures_x4', extra_sources, '*.png') if extra_sources else {}
    textures = dict(masters, **textures)
    # Terrain's native path falls back to textures; bmodels may use terrain liquid materials.
    return dict(terrain, **textures), dict(textures, **terrain)


def texture_logical_scale(path):
    """Texels of a source image per logical texel: 4 for a textures_x4 master, 1 for the native BMPs."""
    return 4 if 'textures_x4' in Path(path).parts else 1
