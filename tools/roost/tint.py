"""
Roost Print: give the app's greys a Rapid Roost blue tint, like rapidroost.net's night theme
(navy backgrounds, blue-grey panels and lines) instead of Creality's flat greys.

Every neutral grey in the interface code, the app's web pages and its icons is moved to the
brand blue's hue, keeping how light it was. Dark greys become navy, mid greys become slate blue,
light greys pick up a faint blue. Pure black and near-white are left as they are. Run recolor.py
first (it handles the green accents); this one only touches greys.

Profiles, colour pickers, palettes and the G-code preview colours (line types) are left alone.

Run from the repo root:  python tools/roost/tint.py
"""
import colorsys
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recolor  # noqa: E402

ROOT = recolor.ROOT
HUE = recolor.BLUE_HUE
DIRS = ['src/slic3r/GUI', 'src/slic3r/Config', 'resources/web', 'resources/images']
SKIP_NAME = re.compile(recolor.SKIP_NAME.pattern + r'|GCodeViewer|GCodeProcessor|LibVGCode|Thermal|Heat|SlicerBridge|MCPTool', re.I)
NIGHT = (0x07, 0x0C, 0x12)  # the site's darkest panel


def sat_for(l):
    # Matched to the site's night theme: navy surfaces fairly blue (#172839), slate fields
    # greyer (#4a6076), light lines only faintly blue (#d5dbe1).
    pts = [(0.0, 0.40), (0.18, 0.38), (0.40, 0.22), (0.62, 0.16), (1.0, 0.15)]
    for (l0, s0), (l1, s1) in zip(pts, pts[1:]):
        if l <= l1:
            return s0 + (l - l0) / (l1 - l0) * (s1 - s0)
    return pts[-1][1]


def tint(rgb):
    if rgb == (0, 0, 0):
        return rgb
    r, g, b = (c / 255 for c in rgb)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if max(rgb) - min(rgb) > 10 or s > 0.14:
        return rgb  # not a grey
    if l > 0.955:
        return rgb  # whites stay white
    if l < 0.03:
        return NIGHT
    nr, ng, nb = colorsys.hls_to_rgb(HUE, l, sat_for(l))
    return tuple(max(0, min(255, round(c * 255))) for c in (nr, ng, nb))


def main():
    recolor.RECOLOURS.append(tint)
    recolor.shift = tint
    total, files = 0, 0
    for d in DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            if any(p in dp.replace('\\', '/').split('/') for p in recolor.SKIP_PARTS):
                continue
            for fn in fns:
                ext = os.path.splitext(fn)[1].lower()
                if ext not in recolor.EXTS or SKIP_NAME.search(fn):
                    continue
                p = os.path.join(dp, fn)
                try:
                    text = open(p, 'rb').read().decode('utf-8')
                except (UnicodeDecodeError, OSError):
                    continue
                new, n = recolor.fix(text, ext)
                if n:
                    open(p, 'wb').write(new.encode('utf-8'))
                    total += n
                    files += 1
    print(f'tinted {total} greys in {files} files')


if __name__ == '__main__':
    main()
