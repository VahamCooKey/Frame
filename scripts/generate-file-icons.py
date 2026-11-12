"""Export Frame's Windows file icons from the user's edited SVG.

Development dependencies: fontTools, Pillow, and the Qt icon-export helper.
The output SVGs contain outlined letters, so installed fonts are not needed.

Design ("lit screen"): colour carries the KIND, the label carries the FORMAT.
Every icon is one rounded silhouette in two parts:

* field (top): the kind colour. Video fields are deep, cool shades with a paper
  play mark (Frame's exact play path from 32 px up); audio fields are bright,
  warm shades with a charcoal waveform and rounder top corners. From 24 px up,
  audio fields get a darker keyline so they hold on white backgrounds.
* plinth (bottom): charcoal, with the format label in paper.

Four renderings per format:

* 16, 20, 24 px: drawn pixel by pixel with Pillow (3 x 5 pixel font at 16/20,
  5 x 7 at 24). No SVG.
* 32, 40, 48 px (compact): one SVG per size with every edge on the pixel grid.
  compact/<id>.svg is the 48 px drawing.
* 64-256 px (full): one SVG on an 8-unit grid, whole pixels at 64/96/128/256.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import itertools
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

from fontTools.pens.basePen import BasePen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path
from fontTools.ttLib import TTFont
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
COMPACT_SIZES = (32, 40, 48)
FULL_SIZES = (64, 96, 128, 256)
NS = {"svg": "http://www.w3.org/2000/svg"}
CHARCOAL = "#202020"
PAPER = "#F5F4F0"
RIM = "#5C5C5C"
WHITE = "#FFFFFF"
# Contrast floors the generator enforces (WCAG ratios).
MIN_VIDEO_GLYPH = 4.5   # paper play mark on a video field
MIN_AUDIO_GLYPH = 7.0   # charcoal waveform on an audio field
MIN_LABEL = 7.0         # paper label on the charcoal plinth
MIN_KEYLINE = 3.0       # audio keyline against a white background
# 3 x 5 pixel font for 16/20 px (rows top to bottom, 1 = ink). M and W are
# 5 px wide, O is rounded and F has a short middle bar so that every pair of
# letters a label uses differs by at least two pixels.
FONT_3X5 = {
    "0": ("111", "101", "101", "101", "111"), "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"), "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"), "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"), "7": ("111", "001", "001", "001", "001"),
    "8": ("111", "101", "111", "101", "111"), "9": ("111", "101", "111", "001", "111"),
    "A": ("010", "101", "111", "101", "101"), "B": ("110", "101", "110", "101", "110"),
    "C": ("111", "100", "100", "100", "111"), "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "111", "100", "111"), "F": ("111", "100", "110", "100", "100"),
    "G": ("111", "100", "101", "101", "111"), "H": ("101", "101", "111", "101", "101"),
    "I": ("111", "010", "010", "010", "111"), "J": ("001", "001", "001", "101", "111"),
    "K": ("101", "101", "110", "101", "101"), "L": ("100", "100", "100", "100", "111"),
    "M": ("10001", "11011", "10101", "10001", "10001"), "N": ("110", "101", "101", "101", "101"),
    "O": ("010", "101", "101", "101", "010"), "P": ("111", "101", "111", "100", "100"),
    "Q": ("111", "101", "101", "111", "001"), "R": ("111", "101", "110", "101", "101"),
    "S": ("111", "100", "111", "001", "111"), "T": ("111", "010", "010", "010", "010"),
    "U": ("101", "101", "101", "101", "111"), "V": ("101", "101", "101", "101", "010"),
    "W": ("10001", "10001", "10101", "11011", "10001"), "X": ("101", "101", "010", "101", "101"),
    "Y": ("101", "101", "010", "010", "010"), "Z": ("111", "001", "010", "100", "111"),
}
# 5 x 7 pixel font for 24 px. Blank side columns are trimmed when drawn.
FONT_5X7 = {
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11111", "00010", "00100", "00010", "00001", "10001", "01110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110"),
    "6": ("00110", "01000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00010", "01100"),
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01110", "10001", "10000", "10000", "10000", "10001", "01110"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01110", "10001", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    "J": ("00111", "00010", "00010", "00010", "00010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "10001", "11001", "10101", "10011", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
}
# Pixel tiers. field = rows of kind colour from the top; the plinth fills the
# rest. Corner cuts list how many pixels each row loses at both ends, starting
# from the outer row. Waveform bars are centred in the field.
PIXEL = {
    16: dict(field=9, text_top=10, chars=3, font=FONT_3X5, keyline=False,
             top_cut={"video": (1,), "audio": (2, 1)}, bottom_cut=(1,),
             play=dict(left=6, top=1, height=7),
             wave=dict(left=3, width=1, gap=1, heights=(3, 5, 7, 5, 3))),
    20: dict(field=11, text_top=13, chars=4, font=FONT_3X5, keyline=False,
             top_cut={"video": (1,), "audio": (3, 1, 1)}, bottom_cut=(1,),
             play=dict(left=8, top=1, height=9),
             wave=dict(left=3, width=1, gap=2, heights=(3, 5, 9, 5, 3))),
    24: dict(field=14, text_top=16, chars=3, font=FONT_5X7, keyline=True,
             top_cut={"video": (2, 1), "audio": (4, 2, 1, 1)}, bottom_cut=(2, 1),
             play=dict(height=10.5),
             wave=dict(left=3, width=2, gap=2, heights=(4, 6, 10, 6, 4))),
}
# Compact tier (pixel units, one SVG per size).
COMPACT = {32: dict(cap=9, margin=1.5), 40: dict(cap=11, margin=2), 48: dict(cap=14, margin=2.5)}
COMPACT_FIELD = 0.58          # share of the height the field takes
COMPACT_PLAY = 0.62           # play mark height / field height
COMPACT_WAVE = 0.70           # tallest bar / field height
# Full tier, 256-unit canvas on an 8-unit grid. The field is the brand screen:
# 208 x 144, the exact play path moved up by 40 so it sits in the field.
FULL = dict(left=24, right=232, top=16, seam=160, bottom=240, audio_r=64,
            rim=4, keyline=4, cap=40, baseline=220, label_width=160)
FULL_WAVE = dict(width=16, pitch=32, heights=(42, 70, 98, 70, 42))
# Narrowest synthetic condensation applied to an Inter label.
MIN_CONDENSE = {"compact": 0.70, "full": 0.78}


# ---------------------------------------------------------------- colour ---

def rgb(hex_color):
    return tuple(int(hex_color[i:i + 2], 16) for i in (1, 3, 5))


def _linear(value):
    value /= 255
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def relative_luminance(hex_color):
    r, g, b = (_linear(c) for c in rgb(hex_color))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = relative_luminance(a), relative_luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def to_oklab(hex_color):
    r, g, b = (_linear(c) for c in rgb(hex_color))
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787046 * b) ** (1 / 3)
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720403 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def from_oklab(L, a, b):
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    linear = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
              -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
              -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)

    def encode(value):
        value = min(1.0, max(0.0, value))
        value = value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055
        return round(value * 255)
    return "#%02X%02X%02X" % tuple(encode(v) for v in linear)


def keyline_color(hex_color):
    """Same hue, darker: the edge that keeps a bright audio field on white."""
    L, a, b = to_oklab(hex_color)
    return from_oklab(L - 0.22, a * 1.1, b * 1.1)


def scheme(spec):
    """Field, glyph and (audio only) keyline colours for one format."""
    field = spec["color"]
    if spec["kind"] == "video":
        return field, PAPER, None
    return field, CHARCOAL, keyline_color(field)


# ----------------------------------------------------------------- labels ---

def label_for(spec, max_chars):
    label = spec["label"]
    if len(label) <= max_chars:
        return label
    tiny = spec.get("tinyLabel")
    if tiny and len(tiny) <= max_chars:
        return tiny
    return label[:max_chars]


def trimmed(rows):
    """Drop blank side columns so narrow letters (I, 1) take their own width."""
    used = [i for i in range(len(rows[0])) if any(row[i] == "1" for row in rows)]
    return tuple(row[used[0]:used[-1] + 1] for row in rows)


def pixel_glyph(font, character):
    rows = font[character]
    return trimmed(rows) if font is FONT_5X7 else rows


def padded(rows, width):
    left = (width - len(rows[0])) // 2
    return [("0" * left + row).ljust(width, "0") for row in rows]


def letter_distance(font, a, b):
    ga, gb = pixel_glyph(font, a), pixel_glyph(font, b)
    width = max(len(ga[0]), len(gb[0]))
    pa, pb = padded(ga, width), padded(gb, width)
    return sum(x != y for ra, rb in zip(pa, pb) for x, y in zip(ra, rb))


def check_pixel_letters(specs):
    """Every pair of letters a pixel label uses must differ by >= 2 pixels."""
    weakest = {}
    for size, layout in PIXEL.items():
        letters = sorted({c for spec in specs for c in label_for(spec, layout["chars"])})
        for character in letters:
            if character not in layout["font"]:
                raise ValueError(f"{size} px font has no '{character}'")
        pairs = [(letter_distance(layout["font"], a, b), a, b)
                 for a, b in itertools.combinations(letters, 2)]
        worst = min(pairs)
        if worst[0] < 2:
            raise ValueError(f"{size} px letters {worst[1]}/{worst[2]} differ by {worst[0]} px")
        weakest[size] = worst
    return weakest


class Label:
    """Outlined text from the font, scaled by cap height and set on a baseline."""

    def __init__(self, font, text, tracking=0):
        glyphs = font.getGlyphSet()
        cmap = font.getBestCmap()
        self.text = text
        self.cap = font["OS/2"].sCapHeight
        self.pen = SVGPathPen(glyphs)
        bounds = BoundsPen(glyphs)
        cursor = 0
        for character in text:
            glyph = cmap[ord(character)]
            transform = (1, 0, 0, 1, cursor, 0)
            glyphs[glyph].draw(TransformPen(self.pen, transform))
            glyphs[glyph].draw(TransformPen(bounds, transform))
            cursor += font["hmtx"][glyph][0] + tracking
        self.x0, self.y0, self.x1, self.y1 = bounds.bounds

    def fit(self, cap_height, max_width):
        """Scale to cap_height, condensing to max_width; returns the condensation."""
        self.scale = cap_height / self.cap
        self.condense = min(1.0, max_width / ((self.x1 - self.x0) * self.scale))
        self.sx = self.scale * self.condense
        return self.condense

    def svg(self, center_x, baseline, color):
        tx = center_x - (self.x0 + self.x1) * self.sx / 2
        return (f'<path fill="{color}" transform="matrix({self.sx:.6f} 0 0 '
                f'{-self.scale:.6f} {tx:.4f} {baseline:g})" d="{self.pen.getCommands()}"/>')


def fitted_label(font, spec, cap, width, variant, max_chars):
    """The longest label (up to max_chars) that fits without over-condensing."""
    max_chars = min(max_chars, len(spec["label"]))
    for chars in range(max_chars, min(max_chars, 3) - 1, -1):
        label = Label(font, label_for(spec, chars), tracking=0 if variant == "compact" else 60)
        if label.fit(cap, width) >= MIN_CONDENSE[variant]:
            return label
    raise ValueError(f'{spec["id"]}: label does not fit at cap {cap}')


# --------------------------------------------------------------- play mark ---

class _Flatten(BasePen):
    """Flattens an SVG path into polygons (for the 24 px anti-aliased play mark)."""

    def __init__(self, steps=24):
        super().__init__(None)
        self.steps, self.polygons, self.points = steps, [], []

    def _moveTo(self, point):
        self.points = [point]

    def _lineTo(self, point):
        self.points.append(point)

    def _curveToOne(self, p1, p2, p3):
        p0 = self._getCurrentPoint()
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.points.append(tuple(u ** 3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t ** 3 * d
                                     for a, b, c, d in zip(p0, p1, p2, p3)))

    def _closePath(self):
        self.polygons.append(self.points)
        self.points = []

    _endPath = _closePath


class PlayMark:
    def __init__(self, d):
        self.d = d
        flat = _Flatten()
        parse_path(d, flat)
        self.polygons = flat.polygons
        xs = [x for poly in self.polygons for x, _ in poly]
        ys = [y for poly in self.polygons for _, y in poly]
        self.left, self.right, self.top, self.bottom = min(xs), max(xs), min(ys), max(ys)
        self.height = self.bottom - self.top

    def placement(self, height, center_x, center_y):
        """Scale and centre for a target height, with the straight left edge on a pixel line."""
        k = height / self.height
        left = round(center_x + k * (self.left - 128))
        return k, left - k * (self.left - 128), center_y

    def mask(self, size, k, cx, cy, supersample=16):
        from PIL import ImageDraw
        big = Image.new("L", (size * supersample, size * supersample), 0)
        draw = ImageDraw.Draw(big)
        for poly in self.polygons:
            draw.polygon([((cx + k * (x - 128)) * supersample, (cy + k * (y - 128)) * supersample)
                          for x, y in poly], fill=255)
        return big.resize((size, size), Image.BOX)


# ------------------------------------------------------------ pixel tiers ---

def stamp(pixels, rows, left, top, color):
    for dy, row in enumerate(rows):
        for dx, bit in enumerate(row):
            if bit == "1":
                pixels[left + dx, top + dy] = color


def pixel_text(pixels, size, text, font, top, color):
    glyphs = [pixel_glyph(font, c) for c in text]
    width = sum(len(g[0]) for g in glyphs) + len(glyphs) - 1
    if width > size:
        raise ValueError(f"'{text}' is {width} px wide at {size} px")
    x = (size - width + 1) // 2
    for rows in glyphs:
        stamp(pixels, rows, x, top, color)
        x += len(rows[0]) + 1


def pixel_png(spec, size, target, play):
    """16/20/24 px icon drawn pixel-exact: no smoothing except the 24 px play mark."""
    layout = PIXEL[size]
    kind = spec["kind"]
    field, glyph, key = (rgb(c) + (255,) if c else None for c in scheme(spec))
    clear = (0, 0, 0, 0)
    image = Image.new("RGBA", (size, size), clear)
    px = image.load()
    rows = layout["field"]
    for y in range(size):
        for x in range(size):
            px[x, y] = field if y < rows else rgb(CHARCOAL) + (255,)
    for y, cut in enumerate(layout["top_cut"][kind]):
        for x in range(cut):
            px[x, y] = px[size - 1 - x, y] = clear
    for row, cut in enumerate(layout["bottom_cut"]):
        for x in range(cut):
            px[x, size - 1 - row] = px[size - 1 - x, size - 1 - row] = clear
    if key and layout["keyline"]:
        edge = [(x, y) for y in range(rows) for x in range(size)
                if px[x, y][3] and (x in (0, size - 1) or y == 0 or
                                    any(px[nx, ny][3] == 0 for nx, ny in
                                        ((x - 1, y), (x + 1, y), (x, y - 1))))]
        for point in edge:
            px[point] = key
    if kind == "video":
        spec_play = layout["play"]
        if "left" in spec_play:
            h = spec_play["height"]
            mark = tuple("1" * (min(i, h - 1 - i) + 1) for i in range(h))
            stamp(px, mark, spec_play["left"], spec_play["top"], glyph)
        else:
            k, cx, cy = play.placement(spec_play["height"], size / 2, rows / 2)
            paper = Image.new("RGBA", (size, size), glyph)
            paper.putalpha(play.mask(size, k, cx, cy))
            image.alpha_composite(paper)
            px = image.load()
    else:
        wave = layout["wave"]
        x = wave["left"]
        for height in wave["heights"]:
            top = (rows - height) // 2
            for dx in range(wave["width"]):
                for y in range(top, top + height):
                    px[x + dx, y] = glyph
            x += wave["width"] + wave["gap"]
    pixel_text(px, size, label_for(spec, layout["chars"]), layout["font"],
               layout["text_top"], rgb(PAPER) + (255,))
    image.save(target)


# --------------------------------------------------------------- SVG tiers ---

def field_path(x0, y0, x1, y1, r, fill):
    """Rounded top corners, square bottom edge (meets the plinth)."""
    return (f'<path fill="{fill}" d="M{x0:g} {y1:g}V{y0 + r:g}A{r:g} {r:g} 0 0 1 {x0 + r:g} {y0:g}'
            f'H{x1 - r:g}A{r:g} {r:g} 0 0 1 {x1:g} {y0 + r:g}V{y1:g}Z"/>')


def plinth_path(x0, y0, x1, y1, r, fill):
    """Square top edge, rounded bottom corners."""
    return (f'<path fill="{fill}" d="M{x0:g} {y0:g}H{x1:g}V{y1 - r:g}A{r:g} {r:g} 0 0 1 {x1 - r:g} {y1:g}'
            f'H{x0 + r:g}A{r:g} {r:g} 0 0 1 {x0:g} {y1 - r:g}Z"/>')


def svg_document(spec, size, parts, shown):
    title = escape(f'Frame {shown} {spec["kind"]} file')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" '
            f'viewBox="0 0 {size} {size}" role="img" aria-label="{title}">\n'
            f'  <title>{title}</title>\n  ' + '\n  '.join(parts) + '\n</svg>\n')


def nearest_with_parity(value, parity):
    n = round(value)
    if n % 2 != parity:
        n += 1 if value > n else -1
    return n


def compact_svg(spec, size, play, font):
    """24-48 px drawing in pixel units: every rectangle edge on the pixel grid."""
    field, glyph, key = scheme(spec)
    rows = round(size * COMPACT_FIELD)
    r_video = round(size * 0.11)
    r_top = round(size * 0.25) if spec["kind"] == "audio" else r_video
    parts = [field_path(0, 0, size, rows, r_top, key or field)]
    if key:
        parts.append(field_path(1, 1, size - 1, rows, r_top - 1, field))
    parts.append(plinth_path(0, rows, size, size, r_video, CHARCOAL))
    if spec["kind"] == "video":
        k, cx, cy = play.placement(rows * COMPACT_PLAY, size / 2, rows / 2)
        parts.append(f'<path fill="{glyph}" transform="translate({cx:.4f} {cy:g}) scale({k:.6f}) '
                     f'translate(-128 -128)" d="{play.d}"/>')
    else:
        width = max(2, round(size / 16))
        total = 9 * width
        x = (size - total + 1) // 2
        tallest = nearest_with_parity(rows * COMPACT_WAVE, rows % 2)
        bars = []
        for ratio in (3, 5, 7, 5, 3):
            height = nearest_with_parity(tallest * ratio / 7, rows % 2)
            bars.append(f'<rect x="{x}" y="{(rows - height) // 2}" width="{width}" height="{height}"/>')
            x += 2 * width
        parts.append(f'<g fill="{glyph}">' + "".join(bars) + "</g>")
    cap, margin = COMPACT[size]["cap"], COMPACT[size]["margin"]
    label = fitted_label(font, spec, cap, size - 2 * margin, "compact", 4)
    baseline = rows + (size - rows + cap) // 2
    parts.append(label.svg(size / 2, baseline, PAPER))
    return svg_document(spec, size, parts, label.text), label


def full_svg(spec, play, font, corner):
    """64-256 px: the brand screen lit in the kind colour above a charcoal plinth."""
    field, glyph, key = scheme(spec)
    f = FULL
    r_top = f["audio_r"] if spec["kind"] == "audio" else corner
    parts = [field_path(f["left"], f["top"], f["right"], f["seam"], r_top, key or field)]
    if key:
        k = f["keyline"]
        parts.append(field_path(f["left"] + k, f["top"] + k, f["right"] - k, f["seam"], r_top - k, field))
    rim = f["rim"]
    parts.append(plinth_path(f["left"], f["seam"], f["right"], f["bottom"], corner, RIM))
    parts.append(plinth_path(f["left"] + rim, f["seam"], f["right"] - rim, f["bottom"] - rim,
                             corner - rim, CHARCOAL))
    center_y = (f["top"] + f["seam"]) / 2
    if spec["kind"] == "video":
        # The exact play path from the edited SVG, moved (not scaled) into the field.
        parts.append(f'<path fill="{glyph}" transform="translate(0 {center_y - 128:g})" d="{play.d}"/>')
    else:
        w = FULL_WAVE
        bars = []
        for index, height in enumerate(w["heights"]):
            x = 128 + (index - 2) * w["pitch"] - w["width"] / 2
            bars.append(f'<rect x="{x:g}" y="{center_y - height / 2:g}" width="{w["width"]}" '
                        f'height="{height}" rx="{w["width"] / 2:g}"/>')
        parts.append(f'<g fill="{glyph}">' + "".join(bars) + "</g>")
    label = fitted_label(font, spec, f["cap"], f["label_width"], "full", len(spec["label"]))
    parts.append(label.svg(128, f["baseline"], PAPER))
    return svg_document(spec, 256, parts, spec["label"]), label


# ----------------------------------------------------------------- output ---

def write_ico(target, images):
    # Store each separately rendered size without resampling it. ICO supports
    # PNG payloads; transparent RGBA is retained at every resolution.
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, payloads = [], []
    for size, image_path in images:
        data = image_path.read_bytes()
        with Image.open(io.BytesIO(data)) as image:
            assert image.size == (size, size)
            assert image.mode == "RGBA"
            assert image.getchannel("A").getextrema() == (0, 255)
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                                   1, 32, len(data), offset))
        payloads.append(data)
        offset += len(data)
    target.write_bytes(header + b"".join(entries) + b"".join(payloads))
    with Image.open(target) as image:
        assert image.ico.sizes() == {(size, size) for size, _ in images}


PREVIEW_NOTE = ('<p class="note">Colour says the kind: deep cool fields with a paper play mark are '
                'video; bright warm fields with a charcoal waveform and rounder top corners are audio. '
                'The paper label on the charcoal plinth names the format. 16–24 px are drawn pixel by '
                'pixel; larger sizes use outlined Inter Bold and Frame’s exact play path.</p>')


def write_preview(output, preview_dir, specs):
    def data_uri(path):
        return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii")
    cards, sources = {"video": [], "audio": []}, {}
    for spec in specs:
        stem = spec["id"]
        sources[stem] = {size: data_uri(preview_dir / f"{stem}-{size}.png")
                         for size in (16, 24, 32, 48, 64, 96)}
        label = " / ".join(ext.upper() for ext in spec["extensions"]) or spec["label"]
        cards[spec["kind"]].append(
            f'<div class="tile"><div class="well"><img data-icon="{stem}" '
            f'src="{sources[stem][96]}" alt="{escape(label)} Windows file icon"></div>'
            f'<div class="label">{escape(label)}</div>'
            f'<div class="color">{escape(spec["colorName"])} {spec["color"]}</div></div>')
    template = (ROOT / "tools/icon-export/preview.html").read_text(encoding="utf-8")
    html = template.replace("{{APP_ICON}}", data_uri(preview_dir / "user-edited-app-128.png"))
    html = html.replace("{{CARDS}}", "".join(cards["video"]))
    html = html.replace("{{AUDIO_CARDS}}", "".join(cards["audio"]))
    html = html.replace("{{SOURCES}}", json.dumps(sources))
    html = re.sub(r'<p class="note">.*?</p>', lambda _: PREVIEW_NOTE, html, count=1, flags=re.S)
    (output / "preview.html").write_text(html, encoding="utf-8")


def check_colours(specs):
    """Glyph-on-field, label-on-plinth and keyline-on-white contrast floors."""
    label = contrast(PAPER, CHARCOAL)
    if label < MIN_LABEL:
        raise ValueError(f"label {PAPER} on {CHARCOAL} is {label:.2f}:1, below {MIN_LABEL}:1")
    weakest = {"video": None, "audio": None, "keyline": None}
    for spec in specs:
        field, glyph, key = scheme(spec)
        need = MIN_VIDEO_GLYPH if spec["kind"] == "video" else MIN_AUDIO_GLYPH
        ratio = contrast(glyph, field)
        if ratio < need:
            raise ValueError(f'{spec["id"]}: {glyph} on {field} is {ratio:.2f}:1, below {need}:1')
        kind = spec["kind"]
        weakest[kind] = ratio if weakest[kind] is None else min(weakest[kind], ratio)
        if key:
            edge = contrast(key, WHITE)
            if edge < MIN_KEYLINE:
                raise ValueError(f'{spec["id"]}: keyline {key} on white is {edge:.2f}:1')
            weakest["keyline"] = edge if weakest["keyline"] is None else min(weakest["keyline"], edge)
    return label, weakest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path, default=ROOT / "build-ui/file-icons")
    args = parser.parse_args()
    output = ROOT / "assets/file-icons"
    manifest = json.loads((output / "formats.json").read_text(encoding="utf-8"))
    specs = manifest["formats"]
    source = (output / manifest["source"]).resolve()
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    document = ET.parse(source).getroot()
    if document.get("viewBox") != "0 0 256 256":
        raise ValueError("Expected a 256 x 256 source viewBox")
    for spec in specs:
        if spec["kind"] not in ("video", "audio"):
            raise ValueError(f'{spec["id"]}: kind must be video or audio')
    label_ratio, weakest = check_colours(specs)
    letters = check_pixel_letters(specs)
    # Only the play mark is reused from the app icon.
    play = PlayMark(document.find("svg:path", NS).get("d"))
    font = TTFont(args.font)
    compact_dir = args.preview_dir / "compact"
    for folder in (output / "compact", args.preview_dir, compact_dir):
        folder.mkdir(parents=True, exist_ok=True)
    renderer = str(args.renderer.resolve())
    condensed = []
    for spec in specs:
        stem = spec["id"]
        for size in PIXEL:
            pixel_png(spec, size, args.preview_dir / f"{stem}-{size}.png", play)
        for size in COMPACT_SIZES:
            text, label = compact_svg(spec, size, play, font)
            svg = (output / "compact" / f"{stem}.svg") if size == 48 else compact_dir / f"{stem}-{size}.svg"
            svg.write_text(text, encoding="utf-8")
            subprocess.run([renderer, str(svg), str(args.preview_dir / stem), str(size)], check=True)
            if label.condense < 1:
                condensed.append(f"{label.text}@{size}:{label.condense:.2f}")
        text, label = full_svg(spec, play, font, manifest["cornerRadius"])
        svg = output / f"{stem}.svg"
        svg.write_text(text, encoding="utf-8")
        subprocess.run([renderer, str(svg), str(args.preview_dir / stem), *map(str, FULL_SIZES)], check=True)
        write_ico(output / (stem + ".ico"),
                  [(size, args.preview_dir / f"{stem}-{size}.png") for size in SIZES])
    font.close()
    subprocess.run([renderer, str(source), str(args.preview_dir / "user-edited-app"), "128"], check=True)
    write_preview(output, args.preview_dir, specs)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
    print(f"Exported {len(specs)} icon families at {len(SIZES)} sizes each (generation {manifest['generation']}).")
    print(f"Label {PAPER} on {CHARCOAL}: {label_ratio:.2f}:1")
    print(f"Weakest glyph contrast: video {weakest['video']:.2f}:1, audio {weakest['audio']:.2f}:1; "
          f"weakest audio keyline on white {weakest['keyline']:.2f}:1")
    print("Closest pixel-letter pairs: " + ", ".join(
        f"{size} px {a}/{b} {d} px" for size, (d, a, b) in letters.items()))
    print("Condensed Inter labels: " + (", ".join(condensed) or "none"))
    print(f"Edited source preserved: SHA256 {original_hash}")


if __name__ == "__main__":
    main()
