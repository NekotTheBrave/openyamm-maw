#!/usr/bin/env python3
"""Convert a GLB's normal maps to the glTF convention (+x along +u, +y up the image), in place or to a new file.

Meshy's retexture returns +y-down (DirectX) normal maps, which fs_model.sc would light from the wrong side. The
convention is measured, not assumed: a tangent-space normal map is the slope field of a height map, so its mixed
derivatives agree (d(dh/dx)/dy = d(dh/dy)/dx) only when y is read in the map's own direction. The correlation of the
two over the detailed texels is near +1 for a +y-up map and near -1 for a +y-down one; a map at or below -FLIP has its
green channel inverted, so a converted map measures positive and a second run leaves it alone. Maps between -FLIP and
+FLIP are left as they are: leaf cards, impostors and flat maps measure -0.25..+0.15. Detailed maps measure beyond
0.6; downscaled ones weaken (Meshy creature maps resampled to 1K: -0.18..-0.45), so the actor pipeline, whose maps all
come from Meshy, passes a lower threshold (prepare_actor_glb.py MESHY_FLIP).
"""
import argparse
import io
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image

FLIP = 0.5


def _sobel(values, axis):
    """Central difference along axis, smoothed 1-2-1 across it (edges repeat)."""
    padded = np.pad(values, 1, mode='edge')
    if axis == 0:
        difference = padded[2:, :] - padded[:-2, :]
        return difference[:, :-2] + 2.0 * difference[:, 1:-1] + difference[:, 2:]
    difference = padded[:, 2:] - padded[:, :-2]
    return difference[:-2, :] + 2.0 * difference[1:-1, :] + difference[2:, :]


def measure(rgb):
    """Correlation of the map's mixed slope derivatives read +y up: +1 glTF convention, -1 +y down."""
    normal = rgb.astype(np.float64) / 255.0 * 2.0 - 1.0
    z = np.clip(normal[..., 2], 0.2, 1.0)
    slope_x = -normal[..., 0] / z
    slope_y = -normal[..., 1] / z
    d_slope_x_up = -_sobel(slope_x, 0)       # rows run down the image
    d_slope_y_right = _sobel(slope_y, 1)
    usable = (np.abs(normal[..., 0]) < 0.95) & (normal[..., 2] > 0.3)
    usable[1:] &= usable[:-1]
    usable[:-1] &= usable[1:]
    usable[:, 1:] &= usable[:, :-1]
    usable[:, :-1] &= usable[:, 1:]
    usable &= np.abs(d_slope_x_up) + np.abs(d_slope_y_right) > 0.02
    if usable.sum() < 1000:
        return 0.0
    return float(np.corrcoef(d_slope_x_up[usable], d_slope_y_right[usable])[0, 1])


def read_glb(data):
    magic, version, length = struct.unpack_from('<III', data)
    if magic != 0x46546C67 or version != 2 or length != len(data):
        raise ValueError('expected a complete glTF 2 GLB')
    json_length, json_kind = struct.unpack_from('<II', data, 12)
    document = json.loads(data[20:20 + json_length])
    bin_length, bin_kind = struct.unpack_from('<II', data, 20 + json_length)
    if json_kind != 0x4E4F534A or bin_kind != 0x004E4942 or 28 + json_length + bin_length != len(data):
        raise ValueError('expected one JSON and one embedded BIN chunk')
    return document, data[28 + json_length:]


def write_glb(document, views):
    """Lay the buffer views out again (4-byte aligned) from their new contents."""
    payload = bytearray()
    for view, content in zip(document['bufferViews'], views):
        payload.extend(b'\0' * (-len(payload) % 4))
        view['byteOffset'] = len(payload)
        view['byteLength'] = len(content)
        payload.extend(content)
    document['buffers'][0]['byteLength'] = len(payload)
    payload.extend(b'\0' * (-len(payload) % 4))
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    return (struct.pack('<III', 0x46546C67, 2, 28 + len(encoded) + len(payload))
            + struct.pack('<II', len(encoded), 0x4E4F534A) + encoded
            + struct.pack('<II', len(payload), 0x004E4942) + payload)


def convert(source, destination, check=False, threshold=FLIP):
    """Returns [(image name, measured correlation, flipped)] for every normal map in the GLB; check: write nothing."""
    document, binary = read_glb(source.read_bytes())
    textures = document.get('textures', [])
    normal_images, other_images = set(), set()
    for material in document.get('materials', []):
        for key, info in _texture_slots(material):
            target = normal_images if key == 'normalTexture' else other_images
            target.add(textures[info['index']]['source'])
    if normal_images & other_images:
        raise ValueError(f'{source}: an image is both a normal map and another map')
    views = [binary[view.get('byteOffset', 0):view.get('byteOffset', 0) + view['byteLength']]
             for view in document.get('bufferViews', [])]
    report = []
    for index in sorted(normal_images):
        image = document['images'][index]
        if 'bufferView' not in image:
            raise ValueError(f'{source}: normal map {index} is not embedded')
        with Image.open(io.BytesIO(views[image['bufferView']])) as pixels:
            mode = pixels.mode
            rgba = np.array(pixels.convert('RGBA'))
        correlation = measure(rgba[..., :3])
        flipped = correlation <= -threshold
        if flipped and not check:
            rgba[..., 1] = 255 - rgba[..., 1]
            encoded = io.BytesIO()
            Image.fromarray(rgba).convert(mode if mode in ('RGB', 'RGBA') else 'RGB').save(encoded, format='PNG')
            views[image['bufferView']] = encoded.getvalue()
            image['mimeType'] = 'image/png'
        report.append((image.get('name', str(index)), correlation, flipped))
    if check:
        pass
    elif any(flipped for _, _, flipped in report):
        destination.write_bytes(write_glb(document, views))
    elif destination != source:
        destination.write_bytes(source.read_bytes())
    return report


def _texture_slots(material):
    for key in ('normalTexture', 'occlusionTexture', 'emissiveTexture'):
        if key in material:
            yield key, material[key]
    for key, value in material.get('pbrMetallicRoughness', {}).items():
        if isinstance(value, dict) and 'index' in value:
            yield key, value
    for key, value in material.get('extras', {}).items():
        if isinstance(value, dict) and 'index' in value:
            yield key, value


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('glbs', type=Path, nargs='+', help='GLBs converted in place')
    parser.add_argument('--check', action='store_true', help='measure and report only')
    parser.add_argument('--threshold', type=float, default=FLIP, help='convert at or below minus this correlation')
    arguments = parser.parse_args()
    verb = 'needs conversion' if arguments.check else 'converted to +y up'
    for path in arguments.glbs:
        for name, correlation, flipped in convert(path, path, arguments.check, arguments.threshold):
            print(f'{path.name}: {name} {correlation:+.2f} {verb if flipped else "kept"}')
