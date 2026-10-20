"""
Roost Print: chickens for every message, and no Creality logo left anywhere.

1. Message pictures. Every message box in Roost Print shows the chicken holding up a sign:
     roost_alert_error    red sign, "!"     (errors)
     roost_alert_warning  yellow sign, "!"  (warnings)
     roost_alert_info     blue sign, "i"    (information)
     roost_alert_question blue sign, "?"    (questions, like "save first?")
     roost_alert_done     blue sign, tick   (done)
   MsgDialog.cpp uses these names.

2. Logos. Creality's "A" logo (CrealityPrint.svg and the gradient versions) and the web pages'
   logo pictures are replaced by the Roost Print icon. The SVG files go (the app's SVG reader can't
   show a picture inside an SVG), and PNGs with the same names take their place, which the app
   loads instead. The web pages' Creality mascot becomes the chicken.

Run from the repo root after make_art.py:  python tools/roost/alerts.py
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_art  # noqa: E402

ROOT, IMG, ART = make_art.ROOT, make_art.IMG, make_art.ART
INK = make_art.INK
SS = 4

SIGNS = {
    'error': ((0xE0, 0x45, 0x3A, 255), '!', (255, 255, 255, 255)),
    'warning': ((0xFF, 0xC9, 0x3C, 255), '!', INK),
    'info': ((0x1E, 0x90, 0xD8, 255), 'i', (255, 255, 255, 255)),
    'question': ((0x1E, 0x90, 0xD8, 255), '?', (255, 255, 255, 255)),
    'done': ((0x1E, 0x90, 0xD8, 255), 'tick', (255, 255, 255, 255)),
}


def alert(kind, size=256):
    fill, glyph, glyph_col = SIGNS[kind]
    S = size * SS
    canvas = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    # The chicken, bottom left, most of the picture.
    h = make_art.hen()
    hw = int(S * 0.82)
    h = h.resize((hw, int(h.height * hw / h.width)), Image.LANCZOS)
    canvas.alpha_composite(h, (S - hw - int(S * 0.01), S - h.height - int(S * 0.03)))
    # The sign: a round badge, top left (over the tail, clear of the face), thick black rim like the logo.
    d = ImageDraw.Draw(canvas)
    r = int(S * 0.20)
    cx, cy = int(S * 0.24), int(S * 0.24)
    rim = int(S * 0.028)
    d.ellipse((cx - r - rim, cy - r - rim, cx + r + rim, cy + r + rim), fill=INK)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)
    if glyph == 'tick':
        w = int(S * 0.045)
        pts = [(cx - r * 0.48, cy + r * 0.02), (cx - r * 0.12, cy + r * 0.38), (cx + r * 0.52, cy - r * 0.36)]
        d.line(pts, fill=glyph_col, width=w, joint='curve')
        for p in (pts[0], pts[-1]):
            d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=glyph_col)
    else:
        f = ImageFont.truetype(os.path.join(ART, 'Orbitron-900.ttf'), int(r * 1.5))
        l, t, rr, b = d.textbbox((0, 0), glyph, font=f)
        d.text((cx - (l + rr) / 2, cy - (t + b) / 2), glyph, font=f, fill=glyph_col)
    return canvas.resize((size, size), Image.LANCZOS)


def icon_png(size):
    src = Image.open(os.path.join(ART, 'roost-print-icon.png')).convert('RGBA')
    return src.resize((size, size), Image.LANCZOS)


def replace_raster(path, make):
    """Overwrite a picture with ours at the same size."""
    if not os.path.exists(path):
        return False
    old = Image.open(path)
    w, h = old.size
    pic = make(min(w, h))
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    out.alpha_composite(pic, ((w - pic.width) // 2, (h - pic.height) // 2))
    if path.lower().endswith(('.jpg', '.jpeg')):
        out = out.convert('RGB')
    out.save(path)
    return True


def main():
    for kind in SIGNS:
        alert(kind).save(os.path.join(IMG, f'roost_alert_{kind}.png'))
        print('wrote', f'roost_alert_{kind}.png')

    # Creality's "A" logo as SVG: remove, and leave a PNG of the same name.
    for name in ('CrealityPrint', 'CrealityPrint_gradient', 'CrealityPrint_gradient_narrow', 'CrealityPrint_gradient_circle'):
        svg = os.path.join(IMG, name + '.svg')
        if os.path.exists(svg):
            os.remove(svg)
        png = os.path.join(IMG, name + '.png')
        if name != 'CrealityPrint' or not os.path.exists(png):
            icon_png(256).save(png)
        print('logo', name)

    # The web pages' logos and the Creality mascot.
    web = os.path.join(ROOT, 'resources', 'web')
    for rel in ('image/logo.png', 'image/logo2.png', 'image/CrealityPrint_128px.png', 'deviceMgr/img/logo.png'):
        if replace_raster(os.path.join(web, rel), icon_png):
            print('web logo', rel)
    hen_only = lambda s: make_art.hen().resize((s, int(s * make_art.hen().height / make_art.hen().width)), Image.LANCZOS)
    if replace_raster(os.path.join(web, 'homepage/images/Community/common/emptyLogo.png'), hen_only):
        print('web mascot emptyLogo.png')


if __name__ == '__main__':
    main()
