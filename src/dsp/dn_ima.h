/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the IMA ADPCM decode step, shared by BREAKS and SAMPLER. Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>

/* ---- IMA ADPCM (DREADNOUGHT's own, 2026-10, from the IMA Digital Audio Focus and Technical Working Groups'
 * "Recommended Practices for Enhancing Digital Audio Compatibility in Multimedia Systems", rev. 3.00, 1992).
 * Each 4-bit code is a sign (bit 3) and a magnitude (bits 2..0) in units of the current step size. The difference is
 * built the way the recommendation spells it out, from shifts of the step (not a multiply: the rounding of the shifts
 * is the format, and every encoder's predictor tracks exactly this), so a loop decodes to the bit as it was encoded.
 * The step size then moves along the 89-step table by the index adjustment of the code's magnitude. */
static const int16_t IMA_STEP[89] = {                  /* the step sizes: 7 .. 32767, ~1.1x apart */
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int8_t IMA_MOVE[8] = {-1, -1, -1, -1, 2, 4, 6, 8};   /* index change by magnitude 0..7 */

/* the sample at nibble i (two to a byte, low nibble first), from the decoder state before it; the state moves on */
static inline int32_t dn_ima(const uint8_t *d, uint32_t i, int32_t *pred, int32_t *idx)
{
    uint32_t nib = (uint32_t)(d[i >> 1] >> ((i & 1u) << 2)) & 15u, mag = nib & 7u;
    int32_t q = IMA_STEP[*idx], delta = q >> 3, v;
    if (mag & 4u)
        delta += q;
    if (mag & 2u)
        delta += q >> 1;
    if (mag & 1u)
        delta += q >> 2;
    v = nib & 8u ? *pred - delta : *pred + delta;
    *pred = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
    v = *idx + IMA_MOVE[mag];
    *idx = v < 0 ? 0 : v > 88 ? 88 : v;
    return *pred;
}
