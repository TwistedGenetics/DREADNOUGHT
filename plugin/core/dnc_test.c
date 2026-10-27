/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT plugin core, tested without a host: the sound, the host's notes and transport, the panel, the screen,
 * the state saved and loaded. dnc_test [SCREEN.ppm]. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdint.h>
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

    /* (0.45) the GUI's API: every value of every instrument, the presets, the steps, the samples, the slices */
    dnc_init();
    peak_of(44100 * 2);
    {
        static dnc_section_t sec[48];
        int e, bad = 0, total = 0, engines = 0;
        for (e = 1; e < dnc_engine_count(); e++) {
            uint32_t ns, k, j;
            dnc_track_set_engine(3, e);
            dnc_track_select(3);
            peak_of(441);
            if (dnc_track_engine(3) != e) {
                printf("    instrument %s not set on track 4\n", dnc_engine_name(e));
                bad++;
                continue;
            }
            ns = dnc_sections(3, sec, 48);
            engines += ns > 0;
            for (k = 0; k < ns; k++)
                for (j = 0; j < sec[k].n; j++) {
                    int idx, nidx = sec[k].sel ? 2 : 1;
                    for (idx = 0; idx < nidx; idx++) {
                        uint32_t id = sec[k].id[j];
                        int32_t lo = dnc_min(id, sec[k].sel, idx), hi = dnc_max(id, sec[k].sel, idx), was, got;
                        char t[16];
                        const char *nm = dnc_name(id, sec[k].sel, idx);
                        total++;
                        was = dnc_get(id, sec[k].sel, idx);
                        if (!nm || !nm[0] || lo > hi) {
                            printf("    %s / %s: value %u: no name or no range\n", dnc_engine_name(e), sec[k].name, id);
                            bad++;
                            continue;
                        }
                        if (hi > lo && dnc_name(id, sec[k].sel, idx)[0] && id != dnc_id_bpm()) {
                            int32_t ch[256];
                            uint32_t nc = dnc_choices(id, sec[k].sel, idx, ch, 256);
                            int32_t to = nc ? ch[nc - 1u] : hi;
                            dnc_set(id, sec[k].sel, idx, to);
                            got = dnc_get(id, sec[k].sel, idx);
                            if (got != to) {
                                printf("    %s / %s / %s: set %d, read %d\n", dnc_engine_name(e), sec[k].name, nm, to, got);
                                bad++;
                            }
                            dnc_set(id, sec[k].sel, idx, was);
                        }
                        dnc_text(id, sec[k].sel, idx, dnc_get(id, sec[k].sel, idx), t);
                        if (!t[0]) {
                            printf("    %s / %s: no text\n", dnc_engine_name(e), nm);
                            bad++;
                        }
                    }
                }
            peak_of(441);
        }
        printf("    (%d values on %d instruments)\n", total, engines);
        check("every value of every instrument: named, in range, set and read back", bad == 0 && total > 300);
        {
            uint32_t n = dnc_fx_sections(sec, 48);
            check("the effects' sections", n == 3u && sec[1].n >= 1u && sec[2].n >= 1u);
        }
    }
    dnc_track_set_engine(0, 1);                     /* REESE on track 1 */
    dnc_track_select(0);
    check("REESE's presets", dnc_preset_count() > 8u && dnc_preset_name(1)[0]);
    dnc_preset_load(2);
    check("... one loaded", dnc_preset_current() == 2);
    check("... a user sound stored, named", dnc_user_store(0, "mybass") == 0 &&
                                               !strcmp(dnc_preset_name((uint32_t)dnc_preset_current()), "MYBASS"));
    {
        dnc_step_t a, b;
        dnc_step_default(0, &a);
        a.note = 40, a.vel = 99, a.len = 6, a.flags = DNC_ST_ON | DNC_ST_SLIDE, a.cond = 3;
        dnc_step_set(0, 5, &a);
        dnc_step_get(0, 5, &b);
        check("a step set and read back", !memcmp(&a, &b, sizeof a));
        dnc_track_set_len(0, 32);
        check("... the track's length", dnc_track_len(0) == 32);
        dnc_pat_select(0, 3);
        peak_of(441);
        check("... another pattern picked (stopped: at once)", dnc_pat_current(0) == 3);
        dnc_pat_select(0, 0);
        peak_of(441);
    }
    {
        static float x[44100];
        static uint8_t ad[44100], ad2[30000];
        static dnc_part_t parts[48];
        uint32_t i, n0 = dnc_sample_count(), m, nr, kk;
        int kind;
        for (i = 0; i < 44100u; i++)
            x[i] = (float)(0.6 * sin(i * 0.05) * exp(-i / 9000.0));
        m = dnc_convert(x, 44100, 44100, 22050, 1, ad, sizeof ad);
        check("a sample converted (44.1 kHz to 22 kHz, trimmed)", m > 10000u && m <= 22050u);
        for (i = 0; i < n0; i++)
            parts[i].adpcm = dnc_sample(i, &parts[i].n, &parts[i].rate, &kind), parts[i].bytes = (parts[i].n + 1u) / 2u;
        parts[n0].adpcm = ad, parts[n0].n = m, parts[n0].rate = 22050, parts[n0].bytes = (m + 1u) / 2u;
        check("... stored with the others", dnc_samples_store(parts, n0 + 1u) == 0 && dnc_sample_count() == n0 + 1u);
        dnc_sample_set_name(n0, "MY HIT");
        {
            const uint8_t *a = dnc_sample(n0, &nr, &kk, &kind);
            check("... read back the same", a && nr == m && kk == 22050u && !memcmp(a, ad, (m + 1u) / 2u) && kind == 0);
        }
        check("... it's a loop BREAKS can pick", dnc_loop_of_sample(n0) == (int)n0);
        n = dnc_state_save(st, sizeof st);
        dnc_state_load(st, n);
        dnc_init();
        peak_of(44100);
        check("... kept by the state, with its name", dnc_sample_count() == n0 + 1u && !strcmp(dnc_sample_name(n0), "MY HIT"));
        {
            static int16_t s16[44100];
            dnc_ima_decode(ad, m, s16);
            dnc_ima_encode(s16, m, ad2);
            check("ADPCM: decode, encode: the same bytes", !memcmp(ad, ad2, (m + 1u) / 2u));
        }
        for (i = 0; i < 48u; i++)
            parts[i].adpcm = ad, parts[i].n = 44000, parts[i].rate = 44100, parts[i].bytes = 22000;
        check("too much for the memory: refused, nothing changed", dnc_samples_store(parts, 20) != 0 && dnc_sample_count() == n0 + 1u);
    }
    {
        const uint8_t *a;
        uint32_t n, r, ns, st0[64], set[3] = {0, 3000, 9000};
        int man;
        int t, tb = -1;
        for (t = 0; t < 8; t++)
            if (dnc_brk_of_track(t) == 0)
                tb = t;
        peak_of(441);
        check("BREAKS A's loop and its slices", tb >= 0 && dnc_brk_info(0, &a, &n, &r, st0, &ns, &man) && ns > 1u);
        check("... slices set by hand", dnc_brk_slices_set(0, set, 3) == 0 && dnc_brk_info(0, &a, &n, &r, st0, &ns, &man) &&
                                          ns == 3u && man && st0[1] == 3000u);
    }
    {
        int e, bad = 0, n = 0;
        for (e = 1; e < dnc_engine_count(); e++) {
            uint16_t ids[8];
            uint32_t k, c = dnc_auto_ids(e, ids);
            for (k = 0; k < c; k++, n++) {
                int32_t lo = dnc_auto_min(e, ids[k]), hi = dnc_auto_max(e, ids[k]), was = dnc_auto_get(e, ids[k]);
                dnc_auto_set(e, ids[k], hi);
                bad += dnc_auto_get(e, ids[k]) != hi || lo >= hi;
                dnc_auto_set(e, ids[k], was);
            }
        }
        printf("    (%d automatable values)\n", n);
        check("the DAW's automation: every value set and read back", bad == 0 && n > 40);
    }
    peak_of(44100);
    if (argc > 1)
        ppm(argv[1]);
    (void)lit, (void)outs;
    printf(fails ? "PLUGIN CORE TEST FAILED\n" : "PLUGIN CORE TEST: ok\n");
    return fails != 0;
}
