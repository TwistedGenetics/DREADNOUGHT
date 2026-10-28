/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * DREADNOUGHT on the SMK-37 Pro / Elite: the store, the sample slots and the editor's SysEx. Copyright (C) 2026 Jamie
 * (TwistedGenetics); written 2026-10 for the SMK, after the FM-1's editor protocol (x0x-firmware/web/EDITOR_PROTOCOL.md:
 * the frame, the slot commands and the slot format), so the web editor works as with an FM-1 (replies over 128 bytes
 * go in pieces, and SMP_INFO ends with the slots' sizes: the editor takes both; the FM-1's breaks page does not: on the
 * SMK the editor sends the loops).
 *
 * The user flash (fm1_flash_*: the SDK finds it; 4 KB sectors; erases and writes from the control task only, the
 * audio silenced while they run), DREADNOUGHT's layout:
 *   0x00000  the store: 9 objects (the app's 7, then the two settings objects), two sectors each, written in turn;
 *            a record is "DNO1", its number, its length, the CRC-32 of its bytes, then the bytes (up to 3840); the
 *            newer good one is read
 *   0x12000  sample slot 1 (40 KB), 0x1C000 slot 2 (40 KB), 0x26000 slot 3 (24 KB): each a 512-byte header ("FSMP",
 *            zones) and its 4-bit ADPCM, as on the FM-1 (whose slots are 80, 80 and 64 KB), read in place (fm1_flash_map)
 *   0x2C000  the end: the user area is 176 KB (tools/smk_assemble.py USER_BYTES; M-VAVE's own layout keeps 524 KB,
 *            which leaves too little room for DREADNOUGHT's code)
 * Nothing of M-VAVE's own instrument's data is kept: its projects, voices and samples are written over (back them up
 * with M-VAVE's workbench before changing firmware). */
#include "fm1.h"
#include "fm1_device.h"
#include "dn_smk.h"
#include "dn/dn_smk_version.h"
#include <string.h>

#include "dn/dn_app.h"

/* ------------------------------------------------------------------------------------------------- CRC-32 --- */
/* zlib's CRC-32 (reflected 0xEDB88320), four bits at a time */
static uint32_t crc32_run(uint32_t c, const uint8_t *p, uint32_t n)
{
    static const uint32_t T[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
                                   0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                   0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    c = ~c;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* -------------------------------------------------------------------------------------------------- store --- */
#define SEC 4096u
#define OBJ_MAX 3840u
#define NOBJ 9u                                      /* OBJ_SET .. OBJ_MOTION (0..6), then EXT0, EXT1 (16, 17) */
#define REC_MAGIC 0x314F4E44u                        /* "DNO1" */
#define SLOT_BASE (NOBJ * 2u * SEC)
#define SLOT_N 3u
#define SLOT_DATA 512u
#define SLOT_MAGIC 0x504D5346u                       /* "FSMP" */
static const uint32_t SLOT_OFF[SLOT_N] = {SLOT_BASE, SLOT_BASE + 0xA000u, SLOT_BASE + 0x14000u};
static const uint32_t SLOT_SIZE[SLOT_N] = {0xA000u, 0xA000u, 0x6000u};
#define LAYOUT_END (SLOT_BASE + 0x1A000u)
_Static_assert(LAYOUT_END == 176u * 1024u, "the layout fills the user area (tools/smk_assemble.py USER_BYTES)");

struct rec { uint32_t magic, seq, len, crc; };
static uint8_t flash_ok;
static uint32_t obj_seq[NOBJ];                       /* the newest record's number (0: none) */
static uint8_t obj_side[NOBJ];                       /* which of its two sectors holds it */
static uint8_t chunk[256] __attribute__((aligned(4)));

static int obj_index(uint32_t obj) { return obj < 7u ? (int)obj : obj == 16u ? 7 : obj == 17u ? 8 : -1; }
static uint32_t obj_off(uint32_t i, uint32_t side) { return (2u * i + side) * SEC; }

static int rec_read(uint32_t off, struct rec *r)     /* 1: a good record (its bytes checked) */
{
    uint32_t at, c = 0;
    if (fm1_flash_read(off, r, sizeof *r) || r->magic != REC_MAGIC || r->len > OBJ_MAX || !r->seq)
        return 0;
    for (at = 0; at < r->len; at += sizeof chunk) {  /* (the CRC a piece at a time: no 4 KB buffer) */
        uint32_t n = r->len - at < sizeof chunk ? r->len - at : sizeof chunk;
        if (fm1_flash_read(off + sizeof *r + at, chunk, n))
            return 0;
        c = crc32_run(c, chunk, n);
    }
    return c == r->crc;
}

void dn_smk_store_init(void)
{
    uint32_t i, s;
    struct rec r;
    flash_ok = fm1_flash_size() >= LAYOUT_END;
    for (i = 0; i < NOBJ; i++) {
        obj_seq[i] = 0;
        for (s = 0; flash_ok && s < 2u; s++)
            if (rec_read(obj_off(i, s), &r) && r.seq > obj_seq[i])
                obj_seq[i] = r.seq, obj_side[i] = (uint8_t)s;
    }
}
int dn_smk_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int i = obj_index(obj);
    uint32_t off;
    struct rec r;
    if (i < 0 || !obj_seq[i])
        return -1;
    if (!rec_read(off = obj_off((uint32_t)i, obj_side[i]), &r) &&
        !rec_read(off = obj_off((uint32_t)i, 1u - obj_side[i]), &r))   /* (the newer gone bad since: the older) */
        return -1;
    if (r.len > max)
        r.len = max;
    if (fm1_flash_read(off + sizeof r, dst, r.len))
        return -1;
    return (int)r.len;
}
int dn_smk_store_save(uint32_t obj, const void *src, uint32_t len)
{
    int i = obj_index(obj);
    uint32_t side, off;
    struct rec r;
    if (i < 0 || len > OBJ_MAX || !flash_ok)
        return -1;
    side = obj_seq[i] ? 1u - obj_side[i] : 0u;      /* (src: the app's, in RAM, still while this runs) */
    off = obj_off((uint32_t)i, side);
    r.magic = REC_MAGIC, r.seq = obj_seq[i] + 1u, r.len = len, r.crc = crc32_run(0, (const uint8_t *)src, len);
    if (fm1_flash_erase(off) || (len && fm1_flash_write(off + sizeof r, src, len)) || fm1_flash_write(off, &r, sizeof r))
        return -2;                                   /* (the older record still stands) */
    obj_seq[i] = r.seq, obj_side[i] = (uint8_t)side;
    return 0;
}

/* ------------------------------------------------------------------------------------------------- slots --- */
struct zone { uint32_t off, n, ls, le, rate; int16_t root, pred; uint8_t idx, lo, hi, looped; };
struct slot_hdr { uint32_t magic; uint16_t version; uint8_t nz, rsv; char name[8]; uint32_t data_len, crc, rsv2[2];
                  struct zone zone[16]; };
_Static_assert(sizeof(struct zone) == 28 && sizeof(struct slot_hdr) == 480, "the FM-1's slot header");
static uint8_t slot_nz[SLOT_N];

static const uint8_t *slot_map(uint32_t k) { return fm1_flash_map(SLOT_OFF[k], SLOT_SIZE[k]); }
static void slot_scan(uint32_t k)                    /* how many zones slot k holds, all of them inside it */
{
    const uint8_t *m = slot_map(k);
    const struct slot_hdr *h = (const struct slot_hdr *)m;
    uint32_t z;
    slot_nz[k] = 0;
    if (!m || h->magic != SLOT_MAGIC || h->version != 1u || !h->nz || h->nz > 16u ||
        h->data_len > SLOT_SIZE[k] - SLOT_DATA)
        return;
    for (z = 0; z < h->nz; z++)
        if (h->zone[z].off > h->data_len || (h->zone[z].n + 1u) / 2u > h->data_len - h->zone[z].off)
            return;
    slot_nz[k] = h->nz;
}
int dn_smk_slot_zones(int k)
{
    static uint8_t scanned;
    if (!scanned && flash_ok) {
        uint32_t i;
        for (i = 0; i < SLOT_N; i++)
            slot_scan(i);
        scanned = 1;
    }
    return k >= 0 && k < (int)SLOT_N ? slot_nz[k] : 0;
}
const uint8_t *dn_smk_slot(int k, int z, uint32_t *nsamples, uint32_t *rate, char name[9])
{
    const uint8_t *m;
    const struct slot_hdr *h;
    uint32_t i;
    if (k < 0 || k >= (int)SLOT_N || z < 0 || z >= dn_smk_slot_zones(k) || !(m = slot_map((uint32_t)k)))
        return 0;
    h = (const struct slot_hdr *)m;
    for (i = 0; i < 8u; i++)
        name[i] = h->name[i] >= 32 && h->name[i] < 127 ? h->name[i] : 0;
    name[8] = 0;
    *nsamples = h->zone[z].n;
    *rate = (uint32_t)(((uint64_t)h->zone[z].rate * 44100u + 32768u) >> 16);   /* (Q16 of 44.1 kHz to Hz) */
    return m + SLOT_DATA + h->zone[z].off;
}

/* -------------------------------------------------------------------------------------------- the editor --- */
enum { ED_INFO = 1, ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO, ED_SMP_READ, ED_PING = 25,
       ED_SLICE_GET = 50, ED_SLICE_SET = 51 };
static uint8_t out[1100];
static uint32_t out_n;
static void put(uint32_t v)
{
    if (out_n < sizeof out - 1u)
        out[out_n++] = (uint8_t)(v & 0x7Fu);
}
static void put_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        put((uint8_t)s[i]);
    put(0);
}
static uint32_t get21(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14; }
static void put21(uint32_t v) { put(v); put(v >> 7); put(v >> 14); }
/* pack7: each group of up to 7 bytes after a byte of their top bits (bit j: byte j's) */
static uint32_t unpack7(const uint8_t *a, uint32_t na, uint8_t *o, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t tops = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            o[n++] = (uint8_t)(*a++ | ((tops >> j) & 1u) << 7);
    }
    return n;
}
static uint8_t wbuf[512] __attribute__((aligned(4)));

static int slot_erase(uint32_t k, int all)
{
    uint32_t s, n = all ? SLOT_SIZE[k] / SEC : 1u;
    dn_app_slots_changed(0);                         /* (BREAKS lets go of the slots' loops first) */
    slot_nz[k] = 0;
    for (s = 0; s < n; s++)
        if (fm1_flash_erase(SLOT_OFF[k] + s * SEC))
            return 1;
    return 0;
}
static uint32_t slot_end(uint32_t k, const uint8_t *a, uint32_t na)
{
    const struct slot_hdr *h = (const struct slot_hdr *)wbuf;
    const uint8_t *m;
    if (unpack7(a, na, wbuf, sizeof(struct slot_hdr)) != sizeof(struct slot_hdr))
        return 1;
    if (h->magic != SLOT_MAGIC || h->version != 1u || !h->nz || h->nz > 16u || h->data_len > SLOT_SIZE[k] - SLOT_DATA)
        return 2;
    if (!(m = slot_map(k)) || crc32_run(0, m + SLOT_DATA, h->data_len) != h->crc)
        return 3;
    if (fm1_flash_write(SLOT_OFF[k], wbuf, sizeof(struct slot_hdr)))
        return 4;
    slot_scan(k);
    dn_app_slots_changed(1);
    return slot_nz[k] ? 0u : 5u;
}

static void ed_handle(unsigned port, const uint8_t *f, uint32_t n)   /* f: 7D 46 4C cmd args (no F0 / F7) */
{
    uint32_t cmd = f[3], na = n - 4u, i;
    const uint8_t *a = f + 4;
    out[0] = 0xF0, out[1] = 0x7D, out[2] = 0x46, out[3] = 0x4C, out[4] = (uint8_t)cmd;
    out_n = 5;
    switch (cmd) {
    case ED_INFO:
        put_str("DREADNOUGHT " DN_VERSION, 24);
        for (i = 0; i < 6u; i++)
            put(0);
        break;
    case ED_PING:
        put(0);
        break;
    case ED_SMP_BEGIN:
    case ED_SMP_ERASE:
        if (na < 1u || a[0] >= SLOT_N || !flash_ok)
            return;
        put(a[0]);
        put((uint32_t)slot_erase(a[0], cmd == ED_SMP_ERASE));
        break;
    case ED_SMP_WRITE: {
        uint32_t off, len, rc = 0;
        if (na < 5u || a[0] >= SLOT_N || !flash_ok)
            return;
        off = get21(a + 1);
        len = unpack7(a + 4, na - 4u, wbuf, 256u);
        if (off < SLOT_DATA || (off & 0xFFu) || !len || off + len > SLOT_SIZE[a[0]])
            rc = 1;
        else if (slot_nz[a[0]])
            rc = 4;                                  /* (SMP_BEGIN first: it empties the slot) */
        else if (!(off & (SEC - 1u)) && fm1_flash_erase(SLOT_OFF[a[0]] + off))
            rc = 2;
        else if (fm1_flash_write(SLOT_OFF[a[0]] + off, wbuf, len))
            rc = 3;
        put(a[0]);
        put21(off);
        put(rc);
        break;
    }
    case ED_SMP_END:
        if (na < 2u || a[0] >= SLOT_N || !flash_ok)
            return;
        put(a[0]);
        put(slot_end(a[0], a + 1, na - 1u));
        break;
    case ED_SMP_INFO:
        put(SLOT_N);
        put(SLOT_SIZE[0] / 1024u);
        for (i = 0; i < SLOT_N; i++) {
            const struct slot_hdr *h = (const struct slot_hdr *)slot_map(i);
            char nm[9] = {0};
            uint32_t j, nz = (uint32_t)dn_smk_slot_zones((int)i);
            put(nz);
            for (j = 0; nz && h && j < 8u; j++)
                nm[j] = h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
            put_str(nm, 8);
            put(nz && h ? (h->data_len + 1023u) / 1024u : 0u);
        }
        for (i = 0; i < SLOT_N; i++)                 /* (0.46, after what an FM-1 sends: each slot's KB, as they differ) */
            put(SLOT_SIZE[i] / 1024u);
        break;
    case ED_SMP_READ: {                              /* slot, offset, pairs -> slot, offset, the bytes packed */
        uint32_t off, cnt, k, j;
        const uint8_t *m;
        if (na < 5u || a[0] >= SLOT_N || !(m = slot_map(a[0])))
            return;
        off = get21(a + 1);
        cnt = a[4] > 112u ? 224u : a[4] * 2u;
        if (off >= SLOT_SIZE[a[0]])
            return;
        if (cnt > SLOT_SIZE[a[0]] - off)
            cnt = SLOT_SIZE[a[0]] - off;
        put(a[0]);
        put21(off);
        for (k = 0; k < cnt; k += 7u) {
            uint32_t tops = 0;
            for (j = 0; j < 7u && k + j < cnt; j++)
                tops |= (uint32_t)(m[off + k + j] >> 7) << j;
            put(tops);
            for (j = 0; j < 7u && k + j < cnt; j++)
                put(m[off + k + j]);
        }
        break;
    }
    case ED_SLICE_GET:
    case ED_SLICE_SET: {
        static uint32_t st[DN_BRK_MAXSL];
        uint32_t key, cnt, j;
        if (na < 5u)
            return;
        key = (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
        for (j = 0; j < 5u; j++)
            put(a[j]);
        if (cmd == ED_SLICE_GET) {
            cnt = dn_app_slices_get(key, st);
            put(cnt);
            for (j = 0; j < cnt; j++)
                put21(st[j]);
        } else {
            cnt = na > 5u ? a[5] : 0u;
            if (cnt > DN_BRK_MAXSL || na < 6u + 3u * cnt) {
                put(1);
                break;
            }
            for (j = 0; j < cnt; j++)
                st[j] = get21(a + 6u + 3u * j);
            put((uint32_t)dn_app_slices_set(key, cnt, st));
        }
        break;
    }
    default:
        if (!dn_ed_cmd(cmd, a, na, put))
            return;                                  /* (not ours: no reply) */
        break;
    }
    out[out_n++] = 0xF7;
    (void)dn_smk_send(port, out, out_n);
}

/* frames arrive on the SDK's MIDI routing task (fm1_app_sysex: copied, nothing else done there) and wait here for
 * dn_smk_editor_poll on the control task, where the flash may be written */
#define FRAME_MAX 1024u
static struct { uint8_t b[FRAME_MAX]; volatile uint32_t n; uint8_t port; } inq[2];
void fm1_app_sysex(const uint8_t *m, size_t n)
{
    uint32_t i;
    if (!m || n < 6u || n > FRAME_MAX || m[0] != 0xF0u || m[n - 1u] != 0xF7u || m[1] != 0x7Du || m[2] != 0x46u ||
        m[3] != 0x4Cu)
        return;
    for (i = 0; i < 2u; i++)
        if (!inq[i].n) {
            memcpy(inq[i].b, m, n);
            inq[i].port = (uint8_t)fm1_midi_input_source();
            __asm__ volatile("csync" ::: "memory");
            inq[i].n = (uint32_t)n;
            fm1_control_request();
            return;
        }
}
void dn_smk_editor_poll(void)
{
    uint32_t i;
    for (i = 0; i < 2u; i++)
        if (inq[i].n) {
            __asm__ volatile("csync" ::: "memory");   /* (the frame's bytes after its length) */
            ed_handle(inq[i].port, inq[i].b + 1, inq[i].n - 2u);
            inq[i].n = 0;
        }
}

/* the SDK's transfer hooks (M-VAVE's project transfers): DREADNOUGHT has its own (above) */
void groove_transfer_init(void) {}
void groove_transfer_poll(void) {}
