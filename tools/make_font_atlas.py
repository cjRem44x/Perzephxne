#!/usr/bin/env python3
"""Render a monospace TTF/OTF font into a std/graphics Font atlas.

    python3 tools/make_font_atlas.py <font.ttf> <out.png> [--px 20]

Writes a 16-column grid of ASCII 32..126, white glyphs on a transparent
background, and prints the cell size to pass to Font.load:

    f, err: !gfx.Font = gfx.Font.load("out.png", <glyph_w>, <glyph_h>, 32)

Needs Pillow (pip install pillow). Use a monospace font: draw_text places
every glyph on a fixed grid. Check the font's licence before shipping it.
"""
import argparse
import math
from PIL import Image, ImageDraw, ImageFont

FIRST, LAST, COLS = 32, 126, 16


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("font")
    ap.add_argument("out")
    ap.add_argument("--px", type=int, default=20, help="font size in pixels (default 20)")
    a = ap.parse_args()

    font = ImageFont.truetype(a.font, a.px)
    ascent, descent = font.getmetrics()
    cell_w = max(math.ceil(font.getlength(chr(c))) for c in range(FIRST, LAST + 1))
    cell_h = ascent + descent
    n = LAST - FIRST + 1
    rows = math.ceil(n / COLS)

    img = Image.new("RGBA", (COLS * cell_w, rows * cell_h), (255, 255, 255, 0))
    draw = ImageDraw.Draw(img)
    for i in range(n):
        x, y = (i % COLS) * cell_w, (i // COLS) * cell_h
        draw.text((x, y), chr(FIRST + i), font=font, fill=(255, 255, 255, 255))
    img.save(a.out)
    print(f"{a.out}: glyph_w={cell_w} glyph_h={cell_h} first_char={FIRST}")


if __name__ == "__main__":
    main()
