/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.43): the update loader (X0X's ldr_core.c as tools/dn_patches.py changes it) cut off at
 * every point of an install. A fake host serves PACKAGE over the SysEx protocol (as X0X's tests/ldr_test.c does);
 * the "device" flash starts as the same package with half its app sectors changed (another firmware installed), the
 * app's first sector among them. The install is cut (the power, the cable) after each n-th flash operation, and then:
 *   - no app is left to start over a half-written one: once any sector but the header has changed, the header must
 *     be neither the old one nor the new one until every sector is the new one (the FM-1's boot stage starts any app
 *     whose header is valid, even over a pending update record: a black screen, no USB, 2026-10-25);
 *   - the head [0, 0x4000) is never touched;
 *   - the install run again from there (the loader started from its record) finishes, and the app is the package's.
 *   ldr_cut_test PACKAGE.fwsc        (LDR_CORE: the patched ldr_core.c, -DLDR_CORE='"path"')
 * Copyright (C) 2026 Jamie (TwistedGenetics); the harness after X0X's tests/ldr_test.c (Leo Kuroshita, GPL-3.0). */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, ops, cut_at;
static jmp_buf cut;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../../x0x-firmware/firmware/src/ota.c"

static void op(void)                                /* a flash operation: the cut may come before it */
{
    if (cut_at && ++ops >= cut_at)
        longjmp(cut, 1);
}
static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    op();
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i, half = n / 2u;
    if (off < 0x4000u) { printf("WRITE IN THE HEAD %#x\n", off); exit(1); }
    if (cut_at && ops + 1u >= cut_at) {             /* (a cut in the middle of a page: half of it written) */
        for (i = 0; i < half; i++) nor[off + i] &= s[i];
    }
    op();
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) {}
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include LDR_CORE

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}
static uint32_t flash_off(const uint8_t *lg)
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static uint8_t old_img[0x93000];
static void device_old(const uint8_t *img)          /* the package, half its app sectors (the header too) changed */
{
    uint32_t s, i;
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, img, 0x93000);
    for (s = 0x4000; s < 0x93000; s += 0x2000)
        for (i = 0; i < 0x1000; i++)
            nor[s + i] ^= (uint8_t)(0x5A + i);
    memcpy(old_img, nor, sizeof old_img);
}

int main(int argc, char **argv)
{
    static const uint8_t *img;                      /* (static: kept across the longjmp) */
    static uint32_t fo, total, k, step, cuts, bad, mixed_starts, resumes_bad, head_touched;
    static int rc;
    if (argc < 2) { printf("usage: ldr_cut_test PACKAGE.fwsc\n"); return 2; }
    logical = load_logical(argv[1], &logical_len);
    fo = flash_off(logical);
    img = logical + fo;
    /* the whole install once: how many flash operations it takes */
    device_old(img);
    ops = 0, cut_at = 0xFFFFFFFFu;
    if (setjmp(cut) == 0) {
        rc = ldr_session();
        if (rc != 0) { printf("FAIL the install does not finish (rc %d)\n", rc); return 1; }
    }
    total = ops;
    if (memcmp(nor + 0x4000, img + 0x4000, 0x93000 - 0x4000)) { printf("FAIL the app is not the package's\n"); return 1; }
    printf("     an install of %s: %u flash operations\n", argv[1], total);
    step = total / 400u ? total / 400u : 1u;
    for (k = 1; k < total; k += step) {
        int complete, changed = 0, hdr_old, hdr_new;
        uint32_t s;
        device_old(img);
        ops = 0, cut_at = k;
        if (setjmp(cut) == 0)
            ldr_session();
        cuts++;
        for (s = 0x5000; s < 0x93000 && !changed; s += 0x1000)
            changed = memcmp(nor + s, old_img + s, 0x1000) != 0;
        complete = !memcmp(nor + 0x4000, img + 0x4000, 0x93000 - 0x4000);
        hdr_old = !memcmp(nor + 0x4000, old_img + 0x4000, 0x1000);
        hdr_new = !memcmp(nor + 0x4000, img + 0x4000, 0x1000);
        if (changed && !complete && (hdr_old || hdr_new)) {
            if (!mixed_starts)
                printf("     cut after operation %u: a valid header (%s) over a half-written app\n", k,
                       hdr_old ? "the old one" : "the new one");
            mixed_starts++;
        }
        if (memcmp(nor, img, 0x4000))
            head_touched++;
        cut_at = 0;                                 /* the loader again, from its record: it finishes */
        ops = 0;
        rc = ldr_session();
        if (rc != 0 || memcmp(nor + 0x4000, img + 0x4000, 0x93000 - 0x4000))
            resumes_bad++;
    }
    printf("%s an install cut at %u points: never a valid header over a half-written app: %u\n",
           mixed_starts ? "FAIL" : "ok  ", cuts, mixed_starts);
    printf("%s ... the head [0, 0x4000) never touched: %u\n", head_touched ? "FAIL" : "ok  ", head_touched);
    printf("%s ... run again after each cut, the install finishes with the package's app: %u failed\n",
           resumes_bad ? "FAIL" : "ok  ", resumes_bad);
    bad = mixed_starts + head_touched + resumes_bad;
    printf(bad ? "LOADER CUT TEST FAILED\n" : "LOADER CUT TEST: ok\n");
    return bad != 0;
}
