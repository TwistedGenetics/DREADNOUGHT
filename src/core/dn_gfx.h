/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the screen's drawing, for every target but the FM-1 (which keeps its platform's): a strip of the
 * screen drawn in RAM (rectangles, lines, points, anti-aliased text) and sent in one go. Copyright (C) 2026 Jamie
 * (TwistedGenetics); written 2026-10.
 *
 * A unity-build fragment: the includer gives lcd_blit(x, y, w, h, pixels) (RGB565, native byte order, w x h, row by
 * row) and, if it likes, DN_POOL (where the strip lives). Pixels are RGB565; RGB(r, g, b) makes one from 8-bit parts.
 * Text: tools/gen_dn_font.py's faces, FONT_T FONT_XS FONT_S FONT_B FONT_M FONT_L; (x, y) is the pen's left and the
 * line's top. Everything is clipped to the strip. */
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

#define DN_STRIP_MAX (240u * 144u)
static uint16_t dn_strip[DN_STRIP_MAX] DN_POOL;
static int32_t dn_sw, dn_sh;   /* the strip being drawn */

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t n, i;
    if (w > 240u)
        w = 240u;
    if (w * h > DN_STRIP_MAX)
        h = DN_STRIP_MAX / w;
    dn_sw = (int32_t)w, dn_sh = (int32_t)h;
    n = w * h;
    for (i = 0; i < n; i++)
        dn_strip[i] = bg;
}

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, (uint32_t)dn_sw, (uint32_t)dn_sh, dn_strip); }

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    if ((uint32_t)x < (uint32_t)dn_sw && (uint32_t)y < (uint32_t)dn_sh)
        dn_strip[y * dn_sw + x] = c;
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h, i;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > dn_sw)
        x1 = dn_sw;
    if (y1 > dn_sh)
        y1 = dn_sh;
    for (; y < y1; y++) {
        uint16_t *row = dn_strip + y * dn_sw;
        for (i = x; i < x1; i++)
            row[i] = c;
    }
}

/* both ends drawn: Bresenham's line in its symmetric form (one error term for both axes) */
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1, err = dx + dy, e2;
    for (;;) {
        cv_pset(x0, y0, c);
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
static inline uint16_t dn_mix565(uint16_t under, uint16_t c, uint32_t a)
{
    uint32_t k = 15u - a;
    uint32_t r = ((uint32_t)(c >> 11) * a + (uint32_t)(under >> 11) * k) / 15u;
    uint32_t g = ((uint32_t)((c >> 5) & 63u) * a + (uint32_t)((under >> 5) & 63u) * k) / 15u;
    uint32_t b = ((uint32_t)(c & 31u) * a + (uint32_t)(under & 31u) * k) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline int32_t dn_glyph_index(const dn_font_t *f, uint32_t ch)
{
    if ((ch < f->first || ch > f->last || !f->adv[ch - f->first]) && ch >= 'a' && ch <= 'z')
        ch -= 'a' - 'A';        /* (a face of capitals: a small letter as its capital) */
    if (ch < f->first || ch > f->last || !f->adv[ch - f->first])
        return -1;              /* (a character the face lacks: nothing, no room) */
    return (int32_t)(ch - f->first);
}

/* the text at the pen (x, y top); where the pen ends */
static int32_t cv_text(int32_t x, int32_t y, const dn_font_t *f, const char *s, uint16_t c)
{
    for (; *s; s++) {
        int32_t gi = dn_glyph_index(f, (uint8_t)*s), left = x - f->margin;
        uint32_t g, w, stride, row, col;
        const uint8_t *bits;
        if (gi < 0)
            continue;
        g = (uint32_t)gi, w = f->bw[g], stride = (w + 1u) >> 1, bits = f->data + f->off[g];
        for (row = 0; row < f->h; row++) {
            int32_t py = y + (int32_t)row;
            if ((uint32_t)py >= (uint32_t)dn_sh)
                continue;
            for (col = 0; col < w; col++) {
                uint32_t a = (bits[row * stride + (col >> 1)] >> ((col & 1u) ? 0 : 4)) & 15u;
                int32_t px = left + (int32_t)col;
                if (a && (uint32_t)px < (uint32_t)dn_sw) {
                    uint16_t *p = dn_strip + py * dn_sw + px;
                    *p = a == 15u ? c : dn_mix565(*p, c, a);
                }
            }
        }
        x += f->adv[g];
    }
    return x;
}

static int32_t text_w(const dn_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) {
        int32_t g = dn_glyph_index(f, (uint8_t)*s);
        if (g >= 0)
            w += f->adv[g];
    }
    return w;
}
