#!/usr/bin/env python3
"""Link the LOD meshes of a static (unskinned) GLB, as exported from Blender, into the engine's LOD chain.

Blender exports one node per object. For every base node `Name` with sibling nodes `Name_LOD1` .. `Name_LOD3`, the
LOD meshes are listed in the base mesh's extras (`openyamm_lods`, coarsest last) and the LOD nodes are removed from
the scene, so each LOD mesh is referenced only by its owning mesh. LOD nodes must share the base node's transform.
Triangle counts must decrease from level to level. Normal maps are written in the glTF convention
(normal_map_convention.py). Usage: link_static_model_lods.py in.glb out.glb"""
import json
import re
import struct
import sys
from pathlib import Path

import normal_map_convention


def read_glb(path):
    raw = path.read_bytes()
    magic, version, length = struct.unpack_from('<III', raw)
    if magic != 0x46546c67 or version != 2 or length != len(raw):
        raise ValueError('Expected a complete GLB 2 file')
    size, kind = struct.unpack_from('<II', raw, 12)
    if kind != 0x4e4f534a:
        raise ValueError('Expected JSON followed by an embedded BIN chunk')
    return json.loads(raw[20:20 + size]), bytearray(raw[28 + size:])


def write_glb(path, document, binary):
    binary += b'\0' * (-len(binary) % 4)
    document['buffers'][0]['byteLength'] = len(binary)
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    path.write_bytes(struct.pack('<III', 0x46546c67, 2, 28 + len(encoded) + len(binary))
                     + struct.pack('<II', len(encoded), 0x4e4f534a) + encoded
                     + struct.pack('<II', len(binary), 0x004e4942) + binary)
    # Normal maps leave in the glTF convention (+y up); a Meshy-textured part is +y down.
    for name, correlation, flipped in normal_map_convention.convert(path, path):
        if flipped:
            print(f'{path.name}: normal map {name} {correlation:+.2f} converted to the glTF convention (+y up)')


def triangles(document, mesh_index):
    return sum(document['accessors'][p['indices']]['count'] // 3 for p in document['meshes'][mesh_index]['primitives'])


def transform(node):
    return (node.get('translation', [0, 0, 0]), node.get('rotation', [0, 0, 0, 1]), node.get('scale', [1, 1, 1]),
            node.get('matrix'))


def link(document):
    nodes = document['nodes']
    by_name = {node.get('name'): index for index, node in enumerate(nodes)}
    pattern = re.compile(r'^(.*)_LOD([1-3])$')
    chains = {}
    for index, node in enumerate(nodes):
        match = pattern.match(node.get('name', ''))
        if match is None:
            continue
        base = match.group(1)
        if base not in by_name or 'mesh' not in nodes[by_name[base]] or 'mesh' not in node:
            raise ValueError(f'LOD node {node.get("name")} has no base mesh node {base}')
        if transform(node) != transform(nodes[by_name[base]]):
            raise ValueError(f'LOD node {node.get("name")} must share the transform of {base}')
        chains.setdefault(base, {})[int(match.group(2))] = index
    removed = set()
    report = {}
    for base, levels in chains.items():
        if sorted(levels) != list(range(1, len(levels) + 1)):
            raise ValueError(f'{base}: LOD levels must be consecutive from 1, found {sorted(levels)}')
        base_mesh = nodes[by_name[base]]['mesh']
        lod_meshes = [nodes[levels[level]]['mesh'] for level in sorted(levels)]
        counts = [triangles(document, m) for m in [base_mesh] + lod_meshes]
        if not all(a > b for a, b in zip(counts, counts[1:])):
            raise ValueError(f'{base}: LOD triangle counts must decrease, found {counts}')
        document['meshes'][base_mesh].setdefault('extras', {})['openyamm_lods'] = lod_meshes
        removed.update(levels.values())
        report[base] = counts
    if not removed:
        raise ValueError('No Name_LOD1 nodes found')
    # Drop the LOD nodes and renumber every node reference.
    remap = {}
    kept = []
    for index, node in enumerate(nodes):
        if index not in removed:
            remap[index] = len(kept)
            kept.append(node)
    for node in kept:
        if 'children' in node:
            node['children'] = [remap[c] for c in node['children'] if c not in removed]
            if not node['children']:
                del node['children']
    for scene in document.get('scenes', []):
        scene['nodes'] = [remap[n] for n in scene['nodes'] if n not in removed]
    for skin in document.get('skins', []):
        skin['joints'] = [remap[j] for j in skin['joints']]
        if 'skeleton' in skin:
            skin['skeleton'] = remap[skin['skeleton']]
    for animation in document.get('animations', []):
        for channel in animation['channels']:
            if channel['target'].get('node') in removed:
                raise ValueError('LOD nodes must not be animated')
            channel['target']['node'] = remap[channel['target']['node']]
    document['nodes'] = kept
    return report


def main():
    source, destination = Path(sys.argv[1]), Path(sys.argv[2])
    document, binary = read_glb(source)
    report = link(document)
    write_glb(destination, document, binary)
    for base, counts in report.items():
        print('LOD triangles', base, counts)


if __name__ == '__main__':
    main()
