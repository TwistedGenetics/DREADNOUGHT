#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: the screen's fonts as a C header (src/core/dn_gfx.h reads it). Copyright (C) 2026 Jamie (TwistedGenetics).
# Barlow Semi Condensed (SIL OFL 1.1, assets/fonts/Barlow-OFL.txt), drawn anti-aliased by Pillow (with libraqm, for the
# tabular figures: every digit the same width, so numbers do not jitter), 16 levels of coverage, two pixels a byte.
#
# Six faces, the roles the screen uses: T (footers), XS (small labels), S (body), B (names), M (values), L (big
# numbers). Per face: the line height, the margin each glyph's bitmap keeps either side of its advance, the first and
# last character, and per glyph its advance, its bitmap width (advance + both margins) and where its rows start.
#   gen_dn_font.py OUT.h
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, features

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
MARGIN = 2
UPPER = " #%+-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ"     # M: values (numbers, units, names in capitals)
# (role, file, size in px, line height, baseline row, first, last, the characters drawn (None: all))
FACES = [
    ("T", "BarlowSemiCondensed-SemiBold.ttf", 10, 12, 10, 32, 95, None),
    ("XS", "BarlowSemiCondensed-SemiBold.ttf", 12, 15, 12, 32, 126, None),
    ("S", "BarlowSemiCondensed-Medium.ttf", 15, 16, 13, 32, 126, None),
    ("B", "BarlowSemiCondensed-Bold.ttf", 15, 16, 13, 32, 126, None),
    ("M", "BarlowSemiCondensed-SemiBold.ttf", 23, 24, 19, 32, 95, UPPER),
    ("L", "BarlowSemiCondensed-Bold.ttf", 31, 32, 25, 32, 57, None),
]


def face(role, file, size, height, base, first, last, chars):
    font = ImageFont.truetype(str(FONTS / file), size, layout_engine=ImageFont.Layout.RAQM)
    adv, bw, off, data = [], [], [], bytearray()
    for code in range(first, last + 1):
        ch = chr(code)
        off.append(len(data))
        if chars is not None and ch not in chars:
            adv.append(0)
            bw.append(0)
            continue
        a = int(round(font.getlength(ch, features=["tnum"])))
        w = a + 2 * MARGIN
        img = Image.new("L", (w, height), 0)
        ImageDraw.Draw(img).text((MARGIN, base), ch, font=font, fill=255, anchor="ls", features=["tnum"])
        px = img.load()
        for y in range(height):
            row = [(px[x, y] * 15 + 127) // 255 for x in range(w)] + [0]
            for x in range(0, w, 2):
                data.append(row[x] << 4 | row[x + 1])
        adv.append(a)
        bw.append(w)
    return adv, bw, off, data


def main():
    if not features.check("raqm"):
        raise SystemExit("gen_dn_font: Pillow needs libraqm (the tabular figures)")
    out = ["/* made by tools/gen_dn_font.py from Barlow Semi Condensed (SIL OFL 1.1): do not edit */", "#pragma once",
           "#include <stdint.h>", f"#define DN_FONT_MARGIN {MARGIN}", ""]
    for role, file, size, height, base, first, last, chars in FACES:
        adv, bw, off, data = face(role, file, size, height, base, first, last, chars)
        n = last - first + 1
        out.append(f"static const uint8_t DN_FONT_{role}_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        out.append("};")
        out.append(f"static const uint16_t DN_FONT_{role}_OFF[{n}] = {{{', '.join(map(str, off))}}};")
        out.append(f"static const uint8_t DN_FONT_{role}_ADV[{n}] = {{{', '.join(map(str, adv))}}};")
        out.append(f"static const uint8_t DN_FONT_{role}_BW[{n}] = {{{', '.join(map(str, bw))}}};")
        out.append(f"static const dn_font_t FONT_{role} = {{ {height}, {MARGIN}, {first}, {last}, DN_FONT_{role}_ADV, "
                   f"DN_FONT_{role}_BW, DN_FONT_{role}_OFF, DN_FONT_{role}_DATA }};")
        out.append("")
    Path(sys.argv[1]).write_text("\n".join(out))


if __name__ == "__main__":
    main()
