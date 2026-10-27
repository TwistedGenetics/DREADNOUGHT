/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: WAVE (0.32), the wavetable synth (dn_wt.h says what it does). Copyright (C) 2026 Jamie (TwistedGenetics).
 * Single-precision float for the filter and the envelopes (the FM-1's FPU), integers for the phases and the tables;
 * no 64-bit divides. */
#include "dn_wt.h"
#include "dn_tables.h"
#include "dn_math.h"

#define WT_TICK 32u                                 /* control: every 32 samples */
static const uint16_t WT_MIPOFF[7] = {0, 512, 768, 896, 960, 992, 1008};   /* each version's first sample in a frame */

/* ---- tables ------------------------------------------------------------------------------------------------ */
uint32_t dn_wt_frames(const uint8_t *t) { return t ? t[5] : 0u; }
int dn_wt_valid(const uint8_t *t, uint32_t len)
{
    if (!t || len < DN_WT_HDR || t[0] != 'D' || t[1] != 'N' || t[2] != 'W' || t[3] != 'T' || t[4] != 1u || !t[5] ||
        t[5] > DN_WT_MAXF || t[6] != 9u || t[7] != 7u)
        return 0;
    return len >= DN_WT_HDR + (uint32_t)t[5] * DN_WT_FRAME_BYTES ? (int)t[5] : 0;
}
void dn_wt_name(const uint8_t *t, char out[9])
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        out[i] = t && t[8u + i] >= 32u && t[8u + i] < 127u ? (char)t[8u + i] : 0;
    out[8] = 0;
}
static inline int32_t wt_s16(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }   /* (any alignment) */
/* a frame's version m at phase q, linear between its samples */
static inline int32_t wt_read(const uint8_t *fr, uint32_t m, uint32_t q)
{
    uint32_t sh = 23u + m, sz = 512u >> m, i = q >> sh, j = (i + 1u) & (sz - 1u), f = (q >> (sh - 15u)) & 0x7FFFu;
    const uint8_t *b = fr + 2u * WT_MIPOFF[m];
    int32_t a = wt_s16(b + 2u * i), c = wt_s16(b + 2u * j);
    return a + (((c - a) * (int32_t)f) >> 15);
}
int32_t dn_wt_peek(const uint8_t *t, uint32_t f, uint32_t m, uint32_t q)
{
    return wt_read(t + DN_WT_HDR + f * DN_WT_FRAME_BYTES, m, q);
}

/* ---- small maths ------------------------------------------------------------------------------------------- */
static float wt_exp2(float x)                       /* 2^x for |x| < 8, to ~1e-4 */
{
    int32_t i = (int32_t)(x < 0.0f ? x - 1.0f : x);
    float f = x - (float)i, p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    while (i > 0) p *= 2.0f, i--;
    while (i < 0) p *= 0.5f, i++;
    return p;
}
static uint32_t wt_inc(float semis)                 /* the phase step of a pitch in semitones (MIDI note numbers) */
{
    int32_t n = (int32_t)(semis < 0.0f ? semis - 1.0f : semis);
    float f;
    if (n < 0) n = 0;
    if (n > 126) n = 126;
    f = semis - (float)n;
    if (f > 2.0f) f = 2.0f;
    if (f < 0.0f) f = 0.0f;
    return (uint32_t)((float)DN_NOTE_INC[n] * wt_exp2(f * (1.0f / 12.0f)));
}
static inline int32_t wt_sine(uint32_t q)           /* Q15 */
{
    uint32_t i = q >> 22, f = (q >> 7) & 0x7FFFu;
    int32_t a = DN_SINE[i], b = DN_SINE[i + 1u];
    return a + (((b - a) * (int32_t)f) >> 15);
}
static inline float wt_clip(float x)                /* a soft clip (tanh-like) */
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
static float wt_k(int32_t v) { return (float)DN_ENV_K[v < 0 ? 0 : v > 127 ? 127 : v] * (1.0f / 65536.0f); }


/* ---- values ------------------------------------------------------------------------------------------------ */
static const int8_t WT_MIN[WP_N] = {
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -64, 0, 0, 0, 0, 0, 0, 0, 0, 0, -64, 0, 0, 0, 0, 1, 0, -24,
    0, 0, 0, 0, 0, -3, -12, -50, 0, 0, 0, 0, 0, 0, 0, 0,                    /* OSC B .. SAT */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                             /* the LFOs, ENV 3 */
    0, 0, -64, 0, 0, -64, 0, 0, -64, 0, 0, -64, 0, 0, -64, 0, 0, -64, 0, 0, -64, 0, 0, -64};
static const int8_t WT_MAX[WP_N] = {
    0, 127, WW_N - 1, 127, 4, 127, 127, 127, 127, 127, 63, 127, 127, 127, 127, 127, 127, 127, 127, 127, 63, 127, 127,
    127, 127, DN_WT_NV, 127, 24,
    1, 0, 127, WW_N - 1, 127, 3, 12, 50, 127, WB_N - 1, 127, 127, WF_N - 1, 127, WD_N - 1, 127,
    WL_N - 1, 1, 1, WL_N - 1, 127, 1, 1, WL_N - 1, 127, 1, 1, 127, 127, 127, 127,
    WS_N - 1, WT_N - 1, 63, WS_N - 1, WT_N - 1, 63, WS_N - 1, WT_N - 1, 63, WS_N - 1, WT_N - 1, 63,
    WS_N - 1, WT_N - 1, 63, WS_N - 1, WT_N - 1, 63, WS_N - 1, WT_N - 1, 63, WS_N - 1, WT_N - 1, 63};
static const char *const WT_NAME[WP_N] = {
    "TABLE", "POS", "WARP", "AMOUNT", "UNISON", "DETUNE", "SPREAD", "SUB", "CUT", "RES", "ENV", "DRIVE", "ATK", "DEC",
    "SUS", "REL", "M.ATK", "M.DEC", "M.SUS", "M.REL", "POS ENV", "POS LFO", "L1 RATE", "CUT LFO", "LEVEL", "VOICES",
    "GLIDE", "TRANS",
    "OSC B", "B TABLE", "B POS", "B WARP", "B AMT", "B OCT", "B SEMI", "B FINE", "B LEVEL", "B MODE", "MOD AMT",
    "NOISE", "FILTER", "KEY", "DIST", "SAT",
    "L1 SHAPE", "L1 SYNC", "L1 MODE", "L2 SHAPE", "L2 RATE", "L2 SYNC", "L2 MODE", "L3 SHAPE", "L3 RATE", "L3 SYNC",
    "L3 MODE", "E3 ATK", "E3 DEC", "E3 SUS", "E3 REL",
    "SOURCE", "DEST", "DEPTH", "SOURCE", "DEST", "DEPTH", "SOURCE", "DEST", "DEPTH", "SOURCE", "DEST", "DEPTH",
    "SOURCE", "DEST", "DEPTH", "SOURCE", "DEST", "DEPTH", "SOURCE", "DEST", "DEPTH", "SOURCE", "DEST", "DEPTH"};
static const int8_t WT_DEF[WP_N] = {
    0, 0, 0, 0, 1, 20, 64, 0, 100, 20, 0, 0, 2, 60, 110, 40, 2, 60, 0, 40, 0, 0, 40, 0, 100, DN_WT_NV, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 100, WB_MIX, 40, 0, WF_LP12, 0, WD_SOFT, 0,
    WL_SINE, 0, 0, WL_TRI, 30, 0, 0, WL_SH, 70, 0, 0, 2, 60, 0, 40,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
typedef char wt_tables_fit[sizeof WT_NAME / sizeof WT_NAME[0] == WP_N ? 1 : -1];
static const char *const WT_WARPN[WW_N] = {"OFF", "SYNC", "BEND", "MIRROR", "PWM", "FM", "CRUSH"};
static const char *const WT_BMN[WB_N] = {"MIX", "FM", "RING", "AM", "SYNC"};
static const char *const WT_FTN[WF_N] = {"LP12", "LP24", "HP", "BP", "NOTCH", "COMB"};
static const char *const WT_DTN[WD_N] = {"SOFT", "HARD", "FOLD", "RECT", "TUBE", "BITS", "RATE"};
static const char *const WT_LSN[WL_N] = {"SINE", "TRI", "SAW", "SQUARE", "S&H", "RANDOM"};
static const char *const WT_SRCN[WS_N] = {"OFF", "LFO 1", "LFO 2", "LFO 3", "MOD ENV", "ENV 3", "AMP ENV", "VEL",
                                          "KEY", "RANDOM"};
static const char *const WT_DSTN[WT_N] = {"OFF", "POS A", "POS B", "WARP A", "WARP B", "PITCH", "PITCH B", "MOD AMT",
                                          "B LEVEL", "CUTOFF", "RES", "DRIVE", "LEVEL", "PAN", "SUB", "NOISE",
                                          "L1 RATE", "L2 RATE", "DETUNE"};
static const char *const WT_DIVN[DN_WT_NDIV] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4D", "1/2T", "1/4", "1/8D",
                                                "1/4T", "1/8", "1/16D", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const float WT_DIVB[DN_WT_NDIV] = {16.0f, 8.0f, 4.0f, 2.0f, 1.5f, 1.3333333f, 1.0f, 0.75f, 0.6666667f, 0.5f,
                                          0.375f, 0.3333333f, 0.25f, 0.1666667f, 0.125f, 0.0833333f};   /* (beats) */
const char *dn_wt_vname(uint32_t k) { return k < WP_N ? WT_NAME[k] : ""; }
int32_t dn_wt_vmin(uint32_t k) { return k < WP_N ? WT_MIN[k] : 0; }
int32_t dn_wt_vmax(uint32_t k) { return k < WP_N ? WT_MAX[k] : 0; }
int dn_wt_is_rate(uint32_t k) { return k == WP_LRATE ? 1 : k == WP_L2RATE ? 2 : k == WP_L3RATE ? 3 : 0; }
static uint32_t wt_sync_of(uint32_t l) { return l == 0u ? WP_L1SYNC : l == 1u ? WP_L2SYNC : WP_L3SYNC; }
static void wt_num(char *o, int32_t v)
{
    char t[8];
    int32_t n = 0, i = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0) o[i++] = '-';
    do t[n++] = (char)('0' + u % 10u), u /= 10u; while (u);
    while (n) o[i++] = t[--n];
    o[i] = 0;
}
static void wt_str(char *o, const char *w)
{
    uint32_t i;
    for (i = 0; w[i] && i < 7u; i++) o[i] = w[i];
    o[i] = 0;
}
static int32_t wt_cl(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
void dn_wt_vtext(uint32_t k, int32_t v, const int16_t *p, char out[8])
{
    int l = dn_wt_is_rate(k);
    if (k == WP_WARP || k == WP_BWARP) { wt_str(out, WT_WARPN[wt_cl(v, 0, WW_N - 1)]); return; }
    if (k == WP_BMODE) { wt_str(out, WT_BMN[wt_cl(v, 0, WB_N - 1)]); return; }
    if (k == WP_FTYPE) { wt_str(out, WT_FTN[wt_cl(v, 0, WF_N - 1)]); return; }
    if (k == WP_DTYPE) { wt_str(out, WT_DTN[wt_cl(v, 0, WD_N - 1)]); return; }
    if (k == WP_L1SHAPE || k == WP_L2SHAPE || k == WP_L3SHAPE) { wt_str(out, WT_LSN[wt_cl(v, 0, WL_N - 1)]); return; }
    if (k == WP_L1SYNC || k == WP_L2SYNC || k == WP_L3SYNC || k == WP_OSCB) { wt_str(out, v ? (k == WP_OSCB ? "ON" : "SYNC") : (k == WP_OSCB ? "OFF" : "FREE")); return; }
    if (k == WP_L1MODE || k == WP_L2MODE || k == WP_L3MODE) { wt_str(out, v ? "RETRIG" : "FREE"); return; }
    if (k >= WP_M0 && k < WP_N && (k - WP_M0) % 3u == 0u) { wt_str(out, WT_SRCN[wt_cl(v, 0, WS_N - 1)]); return; }
    if (k >= WP_M0 && k < WP_N && (k - WP_M0) % 3u == 1u) { wt_str(out, WT_DSTN[wt_cl(v, 0, WT_N - 1)]); return; }
    if (l && p && p[wt_sync_of((uint32_t)l - 1u)]) { wt_str(out, WT_DIVN[wt_cl(v, 0, 127) * (int32_t)DN_WT_NDIV / 128]); return; }
    if (l) {                                        /* 0.05 .. 20 Hz */
        float hz = 0.05f;
        int32_t c, i;
        for (i = 0; i < v; i++) hz *= 1.048309f;
        c = (int32_t)(hz * 100.0f + 0.5f);
        if (c < 1000) {
            out[0] = (char)('0' + c / 100), out[1] = '.', out[2] = (char)('0' + c / 10 % 10), out[3] = (char)('0' + c % 10);
            out[4] = 'H', out[5] = 'z', out[6] = 0;
        } else {
            c = (c + 5) / 10;
            wt_num(out, c / 10);
            for (i = 0; out[i]; i++) ;
            out[i] = '.', out[i + 1] = (char)('0' + c % 10), out[i + 2] = 'H', out[i + 3] = 'z', out[i + 4] = 0;
        }
        return;
    }
    if (k == WP_VOICES && v == 1) { wt_str(out, "MONO"); return; }
    if ((k == WP_FENV || k == WP_PENV || k == WP_TRANS || k == WP_BOCT || k == WP_BSEMI || k == WP_BFINE ||
         (k >= WP_M0 && (k - WP_M0) % 3u == 2u)) && v > 0) {
        out[0] = '+';
        wt_num(out + 1, v);
        return;
    }
    wt_num(out, (k == WP_TABLE || k == WP_BTAB) ? v + 1 : v);
}
void dn_wt_defaults(int16_t *p)
{
    uint32_t k;
    for (k = 0; k < WP_N; k++)
        p[k] = WT_DEF[k];
}
void dn_wt_upgrade(int16_t *p, uint32_t n)
{
    for (; n < WP_N; n++)
        p[n] = WT_DEF[n];
}

/* ---- the factory sounds: INIT's values, then each one's own (value, setting) pairs ------------------------------ */
typedef struct { char name[11]; int8_t kv[64]; } wt_preset_t;   /* (value, setting) pairs, then -1 */
static const wt_preset_t WT_PRESET[] = {
    {"INIT", {-1}},
    {"SUPERSAW", {WP_POS, 127, WP_UNI, 4, WP_DET, 45, WP_SPREAD, 110, WP_CUT, 112, WP_RES, 10, WP_SUS, 115, WP_REL, 55,
                      WP_LEVEL, 90, WP_FTYPE, WF_LP12, WP_SAT, 20, WP_FKEY, 30, WP_ATK, 2, -1}},
    {"REESE", {WP_TABLE, 1, WP_POS, 40, WP_UNI, 2, WP_DET, 25, WP_SPREAD, 60, WP_SUB, 60, WP_CUT, 62, WP_RES, 30,
                   WP_FENV, 18, WP_DRIVE, 40, WP_ATK, 0, WP_SUS, 127, WP_REL, 22, WP_MDEC, 70, WP_PLFO, 30, WP_LRATE, 18,
                   WP_VOICES, 1, WP_GLIDE, 18, -1}},
    {"PPG BELL", {WP_TABLE, 2, WP_CUT, 120, WP_RES, 10, WP_ATK, 0, WP_DEC, 82, WP_SUS, 40, WP_REL, 70, WP_MATK, 0,
                      WP_MDEC, 72, WP_MREL, 60, WP_PENV, 50, -1}},
    {"VOX PAD", {WP_TABLE, 3, WP_POS, 30, WP_UNI, 2, WP_DET, 15, WP_SPREAD, 90, WP_CUT, 92, WP_RES, 15, WP_ATK, 62,
                     WP_DEC, 80, WP_REL, 82, WP_PLFO, 48, WP_LRATE, 14, -1}},
    {"SYNC LEAD", {WP_TABLE, 4, WP_POS, 10, WP_WARP, WW_SYNC, WP_WAMT, 50, WP_SUB, 30, WP_RES, 30, WP_FENV, 10,
                       WP_DRIVE, 20, WP_ATK, 0, WP_SUS, 110, WP_MATK, 0, WP_MDEC, 62, WP_MSUS, 20, -1}},
    {"PWM STRNG", {WP_TABLE, 5, WP_POS, 40, WP_UNI, 2, WP_DET, 20, WP_SPREAD, 90, WP_CUT, 86, WP_RES, 10, WP_ATK, 52,
                       WP_DEC, 70, WP_REL, 70, WP_PLFO, 40, WP_LRATE, 24, -1}},
    {"GRIT BASS", {WP_TABLE, 6, WP_POS, 20, WP_SUB, 50, WP_CUT, 70, WP_RES, 40, WP_FENV, 30, WP_DRIVE, 70, WP_ATK, 0,
                       WP_DEC, 64, WP_SUS, 100, WP_REL, 28, WP_MATK, 0, WP_MDEC, 52, WP_MREL, 30, WP_PENV, 60,
                       WP_VOICES, 1, WP_GLIDE, 6, -1}},
    {"ORGAN", {WP_TABLE, 7, WP_POS, 60, WP_SUB, 20, WP_CUT, 104, WP_RES, 10, WP_DRIVE, 10, -1}},
    {"FM GROWL", {WP_WARP, WW_FM, WP_WAMT, 60, WP_SUB, 40, WP_CUT, 80, WP_RES, 50, WP_FENV, 20, WP_DRIVE, 50,
                      WP_ATK, 0, WP_SUS, 120, WP_REL, 30, WP_CLFO, 30, WP_VOICES, 1, WP_GLIDE, 10, WP_TRANS, -12, -1}},
    {"BENT KEYS", {WP_TABLE, 7, WP_POS, 30, WP_WARP, WW_BEND, WP_WAMT, 70, WP_UNI, 2, WP_DET, 12, WP_SPREAD, 70,
                       WP_CUT, 96, WP_FENV, 15, WP_ATK, 0, WP_DEC, 72, WP_SUS, 70, -1}},
    {"CRUSHER", {WP_TABLE, 2, WP_POS, 50, WP_WARP, WW_CRUSH, WP_WAMT, 64, WP_SUB, 30, WP_CUT, 90, WP_RES, 30,
                     WP_DRIVE, 40, WP_ATK, 0, WP_REL, 30, WP_PLFO, 40, WP_LRATE, 50, WP_LEVEL, 95, -1}},
    /* 0.33: two oscillators, filters, drive, the matrix */
    {"FM BELL", {WP_OSCB, 1, WP_BSEMI, 7, WP_BOCT, 1, WP_BLEVEL, 0, WP_BMODE, WB_FM, WP_BAMT, 40, WP_CUT, 124,
                     WP_ATK, 0, WP_DEC, 86, WP_SUS, 0, WP_REL, 72, WP_E3DEC, 70, WP_M0, WS_ENV3, WP_M0 + 1, WT_BMOD,
                     WP_M0 + 2, 50, WP_VOICES, 4, -1}},
    {"SYNC SWEEP", {WP_OSCB, 1, WP_BPOS, 127, WP_BLEVEL, 0, WP_BMODE, WB_SYNC, WP_BAMT, 10, WP_POS, 127,
                        WP_CUT, 100, WP_RES, 25, WP_MATK, 0, WP_MDEC, 75, WP_MSUS, 20, WP_M0, WS_MENV, WP_M0 + 1, WT_BMOD,
                        WP_M0 + 2, 50, WP_VOICES, 1, WP_GLIDE, 16, WP_DRIVE, 25, -1}},
    {"RING DRONE", {WP_OSCB, 1, WP_TABLE, 3, WP_POS, 30, WP_BTAB, 7, WP_BOCT, -1, WP_BSEMI, 7, WP_BMODE, WB_RING,
                        WP_BAMT, 90, WP_BLEVEL, 20, WP_ATK, 70, WP_REL, 90, WP_CUT, 90, WP_M0, WS_LFO2, WP_M0 + 1, WT_POSA,
                        WP_M0 + 2, 40, WP_L2RATE, 10, WP_VOICES, 4, -1}},
    {"COMB PLUCK", {WP_POS, 127, WP_FTYPE, WF_COMB, WP_CUT, 80, WP_RES, 116, WP_FKEY, 127, WP_ATK, 0, WP_SUS, 0,
                       WP_DEC, 84, WP_REL, 70, WP_NOISE, 50, WP_LEVEL, 110, -1}},
    {"FOLD BASS", {WP_DTYPE, WD_FOLD, WP_DRIVE, 70, WP_FTYPE, WF_LP24, WP_CUT, 74, WP_RES, 30, WP_SUB, 60,
                       WP_ATK, 0, WP_SUS, 120, WP_MATK, 0, WP_MDEC, 60, WP_M0, WS_MENV, WP_M0 + 1, WT_DRIVE, WP_M0 + 2, 40,
                       WP_VOICES, 1, WP_GLIDE, 8, WP_TRANS, -12, -1}},
    {"HOOVER", {WP_TABLE, 1, WP_POS, 80, WP_UNI, 4, WP_DET, 70, WP_SPREAD, 120, WP_OSCB, 1, WP_BPOS, 127,
                    WP_BOCT, -1, WP_BLEVEL, 80, WP_MATK, 0, WP_MDEC, 45, WP_M0, WS_MENV, WP_M0 + 1, WT_PITCH, WP_M0 + 2, -8,
                    WP_VOICES, 1, WP_GLIDE, 30, WP_SAT, 40, -1}},
    {"BIT WOBBLE", {WP_TABLE, 5, WP_POS, 60, WP_DTYPE, WD_BITS, WP_DRIVE, 70, WP_FTYPE, WF_LP24, WP_CUT, 60,
                        WP_RES, 60, WP_L2SYNC, 1, WP_L2RATE, 76, WP_M0, WS_LFO2, WP_M0 + 1, WT_CUT, WP_M0 + 2, 40,
                        WP_VOICES, 1, -1}},
    {"S&H TWIST", {WP_TABLE, 2, WP_POS, 40, WP_L3SYNC, 1, WP_L3RATE, 100, WP_L3SHAPE, WL_SH, WP_M0, WS_LFO3,
                       WP_M0 + 1, WT_POSA, WP_M0 + 2, 40, WP_M0 + 3, WS_LFO3, WP_M0 + 4, WT_CUT, WP_M0 + 5, 20,
                       WP_RES, 40, -1}},
};
#define WT_NPRESET (sizeof WT_PRESET / sizeof WT_PRESET[0])
uint32_t dn_wt_preset_count(void) { return WT_NPRESET; }
const char *dn_wt_preset_name(uint32_t i) { return WT_PRESET[i % WT_NPRESET].name; }
void dn_wt_preset(uint32_t i, int16_t *p)
{
    const wt_preset_t *q = &WT_PRESET[i % WT_NPRESET];
    uint32_t k;
    dn_wt_defaults(p);
    for (k = 0; k < 32u && q->kv[2u * k] >= 0; k++)
        p[q->kv[2u * k]] = q->kv[2u * k + 1u];
}

/* ---- voices ------------------------------------------------------------------------------------------------ */
void dn_wt_init(dn_wt_t *s)
{
    uint8_t *b = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    dn_wt_defaults(s->p);
    s->rng = 0x12345678u;
    s->bpm = 120;
}
void dn_wt_set_table(dn_wt_t *s, const uint8_t *t) { s->tab = t; }
void dn_wt_set_table_b(dn_wt_t *s, const uint8_t *t) { s->tabb = t; }
static float wt_rand(uint32_t *r)                   /* -1..1 */
{
    *r = *r * 1664525u + 1013904223u;
    return (float)(int32_t)*r * (1.0f / 2147483648.0f);
}

static void wt_start(dn_wt_t *s, dn_wt_v_t *v, uint8_t note, uint8_t vel, int legato)
{
    uint32_t u, l;
    v->note = note, v->vel = vel, v->gate = 1, v->seq = ++s->seq;
    v->target = (float)note + (float)s->p[WP_TRANS];
    if (!legato || !v->live || s->p[WP_GLIDE] == 0)
        v->pitch = v->target;
    if (!legato || !v->live) {
        v->amp.st = 0, v->amp.lv = 0.0f;
        v->mod.st = 0, v->mod.lv = 0.0f;
        v->e3.st = 0, v->e3.lv = 0.0f;
        for (u = 0; u < DN_WT_NU; u++) {            /* (unison: each its own start, so they beat) */
            v->ph[u] = u ? (s->seq * 0x9E3779B9u) ^ (u * 0x85EBCA6Bu) : 0u;
            v->phb[u] = v->ph[u];
        }
        v->sph = 0;
        v->ic1[0] = v->ic1[1] = v->ic2[0] = v->ic2[1] = 0.0f;
        v->jc1[0] = v->jc1[1] = v->jc2[0] = v->jc2[1] = 0.0f;
        v->hold[0] = v->hold[1] = 0.0f, v->hph = 0.0f, v->dx[0] = v->dx[1] = 0.0f, v->sblep = 0.0f;
        v->dcx[0] = v->dcx[1] = v->dcy[0] = v->dcy[1] = 0.0f;
        v->volp = 0.0f, v->posp = -1.0f, v->bposp = -1.0f, v->f1p = -1.0f, v->glp = -1.0f;
        for (l = 0; l < DN_WT_NLFO; l++) {          /* (RETRIG: from the start) */
            v->lph[l] = 0;
            v->lsh[l] = v->lsh0[l] = wt_rand(&s->rng);
        }
        v->rnd = wt_rand(&s->rng);
        for (u = 0; u < DN_WT_COMB; u++)
            v->comb[0][u] = v->comb[1][u] = 0;
    } else {
        v->amp.st = v->amp.st > 1u ? 1u : v->amp.st;   /* (legato: the envelopes go on) */
        v->mod.st = v->mod.st > 1u ? 1u : v->mod.st;
        v->e3.st = v->e3.st > 1u ? 1u : v->e3.st;
    }
    v->live = 1;
}
/* the voices a note can take: VOICES, as many as the FM-1 can play beside the rest (measured: a voice's filter and
 * mix cost about what an oscillator does; unison and OSC B add oscillators, the drives and LP24 a little more). The
 * budget is 0.32's heaviest (6 voices, or 2 voices x 4 in unison): UNISON 2 plays 4 voices, OSC B on 3 */
#define WT_BUDGET 156u
/* the oscillators in unison: UNISON, but with OSC B on 2 at most (B on is two oscillators each) */
static uint32_t wt_nu(const int16_t *p)
{
    uint32_t nu = p[WP_UNI] < 1 ? 1u : p[WP_UNI] > (int16_t)DN_WT_NU ? DN_WT_NU : (uint32_t)p[WP_UNI];
    return p[WP_OSCB] && nu > 2u ? 2u : nu;
}
uint32_t dn_wt_cost(const int16_t *p)              /* one voice's cost (units of ~2 % of FM's 6 voices; the budget 156) */
{
    static const uint8_t DC[WD_N] = {13, 4, 14, 13, 14, 3, 3};   /* (SOFT HARD FOLD RECT TUBE BITS RATE) */
    uint32_t nu = wt_nu(p), post = 11u, k, st = nu > 1u && p[WP_SPREAD] > 0;
    for (k = 0; k < DN_WT_NMOD; k++)               /* (PAN in the matrix: the voice in stereo too) */
        st |= p[WP_M0 + 3u * k] > 0 && p[WP_M0 + 3u * k + 1u] == WT_PAN;
    if (p[WP_DRIVE] > 0)
        post += DC[p[WP_DTYPE] >= 0 && p[WP_DTYPE] < WD_N ? p[WP_DTYPE] : 0];
    if (p[WP_FTYPE] == WF_LP24)
        post += 7u;
    if (p[WP_SAT] > 0)
        post += 5u;
    if (st)                                         /* (each side its own drive and filter) */
        post = post * 17u / 10u;
    return post + nu * (15u + (p[WP_OSCB] ? (p[WP_BMODE] == WB_FM ? 24u : 10u) : 0u));
}
uint32_t dn_wt_voices(const dn_wt_t *s)
{
    uint32_t nv = s->p[WP_VOICES] < 1 ? 1u : (uint32_t)s->p[WP_VOICES], most = WT_BUDGET / dn_wt_cost(s->p);
    if (nv > DN_WT_NV)
        nv = DN_WT_NV;
    if (nv > most)
        nv = most;
    return nv ? nv : 1u;
}
static void wt_release(dn_wt_v_t *v)
{
    v->gate = 0;
    v->amp.st = 2, v->mod.st = v->mod.st < 3u ? 2u : 3u, v->e3.st = v->e3.st < 3u ? 2u : 3u;
}
void dn_wt_note_on(dn_wt_t *s, uint8_t note, uint8_t vel)
{
    uint32_t k, best = 0, nv = s->p[WP_VOICES] <= 1 ? 1u : dn_wt_voices(s);
    if (nv == 1u) {                                 /* mono: the one voice, legato under a held key */
        wt_start(s, &s->v[0], note, vel, s->nheld > 0u && s->v[0].gate);
        s->nheld++;
        return;
    }
    for (k = nv; k < DN_WT_NV; k++)                 /* (fewer now than before: those over the count let go) */
        if (s->v[k].live && s->v[k].gate)
            wt_release(&s->v[k]);
    for (k = 0; k < nv; k++)                        /* a free one, else the one released longest, else the oldest */
        if (!s->v[k].live) {
            best = k;
            goto found;
        }
    for (k = 0, best = 0; k < nv; k++)
        if ((!s->v[k].gate && (s->v[best].gate || s->v[k].seq < s->v[best].seq)) ||
            (s->v[k].gate && s->v[best].gate && s->v[k].seq < s->v[best].seq))
            best = k;
found:
    wt_start(s, &s->v[best], note, vel, 0);
    s->nheld++;
}
void dn_wt_note_off(dn_wt_t *s, uint8_t note)
{
    uint32_t k;
    if (s->nheld)
        s->nheld--;
    for (k = 0; k < DN_WT_NV; k++)
        if (s->v[k].live && s->v[k].gate && s->v[k].note == note) {
            if (k == 0u && s->p[WP_VOICES] <= 1 && s->nheld)
                continue;                           /* (mono: another key still held) */
            wt_release(&s->v[k]);
        }
}
void dn_wt_all_off(dn_wt_t *s)
{
    uint32_t k;
    s->nheld = 0;
    for (k = 0; k < DN_WT_NV; k++)
        if (s->v[k].live)
            wt_release(&s->v[k]);
}
void dn_wt_cut(dn_wt_t *s)
{
    uint32_t k;
    s->nheld = 0;
    for (k = 0; k < DN_WT_NV; k++)
        s->v[k].live = 0, s->v[k].gate = 0;
}
uint32_t dn_wt_sounding(const dn_wt_t *s)
{
    uint32_t k, n = 0;
    for (k = 0; k < DN_WT_NV; k++)
        n += s->v[k].live;
    return n;
}

static void wt_env(dn_wt_env_t *e, int32_t a, int32_t d, int32_t sus, int32_t r)
{
    float s = (float)sus * (1.0f / 127.0f);
    if (e->st == 0u) {                              /* (towards a bit over the top, so it gets there) */
        e->lv += (1.25f - e->lv) * (a ? wt_k(a) : 1.0f);
        if (e->lv >= 1.0f)
            e->lv = 1.0f, e->st = 1;
    } else if (e->st == 1u) {
        e->lv += (s - e->lv) * wt_k(d);
    } else if (e->st == 2u) {
        e->lv -= e->lv * wt_k(r);
        if (e->lv < 0.0003f)
            e->lv = 0.0f, e->st = 3;
    }
}

/* the phase, warped (w: the mode; a: its amount, 0..127, turned into a1 / a2 at control rate) */
static inline uint32_t wt_warp(uint32_t q, uint32_t w, uint32_t a1, uint32_t a2, uint32_t bp)
{
    switch (w) {
    case WW_SYNC:                                   /* (the cycle read 1x .. 8x, restarting with the note's) */
        return (uint32_t)(((uint64_t)q * a1) >> 8);
    case WW_BEND:                                   /* (the middle of the cycle moved: one half squeezed) */
    case WW_PWM:
        if (q < bp)
            return (uint32_t)(((uint64_t)q * a1) >> 16);   /* a1 = 2^31 / bp, Q16 */
        return w == WW_PWM ? 0u : 0x80000000u + (uint32_t)(((uint64_t)(q - bp) * a2) >> 16);
    case WW_MIRROR:                                 /* (forward, then back) */
        return q < 0x80000000u ? q << 1 : (~q) << 1;
    case WW_FM:                                     /* (a sine at the note, its depth up to half a cycle) */
        return q + (uint32_t)(wt_sine(q) * (int32_t)a1);
    case WW_CRUSH:                                  /* (fewer places in the cycle) */
        return q & a1;
    default:
        return q;
    }
}
/* a warp's constants for amount a (0..127): a1, a2, the break point; and how much faster than the note it reads
 * the table at most (the version read must allow for it, or it aliases) */
typedef struct { uint32_t w, a1, a2, bp; float sp; } wt_wp_t;
static void wt_warp_set(wt_wp_t *x, uint32_t w, int32_t a)
{
    a = a < 0 ? 0 : a > 127 ? 127 : a;
    x->w = w, x->a1 = 0, x->a2 = 0, x->bp = 0x80000000u, x->sp = 1.0f;
    switch (w) {
    case WW_SYNC:
        x->a1 = 256u + (uint32_t)a * 1792u / 127u;
        x->sp = (float)x->a1 * (1.0f / 256.0f);
        break;
    case WW_BEND:
    case WW_PWM: {
        uint32_t b = w == WW_PWM ? 0x80000000u - (uint32_t)a * 0x0F80000u   /* (50 % .. 3 %) */
                                 : 0x80000000u + (uint32_t)(a - 64) * 0x00E00000u;
        float fx = 2147483648.0f / (float)b, fy = 2147483648.0f / (float)(0u - b);
        x->bp = b;                                  /* (the scales as Q16, by a float divide: no 64-bit one) */
        x->a1 = (uint32_t)((w == WW_PWM ? 4294967296.0f : 2147483648.0f) / (float)b * 65536.0f);
        x->a2 = (uint32_t)(2147483648.0f / (float)(0u - b) * 65536.0f);
        x->sp = w == WW_PWM ? 2.0f * fx : fx > fy ? fx : fy;
        break;
    }
    case WW_MIRROR: x->sp = 2.0f; break;
    case WW_FM:
        x->a1 = (uint32_t)a * 516u;                 /* (Q15 sine x this: up to half a cycle) */
        x->sp = 1.0f + 6.2831853f * 32767.0f * (float)x->a1 * (1.0f / 4294967296.0f);
        break;
    case WW_CRUSH: x->a1 = ~((1u << (24u + (uint32_t)a * 6u / 127u)) - 1u); break;
    default: break;
    }
}
static uint32_t wt_mip(uint32_t inc, float sp)     /* the version with no harmonic past Nyquist at this speed */
{
    float f = (float)inc * sp;
    uint32_t allow = f >= 1.0f ? (uint32_t)(2147483648.0f / f) : 128u, m = 0;
    while (m < 6u && (128u >> m) - 1u > allow)
        m++;
    return m;
}

/* a frame pair's version at phase q, read 4-point (Hermite), mixed tf of the way from a to b (the points mixed
 * first: one curve, not two) */
static inline float wt_rd(const int16_t *pa, const int16_t *pb, float tf, uint32_t sh, uint32_t msk, uint32_t q)
{
    uint32_t j = q >> sh, j0 = (j - 1u) & msk, j2 = (j + 1u) & msk, j3 = (j + 2u) & msk;
    float x = (float)(q & ((1u << sh) - 1u)) * (1.0f / (float)(1u << sh));
    float x0 = pa[j0], x1 = pa[j], x2 = pa[j2], x3 = pa[j3], c1, c2, c3;
    if (pb != pa)
        x0 += ((float)pb[j0] - x0) * tf, x1 += ((float)pb[j] - x1) * tf, x2 += ((float)pb[j2] - x2) * tf,
        x3 += ((float)pb[j3] - x3) * tf;
    c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * x + c2) * x + c1) * x + x1;
}
/* (0.32.1) a step of height j in the signal at phase e, smoothed (PolyBLEP: the two samples either side of it): what
 * to add to the sample at phase ph (inv: 1 / the phase step) */
static inline float wt_blep(uint32_t ph, uint32_t e, uint32_t inc, float inv, float j)
{
    uint32_t d = ph - e;
    if (d < inc) {
        float t = (float)d * inv;
        return j * 0.5f * (t + t - t * t - 1.0f);
    }
    d = e - ph;
    if (d && d <= inc) {
        float t = -(float)d * inv;
        return j * 0.5f * (t * t + t + t + 1.0f);
    }
    return 0.0f;
}

/* ---- control (every 32 samples, each voice) ---------------------------------------------------------------- */
typedef struct {
    float pos, bpos, cut, res, drive, lvl, pan, sub, noise, bmod, blevel;
    wt_wp_t wa, wb;
    float gl[DN_WT_NU], gr[DN_WT_NU];
} wt_ctl_t;
static uint32_t wt_lfo_inc(const dn_wt_t *s, uint32_t l, float rmod)
{
    static const uint8_t RATE[DN_WT_NLFO] = {WP_LRATE, WP_L2RATE, WP_L3RATE};
    int32_t r = s->p[RATE[l]] + (int32_t)rmod;
    r = r < 0 ? 0 : r > 127 ? 127 : r;
    if (s->p[wt_sync_of(l)]) {                      /* (a length at the tempo: the cycles a tick, x 2^32) */
        float bps = (float)(s->bpm ? s->bpm : 120u) * (1.0f / 60.0f);
        return (uint32_t)(bps / WT_DIVB[r * (int32_t)DN_WT_NDIV / 128] * (32.0f / 44100.0f) * 4294967296.0f);
    }
    return DN_LFO_INC[r];
}
static float wt_lfo_val(uint32_t shape, uint32_t ph, float sh, float sh0)
{
    float x = (float)ph * (1.0f / 4294967296.0f);
    switch (shape) {
    case WL_TRI: return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
    case WL_SAW: return 2.0f * x - 1.0f;
    case WL_SQUARE: return x < 0.5f ? 1.0f : -1.0f;
    case WL_SH: return sh;
    case WL_RANDOM: return sh0 + (sh - sh0) * x * x * (3.0f - 2.0f * x);
    default: return (float)wt_sine(ph) * (1.0f / 32768.0f);
    }
}
static void wt_lfo_step(uint32_t *ph, uint32_t inc, float *sh, float *sh0, uint32_t *rng)
{
    uint32_t o = *ph;
    *ph += inc;
    if (*ph < o)                                    /* (a new cycle: a new random value) */
        *sh0 = *sh, *sh = wt_rand(rng);
}
static void wt_tick(dn_wt_t *s, dn_wt_v_t *v, wt_ctl_t *c)
{
    const int16_t *p = s->p;
    static const uint8_t SHAPE[DN_WT_NLFO] = {WP_L1SHAPE, WP_L2SHAPE, WP_L3SHAPE};
    static const uint8_t MODE[DN_WT_NLFO] = {WP_L1MODE, WP_L2MODE, WP_L3MODE};
    uint32_t u, l, nu = wt_nu(p);
    float src[WS_N], d[WT_N], det, spr, pa, pb, ci, fmd;
    wt_env(&v->amp, p[WP_ATK], p[WP_DEC], p[WP_SUS], p[WP_REL]);
    wt_env(&v->mod, p[WP_MATK], p[WP_MDEC], p[WP_MSUS], p[WP_MREL]);
    wt_env(&v->e3, p[WP_E3ATK], p[WP_E3DEC], p[WP_E3SUS], p[WP_E3REL]);
    if (v->amp.st == 3u) {
        v->live = 0;
        return;
    }
    if (p[WP_GLIDE])
        v->pitch += (v->target - v->pitch) * wt_k(p[WP_GLIDE]);
    else
        v->pitch = v->target;
    /* the matrix's sources: the LFOs first (their rates from last tick's matrix) */
    for (l = 0; l < WT_N; l++)
        d[l] = 0.0f;
    src[WS_OFF] = 0.0f;
    for (l = 0; l < DN_WT_NLFO; l++) {
        if (p[MODE[l]]) {                           /* RETRIG: the voice's own */
            wt_lfo_step(&v->lph[l], wt_lfo_inc(s, l, l < 2u ? v->lrm[l] : 0.0f), &v->lsh[l], &v->lsh0[l], &s->rng);
            src[WS_LFO1 + l] = wt_lfo_val((uint32_t)p[SHAPE[l]], v->lph[l], v->lsh[l], v->lsh0[l]);
        } else {                                    /* FREE: all the voices' one */
            src[WS_LFO1 + l] = wt_lfo_val((uint32_t)p[SHAPE[l]], s->lph[l], s->lsh[l], s->lsh0[l]);
        }
    }
    src[WS_MENV] = v->mod.lv, src[WS_ENV3] = v->e3.lv, src[WS_AENV] = v->amp.lv;
    src[WS_VEL] = (float)v->vel * (1.0f / 127.0f);
    src[WS_KEY] = ((float)v->note - 60.0f) * (1.0f / 60.0f);
    src[WS_RAND] = v->rnd;
    for (l = 0; l < DN_WT_NMOD; l++) {
        int32_t sr = p[WP_M0 + 3u * l], ds = p[WP_M0 + 3u * l + 1u];
        if (sr > 0 && sr < WS_N && ds > 0 && ds < WT_N)
            d[ds] += src[sr] * (float)p[WP_M0 + 3u * l + 2u] * (1.0f / 64.0f);
    }
    /* what they move (each destination's reach at full depth) */
    pa = (float)p[WP_POS] * (1.0f / 127.0f) + (float)p[WP_PENV] * (1.0f / 64.0f) * v->mod.lv +
         (float)p[WP_PLFO] * (1.0f / 127.0f) * src[WS_LFO1] + d[WT_POSA];
    c->pos = pa < 0.0f ? 0.0f : pa > 1.0f ? 1.0f : pa;
    pb = (float)p[WP_BPOS] * (1.0f / 127.0f) + d[WT_POSB];
    c->bpos = pb < 0.0f ? 0.0f : pb > 1.0f ? 1.0f : pb;
    ci = (float)p[WP_CUT] + (float)p[WP_FENV] * 2.0f * v->mod.lv + (float)p[WP_CLFO] * 0.5f * src[WS_LFO1] +
         ((float)v->note - 60.0f) * (float)p[WP_FKEY] * (1.168f / 127.0f) + d[WT_CUT] * 127.0f;
    c->cut = ci < 0.0f ? 0.0f : ci > 127.0f ? 127.0f : ci;
    c->res = (float)p[WP_RES] + d[WT_RES] * 127.0f;
    c->res = c->res < 0.0f ? 0.0f : c->res > 127.0f ? 127.0f : c->res;
    c->drive = (float)p[WP_DRIVE] + d[WT_DRIVE] * 127.0f;
    c->drive = c->drive < 0.0f ? 0.0f : c->drive > 127.0f ? 127.0f : c->drive;
    c->lvl = 1.0f + d[WT_LEVEL];
    c->lvl = c->lvl < 0.0f ? 0.0f : c->lvl > 2.0f ? 2.0f : c->lvl;
    c->pan = d[WT_PAN] < -1.0f ? -1.0f : d[WT_PAN] > 1.0f ? 1.0f : d[WT_PAN];
    c->sub = ((float)p[WP_SUB] + d[WT_SUB] * 127.0f) * (1.0f / 127.0f);
    c->sub = c->sub < 0.0f ? 0.0f : c->sub > 1.0f ? 1.0f : c->sub;
    c->noise = ((float)p[WP_NOISE] + d[WT_NOISE] * 127.0f) * (1.0f / 127.0f);
    c->noise = c->noise < 0.0f ? 0.0f : c->noise > 1.0f ? 1.0f : c->noise;
    c->bmod = ((float)p[WP_BAMT] + d[WT_BMOD] * 127.0f) * (1.0f / 127.0f);
    c->bmod = c->bmod < 0.0f ? 0.0f : c->bmod > 1.0f ? 1.0f : c->bmod;
    c->blevel = ((float)p[WP_BLEVEL] + d[WT_BLEVEL] * 127.0f) * (1.0f / 127.0f);
    c->blevel = c->blevel < 0.0f ? 0.0f : c->blevel > 1.0f ? 1.0f : c->blevel;
    wt_warp_set(&c->wa, (uint32_t)p[WP_WARP], p[WP_WAMT] + (int32_t)(d[WT_WARPA] * 127.0f));
    wt_warp_set(&c->wb, (uint32_t)p[WP_BWARP], p[WP_BWAMT] + (int32_t)(d[WT_WARPB] * 127.0f));
    v->lrm[0] = d[WT_L1RATE] * 64.0f, v->lrm[1] = d[WT_L2RATE] * 64.0f;   /* (the RETRIG LFOs' rates, next tick) */
    /* each oscillator's pitch and the versions that can't alias at it */
    det = ((float)p[WP_DET] + d[WT_DETUNE] * 127.0f) * (0.5f / 127.0f);
    det = det < 0.0f ? 0.0f : det;
    spr = (float)p[WP_SPREAD] * (1.0f / 127.0f);
    {
        int b = p[WP_OSCB] != 0;
        float pb_ = v->pitch + (float)(p[WP_BOCT] * 12 + p[WP_BSEMI]) + (float)p[WP_BFINE] * 0.01f + d[WT_PITCHB] * 24.0f;
        float pa_ = v->pitch + d[WT_PITCH] * 24.0f;
        if (b && p[WP_BMODE] == WB_SYNC)           /* SYNC: A is B's pitch, MOD AMT higher (up to 4 octaves) */
            pa_ = pb_ + c->bmod * 48.0f + d[WT_PITCH] * 24.0f;
        else if (b)
            pb_ += d[WT_PITCH] * 24.0f;
        fmd = b && p[WP_BMODE] == WB_FM ? c->bmod * c->bmod * 3.0f : 0.0f;   /* (FM: the depth, cycles) */
        for (u = 0; u < nu; u++) {
            float o = nu > 1u ? det * (2.0f * (float)u / (float)(nu - 1u) - 1.0f) : 0.0f;
            float pan = nu > 1u ? spr * (2.0f * (float)u / (float)(nu - 1u) - 1.0f) : 0.0f;   /* -1 L .. +1 R */
            uint32_t inc = wt_inc(pa_ + o), incb = b ? wt_inc(pb_ + o) : 0u;
            float sp = c->wa.sp;
            if (fmd > 0.0f && inc)                  /* (FM: A read faster by B's sweep) */
                sp *= 1.0f + 6.2831853f * fmd * (float)incb / (float)inc;
            v->inc[u] = inc, v->mip[u] = (uint8_t)wt_mip(inc, sp);
            v->incb[u] = incb, v->mipb[u] = (uint8_t)wt_mip(incb, c->wb.sp);
            c->gl[u] = (1.0f - pan) * 0.5f, c->gr[u] = (1.0f + pan) * 0.5f;
        }
    }
    v->sinc = wt_inc(v->pitch - 12.0f);
}

/* ---- the drive, the filter, the saturation (a sample) ------------------------------------------------------ */
static inline float wt_shape(uint32_t t, float x, float g)
{
    switch (t) {
    case WD_HARD: x *= g; return x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
    case WD_FOLD: {                                 /* (a triangle fold: past +-1 it turns back) */
        float q = (x * g + 1.0f) * 0.25f;
        q -= (float)(int32_t)(q < 0.0f ? q - 1.0f : q);
        return 1.0f - 4.0f * (q > 0.5f ? q - 0.5f : 0.5f - q);
    }
    case WD_RECT:                                   /* (full-wave: the octave up, then driven; its DC taken out after) */
        return wt_clip(g * (2.0f * (x < 0.0f ? -x : x) - 0.6f));
    case WD_TUBE: {                                 /* (lopsided: even harmonics) */
        float b = (g - 1.0f) * 0.03f;
        return wt_clip(x * g + b) - wt_clip(b);
    }
    default: return wt_clip(x * g);
    }
}

/* HARD, FOLD by their integrals (first-order antiderivative anti-aliasing): F(u) the integral of the curve; the
 * output (F(u) - F(u0)) / (u - u0), or the curve half way when they're close. *u0: the last input (kept) */
static inline float wt_iclip(float u) { return u > 1.0f ? u - 0.5f : u < -1.0f ? -u - 0.5f : 0.5f * u * u; }
static inline float wt_ifold(float u)               /* the integral of the triangle fold (period 4, 0 at 0) */
{
    float q = (u + 1.0f) * 0.25f, t;
    t = q - (float)(int32_t)(q < 0.0f ? q - 1.0f : q);
    return 4.0f * (t <= 0.5f ? 2.0f * t * t - t : 3.0f * t - 2.0f * t * t - 1.0f);
}
static inline float wt_fold(float u)
{
    float q = (u + 1.0f) * 0.25f;
    q -= (float)(int32_t)(q < 0.0f ? q - 1.0f : q);
    return 1.0f - 4.0f * (q > 0.5f ? q - 0.5f : 0.5f - q);
}
static inline float wt_adaa(uint32_t t, float *u0, float u, float m)
{
    float p = *u0, d = u - p, y;
    *u0 = u;
    if (d > -1e-4f && d < 1e-4f) {                  /* (close: the curve itself, half way) */
        float h = 0.5f * (u + p), c = h > 1.0f ? 1.0f : h < -1.0f ? -1.0f : h;
        (void)m;
        return t == WD_FOLD ? wt_fold(h) : c;
    }
    y = t == WD_FOLD ? (wt_ifold(u) - wt_ifold(p)) / d : (wt_iclip(u) - wt_iclip(p)) / d;
    return y;
}

void dn_wt_render(dn_wt_t *s, int32_t *lr, uint32_t n)
{
    uint32_t i0 = 0, l;
    const uint8_t *t = s->tab, *tb = s->tabb ? s->tabb : s->tab;
    uint32_t nf = dn_wt_frames(t), nfb = dn_wt_frames(tb);
    while (i0 < n) {
        uint32_t len = n - i0 < WT_TICK ? n - i0 : WT_TICK, k, busy = 0;
        float rl = 1.0f / (float)len;
        for (l = 0; l < DN_WT_NLFO; l++)            /* the FREE LFOs (all the voices') */
            wt_lfo_step(&s->lph[l], wt_lfo_inc(s, l, 0.0f), &s->lsh[l], &s->lsh0[l], &s->rng);
        for (k = 0; k < DN_WT_NV; k++) {
            dn_wt_v_t *v = &s->v[k];
            wt_ctl_t c;
            float a1f, a2f, a3f, vol, f1, f2, f3, d1, d2, d3, vc, dv, kr, gk, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
            float tf0, tf1, btf0, btf1, glc, grc, dgl, dgr;
            uint32_t u, i, nu, fa, fb, bfa = 0, bfb = 0, ft = (uint32_t)s->p[WP_FTYPE], dt = (uint32_t)s->p[WP_DTYPE];
            int ob = s->p[WP_OSCB] != 0, bm = (int)s->p[WP_BMODE];
            const uint8_t *fra, *frb, *bra = 0, *brb = 0;
            if (!v->live || !t || !nf)
                continue;
            wt_tick(s, v, &c);
            if (!v->live)
                continue;
            busy++;
            nu = wt_nu(s->p);
            {   /* the frames either side of where each position was at the tick's start, ramped to where it is now */
                float fp;
                if (v->posp < 0.0f) v->posp = c.pos;
                if (v->bposp < 0.0f) v->bposp = c.bpos;
                fp = v->posp * (float)(nf - 1u);
                fa = (uint32_t)fp;
                if (fa >= nf - 1u) fa = nf > 1u ? nf - 2u : 0u;
                fb = nf > 1u ? fa + 1u : fa;
                tf0 = fp - (float)fa, tf1 = c.pos * (float)(nf - 1u) - (float)fa;
                tf0 = tf0 < 0.0f ? 0.0f : tf0 > 1.0f ? 1.0f : tf0;
                tf1 = tf1 < 0.0f ? 0.0f : tf1 > 1.0f ? 1.0f : tf1;
                v->posp = c.pos;
                fra = t + DN_WT_HDR + fa * DN_WT_FRAME_BYTES, frb = t + DN_WT_HDR + fb * DN_WT_FRAME_BYTES;
                fp = v->bposp * (float)(nfb - 1u);
                bfa = (uint32_t)fp;
                if (bfa >= nfb - 1u) bfa = nfb > 1u ? nfb - 2u : 0u;
                bfb = nfb > 1u ? bfa + 1u : bfa;
                btf0 = fp - (float)bfa, btf1 = c.bpos * (float)(nfb - 1u) - (float)bfa;
                btf0 = btf0 < 0.0f ? 0.0f : btf0 > 1.0f ? 1.0f : btf0;
                btf1 = btf1 < 0.0f ? 0.0f : btf1 > 1.0f ? 1.0f : btf1;
                v->bposp = c.bpos;
                bra = tb + DN_WT_HDR + bfa * DN_WT_FRAME_BYTES, brb = tb + DN_WT_HDR + bfb * DN_WT_FRAME_BYTES;
            }
            {   /* the filter's coefficients (the cutoff between the table's steps), ramped from last tick's */
                uint32_t ix = (uint32_t)c.cut;
                float fr = c.cut - (float)ix, g;
                if (ix >= 127u) ix = 126u, fr = 1.0f;
                g = ((float)DN_SVF_G[ix] + ((float)DN_SVF_G[ix + 1u] - (float)DN_SVF_G[ix]) * fr) * (1.0f / 4096.0f);
                kr = 2.0f - 1.94f * c.res * (1.0f / 127.0f);
                a1f = 1.0f / (1.0f + g * (g + kr)), a2f = g * a1f, a3f = g * a2f;
                if (ft == WF_LP24) {                /* (the first stage flat, Butterworth: the resonance the second's) */
                    b1 = 1.0f / (1.0f + g * (g + 1.4142f)), b2 = g * b1, b3 = g * b2;
                }
                gk = kr;
            }
            if (v->f1p < 0.0f)
                v->f1p = a1f, v->f2p = a2f, v->f3p = a3f;
            f1 = v->f1p, f2 = v->f2p, f3 = v->f3p;
            d1 = (a1f - f1) * rl, d2 = (a2f - f2) * rl, d3 = (a3f - f3) * rl;
            v->f1p = a1f, v->f2p = a2f, v->f3p = a3f;
            vol = v->amp.lv * (float)v->vel * (1.0f / 127.0f) * (float)s->p[WP_LEVEL] * (1.0f / 127.0f) * c.lvl;
            vc = v->volp, dv = (vol - vc) * rl;
            v->volp = vol;
            {
                float pl = c.pan < 0.0f ? 1.0f : 1.0f - c.pan, pr = c.pan > 0.0f ? 1.0f : 1.0f + c.pan;
                if (v->glp < 0.0f) v->glp = pl, v->grp = pr;
                glc = v->glp, grc = v->grp, dgl = (pl - glc) * rl, dgr = (pr - grc) * rl;
                v->glp = pl, v->grp = pr;
            }
            {
                float bl[WT_TICK], br[WT_TICK], bb[WT_TICK], dtf = (tf1 - tf0) * rl, dbtf = (btf1 - btf0) * rl;
                uint32_t bwr[WT_TICK];              /* (B's cycle began here: how far past, x 2^32 / inc; 0 none) */
                int stereo = (nu > 1u && s->p[WP_SPREAD] > 0) || c.pan != 0.0f || v->glp != v->grp;
                float osc = (1.0f / 32768.0f) / (float)nu * (ob && bm != WB_MIX ? 1.0f : 1.1f);
                float g = 1.0f + c.drive * (15.0f / 127.0f), dg = 1.0f / (0.7f + 0.3f * fm_sqrtf(g));
                float sat = (float)s->p[WP_SAT] * (4.0f / 127.0f), sg = 1.0f / (0.8f + 0.2f * (1.0f + sat));
                float fmd = c.bmod * c.bmod * 3.0f;     /* (FM: B's +-1 moves A's phase this many cycles) */
                float fb = c.res * (0.97f / 127.0f), fr = 0.0f;
                uint32_t di = 2u;
                if (ft == WF_COMB) {                /* (the comb's delay: the cutoff as a pitch, 30 Hz .. 16 kHz) */
                    float dly = 44100.0f / (30.0f * wt_exp2(c.cut * (9.0589f / 127.0f)));
                    dly = dly < 2.0f ? 2.0f : dly > (float)(DN_WT_COMB - 2u) ? (float)(DN_WT_COMB - 2u) : dly;
                    di = (uint32_t)dly, fr = dly - (float)di;
                }
                for (i = 0; i < len; i++)
                    bl[i] = br[i] = 0.0f;
                for (u = 0; u < nu; u++) {          /* each oscillator over the tick: B first (A may need it) */
                    uint32_t m = v->mip[u], sh = 23u + m, msk = (512u >> m) - 1u, ph = v->ph[u], inc = v->inc[u];
                    const int16_t *pa = (const int16_t *)(const void *)(fra + 2u * WT_MIPOFF[m]);
                    const int16_t *pb = (const int16_t *)(const void *)(frb + 2u * WT_MIPOFF[m]);
                    float cl = c.gl[u] * osc, cr = c.gr[u] * osc, tf = tf0, inv = inc ? 1.0f / (float)inc : 0.0f;
                    float j0 = 0.0f, j1 = 0.0f, bl_ = c.blevel, rm = c.bmod;
                    uint32_t w = c.wa.w, a1 = c.wa.a1, a2 = c.wa.a2, bp = c.wa.bp;
                    if (ob) {
                        uint32_t mb = v->mipb[u], shb = 23u + mb, mskb = (512u >> mb) - 1u, phb = v->phb[u], incb = v->incb[u];
                        const int16_t *qa = (const int16_t *)(const void *)(bra + 2u * WT_MIPOFF[mb]);
                        const int16_t *qb = (const int16_t *)(const void *)(brb + 2u * WT_MIPOFF[mb]);
                        float btf = btf0, invb = incb ? 1.0f / (float)incb : 0.0f, k0 = 0.0f, k1 = 0.0f;
                        uint32_t wb = c.wb.w;
                        if (wb == WW_SYNC)
                            k0 = wt_rd(qa, qb, 0.5f * (btf0 + btf1), shb, mskb, 0u) -
                                 wt_rd(qa, qb, 0.5f * (btf0 + btf1), shb, mskb, (c.wb.a1 & 255u) << 24);
                        else if (wb == WW_PWM)
                            k0 = wt_rd(qa, qb, 0.5f * (btf0 + btf1), shb, mskb, 0u), k1 = -k0;
                        for (i = 0; i < len; i++, btf += dbtf) {
                            uint32_t q = wb ? wt_warp(phb, wb, c.wb.a1, c.wb.a2, c.wb.bp) : phb;
                            float y = wt_rd(qa, qb, btf, shb, mskb, q);
                            if (wb == WW_PWM && phb >= c.wb.bp)
                                y = 0.0f;
                            else if (wb == WW_CRUSH)
                                y = (float)((int32_t)y & (int32_t)~((1 << (4 + s->p[WP_BWAMT] * 8 / 127)) - 1));
                            if (k0 != 0.0f) {
                                y += wt_blep(phb, 0u, incb, invb, k0);
                                if (k1 != 0.0f)
                                    y += wt_blep(phb, c.wb.bp, incb, invb, k1);
                            }
                            bb[i] = y * (1.0f / 32768.0f);
                            bwr[i] = phb < incb ? phb | 1u : 0u;   /* (the cycle began between the last sample and this) */
                            phb += incb;
                        }
                        v->phb[u] = phb;
                    }
                    if (w == WW_SYNC) {
                        float tm = 0.5f * (tf0 + tf1);
                        j0 = wt_rd(pa, pb, tm, sh, msk, 0u) - wt_rd(pa, pb, tm, sh, msk, (a1 & 255u) << 24);
                    } else if (w == WW_PWM) {
                        j0 = wt_rd(pa, pb, 0.5f * (tf0 + tf1), sh, msk, 0u);
                        j1 = -j0;
                    }
                    for (i = 0; i < len; i++, ph += inc, tf += dtf) {
                        uint32_t q, ph2 = ph;
                        float y, sb = 0.0f;
                        if (ob && bm == WB_SYNC) {  /* SYNC: A starts again with B's cycle (its step smoothed) */
                            if (i + 1u < len && bwr[i + 1u]) {   /* (next sample: the step's first half, here) */
                                float te = 1.0f - (float)bwr[i + 1u] / (float)v->incb[u];
                                uint32_t at = ph + (uint32_t)(te * (float)inc);
                                float jj = wt_rd(pa, pb, tf, sh, msk, 0u) - wt_rd(pa, pb, tf, sh, msk, w ? wt_warp(at, w, a1, a2, bp) : at);
                                sb = jj * 0.5f * (1.0f - te) * (1.0f - te);
                                v->sblep = -jj * 0.5f * te * te;
                            }
                            if (bwr[i]) {
                                ph2 = ph = (uint32_t)((float)bwr[i] / (float)v->incb[u] * (float)inc);
                                sb += v->sblep;
                                v->sblep = 0.0f;
                            }
                        } else if (ob && bm == WB_FM) {
                            float cy = bb[i] * fmd;
                            cy -= (float)(int32_t)cy;
                            if (cy < 0.0f)
                                cy += 1.0f;
                            ph2 = ph + (uint32_t)(cy * 4294967040.0f);
                        }
                        q = w ? wt_warp(ph2, w, a1, a2, bp) : ph2;
                        y = wt_rd(pa, pb, tf, sh, msk, q);
                        if (w == WW_PWM && ph2 >= bp)
                            y = 0.0f;
                        else if (w == WW_CRUSH)
                            y = (float)((int32_t)y & (int32_t)~((1 << (4 + s->p[WP_WAMT] * 8 / 127)) - 1));
                        if (j0 != 0.0f && !(ob && bm != WB_MIX)) {
                            y += wt_blep(ph, 0u, inc, inv, j0);
                            if (j1 != 0.0f)
                                y += wt_blep(ph, bp, inc, inv, j1);
                        }
                        y += sb;
                        if (ob) {
                            float x = bb[i];
                            if (bm == WB_RING)
                                y = y * (1.0f - rm) + y * x * rm;
                            else if (bm == WB_AM)
                                y = y * (1.0f - 0.5f * rm + 0.5f * rm * x);
                            y += x * 32768.0f * bl_;
                        }
                        bl[i] += y * cl, br[i] += y * cr;
                    }
                    v->ph[u] = ph;
                }
                for (i = 0; i < len; i++, vc += dv, f1 += d1, f2 += d2, f3 += d3, glc += dgl, grc += dgr) {
                    float lx, rx, x = 0.0f;
                    if (c.sub > 0.0f)               /* the sub */
                        x = (float)wt_sine(v->sph) * (1.0f / 32768.0f) * c.sub * 0.7f;
                    v->sph += v->sinc;
                    if (c.noise > 0.0f) {
                        v->nz = v->nz * 1664525u + 1013904223u;
                        x += (float)(int32_t)v->nz * (1.0f / 2147483648.0f) * c.noise * 0.5f;
                    }
                    lx = (bl[i] + x) * vc, rx = (br[i] + x) * vc;
                    if (c.drive > 0.0f) {           /* DRIVE (the curves at twice the rate: less folding back) */
                        if (dt == WD_BITS) {
                            float q = (float)(1 << (12 - (int32_t)(c.drive * (10.0f / 127.0f))));
                            lx = (float)(int32_t)(lx * q + (lx < 0.0f ? -0.5f : 0.5f)) / q;
                            rx = (float)(int32_t)(rx * q + (rx < 0.0f ? -0.5f : 0.5f)) / q;
                        } else if (dt == WD_RATE) {
                            v->hph += 1.0f / (1.0f + c.drive * (31.0f / 127.0f));
                            if (v->hph >= 1.0f)
                                v->hph -= 1.0f, v->hold[0] = lx, v->hold[1] = rx;
                            lx = v->hold[0], rx = v->hold[1];
                        } else {
                            if (dt == WD_FOLD || dt == WD_HARD) {   /* (the sharp ones by their
                                                     * integral (ADAA): the slope between two samples, not the curve at
                                                     * each, so little folds back) */
                                float m = (g - 1.0f) * (1.0f / 15.0f), o = dt == WD_HARD ? dg : 0.8f;
                                if (dt == WD_HARD) {
                                    lx = wt_adaa(dt, &v->dx[0], lx * g, m) * o;
                                    rx = stereo ? wt_adaa(dt, &v->dx[1], rx * g, m) * o : lx;
                                } else {            /* (FOLD: at twice the rate too) */
                                    float ul = lx * g, ur = rx * g;
                                    lx = 0.5f * (wt_adaa(dt, &v->dx[0], 0.5f * (v->dx[0] + ul), m) + wt_adaa(dt, &v->dx[0], ul, m)) * o;
                                    rx = stereo ? 0.5f * (wt_adaa(dt, &v->dx[1], 0.5f * (v->dx[1] + ur), m) +
                                                          wt_adaa(dt, &v->dx[1], ur, m)) * o : lx;
                                }
                            } else {                /* (the smooth ones at twice the rate) */
                                float pl = v->dx[0], pr = v->dx[1];
                                v->dx[0] = lx, v->dx[1] = rx;
                                lx = 0.5f * (wt_shape(dt, 0.5f * (pl + lx), g) + wt_shape(dt, lx, g)) * dg;
                                rx = stereo ? 0.5f * (wt_shape(dt, 0.5f * (pr + rx), g) + wt_shape(dt, rx, g)) * dg : lx;
                            }
                            if (dt == WD_RECT || dt == WD_TUBE) {   /* (the DC they make: out, below ~30 Hz) */
                                float yl = lx - v->dcx[0] + 0.996f * v->dcy[0], yr = rx - v->dcx[1] + 0.996f * v->dcy[1];
                                v->dcx[0] = lx, v->dcy[0] = yl, v->dcx[1] = rx, v->dcy[1] = yr;
                                lx = yl, rx = stereo ? yr : yl;
                            }
                        }
                    }
                    if (ft == WF_COMB) {            /* COMB: a delay at the cutoff's pitch, RES its feedback */
                        uint32_t ch;
                        for (ch = 0; ch < (stereo ? 2u : 1u); ch++) {
                            const int16_t *cb = v->comb[ch];
                            float z0 = cb[(uint8_t)(v->cw - di)], z1 = cb[(uint8_t)(v->cw - di - 1u)];
                            float y = (ch ? rx : lx) + fb * (z0 + (z1 - z0) * fr) * (1.0f / 8192.0f), yy = y * 8192.0f;
                            v->comb[ch][v->cw] = (int16_t)(yy > 32767.0f ? 32767.0f : yy < -32768.0f ? -32768.0f : yy);
                            if (ch) rx = y * (1.0f - 0.5f * fb); else lx = y * (1.0f - 0.5f * fb);
                        }
                        v->cw++;
                        if (!stereo) rx = lx;
                    } else {                        /* the state-variable filter (TPT), each side or one for both */
                        uint32_t ch;
                        for (ch = 0; ch < (stereo ? 2u : 1u); ch++) {
                            float xi = ch ? rx : lx, y;
                            if (ft == WF_LP24) {    /* (a flat stage first) */
                                float v3 = xi - v->jc2[ch], v1 = b1 * v->jc1[ch] + b2 * v3, v2 = v->jc2[ch] + b2 * v->jc1[ch] + b3 * v3;
                                v->jc1[ch] = 2.0f * v1 - v->jc1[ch], v->jc2[ch] = 2.0f * v2 - v->jc2[ch], xi = v2;
                            }
                            {
                                float v3 = xi - v->ic2[ch], v1 = f1 * v->ic1[ch] + f2 * v3, v2 = v->ic2[ch] + f2 * v->ic1[ch] + f3 * v3;
                                v->ic1[ch] = 2.0f * v1 - v->ic1[ch], v->ic2[ch] = 2.0f * v2 - v->ic2[ch];
                                y = ft == WF_HP ? xi - gk * v1 - v2 : ft == WF_BP ? v1 * gk : ft == WF_NOTCH ? xi - gk * v1 : v2;
                            }
                            if (ch) rx = y; else lx = y;
                        }
                        if (!stereo) rx = lx;
                    }
                    if (sat > 0.0f)                 /* SAT */
                        lx = wt_clip(lx * (1.0f + sat)) * sg, rx = wt_clip(rx * (1.0f + sat)) * sg;
                    lr[2u * (i0 + i)] += (int32_t)(lx * glc * 32767.0f);
                    lr[2u * (i0 + i) + 1u] += (int32_t)(rx * grc * 32767.0f);
                }
            }
        }
        s->busy = busy;
        i0 += len;
    }
}
