#!/usr/bin/env python3
"""Cut the alpha-tested cards of a static GLB to the shape of their texture. Every card of an alpha-masked
(MASK) primitive — two triangles over an axis-aligned rectangle of texture coordinates — is
replaced by a fan over a convex outline of the texels its rectangle covers (alpha >= threshold, a small margin added),
so the depth prepass and shading skip most of the card's transparent area. Every float vertex attribute is
interpolated bilinearly over the card, so normals, occlusion colours and texture coordinates follow the old card
exactly. Triangles that do not pair into such a card are kept as they are.
Camera-facing impostors (openyamm_billboard) are cut too, with their own vertex count: one quad per far tree.
Usage: cut_foliage_cards.py in.glb out.glb [--vertices 4] [--billboard-vertices 6] [--threshold 32]"""
import argparse
import io
import itertools
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.spatial import ConvexHull

from link_static_model_lods import read_glb, write_glb

COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
TYPES = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}


def accessor_array(document, binary, index):
    accessor = document['accessors'][index]
    view = document['bufferViews'][accessor['bufferView']]
    dtype = np.dtype(TYPES[accessor['componentType']])
    components = COMPONENTS[accessor['type']]
    if view.get('byteStride', dtype.itemsize * components) != dtype.itemsize * components:
        raise ValueError('interleaved accessors are not supported')
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    data = np.frombuffer(bytes(binary[offset:offset + accessor['count'] * components * dtype.itemsize]), dtype)
    return data.reshape(accessor['count'], components)


def append_accessor(document, binary, values, accessor_type, component_type, target):
    binary.extend(b'\0' * (-len(binary) % 4))
    packed = np.ascontiguousarray(values, TYPES[component_type]).tobytes()
    document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(packed),
                                    'target': target})
    binary.extend(packed)
    accessor = {'bufferView': len(document['bufferViews']) - 1, 'componentType': component_type,
                'count': int(len(values)), 'type': accessor_type}
    if accessor_type == 'VEC3' and component_type == 5126:
        accessor['min'] = np.asarray(values).min(axis=0).tolist()
        accessor['max'] = np.asarray(values).max(axis=0).tolist()
    document['accessors'].append(accessor)
    return len(document['accessors']) - 1


def image_alpha(document, binary, material):
    texture = material['pbrMetallicRoughness']['baseColorTexture']['index']
    image = document['images'][document['textures'][texture]['source']]
    view = document['bufferViews'][image['bufferView']]
    data = bytes(binary[view.get('byteOffset', 0):view.get('byteOffset', 0) + view['byteLength']])
    return np.asarray(Image.open(io.BytesIO(data)).convert('RGBA'))[..., 3]


def intersect(a0, a1, b0, b1):
    """Meeting point of line a (through a0, a1) and line b, ahead of a1 along a; None for parallel lines."""
    da, db = a1 - a0, b1 - b0
    denominator = da[0] * db[1] - da[1] * db[0]
    if abs(denominator) < 1e-12:
        return None
    t = ((b0[0] - a0[0]) * db[1] - (b0[1] - a0[1]) * db[0]) / denominator
    return a0 + da * t if t >= 1.0 - 1e-9 else None


def polygon_area(polygon):
    x, y = polygon[:, 0], polygon[:, 1]
    return abs(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1))) * 0.5


def encloses(polygon, points):
    edges = np.roll(polygon, -1, axis=0) - polygon
    for point in points:
        cross = edges[:, 0] * (point[1] - polygon[:, 1]) - edges[:, 1] * (point[0] - polygon[:, 0])
        if (cross < -1e-6).any() and (cross > 1e-6).any():
            return False
    return True


def reduce(hull, target):
    """Replace the edge whose neighbours' extensions meet adding the least area, until `target` vertices remain."""
    while len(hull) > target:
        n = len(hull)
        best = None
        for edge in range(n):
            a0, a1 = np.array(hull[edge - 1]), np.array(hull[edge])
            b0, b1 = np.array(hull[(edge + 1) % n]), np.array(hull[(edge + 2) % n])
            meet = intersect(a0, a1, b1, b0)
            if meet is None or (meet < 0.0).any() or (meet > 1.0).any():
                continue
            area = abs((meet[0] - a1[0]) * (b0[1] - a1[1]) - (meet[1] - a1[1]) * (b0[0] - a1[0])) * 0.5
            if best is None or area < best[0]:
                best = (area, edge, meet)
        if best is None:
            return None
        _, edge, meet = best
        hull[edge] = meet.tolist()
        del hull[(edge + 1) % n]
    return hull


def outline(alpha, rect, threshold, vertices):
    """Convex polygon (rect-local coordinates in [0, 1]) enclosing the covered texels of a texture-space rectangle."""
    height, width = alpha.shape
    u0, v0, u1, v1 = rect
    x0, x1 = int(round(u0 * width)), int(round(u1 * width))
    y0, y1 = int(round(v0 * height)), int(round(v1 * height))
    cell = alpha[max(y0, 0):min(y1, height), max(x0, 0):min(x1, width)] >= threshold
    full = np.array([[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]])
    if cell.sum() < 3 or cell.shape[0] < 2 or cell.shape[1] < 2:
        return full
    ys, xs = np.nonzero(cell)
    corners = np.concatenate([np.c_[xs, ys], np.c_[xs + 1, ys], np.c_[xs, ys + 1], np.c_[xs + 1, ys + 1]])
    points = corners / np.array([cell.shape[1], cell.shape[0]], float)
    hull = points[ConvexHull(points).vertices]
    # Fewer vertices: first replace edges by the meeting point of their neighbours' extensions (least added area)
    # down to a dozen, then try every choice of `vertices` lines through those edges and keep the smallest enclosing
    # polygon inside the rectangle.
    hull = reduce(hull.tolist(), max(vertices, 12))
    if hull is None:
        return full
    hull = np.array(hull)
    if len(hull) > vertices:
        best = None
        for chosen in itertools.combinations(range(len(hull)), vertices):
            polygon = []
            for k, edge in enumerate(chosen):
                following = chosen[(k + 1) % vertices]
                meet = intersect(hull[edge], hull[(edge + 1) % len(hull)], hull[following],
                                 hull[(following + 1) % len(hull)])
                if meet is None or (meet < -1e-6).any() or (meet > 1 + 1e-6).any():
                    break
                polygon.append(meet)
            else:
                polygon = np.array(polygon)
                area = polygon_area(polygon)
                if (best is None or area < best[0]) and encloses(polygon, hull):
                    best = (area, polygon)
        if best is None:
            return full
        hull = best[1]
    polygon = np.clip(np.array(hull), 0.0, 1.0)
    centre = polygon.mean(axis=0)
    return np.clip(centre + (polygon - centre) * 1.04, 0.0, 1.0)


def cut_primitive(document, binary, primitive, alpha, threshold, vertices):
    attributes = {}
    for name, index in primitive['attributes'].items():
        accessor = document['accessors'][index]
        values = accessor_array(document, binary, index).astype(np.float64)
        if accessor['componentType'] != 5126:
            if not accessor.get('normalized'):
                raise ValueError(f'attribute {name} must be float or normalized to interpolate')
            values /= float(np.iinfo(TYPES[accessor['componentType']]).max)
        attributes[name] = values
    uv = attributes['TEXCOORD_0']
    position = attributes['POSITION']
    triangles = accessor_array(document, binary, primitive['indices']).reshape(-1, 3)
    key = lambda vertex: (tuple(np.round(position[vertex], 5)), tuple(np.round(uv[vertex], 5)))
    edges = {}
    for number, triangle in enumerate(triangles):
        for corner in range(3):
            edge = frozenset((key(triangle[corner]), key(triangle[(corner + 1) % 3])))
            edges.setdefault(edge, []).append(number)
    used = np.zeros(len(triangles), bool)
    out = {name: [] for name in attributes}
    out_indices = []
    rects = {}
    cards = 0

    def emit(values):
        first = len(out['POSITION'])
        for name in attributes:
            out[name].extend(values[name])
        return first

    for number, triangle in enumerate(triangles):
        if used[number]:
            continue
        partner = None
        for corner in range(3):
            edge = frozenset((key(triangle[corner]), key(triangle[(corner + 1) % 3])))
            others = [other for other in edges[edge] if other != number and not used[other]]
            if others:
                partner = others[0]
                break
        quad = None
        if partner is not None:
            corners = {}
            for vertex in list(triangle) + list(triangles[partner]):
                corners[key(vertex)] = vertex
            if len(corners) == 4:
                quad = list(corners.values())
                us = sorted({round(float(uv[v][0]), 5) for v in quad})
                vs = sorted({round(float(uv[v][1]), 5) for v in quad})
                if len(us) != 2 or len(vs) != 2:
                    quad = None
        if quad is None:
            first = emit({name: [values[v] for v in triangle] for name, values in attributes.items()})
            out_indices.extend([first, first + 1, first + 2])
            used[number] = True
            continue
        used[number] = used[partner] = True
        u0, u1 = float(min(uv[v][0] for v in quad)), float(max(uv[v][0] for v in quad))
        v0, v1 = float(min(uv[v][1] for v in quad)), float(max(uv[v][1] for v in quad))
        corner = {}
        for v in quad:
            corner[(abs(uv[v][0] - u1) < abs(uv[v][0] - u0), abs(uv[v][1] - v1) < abs(uv[v][1] - v0))] = v
        rect = (round(u0, 5), round(v0, 5), round(u1, 5), round(v1, 5))
        if rect not in rects:
            rects[rect] = outline(alpha, rect, threshold, vertices)
        polygon = rects[rect]
        values = {name: [] for name in attributes}
        for s, t in polygon:
            for name, data in attributes.items():
                value = ((1 - s) * (1 - t) * data[corner[(False, False)]] + s * (1 - t) * data[corner[(True, False)]]
                         + s * t * data[corner[(True, True)]] + (1 - s) * t * data[corner[(False, True)]])
                if name == 'NORMAL':
                    value = value / max(np.linalg.norm(value), 1e-9)
                values[name].append(value)
        first = emit(values)
        for k in range(1, len(polygon) - 1):
            out_indices.extend([first, first + k, first + k + 1])
        cards += 1
    for name, index in list(primitive['attributes'].items()):
        source = document['accessors'][index]
        values = np.array(out[name])
        if source['componentType'] != 5126:
            values = np.round(np.clip(values, 0.0, 1.0) * np.iinfo(TYPES[source['componentType']]).max)
        primitive['attributes'][name] = append_accessor(document, binary, values, source['type'],
                                                        source['componentType'], 34962)
        if source.get('normalized'):
            document['accessors'][primitive['attributes'][name]]['normalized'] = True
    index_type = 5123 if len(out['POSITION']) < 65536 else 5125
    primitive['indices'] = append_accessor(document, binary, np.array(out_indices), 'SCALAR', index_type, 34963)
    return cards, len(triangles), len(out_indices) // 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--vertices', type=int, default=4, help='outline vertices of a foliage card')
    parser.add_argument('--billboard-vertices', type=int, default=6,
                        help='outline vertices of an impostor quad (0 keeps it a quad)')
    parser.add_argument('--threshold', type=int, default=32)
    args = parser.parse_args()
    document, binary = read_glb(args.source)
    alphas = {}
    for mesh in document['meshes']:
        for primitive in mesh['primitives']:
            material = document['materials'][primitive.get('material', 0)]
            billboard = material.get('extras', {}).get('openyamm_billboard', False)
            vertices = args.billboard_vertices if billboard else args.vertices
            if material.get('alphaMode') != 'MASK' or vertices < 3:
                continue
            if primitive['material'] not in alphas:
                alphas[primitive['material']] = image_alpha(document, binary, material)
            cards, before, after = cut_primitive(document, binary, primitive, alphas[primitive['material']],
                                                 args.threshold, vertices)
            print(f"CUT {mesh.get('name')} {material['name']}: {cards} cards, triangles {before} -> {after}")
    # Old attribute data stays in the buffer unreferenced; rewrite compactly.
    write_glb(args.destination, *compact(document, binary))


def compact(document, binary):
    """Drop buffer views no accessor or image references (the replaced attributes)."""
    used = {a['bufferView'] for a in document['accessors'] if 'bufferView' in a}
    used |= {image['bufferView'] for image in document.get('images', []) if 'bufferView' in image}
    remap, packed, views = {}, bytearray(), []
    for index, view in enumerate(document['bufferViews']):
        if index not in used:
            continue
        packed.extend(b'\0' * (-len(packed) % 4))
        start = view.get('byteOffset', 0)
        data = binary[start:start + view['byteLength']]
        view = dict(view, byteOffset=len(packed))
        packed.extend(data)
        remap[index] = len(views)
        views.append(view)
    for accessor in document['accessors']:
        if 'bufferView' in accessor:
            accessor['bufferView'] = remap[accessor['bufferView']]
    for image in document.get('images', []):
        if 'bufferView' in image:
            image['bufferView'] = remap[image['bufferView']]
    document['bufferViews'] = views
    return document, packed


if __name__ == '__main__':
    main()
