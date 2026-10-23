#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mock-ups of DREADNOUGHT's Massive-style pages on the FM-1's 240x240 screen.

    python3 tools/ui_mock.py [OUT.png]        default: docs/ui-mock.png

Each screen is drawn at 4x and reduced to the real 240x240 pixels, then shown at 2x on the sheet so the
detail is visible. Layout follows the panel: KNOB 1-4 sit under the screen, so their four knobs are the
bottom row of every page; the tab strip is the page the ALGORITHM knob steps through.
An original design in the spirit of Massive's workflow (tabs, value arcs, blue modulation rings with
the source shown under the knob); no NI artwork, names or fonts.
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

S = 4                       # supersampling
W = H = 240
BG = (24, 27, 31)
PANEL = (36, 40, 46)
LINE = (58, 64, 72)
TXT = (214, 219, 224)
DIM = (128, 136, 146)
MOD = (64, 150, 255)        # modulation blue
VAL = (236, 239, 242)
ACC = {"OSC": (255, 160, 64), "FILT": (120, 220, 140), "LFO": (64, 150, 255), "BRK": (255, 92, 92)}

FONT_DIRS = [Path("/usr/share/fonts/opentype/inter"), Path("/usr/share/fonts/truetype/dejavu")]


def font(px, bold=False):
    names = (["Inter-SemiBold.otf", "DejaVuSansCondensed-Bold.ttf"] if bold
             else ["Inter-Medium.otf", "DejaVuSansCondensed.ttf"])
    for d in FONT_DIRS:
        for n in names:
            if (d / n).exists():
                return ImageFont.truetype(str(d / n), px * S)
    return ImageFont.load_default()


def P(*v):
    return [x * S for x in v]


def tabs(d, current, accent):
    names = ["OSC1", "OSC2", "OSC3", "MOD", "FILT", "ENV", "LFO", "FX", "MACRO", "BRK"]
    i = names.index(current)
    shown = names[max(0, min(i - 1, len(names) - 4)):][:4]
    x = 4
    for n in shown:
        w = 56
        on = n == current
        d.rounded_rectangle(P(x, 4, x + w - 3, 22), radius=3 * S, fill=PANEL if on else BG,
                            outline=accent if on else LINE, width=S)
        d.text(P(x + (w - 3) / 2, 13), n, font=font(10, on), fill=TXT if on else DIM, anchor="mm")
        x += w
    d.line(P(4, 25, 236, 25), fill=LINE, width=S)


def knob(d, cx, cy, value, label, shown, accent, mod=None, src=None):
    r = 17
    start, sweep = 135, 270
    d.arc(P(cx - r, cy - r, cx + r, cy + r), start, start + sweep, fill=LINE, width=3 * S)
    d.arc(P(cx - r, cy - r, cx + r, cy + r), start, start + sweep * value, fill=accent, width=3 * S)
    if mod is not None:   # the modulation ring: how far the source moves the value
        r2 = r + 5
        a0 = start + sweep * value
        a1 = start + sweep * max(0, min(1, value + mod))
        d.arc(P(cx - r2, cy - r2, cx + r2, cy + r2), min(a0, a1), max(a0, a1) + 0.01, fill=MOD, width=2 * S)
    d.ellipse(P(cx - 11, cy - 11, cx + 11, cy + 11), fill=PANEL, outline=LINE, width=S)
    a = math.radians(start + sweep * value)
    d.line(P(cx, cy, cx + 9 * math.cos(a), cy + 9 * math.sin(a)), fill=VAL, width=2 * S)
    d.text(P(cx, cy + 27), label, font=font(9, True), fill=TXT, anchor="mm")
    d.text(P(cx, cy + 38), shown, font=font(9), fill=DIM, anchor="mm")
    if src:               # source slot under the knob, as in Massive
        d.rounded_rectangle(P(cx + 9, cy - 24, cx + 25, cy - 13), radius=2 * S, fill=MOD)
        d.text(P(cx + 17, cy - 18.5), src, font=font(8, True), fill=(255, 255, 255), anchor="mm")


def knob_row(d, accent, ks):
    xs = [32, 90, 150, 208]
    for x, k in zip(xs, ks):
        knob(d, x, 178, accent=accent, **k)


def screen(draw_fn):
    im = Image.new("RGB", (W * S, H * S), BG)
    draw_fn(ImageDraw.Draw(im))
    return im.resize((W, H), Image.LANCZOS)


def page_osc(d):
    a = ACC["OSC"]
    tabs(d, "OSC1", a)
    d.text(P(8, 34), "OSC 1 · REESE TABLE", font=font(10, True), fill=a)
    d.text(P(232, 34), "7 SAW · UNI", font=font(9), fill=DIM, anchor="ra")
    # a stack of wavetable frames, the current one bright
    for f in range(9, -1, -1):
        off = f * 3.2
        pts = []
        for i in range(0, 161):
            t = i / 160
            y = sum(math.sin(2 * math.pi * k * t) / k * (1 if k < 3 + f else 0.25) for k in range(1, 12))
            pts.append((40 + i - off * 0.6, 102 - off - y * 18))
        col = a if f == 4 else (LINE if f % 2 else (48, 53, 60))
        d.line([(x * S, y * S) for x, y in pts], fill=col, width=(2 if f == 4 else 1) * S)
    d.text(P(8, 134), "WT-POS", font=font(8), fill=DIM)
    d.rounded_rectangle(P(48, 136, 232, 140), radius=2 * S, fill=LINE)
    d.rounded_rectangle(P(48, 136, 48 + 184 * 0.42, 140), radius=2 * S, fill=a)
    knob_row(d, a, [
        dict(value=0.5, label="PITCH", shown="+0 st"),
        dict(value=0.42, label="WT-POS", shown="42 %", mod=0.25, src="L2"),
        dict(value=0.63, label="DETUNE", shown="18 ct", mod=0.12, src="M1"),
        dict(value=0.8, label="AMP", shown="-2 dB"),
    ])


def page_filter(d):
    a = ACC["FILT"]
    tabs(d, "FILT", a)
    d.text(P(8, 34), "F1 LADDER 24  ⟷  F2 COMB", font=font(10, True), fill=a)
    d.text(P(232, 34), "SER", font=font(9), fill=DIM, anchor="ra")
    for gx in range(30, 236, 34):
        d.line(P(gx, 48, gx, 132), fill=(40, 44, 50), width=S)
    pts = []
    for i in range(0, 201):
        f = i / 200
        lp = 1 / (1 + (f / 0.35) ** 8) ** 0.5
        peak = 0.55 * math.exp(-((f - 0.35) / 0.03) ** 2)
        comb = 0.82 + 0.18 * math.cos(2 * math.pi * f * 9)
        y = (lp + peak) * comb
        pts.append((24 + i * 1.04, 128 - y * 66))
    d.line([(x * S, y * S) for x, y in pts], fill=a, width=2 * S)
    d.polygon([(24 * S, 128 * S)] + [(x * S, y * S) for x, y in pts] + [(232 * S, 128 * S)], fill=(34, 58, 42))
    d.line([(x * S, y * S) for x, y in pts], fill=a, width=2 * S)
    knob_row(d, a, [
        dict(value=0.35, label="CUTOFF", shown="410 Hz", mod=0.3, src="L1"),
        dict(value=0.7, label="RESO", shown="70"),
        dict(value=0.45, label="COMB", shown="F#1", mod=0.2, src="M1"),
        dict(value=0.25, label="SER/PAR", shown="25 %"),
    ])


def page_lfo(d):
    a = ACC["LFO"]
    tabs(d, "LFO", a)
    d.text(P(8, 34), "MOD 2 · STEPPER  1/16T", font=font(10, True), fill=a)
    d.text(P(232, 34), "SYNC", font=font(9), fill=DIM, anchor="ra")
    steps = [0.9, 0.2, 0.6, 0.2, 1.0, 0.1, 0.75, 0.3, 0.9, 0.2, 0.5, 0.15, 1.0, 0.6, 0.35, 0.05]
    for i, v in enumerate(steps):
        x0 = 10 + i * 14
        on = i == 5
        d.rectangle(P(x0, 128 - v * 76, x0 + 11, 128), fill=a if not on else VAL)
        d.rectangle(P(x0, 128 - 76, x0 + 11, 128), outline=LINE, width=S)
    d.text(P(8, 134), "→ WT-POS  CUTOFF  COMB", font=font(8), fill=DIM)
    knob_row(d, a, [
        dict(value=0.62, label="RATE", shown="1/16T"),
        dict(value=0.5, label="GLIDE", shown="50 %"),
        dict(value=0.3, label="SWING", shown="12 %"),
        dict(value=0.8, label="DEPTH", shown="80 %"),
    ])


def page_breaks(d):
    a = ACC["BRK"]
    tabs(d, "BRK", a)
    d.text(P(8, 34), "BREAKS · USR1  172 BPM", font=font(10, True), fill=a)
    d.text(P(232, 34), "16 SL", font=font(9), fill=DIM, anchor="ra")
    import random
    rnd = random.Random(7)
    hits = {0: 1.0, 4: 0.95, 6: 0.5, 8: 0.9, 10: 0.45, 12: 1.0, 14: 0.6}
    mid = 88
    for x in range(10, 231):
        sl = (x - 10) / 220 * 16
        k = int(sl)
        env = hits.get(k, 0.25) * math.exp(-(sl - k) * 3.5)
        h = env * 38 * (0.4 + 0.6 * rnd.random())
        col = a if k == 6 else (150, 158, 168)
        d.line(P(x, mid - h, x, mid + h), fill=col, width=S)
    for k in range(17):
        x = 10 + k * 220 / 16
        d.line(P(x, 48, x, 128), fill=LINE, width=S)
    d.text(P(10 + 6.5 * 220 / 16, 124), "7", font=font(8, True), fill=VAL, anchor="mm")
    knob_row(d, a, [
        dict(value=6 / 15, label="SLICE", shown="7"),
        dict(value=0.5, label="PITCH", shown="+0 st"),
        dict(value=0.4, label="ROLL", shown="x3"),
        dict(value=0.0, label="REV", shown="OFF"),
    ])


def main(out):
    pages = [("OSC 1 — Reese wavetable", page_osc), ("FILTER — ladder into comb", page_filter),
             ("MOD 2 — stepper (the wobbles)", page_lfo), ("BREAKS — slice view", page_breaks)]
    z = 2
    pad, cap = 24, 30
    sheet = Image.new("RGB", (pad + 2 * (W * z + pad), pad + 2 * (H * z + cap + pad)), (236, 239, 242))
    d = ImageDraw.Draw(sheet)
    title = ImageFont.truetype(str(FONT_DIRS[0] / "Inter-SemiBold.otf"), 18) if (FONT_DIRS[0] / "Inter-SemiBold.otf").exists() else None
    for n, (name, fn) in enumerate(pages):
        cx, cy = n % 2, n // 2
        x = pad + cx * (W * z + pad)
        y = pad + cy * (H * z + cap + pad)
        d.text((x, y + 4), name, fill=(27, 31, 36), font=title)
        sheet.paste(screen(fn).resize((W * z, H * z), Image.NEAREST), (x, y + cap))
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "docs/ui-mock.png")
