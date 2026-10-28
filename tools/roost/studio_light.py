#!/usr/bin/env python3
"""Roost Print: light models with the shop, the same way rapidroost.net does.

The website lights the 3D preview with the shop's 360 light (an HDRI rendered in Blender), turned
so the counter runs square to the plate, plus a warm key light, and tone maps with AgX. This script
reads the same files (copied from the website repo's public/studio/ into tools/roost/studio/) and
bakes that lighting into the 3D view's object shader:

  - the 360 light as 9 spherical-harmonic numbers (soft light from every direction, exactly what a
    matte surface sees; the website's blurriest light level is the same thing),
  - the key light's direction and colour,

all turned into the slicer's frame (Z up, X right, Y to the back of the plate).

It writes them between the ROOST_STUDIO markers in resources/shaders/{110,140}/gouraud.fs.
Run it again if the website's studio changes: copy shop_360_light.bin (as shop_360_light.hdr),
studio.json and counter_mesh.bin from public/studio/ and run

    python tools/roost/studio_light.py
"""
import json
import math
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
STUDIO = os.path.join(HERE, "studio")
SHADERS = [os.path.join(ROOT, "resources", "shaders", v, "gouraud.fs") for v in ("110", "140")]

# The website's key light in the studio (ModelViewer.tsx, enterStudio): colour and intensity.
KEY_COLOUR_SRGB = (0xFF, 0xE7, 0xCF)
KEY_INTENSITY = 1.5

# Tuned for the slicer. On the website the part sits on the shop's warm wooden counter, in the room;
# here it's on a navy plate, where the full-strength, full-colour shop light washes filament colours
# out (yellow goes tan, black goes brown). So: the same light from the same directions, but mostly
# colour-balanced (BALANCE 1 = neutral, 0 = the website's exact warmth), and
# a stronger key light so the shape reads. The shader adds AgX's "punchy" look (Blender's AgX Punchy).
BALANCE = 0.85
SHOP_STRENGTH = 1.0
KEY_BOOST = 2.4


def read_hdr(path):
    """Radiance .hdr (RGBE, new-style run-length rows) -> float32 array (H, W, 3), top row first."""
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    flip_y = False
    while True:
        end = data.index(b"\n", pos)
        line = data[pos:end].decode("ascii", "replace").strip()
        pos = end + 1
        m = re.match(r"([-+])Y (\d+) ([-+])X (\d+)", line)
        if m:
            if m.group(1) != "-" or m.group(3) != "+":
                raise SystemExit("Only top-down, left-to-right .hdr files are supported (%s)" % line)
            h, w = int(m.group(2)), int(m.group(4))
            break
    out = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        if data[pos] == 2 and data[pos + 1] == 2 and (data[pos + 2] << 8 | data[pos + 3]) == w:
            pos += 4
            for c in range(4):
                x = 0
                while x < w:
                    n = data[pos]
                    pos += 1
                    if n > 128:
                        n -= 128
                        out[y, x:x + n, c] = data[pos]
                        pos += 1
                    else:
                        out[y, x:x + n, c] = np.frombuffer(data, np.uint8, n, pos)
                        pos += n
                    x += n
        else:  # flat (old-style) row
            out[y] = np.frombuffer(data, np.uint8, w * 4, pos).reshape(w, 4)
            pos += w * 4
    e = out[..., 3].astype(np.int32)
    scale = np.where(e > 0, np.ldexp(1.0, e - 136), 0.0).astype(np.float64)  # 2^(e-128) / 256
    rgb = out[..., :3].astype(np.float64) * scale[..., None]
    return rgb


def convex_hull(points):
    p = sorted(points)
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for q in p:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], q) <= 0:
            lower.pop()
        lower.append(q)
    for q in reversed(p):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], q) <= 0:
            upper.pop()
        upper.append(q)
    return lower[:-1] + upper[:-1]


def counter_angle(mesh_path, vertices):
    """studio.client.ts counterLine(): which way the real counter runs (its top's tightest rectangle)."""
    with open(mesh_path, "rb") as f:
        data = struct.unpack("<%df" % (vertices * 8), f.read(vertices * 8 * 4))
    top = max(data[i * 8 + 1] for i in range(vertices))
    pts = [(data[i * 8], data[i * 8 + 2]) for i in range(vertices) if data[i * 8 + 1] > top - 2]
    hull = convex_hull(pts)
    best_area, best_angle = math.inf, 0.0
    for i in range(len(hull)):
        x0, z0 = hull[i]
        x1, z1 = hull[(i + 1) % len(hull)]
        ln = math.hypot(x1 - x0, z1 - z0)
        if ln < 1:
            continue
        ux, uz = (x1 - x0) / ln, (z1 - z0) / ln
        a = [x * ux + z * uz for x, z in hull]
        b = [-x * uz + z * ux for x, z in hull]
        area = (max(a) - min(a)) * (max(b) - min(b))
        if area >= best_area:
            continue
        angle = math.atan2(uz, ux) if max(a) - min(a) >= max(b) - min(b) else math.atan2(ux, -uz)
        if math.cos(angle) < 0:
            angle += math.pi
        best_area, best_angle = area, angle
    return best_angle


def rot_y(v, t):
    """three.js applyAxisAngle(+Y, t) / makeRotationY(t)."""
    x, y, z = v
    return np.array([x * math.cos(t) + z * math.sin(t), y, -x * math.sin(t) + z * math.cos(t)])


def to_slicer(v):
    """three.js scene (Y up; the plate's +y is -z) -> slicer (Z up)."""
    return np.array([v[0], -v[2], v[1]])


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def main():
    with open(os.path.join(STUDIO, "studio.json")) as f:
        meta = json.load(f)
    hdr = read_hdr(os.path.join(STUDIO, "shop_360_light.hdr"))
    h, w, _ = hdr.shape

    # How the shop is turned round the plate (studio.client.ts loadStudio).
    if meta.get("counter") and meta.get("counterMask") and meta.get("depth"):
        yaw = counter_angle(os.path.join(STUDIO, meta["counter"]["mesh"]), meta["counter"]["vertices"]) + math.pi / 2
    else:
        old = np.array([-1.3, 0, -1.6]) / math.hypot(1.3, 1.6)
        yaw = math.atan2(-old[2], old[0])
    view = np.array([math.cos(yaw), 0.0, -math.sin(yaw)])
    cx, cy = meta["center"]
    kx, ky, kz = meta["key"]
    turn = math.atan2(cx * ky - cy * kx, cx * kx + cy * ky)
    across = rot_y(view, turn)
    lift = min(max(math.asin(kz / math.sqrt(kx * kx + ky * ky + kz * kz)), 0.75), 1.2)
    key_dir = np.array([across[0] * math.cos(lift), math.sin(lift), across[2] * math.cos(lift)])
    key_dir /= np.linalg.norm(key_dir)

    # Every pixel's direction in the 360 (three.js equirectUv, top row = up), turned by the yaw
    # (scene.environmentRotation.y), then into the slicer's frame.
    v = 1.0 - (np.arange(h) + 0.5) / h
    u = (np.arange(w) + 0.5) / w
    lat = (v - 0.5) * math.pi
    lon = (u - 0.5) * 2 * math.pi
    lat, lon = np.meshgrid(lat, lon, indexing="ij")
    ex, ey, ez = np.cos(lat) * np.cos(lon), np.sin(lat), np.cos(lat) * np.sin(lon)
    # world = R_y(+yaw) * env
    wx = ex * math.cos(yaw) + ez * math.sin(yaw)
    wy = ey
    wz = -ex * math.sin(yaw) + ez * math.cos(yaw)
    sx, sy, sz = wx, -wz, wy  # slicer frame
    d_omega = (2 * math.pi / w) * (math.pi / h) * np.cos(lat)

    # Real spherical harmonics, bands 0-2.
    basis = [
        0.282095 * np.ones_like(sx),
        0.488603 * sy, 0.488603 * sz, 0.488603 * sx,
        1.092548 * sx * sy, 1.092548 * sy * sz, 0.315392 * (3 * sz * sz - 1), 1.092548 * sx * sz, 0.546274 * (sx * sx - sy * sy),
    ]
    band = [0, 1, 1, 1, 2, 2, 2, 2, 2]
    a_hat = [math.pi, 2 * math.pi / 3, math.pi / 4]
    coeffs = []
    for b, Y in zip(band, basis):
        L = (hdr * (Y * d_omega)[..., None]).sum(axis=(0, 1))
        # Irradiance / pi: what a white matte surface sends back (the website's diffuse from the 360).
        coeffs.append(L * a_hat[b] / math.pi)

    # Colour balance and strength (see BALANCE above): scale each channel towards the light's
    # average brightness, keeping where the light comes from.
    avg = coeffs[0]
    luma = float(np.dot(avg, [0.2126, 0.7152, 0.0722]))
    gain = (luma / avg) ** BALANCE * SHOP_STRENGTH
    coeffs = [c * gain for c in coeffs]

    key = to_slicer(key_dir)
    key_col = np.array([srgb_to_linear(c) for c in KEY_COLOUR_SRGB]) * KEY_INTENSITY * KEY_BOOST

    def evaluate(n):
        x, y, z = n
        bs = [0.282095, 0.488603 * y, 0.488603 * z, 0.488603 * x, 1.092548 * x * y, 1.092548 * y * z,
              0.315392 * (3 * z * z - 1), 1.092548 * x * z, 0.546274 * (x * x - y * y)]
        return sum(c * b for c, b in zip(coeffs, bs))

    print("yaw %.4f rad, key light (slicer) %s" % (yaw, np.round(key, 4)))
    for name, n in (("up", (0, 0, 1)), ("down", (0, 0, -1)), ("front", (0, -1, 0)), ("back", (0, 1, 0)),
                    ("left", (-1, 0, 0)), ("right", (1, 0, 0))):
        print("  diffuse light from %-5s %s" % (name, np.round(evaluate(n), 3)))

    def vec3(c):
        return "vec3(%.6f, %.6f, %.6f)" % tuple(c)

    block = ["// ROOST_STUDIO_BEGIN (written by tools/roost/studio_light.py from the website's shop studio; don't edit)"]
    for i, c in enumerate(coeffs):
        block.append("const vec3 ROOST_SH%d = %s;" % (i, vec3(c)))
    block.append("const vec3 ROOST_KEY_DIR = %s;" % vec3(key))
    block.append("const vec3 ROOST_KEY_COLOR = %s;" % vec3(key_col))
    block.append("// ROOST_STUDIO_END")
    text = "\n".join(block)

    for path in SHADERS:
        with open(path, encoding="utf-8") as f:
            src = f.read()
        new, n = re.subn(r"// ROOST_STUDIO_BEGIN.*?// ROOST_STUDIO_END", lambda m: text, src, flags=re.S)
        if n != 1:
            raise SystemExit("No ROOST_STUDIO markers in %s" % path)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(new)
        print("wrote", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    sys.exit(main())
