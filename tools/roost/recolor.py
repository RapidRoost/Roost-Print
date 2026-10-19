"""
Roost Print: turn Creality Print's green accent into Rapid Roost blue.

Every green or teal accent colour in the app's code and resources (hex strings, 0xRRGGBB numbers,
wxColour/ImColor(r, g, b) calls, CSS rgb()/rgba()) is turned to the same blue hue, keeping how
light and how strong it was, so hovers, borders and pale backgrounds all stay in step. The main
greens map exactly onto the brand blue.

Printer and filament profiles, colour pickers and palettes are left alone: a green filament is
still green.

Run from the repo root:  python tools/roost/recolor.py   (safe to run again after an upstream merge)
"""
import colorsys, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from css_names import CSS_NAMES  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BRAND = (0x1E, 0x90, 0xD8)
BLUE_HUE = colorsys.rgb_to_hls(*(c / 255 for c in BRAND))[0]
# The main greens, mapped exactly (the rest are hue-shifted).
EXACT = {
    (0x17, 0xCC, 0x5F): BRAND, (0x1F, 0xCA, 0x63): BRAND, (0x18, 0xCC, 0x5C): BRAND, (0x00, 0xAE, 0x42): BRAND,
    (0x00, 0x96, 0x88): BRAND, (0x15, 0xBF, 0x59): (0x18, 0x77, 0xB4), (0x00, 0x89, 0x7B): (0x18, 0x77, 0xB4),
    (0x26, 0xA6, 0x9A): (0x4A, 0xA8, 0xE4),
}
DIRS = ['src/slic3r', 'resources/images', 'resources/web', 'resources/calib', 'resources/dailytip', 'resources/guide', 'resources/tooltip']
SKIP_PARTS = ['profiles', 'printers', 'filament_mixing', 'profiles_template']
SKIP_NAME = re.compile(r'(colou?r_?pick|palette|ColorDecompose|AMSColou?r|FilamentColou?r|ColorSpace|colormap)', re.I)
EXTS = {'.cpp', '.hpp', '.h', '.svg', '.css', '.html', '.js', '.json', '.vue', '.scss'}

def shift(rgb):
    if rgb in EXACT:
        return EXACT[rgb]
    r, g, b = (c / 255 for c in rgb)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    deg = h * 360
    if not (80 <= deg <= 178) or s < 0.3 or l < 0.12 or l > 0.95:
        return rgb
    # Pure named colours ("green" is #00FF00, "lime"...) mean the colour itself, not the accent.
    if s > 0.99 and abs(l - 0.5) < 0.02:
        return rgb
    # Pale tints stay pale; strong greens land on the brand blue's strength.
    nr, ng, nb = colorsys.hls_to_rgb(BLUE_HUE, l, min(1.0, s * 0.95))
    return tuple(round(c * 255) for c in (nr, ng, nb))

HEX6 = re.compile(r'(?<![0-9A-Za-z_])#([0-9A-Fa-f]{6})(?![0-9A-Za-z_])')
HEX3 = re.compile(r'(?<![0-9A-Za-z_&])#([0-9A-Fa-f]{3})(?![0-9A-Za-z_])')
INT = re.compile(r'(?<![0-9A-Za-z_])0x([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')
FLOATS = re.compile(r'(ImVec4\s*[({]\s*)([01]?\.\d+|[01])f?(\s*,\s*)([01]?\.\d+|[01])f?(\s*,\s*)([01]?\.\d+|[01])f?')
TRIPLE = re.compile(r'((?:wxColour|wxColor|ImColor|rgba?)\(\s*)(\d{1,3})(\s*,\s*)(\d{1,3})(\s*,\s*)(\d{1,3})')

def fix(text, ext):
    n = [0]
    def h6(m):
        rgb = tuple(int(m.group(1)[i:i + 2], 16) for i in (0, 2, 4))
        new = shift(rgb)
        if new == rgb:
            return m.group(0)
        n[0] += 1
        out = '%02X%02X%02X' % new
        return '#' + (out.lower() if m.group(1).islower() else out)
    def h3(m):
        rgb = tuple(int(c * 2, 16) for c in m.group(1))
        new = shift(rgb)
        if new == rgb:
            return m.group(0)
        n[0] += 1
        return '#%02X%02X%02X' % new
    def i6(m):
        rgb = tuple(int(m.group(1)[i:i + 2], 16) for i in (0, 2, 4))
        new = shift(rgb)
        if new == rgb:
            return m.group(0)
        n[0] += 1
        return '0x%02X%02X%02X' % new
    def tr(m):
        rgb = (int(m.group(2)), int(m.group(4)), int(m.group(6)))
        if max(rgb) > 255:
            return m.group(0)
        new = shift(rgb)
        if new == rgb:
            return m.group(0)
        n[0] += 1
        return f'{m.group(1)}{new[0]}{m.group(3)}{new[1]}{m.group(5)}{new[2]}'
    text = HEX6.sub(h6, text)
    if ext in ('.svg', '.css', '.html', '.vue', '.scss'):
        text = HEX3.sub(h3, text)
    if ext in ('.cpp', '.hpp', '.h'):
        text = INT.sub(i6, text)
    text = TRIPLE.sub(tr, text)
    def fl(m):
        vals = [float(m.group(i)) for i in (2, 4, 6)]
        rgb = tuple(round(v * 255) for v in vals)
        new = shift(rgb)
        if new == rgb:
            return m.group(0)
        n[0] += 1
        a, b, c = (f'{v / 255:.3f}f' for v in new)
        return f'{m.group(1)}{a}{m.group(3)}{b}{m.group(5)}{c}'
    if ext in ('.cpp', '.hpp', '.h'):
        text = FLOATS.sub(fl, text)
    text, fixed = restore_names(text)
    n[0] = max(0, n[0] - fixed)
    return text, n[0]

# A colour-name table ({gray:"#808080"}, {"green", "#008000"}) says what a name means, e.g. for
# a filament's colour; those keep their real values.
_NAMES = '|'.join(sorted(CSS_NAMES, key=len, reverse=True))
NAMED = re.compile(r'(?<![A-Za-z0-9_$-])(["\']?)(' + _NAMES + r')\1(\s*[:,]\s*)(["\'])#([0-9A-Fa-f]{6})\4', re.I)

RECOLOURS = [shift]  # the colour changes in use (tint.py adds its own)

def restore_names(text):
    count = [0]
    def put_back(m):
        real = CSS_NAMES[m.group(2).lower()]
        cur = ('#' + m.group(5)).lower()
        if cur == real:
            return m.group(0)
        # Only undo our own recolouring of the name's real value; a table with its own idea of
        # a name ("green": "#00ff00") stays as written.
        rgb = tuple(int(real[i:i + 2], 16) for i in (1, 3, 5))
        ours = {'#%02x%02x%02x' % f(rgb) for f in RECOLOURS}
        if cur not in ours:
            return m.group(0)
        count[0] += 1
        return f'{m.group(1)}{m.group(2)}{m.group(1)}{m.group(3)}{m.group(4)}{real}{m.group(4)}'
    return NAMED.sub(put_back, text), count[0]

def main():
    total, files = 0, 0
    # Orca's own accent, used all over the 3D view.
    p = os.path.join(ROOT, 'src/libslic3r/Color.hpp')
    t = open(p, encoding='utf-8').read()
    t2 = t.replace('{0.0f, 150.0f / 255.0f, 136.0f / 255.0f}', '{30.0f / 255.0f, 144.0f / 255.0f, 216.0f / 255.0f}')
    t2 = t2.replace('{0.0f, 150.f / 255.0f, 136.0f / 255, 1.0f}', '{30.0f / 255.0f, 144.0f / 255.0f, 216.0f / 255, 1.0f}')
    if t2 != t:
        open(p, 'w', encoding='utf-8').write(t2)
        total += 2
    for d in DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            if any(p in dp.replace('\\', '/').split('/') for p in SKIP_PARTS):
                continue
            for fn in fns:
                ext = os.path.splitext(fn)[1].lower()
                if ext not in EXTS or SKIP_NAME.search(fn):
                    continue
                p = os.path.join(dp, fn)
                try:
                    raw = open(p, 'rb').read()
                    text = raw.decode('utf-8')
                except (UnicodeDecodeError, OSError):
                    continue
                new, n = fix(text, ext)
                if n:
                    open(p, 'wb').write(new.encode('utf-8'))
                    total += n
                    files += 1
    print(f'recoloured {total} colours in {files} files')

if __name__ == '__main__':
    main()
