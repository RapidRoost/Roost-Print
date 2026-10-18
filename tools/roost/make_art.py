"""
Roost Print: the pictures. Makes the app icon, the splash screen and the small top-bar logo from
Rapid Roost's own art, and writes them over Creality Print's.

- The chicken (art/hen-ink.png) is rendered from Rapid Roost's 3D chicken (the website's ink look)
  at 1400 px, so it's sharp at any size. It gets the same thick black outline as the logo's
  lettering, and a real contact shadow where it sits.
- The printer is the one in the Rapid Roost logo, redrawn as clean shapes (not scaled up from the
  small logo), so the icon is crisp from 16 px to 1024 px.
- The icon: the chicken sat on the printer's bed. The splash: the icon and "Roost Print" on the
  left (Creality Print writes its loading text under them), the chicken on Rapid Roost blue graph
  paper on the right. The top-bar logo: the printer as a tiny SVG.

Everything is drawn 4x too big and shrunk at the end, for smooth edges.
Run from the repo root: python tools/roost/make_art.py   (needs Pillow)
"""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, 'tools', 'roost', 'art')
IMG = os.path.join(ROOT, 'resources', 'images')
BLUE = (0x1E, 0x90, 0xD8, 255)
BLUE_LIGHT = (0x4A, 0xA8, 0xE4, 255)
INK = (0x0B, 0x0E, 0x12, 255)
SCREEN = (0xEC, 0xF5, 0xFC, 255)
SS = 4  # supersampling


def font(size, weight=900):
    return ImageFont.truetype(os.path.join(ART, f'Orbitron-{weight}.ttf'), size)


def hen(outline_frac=0.022):
    """The chicken, trimmed to itself, with a thick black outline round it (like the logo's letters)."""
    h = Image.open(os.path.join(ART, 'hen-ink.png')).convert('RGBA')
    h = h.crop(h.getbbox())
    pad = round(max(h.size) * outline_frac) + 4
    big = Image.new('RGBA', (h.width + pad * 2, h.height + pad * 2), (0, 0, 0, 0))
    big.alpha_composite(h, (pad, pad))
    a = big.getchannel('A').point(lambda v: 255 if v > 40 else 0)
    r = round(max(h.size) * outline_frac)
    # Grow the silhouette evenly (blur, then cut low), then soften the cut for a clean edge.
    grown = a.filter(ImageFilter.GaussianBlur(r * 0.55)).point(lambda v: 255 if v > 10 else 0)
    grown = grown.filter(ImageFilter.GaussianBlur(1.5)).point(lambda v: min(255, v * 2))
    out = Image.new('RGBA', big.size, (0, 0, 0, 0))
    edge = Image.new('RGBA', big.size, INK)
    edge.putalpha(grown)
    out.alpha_composite(edge)
    out.alpha_composite(big)
    return out


def contact_shadow(w, h, strength=170):
    """A soft dark oval, darkest in the middle: where the chicken's body meets the surface."""
    s = Image.new('L', (w, h), 0)
    ImageDraw.Draw(s).ellipse((w * 0.12, h * 0.2, w * 0.88, h * 0.8), fill=strength)
    s = s.filter(ImageFilter.GaussianBlur(h * 0.22))
    core = Image.new('L', (w, h), 0)
    ImageDraw.Draw(core).ellipse((w * 0.28, h * 0.36, w * 0.72, h * 0.64), fill=min(255, strength + 60))
    s = ImageChops.lighter(s, core.filter(ImageFilter.GaussianBlur(h * 0.1)))
    out = Image.new('RGBA', (w, h), INK)
    out.putalpha(s)
    return out


def place_on(canvas, chicken, cx, base_y, width, shadow_w=1.05, shadow_h=0.13):
    """The chicken, `width` wide, centred on cx, its body resting on the line y = base_y, shadow under it."""
    k = width / chicken.width
    c = chicken.resize((round(chicken.width * k), round(chicken.height * k)), Image.LANCZOS)
    # The lowest solid row of the chicken is where it sits.
    a = c.getchannel('A')
    bottom = a.getbbox()[3]
    sw, sh = round(c.width * shadow_w), round(c.width * shadow_h)
    canvas.alpha_composite(contact_shadow(sw, sh), (round(cx - sw / 2), round(base_y - sh * 0.38)))
    # Sink it a hair into its shadow, so it's sitting, not hovering.
    canvas.alpha_composite(c, (round(cx - c.width / 2), round(base_y - bottom + sh * 0.12)))


def printer(size):
    """The printer from the logo, redrawn: frame, print head, screen and bed. Returns (image, bed y, bed centre x)."""
    W = H = size * SS
    u = W / 176.0
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    R = lambda *v: [round(x * u) for x in v]
    ox = 4  # centre the 168-wide drawing in 176
    def rr(box, r, fill):
        x0, y0, x1, y1 = box
        d.rounded_rectangle(R(x0 + ox, y0, x1 + ox, y1), radius=round(r * u), fill=fill)
    # Frame: black, blue band, black, then the screen.
    rr((2, 10, 166, 174), 22, INK)
    rr((9, 17, 159, 167), 16, BLUE)
    rr((17, 25, 151, 159), 10, INK)
    rr((21, 29, 147, 155), 7, SCREEN)
    # Faint graph paper on the screen.
    for x in range(29, 147, 9):
        d.line(R(x + ox, 30, x + ox, 154), fill=(0xD6, 0xE8, 0xF6, 255), width=max(1, round(0.6 * u)))
    for y in range(37, 155, 9):
        d.line(R(22 + ox, y, 146 + ox, y), fill=(0xD6, 0xE8, 0xF6, 255), width=max(1, round(0.6 * u)))
    # Print head, over the top of the frame, with its nozzle.
    rr((63, 2, 105, 38), 7, INK)
    rr((68, 7, 100, 33), 4, BLUE)
    rr((75, 14, 93, 19), 2, INK)
    rr((79, 38, 89, 46), 2, INK)
    rr((82, 38, 86, 43), 1, BLUE)
    # The bed: a blue plate seen from a little above, on a black base.
    d.polygon([tuple(R(32 + ox, 131)), tuple(R(136 + ox, 131)), tuple(R(148 + ox, 146)), tuple(R(20 + ox, 146))], fill=INK)
    d.polygon([tuple(R(36 + ox, 134)), tuple(R(132 + ox, 134)), tuple(R(142 + ox, 145)), tuple(R(26 + ox, 145))], fill=BLUE)
    rr((21, 145, 147, 155), 3, INK)
    return im, 139.5 * u, (84 + ox) * u


def icon(size=1024):
    im, bed_y, bed_x = printer(size)
    place_on(im, hen(0.026), bed_x, bed_y, width=im.width * 0.5, shadow_w=1.12, shadow_h=0.16)
    # Keep the chicken inside the frame's top: it's sat inside the printer.
    return im.resize((size, size), Image.LANCZOS)


def save_ico(img, path, sizes=(16, 24, 32, 48, 64, 128, 256)):
    img.save(path, format='ICO', sizes=[(s, s) for s in sizes])


def splash(w=860, h=480):
    W, H = w * SS, h * SS
    out = Image.new('RGBA', (W, H), (255, 255, 255, 255))
    d = ImageDraw.Draw(out)
    left_w = 262 * SS
    # Right: graph paper in Rapid Roost blue.
    d.rectangle((left_w, 0, W, H), fill=BLUE)
    for x in range(left_w, W, 24 * SS):
        d.line((x, 0, x, H), fill=BLUE_LIGHT, width=SS)
    for y in range(0, H, 24 * SS):
        d.line((left_w, y, W, y), fill=BLUE_LIGHT, width=SS)
    # The chicken, big, sitting on the paper with its shadow.
    place_on(out, hen(0.014), left_w + (W - left_w) / 2, H * 0.84, width=(W - left_w) * 0.58, shadow_w=1.18, shadow_h=0.17)
    # Left: the icon and the name. (The loading text goes in below, from half way down.)
    ic = icon(512).resize((112 * SS, 112 * SS), Image.LANCZOS)
    out.alpha_composite(ic, ((left_w - ic.width) // 2, 30 * SS))
    f1, f2 = font(25 * SS, 900), font(12 * SS, 700)
    for text, f, y, col in (('ROOST PRINT', f1, 152, INK), ('BY RAPID ROOST', f2, 188, BLUE)):
        tw = d.textlength(text, font=f)
        d.text(((left_w - tw) / 2, y * SS), text, font=f, fill=col)
    d.line(((left_w - 40 * SS) / 2, 216 * SS, (left_w + 40 * SS) / 2, 216 * SS), fill=(0xC8, 0xD3, 0xDD, 255), width=2 * SS)
    return out.resize((w, h), Image.LANCZOS)


# The printer as a tiny SVG (the top bar draws SVGs, not pictures), same shapes as printer() above.
TOPBAR_SVG = '''<svg xmlns="http://www.w3.org/2000/svg" width="25px" height="20px" viewBox="0 0 176 176">
  <g transform="translate(4 0)">
    <rect x="2" y="10" width="164" height="164" rx="22" fill="#0b0e12"/>
    <rect x="9" y="17" width="150" height="150" rx="16" fill="#1E90D8"/>
    <rect x="17" y="25" width="134" height="134" rx="10" fill="#0b0e12"/>
    <rect x="21" y="29" width="126" height="126" rx="7" fill="{screen}"/>
    <rect x="63" y="2" width="42" height="36" rx="7" fill="#0b0e12"/>
    <rect x="68" y="7" width="32" height="26" rx="4" fill="#1E90D8"/>
    <path d="M32 131 H136 L148 146 H20 Z" fill="#0b0e12"/>
    <path d="M36 134 H132 L142 145 H26 Z" fill="#1E90D8"/>
    <rect x="21" y="145" width="126" height="10" rx="3" fill="#0b0e12"/>
    <ellipse cx="84" cy="139" rx="30" ry="5" fill="#0b0e12" opacity="0.45"/>
    <path d="M52 132 C46 112 52 92 64 86 C60 70 68 60 80 66 C88 52 108 54 110 72 C122 74 126 84 120 92 C130 104 126 124 116 132 Z" fill="#ffffff" stroke="#0b0e12" stroke-width="6" stroke-linejoin="round"/>
    <path d="M86 66 C86 56 94 52 98 58 C102 52 110 56 108 64" fill="#e0342b" stroke="#0b0e12" stroke-width="4"/>
    <path d="M112 84 L126 89 L112 94 Z" fill="#ffc93c" stroke="#0b0e12" stroke-width="3" stroke-linejoin="round"/>
    <circle cx="104" cy="82" r="4.5" fill="#0b0e12"/>
  </g>
</svg>
'''


def main():
    ic = icon(1024)
    ic.save(os.path.join(ART, 'roost-print-icon.png'))
    for name, s in (('CrealityPrint.png', 154), ('Creality3D.png', 154), ('CrealityPrint_32px.png', 32), ('CrealityPrint_128px.png', 128),
                    ('CrealityPrint_192px.png', 192), ('CrealityPrint_192px_transparent.png', 192), ('CrealityPrint-mac_128px.png', 128)):
        ic.resize((s, s), Image.LANCZOS).save(os.path.join(IMG, name))
    g = ic.resize((192, 192), Image.LANCZOS)
    gray = Image.merge('RGBA', (*g.convert('L').convert('RGB').split(), g.getchannel('A')))
    gray.save(os.path.join(IMG, 'CrealityPrint_192px_grayscale.png'))
    save_ico(ic, os.path.join(IMG, 'CrealityPrint.ico'))
    save_ico(ic, os.path.join(IMG, 'CrealityPrint-mac_256px.ico'))
    save_ico(ic, os.path.join(ROOT, 'package', 'app_ico', 'app_link.ico'))
    ic.resize((192, 192), Image.LANCZOS).save(os.path.join(ROOT, 'resources', 'CrealityPrint_192px.png'))
    sp = splash()
    sp.save(os.path.join(IMG, 'splash_logo.png'))
    sp.convert('RGB').resize((878, 480), Image.LANCZOS).save(os.path.join(IMG, 'splash_log.png'))
    sp.save(os.path.join(ART, 'roost-print-splash.png'))
    for name, screen in (('logo.svg', '#ecf5fc'), ('logo_light.svg', '#ecf5fc'), ('logo_dark.svg', '#d6e8f6'), ('logo_checked.svg', '#ecf5fc')):
        open(os.path.join(IMG, name), 'w').write(TOPBAR_SVG.format(screen=screen))
    print('art written')


if __name__ == '__main__':
    main()
