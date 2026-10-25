# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 drawn for DREADNOUGHT's manual, in the BLOCKS skin's colours, with numbered callouts and arrows.
The panel's layout is X0X's (x0x-firmware/docs/img/fm1-panel.svg, GPL-3.0). Copyright (C) 2026 Jamie (TwistedGenetics).

    svg = fm1(callouts=[...], screen="build/host/ui-01-boot.png", lit={"PLAY"})
A callout: dict(t=control or [controls], n=1, title="PRESS PLAY", text="...", side="right", at=y (left/right) or x
(top/bottom), col=0..5). Controls: MASTER SELECT PRESETS ALGORITHM OCT- OCT+ SCREEN K1..K4 FX SEL ENV LFO EDIT GLO
HOME SAVE ARP SEQ PLAY REC W1..W16 (white keys) B1..B11 (black keys) KEYS."""
import base64
from html import escape
from pathlib import Path

# BLOCKS: the four knob colours and their tints, then HOME's pink and purple
COL = ["#FF6A2B", "#FFC21A", "#3DDC97", "#5B9BFF", "#FF5CA8", "#C3A2FF"]
TINT = ["#FFC3AA", "#FFE59E", "#ACEFD2", "#B8D3FF", "#FFC2DE", "#E4D8FF"]
INK, PAPER = "#101114", "#F4F3EE"
FONT = "'Barlow Semi Condensed', 'Arial Narrow', Arial, sans-serif"

KNOBS = {"MASTER": (105, 92, 29), "SELECT": (215, 92, 29), "PRESETS": (105, 210, 29), "ALGORITHM": (215, 210, 29),
         "K1": (610, 92, 27), "K2": (714, 92, 27), "K3": (818, 92, 27), "K4": (922, 92, 27)}
TOP = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"]
BOT = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
BTN = {}
for i, (a, b) in enumerate(zip(TOP, BOT)):
    BTN[a] = (582 + 63 * i, 158, 54, 48)
    BTN[b] = (582 + 63 * i, 264, 54, 48)
BTN["OCT-"] = (72, 294, 82, 38)
BTN["OCT+"] = (170, 294, 82, 38)
WX = [54.2 + 56.5 * i for i in range(16)]
BX = [82.5, 139.0, 195.5, 308.5, 365.0, 478.0, 534.5, 591.0, 704.0, 760.5, 873.5]


def where(name):
    """a control's box: (x, y, w, h)"""
    if name in KNOBS:
        x, y, r = KNOBS[name]
        return (x - r, y - r, 2 * r, 2 * r)
    if name in BTN:
        return BTN[name]
    if name == "SCREEN":
        return (300, 44, 236, 236)
    if name == "KEYS":
        return (46, 408, 908, 254)
    if name[0] == "W":
        return (WX[int(name[1:]) - 1], 516.1, 44.1, 140.4)
    if name[0] == "B":
        return (BX[int(name[1:]) - 1], 416.2, 44.1, 89.1)
    raise KeyError(name)


def centre(name):
    x, y, w, h = where(name)
    return x + w / 2, y + h / 2


def png_uri(path):
    return "data:image/png;base64," + base64.b64encode(Path(path).read_bytes()).decode()


def _wrap(text, n):
    out, line = [], ""
    for w in text.split():
        if len(line) + len(w) + 1 > n and line:
            out.append(line)
            line = w
        else:
            line = (line + " " + w).strip()
    if line:
        out.append(line)
    return out


def fm1(callouts=(), screen=None, lit=(), margin=(330, 330, 40, 40), labels=True, ft=34, fx=26):
    """the FM-1 (1000 x 720) with callouts in the margins (left, right, top, bottom) -> an SVG string"""
    ml, mr, mt, mb = margin
    W, H = 1000 + ml + mr, 720 + mt + mb
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-ml} {-mt} {W} {H}" class="fm1" role="img" '
         f'aria-label="The FM-1">',
         f'<defs><marker id="ah" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
         f'<path d="M0,0 L10,5 L0,10 z" fill="{INK}"/></marker></defs>',
         f'<style>.fm1 text{{font-family:{FONT};}} .pl{{font-weight:700;font-size:17px;fill:{INK};letter-spacing:.05em}}'
         f' .bl{{font-weight:700;font-size:14px;fill:{INK};letter-spacing:.03em}}</style>']
    # the body
    o.append(f'<rect x="4" y="4" width="992" height="712" rx="44" fill="#DAD8D0" stroke="#B4B2A9" stroke-width="3"/>')
    o.append('<rect x="58" y="284" width="210" height="58" rx="14" fill="#C9C7BE"/>')
    o.append('<rect x="568" y="146" width="400" height="236" rx="18" fill="#C9C7BE"/>')
    o.append('<rect x="36" y="400" width="928" height="270" rx="18" fill="#C9C7BE"/>')
    for name, (x, y, r) in KNOBS.items():
        hi = name in lit
        o.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#9A988F"/><circle cx="{x}" cy="{y}" r="{r - 5}" fill="#2A2C33"/>'
                 f'<line x1="{x}" y1="{y - r + 7}" x2="{x}" y2="{y - r + 18}" stroke="#F4F3EE" stroke-width="3.5" stroke-linecap="round"/>')
        if labels:
            lab = {"K1": "KNOB 1", "K2": "KNOB 2", "K3": "KNOB 3", "K4": "KNOB 4"}.get(name, name)
            o.append(f'<text x="{x}" y="{y - r - 10}" text-anchor="middle" class="pl">{lab}</text>')
    for name, (x, y, w, h) in BTN.items():
        hi = name in lit
        face = "#FF6A2B" if hi else "#FAF9F5"
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{face}" stroke="#A9A79E" stroke-width="2"/>')
        if labels:
            if name == "PLAY":
                o.append(f'<text x="{x + w / 2}" y="{y + 21}" text-anchor="middle" class="bl" font-size="12">PLAY</text>'
                         f'<text x="{x + w / 2}" y="{y + 36}" text-anchor="middle" class="bl" font-size="12">STOP</text>')
            else:
                o.append(f'<text x="{x + w / 2}" y="{y + h / 2 + 5}" text-anchor="middle" class="bl">{name}</text>')
    for i, x in enumerate(WX):
        fill = "#FF6A2B" if f"W{i + 1}" in lit else "#FFFFFF"
        o.append(f'<rect x="{x}" y="516.1" width="44.1" height="140.4" rx="22" fill="{fill}" stroke="#A9A79E" stroke-width="1.5"/>')
    for i, x in enumerate(BX):
        fill = "#FF6A2B" if f"B{i + 1}" in lit else "#8C8F97"
        o.append(f'<rect x="{x}" y="416.2" width="44.1" height="89.1" rx="22" fill="{fill}"/>')
    # the screen
    o.append('<rect x="300" y="44" width="236" height="236" rx="26" fill="#1A1B1F"/>')
    if screen:
        o.append(f'<image x="318" y="62" width="200" height="200" href="{png_uri(screen)}" style="image-rendering:pixelated"/>')
    # callouts: the rings, then the arrows, then the boxes on top
    rings, arrows, boxes = [], [], []
    for c in callouts:
        col = c.get("col", (c["n"] - 1) % 6) if c.get("n") else c.get("col", 0)
        targets = c["t"] if isinstance(c["t"], (list, tuple)) else [c["t"]]
        for t in targets:                           # a ring round each control it points at
            x, y, w, h = where(t)
            if t in KNOBS:
                cx, cy, r = KNOBS[t]
                rings.append(f'<circle cx="{cx}" cy="{cy}" r="{r + 7}" fill="none" stroke="{COL[col]}" stroke-width="7"/>')
            else:
                rings.append(f'<rect x="{x - 6}" y="{y - 6}" width="{w + 12}" height="{h + 12}" rx="14" fill="none" stroke="{COL[col]}" stroke-width="7"/>')
        side, title, text = c.get("side", "right"), c.get("title", ""), c.get("text", "")
        bw = c.get("w", 300)
        lines = _wrap(text, c.get("wrap", int((bw - 36) / (fx * 0.47)))) if text else []
        lh = round(fx * 1.2)
        bh = round(ft * 1.55) + lh * len(lines) + (10 if lines else 0)
        if side in ("left", "right"):
            bx = -ml + 12 if side == "left" else 1000 + mr - 12 - bw
            by = c["at"] - bh / 2
            ax, ay = (bx + bw, c["at"]) if side == "left" else (bx, c["at"])
        else:
            bx = c["at"] - bw / 2
            by = -mt + 8 if side == "top" else 720 + mb - 8 - bh
            ax, ay = (c["at"], by + bh) if side == "top" else (c["at"], by)
        boxes.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="12" fill="{TINT[col]}"/>'
                     f'<rect x="{bx}" y="{by}" width="12" height="{bh}" rx="5" fill="{COL[col]}"/>')
        ty = by + ft * 1.08
        tx = bx + 26
        if c.get("n"):
            r = ft * 0.62
            boxes.append(f'<circle cx="{bx + 26 + r}" cy="{ty - ft * 0.34}" r="{r}" fill="#FFFFFF" stroke="{INK}" stroke-width="3.5"/>'
                         f'<text x="{bx + 26 + r}" y="{ty}" text-anchor="middle" font-weight="700" font-size="{ft * 0.9}" fill="{INK}">{c["n"]}</text>')
            tx = bx + 26 + 2 * r + 12
        boxes.append(f'<text x="{tx}" y="{ty}" font-weight="700" font-size="{ft}" fill="{INK}" letter-spacing=".02em">{escape(title)}</text>')
        for k2, ln in enumerate(lines):
            boxes.append(f'<text x="{bx + 26}" y="{ty + 8 + lh * (k2 + 1)}" font-weight="500" font-size="{fx}" fill="{INK}">{escape(ln)}</text>')
        for t in targets:                           # the arrow: from the box into the control's edge (c["edge"])
            x, y, w, h = where(t)
            edge = c.get("edge") or {"left": "left", "right": "right", "top": "top", "bottom": "bottom"}[side]
            gap = 9
            if t in KNOBS:
                cx, cy, r = KNOBS[t]
                pts = {"left": (cx - r - gap, cy), "right": (cx + r + gap, cy), "top": (cx, cy - r - 36), "bottom": (cx, cy + r + gap)}
            else:
                pts = {"left": (x - gap, y + h / 2), "right": (x + w + gap, y + h / 2), "top": (x + w / 2, y - gap),
                       "bottom": (x + w / 2, y + h + gap)}
            px, py = pts[edge]
            nx, ny = {"left": (-1, 0), "right": (1, 0), "top": (0, -1), "bottom": (0, 1)}[edge]
            ox, oy = {"left": (1, 0), "right": (-1, 0), "top": (0, 1), "bottom": (0, -1)}[side]
            k = max(60, 0.45 * ((ax - px) ** 2 + (ay - py) ** 2) ** 0.5)
            d = f"M{ax},{ay} C{ax + ox * k},{ay + oy * k} {px + nx * k},{py + ny * k} {px},{py}"
            arrows.append(f'<path d="{d}" fill="none" stroke="#FFFFFF" stroke-width="9" stroke-linecap="round"/>'
                     f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="3.5" marker-end="url(#ah)"/>')
    o += rings + arrows + boxes
    o.append("</svg>")
    return "".join(o)


def screen(png, callouts, scale=3, ml=420, mr=420, mt=20, mb=20):
    """a screenshot, scale x, with callouts at the sides: dict(r=(x, y, w, h) in screen pixels, title, text, side, at, col)"""
    S = 240 * scale
    W, H = S + ml + mr, S + mt + mb
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-ml} {-mt} {W} {H}" class="fm1">',
         f'<defs><marker id="ahs" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
         f'<path d="M0,0 L10,5 L0,10 z" fill="{INK}"/></marker></defs>',
         f'<style>.fm1 text{{font-family:{FONT};}}</style>',
         f'<rect x="-14" y="-14" width="{S + 28}" height="{S + 28}" rx="22" fill="#1A1B1F"/>',
         f'<image x="0" y="0" width="{S}" height="{S}" href="{png_uri(png)}" style="image-rendering:pixelated"/>']
    rings, arrows, boxes = [], [], []
    for c in callouts:
        col = c.get("col", 0)
        x, y, w, h = [v * scale for v in c["r"]]
        rings.append(f'<rect x="{x - 3}" y="{y - 3}" width="{w + 6}" height="{h + 6}" rx="8" fill="none" stroke="{COL[col]}" stroke-width="6"/>')
        lines = _wrap(c.get("text", ""), c.get("wrap", 24))
        bw, bh = ml - 30, 62 + 32 * len(lines)
        left = c.get("side", "left") == "left"
        bx = -ml + 10 if left else S + mr - 10 - bw
        by = c["at"] - bh / 2
        ax, ay = (bx + bw, c["at"]) if left else (bx, c["at"])
        px, py = (x - 8, y + h / 2) if left else (x + w + 8, y + h / 2)
        k = max(40, abs(ax - px) * 0.5)
        d = f"M{ax},{ay} C{ax + (k if left else -k)},{ay} {px - (k if left else -k)},{py} {px},{py}"
        arrows.append(f'<path d="{d}" fill="none" stroke="#FFFFFF" stroke-width="9" stroke-linecap="round"/>'
                      f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="3.5" marker-end="url(#ahs)"/>')
        boxes.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="10" fill="{TINT[col]}"/>'
                     f'<rect x="{bx}" y="{by}" width="10" height="{bh}" rx="4" fill="{COL[col]}"/>'
                     f'<text x="{bx + 26}" y="{by + 40}" font-weight="700" font-size="32" fill="{INK}" letter-spacing=".02em">{escape(c["title"])}</text>')
        for i, ln in enumerate(lines):
            boxes.append(f'<text x="{bx + 26}" y="{by + 76 + 32 * i}" font-weight="500" font-size="27" fill="{INK}">{escape(ln)}</text>')
    o += rings + arrows + boxes + ["</svg>"]
    return "".join(o)
