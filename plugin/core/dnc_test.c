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

int main(int argc, char **argv)
{
    static uint8_t st[64 * 1024], st2[64 * 1024];
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
    if (argc > 1)
        ppm(argv[1]);
    (void)lit, (void)outs;
    printf(fails ? "PLUGIN CORE TEST FAILED\n" : "PLUGIN CORE TEST: ok\n");
    return fails != 0;
}
