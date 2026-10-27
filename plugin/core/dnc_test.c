/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT plugin core, tested without a host: the sound, the host's notes and transport, the panel, the screen,
 * the state saved and loaded. dnc_test [SCREEN.ppm]. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_core.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    fails += !ok;
}
static float buf[2 * 44100];
static float peak_of(uint32_t frames)              /* render, and the peak (NaN: -1) */
{
    float p = 0.0f;
    uint32_t i;
    while (frames) {
        uint32_t k = frames < 44100u ? frames : 44100u;
        dnc_render(buf, k);
        for (i = 0; i < 2u * k; i++) {
            if (buf[i] != buf[i])
                return -1.0f;
            if (fabsf(buf[i]) > p)
                p = fabsf(buf[i]);
        }
        frames -= k;
    }
    return p;
}
static void ppm(const char *path)
{
    static uint32_t px[240 * 240];
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return;
    dnc_screen_copy(px);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint8_t c[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

/* (0.45) the web editor's frames, as it sends them */
int plat_slot_zones(int k);
static uint8_t rep[2048];
static uint32_t rep_n;
static int frame(const uint8_t *args, uint32_t na, uint32_t cmd)   /* sent; the answer in rep (args after cmd) */
{
    static uint8_t f[2048];
    uint32_t n = 0, i;
    f[n++] = 0xF0, f[n++] = 0x7D, f[n++] = 0x46, f[n++] = 0x4C, f[n++] = (uint8_t)cmd;
    for (i = 0; i < na; i++)
        f[n++] = args[i];
    f[n++] = 0xF7;
    if (!dnc_sysex(f, n))
        return -1;
    rep_n = dnc_sysex_out(rep, sizeof rep);
    return rep_n >= 6u && rep[4] == cmd ? 0 : -1;
}
static uint32_t pack7(const uint8_t *b, uint32_t n, uint8_t *out)
{
    uint32_t o = 0, i, j;
    for (i = 0; i < n; i += 7) {
        uint32_t m = 0;
        for (j = 0; j < 7u && i + j < n; j++)
            m |= (uint32_t)(b[i + j] >> 7) << j;
        out[o++] = (uint8_t)m;
        for (j = 0; j < 7u && i + j < n; j++)
            out[o++] = b[i + j] & 0x7Fu;
    }
    return o;
}
static uint32_t crc32_t(const uint8_t *b, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
/* a sample into slot k as the editor sends one (media.js slotImage, writeSlots): BEGIN, WRITE x n, END; END's rc */
static int send_slot(uint32_t k, uint32_t nsamp)
{
    static uint8_t data[8192], hdr[512], a[600];
    uint32_t nb = (nsamp + 1u) / 2u, off, i, na;
    uint32_t seed = 12345u;
    for (i = 0; i < nb; i++)
        seed = seed * 1103515245u + 12345u, data[i] = (uint8_t)(seed >> 16);
    memset(hdr, 0, sizeof hdr);
    w32(hdr, 0x504D5346u);
    hdr[4] = 1, hdr[6] = 1;
    memcpy(hdr + 8, "MYLOOP", 6);
    w32(hdr + 16, nb);
    w32(hdr + 20, crc32_t(data, nb));
    w32(hdr + 32, 0), w32(hdr + 36, nsamp), w32(hdr + 40, 0), w32(hdr + 44, nsamp - 1u), w32(hdr + 48, 32768u);
    hdr[52] = (uint8_t)(60 * 16), hdr[53] = (uint8_t)((60 * 16) >> 8);
    a[0] = (uint8_t)k;
    if (frame(a, 1, 11) || rep[6] != 0)
        return -1;
    for (off = 0; off < nb; off += 256) {
        uint32_t adr = 512u + off, len = nb - off < 256u ? nb - off : 256u;
        a[0] = (uint8_t)k, a[1] = adr & 127u, a[2] = (adr >> 7) & 127u, a[3] = (adr >> 14) & 127u;
        na = 4u + pack7(data + off, len, a + 4);
        if (frame(a, na, 12) || rep[9] != 0)
            return -2;
    }
    a[0] = (uint8_t)k;
    na = 1u + pack7(hdr, 480, a + 1);
    if (frame(a, na, 13))
        return -3;
    return rep[6];
}

int main(int argc, char **argv)
{
    static uint8_t st[300 * 1024], st2[300 * 1024];
    uint32_t n, n2, s0, i, lit = 0, outs = 0;
    uint8_t m[3];
    float p;
    dnc_init();
    printf("DREADNOUGHT plugin core %s\n", dnc_version());
    p = peak_of(44100);
    check("switched on: quiet (no NaN)", p >= 0.0f && p < 0.01f);
    s0 = dnc_screen_serial();
    p = peak_of(44100);
    check("the screen drawn after the splash", dnc_screen_serial() != s0);
    dnc_midi(0x90, 45, 110);                        /* channel 1: track 1 (REESE) */
    p = peak_of(22050);
    check("a host note on channel 1 sounds", p > 0.05f);
    dnc_midi(0x80, 45, 0);
    peak_of(44100);
    p = peak_of(4410);
    check("... and stops", p < 0.01f);
    dnc_midi(0x91, 36, 120);                        /* channel 2: track 2 (BREAKS A), C1 its first slice */
    p = peak_of(11025);
    check("a host note on channel 2 plays a break's slice", p > 0.02f);
    dnc_midi(0x81, 36, 0);
    peak_of(44100 * 2);
    dnc_transport(1, 172.0);
    check("the host plays: the sequencer starts", dnc_playing());
    check("... at the host's tempo", dnc_bpm() == 172u);
    p = peak_of(44100 * 2);
    check("... and sounds", p > 0.05f && p <= 1.0f);
    for (i = 0; i < 200u; i++) {
        dnc_render(buf, 441);
        lit |= dnc_leds_keys();
        while (dnc_midi_out(m))
            outs++;
    }
    dnc_transport(0, 172.0);
    check("the host stops: the sequencer stops", !dnc_playing());
    dnc_button(10, 1);                              /* PLAY on the panel */
    peak_of(4410);
    dnc_button(10, 0);
    peak_of(4410);
    check("PLAY on the panel starts it too", dnc_playing());
    dnc_button(10, 1);
    peak_of(4410);
    dnc_button(10, 0);
    peak_of(4410);
    check("... and stops it", !dnc_playing());
    dnc_turn(3, 5);                                 /* K1 */
    peak_of(4410);
    n = dnc_state_save(st, sizeof st);
    check("the state saved", n > 100u && n <= dnc_state_max());
    dnc_turn(3, 20);
    peak_of(4410);
    check("the state loads", dnc_state_load(st, n) == 0);
    dnc_init();
    peak_of(44100 * 2);
    n2 = dnc_state_save(st2, sizeof st2);
    check("... and comes back the same", n2 == n && !memcmp(st, st2, n));
    if (n2 != n || memcmp(st, st2, n))
        for (i = 0; i < (n < n2 ? n : n2); i++)
            if (st[i] != st2[i]) {
                printf("    (%u bytes, then %u: the first difference at %u)\n", n, n2, i);
                break;
            }
    check("a bad state is refused", dnc_state_load(st, 7) != 0 && dnc_state_load((const uint8_t *)"XXXXXXXXXXXX", 12) != 0);
    /* (0.45) the web editor over the plugin's MIDI port */
    check("a new plugin: ROLLER and STEPPER in slot 1", plat_slot_zones(0) == 2 && plat_slot_zones(1) == 0);
    check("the editor's INFO answered: DREADNOUGHT ... PLUGIN",
          frame(0, 0, 1) == 0 && !memcmp(rep + 5, "DREADNOUGHT", 11) && strstr((const char *)rep + 5, "PLUGIN") != 0);
    check("not the editor's frame: left alone", dnc_sysex((const uint8_t *)"\xF0\x7E\x7F\x06\x01\xF7", 6) == 0);
    {
        int rc = send_slot(1, 9000);
        check("a sample sent to slot 2, as the editor sends one", rc == 0 && plat_slot_zones(1) == 1);
        printf("    (END: %d)\n", rc);
    }
    {
        uint32_t at = 7, k, nz[3] = {0, 0, 0};       /* slots, KB, then each: zones, name (0 after), KB */
        int ok = frame(0, 0, 15) == 0 && rep[5] == 3;
        for (k = 0; ok && k < 3u; k++) {
            nz[k] = rep[at++];
            while (at < rep_n && rep[at])
                at++;
            at += 2u;
        }
        check("... SMP_INFO lists it", ok && nz[0] == 2u && nz[1] == 1u && nz[2] == 0u);
    }
    peak_of(4410);
    n = dnc_state_save(st, sizeof st);
    check("the state keeps the slots", n > 4500u + 2000u);
    dnc_state_load(st, n);
    dnc_init();
    peak_of(44100 * 2);
    check("... and brings them back", plat_slot_zones(0) == 2 && plat_slot_zones(1) == 1);
    n2 = dnc_state_save(st2, sizeof st2);
    check("... the same", n2 == n && !memcmp(st, st2, n));
    {
        static uint8_t v1[8 + 9 * 4];               /* (0.44's state: no slots) */
        memcpy(v1, "DNPS", 4);
        v1[4] = 1;
        memset(v1 + 8, 0xFF, 9 * 4);
        check("a 0.44 state: the factory's slots", dnc_state_load(v1, sizeof v1) == 0 && (dnc_init(), plat_slot_zones(0) == 2 && plat_slot_zones(1) == 0));
    }
    {
        uint8_t a[1] = {0};
        check("... ERASE empties a slot", frame(a, 1, 14) == 0 && rep[6] == 0 && plat_slot_zones(0) == 0);
    }
    peak_of(44100);
    if (argc > 1)
        ppm(argv[1]);
    (void)lit, (void)outs;
    printf(fails ? "PLUGIN CORE TEST FAILED\n" : "PLUGIN CORE TEST: ok\n");
    return fails != 0;
}
