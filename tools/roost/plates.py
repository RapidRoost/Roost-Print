"""Roost Print: take the Creality marks off the build plates.

Two kinds of plate picture carry Creality marks:
  * the K1 / K2 family (and the plate-type overlays): a small trapezoid badge, bottom centre,
    with CREALITY written in it, plus a "Creality ... PEI Plate" title top left on the overlays;
  * older printers: a big CREALITY wordmark, bottom right.

The badge keeps its shape with RAPID ROOST written in it instead; the big wordmark is replaced
by RAPID ROOST in the same spot and colour; the title is removed. Run from the repo root:
    python3 tools/roost/plates.py
"""
import collections
import glob
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "art", "Orbitron-900.ttf")
WORD = "RAPID ROOST"

PROFILE_PLATES = sorted(glob.glob("resources/profiles/Creality/*_buildplate_texture.png"))
OVERLAYS = [f"resources/images/{n}.png" for n in (
    "custom", "texture", "smooth",
    "creality_bed_texture", "creality_bed_smooth", "creality_bed_custom", "creality_bed_none")]


def fit_font(text, max_w, max_h):
    size = max(8, int(max_h))
    while size > 6:
        font = ImageFont.truetype(FONT, size)
        l, t, r, b = font.getbbox(text)
        if r - l <= max_w and b - t <= max_h:
            return font
        size -= 1
    return ImageFont.truetype(FONT, 6)


def draw_word(im, box, colour):
    """Write WORD centred in box (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    font = fit_font(WORD, (x1 - x0), (y1 - y0))
    d = ImageDraw.Draw(im)
    l, t, r, b = font.getbbox(WORD)
    x = x0 + ((x1 - x0) - (r - l)) / 2 - l
    y = y0 + ((y1 - y0) - (b - t)) / 2 - t
    d.text((x, y), WORD, font=font, fill=colour)


def main_colour(pixels):
    c = collections.Counter(map(tuple, pixels.tolist()))
    return c.most_common(1)[0][0]


def fix_badge(a):
    """Bottom-centre trapezoid badge. Returns the text box to write in, or None."""
    h, w = a.shape[:2]
    top = int(h * 0.9)
    lo, hi = int(w * 0.3), int(w * 0.7)
    sub = a[top:, lo:hi]
    op = sub[..., 3] > 20
    colh = op.sum(0)
    if colh.max() < h * 0.008:
        return None
    tall = np.nonzero(colh > colh.max() * 0.55)[0]
    c0, c1 = lo + tall.min(), lo + tall.max()
    # A badge sits in the middle and is small; a big wordmark further right is not one.
    mid = (c0 + c1) / 2
    if abs(mid - w / 2) > w * 0.04 or (c1 - c0) > w * 0.25 or colh.max() > h * 0.05:
        return None
    rows = np.nonzero(a[top:, c0:c1 + 1, 3].max(1) > 20)[0]
    # The badge rows: where most of the badge width is covered.
    cover = (a[top:, c0:c1 + 1, 3] > 20).mean(1)
    brow = np.nonzero(cover > 0.6)[0]
    if len(brow) == 0:
        return None
    r0, r1 = top + brow.min(), top + brow.max()
    fill = main_colour(a[r0:r1 + 1, c0:c1 + 1][a[r0:r1 + 1, c0:c1 + 1, 3] > 200])
    # Fill each badge row solid between its outer edges (wipes the old letters).
    for y in range(r0, r1 + 1):
        xs = np.nonzero(a[y, c0 - 40:c1 + 41, 3] > 20)[0]
        if len(xs) == 0:
            continue
        x0, x1 = c0 - 40 + xs.min(), c0 - 40 + xs.max()
        # stay inside the slanted ends
        a[y, x0 + 2:x1 - 1] = fill
    # text colour: darker if the badge is light, lighter if it's dark
    lum = sum(fill[:3]) / 3
    ink = (40, 40, 44, 255) if lum > 110 else (225, 228, 232, 255)
    bw, bh = c1 - c0, r1 - r0
    return (c0 + bw * 0.12, r0 + bh * 0.2, c1 - bw * 0.12, r1 - bh * 0.18), ink


def fix_wordmark(a):
    """Older printers: CREALITY wordmark bottom right. Returns (box, colour) or None."""
    h, w = a.shape[:2]
    y0, x0 = int(h * 0.82), int(w * 0.5)
    sub = a[y0:, x0:]
    op = sub[..., 3] > 40
    if op.sum() < 50:
        return None
    ys, xs = np.nonzero(op)
    # ignore thin full-width edge lines: keep only rows with letters (several separate runs)
    box = (x0 + xs.min(), y0 + ys.min(), x0 + xs.max(), y0 + ys.max())
    if (box[3] - box[1]) < h * 0.008:
        return None
    colour = main_colour(sub[op][:, :4])
    a[box[1]:box[3] + 1, box[0]:box[2] + 1] = 0
    return box, colour


def clear_title(a):
    """'Creality Textured PEI Plate' etc., top left of the overlays: rows with ink on the left
    but nothing on the right (the glue strip along the very top spans the whole width)."""
    h, w = a.shape[:2]
    left = a[:int(h * 0.15), int(w * 0.02):int(w * 0.62), 3] > 20
    right = a[:int(h * 0.15), int(w * 0.70):int(w * 0.98), 3] > 20
    rows = np.nonzero(left.any(1) & ~right.any(1))[0]
    rows = rows[rows > h * 0.02]
    if len(rows):
        a[rows.min() - 2:rows.max() + 3, int(w * 0.02):int(w * 0.62)] = 0


def process(path, overlay):
    im = Image.open(path)
    mode = im.mode
    a = np.array(im.convert("RGBA"))
    out = None
    res = fix_badge(a)
    if res is not None:
        out = ("badge", res)
    elif not overlay:
        res = fix_wordmark(a)
        if res is not None:
            out = ("wordmark", res)
    if overlay:
        clear_title(a)
    img = Image.fromarray(a, "RGBA")
    if out is not None:
        box, colour = out[1]
        draw_word(img, box, tuple(colour))
    if mode == "LA":
        img = img.convert("LA")
    elif mode != "RGBA":
        img = img.convert(mode)
    img.save(path)
    return out[0] if out else "none"


if __name__ == "__main__":
    for p in PROFILE_PLATES:
        print(process(p, False), p)
    for p in OVERLAYS:
        if os.path.exists(p):
            print(process(p, True), p)
