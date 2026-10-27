/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT as a plugin: the C core. The app (src/app/dn_app.c) and the engines on a simulated FM-1, as
 * tests/host/dn_host.c runs them for the tests, here driven by the plugin instead of a script: its panel (buttons,
 * encoders, keys), the host's MIDI and transport, the screen into a framebuffer, the flash objects into the
 * plugin's state. Copyright (C) 2026 Jamie (TwistedGenetics); the screen and the panel over src/core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dn_core.h"
#ifndef DN_VERSION
#define DN_VERSION "0.0"
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(x)
#define DNC_NO_ATTR_UNDEF 1
#else
#define __attribute__(x)
#endif

/* ---------------------------------------------------------------------------------------------- screen --- */
static uint16_t fb[240 * 240];
static uint32_t fb_serial;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
    fb_serial++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));   /* (fb: the panel's order) */
    fb_serial++;
}
#include "dn_gfx.h"
#ifndef DNC_NO_ATTR_UNDEF
#undef __attribute__
#define __attribute__(x)
#endif

/* --------------------------------------------------------------------------------------------- platform --- */
#include "dn_plat.h"
static uint32_t now_ms, held_btn, held_keys, lit_btn, lit_keys, dim_keys;
static int32_t enc_acc[NE];
uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
void plat_keys_dim(uint32_t k) { dim_keys = k; }
/* MIDI out (the MIDI tracks' notes): a ring the plugin empties into the host's MIDI; clock and the rest dropped */
static uint32_t mo_ring[256], mo_w, mo_r;
void plat_midi_out(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu;
    if (st < 0x80u || st >= 0xF0u)
        return;
    if (mo_w - mo_r < 256u)
        mo_ring[mo_w++ & 255u] = pkt;
}
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ] = {-1, -1, -1, -1, -1, -1, -1};
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    return 0;
}
/* one core: a computer has plenty (the second core's jobs run where they are handed over) */
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }
static uint32_t sim_ticks;
uint32_t plat_ticks24(void) { return sim_ticks += 7u; }
uint32_t plat_cpu_pct(void) { return 0; }
uint32_t plat_cpu_peak_pct(void) { return 0; }
uint32_t plat_xruns(void) { return 0; }
/* the sample slots: slot 1 holds ROLLER and STEPPER (the loops the FM-1 had built in before 0.29) */
#include "dn_breaks_builtin.h"
int plat_slot_zones(int k) { return k == 0 ? DN_BRK_BUILTIN : 0; }
const uint8_t *plat_slot(int k, int z, uint32_t *n, uint32_t *rate, char name[9])
{
    if (k == 0 && z >= 0 && z < DN_BRK_BUILTIN) {
        *n = DN_BRK_BUILTIN_LOOPS[z].n;
        *rate = DN_BRK_BUILTIN_LOOPS[z].rate;
        memcpy(name, "BREAKS", 7);
        return DN_BRK_BUILTIN_LOOPS[z].adpcm;
    }
    return 0;
}
/* everything runs on the plugin's audio thread (the plugin's own lock keeps the rest out): no lock here */
static void dn_lock(void) {}
static void dn_unlock(void) {}

#include "../../src/app/dn_app.c"

/* the settings objects (slot 3's top on the FM-1) */
static uint8_t sim_ext[DN_NEXT][PLAT_STORE_MAX];
static int sim_ext_len[DN_NEXT] = {-1, -1};
int dn_ext_load(uint32_t i, void *dst, uint32_t max)
{
    if (i >= DN_NEXT || sim_ext_len[i] < 0)
        return -1;
    memcpy(dst, sim_ext[i], (uint32_t)sim_ext_len[i] < max ? (uint32_t)sim_ext_len[i] : max);
    return (uint32_t)sim_ext_len[i] < max ? sim_ext_len[i] : (int)max;
}
int dn_ext_save(uint32_t i, const void *src, uint32_t len)
{
    if (i >= DN_NEXT || len > PLAT_STORE_MAX)
        return -1;
    memcpy(sim_ext[i], src, len);
    sim_ext_len[i] = (int)len;
    return 0;
}
#include "../../src/app/dn_splash.c"

/* ------------------------------------------------------------------------------------------------- the core --- */
static uint32_t frames_acc;                         /* frames since the last millisecond, x1000 */
static uint32_t splash_left;                        /* ms the splash stays up */
static int host_playing;
static uint32_t host_bpm;

const char *dnc_version(void) { return DN_VERSION; }

void dnc_init(void)
{
    memset(&dn, 0, sizeof dn);
    held_btn = held_keys = 0;
    memset(enc_acc, 0, sizeof enc_acc);
    mo_w = mo_r = 0;
    frames_acc = 0;
    host_playing = 0;
    host_bpm = 0;
    dn_app_init();
    dn_splash(DN_VERSION);
    splash_left = 1200u;
}

static void tick_ms(void)
{
    now_ms++;
    dn_app_input();
    if (splash_left) {
        if (!--splash_left)
            dn_app_draw(1);
    } else if (now_ms % 16u == 0u) {
        dn_app_draw(0);
    }
}

void dnc_render(float *lr, uint32_t n)
{
    static int32_t buf[2 * 64];
    while (n) {
        uint32_t k = n < 32u ? n : 32u, i;          /* 32 frames at a time: notes and the panel within a millisecond */
        dn_app_render(buf, k);
        for (i = 0; i < 2u * k; i++) {
            int32_t s = buf[i] > 32767 ? 32767 : buf[i] < -32768 ? -32768 : buf[i];
            lr[i] = (float)s * (1.0f / 32768.0f);
        }
        lr += 2u * k;
        n -= k;
        frames_acc += k * 1000u;
        while (frames_acc >= DNC_RATE) {
            frames_acc -= DNC_RATE;
            tick_ms();
        }
    }
}

void dnc_idle(uint32_t ms)
{
    while (ms--)
        tick_ms();
}

void dnc_button(int b, int down)
{
    if (b < 0 || b >= NB)
        return;
    held_btn = down ? held_btn | 1u << b : held_btn & ~(1u << b);
}
void dnc_key(int k, int down)
{
    if (k < 0 || k >= NKEYS)
        return;
    held_keys = down ? held_keys | 1u << k : held_keys & ~(1u << k);
}
void dnc_turn(int e, int d)
{
    if (e >= 0 && e < NE)
        enc_acc[e] += d;
}
void dnc_all_up(void) { held_btn = held_keys = 0; }

/* the host's notes: a track's instrument, as the sequencer plays it (velocity and all) */
static uint8_t host_note_trk[16][128];               /* the track each held note went to (+1; 0: none) */
static uint8_t dnc_note_for(uint32_t t, uint8_t note)
{
    if (DN_IS_PADS(t))                              /* a slice / a pad: C1 the first */
        return note >= 36u ? (uint8_t)(note - 36u) : 0xFFu;
    return note;
}
void dnc_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t ch = st & 15u, ty = st & 0xF0u, t;
    if (!dn.ready || d1 > 127u)
        return;
    if (ty == 0x90u && d2) {
        uint8_t nn;
        t = ch < 8u ? ch : dn.trk;
        nn = dnc_note_for(t, d1);
        if (nn == 0xFFu)
            return;
        if (host_note_trk[ch][d1])                  /* (the same note again before its end: that one ends first) */
            dn_sq_off1(host_note_trk[ch][d1] - 1u, dnc_note_for(host_note_trk[ch][d1] - 1u, d1));
        dn_sq_on1(t, nn, d2, 0, 0);
        host_note_trk[ch][d1] = (uint8_t)(t + 1u);
        if (DN_IS_RS(t))
            dn.midi_note = nn;                      /* (as MIDI in plays REESE: the keys leave it sounding) */
    } else if (ty == 0x80u || ty == 0x90u) {
        if (!host_note_trk[ch][d1])
            return;
        t = host_note_trk[ch][d1] - 1u;
        host_note_trk[ch][d1] = 0;
        if (DN_IS_RS(t) && dn.midi_note == dnc_note_for(t, d1))
            dn.midi_note = 0xFF;
        dn_sq_off1(t, dnc_note_for(t, d1));
    } else if (ty == 0xB0u && (d1 == 123u || d1 == 120u)) {   /* all notes off / all sound off */
        uint32_t n;
        for (n = 0; n < 128u; n++)
            if (host_note_trk[ch][n]) {
                t = host_note_trk[ch][n] - 1u;
                host_note_trk[ch][n] = 0;
                dn_sq_off1(t, dnc_note_for(t, (uint8_t)n));
            }
        dn.midi_note = 0xFF;
        reese_note_off(&dn_rs);
    }
}
int dnc_midi_out(uint8_t msg[3])
{
    uint32_t p;
    if (mo_r == mo_w)
        return 0;
    p = mo_ring[mo_r++ & 255u];
    msg[0] = (uint8_t)(p >> 8), msg[1] = (uint8_t)(p >> 16), msg[2] = (uint8_t)(p >> 24);
    return 1;
}

/* the transport: PLAY's own (src/app/dn_app.c, B_PLAY) when the host starts and stops */
static void dnc_start(void)
{
    dn.cin_left = 0;
    dn.clk_last = 0;
    dn_seq_start(&dn_sq, &DN_SQ_CB);
    dn.song_start = dn.song_sel;
    dn.song_jump = 0;
    dn.redraw |= DN_RD_ALL;
}
static void dnc_stop(void)
{
    uint32_t k;
    dn.cin_left = 0;
    if (dn_sq.playing) {
        dn_seq_stop(&dn_sq, &DN_SQ_CB);
        for (k = 0; k < DN_NBRK; k++)
            dn_brk_cut(&dn_bk[k]);
    }
    dn.redraw |= DN_RD_ALL;
}
void dnc_transport(int playing, double bpm)
{
    uint32_t k, b;
    if (!dn.ready)
        return;
    if (bpm > 0.0) {
        b = (uint32_t)(bpm + 0.5);
        b = b < 60u ? 60u : b > 200u ? 200u : b;
        if (b != host_bpm || b != dn_sq.bpm) {      /* the host's tempo (not a change to save) */
            host_bpm = b;
            dn_rs.bpm = dn_sq.bpm = (uint16_t)b;
            for (k = 0; k < DN_NBRK; k++)
                dn_brk_tempo(&dn_bk[k], dn_sq.bpm);
            dn.redraw |= DN_RD_ALL;
        }
    }
    playing = playing != 0;
    if (playing != host_playing) {
        host_playing = playing;
        if (playing && !dn_sq.playing)
            dnc_start();
        else if (!playing && dn_sq.playing)
            dnc_stop();
    }
}
int dnc_playing(void) { return dn_sq.playing != 0; }
uint32_t dnc_bpm(void) { return dn_sq.bpm; }

uint32_t dnc_screen_serial(void) { return fb_serial; }
void dnc_screen_copy(uint32_t *argb)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8));
        uint32_t r = ((c >> 11) & 31u) * 255u / 31u, g = ((c >> 5) & 63u) * 255u / 63u, b = (c & 31u) * 255u / 31u;
        argb[i] = 0xFF000000u | r << 16 | g << 8 | b;
    }
}
uint32_t dnc_leds_buttons(void) { return lit_btn; }
uint32_t dnc_leds_keys(void) { return lit_keys; }
uint32_t dnc_leds_keys_dim(void) { return dim_keys; }

/* ------------------------------------------------------------------------------------------------ the state --- */
/* "DNPS", a version, then each flash object and settings object: its length (-1: none) and its bytes */
#define DNC_MAGIC 0x53504E44u
#define DNC_NOBJ (OBJ_NOBJ + DN_NEXT)
uint32_t dnc_state_max(void) { return 8u + DNC_NOBJ * (4u + PLAT_STORE_MAX); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t dnc_state_save(uint8_t *out, uint32_t max)
{
    uint32_t o = 8, i;
    if (max < dnc_state_max())
        return 0;
    if (dn.ready)
        dn_save();                                  /* (as SAVE does: the sound, the patterns, the song) */
    put32(out, DNC_MAGIC);
    put32(out + 4, 1);
    for (i = 0; i < DNC_NOBJ; i++) {
        int n = i < OBJ_NOBJ ? store_len[i] : sim_ext_len[i - OBJ_NOBJ];
        const uint8_t *d = i < OBJ_NOBJ ? store[i] : sim_ext[i - OBJ_NOBJ];
        put32(out + o, (uint32_t)n);
        o += 4;
        if (n > 0) {
            memcpy(out + o, d, (size_t)n);
            o += (uint32_t)n;
        }
    }
    return o;
}
int dnc_state_load(const uint8_t *in, uint32_t n)
{
    uint32_t o = 8, i;
    static int len[DNC_NOBJ];
    static uint8_t data[DNC_NOBJ][PLAT_STORE_MAX];
    if (n < 8u || get32(in) != DNC_MAGIC || get32(in + 4) != 1u)
        return -1;
    for (i = 0; i < DNC_NOBJ; i++) {                /* (all of it read first: a bad one changes nothing) */
        int32_t l;
        if (o + 4u > n)
            return -1;
        l = (int32_t)get32(in + o);
        o += 4;
        if (l > (int32_t)PLAT_STORE_MAX || (l > 0 && o + (uint32_t)l > n))
            return -1;
        len[i] = l < 0 ? -1 : l;
        if (l > 0) {
            memcpy(data[i], in + o, (size_t)l);
            o += (uint32_t)l;
        }
    }
    for (i = 0; i < DNC_NOBJ; i++) {
        int *pl = i < OBJ_NOBJ ? &store_len[i] : &sim_ext_len[i - OBJ_NOBJ];
        uint8_t *d = i < OBJ_NOBJ ? store[i] : sim_ext[i - OBJ_NOBJ];
        *pl = len[i];
        if (len[i] > 0)
            memcpy(d, data[i], (size_t)len[i]);
    }
    return 0;
}
