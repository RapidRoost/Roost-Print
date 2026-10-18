"""
Roost Print: the pictures. Makes the app icon, the splash screen and the small top-bar logo from
Rapid Roost's own art (tools/roost/art), and writes them over Creality Print's.

- The icon: the printer from the Rapid Roost logo, with the chicken sat inside it.
- The splash: the icon and "Roost Print" on the left (Creality Print writes its loading text
  under them), the chicken on Rapid Roost blue graph paper on the right.
- The top-bar logo: a small drawing of the printer frame (it has to be an SVG).

Run from the repo root: python tools/roost/make_art.py   (needs Pillow)
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, 'tools', 'roost', 'art')
IMG = os.path.join(ROOT, 'resources', 'images')
BLUE = (0x1E, 0x90, 0xD8, 255)
INK = (0x0B, 0x1A, 0x2A, 255)
PAPER = (0xF4, 0xF9, 0xFD, 255)

def font(size, weight=900):
    return ImageFont.truetype(os.path.join(ART, f'Orbitron-{weight}.ttf'), size)

def hen():
    return Image.open(os.path.join(ART, 'hen-look-ink.webp')).convert('RGBA')

def printer():
    """The printer frame out of the logo (364..515 across, 6..169 down, with a little margin)."""
    logo = Image.open(os.path.join(ART, 'logo-empty-printer.png')).convert('RGBA')
    return logo.crop((356, 0, 524, 176))

def icon(size=1024):
    """The printer, a pale screen inside it, and the chicken sat on the bed."""
    fr = printer()
    k = size / max(fr.size)
    fr = fr.resize((round(fr.width * k), round(fr.height * k)), Image.LANCZOS)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    ox, oy = (size - fr.width) // 2, (size - fr.height) // 2
    # The opening inside the frame (from the logo: about 20%..80% across, 26%..80% down).
    sx0, sx1 = ox + round(fr.width * 0.19), ox + round(fr.width * 0.81)
    sy0, sy1 = oy + round(fr.height * 0.25), oy + round(fr.height * 0.82)
    d = ImageDraw.Draw(out)
    d.rectangle((sx0, sy0, sx1, sy1), fill=(0xE4, 0xF1, 0xFB, 255))
    h = hen()
    hs = round((sx1 - sx0) * 1.02)
    h = h.resize((hs, hs), Image.LANCZOS)
    out.alpha_composite(h, ((sx0 + sx1) // 2 - hs // 2, sy1 - round(hs * 0.86)))
    out.alpha_composite(fr, (ox, oy))
    return out

def save_ico(img, path, sizes=(16, 24, 32, 48, 64, 128, 256)):
    img.save(path, format='ICO', sizes=[(s, s) for s in sizes])

def splash(w=860, h=480):
    out = Image.new('RGBA', (w, h), (255, 255, 255, 255))
    d = ImageDraw.Draw(out)
    # Right: graph paper in Rapid Roost blue.
    left_w = 262
    d.rectangle((left_w, 0, w, h), fill=BLUE)
    for x in range(left_w, w, 24):
        d.line((x, 0, x, h), fill=(0x4A, 0xA8, 0xE4, 255), width=1)
    for y in range(0, h, 24):
        d.line((left_w, y, w, y), fill=(0x4A, 0xA8, 0xE4, 255), width=1)
    # The chicken, big (it brings its own shadow).
    hh = hen().resize((360, 360), Image.LANCZOS)
    out.alpha_composite(hh, (left_w + 120, 70))
    # Left: the icon and the name. (The loading text goes in below, from half way down.)
    ic = icon(512).resize((104, 104), Image.LANCZOS)
    out.alpha_composite(ic, ((left_w - 104) // 2, 34))
    f1, f2 = font(25, 900), font(12, 700)
    for text, f, y, col in (('ROOST PRINT', f1, 150, INK), ('BY RAPID ROOST', f2, 186, BLUE)):
        tw = d.textlength(text, font=f)
        d.text(((left_w - tw) / 2, y), text, font=f, fill=col)
    d.line(((left_w - 40) / 2, 214, (left_w + 40) / 2, 214), fill=(0xC8, 0xD3, 0xDD, 255), width=2)
    return out

TOPBAR_SVG = '''<svg xmlns="http://www.w3.org/2000/svg" width="25px" height="20px" viewBox="0 0 25 20">
  <rect x="9.6" y="0.4" width="5.8" height="2.6" rx="0.8" fill="{blue}" stroke="{ink}" stroke-width="0.8"/>
  <rect x="3.4" y="2.6" width="18.2" height="16.6" rx="2.4" fill="{blue}" stroke="{ink}" stroke-width="1"/>
  <rect x="6.4" y="5.4" width="12.2" height="9.6" rx="0.6" fill="{screen}"/>
  <ellipse cx="12.5" cy="11.6" rx="3.6" ry="2.7" fill="#ffffff" stroke="{ink}" stroke-width="0.6"/>
  <circle cx="14.4" cy="9.4" r="1.7" fill="#ffffff" stroke="{ink}" stroke-width="0.6"/>
  <path d="M13.6 7.9 q0.6 -1.3 1.5 -0.4 q0.6 -0.6 0.9 0.5" fill="#e8344a"/>
  <path d="M15.9 9.3 l1.3 0.4 l-1.2 0.5 z" fill="#ffb13b"/>
  <rect x="6.4" y="15.6" width="12.2" height="1.6" rx="0.6" fill="{ink}"/>
</svg>
'''

def main():
    ic = icon(1024)
    ic.save(os.path.join(ART, 'roost-print-icon.png'))
    for name, s in (('CrealityPrint.png', 154), ('Creality3D.png', 154), ('CrealityPrint_32px.png', 32), ('CrealityPrint_128px.png', 128),
                    ('CrealityPrint_192px.png', 192), ('CrealityPrint_192px_transparent.png', 192), ('CrealityPrint-mac_128px.png', 128)):
        ic.resize((s, s), Image.LANCZOS).save(os.path.join(IMG, name))
    g = ic.resize((192, 192), Image.LANCZOS)
    a = g.getchannel('A')
    g.convert('L').convert('RGBA').putalpha(a)
    gray = Image.merge('RGBA', (*g.convert('L').convert('RGB').split(), a))
    gray.save(os.path.join(IMG, 'CrealityPrint_192px_grayscale.png'))
    save_ico(ic, os.path.join(IMG, 'CrealityPrint.ico'))
    save_ico(ic, os.path.join(IMG, 'CrealityPrint-mac_256px.ico'))
    save_ico(ic, os.path.join(ROOT, 'package', 'app_ico', 'app_link.ico'))
    resources_root = os.path.join(ROOT, 'resources')
    ic.resize((192, 192), Image.LANCZOS).save(os.path.join(resources_root, 'CrealityPrint_192px.png'))
    sp = splash()
    sp.save(os.path.join(IMG, 'splash_logo.png'))
    sp.convert('RGB').resize((878, 480), Image.LANCZOS).save(os.path.join(IMG, 'splash_log.png'))
    sp.save(os.path.join(ART, 'roost-print-splash.png'))
    for name, ink, screen in (('logo.svg', '#0b1a2a', '#e4f1fb'), ('logo_light.svg', '#0b1a2a', '#e4f1fb'), ('logo_dark.svg', '#0b1a2a', '#cfe6f7'), ('logo_checked.svg', '#0b1a2a', '#e4f1fb')):
        open(os.path.join(IMG, name), 'w').write(TOPBAR_SVG.format(blue='#1E90D8', ink=ink, screen=screen))
    print('art written')

if __name__ == '__main__':
    main()
