/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT on the FM-1: boot, safe mode, crash screen, audio interrupt and main loop, over X0X's platform.
 * Adapted from Carnac OS's src/hardware/synth_main_fm1.c (owenkirby/carnac-os), itself adapted from X0X's
 * firmware/src/app/main_fm1.c (charlesvestal/fm1-x0x), from Felucca's main.c / audio.c, Copyright (C) 2026 Leo
 * Kuroshita (@kurogedelic), Hugelton Instruments. All GPL-3.0-only. DREADNOUGHT's changes: Copyright (C) 2026 Jamie
 * (TwistedGenetics).
 *
 * A unity-build fragment (tools/build_fm1.py): X0X's firmware/src/x0x.c up to, not including, its app; then
 * src/app/dn_app.c (the app); then this file. The engines (src/dsp/*.c) are one separate -O2 unit.
 *
 * Kept from X0X / Carnac unchanged, because they are what makes a bad build recoverable:
 *   - the boot-loop guard: a crash or hang in the first 30 s counts as a failed boot; after two, SAFE MODE (no sound,
 *     USB on, the web flasher reaches it; PLAY tries again); after four, the chip's own update mode (UBOOT);
 *   - the M-UPGRADE update entry (ota_service) in the main loop and in safe mode: the web flasher always works;
 *   - OCT- + OCT+ held 5 s: UBOOT, from the main loop and from safe mode;
 *   - OCT- + OCT+ held at power-on: HARDWARE CALIBRATION (X0X's panel map);
 *   - the crash screen and the watchdog.
 * Who touches the sound: the audio interrupt (ALNK0) renders and reads USB / TRS MIDI; the main loop reads the panel
 * through dn_app_input(), whose edits run inside dn_lock() (interrupts off for a few microseconds). */
#include "dn_app.h"

#ifndef DN_VERSION
#define DN_VERSION "DEV"
#endif

/* X0X's platform pieces that are not its app: the user-slot layout, the panel map and its calibration, plat.h */
#include "app/slots.c"
#include "app/panel.c"
#include "app/plat_fm1.c"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[], _c1_start[], _c1_end[], _c1_load[];
extern uint32_t _rt_text_end[] __attribute__((weak));   /* (Carnac's --dsp-ram layout; undefined here: reads 0) */

static void dn_lock(void) { (void)irq_save(); }
static void dn_unlock(void) { irq_restore(0); }

#include "dn_app.c"
#include "dn_splash.c"

/* ---------------------------------------------------------------------------------------------------- audio --- */
#define FS 44100u
#define HALF_FRAMES 512u                            /* X0X's halves: 11.6 ms */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define REND_FRAMES 128u
#if FELUCCA_UAC
#define DN_UAC_FRAMES 256u                          /* usb.c sizes its ring band for this */
_Static_assert(DN_UAC_FRAMES == UA_HALF, "usb.c's USB audio band");
#endif
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

#define DBG_MAGIC 0x444E5231u                       /* "DNR1": read with `fm1t memr` */
struct dn_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots, stage, crash_seen;
} dn_dbg __attribute__((section(".noinit")));
static uint8_t safe_mode;

static void midi_drain(void)                        /* USB and TRS MIDI in (USB-MIDI event packets). ISR only. */
{
    uint32_t pkt, guard = 64;
    while (guard-- && plat_midi_in(&pkt)) {
        uint32_t cin = pkt & 0x0Fu;
        uint8_t st = (uint8_t)(pkt >> 8);
        if (cin >= 0x8u && cin <= 0xEu && st >= 0x80u && st < 0xF0u)
            dn_app_midi(st, (uint8_t)((pkt >> 16) & 0x7Fu), (uint8_t)((pkt >> 24) & 0x7Fu));
    }
}

/* Q15 to the codec through the MASTER pot (master_q12, 0..4096), glided over the block: full MASTER puts Q15 full
 * scale at 2^22, X0X's level (Felucca's) */
static int32_t out_vol = -1;
static void out_level(int32_t *lr, uint32_t n)
{
    int32_t tgt = (int32_t)master_q12, v = out_vol < 0 ? tgt : out_vol, step = (tgt - v) / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++) {
        v += step;
        lr[2u * i] = (lr[2u * i] * v) >> 5;
        lr[2u * i + 1u] = (lr[2u * i + 1u] * v) >> 5;
    }
    out_vol = tgt;
}

static void render_block(int32_t *o)
{
    uint32_t t0 = fm1_ticks(), us, budget = HALF_FRAMES * 1000000u / FS, k;
    dn_dbg.in_audio = 1;
#if FELUCCA_UAC
    uac_render_start();
#endif
    for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
        midi_drain();
        dn_app_render(o + 2u * k, REND_FRAMES);
        out_level(o + 2u * k, REND_FRAMES);
    }
#if FELUCCA_UAC
    for (k = 0; k < HALF_FRAMES; k += DN_UAC_FRAMES)
        uac_tap(o + 2u * k, DN_UAC_FRAMES);         /* the USB audio input: the same output */
#endif
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    dn_dbg.cpu_q8 = (dn_dbg.cpu_q8 * 15u + (us * 256u) / budget) / 16u;
    audio_cpu_pct = (dn_dbg.cpu_q8 * 100u) >> 8;
    if (us * 100u / budget > audio_peak_pct)
        audio_peak_pct = us * 100u / budget;
    dn_dbg.halves++;
    dn_dbg.last_us = us;
    if (us > dn_dbg.max_us)
        dn_dbg.max_us = us;
    dn_dbg.in_audio = 0;
}

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        render_block(&abuf[half * HALF_WORDS]);
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) {
            dn_dbg.late++;                          /* the DMA moved on while we rendered */
            audio_xruns++;
        }
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ---------------------------------------------------------------------------------------------------- timer --- */
static uint8_t usb_due;
void fm1_timer5_irq(void)                           /* 10 kHz: the panel scan, USB at 2 kHz, the ms count */
{
    static uint32_t sub;
    fm1_timer5_ack();
    dn_dbg.timer_irqs++;
    if (dn_dbg.in_audio)
        dn_dbg.nested++;
    fm1_input_tick();
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC
        if (dn_dbg.in_audio)
            uac_service();                          /* one packet per 1 ms frame: cannot wait for the render */
#endif
    }
    if (usb_due && !dn_dbg.in_audio) {              /* usb_poll shares the MIDI queues with the audio ISR */
        usb_due = 0;
        usb_poll();
#if X0X_TRS
        uart_midi_poll();
#endif
    }
    if (++sub == 10u)
        sub = 0;
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ---------------------------------------------------------------------------------------------------- editor --- */
/* The editor frame (F0 7D 46 4C cmd args F7, Felucca's protocol, x0x-firmware/web/EDITOR_PROTOCOL.md): INFO, PING
 * and the sample-slot commands SMP_BEGIN .. SMP_READ (11..16), so the breaks page (web/breaks) and X0X's
 * tools/upload_breaks.py load loops into the three slots. The slot commands are X0X's app/editor.c, adapted from
 * Felucca's editor.c (Copyright (C) 2026 Leo Kuroshita, Hugelton Instruments), GPL-3.0-only; DREADNOUGHT's change:
 * BREAKS lets go of an uploaded loop before its slot is erased and picks it up again after (dn_app_slots_changed). */
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO, ED_SMP_READ, ED_PING = 25 };
#define ED_READ_MAX 224u                            /* SMP_READ's bytes a frame */
static uint8_t ed_out[288];
static uint32_t ed_n;
static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
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
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
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
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static uint32_t ed_smp_slot(uint32_t k) { return SMP_USER_BASE + k * SMP_USER_SIZE; }
static void ed_smp_inval(uint32_t k)
{
    fm1_irq_off();
    fl_inval(ed_smp_slot(k), SMP_USER_SIZE);
    fm1_irq_on();
}
static int ed_smp_erase(uint32_t k, uint32_t all)
{
    uint32_t i, took;
    int rc = 0;
    dn_app_slots_changed(0);                        /* BREAKS lets go of uploaded loops first */
    usr_nz[k] = 0;
    for (i = 0; i < (all ? SMP_USER_SIZE / 0x1000u : 1u) && !rc; i++) {
        audio_silence();
        rc = fl_erase4k(ed_smp_slot(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    ed_smp_inval(k);
    return rc;
}
static int ed_smp_end(uint32_t k, const uint8_t *a, uint32_t na)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)ed_smp_buf;
    if (ed_unpack7(a, na, ed_smp_buf, sizeof(smp_user_hdr_t)) != sizeof(smp_user_hdr_t))
        return 1;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_SIZE - SMP_USER_DATA)
        return 2;
    ed_smp_inval(k);
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, h->data_len) != h->crc)
        return 3;
    if (fl_write(ed_smp_slot(k), ed_smp_buf, sizeof(smp_user_hdr_t)))
        return 4;
    ed_smp_inval(k);
    smp_user_scan(k);
    dn_app_slots_changed(1);
    return usr_nz[k] ? 0 : 5;
}

static void ed_handle(const uint8_t *f, uint32_t n)
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
        ed_str("DREADNOUGHT " DN_VERSION, 24);
        for (i = 0; i < 6u; i++)
            ed_b(0);                                /* no engines / parameters over this protocol yet */
        break;
    case ED_PING:
        ed_b(0);
        break;
    case ED_SMP_BEGIN:
    case ED_SMP_ERASE:
        if (na < 1u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        ed_b(a[0]);
        ed_b(ed_smp_erase(a[0], cmd == ED_SMP_ERASE) ? 1u : 0u);
        break;
    case ED_SMP_WRITE: {
        uint32_t off, len, rc = 0, took;
        if (na < 5u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = ed_unpack7(a + 4, na - 4u, ed_smp_buf, 256u);
        if (off < SMP_USER_DATA || (off & 0xFFu) || !len || off + len > SMP_USER_SIZE)
            rc = 1;
        else if (usr_nz[a[0]])
            rc = 4;                                 /* (BEGIN first: it empties the slot) */
        else {
            if (!(off & 0xFFFu)) {
                audio_silence();
                rc = fl_erase4k(ed_smp_slot(a[0]) + off, &took) ? 2u : 0u;
            }
            if (!rc && fl_write(ed_smp_slot(a[0]) + off, ed_smp_buf, len))
                rc = 3;
        }
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        break;
    }
    case ED_SMP_END:
        if (na < 2u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        ed_b(a[0]);
        ed_b((uint32_t)ed_smp_end(a[0], a + 1, na - 1u));
        break;
    case ED_SMP_READ: {                             /* slot, offset (3 x 7 bits, from the slot's start), count (pairs)
                                                     * -> slot, offset, the bytes packed 7 to 8 */
        uint32_t off, cnt, k, j;
        const uint8_t *src;
        if (na < 5u || a[0] >= SMP_USER_SLOTS)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        cnt = a[4] > ED_READ_MAX / 2u ? ED_READ_MAX : a[4] * 2u;
        if (off >= SMP_USER_SIZE)
            return;
        if (cnt > SMP_USER_SIZE - off)
            cnt = SMP_USER_SIZE - off;
        src = smp_user_xip(a[0]) + off;
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
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(i);
            char nm[9] = {0};
            uint32_t j;
            ed_b(usr_nz[i]);
            for (j = 0; usr_nz[i] && j < 8u; j++)
                nm[j] = h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
            ed_str(nm, 8);
            ed_b(usr_nz[i] ? (h->data_len + 1023u) / 1024u : 0u);
        }
        break;
    default:
        return;                                     /* not ours: no reply */
    }
    ed_send();
}

static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;                                     /* not an editor frame: ota_service() reads it */
    if (p[3] >= ED_SMP_BEGIN && p[3] <= ED_SMP_READ) {
        ed_handle(p, n);                            /* long frames (data): handled in place */
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();
        ed_handle(f, k);
    }
}

/* ------------------------------------------------------------------------------------------------------ boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));   /* declared in x0x.c (ota_commit) */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    if (fm1_c1_on)
        fm1_cpu1_bar();
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "DREADNOUGHT CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

/* SAFE MODE: it crashed (or hung) twice while starting. Nothing that makes sound runs; USB is on, so the web flasher
 * (this firmware, X0X, Felucca, SLOOP or the stock one) and M-UPGRADE reach it. PLAY tries again. X0X's, but the name. */
static void safe_main(void)
{
    char b[24];
    uint32_t t0, play, both, t_oct;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_B, "DREADNOUGHT SAFE MODE", C_HI, 1);
    draw_text_box(0, 52, 240, &FONT_S, "IT CRASHED TWICE", C_WHITE, 1);
    draw_text_box(0, 72, 240, &FONT_S, "WHILE STARTING.", C_WHITE, 1);
    draw_text_box(0, 102, 240, &FONT_S, "NO SOUND. USB IS ON:", C_WHITE, 1);
    draw_text_box(0, 122, 240, &FONT_S, "REINSTALL FROM THE", C_WHITE, 1);
    draw_text_box(0, 142, 240, &FONT_S, "WEB FLASHER.", C_WHITE, 1);
    draw_text_box(0, 172, 240, &FONT_S, "PLAY: TRY AGAIN", C_HI, 1);
    draw_text_box(0, 190, 240, &FONT_S, "HOLD OCT- + OCT+: UPDATE", C_GRAY, 1);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 208, 240, &FONT_S, b, C_GRAY, 1);
    }
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    t0 = t_oct = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;                   /* try again, from scratch */
            bootguard.pending = 0;
            draw_text_box(0, 172, 240, &FONT_S, "STARTING...", C_HI, 1);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        if ((fm1_in.buttons & both) != both)
            t_oct = fm1_ms;
        else if (fm1_ms - t_oct > 5000u)
            enter_uboot("UBOOT");
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
    }
}

#define SPLASH_MS 1500u                             /* the start-up screen shows at least this long */

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t last_frame = 0, force = 1, splash_t0;
    {
        uint32_t f = irq_save();
        fm1_vdc14_raise(5);                         /* 1.50 V before anything loads it (fm1_sys.h) */
        plat_vdc14_level = (uint8_t)fm1_vdc14();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
        irq_restore(f);
        if (flash_ok) {
            uint32_t k;
            fl_plain_window_init();                 /* the store and the sample slots read as plaintext through XIP */
            for (k = 0; k < SMP_USER_SLOTS; k++)
                smp_user_scan(k);                   /* the uploaded loops BREAKS can play */
            ota_boot_cleanup();
        }
    }
    lcd_init();
    palette_set(2);                                 /* CYAN: the safe mode and update screens match Massive's blue */
    if (dn_dbg.magic != DBG_MAGIC) {
        uint32_t i;
        for (i = 0; i < sizeof dn_dbg / 4u; i++)
            ((uint32_t *)&dn_dbg)[i] = 0;
        dn_dbg.magic = DBG_MAGIC;
    }
    if (safe_mode)
        safe_main();                                /* never returns */
    dn_splash(DN_VERSION);
    splash_t0 = fm1_ticks();
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != dn_dbg.crash_seen) {
        char b[16];                                 /* a crash since the last boot: say so, a moment */
        dn_dbg.crash_seen = fm1_crash.count;
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 176, 240, &FONT_S, "RESTARTED AFTER A CRASH", RGB(255, 80, 60), 1);
        draw_text_box(0, 196, 240, &FONT_S, b, C_GRAY, 1);
        fm1_wdt_feed();
        fm1_delay_ms(2500);
        fm1_wdt_feed();
    }
    dn_dbg.boots++;
    dn_dbg.max_us = 0;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    led_pos_init();
    plat_perf_init();
    dn_dbg.stage = 1;                               /* breadcrumbs: a WDT reset leaves the last one */
    audio_init();
    usb_start();
#if X0X_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    fm1_timer5_start(isr_timer5, 4);                /* above ALNK0 (3): the panel scan keeps its rhythm */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u)
        panel_setup();                              /* OCT- + OCT+ held at power-on: HARDWARE CALIBRATION */
    dn_dbg.stage = 2;
    dn_app_init();                                  /* the saved sound; the audio starts sounding from here */
    dn_dbg.stage = 3;
    while ((uint32_t)(fm1_ticks() - splash_t0) < SPLASH_MS * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    dn_dbg.stage = 4;

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) { /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);
            if (b > 0)
                batt_raw = batt_raw ? batt_raw + (b - batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                master_q12 = (k10 * k10) >> 8;      /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: update mode (Felucca's and X0X's, unchanged) */
            static uint32_t t0, shown;
            uint32_t both = (1u << B_OCTDN) | (1u << B_OCTUP);
            if ((plat_buttons() & both) != both) {
                if (shown)
                    force = 1;                      /* the countdown covered the screen */
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[24] = "UPDATE MODE IN 5..";
                    d[15] = (char)('0' + left);
                    draw_text_box(0, 110, 240, &FONT_S, d, C_WHITE, 1);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                enter_uboot("UBOOT");
            }
        }
        ed_service();                               /* the editor SysEx (INFO, PING, the sample slots) */
        ota_service();                              /* M-UPGRADE handshake: the web flasher */
        if (usb.ota_req) {
            usb.ota_req = 0;
            dn_lock();
            dn_seq_stop(&dn_sq, &DN_SQ_CB);         /* nothing may hang on through an install */
            reese_note_off(&dn_rs);
            dn_unlock();
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            force = 1;
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
#if X0X_CDC
        cdc_task();
#endif
        dn_app_input();
        if (fm1_ms - last_frame >= 33u) {           /* ~30 frames/s: the scope moves, the panel stays quick */
            last_frame = fm1_ms;
            dn_dbg.ui_frames++;
            dn_app_draw((int)force);
            force = 0;
        }
    }
}

/* X0X's, unchanged (as Carnac keeps it): halt a second core left running, the watchdog first, the boot-loop guard,
 * RAM set up, the guards. tools/fm1_preflight.py reads these loops and simulates them before any package is made. */
void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_cpu1_halt();
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    irq_stack_mark();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    {
        uint32_t *end = (uintptr_t)_rt_text_end ? _rt_text_end : _rt_end;
        for (s = _rt_load, d = _rt_start; d < end; s++, d++)
            *d = *s;
    }
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
