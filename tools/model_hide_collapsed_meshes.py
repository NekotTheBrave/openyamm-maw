#!/usr/bin/env python3
"""Hide skinned meshes while their animation collapses them, so the renderer skips them instead of drawing them.

A creature that swaps its whole shape (a gargoyle crumbling into rubble) keeps both meshes and collapses the unused
one by scaling its bones to almost nothing. The engine still draws, skins and shadows every collapsed triangle; it
only skips a mesh whose own node matrix is singular (Engine::modelMatrixVisible). This tool finds, in every clip,
the key spans where all bones a mesh is weighted to are collapsed, and keys that mesh node's scale to 0 there
(1 elsewhere, STEP interpolation, only at the clip's existing key times, so no clip changes length).

A span is hidden only when the mesh is collapsed at both of its keys, so a mesh growing in or collapsing away stays
drawn. Rerunning replaces the keys written before. Rotation never makes a matrix singular, so only scale channels
are evaluated.

    tools/model_hide_collapsed_meshes.py <model.glb> [<more.glb> ...]      report only
    tools/model_hide_collapsed_meshes.py <export.glb> --write <out.glb>   write the keyed copy (may be the same path)

Run it on the project export before tools/install_model.py.
"""
import argparse
import bisect
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_actor_lods import accessor_bytes, append_accessor, read_glb, write_glb  # noqa: E402

COLLAPSED = 1e-9  # |scale x*y*z| of a bone's model-space matrix
FLOAT = 5126


def floats(document, binary, index):
    packed = accessor_bytes(document, binary, index)
    return list(struct.unpack(f'<{len(packed) // 4}f', packed))


def mesh_joints(document, binary, node):
    """Skin-joint node indices with any weight in the node's mesh, its LODs and its shadow meshes."""
    mesh = document['meshes'][node['mesh']]
    extras = mesh.get('extras', {})
    meshes = [mesh] + [document['meshes'][i] for i in extras.get('openyamm_lods', []) + extras.get('openyamm_shadow_lods', [])]
    skin = document['skins'][node['skin']]['joints']
    used = set()
    for each in meshes:
        for primitive in each['primitives']:
            for set_index in range(4):
                joints_key, weights_key = f'JOINTS_{set_index}', f'WEIGHTS_{set_index}'
                if joints_key not in primitive['attributes']:
                    break
                accessor = document['accessors'][primitive['attributes'][joints_key]]
                code = {5121: 'B', 5123: 'H'}[accessor['componentType']]
                joints = struct.iter_unpack('<' + code, accessor_bytes(document, binary, primitive['attributes'][joints_key]))
                weights = document['accessors'][primitive['attributes'][weights_key]]
                if weights['componentType'] != FLOAT:
                    raise ValueError('expected float skin weights')
                for (joint,), weight in zip(joints, floats(document, binary, primitive['attributes'][weights_key])):
                    if weight > 0.0:
                        used.add(skin[joint])
    return used


class ScaleSampler:
    def __init__(self, document, binary, sampler):
        self.times = floats(document, binary, sampler['input'])
        values = floats(document, binary, sampler['output'])
        self.values = [values[i * 3:i * 3 + 3] for i in range(len(self.times))]
        self.step = sampler.get('interpolation', 'LINEAR') == 'STEP'
        if sampler.get('interpolation') == 'CUBICSPLINE':
            raise ValueError('cubic spline scale channels are not supported')

    def product(self, time):
        right = bisect.bisect_right(self.times, time)
        left = max(right - 1, 0); right = min(right, len(self.times) - 1)
        value = self.values[left]
        if not self.step and right != left and self.times[right] > self.times[left]:
            amount = (time - self.times[left]) / (self.times[right] - self.times[left])
            value = [a + (b - a) * amount for a, b in zip(value, self.values[right])]
        return value[0] * value[1] * value[2]


def analyse(document, binary, written=()):
    """{mesh node index: {clip name: [(time, hidden), ...]}} plus the clip key times; ignores keys in `written`."""
    nodes = document['nodes']
    parents = {child: index for index, node in enumerate(nodes) for child in node.get('children', [])}
    rest = []
    for node in nodes:
        scale = node.get('scale', [1.0, 1.0, 1.0])
        rest.append(scale[0] * scale[1] * scale[2])
    skinned = {index: mesh_joints(document, binary, node) for index, node in enumerate(nodes)
               if 'mesh' in node and 'skin' in node}
    result = {index: {} for index in skinned}
    for animation in document.get('animations', []):
        samplers = {}
        times = set()
        for number, channel in enumerate(animation['channels']):
            if (animation['name'], number) in written:
                continue
            sampler = animation['samplers'][channel['sampler']]
            times.update(floats(document, binary, sampler['input']))
            if channel['target']['path'] == 'scale':
                samplers[channel['target']['node']] = ScaleSampler(document, binary, sampler)
        times = sorted(times)

        def collapsed(joint, time):
            product, index = 1.0, joint
            while index is not None:
                product *= samplers[index].product(time) if index in samplers else rest[index]
                index = parents.get(index)
            return abs(product) < COLLAPSED

        for index, joints in skinned.items():
            flags = [all(collapsed(joint, time) for joint in joints) for time in times]
            result[index][animation['name']] = [(time, flags[i] and (i + 1 == len(times) or flags[i + 1]))
                                                for i, time in enumerate(times)]
    return result


def spans(keys):
    """Hidden time ranges of one clip, for the report."""
    out, start = [], None
    for time, hidden in keys:
        if hidden and start is None:
            start = time
        elif not hidden and start is not None:
            out.append((start, time)); start = None
    if start is not None:
        out.append((start, keys[-1][0]))
    return out


def written_channels(document):
    marked = set()
    for animation in document.get('animations', []):
        for number, channel in enumerate(animation['channels']):
            node = document['nodes'][channel['target']['node']]
            if channel['target']['path'] == 'scale' and 'mesh' in node and 'skin' in node:
                marked.add((animation['name'], number))
    return marked


def triangles(document, node):
    return sum(document['accessors'][p['indices']]['count'] // 3 for p in document['meshes'][node['mesh']]['primitives'])


def report(path, document, result):
    print(f'{path}:')
    for index, clips in result.items():
        node = document['nodes'][index]
        hidden = {clip: spans(keys) for clip, keys in clips.items()}
        label = f"  {node.get('name', index)} ({triangles(document, node)} tris)"
        if not any(hidden.values()):
            print(label + ': never collapsed')
            continue
        print(label + ':')
        for clip, ranges in hidden.items():
            end = clips[clip][-1][0] if clips[clip] else 0.0
            text = 'hidden ' + ', '.join(f'{a:.3f}-{b:.3f}' for a, b in ranges) if ranges else 'drawn'
            print(f'    {clip} ({end:.3f} s): {text}')


def write(document, binary, result):
    binary = bytearray(binary)
    marked = written_channels(document)
    for animation in document.get('animations', []):
        keep = [c for number, c in enumerate(animation['channels']) if (animation['name'], number) not in marked]
        # drop the replaced samplers too (their accessors stay unreferenced in the buffer)
        used = sorted({c['sampler'] for c in keep})
        remap = {old: new for new, old in enumerate(used)}
        animation['samplers'] = [animation['samplers'][old] for old in used]
        for channel in keep:
            channel['sampler'] = remap[channel['sampler']]
        animation['channels'] = keep
    for index, clips in result.items():
        node = document['nodes'][index]
        if not any(hidden for keys in clips.values() for _, hidden in keys):
            continue
        if node.get('children') or 'matrix' in node:
            raise ValueError(f"{node.get('name')}: a keyed mesh node must be a TRS leaf")
        for animation in document['animations']:
            keys = clips[animation['name']] or [(0.0, False)]
            # STEP keys only where visibility changes
            compact = [keys[0]] + [k for previous, k in zip(keys, keys[1:]) if k[1] != previous[1]]
            times = [time for time, _ in compact]
            values = [component for _, hidden in compact for component in ([0.0] * 3 if hidden else [1.0] * 3)]
            source = {'componentType': FLOAT, 'count': len(times), 'type': 'SCALAR',
                      'min': [min(times)], 'max': [max(times)]}
            time_index = append_accessor(document, binary, source, struct.pack(f'<{len(times)}f', *times))
            value_index = append_accessor(document, binary, {'componentType': FLOAT, 'count': len(times), 'type': 'VEC3'},
                                          struct.pack(f'<{len(values)}f', *values))
            animation['samplers'].append({'input': time_index, 'output': value_index, 'interpolation': 'STEP'})
            animation['channels'].append({'sampler': len(animation['samplers']) - 1,
                                          'target': {'node': index, 'path': 'scale'}})
    return bytes(binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('models', type=Path, nargs='+')
    parser.add_argument('--write', type=Path, help='keyed copy of the single input model')
    args = parser.parse_args()
    if args.write and len(args.models) != 1:
        raise SystemExit('--write takes one input model')
    for path in args.models:
        document, binary = read_glb(path)
        result = analyse(document, binary, written_channels(document))
        report(path, document, result)
        if args.write:
            binary = write(document, binary, result)
            write_glb(args.write, document, binary)
            check, check_binary = read_glb(args.write)
            if analyse(check, check_binary, written_channels(check)) != result:
                raise SystemExit('the written keys changed the collapse analysis')
            print(f'wrote {args.write}')


if __name__ == '__main__':
    main()
