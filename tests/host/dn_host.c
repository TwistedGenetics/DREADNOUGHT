/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT on the host: the app (src/app/dn_app.c) and the engines, against a simulated FM-1 driven by a script,
 * as X0X's host/x0x_host.c does for X0X. The screen goes to PNGs, the sound to WAVs. Copyright (C) 2026 Jamie
 * (TwistedGenetics); the screen and panel stubs follow x0x_host.c (Charles Vestal, GPL-3.0-only).
 *
 *   dn_host SCRIPT [OUTDIR]
 *
 *   wait MS                       run for MS milliseconds (audio, panel, screen)
 *   press BTN | release BTN | tap BTN   (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up | tapkey K MS   (K: 0..26 = F3..G5)
 *   turn ENC N                    (SELECT ALGO PRESET K1 K2 K3 K4; N detents, signed)
 *   midi B0 B1 B2                 an incoming MIDI message (hex)
 *   wav FILE | wavstop            record the output
 *   shot FILE.png                 the screen
 *   reboot                        start again from what was saved
 *   expect WHAT VALUE             preset page dirty oct bpm gate note (MIDI, before TRANS) leds-btn leds-keys
 *                                 playing rec trk view swing clocks midinotes len.T mute.T on.T st.T.I nt.T.I ln.T.I;
 *                                 peak (the output's peak since the last expect peak, Q15); VALUE "<N" / ">N" a bound
 */
#define X0X_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define __attribute__(x)

/* ---------------------------------------------------------------------------------------------- screen --- */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../../x0x-firmware/firmware/src/gfx.c"
#undef __attribute__

/* --------------------------------------------------------------------------------------------- platform --- */
#include "../../x0x-firmware/firmware/src/app/plat.h"
static uint32_t now_ms, held_btn, held_keys, lit_btn, lit_keys;
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
static uint32_t dim_keys, midi_out_n, clock_n, midi_notes_n;
void plat_keys_dim(uint32_t k) { dim_keys = k; }
void plat_midi_out(uint32_t pkt)
{
    midi_out_n++;
    if (((pkt >> 8) & 0xFFu) == 0xF8u)
        clock_n++;
    else if (((pkt >> 8) & 0xF0u) == 0x90u && (pkt >> 24))
        midi_notes_n++;
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
uint32_t plat_cpu_pct(void) { return 0; }         /* (the simulator does not measure the FM-1's load) */
uint32_t plat_cpu_peak_pct(void) { return 0; }
uint32_t plat_xruns(void) { return 0; }
int plat_slot_zones(int k) { (void)k; return 0; }   /* the simulator: no uploaded loops (the built-in ones play) */
const uint8_t *plat_slot(int k, int z, uint32_t *n, uint32_t *rate, char name[9])
{
    (void)k, (void)z, (void)n, (void)rate, (void)name;
    return 0;
}
static void dn_lock(void) {}
static void dn_unlock(void) {}

#include "../../src/app/dn_app.c"
#include "../../src/app/dn_splash.c"

/* ---------------------------------------------------------------------------------------- editor frames --- */
static uint8_t ed_reply[1024];
static uint32_t ed_n;
static void ed_put(uint32_t v) { if (ed_n < sizeof ed_reply) ed_reply[ed_n++] = (uint8_t)(v & 0x7Fu); }
static int ed(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    ed_n = 0;
    return dn_ed_cmd(cmd, a, na, ed_put);
}
/* the whole pattern through PAT_READ, as the page reads it */
static uint32_t ed_read_pattern(uint8_t *out)
{
    uint32_t off = 0, size = sizeof(dn_pattern_t);
    while (off < size) {
        uint8_t a[3] = {(uint8_t)(off & 127u), (uint8_t)(off >> 7), 200};
        uint32_t i = 2, k = 0;
        if (!ed(63, a, 3))
            return 0;
        while (i < ed_n) {
            uint32_t m = ed_reply[i++], j;
            for (j = 0; j < 7u && i < ed_n; j++)
                out[off + k++] = (uint8_t)(ed_reply[i++] | ((m >> j) & 1u) << 7);
        }
        off += k;
    }
    return off;
}
/* ... and back with PAT_WRITE (200 bytes a frame) and PAT_COMMIT; the commit's rc */
static int ed_write_pattern(const uint8_t *in)
{
    uint32_t off, size = sizeof(dn_pattern_t);
    for (off = 0; off < size; off += 200) {
        uint8_t a[2 + 240];
        uint32_t na = 2, n = size - off < 200 ? size - off : 200, i, j;
        a[0] = (uint8_t)(off & 127u);
        a[1] = (uint8_t)(off >> 7);
        for (i = 0; i < n; i += 7) {
            uint32_t m = 0;
            for (j = 0; j < 7u && i + j < n; j++)
                m |= (uint32_t)(in[off + i + j] >> 7) << j;
            a[na++] = (uint8_t)m;
            for (j = 0; j < 7u && i + j < n; j++)
                a[na++] = in[off + i + j] & 0x7Fu;
        }
        if (!ed(64, a, na) || ed_reply[0])
            return -1;
    }
    ed(65, 0, 0);
    return ed_reply[0];
}

/* ------------------------------------------------------------------------------------------------ output --- */
static uint32_t crc_tab[256];
static uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++)
                v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_tab[i] = v;
        }
    c = ~c;
    while (n--)
        c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    uint32_t c;
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n)
        fwrite(d, 1, n, f);
    c = crc32(crc32(0, (const uint8_t *)type, 4), d, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}
/* the screen as an RGB PNG (zlib "stored" blocks: no compression library needed) */
static void shot(const char *path)
{
    enum { ROW = 1 + 240 * 3, RAW = ROW * 240 };
    static uint8_t raw[RAW], z[RAW + RAW / 65535 * 5 + 64];
    uint32_t i, n = 0, a = 1, b = 0, off;
    uint8_t ihdr[13];
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    for (uint32_t y = 0; y < 240; y++) {
        raw[y * ROW] = 0;
        for (uint32_t x = 0; x < 240; x++) {
            uint16_t s = fb[y * 240 + x], c = (uint16_t)((s >> 8) | (s << 8));
            uint8_t *p = raw + y * ROW + 1 + x * 3;
            p[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
            p[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
            p[2] = (uint8_t)((c & 31) * 255 / 31);
        }
    }
    z[n++] = 0x78;
    z[n++] = 0x01;
    for (off = 0; off < RAW;) {
        uint32_t len = RAW - off > 65535u ? 65535u : RAW - off;
        z[n++] = off + len == RAW;
        z[n++] = (uint8_t)len;
        z[n++] = (uint8_t)(len >> 8);
        z[n++] = (uint8_t)~len;
        z[n++] = (uint8_t)(~len >> 8);
        memcpy(z + n, raw + off, len);
        n += len;
        off += len;
    }
    for (i = 0; i < RAW; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    be32(z + n, (b << 16) | a);
    n += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(ihdr, 240);
    be32(ihdr + 4, 240);
    ihdr[8] = 8, ihdr[9] = 2, ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, n);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
}

static FILE *wavf;
static uint32_t wav_frames, peak;
static void wav_header(void)
{
    uint32_t v, bytes = wav_frames * 4u;
    fseek(wavf, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wavf); v = 36u + bytes; fwrite(&v, 4, 1, wavf); fwrite("WAVEfmt ", 1, 8, wavf);
    v = 16; fwrite(&v, 4, 1, wavf); v = 1u | 2u << 16; fwrite(&v, 4, 1, wavf); v = 44100; fwrite(&v, 4, 1, wavf);
    v = 44100 * 4; fwrite(&v, 4, 1, wavf); v = 4u | 16u << 16; fwrite(&v, 4, 1, wavf);
    fwrite("data", 1, 4, wavf); fwrite(&bytes, 4, 1, wavf);
}

/* ------------------------------------------------------------------------------------------------- run --- */
static void run(uint32_t ms)
{
    static uint32_t acc;                            /* frames owed, x1000 */
    static int32_t lr[2 * 128];
    while (ms--) {
        acc += 44100;
        while (acc >= 128000u) {
            acc -= 128000u;
            dn_app_render(lr, 128);
            for (int i = 0; i < 256; i++) {
                int32_t s = lr[i] > 32767 ? 32767 : lr[i] < -32768 ? -32768 : lr[i];
                uint32_t a = (uint32_t)(s < 0 ? -s : s);
                if (a > peak)
                    peak = a;
                if (wavf) {
                    int16_t h = (int16_t)s;
                    fwrite(&h, 2, 1, wavf);
                }
            }
            if (wavf)
                wav_frames += 128;
        }
        now_ms++;
        dn_app_input();
        if (now_ms % 16u == 0u)
            dn_app_draw(0);
    }
}

static int btn_id(const char *s)
{
    static const char *const N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY",
                                      "REC", "OCT-", "OCT+"};
    for (int i = 0; i < NB; i++)
        if (!strcmp(s, N[i]))
            return i;
    fprintf(stderr, "no button %s\n", s);
    exit(2);
}
static int enc_id(const char *s)
{
    static const char *const N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
    for (int i = 0; i < NE; i++)
        if (!strcmp(s, N[i]))
            return i;
    fprintf(stderr, "no encoder %s\n", s);
    exit(2);
}

static int check(const char *what, const char *want, long got)
{
    long w = atol(want + (want[0] == '<' || want[0] == '>'));
    int ok = want[0] == '<' ? got < w : want[0] == '>' ? got > w : got == w;
    printf("%s expect %s %s: %ld\n", ok ? "  ok  " : "  FAIL", what, want, got);
    return ok;
}

int main(int argc, char **argv)
{
    char ln[512], out[400], a[64], b[64], c[64], path[512];
    const char *dir = argc > 2 ? argv[2] : "build/host";
    int fails = 0, lineno = 0;
    FILE *sc;
    if (argc < 2 || !(sc = fopen(argv[1], "r"))) {
        fprintf(stderr, "usage: dn_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    palette_set(0);
    dn_app_init();
    dn_app_draw(1);
    while (fgets(ln, sizeof ln, sc)) {
        int n;
        lineno++;
        if (ln[0] == '#' || sscanf(ln, "%63s", a) != 1)
            continue;
        n = sscanf(ln, "%63s %63s %63s %63s", a, b, c, out);
        if (!strcmp(a, "wait") && n >= 2)
            run((uint32_t)atoi(b));
        else if (!strcmp(a, "press") && n >= 2)
            held_btn |= 1u << btn_id(b), run(20);
        else if (!strcmp(a, "release") && n >= 2)
            held_btn &= ~(1u << btn_id(b)), run(20);
        else if (!strcmp(a, "tap") && n >= 2) {
            held_btn |= 1u << btn_id(b);
            run(60);
            held_btn &= ~(1u << btn_id(b));
            run(40);
        } else if (!strcmp(a, "key") && n >= 3) {
            int k = atoi(b);
            if (!strcmp(c, "down"))
                held_keys |= 1u << k;
            else
                held_keys &= ~(1u << k);
            run(1);
        } else if (!strcmp(a, "tapkey") && n >= 3) {
            int k = atoi(b);
            held_keys |= 1u << k;
            run((uint32_t)atoi(c));
            held_keys &= ~(1u << k);
            run(1);
        } else if (!strcmp(a, "turn") && n >= 3) {
            enc_acc[enc_id(b)] += atoi(c);
            run(30);
        } else if (!strcmp(a, "midi") && n >= 4) {
            dn_app_midi((uint8_t)strtol(b, 0, 16), (uint8_t)strtol(c, 0, 16), (uint8_t)strtol(out, 0, 16));
            run(1);
        } else if (!strcmp(a, "wav") && n >= 2) {
            snprintf(path, sizeof path, "%s/%s", dir, b);
            if (!(wavf = fopen(path, "wb"))) { perror(path); return 2; }
            wav_frames = 0;
            wav_header();
        } else if (!strcmp(a, "wavstop") && wavf) {
            wav_header();
            fclose(wavf);
            wavf = 0;
        } else if (!strcmp(a, "shot") && n >= 2) {
            dn_app_draw(1);
            snprintf(path, sizeof path, "%s/%s", dir, b);
            shot(path);
        } else if (!strcmp(a, "splash") && n >= 2) {
            dn_splash("0.3 BETA");
            snprintf(path, sizeof path, "%s/%s", dir, b);
            shot(path);
        } else if (!strcmp(a, "sliceset") && n >= 2) {   /* sliceset A|B s1 s2 ...: as the breaks page sends them */
            uint32_t st[64], cnt = 0, k = b[0] == 'B';
            char *q = ln + 8;
            while (*q == ' ') q++;
            q++;                                    /* (past A / B) */
            while (cnt < 64) {
                char *e;
                long v = strtol(q, &e, 10);
                if (e == q) break;
                st[cnt++] = (uint32_t)v;
                q = e;
            }
            printf("  sliceset %c: %u slices, rc %d\n", b[0], cnt, dn_app_slices_set(dn_bk[k].key, cnt, st));
            run(1);
        } else if (!strcmp(a, "sysex") && n >= 2) {  /* sysex CMD [b ...]: an editor frame (decimal); the reply kept */
            uint8_t args[128];
            uint32_t na = 0;
            char *q = ln + 5;
            long cmd = strtol(q, &q, 10);
            for (;;) {
                char *e;
                long v = strtol(q, &e, 10);
                if (e == q || na >= sizeof args) break;
                args[na++] = (uint8_t)v;
                q = e;
            }
            {
                int ok = ed((uint32_t)cmd, args, na);
                printf("  sysex %ld: %s, %u bytes back\n", cmd, ok ? "answered" : "not answered", ed_n);
            }
            run(1);
        } else if (!strcmp(a, "edround")) {          /* the page's round trip: read the pattern, change step 2 of
                                                     * track 1 to note 60 / 2 steps / on, write it back, commit */
            static uint8_t pat[sizeof(dn_pattern_t)];
            uint32_t got = ed_read_pattern(pat), at = 8u + 8u * 1u;   /* magic, swing, pad; then track 0's step 1 (8 bytes a step) */
            int rc;
            pat[at] = 60;
            pat[at + 1] = DN_ST_ON;
            pat[at + 2] = 8;
            rc = ed_write_pattern(pat);
            printf("  edround: read %u bytes, commit rc %d\n", got, rc);
            fails += !(got == sizeof(dn_pattern_t) && rc == 0);
            run(1);
        } else if (!strcmp(a, "golden") && n >= 2) {  /* the replies the web editor's tests decode */
            static uint8_t pat[sizeof(dn_pattern_t)];
            FILE *g;
            snprintf(path, sizeof path, "%s/%s", dir, b);
            g = fopen(path, "w");
            if (!g) { perror(path); return 2; }
            ed(60, 0, 0);
            fprintf(g, "{\"state\":[");
            for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
            fprintf(g, "],\"bpm\":%u,\"params\":[", dn_sq.bpm);
            ed(61, 0, 0);
            for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
            fprintf(g, "],\"dtn\":%d,\"pattern\":[", dn_rs.p[RP_DTN]);
            ed_read_pattern(pat);
            for (uint32_t i = 0; i < sizeof pat; i++) fprintf(g, "%s%u", i ? "," : "", pat[i]);
            fprintf(g, "],\"names\":[");
            {
                uint8_t q2[2] = {0, 0};
                ed(69, q2, 2);
                for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
            }
            {   /* 0.9: LFO 1's DRAW points, where the LFOs are, and the frames the glue must take whole */
                uint8_t q1[1] = {0};
                fprintf(g, "],\"lfopts\":[");
                ed(70, q1, 1);
                for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
                fprintf(g, "],\"lfopt5\":%d,\"lfonpts\":%u,\"lfonow\":[", dn_lp[0].pts[4], dn_lp[0].npts);
                ed(72, 0, 0);
                for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
                fprintf(g, "],\"conds\":[");
                for (uint32_t c = 0; c < DN_CO_N; c++) {
                    char tx[8];
                    dn_cond_text(c, tx);
                    fprintf(g, "%s\"%s\"", c ? "," : "", tx);
                }
                fprintf(g, "],\"long\":[");
                for (uint32_t c = 60; c <= 72; c++) fprintf(g, "%s%d", c > 60 ? "," : "", dn_ed_long(c));
            }
            {   /* 0.16: PROJ, and every effect's values at their defaults: name, top, text (172 BPM) */
                fprintf(g, "],\"proj\":[");
                ed(75, 0, 0);
                for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
                fprintf(g, "],\"hi\":[");          /* 0.19: PARAMS_HI, and REESE's filter envelope as set */
                ed(79, 0, 0);
                for (uint32_t i = 0; i < ed_n; i++) fprintf(g, "%s%u", i ? "," : "", ed_reply[i]);
                fprintf(g, "],\"rfe\":[%d,%d,%d,%d", dn_rs.fe[0], dn_rs.fe[1], dn_rs.fe[2], dn_rs.fe[3]);
                fprintf(g, "],\"fxp\":[");
                for (uint32_t t = 1; t < DN_FX_N; t++)
                    for (uint32_t i = 0; i < DN_FX_NP; i++) {
                        dn_fx_p_t fp;
                        char tx[8];
                        dn_fx_default(&fp, t);
                        dn_fx_ptext(0, &fp, i, 172, tx);
                        fprintf(g, "%s[%u,%u,\"%s\",%d,\"%s\"]", t > 1 || i ? "," : "", t, i, dn_fx_pname(&fp, i),
                                dn_fx_pmax(&fp, i), tx);
                    }
            }
            fprintf(g, "]}\n");
            fclose(g);
        } else if (!strcmp(a, "reboot")) {
            memset(&dn, 0, sizeof dn);
            held_btn = held_keys = 0;
            dn_app_init();
            dn_app_draw(1);
        } else if (!strcmp(a, "expect") && n >= 3) {
            long got;
            if (!strcmp(b, "preset")) got = dn.preset;
            else if (!strcmp(b, "page")) got = dn.page;
            else if (!strcmp(b, "dirty")) got = dn.dirty;
            else if (!strcmp(b, "oct")) got = dn.oct;
            else if (!strcmp(b, "bpm")) got = dn_rs.bpm;
            else if (!strcmp(b, "gate")) got = dn_rs.gate;
            else if (!strcmp(b, "note")) got = dn_rs.gate ? dn_rs.note : -1;
            else if (!strcmp(b, "leds-btn")) got = (long)lit_btn;
            else if (!strcmp(b, "leds-keys")) got = (long)lit_keys;
            else if (!strcmp(b, "playing")) got = dn_sq.playing;
            else if (!strcmp(b, "rec")) got = dn.rec;
            else if (!strcmp(b, "trk")) got = dn.trk + 1;
            else if (!strcmp(b, "view")) got = dn.view;
            else if (!strcmp(b, "swing")) got = dn_sq.pat.swing;
            else if (!strncmp(b, "reply.", 6)) got = ed_reply[atoi(b + 6)];
            else if (!strcmp(b, "replylen")) got = (long)ed_n;
            else if (!strcmp(b, "reclen")) got = dn_sq.pat.tr[dn.trk].st[dn.rec_last].len;
            else if (!strcmp(b, "rollkeys")) got = dn.roll_keys;
            else if (!strcmp(b, "row")) got = dn.drum_row;
            else if (!strcmp(b, "glide")) got = (dn_rs.target256 - dn_rs.pitch256) / 32;   /* eighths of a semitone to go */
            else if (!strcmp(b, "rolllo")) got = dn.roll_lo;
            else if (!strcmp(b, "drumtop")) got = dn.drum_top;
            else if (!strcmp(b, "held")) got = dn.step_held + 1;
            else if (!strcmp(b, "saveheld")) got = dn.save_held;
            else if (!strncmp(b, "pt.", 3)) {       /* pt.L.I: LFO L's DRAW point I (1-based) */
                int l = 0, i = 0;
                sscanf(b + 3, "%d.%d", &l, &i);
                got = dn_lp[l - 1].pts[i - 1];
            }
            else if (!strncmp(b, "lfo.", 4)) got = dn_ls[atoi(b + 4) - 1].out;   /* lfo.L: its value now */
            else if (!strcmp(b, "modcut")) got = dn_rs.mod[RP_CUT];
            else if (!strcmp(b, "modpitch")) got = dn_rs.mod_pitch;
            else if (!strcmp(b, "bmpitch")) got = dn_bk[0].mpitch;
            else if (!strcmp(b, "lsel")) got = dn.lfo_sel + 1;
            else if (!strcmp(b, "ldest")) got = dn_lp[DN_LSEL].dest;
            else if (!strcmp(b, "lshape")) got = dn_lp[DN_LSEL].shape;
            else if (!strcmp(b, "cin")) got = dn.cin_left != 0;
            else if (!strcmp(b, "clk")) got = (long)dn.clk_count;
            else if (!strcmp(b, "pos")) got = dn_sq.playing ? (long)dn_sq.pos : -1;
            else if (!strcmp(b, "cut")) got = DN_PAT_CUT(&dn_sq.pat);
            else if (!strcmp(b, "slsel")) got = dn.sl_sel;
            else if (!strcmp(b, "cursor")) got = (long)dn.sl_cur;
            else if (!strncmp(b, "bon.", 4)) got = dn_bk[atoi(b + 4) - 1].on;   /* BREAKS A (1) / B (2) sounding */
            else if (!strncmp(b, "pos.", 4)) got = (long)dn_brk_pos(&dn_bk[atoi(b + 4) - 1]);  /* where K plays */
            else if (!strncmp(b, "aud.", 4)) got = dn_bk[atoi(b + 4) - 1].aud;
            else if (!strncmp(b, "ns.", 3)) got = dn_bk[atoi(b + 3) - 1].nslices;
            else if (!strncmp(b, "smode.", 6)) got = dn_bk[atoi(b + 6) - 1].smode;
            else if (!strncmp(b, "sl.", 3)) {       /* sl.K.I: where slice I (1-based) of BREAKS K starts */
                int k = 0, i = 0;
                sscanf(b + 3, "%d.%d", &k, &i);
                got = (long)dn_bk[k - 1].start[i - 1];
            }
            else if (!strcmp(b, "clocks")) got = (long)clock_n;
            else if (!strcmp(b, "midinotes")) got = (long)midi_notes_n;
            else if (!strncmp(b, "len.", 4)) got = dn_sq.pat.tr[atoi(b + 4) - 1].len;
            else if (!strncmp(b, "mute.", 5)) got = dn_sq.pat.tr[atoi(b + 5) - 1].mute;
            else if (!strncmp(b, "on.", 3)) {       /* on.T: how many steps of track T are on */
                const dn_track_t *tr = &dn_sq.pat.tr[atoi(b + 3) - 1];
                got = 0;
                for (uint32_t k = 0; k < tr->len; k++)
                    got += tr->st[k].flags & DN_ST_ON;
            } else if (!strncmp(b, "st.", 3) || !strncmp(b, "nt.", 3) || !strncmp(b, "ln.", 3) || !strncmp(b, "vl.", 3) ||
                       !strncmp(b, "cd.", 3) || !strncmp(b, "rt.", 3) || !strncmp(b, "rv.", 3) || !strncmp(b, "rl.", 3)) {
                int t = 0, i = 0;                   /* st.T.I flags, nt.T.I note, ln.T.I gate of step I (1-based);
                                                     * vl velocity, cd condition, rt retrig rate, rv its curve, rl its
                                                     * length */
                sscanf(b + 3, "%d.%d", &t, &i);
                const dn_step_t *st = &dn_sq.pat.tr[t - 1].st[i - 1];
                got = b[0] == 's' ? st->flags : b[0] == 'n' ? st->note : b[0] == 'l' ? st->len : b[0] == 'v' ? st->vel
                    : b[0] == 'c' ? st->cond : !strncmp(b, "rt.", 3) ? st->rtim : !strncmp(b, "rv.", 3) ? st->rvel : st->rlen;
            }
            else if (!strncmp(b, "pat.", 4)) got = dn_pcur[atoi(b + 4) - 1] + 1;   /* pat.T: track T's pattern */
            else if (!strncmp(b, "patq.", 5)) got = dn_pq[atoi(b + 5) - 1] == 0xFF ? 0 : dn_pq[atoi(b + 5) - 1] + 1;
            else if (!strcmp(b, "fill")) got = dn_sq.fill;
            else if (!strcmp(b, "lst")) got = dn_sq.lst;
            else if (!strcmp(b, "notes")) got = (long)dn_pat_notes();
            else if (!strcmp(b, "chan4")) got = dn_sq.pat.tr[3].chan + 1;
            else if (!strncmp(b, "eng.", 4)) got = dn_eng[(atoi(b + 4) - 1) & 7];   /* track n's instrument (DN_E_*) */
            else if (!strcmp(b, "peng")) got = dn.peng == 0xFF ? -1 : dn.peng;
            else if (!strcmp(b, "fxview")) got = dn.fx_view;
            else if (!strcmp(b, "fxa")) got = dn_fxp[0].type;
            else if (!strcmp(b, "fxb")) got = dn_fxp[1].type;
            else if (!strncmp(b, "fxap.", 5)) got = dn_fxp[0].p[atoi(b + 5) % DN_FX_NP];   /* (0.16) */
            else if (!strncmp(b, "fxbp.", 5)) got = dn_fxp[1].p[atoi(b + 5) % DN_FX_NP];
            else if (!strcmp(b, "knee")) { got = (long)dn_knee; dn_knee = 0; }   /* samples the master knee bent (0.16.1) */
            else if (!strncmp(b, "rfe.", 4)) got = dn_rs.fe[atoi(b + 4) & 3];   /* the filter envelopes (0.19) */
            else if (!strncmp(b, "sfe.", 4)) got = dn_sb.fe[atoi(b + 4) & 3];
            else if (!strcmp(b, "fxbkb")) got = (long)(dn_fxs[1].bytes / 1024u);   /* FX B's memory, KB */
            else if (!strcmp(b, "fxbout")) got = dn_fxs[1].u.m.y_l * 32768.f > 4.f || dn_fxs[1].u.m.y_l * 32768.f < -4.f;   /* (MASSIVE sounding) */
            else if (!strncmp(b, "senda.", 6)) got = dn_send[0][(atoi(b + 6) - 1) & 7];
            else if (!strcmp(b, "dly")) {           /* the delay's output now, both sides */
                got = labs((long)dn_fxs[0].u.d.o_l) + labs((long)dn_fxs[0].u.d.o_r);
            } else if (!strcmp(b, "legato")) got = dn_sq.legato;
            else if (!strcmp(b, "sbgate")) got = dn_sb.gate;       /* SUB (0.13) */
            else if (!strcmp(b, "sbpreset")) got = dn.sb_preset;
            else if (!strcmp(b, "smodcut")) got = dn_sb.mod[SP_CUT] != 0;
            else if (!strcmp(b, "sbsync")) got = dn_sb.esync;
            else if (!strcmp(b, "rssync")) got = dn_rs.esync;
            else if (!strcmp(b, "lph")) got = (long)(dn_ls[DN_LBASE].ph >> 24);   /* the track synth's LFO 1 phase, 0..255 */
            else if (!strcmp(b, "sbnote")) got = dn_sb.gate ? dn_sb.note : -1;
            else if (!strncmp(b, "s.", 2)) {
                got = -9999;
                for (uint32_t k = 0; k < SP_COUNT; k++)
                    if (!strcmp(b + 2, dn_sub_param_name(k)))
                        got = dn_sb.p[k];
            }
            else if (!strcmp(b, "rlfo1")) got = dn_lp[0].dest;                 /* REESE's LFO 1 DEST (0.15) */
            else if (!strcmp(b, "slfo1")) got = dn_lp[DN_NLFO].dest;           /* SUB's LFO 1 DEST */
            else if (!strcmp(b, "slfo1d")) got = dn_lp[DN_NLFO].depth;
            else if (!strncmp(b, "used.", 5)) got = dn_user[b[5] - '0'][b[7] - '0'].used;   /* used.SET.SLOT */
            else if (!strcmp(b, "msg")) got = dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0;
            else if (!strncmp(b, "p.", 2)) {
                got = -9999;
                for (uint32_t k = 0; k < RP_COUNT; k++)
                    if (!strcmp(b + 2, reese_param_name(k)))
                        got = dn_rs.p[k];
            } else if (!strcmp(b, "peak")) {
                got = (long)peak;
                peak = 0;
            } else {
                fprintf(stderr, "%s:%d: no expect %s\n", argv[1], lineno, b);
                return 2;
            }
            fails += !check(b, c, got);
        } else {
            fprintf(stderr, "%s:%d: what is '%s'?\n", argv[1], lineno, a);
            return 2;
        }
    }
    if (wavf) {
        wav_header();
        fclose(wavf);
    }
    fclose(sc);
    printf("%s: %s\n", argv[1], fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
