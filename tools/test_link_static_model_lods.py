#!/usr/bin/env python3
"""Check static LOD linking on a minimal document: extras, removed LOD nodes and renumbered node references."""
import copy
import unittest

from link_static_model_lods import link


def document(lod_triangles, lod_translation=None):
    accessors = [{'count': count * 3} for count in lod_triangles]
    meshes = [{'primitives': [{'indices': index}]} for index in range(len(lod_triangles))]
    nodes = [{'name': 'Rock', 'mesh': 0}]
    for level in range(1, len(lod_triangles)):
        node = {'name': f'Rock_LOD{level}', 'mesh': level}
        if lod_translation is not None:
            node['translation'] = lod_translation
        nodes.append(node)
    nodes.append({'name': 'Marker', 'children': [0]})
    return {'accessors': accessors, 'meshes': meshes, 'nodes': nodes,
            'scenes': [{'nodes': list(range(len(nodes)))}]}


class LinkStaticModelLodsTests(unittest.TestCase):
    def test_lods_become_mesh_extras_and_their_nodes_are_removed(self):
        linked = document([2400, 960, 290, 96])
        report = link(linked)
        self.assertEqual(report, {'Rock': [2400, 960, 290, 96]})
        self.assertEqual(linked['meshes'][0]['extras']['openyamm_lods'], [1, 2, 3])
        self.assertEqual([node['name'] for node in linked['nodes']], ['Rock', 'Marker'])
        self.assertEqual(linked['nodes'][1]['children'], [0])
        self.assertEqual(linked['scenes'][0]['nodes'], [0, 1])

    def test_rejects_non_decreasing_triangles(self):
        with self.assertRaises(ValueError):
            link(document([900, 960, 290, 96]))

    def test_rejects_lod_with_its_own_transform(self):
        with self.assertRaises(ValueError):
            link(document([2400, 960], lod_translation=[0, 1, 0]))

    def test_rejects_gaps_in_the_chain(self):
        broken = document([2400, 960, 290])
        broken['nodes'][2]['name'] = 'Rock_LOD3'
        with self.assertRaises(ValueError):
            link(copy.deepcopy(broken))


if __name__ == '__main__':
    unittest.main()
