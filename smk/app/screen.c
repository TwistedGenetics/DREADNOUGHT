/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * DREADNOUGHT on the SMK-37 Pro / Elite: the app over the board. Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * The SDK's control task calls groove_screen_init once and groove_screen_control over and over: here DREADNOUGHT
 * starts on the first call (the flash is up by then), reads the panel, runs dn_app_input, and draws every 16 ms. The
 * audio interrupt (board/audio.c) calls dn_smk_render for each block of 128 frames.
 *
 * The panel, onto the FM-1's (DREADNOUGHT's panel is the FM-1's: 14 buttons, 27 keys F3..G5, 7 encoders):
 *   keys        the 37 keys; F (the 6th) .. G two octaves up are the FM-1's 27 (OCT moves them)
 *   pads        the 16 white keys of the FM-1 (pad 1 = F3, the first step on SEQ, .. pad 16 = G5); lit as those keys
 *   K1..K4      the four dials;  SELECT  SELECT;  PAGE  ALGORITHM (the tempo);  PRESETS  PRESETS;  the 8th: volume
 *   buttons     each the FM-1 button of the same name; SHIFT + ENV is LFO
 * (The SMK's physical button numbers are the board scanner's, app/board/scan.c; their FM-1 roles are the ones M-VAVE's
 * own APP gives them.) */
#include "fm1.h"
#include "fm1_device.h"
#include "fm1_engine_control.h"
#include "smk37_controls.h"
#include "dn_smk.h"
#include "dn/dn_smk_version.h"                     /* (DN_VERSION: tools/smk_assemble.py, from VERSION) */
#include <string.h>

int groove_master_update(unsigned adc);              /* (the SDK: the output level, from a knob's 0..1023) */

/* ------------------------------------------------------------------------------------------------- screen --- */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (x < 240u && y < 240u && w && h && x + w <= 240u && y + h <= 240u)
        (void)fm1_screen_blit(x, y, w, h, px, (size_t)w * h);
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (x < 240u && y < 240u && w && h)
        fm1_screen_fill(x, y, w > 240u - x ? 240u - x : w, h > 240u - y ? 240u - y : h, c);
}
#include "dn/dn_gfx.h"
#include "dn/dn_plat.h"

/* -------------------------------------------------------------------------------------------------- panel --- */
enum { PH_SHIFT = 0, PH_BANK = 1, PH_PAD = 7, PH_N = 18 };
/* the physical buttons' FM-1 roles (-1: none of DREADNOUGHT's) */
static const int8_t PH_ROLE[PH_N] = {-1, -1, B_HOME, B_SAVE, B_OCTDN, B_OCTUP, B_ARP, -1,
                                     B_SEL, B_ENV, B_GLO, -1, B_HOME, B_EDIT, B_FX, B_SEQ, B_PLAY, B_REC};
/* the physical encoders' roles; the 8th is the volume */
static const uint8_t EN_ROLE[7] = {EN_K1, EN_K2, EN_K3, EN_K4, EN_SELECT, EN_ALGO, EN_PRESET};
/* the FM-1's white keys, the pads' */
static const uint8_t PAD_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
#define KEY_FIRST 5u                                 /* the SMK's key for the FM-1's F3 */

static uint32_t ph_down, ph_as_lfo, keys_down, pads_down, btn_held;
static int32_t enc_acc[NE];
static uint8_t volume = 100;
static uint32_t lit_btn = ~0u, lit_keys, dim_keys;

void smk37_control_event(unsigned kind, unsigned id, int value)
{
    unsigned i;
    if (kind == SMK37_KEY && id < 37u) {
        if (id >= KEY_FIRST && id < KEY_FIRST + NKEYS) {
            uint32_t b = 1u << (id - KEY_FIRST);
            keys_down = value ? keys_down | b : keys_down & ~b;
        }
    } else if (kind == SMK37_PAD && id < 16u) {
        pads_down = value ? pads_down | (1u << id) : pads_down & ~(1u << id);
    } else if (kind == SMK37_ENCODER && id < 8u) {
        if (id < 7u)
            enc_acc[EN_ROLE[id]] += value;
        else {
            int v = (int)volume + value;
            volume = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        }
    } else if (kind == SMK37_BUTTON && id < PH_N) {
        uint32_t b = 1u << id;
        if (value) {
            if (id == 9u && (ph_down & (1u << PH_SHIFT)))
                ph_as_lfo |= b;                      /* SHIFT + ENV: LFO (kept until ENV is let go) */
            ph_down |= b;
        } else {
            ph_down &= ~b;
            ph_as_lfo &= ~b;
        }
        btn_held = 0;
        for (i = 0; i < PH_N; i++)
            if ((ph_down & (1u << i)) && PH_ROLE[i] >= 0)
                btn_held |= 1u << ((ph_as_lfo & (1u << i)) ? B_LFO : PH_ROLE[i]);
    }
}
void smk37_controls_panic(void)                      /* (events were lost: nothing is held) */
{
    ph_down = ph_as_lfo = keys_down = pads_down = btn_held = 0;
}
void smk37_controls_snapshot(struct fm1_controls *out) { memset(out, 0, sizeof *out); }
unsigned smk37_fader_bank(void) { return 0; }
unsigned smk37_pad_steps(void) { return 0; }
unsigned smk37_pad_focus(void) { return 0; }
unsigned smk37_screen_pad_key(unsigned pad) { return pad < 16u ? PAD_KEY[pad] : 255u; }
uint32_t smk37_pad_color(unsigned pad, unsigned lit)
{
    uint32_t k = pad < 16u ? 1u << PAD_KEY[pad] : 0u;
    (void)lit;
    if (pads_down & (1u << pad))
        return 0x303030u;                            /* held: white */
    if (lit_keys & k)
        return 0x301000u;                            /* lit: DREADNOUGHT's orange */
    if (dim_keys & k)
        return 0x080300u;
    return 0x020202u;
}

/* ---------------------------------------------------------------------------------------------- platform --- */
uint32_t plat_ms(void) { return fm1_millis(); }
uint32_t plat_buttons(void) { return btn_held; }
uint32_t plat_keys(void)
{
    uint32_t k = keys_down, i;
    for (i = 0; i < 16u; i++)
        if (pads_down & (1u << i))
            k |= 1u << PAD_KEY[i];
    return k;
}
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return 4096u; }
int plat_battery(void) { return 4; }
/* the FM-1's button numbers on M-VAVE's FM-1 panel API (fm1_led_set): OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP
 * SEQ PLAY REC */
static const uint8_t LED_OF[NB] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
void plat_leds(uint32_t b, uint32_t k)
{
    uint32_t i, ch = b ^ lit_btn;
    for (i = 0; i < NB; i++)
        if (ch & (1u << i))
            fm1_led_set(LED_OF[i], (b >> i) & 1);
    ch = k ^ lit_keys;
    for (i = 0; i < NKEYS; i++)
        if (ch & (1u << i))
            fm1_led_set(14u + i, (k >> i) & 1);
    lit_btn = b;
    lit_keys = k;
}
void plat_glow(int on) { (void)on; }
void plat_keys_dim(uint32_t k) { dim_keys = k; }
void plat_play_red(int red) { (void)red; }
int plat_midi_in(uint32_t *pkt) { return dn_smk_midi_in(pkt); }
void plat_midi_out(uint32_t pkt) { dn_smk_midi_out(pkt); }
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return dn_smk_store_load(obj, dst, max); }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return dn_smk_store_save(obj, src, len); }
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max) { (void)obj, (void)dst, (void)max; return -1; }
int plat_slot_zones(int k) { return dn_smk_slot_zones(k); }
const uint8_t *plat_slot(int k, int z, uint32_t *n, uint32_t *rate, char name[9]) { return dn_smk_slot(k, z, n, rate, name); }
/* one core for DREADNOUGHT */
int plat_cpu2_set(int on) { (void)on; return -1; }
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 1; }
/* the meters: the render's share of the block's time, as the audio interrupt measures it */
static volatile uint32_t cpu_q8, cpu_peak, xruns;
uint32_t plat_cpu_pct(void) { return (cpu_q8 * 100u) >> 8; }
uint32_t plat_cpu_peak_pct(void) { return cpu_peak; }
uint32_t plat_xruns(void)
{
    struct fm1_audio_stats s;
    fm1_audio_stats_read(&s);
    return s.overruns;
}
int plat_vdc14_mv(void) { return 0; }
void plat_perf_init(void) {}
uint32_t plat_cycles(void) { return fm1_device_ticks(); }
int plat_cycles_cpu(void) { return 0; }
uint32_t plat_cycles_hz(void) { return 24000000u; }
uint32_t plat_ticks24(void) { return fm1_device_ticks(); }
uint32_t plat_irq_stack_used(void) { return 0; }
uint32_t plat_irq_stack_size(void) { return 0; }
int plat_stalls(uint32_t s[3]) { s[0] = s[1] = s[2] = 0; return 0; }
void plat_stalls_enable(int on) { (void)on; }

/* the app's edits against the audio interrupt: interrupts off, as on the FM-1 (nested: the outermost restores) */
static uint32_t lock_state, lock_depth;
static void dn_lock(void)
{
    uint32_t s = fm1_device_irq_save();
    if (!lock_depth++)
        lock_state = s;
}
static void dn_unlock(void)
{
    if (lock_depth && !--lock_depth)
        fm1_device_irq_restore(lock_state);
}

_Static_assert(sizeof dg_strip >= 4u * 3840u, "DN_PBUF_SHARED: the screen band holds the patterns' store buffer");
#include "dn/dn_app.inc"
#include "dn/dn_splash.inc"

/* the settings objects (on the FM-1, slot 3's top): store objects of their own here */
int dn_ext_load(uint32_t i, void *dst, uint32_t max) { return i < DN_NEXT ? dn_smk_store_load(DN_OBJ_EXT0 + i, dst, max) : -1; }
int dn_ext_save(uint32_t i, const void *src, uint32_t len)
{
    return i < DN_NEXT ? dn_smk_store_save(DN_OBJ_EXT0 + i, src, len) : -1;
}

/* ---------------------------------------------------------------------------------------------- the audio --- */
volatile uint8_t dn_smk_ready;
#define BLOCK_TICKS (24000000u / 44100u * 128u)      /* a block's time in 24 MHz ticks (~2.9 ms) */
void dn_smk_render(int32_t *o, unsigned frames)
{
    uint32_t t0 = fm1_device_ticks(), i, pkt, guard = 64, pct;
    if (!dn_smk_ready) {
        for (i = 0; i < 2u * frames; i++)
            o[i] = 0;
        return;
    }
    while (guard-- && dn_smk_midi_in(&pkt)) {
        uint8_t st = (uint8_t)(pkt >> 8);
        if (st >= 0x80u && st < 0xF0u)
            dn_app_midi(st, (uint8_t)((pkt >> 16) & 0x7Fu), (uint8_t)((pkt >> 24) & 0x7Fu));
    }
    dn_app_render(o, frames);
    for (i = 0; i < 2u * frames; i++) {             /* Q15 to Q23 */
        int32_t v = o[i];
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        o[i] = v * 256;
    }
    pct = (fm1_device_ticks() - t0) * 100u / BLOCK_TICKS;
    cpu_q8 = (cpu_q8 * 15u + pct * 256u / 100u) / 16u;
    if (pct > cpu_peak)
        cpu_peak = pct;
    (void)xruns;
}

/* ------------------------------------------------------------------------------------------- control task --- */
static uint8_t started;
static uint32_t splash_until, drawn_at, knob_sent = ~0u;

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, 240, 240, 0);
}

static void start(void)
{
    dn_smk_store_init();                             /* (the store's records and the slots, read) */
    dn_app_init();
    dn_splash(DN_VERSION);
    splash_until = fm1_millis() + 1200u;
    dn_smk_ready = 1;
    started = 1;
}

void groove_screen_control(void)
{
    struct fm1_controls c;
    uint32_t now, knob;
    if (!started)
        start();
    fm1_controls_read(&c);                           /* (the board's scan: smk37_control_event for each change) */
    knob = (volume * 1023u + 63u) / 127u;
    if (knob != knob_sent) {
        (void)groove_master_update(knob);
        knob_sent = knob;
    }
    dn_app_input();
    dn_smk_midi_poll();
    dn_smk_editor_poll();
    now = fm1_millis();
    if (splash_until) {
        if ((int32_t)(now - splash_until) >= 0) {
            splash_until = 0;
            dn_app_draw(1);
            drawn_at = now;
        }
    } else if (now - drawn_at >= 16u) {
        dn_app_draw(0);
        drawn_at = now;
    }
}
