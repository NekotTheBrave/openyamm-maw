#!/usr/bin/env python3
"""Check runtime effect removal against the selected authored demon, including binary sample parity."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

from prepare_actor_glb import prepare


def read_glb(path):
    raw = path.read_bytes()
    size = struct.unpack_from('<I', raw, 12)[0]
    return json.loads(raw[20:20 + size]), raw[28 + size:]


def accessor_bytes(document, binary, index):
    accessor = document['accessors'][index]
    view = document['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    components = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[accessor['type']]
    size = {5121: 1, 5123: 2, 5125: 4, 5126: 4}[accessor['componentType']] * components
    stride = view.get('byteStride', size)
    return b''.join(binary[offset + i * stride:offset + i * stride + size] for i in range(accessor['count']))


class RuntimeActorGlbTests(unittest.TestCase):
    def test_effect_omission_preserves_body_and_all_remaining_animation_samples(self):
        root = Path(__file__).resolve().parent.parent
        source = root / ('level_generation/creatures/mm6_demon_meshy_reference_20261004/'
                         'texturing_fixed_20261005/mm6_demon_textured.glb')
        before, original = read_glb(source)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'runtime.glb'
            prepare(source, output, ['Demon_Death_Plume'])
            after, binary = read_glb(output)
            self.assertEqual(before['materials'], after['materials'])
            self.assertEqual(len(after['animations']), 9)
            for index, node in enumerate(before['nodes']):
                actual = after['nodes'][index]
                if node.get('name') == 'Demon_Death_Plume':
                    self.assertNotIn('mesh', actual)
                    self.assertNotIn('weights', actual)
                elif 'mesh' in node:
                    left = before['meshes'][node['mesh']]['primitives']
                    right = after['meshes'][actual['mesh']]['primitives']
                    self.assertEqual(len(left), len(right))
                    for a, b in zip(left, right):
                        for key in a['attributes']:
                            self.assertEqual(accessor_bytes(before, original, a['attributes'][key]),
                                             accessor_bytes(after, binary, b['attributes'][key]))
                        self.assertEqual(accessor_bytes(before, original, a['indices']),
                                         accessor_bytes(after, binary, b['indices']))
            for a, b in zip(before['animations'], after['animations']):
                self.assertEqual(a['name'], b['name'])
                expected = [c for c in a['channels']
                            if before['nodes'][c['target']['node']].get('name') != 'Demon_Death_Plume'
                            or c['target']['path'] != 'weights']
                self.assertEqual(len(expected), len(b['channels']))
                for left, right in zip(expected, b['channels']):
                    self.assertEqual(left['target'], right['target'])
                    s = a['samplers'][left['sampler']]
                    t = b['samplers'][right['sampler']]
                    self.assertEqual(s.get('interpolation'), t.get('interpolation'))
                    for key in ('input', 'output'):
                        self.assertEqual(accessor_bytes(before, original, s[key]),
                                         accessor_bytes(after, binary, t[key]))
            for left, right in zip(before['skins'], after['skins']):
                self.assertEqual(left['joints'], right['joints'])
                self.assertEqual(accessor_bytes(before, original, left['inverseBindMatrices']),
                                 accessor_bytes(after, binary, right['inverseBindMatrices']))
            with self.assertRaisesRegex(ValueError, 'Expected one mesh node'):
                prepare(source, output, ['Missing_Effect'])


if __name__ == '__main__':
    unittest.main()
