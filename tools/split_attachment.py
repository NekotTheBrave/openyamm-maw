#!/usr/bin/env python3
"""Split a rigid held item (a staff, a weapon) out of a skinned creature GLB into a static attachment GLB.

The item's triangles are those whose vertices are all weighted fully to one joint (--joint). They are removed from
every mesh of the creature (colour LODs and shadow meshes); the creature keeps that joint, so the engine can attach
the item back to it at run time (actors.yml `attachments`). The attachment GLB holds the item in the joint's own
space (the joint's inverse bind matrix applied), one node whose mesh lists its coarser levels in `openyamm_lods`, and
the creature's material for the item: the item's part of each texture (region mask included) cropped out, with the
texture coordinates remapped into the crop, so the attachment does not carry the creature's whole atlas. Colour
variants are kept with the same names and ramps, so a carrier's skin recolours its item as before.
A triangle that mixes item and body vertices is refused.
Usage: split_attachment.py creature.glb --joint Staff --body body.glb --attachment staff.glb"""
import argparse
import copy
import io
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from merge_head_material import (TEXTURE_SLOTS, TYPES, accessor_array, append_accessor,  # noqa: E402
                                 compact, image_of, read_glb, slot, write_glb)

PADDING = 4
GRID = 64


def item_vertices(document, binary, primitive, joint_slot):
    mask = None
    for name in [n for n in primitive['attributes'] if n.startswith('JOINTS_')]:
        joints = accessor_array(document, binary, primitive['attributes'][name])
        weights = accessor_array(document, binary, primitive['attributes']['WEIGHTS_' + name[7:]]).astype(np.float64)
        if document['accessors'][primitive['attributes']['WEIGHTS_' + name[7:]]].get('normalized'):
            weights /= np.iinfo(weights.dtype).max if weights.dtype.kind in 'ui' else 1.0
        full = ((joints == joint_slot) & (weights > 0.999)).any(axis=1)
        mask = full if mask is None else mask | full
    return mask


def split_primitive(document, binary, primitive, joint_slot):
    """(body indices, item indices) of one primitive's triangles."""
    indices = accessor_array(document, binary, primitive['indices']).reshape(-1, 3)
    item = item_vertices(document, binary, primitive, joint_slot)
    per_triangle = item[indices]
    mixed = per_triangle.any(axis=1) & ~per_triangle.all(axis=1)
    if mixed.any():
        raise ValueError(f'{int(mixed.sum())} triangles mix item and body vertices')
    return indices[~per_triangle.all(axis=1)], indices[per_triangle.all(axis=1)]


def compacted(document, binary, primitive, triangles, names):
    used, remapped = np.unique(triangles.ravel(), return_inverse=True)
    attributes = {name: accessor_array(document, binary, primitive['attributes'][name])[used] for name in names}
    return attributes, remapped.astype(np.uint32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('creature', type=Path)
    parser.add_argument('--joint', required=True)
    parser.add_argument('--body', type=Path, required=True)
    parser.add_argument('--attachment', type=Path, required=True)
    args = parser.parse_args()
    document, binary = read_glb(args.creature)
    names = [node.get('name') for node in document['nodes']]
    if len(document.get('skins', [])) != 1:
        raise ValueError('expected one skin')
    skin = document['skins'][0]
    joint_node = names.index(args.joint)
    joint_slot = skin['joints'].index(joint_node)
    inverse_bind = accessor_array(document, binary, skin['inverseBindMatrices'])[joint_slot].reshape(4, 4).T

    # The colour LOD chain of the skinned mesh: its base mesh and openyamm_lods, coarsest last.
    base = next(index for index, mesh in enumerate(document['meshes']) if 'openyamm_lods' in mesh.get('extras', {}))
    chain = [base] + document['meshes'][base]['extras']['openyamm_lods']
    item_levels = []
    for mesh_index, mesh in enumerate(document['meshes']):
        new_primitives = []
        for primitive in mesh['primitives']:
            body, item = split_primitive(document, binary, primitive, joint_slot)
            if mesh_index in chain and len(item):
                if item_levels and item_levels[-1][0] == mesh_index:
                    raise ValueError(f"{mesh.get('name')}: the item spans more than one primitive")
                item_levels.append((mesh_index, primitive, item))
            if len(body) == 0:
                continue
            attribute_names = sorted(primitive['attributes'])
            values, indices = compacted(document, binary, primitive, body, attribute_names)
            rebuilt = copy.deepcopy(primitive)
            rebuilt['attributes'] = {name: append_accessor(document, binary, values[name],
                                                           document['accessors'][primitive['attributes'][name]], 34962)
                                     for name in attribute_names}
            index_type = 5123 if indices.max() < 65536 else 5125
            rebuilt['indices'] = append_accessor(document, binary, indices.astype(TYPES[index_type]),
                                                 {'componentType': index_type, 'type': 'SCALAR'}, 34963)
            new_primitives.append(rebuilt)
        mesh['primitives'] = new_primitives
    if [level[0] for level in item_levels] != chain:
        raise ValueError(f'the item must appear once in every colour level; found it in {len(item_levels)}')
    variants = document.get('extensions', {}).get('KHR_materials_variants', {}).get('variants', [])
    item_primitive = item_levels[0][1]
    item_materials = [item_primitive['material']] * max(len(variants), 1)
    for mapping in item_primitive.get('extensions', {}).get('KHR_materials_variants', {}).get('mappings', []):
        for variant in mapping['variants']:
            item_materials[variant] = mapping['material']
    attachment = build_attachment(document, binary, item_levels, inverse_bind,
                                  [document['materials'][index] for index in item_materials], variants, args.joint)
    write_glb(args.body, *compact(document, binary))
    write_glb(args.attachment, *attachment)
    print(f'SPLIT {args.joint}: item triangles per level '
          f'{[len(level[2]) for level in item_levels]} -> {args.attachment.name}')


def build_attachment(document, binary, item_levels, inverse_bind, variant_materials, variants, name):
    out = {'asset': {'version': '2.0', 'generator': 'OpenYAMM split_attachment.py'}, 'buffers': [{'byteLength': 0}],
           'bufferViews': [], 'accessors': [], 'images': [], 'textures': [], 'samplers': [],
           'materials': [], 'meshes': [], 'nodes': [{'name': name, 'mesh': 0}], 'scenes': [{'nodes': [0]}],
           'scene': 0}
    out_binary = bytearray()
    levels = []
    for mesh_index, primitive, triangles in item_levels:
        names = [n for n in ('POSITION', 'NORMAL', 'TEXCOORD_0') if n in primitive['attributes']]
        values, indices = compacted(document, binary, primitive, triangles, names)
        positions = np.c_[values['POSITION'].astype(np.float64), np.ones(len(values['POSITION']))]
        values['POSITION'] = (positions @ inverse_bind.T)[:, :3].astype(np.float32)
        if 'NORMAL' in values:
            normals = values['NORMAL'].astype(np.float64) @ inverse_bind[:3, :3].T
            values['NORMAL'] = (normals / np.linalg.norm(normals, axis=1, keepdims=True)).astype(np.float32)
        levels.append((values, indices))
    # Crop every texture to the item's texture coordinates (all levels), then remap the coordinates into the crop.
    texcoords = np.concatenate([values['TEXCOORD_0'] for values, _ in levels])
    # Variants differ only in their ramps; they share the creature's textures and so the cropped ones.
    stripped = [{key: value for key, value in material.items() if key != 'name'} for material in variant_materials]
    for material in stripped:
        material['extras'] = {key: value for key, value in material.get('extras', {}).items()
                              if key != 'openyamm_region_ramps'}
    if any(material != stripped[0] for material in stripped):
        raise ValueError('the item\'s variant materials differ beyond their ramps')
    # One crop box in texture coordinates for every map, on a grid coarse enough for each map size (all powers of
    # two up to GRID times smaller than the largest map stay texel-aligned).
    slots = [path for path in TEXTURE_SLOTS if slot(variant_materials[0], path) is not None]
    images = {path: image_of(document, binary, slot(variant_materials[0], path)['index']) for path in slots}
    largest = max((image.size for image, _ in images.values()), default=(1, 1))
    grid = (largest[0] // GRID, largest[1] // GRID)
    cells = [max(int(np.floor((texcoords[:, axis].min() * largest[axis] - PADDING) / largest[axis] * grid[axis])), 0)
             for axis in (0, 1)]
    ends = [min(int(np.ceil((texcoords[:, axis].max() * largest[axis] + PADDING) / largest[axis] * grid[axis])),
                grid[axis]) for axis in (0, 1)]
    box = (cells[0] / grid[0], cells[1] / grid[1], ends[0] / grid[0], ends[1] / grid[1])
    texture_indices = {}
    for path, (image, image_name) in images.items():
        width, height = image.size
        if width % grid[0] or height % grid[1]:
            raise ValueError(f'{image_name} ({width}x{height}) is too small for a shared crop box')
        crop = (round(box[0] * width), round(box[1] * height), round(box[2] * width), round(box[3] * height))
        buffer = io.BytesIO()
        image.crop(crop).save(buffer, 'PNG', optimize=True)
        out['images'].append({'bufferView': add_view(out, out_binary, buffer.getvalue()), 'mimeType': 'image/png',
                              'name': f'{image_name}_{name.lower()}'})
        out['textures'].append({'source': len(out['images']) - 1, 'sampler': 0})
        texture_indices[path] = len(out['textures']) - 1
    out['samplers'].append({'magFilter': 9729, 'minFilter': 9987, 'wrapS': 33071, 'wrapT': 33071})
    for material in variant_materials:
        material = copy.deepcopy(material)
        for path, index in texture_indices.items():
            slot(material, path)['index'] = index
        out['materials'].append(material)
    for level, (values, indices) in enumerate(levels):
        uv = values['TEXCOORD_0'].astype(np.float64)
        uv[:, 0] = (uv[:, 0] - box[0]) / (box[2] - box[0])
        uv[:, 1] = (uv[:, 1] - box[1]) / (box[3] - box[1])
        values['TEXCOORD_0'] = uv.astype(np.float32)
        attributes = {}
        for attribute, data in values.items():
            attributes[attribute] = add_accessor(out, out_binary, data, 5126, 'VEC3' if data.shape[1] == 3 else 'VEC2',
                                                 34962)
        index_accessor = add_accessor(out, out_binary, indices.astype(np.uint16 if indices.max() < 65536
                                                                      else np.uint32).reshape(-1, 1),
                                      5123 if indices.max() < 65536 else 5125, 'SCALAR', 34963)
        primitive = {'attributes': attributes, 'indices': index_accessor, 'material': 0}
        if variants:
            primitive['extensions'] = {'KHR_materials_variants': {'mappings': [
                {'material': variant, 'variants': [variant]} for variant in range(len(variants))]}}
        out['meshes'].append({'name': name if level == 0 else f'{name}_LOD{level}', 'primitives': [primitive]})
    if len(levels) > 1:
        out['meshes'][0]['extras'] = {'openyamm_lods': list(range(1, len(levels)))}
    if variants:
        out['extensionsUsed'] = ['KHR_materials_variants']
        out['extensions'] = {'KHR_materials_variants': {'variants': copy.deepcopy(variants)}}
    return out, out_binary


def add_view(out, out_binary, data, target=None):
    out_binary.extend(b'\0' * (-len(out_binary) % 4))
    view = {'buffer': 0, 'byteOffset': len(out_binary), 'byteLength': len(data)}
    if target is not None:
        view['target'] = target
    out['bufferViews'].append(view)
    out_binary.extend(data)
    return len(out['bufferViews']) - 1


def add_accessor(out, out_binary, values, component_type, kind, target):
    values = np.ascontiguousarray(values)
    accessor = {'bufferView': add_view(out, out_binary, values.tobytes(), target), 'componentType': component_type,
                'count': int(len(values)), 'type': kind}
    if kind == 'VEC3':
        accessor['min'] = values.min(axis=0).tolist()
        accessor['max'] = values.max(axis=0).tolist()
    out['accessors'].append(accessor)
    return len(out['accessors']) - 1


if __name__ == '__main__':
    main()
