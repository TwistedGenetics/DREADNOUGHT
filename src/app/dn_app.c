/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the app. Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * A unity-build fragment, the same on the FM-1 (src/hardware/dn_main_fm1.c) and on the host (tests/host/dn_host.c).
 * Before it, the includer has: X0X's plat.h (the panel, MIDI, storage), gfx.c (the canvas, the fonts, RGB), reese.h
 * and dn_dsp.h, and two functions: dn_lock() / dn_unlock() (on the FM-1: interrupts off and on again, so the audio
 * interrupt never sees half an edit). The engine itself (src/dsp/reese.c) is a separate -O2 unit.
 *
 * Who calls what:
 *   dn_app_init()           once, at start-up (the saved sound comes back)
 *   dn_app_render(lr, n)    the audio interrupt: n frames, stereo, Q15 (it clears lr itself)
 *   dn_app_midi(st, d1, d2) the audio interrupt: a channel message from USB or the TRS jack
 *   dn_app_input()          the main loop, as often as it likes: the panel
 *   dn_app_draw(force)      the main loop, ~30-60 times a second: the screen and the lights
 *
 * The panel (one engine for now: REESE):
 *   keys F3..G5              notes, mono, last note wins; a key held under another slides to it (legato + glide)
 *   OCT- / OCT+              octave down / up (-3..+3)
 *   SELECT encoder           the page: OSC  FILTER  AMP  MOD  OUT
 *   SEL EDIT ENV LFO GLO     straight to a page (lit: the page shown)
 *   KNOB 1..4                the page's four values (the dials on the screen)
 *   PRESETS encoder          the factory sounds (the edits since are dropped)
 *   ALGORITHM encoder        the tempo (BEAT locks the reese's movement to it)
 *   PLAY                     hold a note (F3) to tweak with both hands free
 *   SAVE                     keep the sound: it comes back at power-on (lit: changed since)
 */
#include "dn_app.h"

#define DN_SAVE_MAGIC 0x31524E44u                   /* "DNR1" */
#define DN_NPAGES 5u
#define DN_KEY0 53                                  /* key 0 is F3 */
#define DN_PX_BPM 100                               /* the OUT page's two non-engine dials */
#define DN_PX_OCT 101

static const char *const DN_PAGE_NAME[DN_NPAGES] = {"OSC", "FILTER", "AMP", "MOD", "OUT"};
static const uint8_t DN_PAGE[DN_NPAGES][4] = {
    {RP_OSC, RP_DTN, RP_BEAT, RP_SUB},
    {RP_CUT, RP_RES, RP_DRV, RP_NTCH},
    {RP_ATK, RP_DEC, RP_SUS, RP_REL},
    {RP_FENV, RP_GLIDE, RP_LRATE, RP_LFLT},
    {RP_LEVEL, RP_TRANS, DN_PX_BPM, DN_PX_OCT},
};
static const uint8_t DN_PAGE_BTN[DN_NPAGES] = {B_SEL, B_EDIT, B_ENV, B_LFO, B_GLO};

typedef struct {
    uint32_t magic;
    uint8_t preset, page;
    int8_t oct;
    uint8_t pad;
    uint16_t bpm, pad2;
    int16_t p[RP_COUNT];
} dn_save_t;

static reese_t dn_rs;
static struct {
    uint8_t preset, page, dirty, play, ready;
    int8_t oct;
    uint8_t held[NKEYS], nheld;                     /* the keys down, oldest first */
    uint8_t sounding;                               /* the note the keys play now (0xFF: none) */
    uint8_t midi_note;                              /* the note MIDI plays now (0xFF: none) */
    uint32_t btn_prev, keys_prev;
    uint32_t redraw;                                /* DN_RD_* */
} dn;
enum { DN_RD_TOP = 1u, DN_RD_BOTTOM = 2u, DN_RD_ALL = 3u };

/* the output, every 8th sample, for the screen's scope (written in the audio interrupt, read by the drawing) */
#define DN_SCOPE 512u
static int16_t dn_scope[DN_SCOPE];
static volatile uint32_t dn_scope_w;

/* -------------------------------------------------------------------------------------------- audio side --- */
void dn_app_render(int32_t *lr, uint32_t n)
{
    uint32_t i, w = dn_scope_w;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (!dn.ready)
        return;
    reese_render(&dn_rs, lr, n);
    for (i = 0; i < 2u * n; i++)                    /* the master: a soft knee at the top, nothing ever wraps */
        lr[i] = lr[i] > 24576 || lr[i] < -24576 ? (dn_softclip(lr[i] * 4 / 3) * 3) / 4 : lr[i];
    for (i = 0; i < n; i += 8u)
        dn_scope[w++ & (DN_SCOPE - 1u)] = (int16_t)dn_clamp(lr[2u * i], -32768, 32767);
    dn_scope_w = w;
}

void dn_app_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t t = st & 0xF0u;
    if (!dn.ready)
        return;
    if (t == 0x90u && d2) {
        reese_note_on(&dn_rs, d1, d2, dn_rs.gate);  /* overlapping notes slide, as a mono synth does */
        dn.midi_note = d1;
    } else if ((t == 0x80u || t == 0x90u) && d1 == dn.midi_note) {
        dn.midi_note = 0xFF;
        if (dn.sounding == 0xFF)
            reese_note_off(&dn_rs);
    } else if (t == 0xB0u && d1 == 123u) {          /* all notes off */
        dn.midi_note = 0xFF;
        reese_note_off(&dn_rs);
    }
}

/* ------------------------------------------------------------------------------------------------ values --- */
static int32_t dn_get(uint32_t id)
{
    if (id == DN_PX_BPM)
        return dn_rs.bpm;
    if (id == DN_PX_OCT)
        return dn.oct;
    return dn_rs.p[id];
}
static int32_t dn_min(uint32_t id) { return id == DN_PX_BPM ? 60 : id == DN_PX_OCT ? -3 : reese_param_min(id); }
static int32_t dn_max(uint32_t id) { return id == DN_PX_BPM ? 200 : id == DN_PX_OCT ? 3 : reese_param_max(id); }
static const char *dn_name(uint32_t id) { return id == DN_PX_BPM ? "BPM" : id == DN_PX_OCT ? "OCT" : reese_param_name(id); }
static void dn_text(uint32_t id, int32_t v, char out[8])
{
    if (id == DN_PX_BPM || id == DN_PX_OCT) {
        reese_param_text(RP_LEVEL, v, out);         /* (a plain number) */
        if (id == DN_PX_OCT && v > 0) {
            out[0] = '+';
            out[1] = (char)('0' + v);
            out[2] = 0;
        }
        return;
    }
    reese_param_text(id, v, out);
}
static void dn_set(uint32_t id, int32_t v)
{
    v = dn_clamp(v, dn_min(id), dn_max(id));
    if (v == dn_get(id))
        return;
    dn_lock();
    if (id == DN_PX_BPM)
        dn_rs.bpm = (uint16_t)v;
    else if (id == DN_PX_OCT)
        dn.oct = (int8_t)v;
    else
        dn_rs.p[id] = (int16_t)v;
    dn_unlock();
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}

static void dn_load_preset(uint32_t i)
{
    dn_lock();
    reese_load_preset(&dn_rs, i);
    dn_unlock();
    dn.preset = (uint8_t)i;
    dn.dirty = 0;
    dn.redraw |= DN_RD_ALL;
}

static void dn_save(void)
{
    dn_save_t s;
    uint32_t k;
    s.magic = DN_SAVE_MAGIC;
    s.preset = dn.preset;
    s.page = dn.page;
    s.oct = dn.oct;
    s.pad = 0;
    s.bpm = dn_rs.bpm;
    s.pad2 = 0;
    for (k = 0; k < RP_COUNT; k++)
        s.p[k] = dn_rs.p[k];
    if (plat_store_save(OBJ_SOUND, &s, sizeof s) == 0)
        dn.dirty = 0;
    dn.redraw |= DN_RD_ALL;
}

void dn_app_init(void)
{
    dn_save_t s;
    uint32_t k;
    reese_init(&dn_rs);
    dn.sounding = dn.midi_note = 0xFF;
    dn.redraw = DN_RD_ALL;
    if (plat_store_load(OBJ_SOUND, &s, sizeof s) == (int)sizeof s && s.magic == DN_SAVE_MAGIC) {
        dn.preset = (uint8_t)(s.preset % reese_preset_count());
        dn.page = (uint8_t)(s.page % DN_NPAGES);
        dn.oct = (int8_t)dn_clamp(s.oct, -3, 3);
        dn_rs.bpm = (uint16_t)dn_clamp(s.bpm, 60, 200);
        for (k = 0; k < RP_COUNT; k++)
            dn_rs.p[k] = (int16_t)dn_clamp(s.p[k], reese_param_min(k), reese_param_max(k));
    }
    dn.btn_prev = plat_buttons();                   /* what is held at start-up is no press */
    dn.keys_prev = plat_keys();
    dn.ready = 1;
}

/* ------------------------------------------------------------------------------------------------- input --- */
static uint8_t dn_key_note(uint32_t k) { return (uint8_t)dn_clamp(DN_KEY0 + (int32_t)k + 12 * dn.oct, 0, 127); }

static void dn_play(uint8_t note, int legato)
{
    dn_lock();
    reese_note_on(&dn_rs, note, 100, legato);
    dn_unlock();
    dn.sounding = note;
}
static void dn_stop(void)
{
    dn_lock();
    if (dn.midi_note == 0xFF)
        reese_note_off(&dn_rs);
    dn_unlock();
    dn.sounding = 0xFF;
}

static void dn_keys(uint32_t keys)
{
    uint32_t down = keys & ~dn.keys_prev, up = dn.keys_prev & ~keys, k, i, j;
    for (k = 0; k < NKEYS; k++) {
        if (up & (1u << k)) {
            for (i = j = 0; i < dn.nheld; i++)
                if (dn.held[i] != k)
                    dn.held[j++] = dn.held[i];
            dn.nheld = (uint8_t)j;
        }
        if ((down & (1u << k)) && dn.nheld < NKEYS) {
            dn_play(dn_key_note(k), dn.nheld > 0u || dn.play);   /* under a held key: a slide */
            dn.held[dn.nheld++] = (uint8_t)k;
        }
    }
    if (up && dn.nheld == 0u && !dn.play && dn.sounding != 0xFF)
        dn_stop();
    else if (up && dn.nheld && dn.sounding != dn_key_note(dn.held[dn.nheld - 1u]))
        dn_play(dn_key_note(dn.held[dn.nheld - 1u]), 1);       /* back to the last key still held */
    else if (up && !dn.nheld && dn.play)
        dn_play(DN_KEY0, 1);
    dn.keys_prev = keys;
}

static int32_t dn_step(uint32_t id, int32_t d)     /* a detent: one step, faster when turned fast */
{
    int32_t span = dn_max(id) - dn_min(id);
    if (span > 40 && (d > 2 || d < -2))
        d *= 3;
    return d;
}

void dn_app_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), press = btn & ~dn.btn_prev, k;
    int32_t d;
    dn.btn_prev = btn;
    if (keys != dn.keys_prev)
        dn_keys(keys);
    if ((btn & (1u << B_OCTDN)) && (btn & (1u << B_OCTUP)))
        press &= ~((1u << B_OCTDN) | (1u << B_OCTUP));          /* both: the update-mode hold, not an octave */
    else if (press & (1u << B_OCTDN))
        dn_set(DN_PX_OCT, dn.oct - 1);
    else if (press & (1u << B_OCTUP))
        dn_set(DN_PX_OCT, dn.oct + 1);
    for (k = 0; k < DN_NPAGES; k++)
        if (press & (1u << DN_PAGE_BTN[k])) {
            dn.page = (uint8_t)k;
            dn.redraw |= DN_RD_BOTTOM;
        }
    if (press & (1u << B_SAVE))
        dn_save();
    if (press & (1u << B_PLAY)) {
        dn.play = (uint8_t)!dn.play;
        if (dn.play && !dn.nheld)
            dn_play(DN_KEY0, 0);
        else if (!dn.play && !dn.nheld)
            dn_stop();
        dn.redraw |= DN_RD_BOTTOM;
    }
    if ((d = plat_enc(EN_SELECT)) != 0) {
        dn.page = (uint8_t)(((int32_t)dn.page + d % (int32_t)DN_NPAGES + (int32_t)DN_NPAGES) % (int32_t)DN_NPAGES);
        dn.redraw |= DN_RD_BOTTOM;
    }
    if ((d = plat_enc(EN_PRESET)) != 0) {
        int32_t n = (int32_t)reese_preset_count();
        dn_load_preset((uint32_t)((((int32_t)dn.preset + d) % n + n) % n));
    }
    if ((d = plat_enc(EN_ALGO)) != 0)
        dn_set(DN_PX_BPM, (int32_t)dn_rs.bpm + d);
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) {
            uint32_t id = DN_PAGE[dn.page][k];
            dn_set(id, dn_get(id) + dn_step(id, d));
        }
}

/* ---------------------------------------------------------------------------------------------- the screen --- */
/* Massive's look: graphite panels, light-blue values, one knob row; DREADNOUGHT's red only for its name */
#define DN_C_BG RGB(44, 46, 50)
#define DN_C_BAR RGB(24, 25, 28)
#define DN_C_PANEL RGB(62, 65, 70)
#define DN_C_EDGE RGB(88, 92, 98)
#define DN_C_WELL RGB(18, 19, 21)
#define DN_C_GRID RGB(36, 39, 44)
#define DN_C_LABEL RGB(176, 180, 186)
#define DN_C_DIMTXT RGB(120, 124, 130)
#define DN_C_BLUE RGB(98, 200, 255)
#define DN_C_BLUE_D RGB(40, 92, 124)
#define DN_C_TRACK RGB(30, 31, 34)
#define DN_C_KNOB RGB(92, 96, 102)
#define DN_C_KNOB_HI RGB(130, 134, 140)
#define DN_C_RED RGB(232, 58, 46)

/* a point on a circle: angle a in 1/1024 of a turn, 0 = up, clockwise */
static void dn_polar(int32_t cx, int32_t cy, int32_t r, int32_t a, int32_t *x, int32_t *y)
{
    uint32_t ph = (uint32_t)a << 22;
    *x = cx + ((dn_sine(ph) * r + 16384) >> 15);
    *y = cy - ((dn_sine(ph + 0x40000000u) * r + 16384) >> 15);
}

static void dn_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}

/* the arc from angle a0 to a1 (1/1024 turn), radii r0..r1 */
static void dn_arc(int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t a0, int32_t a1, uint16_t c)
{
    int32_t a, r, x, y;
    if (a1 < a0) { a = a0; a0 = a1; a1 = a; }
    for (a = a0; a <= a1; a += 3)
        for (r = r0; r <= r1; r++) {
            dn_polar(cx, cy, r, a, &x, &y);
            cv_pset(x, y, c);
        }
}

#define DN_KA 384                                   /* the dial's sweep: +-135 degrees */
static void dn_knob(int32_t cx, int32_t cy, uint32_t id, int focus)
{
    int32_t lo = dn_min(id), hi = dn_max(id), v = dn_get(id), span = hi - lo > 0 ? hi - lo : 1;
    int32_t av = -DN_KA + (2 * DN_KA * (v - lo)) / span, a0 = lo < 0 ? 0 : -DN_KA, px, py, qx, qy;
    char t[8];
    dn_arc(cx, cy, 19, 22, -DN_KA, DN_KA, DN_C_TRACK);
    dn_arc(cx, cy, 19, 22, a0, av, focus ? DN_C_BLUE : DN_C_BLUE_D);
    dn_disc(cx, cy, 15, DN_C_KNOB);
    dn_disc(cx - 2, cy - 3, 9, DN_C_KNOB_HI);
    dn_disc(cx, cy, 8, DN_C_KNOB);
    dn_polar(cx, cy, 5, av, &px, &py);
    dn_polar(cx, cy, 14, av, &qx, &qy);
    cv_line(px, py, qx, qy, C_WHITE);
    cv_line(px + 1, py, qx + 1, qy, C_WHITE);
    cv_text(cx - text_w(&FONT_T, dn_name(id)) / 2, cy - 42, &FONT_T, dn_name(id), DN_C_LABEL);
    dn_text(id, v, t);
    cv_text(cx - text_w(&FONT_XS, t) / 2, cy + 27, &FONT_XS, t, DN_C_BLUE);
}

static void dn_draw_top(void)
{
    char t[16];
    int32_t x, i, w = (int32_t)dn_scope_w, start, y, py = 0;
    const char *pn = reese_preset_name(dn.preset);
    cv_begin(240, 96, DN_C_BG);
    /* the title bar */
    cv_rect(0, 0, 240, 22, DN_C_BAR);
    cv_text(8, 3, &FONT_B, "DREADNOUGHT", DN_C_RED);
    reese_param_text(RP_LEVEL, dn_rs.bpm, t);
    x = cv_text(232 - text_w(&FONT_XS, t) - text_w(&FONT_T, " BPM"), 4, &FONT_XS, t, DN_C_LABEL);
    cv_text(x, 6, &FONT_T, " BPM", DN_C_DIMTXT);
    /* the sound: the engine's tag, the preset's name, changed or not */
    cv_rect(0, 22, 240, 24, DN_C_PANEL);
    cv_rect(0, 45, 240, 1, DN_C_EDGE);
    cv_rect(6, 27, 46, 14, DN_C_BLUE);
    cv_text(29 - text_w(&FONT_T, "REESE") / 2, 28, &FONT_T, "REESE", DN_C_BAR);
    x = cv_text(60, 26, &FONT_S, pn, C_WHITE);
    if (dn.dirty)
        cv_text(x + 2, 26, &FONT_S, "*", DN_C_BLUE);
    cv_text(234 - text_w(&FONT_T, "PRESETS"), 29, &FONT_T, "PRESETS", DN_C_DIMTXT);
    /* the scope: the last ~42 ms, from a rising zero crossing so it stands still */
    cv_rect(4, 50, 232, 42, DN_C_WELL);
    for (i = 1; i < 4; i++)
        cv_rect(4 + i * 58, 50, 1, 42, DN_C_GRID);
    cv_rect(4, 71, 232, 1, DN_C_GRID);
    start = w - 230;
    for (i = 0; i < 200; i++) {
        int32_t a = dn_scope[(uint32_t)(start - i - 1) & (DN_SCOPE - 1u)], b = dn_scope[(uint32_t)(start - i) & (DN_SCOPE - 1u)];
        if (a < 0 && b >= 0) {
            start -= i;
            break;
        }
    }
    for (x = 0; x < 230; x++) {
        int32_t s = dn_scope[(uint32_t)(start + x) & (DN_SCOPE - 1u)];
        y = 71 - (s * 20) / 14000;
        y = dn_clamp(y, 51, 91);
        if (x)
            cv_line(4 + x, py, 5 + x, y, DN_C_BLUE);
        py = y;
    }
    cv_blit(0, 0);
}

static void dn_draw_bottom(void)
{
    uint32_t k;
    int32_t x;
    char t[24];
    cv_begin(240, 144, DN_C_BG);
    /* the page tabs */
    for (k = 0, x = 0; k < DN_NPAGES; k++) {
        int32_t w = k == DN_NPAGES - 1u ? 240 - x : 48;
        int on = k == dn.page;
        cv_rect(x, 0, w - 1, 17, on ? DN_C_BLUE : DN_C_PANEL);
        cv_text(x + (w - 1 - text_w(&FONT_T, DN_PAGE_NAME[k])) / 2, 3, &FONT_T, DN_PAGE_NAME[k],
                on ? DN_C_BAR : DN_C_LABEL);
        x += w;
    }
    /* the four dials: KNOB 1..4 */
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    cv_rect(0, 17, 240, 1, DN_C_EDGE);
    for (k = 0; k < 4u; k++)
        dn_knob(30 + 60 * (int32_t)k, 67, DN_PAGE[dn.page][k], 1);
    /* the status line */
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' ';
    dn_text(DN_PX_OCT, dn.oct, t + 4);
    cv_text(8, 125, &FONT_XS, t, DN_C_LABEL);
    if (dn.play)
        cv_text(70, 125, &FONT_XS, "HOLD", DN_C_BLUE);
    if (dn.dirty)
        cv_text(232 - text_w(&FONT_XS, "SAVE?"), 125, &FONT_XS, "SAVE?", DN_C_BLUE);
    else
        cv_text(232 - text_w(&FONT_XS, "SAVED"), 125, &FONT_XS, "SAVED", DN_C_DIMTXT);
    cv_blit(0, 96);
}

void dn_app_draw(int force)
{
    uint32_t lit = 0;
    if (force)
        dn.redraw = DN_RD_ALL;
    dn_draw_top();                                  /* every frame: the scope moves */
    if (dn.redraw & DN_RD_BOTTOM)
        dn_draw_bottom();
    dn.redraw = 0;
    lit |= 1u << DN_PAGE_BTN[dn.page];
    if (dn.dirty)
        lit |= 1u << B_SAVE;
    if (dn.play)
        lit |= 1u << B_PLAY;
    plat_leds(lit, dn.keys_prev);
}
