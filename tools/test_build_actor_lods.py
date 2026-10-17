#!/usr/bin/env python3
"""Check installed LODs against the original authored runtime export."""
import json
from pathlib import Path
import tempfile
import unittest

from build_actor_lods import read_glb, accessor_bytes
from prepare_actor_glb import prepare


class ActorLodTests(unittest.TestCase):
    def test_original_samples_and_merged_shadow_geometry_are_preserved(self):
        root = Path(__file__).resolve().parent.parent
        source = root / ('level_generation/creatures/mm6_demon_meshy_reference_20261004/'
                         'texturing_fixed_20261005/mm6_demon_textured.glb')
        after, binary = read_glb(root / 'assets_dev/worlds/mm6/models/mm6_demon.glb')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'original.glb'
            prepare(source, path, ['Demon_Death_Plume'])
            before, original = read_glb(path)
            for key in ('nodes', 'skins', 'animations', 'materials', 'images'):
                self.assertEqual(before[key], after[key])
            for index in range(len(before['accessors'])):
                self.assertEqual(accessor_bytes(before, original, index), accessor_bytes(after, binary, index))
            owner = next(m for m in after['meshes'] if m.get('extras', {}).get('openyamm_lods'))
            meshes = [after['meshes'].index(owner)] + owner['extras']['openyamm_lods']
            self.assertEqual(len(meshes), 4)
            for color, shadow in zip(meshes, owner['extras']['openyamm_shadow_lods']):
                primitives = after['meshes'][color]['primitives']
                combined = after['meshes'][shadow]['primitives']
                self.assertEqual(len(combined), 1)
                for semantic in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0'):
                    self.assertEqual(b''.join(accessor_bytes(after, binary, p['attributes'][semantic])
                                             for p in primitives),
                                     accessor_bytes(after, binary, combined[0]['attributes'][semantic]))
                self.assertEqual(sum(after['accessors'][p['indices']]['count'] for p in primitives),
                                 after['accessors'][combined[0]['indices']]['count'])


if __name__ == '__main__':
    unittest.main()
