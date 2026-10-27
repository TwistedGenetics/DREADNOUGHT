/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT as a plugin: the C core. The app (src/app/dn_app.c) and the engines on a simulated FM-1, as
 * tests/host/dn_host.c runs them for the tests, here driven by the plugin instead of a script: its panel (buttons,
 * encoders, keys), the host's MIDI and transport, the screen into a framebuffer, the flash objects into the
 * plugin's state. Copyright (C) 2026 Jamie (TwistedGenetics); the screen and panel stubs follow X0X's
 * host/x0x_host.c (Charles Vestal, GPL-3.0-only). */
#define X0X_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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
                fb[(y + j) * 240u + x + i] = px[j * w + i];
    fb_serial++;
}
#include "../../x0x-firmware/firmware/src/gfx.c"
#ifndef DNC_NO_ATTR_UNDEF
#undef __attribute__
#define __attribute__(x)
#endif

/* --------------------------------------------------------------------------------------------- platform --- */
#include "../../x0x-firmware/firmware/src/app/plat.h"
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
/* the sample slots (0.45): the FM-1's three (80, 80 and 64 KB), in RAM, as X0X's slots.c reads them: the web editor
 * fills them over the plugin's MIDI port (dnc_sysex: the FM-1's slot commands), and the plugin's state keeps them. A
 * new plugin starts with ROLLER and STEPPER in slot 1 (the loops the FM-1 had built in before 0.29), as if sent. */
#include "dn_breaks_builtin.h"
static uint32_t slot_ram32[3][0x14000u / 4u];       /* (words: the header read in place, a wavetable's data 2-aligned) */
#define SMP_USER_XIP(k) ((const uint8_t *)slot_ram32[k])
#include "../../x0x-firmware/firmware/src/app/slots.c"
static uint8_t *slot_mem(uint32_t k) { return (uint8_t *)slot_ram32[k]; }
static uint32_t slot_size(uint32_t k) { return k == 2u ? 64u * 1024u : SMP_USER_SIZE; }
int plat_slot_zones(int k) { return (k >= 0 && k < SMP_USER_SLOTS) ? usr_nz[k] : 0; }
const uint8_t *plat_slot(int k, int z, uint32_t *nsamples, uint32_t *rate, char name[9])
{
    const smp_user_hdr_t *h;
    uint32_t i;
    if (k < 0 || k >= SMP_USER_SLOTS || z < 0 || z >= usr_nz[k])
        return 0;
    h = (const smp_user_hdr_t *)smp_user_xip((uint32_t)k);
    for (i = 0; i < 8u; i++)
        name[i] = h->name[i] >= 32 && h->name[i] < 127 ? h->name[i] : 0;
    name[8] = 0;
    *nsamples = h->zone[z].n;
    *rate = (h->zone[z].rate * 44100u + 32768u) >> 16;
    return smp_user_xip((uint32_t)k) + SMP_USER_DATA + h->zone[z].off;
}
static uint32_t crc32_z(const uint8_t *b, uint32_t n)   /* zlib's (the slots' check) */
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static void slot_clear(uint32_t k)
{
    memset(slot_mem(k), 0xFF, SMP_USER_SIZE);
    usr_nz[k] = 0;
}
static void smp_names_factory(void);
static void slots_factory(void)                     /* slot 1: ROLLER and STEPPER (as the editor would pack them) */
{
    smp_user_hdr_t h;
    uint32_t z, off = 0;
    uint8_t *d = slot_mem(0) + SMP_USER_DATA;
    for (z = 0; z < 3u; z++)
        slot_clear(z);
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)DN_BRK_BUILTIN;
    memcpy(h.name, "BREAKS\0\0", 8);
    for (z = 0; z < DN_BRK_BUILTIN; z++) {
        uint32_t nb = (DN_BRK_BUILTIN_LOOPS[z].n + 1u) / 2u;
        smp_zone_t *zn = &h.zone[z];
        memcpy(d + off, DN_BRK_BUILTIN_LOOPS[z].adpcm, nb);
        zn->off = off;
        zn->n = DN_BRK_BUILTIN_LOOPS[z].n;
        zn->ls = 0;
        zn->le = zn->n - 1u;
        zn->rate = (uint32_t)(((uint64_t)DN_BRK_BUILTIN_LOOPS[z].rate * 65536u + 22050u) / 44100u);
        zn->root16 = (int16_t)((60 + (int)z) * 16);
        zn->lo = zn->hi = (uint8_t)(60u + z);
        off += z + 1u < DN_BRK_BUILTIN ? (nb + 1u) & ~1u : nb;
    }
    h.data_len = off;
    h.crc = crc32_z(d, off);
    memcpy(slot_mem(0), &h, sizeof h);
    smp_user_scan(0);
    smp_names_factory();
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

static int slots_ready;                             /* (the slots set: by a state, or the factory's) */
void dnc_init(void)
{
    if (!slots_ready) {
        slots_factory();
        slots_ready = 1;
    }
    memset(&dn, 0, sizeof dn);
    held_btn = held_keys = 0;
    memset(enc_acc, 0, sizeof enc_acc);
    mo_w = mo_r = 0;
    frames_acc = 0;
    host_playing = 0;
    host_bpm = 0;
    palette_set(0);
    dn_app_init();
    dn_splash(DN_VERSION);
    splash_left = 1200u;
}

static int draw_on = 1;
void dnc_set_draw(int on) { draw_on = on; }
static void tick_ms(void)
{
    now_ms++;
    dn_app_input();
    if (!draw_on) {
        splash_left = 0;
    } else if (splash_left) {
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

/* ------------------------------------------------------------------------------------------- the editor's frames --- */
/* The web editor's frames (F0 7D 46 4C cmd args F7), as the FM-1 answers them (src/hardware/dn_main_fm1.c ed_handle):
 * INFO, PING, the slot commands (into the RAM slots), the slices, and the rest by the app (dn_ed_cmd). Replies queue
 * in a ring the plugin sends from (dnc_sysex_out). */
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO, ED_SMP_READ, ED_PING = 25,
       ED_SLICE_GET = 50, ED_SLICE_SET = 51 };
#define ED_READ_MAX 224u
static uint8_t ed_out[1200];
static uint32_t ed_n;
static uint8_t eo_ring[1u << 16];                   /* replies, each: its length (2 bytes), its bytes */
static uint32_t eo_w, eo_r;
static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0, ed_out[1] = ED_HDR0, ed_out[2] = ED_HDR1, ed_out[3] = ED_HDR2, ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static void ed_send(void)
{
    uint32_t i;
    ed_out[ed_n++] = 0xF7;
    if (sizeof eo_ring - (eo_w - eo_r) < ed_n + 2u)
        return;                                     /* (nobody reading: dropped) */
    eo_ring[eo_w++ & (sizeof eo_ring - 1u)] = (uint8_t)ed_n;
    eo_ring[eo_w++ & (sizeof eo_ring - 1u)] = (uint8_t)(ed_n >> 8);
    for (i = 0; i < ed_n; i++)
        eo_ring[eo_w++ & (sizeof eo_ring - 1u)] = ed_out[i];
}
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
static uint8_t ed_smp_buf[512];
static void ed_handle(const uint8_t *f, uint32_t n)   /* f: from 7D, without F0 / F7 */
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
        ed_str("DREADNOUGHT " DN_VERSION " PLUGIN", 24);
        for (i = 0; i < 6u; i++)
            ed_b(0);
        break;
    case ED_PING:
        ed_b(0);
        break;
    case ED_SMP_BEGIN:
    case ED_SMP_ERASE:
        if (na < 1u || a[0] >= SMP_USER_SLOTS)
            return;
        dn_app_slots_changed(0);                    /* (the instruments let go of the slot first) */
        usr_nz[a[0]] = 0;
        memset(slot_mem(a[0]), 0xFF, cmd == ED_SMP_ERASE ? slot_size(a[0]) : 0x1000u);
        ed_b(a[0]);
        ed_b(0);
        break;
    case ED_SMP_WRITE: {
        uint32_t off, len, rc = 0;
        if (na < 5u || a[0] >= SMP_USER_SLOTS)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = ed_unpack7(a + 4, na - 4u, ed_smp_buf, 256u);
        if (off < SMP_USER_DATA || (off & 0xFFu) || !len || off + len > slot_size(a[0]))
            rc = 1;
        else if (usr_nz[a[0]])
            rc = 4;                                 /* (BEGIN first: it empties the slot) */
        else {
            if (!(off & 0xFFFu))
                memset(slot_mem(a[0]) + off, 0xFF, 0x1000u);
            memcpy(slot_mem(a[0]) + off, ed_smp_buf, len);
        }
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        break;
    }
    case ED_SMP_END: {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)(const void *)ed_smp_buf;
        uint32_t rc = 0, k;
        if (na < 2u || a[0] >= SMP_USER_SLOTS)
            return;
        k = a[0];
        if (ed_unpack7(a + 1, na - 1u, ed_smp_buf, sizeof(smp_user_hdr_t)) != sizeof(smp_user_hdr_t))
            rc = 1;
        else if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
                 h->data_len > slot_size(k) - SMP_USER_DATA)
            rc = 2;
        else if (crc32_z(slot_mem(k) + SMP_USER_DATA, h->data_len) != h->crc)
            rc = 3;
        else {
            memcpy(slot_mem(k), ed_smp_buf, sizeof(smp_user_hdr_t));
            smp_user_scan(k);
            dn_app_slots_changed(1);
            rc = usr_nz[k] ? 0u : 5u;
        }
        ed_b(k);
        ed_b(rc);
        break;
    }
    case ED_SMP_READ: {
        uint32_t off, cnt, k, j;
        const uint8_t *src;
        if (na < 5u || a[0] >= SMP_USER_SLOTS)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        cnt = a[4] > ED_READ_MAX / 2u ? ED_READ_MAX : a[4] * 2u;
        if (off >= slot_size(a[0]))
            return;
        if (cnt > slot_size(a[0]) - off)
            cnt = slot_size(a[0]) - off;
        src = slot_mem(a[0]) + off;
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        for (k = 0; k < cnt; k += 7) {
            uint32_t m = 0;
            for (j = 0; j < 7u && k + j < cnt; j++)
                m |= (uint32_t)(src[k + j] >> 7) << j;
            ed_b(m);
            for (j = 0; j < 7u && k + j < cnt; j++)
                ed_b(src[k + j]);
        }
        break;
    }
    case ED_SMP_INFO:
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)(const void *)smp_user_xip(i);
            char nm[9] = {0};
            uint32_t j;
            ed_b(usr_nz[i]);
            for (j = 0; usr_nz[i] && j < 8u; j++)
                nm[j] = h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
            ed_str(nm, 8);
            ed_b(usr_nz[i] ? (h->data_len + 1023u) / 1024u : 0u);
        }
        break;
    case ED_SLICE_GET:
    case ED_SLICE_SET: {
        static uint32_t st[DN_BRK_MAXSL];
        uint32_t key, cnt, j;
        if (na < 5u)
            return;
        key = (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
        for (j = 0; j < 5u; j++)
            ed_b(a[j]);
        if (cmd == ED_SLICE_GET) {
            cnt = dn_app_slices_get(key, st);
            ed_b(cnt);
            for (j = 0; j < cnt; j++) {
                ed_b(st[j]);
                ed_b(st[j] >> 7);
                ed_b(st[j] >> 14);
            }
        } else {
            cnt = na > 5u ? a[5] : 0u;
            if (cnt > DN_BRK_MAXSL || na < 6u + 3u * cnt) {
                ed_b(1);
                break;
            }
            for (j = 0; j < cnt; j++)
                st[j] = (uint32_t)a[6u + 3u * j] | (uint32_t)a[7u + 3u * j] << 7 | (uint32_t)a[8u + 3u * j] << 14;
            ed_b((uint32_t)dn_app_slices_set(key, cnt, st));
        }
        break;
    }
    default:
        if (!dn_ed_cmd(cmd, a, na, ed_b))
            return;                                 /* not ours: no reply */
        break;
    }
    ed_send();
}
int dnc_sysex(const uint8_t *m, uint32_t n)
{
    if (n && m[0] == 0xF0u)
        m++, n--;
    if (n && m[n - 1u] == 0xF7u)
        n--;
    if (n < 4u || m[0] != ED_HDR0 || m[1] != ED_HDR1 || m[2] != ED_HDR2)
        return 0;
    ed_handle(m, n);
    return 1;
}
uint32_t dnc_sysex_out(uint8_t *out, uint32_t max)
{
    uint32_t len, i;
    if (eo_r == eo_w)
        return 0;
    len = eo_ring[eo_r & (sizeof eo_ring - 1u)] | (uint32_t)eo_ring[(eo_r + 1u) & (sizeof eo_ring - 1u)] << 8;
    if (len > max) {                                /* (too long for the caller: dropped) */
        eo_r += 2u + len;
        return 0;
    }
    eo_r += 2u;
    for (i = 0; i < len; i++)
        out[i] = eo_ring[eo_r++ & (sizeof eo_ring - 1u)];
    return len;
}
uint32_t dnc_slots_used(void)                         /* (KB: what the slots hold) */
{
    uint32_t k, b = 0;
    for (k = 0; k < SMP_USER_SLOTS; k++)
        if (usr_nz[k])
            b += ((const smp_user_hdr_t *)(const void *)smp_user_xip(k))->data_len;
    return (b + 1023u) / 1024u;
}

#include "dnc_api.c"

/* ------------------------------------------------------------------------------------------------ the state --- */
/* (3: 0.45's GUI) then the samples' names: 48 x 24 bytes */
/* "DNPS", a version (2: 0.45), then each flash object and settings object: its length (-1: none) and its bytes; then
 * (2) each sample slot: its length (-1: empty) and its bytes (the header's 512 and the data). A version 1 state (0.44)
 * has no slots: the factory's (ROLLER, STEPPER), which 0.44 played. */
#define DNC_MAGIC 0x53504E44u
#define DNC_NOBJ (OBJ_NOBJ + DN_NEXT)
uint32_t dnc_state_max(void) { return 8u + DNC_NOBJ * (4u + PLAT_STORE_MAX) + 3u * (4u + SMP_USER_SIZE) + sizeof smp_names; }
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
    put32(out + 4, 3);
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
    for (i = 0; i < SMP_USER_SLOTS; i++) {
        uint32_t n = usr_nz[i] ? SMP_USER_DATA + ((const smp_user_hdr_t *)(const void *)smp_user_xip(i))->data_len : 0u;
        put32(out + o, n ? n : 0xFFFFFFFFu);
        o += 4;
        memcpy(out + o, slot_mem(i), n);
        o += n;
    }
    memcpy(out + o, smp_names, sizeof smp_names);
    o += sizeof smp_names;
    return o;
}
int dnc_state_load(const uint8_t *in, uint32_t n)
{
    uint32_t o = 8, i, ver;
    static int len[DNC_NOBJ];
    static uint8_t data[DNC_NOBJ][PLAT_STORE_MAX];
    static uint32_t slen[SMP_USER_SLOTS], soff[SMP_USER_SLOTS];
    if (n < 8u || get32(in) != DNC_MAGIC || (ver = get32(in + 4)) < 1u || ver > 3u)
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
    for (i = 0; ver >= 2u && i < SMP_USER_SLOTS; i++) {
        uint32_t l;
        if (o + 4u > n)
            return -1;
        l = get32(in + o);
        o += 4;
        if (l == 0xFFFFFFFFu)
            l = 0;
        if (l && (l < SMP_USER_DATA || l > slot_size(i) || o + l > n))
            return -1;
        slen[i] = l, soff[i] = o;
        o += l;
    }
    if (ver >= 3u && o + sizeof smp_names > n)
        return -1;
    for (i = 0; i < DNC_NOBJ; i++) {
        int *pl = i < OBJ_NOBJ ? &store_len[i] : &sim_ext_len[i - OBJ_NOBJ];
        uint8_t *d = i < OBJ_NOBJ ? store[i] : sim_ext[i - OBJ_NOBJ];
        *pl = len[i];
        if (len[i] > 0)
            memcpy(d, data[i], (size_t)len[i]);
    }
    if (ver >= 2u) {
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            slot_clear(i);
            memcpy(slot_mem(i), in + soff[i], slen[i]);
            if (slen[i])
                smp_user_scan(i);
        }
        if (ver >= 3u) {
            memcpy(smp_names, in + o, sizeof smp_names);
            for (i = 0; i < 48u; i++)
                smp_names[i][23] = 0;
        } else {
            memset(smp_names, 0, sizeof smp_names);
        }
    } else {
        slots_factory();
    }
    slots_ready = 1;
    return 0;
}

static void smp_names_factory(void)
{
    memset(smp_names, 0, sizeof smp_names);
    memcpy(smp_names[0], "ROLLER", 7);
    memcpy(smp_names[1], "STEPPER", 8);
}
