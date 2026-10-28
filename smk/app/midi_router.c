/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * DREADNOUGHT on the SMK-37 Pro / Elite: MIDI in and out. Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * In: the SDK hands each complete short message from USB or Bluetooth to fm1_app_midi_received (its routing task);
 * the channel messages go into a ring the audio interrupt empties into the app (dn_app_midi), as the FM-1's USB queue
 * is emptied. Out: the app's packets (the MIDI tracks' notes, the clock) go into a ring from any context, and the
 * control task sends them (fm1_midi_send: control task only). Long SysEx replies leave in pieces (dn_smk_send).
 *
 * And the hooks M-VAVE's own instrument calls from inside the SDK: DREADNOUGHT never renders that instrument, so
 * they do nothing. */
#include "fm1.h"
#include "fm1_device.h"
#include "instrument.h"
#include "fm_patch.h"
#include "dn_smk.h"
#include <string.h>

static inline void barrier(void) { __asm__ volatile("csync" ::: "memory"); }

/* ------------------------------------------------------------------------------------------------------- in --- */
#define IN_N 128u                                    /* (a power of two) */
static uint32_t in_ring[IN_N];
static volatile uint32_t in_w, in_r;                 /* one writer (the routing task), one reader (the audio) */

void fm1_app_midi_received(unsigned source, const uint8_t *m, size_t n)
{
    uint32_t w = in_w;
    (void)source;
    if (!m || n < 2u || m[0] < 0x80u || m[0] >= 0xF0u)
        return;                                      /* (channel messages only: the app takes no others) */
    if (w - in_r >= IN_N)
        return;                                      /* (full: dropped) */
    in_ring[w & (IN_N - 1u)] = (uint32_t)(m[0] >> 4) | (uint32_t)m[0] << 8 | (uint32_t)(m[1] & 0x7Fu) << 16 |
                               (uint32_t)(n > 2u ? m[2] & 0x7Fu : 0u) << 24;
    barrier();
    in_w = w + 1u;
}
int dn_smk_midi_in(uint32_t *pkt)
{
    uint32_t r = in_r;
    if (r == in_w)
        return 0;
    barrier();                                       /* (the packet after the index that says it is there) */
    *pkt = in_ring[r & (IN_N - 1u)];
    barrier();
    in_r = r + 1u;
    return 1;
}

/* ------------------------------------------------------------------------------------------------------ out --- */
#define OUT_N 256u
static uint32_t out_ring[OUT_N];
static volatile uint32_t out_w, out_r;
void dn_smk_midi_out(uint32_t pkt)                   /* (the audio interrupt, or the control task with it held off) */
{
    uint32_t s = fm1_device_irq_save(), w = out_w;
    if (w - out_r < OUT_N) {
        out_ring[w & (OUT_N - 1u)] = pkt;
        out_w = w + 1u;
    }
    fm1_device_irq_restore(s);
}
static size_t msg_len(uint8_t st)
{
    if (st >= 0xF8u)
        return 1;                                    /* clock, start, continue, stop */
    if (st == 0xF2u)
        return 3;                                    /* song position */
    if (st == 0xF1u || st == 0xF3u || (st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u)
        return 2;
    return st >= 0xF0u ? 0 : 3;
}
void dn_smk_midi_poll(void)
{
    uint32_t guard = OUT_N;
    while (guard-- && out_r != out_w) {
        uint32_t pkt = out_ring[out_r & (OUT_N - 1u)];
        uint8_t m[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
        size_t n = msg_len(m[0]);
        if (n && m[0] >= 0x80u && fm1_midi_send(m, n) == -2)
            return;                                  /* (busy: the rest next time) */
        out_r++;
    }
}

/* the editor's replies: up to 128 bytes go whole; longer ones as F0 7D 46 4C 7E i n <up to 100 bytes> F7, the
 * reply's bytes between its F0 and F7 in n pieces (the web editor puts them together again) */
#define PIECE 100u
int dn_smk_send(unsigned port, const uint8_t *m, size_t n)
{
    uint8_t f[PIECE + 8u];
    size_t body, k, i, pieces;
    if (n <= 128u) {
        uint32_t tries = 50;
        int rc;
        while ((rc = fm1_midi_send_port(port, m, n)) == -2 && tries--)
            fm1_device_delay(1);                     /* (the port's queue full: a moment for it) */
        return rc;
    }
    body = n - 2u;
    pieces = (body + PIECE - 1u) / PIECE;
    if (pieces > 127u)
        return -1;
    for (i = 0; i < pieces; i++) {
        size_t at = i * PIECE, len = body - at > PIECE ? PIECE : body - at;
        uint32_t tries = 50;
        f[0] = 0xF0, f[1] = 0x7D, f[2] = 0x46, f[3] = 0x4C, f[4] = 0x7E, f[5] = (uint8_t)i, f[6] = (uint8_t)pieces;
        for (k = 0; k < len; k++)
            f[7u + k] = m[1u + at + k] & 0x7Fu;
        f[7u + len] = 0xF7;
        while (fm1_midi_send_port(port, f, 8u + len) == -2 && tries--)
            fm1_device_delay(1);                     /* (the port's queue full: wait for it) */
    }
    return 0;
}

/* ------------------------------------------------------------------------- M-VAVE's instrument's hooks --- */
void groove_midi_init(void) {}
void groove_midi_poll(void) {}
void groove_midi_panic(void) {}
void groove_midi_clock_reset(void) {}
FM1_DSP_CODE unsigned groove_midi_mode(unsigned track) { (void)track; return 0; }
FM1_DSP_CODE void groove_midi_note(unsigned track, unsigned note, unsigned velocity) { (void)track, (void)note, (void)velocity; }
FM1_DSP_CODE void groove_midi_arp_note(unsigned track, unsigned note, unsigned velocity) { (void)track, (void)note, (void)velocity; }
FM1_DSP_CODE void groove_midi_track_reset(unsigned track) { (void)track; }
FM1_DSP_CODE void groove_midi_transport(unsigned owner, uint8_t status) { (void)owner, (void)status; }
FM1_DSP_CODE void groove_midi_clock(unsigned owner, unsigned tempo) { (void)owner, (void)tempo; }
FM1_DSP_CODE void groove_fm_program(uint8_t patch[156], unsigned program)
{
    unsigned i;
    (void)program;
    for (i = 0; i < 156u; i++)
        patch[i] = 0;
}
