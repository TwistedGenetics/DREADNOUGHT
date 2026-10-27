/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the screen's drawing, for every target but the FM-1 (which keeps its platform's): a strip of the
 * screen drawn in RAM (rectangles, lines, points, anti-aliased text) and sent in one go. Copyright (C) 2026 Jamie
 * (TwistedGenetics); written 2026-10.
 *
 * A unity-build fragment: the includer gives lcd_blit(x, y, w, h, pixels) (RGB565, native byte order, w x h, row by
 * row) and, if it likes, DN_POOL (where the strip lives). Pixels are RGB565; RGB(r, g, b) makes one from 8-bit parts.
 * Text: tools/gen_dn_font.py's faces, FONT_T FONT_XS FONT_S FONT_B FONT_M FONT_L; (x, y) is the pen's left and the
 * line's top. Everything is clipped to the strip.
 *
 * DN_GFX_BAND_ROWS (a target short of RAM: the SMK-37): the strip is not kept whole. What is drawn is written down (a
 * list of rectangles, lines, points and texts, the texts' characters copied), and cv_blit draws the list again for
 * each band of that many rows and sends each band: the same pixels, a fraction of the RAM. */
#pragma once
#include <stdint.h>

#ifndef DN_POOL
#define DN_POOL
#endif

typedef struct {
    uint8_t h;                 /* line height */
    uint8_t margin;            /* each glyph's bitmap starts this far left of the pen */
    uint8_t first, last;       /* the characters it has */
    const uint8_t *adv;        /* per character: the pen's step (0: not in this face) */
    const uint8_t *bw;         /* the bitmap's width */
    const uint16_t *off;       /* where its rows start in data: 4-bit coverage, two pixels a byte, left one high */
    const uint8_t *data;
} dn_font_t;
#include "dn_font.h"

#define RGB(r, g, b) ((uint16_t)((((uint32_t)(r) & 0xF8u) << 8) | (((uint32_t)(g) & 0xFCu) << 3) | ((uint32_t)(b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu

/* ---- drawing into the strip: rows dg_oy .. dg_oy + dg_sh - 1 of the area being drawn are in it (all of them,
 * unbanded) ---- */
#ifdef DN_GFX_BAND_ROWS
#define DN_STRIP_MAX (240u * (uint32_t)(DN_GFX_BAND_ROWS))
#else
#define DN_STRIP_MAX (240u * 144u)
#endif
static uint16_t dg_strip[DN_STRIP_MAX] DN_POOL;
static int32_t dg_sw, dg_sh, dg_oy;   /* the strip's width, rows, and the area's row its first row is */

static inline void dg_pset(int32_t x, int32_t y, uint16_t c)
{
    y -= dg_oy;
    if ((uint32_t)x < (uint32_t)dg_sw && (uint32_t)y < (uint32_t)dg_sh)
        dg_strip[y * dg_sw + x] = c;
}

static void dg_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h - dg_oy, i;
    y -= dg_oy;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > dg_sw)
        x1 = dg_sw;
    if (y1 > dg_sh)
        y1 = dg_sh;
    for (; y < y1; y++) {
        uint16_t *row = dg_strip + y * dg_sw;
        for (i = x; i < x1; i++)
            row[i] = c;
    }
}

/* both ends drawn: Bresenham's line in its symmetric form (one error term for both axes) */
static void dg_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1, err = dx + dy, e2;
    for (;;) {
        dg_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

/* c over the pixel by a / 15: each channel the weighted sum of the two, rounded down */
static inline uint16_t dg_mix565(uint16_t under, uint16_t c, uint32_t a)
{
    uint32_t k = 15u - a;
    uint32_t r = ((uint32_t)(c >> 11) * a + (uint32_t)(under >> 11) * k) / 15u;
    uint32_t g = ((uint32_t)((c >> 5) & 63u) * a + (uint32_t)((under >> 5) & 63u) * k) / 15u;
    uint32_t b = ((uint32_t)(c & 31u) * a + (uint32_t)(under & 31u) * k) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline int32_t dg_glyph_index(const dn_font_t *f, uint32_t ch)
{
    if ((ch < f->first || ch > f->last || !f->adv[ch - f->first]) && ch >= 'a' && ch <= 'z')
        ch -= 'a' - 'A';        /* (a face of capitals: a small letter as its capital) */
    if (ch < f->first || ch > f->last || !f->adv[ch - f->first])
        return -1;              /* (a character the face lacks: nothing, no room) */
    return (int32_t)(ch - f->first);
}

/* the text at the pen (x, y top); where the pen ends */
static int32_t dg_text(int32_t x, int32_t y, const dn_font_t *f, const char *s, uint16_t c, int draw)
{
    for (; *s; s++) {
        int32_t gi = dg_glyph_index(f, (uint8_t)*s), left = x - f->margin;
        uint32_t g, w, stride, row, col;
        const uint8_t *bits;
        if (gi < 0)
            continue;
        g = (uint32_t)gi, w = f->bw[g], stride = (w + 1u) >> 1, bits = f->data + f->off[g];
        for (row = 0; draw && row < f->h; row++) {
            int32_t py = y + (int32_t)row - dg_oy;
            if ((uint32_t)py >= (uint32_t)dg_sh)
                continue;
            for (col = 0; col < w; col++) {
                uint32_t a = (bits[row * stride + (col >> 1)] >> ((col & 1u) ? 0 : 4)) & 15u;
                int32_t px = left + (int32_t)col;
                if (a && (uint32_t)px < (uint32_t)dg_sw) {
                    uint16_t *p = dg_strip + py * dg_sw + px;
                    *p = a == 15u ? c : dg_mix565(*p, c, a);
                }
            }
        }
        x += f->adv[g];
    }
    return x;
}

static void dg_clear(uint16_t bg)
{
    uint32_t i, n = (uint32_t)(dg_sw * dg_sh);
    for (i = 0; i < n; i++)
        dg_strip[i] = bg;
}

#ifndef DN_GFX_BAND_ROWS
/* ---- unbanded: drawn straight into the strip, sent whole ---- */
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    if (w > 240u)
        w = 240u;
    if (w * h > DN_STRIP_MAX)
        h = DN_STRIP_MAX / w;
    dg_sw = (int32_t)w, dg_sh = (int32_t)h, dg_oy = 0;
    dg_clear(bg);
}
static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, (uint32_t)dg_sw, (uint32_t)dg_sh, dg_strip); }
static inline void cv_pset(int32_t x, int32_t y, uint16_t c) { dg_pset(x, y, c); }
static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { dg_rect(x, y, w, h, c); }
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) { dg_line(x0, y0, x1, y1, c); }
static int32_t cv_text(int32_t x, int32_t y, const dn_font_t *f, const char *s, uint16_t c)
{
    return dg_text(x, y, f, s, c, 1);
}
#else
/* ---- banded: written down, then drawn band by band at cv_blit ---- */
#ifndef DN_GFX_OPS
#define DN_GFX_OPS 1200u                              /* (the busiest screen of the host's tests: tests/host) */
#endif
#ifndef DN_GFX_CHARS
#define DN_GFX_CHARS 3072u
#endif
enum { DG_OP_RECT, DG_OP_LINE, DG_OP_PSET, DG_OP_TEXT, DG_OP_CALL };
#ifndef DN_GFX_ARGS
#define DN_GFX_ARGS 1024u
#endif
typedef struct {
    uint8_t kind;
    uint8_t font;              /* (TEXT: which face) */
    uint16_t c;
    int16_t a, b, w, h;        /* x y w h; LINE: x0 y0 x1 y1; TEXT: x y, h the characters' start */
} dg_op_t;
static dg_op_t dg_ops[DN_GFX_OPS];
static char dg_chars[DN_GFX_CHARS];
/* a shape drawn point by point (the app's discs and arcs: thousands of points) is written down as a call to the
 * function that draws it and its numbers, and called again for each band, its points then drawn into the band */
typedef void (*dg_fn_t)(const int32_t *args);
static dg_fn_t dg_fns[DN_GFX_OPS];
static int32_t dg_args[DN_GFX_ARGS];
static uint32_t dg_nargs;
static uint8_t dg_replaying;                          /* (1: cv_pset draws, into the band) */
static uint32_t dg_nops, dg_nchars, dg_lost;          /* (dg_lost: ops that did not fit: a screen drawn short) */
static uint32_t dg_ops_peak, dg_chars_peak;           /* (the most a screen has needed: the tests watch them) */
static int32_t dg_aw, dg_ah;                          /* the area being drawn */
static uint16_t dg_bg;
static const dn_font_t *const DG_FONTS[6] = {&FONT_T, &FONT_XS, &FONT_S, &FONT_B, &FONT_M, &FONT_L};

static dg_op_t *dg_op(uint32_t kind, uint16_t c)
{
    dg_op_t *o;
    if (dg_nops >= DN_GFX_OPS) {
        dg_lost++;
        return 0;
    }
    o = &dg_ops[dg_nops++];
    o->kind = (uint8_t)kind, o->c = c;
    return o;
}
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    dg_aw = (int32_t)(w > 240u ? 240u : w), dg_ah = (int32_t)h, dg_bg = bg;
    dg_nops = dg_nchars = dg_nargs = 0;
}
static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    dg_op_t *o;
    if (dg_replaying)
        dg_pset(x, y, c);
    else if ((uint32_t)x < (uint32_t)dg_aw && (uint32_t)y < (uint32_t)dg_ah && (o = dg_op(DG_OP_PSET, c)))
        o->a = (int16_t)x, o->b = (int16_t)y;
}
static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    dg_op_t *o;
    if (w > 0 && h > 0 && x < dg_aw && y < dg_ah && x + w > 0 && y + h > 0 && (o = dg_op(DG_OP_RECT, c))) {
        if (x < -1000)
            w += x + 1000, x = -1000;               /* (inside int16, the same pixels) */
        if (y < -1000)
            h += y + 1000, y = -1000;
        o->a = (int16_t)x, o->b = (int16_t)y, o->w = (int16_t)(w > 2000 ? 2000 : w), o->h = (int16_t)(h > 2000 ? 2000 : h);
    }
}
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    dg_op_t *o = dg_op(DG_OP_LINE, c);
    if (o)
        o->a = (int16_t)x0, o->b = (int16_t)y0, o->w = (int16_t)x1, o->h = (int16_t)y1;
}
static int32_t cv_text(int32_t x, int32_t y, const dn_font_t *f, const char *s, uint16_t c)
{
    uint32_t n = 0, k;
    dg_op_t *o;
    while (s[n])
        n++;
    for (k = 0; k < 6u && DG_FONTS[k] != f; k++)
        ;
    if (k < 6u && n && y < dg_ah && y + f->h > 0 && dg_nchars + n + 1u <= DN_GFX_CHARS && (o = dg_op(DG_OP_TEXT, c))) {
        o->a = (int16_t)x, o->b = (int16_t)y, o->font = (uint8_t)k, o->h = (int16_t)dg_nchars;
        for (k = 0; k <= n; k++)
            dg_chars[dg_nchars++] = s[k];
    } else if (n && y < dg_ah && y + f->h > 0)
        dg_lost++;
    return dg_text(x, y, f, s, c, 0);
}
/* fn(args) draws a shape with cv_pset: call it again at each band */
static void cv_call(dg_fn_t fn, const int32_t *args, uint32_t n)
{
    dg_op_t *o;
    uint32_t i;
    if (dg_nargs + n > DN_GFX_ARGS || !(o = dg_op(DG_OP_CALL, 0))) {
        dg_lost++;
        return;
    }
    dg_fns[o - dg_ops] = fn;
    o->h = (int16_t)dg_nargs;
    for (i = 0; i < n; i++)
        dg_args[dg_nargs++] = args[i];
}
static void cv_blit(uint32_t x, uint32_t y)
{
    int32_t top;
    uint32_t i;
    if (dg_nops > dg_ops_peak)
        dg_ops_peak = dg_nops;
    if (dg_nchars > dg_chars_peak)
        dg_chars_peak = dg_nchars;
    dg_sw = dg_aw;
    for (top = 0; top < dg_ah; top += (int32_t)(DN_GFX_BAND_ROWS)) {
        dg_oy = top;
        dg_sh = dg_ah - top < (int32_t)(DN_GFX_BAND_ROWS) ? dg_ah - top : (int32_t)(DN_GFX_BAND_ROWS);
        dg_clear(dg_bg);
        for (i = 0; i < dg_nops; i++) {
            const dg_op_t *o = &dg_ops[i];
            switch (o->kind) {
            case DG_OP_RECT:
                if (o->b < top + dg_sh && o->b + o->h > top)
                    dg_rect(o->a, o->b, o->w, o->h, o->c);
                break;
            case DG_OP_LINE:
                if ((o->b < top + dg_sh || o->h < top + dg_sh) && (o->b >= top || o->h >= top))
                    dg_line(o->a, o->b, o->w, o->h, o->c);
                break;
            case DG_OP_PSET:
                dg_pset(o->a, o->b, o->c);
                break;
            case DG_OP_CALL:
                dg_replaying = 1;
                dg_fns[i](dg_args + o->h);
                dg_replaying = 0;
                break;
            default:
                if (o->b < top + dg_sh && o->b + DG_FONTS[o->font]->h > top)
                    (void)dg_text(o->a, o->b, DG_FONTS[o->font], dg_chars + o->h, o->c, 1);
                break;
            }
        }
        lcd_blit(x, y + (uint32_t)top, (uint32_t)dg_sw, (uint32_t)dg_sh, dg_strip);
    }
}
#endif

static int32_t text_w(const dn_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) {
        int32_t g = dg_glyph_index(f, (uint8_t)*s);
        if (g >= 0)
            w += f->adv[g];
    }
    return w;
}
