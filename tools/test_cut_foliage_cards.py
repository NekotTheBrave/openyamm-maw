#!/usr/bin/env python3
"""Check foliage card outlines: they enclose every covered texel, stay inside their rectangle and trim a round spray."""
import unittest

import numpy as np

from cut_foliage_cards import encloses, outline, polygon_area


class FoliageCardOutlineTests(unittest.TestCase):
    def test_round_spray_outline_encloses_texels_and_trims_corners(self):
        size = 128
        y, x = np.mgrid[0:size, 0:size * 2]
        alpha = np.zeros((size, size * 2), np.uint8)
        # A round spray in the right half of a two-cell atlas.
        alpha[(x - size * 1.5) ** 2 + (y - size * 0.5) ** 2 < (size * 0.42) ** 2] = 255
        for vertices in (4, 5, 6, 8):
            polygon = outline(alpha, (0.5, 0.0, 1.0, 1.0), 32, vertices)
            self.assertLessEqual(len(polygon), vertices)
            self.assertTrue(((polygon >= 0.0) & (polygon <= 1.0)).all())
            ys, xs = np.nonzero(alpha[:, size:] >= 32)
            texels = np.c_[(xs + 0.5) / size, (ys + 0.5) / size]
            self.assertTrue(encloses(polygon, texels))
            self.assertLess(polygon_area(polygon), 0.9)

    def test_empty_cell_keeps_the_full_card(self):
        polygon = outline(np.zeros((64, 64), np.uint8), (0.0, 0.0, 1.0, 1.0), 32, 4)
        self.assertAlmostEqual(polygon_area(polygon), 1.0)


if __name__ == '__main__':
    unittest.main()
