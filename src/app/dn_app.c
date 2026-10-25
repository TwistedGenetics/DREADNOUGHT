/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the app. Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * A unity-build fragment, the same on the FM-1 (src/hardware/dn_main_fm1.c) and on the host (tests/host/dn_host.c).
 * Before it, the includer has: X0X's plat.h (the panel, MIDI, storage), gfx.c (the canvas, the fonts, RGB), reese.h
 * and dn_dsp.h, and two functions: dn_lock() / dn_unlock() (on the FM-1: interrupts off and on again, so the audio
 * interrupt never sees half an edit). The engine itself (src/dsp/reese.c) is a separate -O2 unit.
 *
 * Who calls what:
 *   dn_app_init()           once, at start-up (the saved sound comes back)
 *   dn_app_render(lr, n)    the audio interrupt: n frames, stereo, Q15 (it clears lr itself)
 *   dn_app_midi(st, d1, d2) the audio interrupt: a channel message from USB or the TRS jack
 *   dn_app_input()          the main loop, as often as it likes: the panel
 *   dn_app_draw(force)      the main loop, ~30-60 times a second: the screen and the lights
 *
 * The panel. On every page:
 *   PLAY                     the sequencer: start / stop (MIDI clock and start / stop go out on USB)
 *   REC                      record on / off (lit): keys write into the selected track (below)
 *   HOME                     (0.29) the HOME page: the eight tracks, a key 1-8 (or SELECT, then HOME) opens one
 *   SEQ                      the SEQ page; SEL EDIT ENV LFO GLO: the sound's pages (lit: the page shown)
 *   PRESETS encoder          the factory sounds (the edits since are dropped)
 *   ALGORITHM encoder        the tempo (the sequencer, and BEAT, which locks the reese's movement to it)
 *   OCT- / OCT+              octave down / up (-3..+3)
 *   SAVE                     keep the sound and the pattern: they come back at power-on (lit: changed since)
 * On the sound's pages (OSC FILTER AMP MOD OUT):
 *   keys F3..G5              notes, mono, last note wins; a key held under another slides to it (legato + glide).
 *                            Track 1 plays REESE; with tracks 2..8 selected the keys play MIDI on that track's channel.
 *                            With REC on: playing, each note goes into the selected track at the nearest step;
 *                            stopped, step by step from the cursor (a key held under another: a slide)
 *   SELECT encoder           the page;  KNOB 1..4: the page's four dials
 * On the SEQ page (8 tracks, the 16 steps in view):
 *   white keys               the 16 steps: a tap turns a step on / off; hold one and turn
 *                            KNOB 1 its note, KNOB 2 its length, KNOB 3 accent (right on, left off), KNOB 4 slide
 *   black keys 1..8          mute / unmute tracks 1..8 (lit: playing);  black key 9 held 1 s: clear the track
 *   black keys 10 / 11       the steps in view: 1-16, 17-32, 33-48, 49-64
 *   SELECT encoder, KNOB 1   the track;  KNOB 2 its length (1..64);  KNOB 3 swing;  KNOB 4 the steps in view
 */
#include "dn_app.h"
#include <stddef.h>
#include "dsp/fastmath.h"

/* the editor's panel (0.26): buttons, keys and encoders the web editor presses, merged with the FM-1's own (so every
 * page and every instrument works from the page as from the panel) */
static volatile uint32_t dn_vbtn, dn_vkeys;
static volatile int32_t dn_venc[NE];
static uint32_t dn_lit_btn, dn_lit_keys;            /* (what the LEDs show: the panel lights the same) */
static uint32_t dn_in_buttons(void) { return plat_buttons() | dn_vbtn; }
static uint32_t dn_in_keys(void) { return plat_keys() | dn_vkeys; }
static int32_t dn_in_enc(int role)
{
    int32_t v = plat_enc(role) + dn_venc[role];
    dn_venc[role] = 0;
    return v;
}

#define DN_SAVE_MAGIC 0x31524E44u                   /* "DNR1" */
#define DN_NPAGES 5u                                /* REESE's pages; then SEQ, then BRK */
#define DN_PG_SEQ DN_NPAGES
#define DN_PG_BRK (DN_NPAGES + 1u)
#define DN_NTABS (DN_NPAGES + 2u)
#define DN_PG_ROLL (DN_NPAGES + 2u)                 /* the sequencer's editors: their tab is SEQ's */
#define DN_PG_DRUM (DN_NPAGES + 3u)
#define DN_IS_SEQPG(p) ((p) == DN_PG_SEQ || (p) == DN_PG_ROLL || (p) == DN_PG_DRUM)
#define DN_PG_BRK2 (DN_NPAGES + 4u)                 /* BRK's second page (ARP again): its tab is BRK's */
#define DN_PG_SLICE (DN_NPAGES + 5u)                /* BRK's third page: the slice editor */
#define DN_IS_BRKPG(p) ((p) == DN_PG_BRK || (p) == DN_PG_BRK2 || (p) == DN_PG_SLICE)
#define DN_PG_SET (DN_NPAGES + 6u)                  /* OUT's second page (GLO again): click, count-in, octave */
#define DN_PG_LFO (DN_NPAGES + 7u)                  /* the LFOs (LFO again from MOD: 1, 2, 3, 4, back to MOD) */
#define DN_IS_SNDPG(p) ((p) < DN_NPAGES || (p) == DN_PG_SET || (p) == DN_PG_LFO || (p) == DN_PG_SOSC2 || (p) == DN_PG_DIRT || \
                        (p) == DN_PG_FENV)
#define DN_PG_MIDI (DN_NPAGES + 8u)                 /* the MIDI instruments' page (GLO: OUT, SETUP, MIDI) */
#define DN_PG_FX (DN_NPAGES + 9u)                   /* the FX rack: MIX (the track's sends), FX A, FX B (FX: the next) */
#define DN_PG_PROJ (DN_NPAGES + 10u)                /* the project: each track's instrument, memory, CPU (0.12) */
#define DN_PG_SOSC2 (DN_NPAGES + 11u)               /* SUB: OSC's second page (SEL again): PW, PWM, its rate, the sub */
#define DN_PG_DIRT (DN_NPAGES + 12u)                /* SUB: FILTER's second page (EDIT again): the dirt */
#define DN_PG_FENV (DN_NPAGES + 13u)                /* AMP's second page (ENV again): the filter envelope (0.19) */
#define DN_PG_HOME (DN_NPAGES + 14u)                /* (0.29) HOME: the eight tracks' instruments, one to open */
#define DN_HOME_TAP_MS 150                          /* (0.29) HOME up sooner on a sequencer page: a tap, not FILL */
#define DN_IS_GLOPG(p) ((p) == 4u || (p) == DN_PG_FX || (p) == DN_PG_SET || (p) == DN_PG_MIDI || (p) == DN_PG_PROJ)
#define DN_ROLL_ROWS 12                             /* the piano roll's pitches in view (an octave) */
#define DN_DRUM_ROWS 8                              /* the drum grid's slices in view */
#define DN_NBRK 2u                                  /* BREAKS A and B (tracks 2 and 3 unless the project moves them) */
/* ---- the project's instruments: which one each track plays (0.12). REESE and BREAKS A / B are one each: put on a
 * track that another holds, they swap (dn_eng_set). MIDI on any number of tracks; OFF plays nothing. */
enum { DN_E_OFF, DN_E_REESE, DN_E_BRKA, DN_E_BRKB, DN_E_MIDI, DN_E_SUB, DN_E_SMP, DN_E_FM, DN_E_N };
static const char *const DN_E_NAME[DN_E_N] = {"OFF", "REESE", "BRK A", "BRK B", "MIDI", "SUB", "SAMPLER", "FM"};
static uint8_t dn_eng[8] = {DN_E_REESE, DN_E_BRKA, DN_E_BRKB, DN_E_MIDI, DN_E_MIDI, DN_E_MIDI, DN_E_MIDI, DN_E_MIDI};
#define DN_IS_BRK(t) (dn_eng[t] == DN_E_BRKA || dn_eng[t] == DN_E_BRKB)
#define DN_BK(t) ((uint32_t)dn_eng[t] - DN_E_BRKA)  /* a break track's voice: 0 A, 1 B */
#define DN_IS_RS(t) (dn_eng[t] == DN_E_REESE)
#define DN_IS_MIDI(t) (dn_eng[t] == DN_E_MIDI)
#define DN_IS_SUB(t) (dn_eng[t] == DN_E_SUB)
#define DN_IS_SYN(t) (DN_IS_RS(t) || DN_IS_SUB(t))   /* a synth: REESE or SUB (their sound pages) */
#define DN_IS_SMP(t) (dn_eng[t] == DN_E_SMP)        /* SAMPLER (0.23) */
#define DN_IS_PADS(t) (DN_IS_BRK(t) || (DN_IS_SMP(t) && dn_sm.p.mode == DN_SMP_ONESHOT))   /* played as rows: DRUM */
#define DN_IS_FM(t) (dn_eng[t] == DN_E_FM)          /* FM (0.25) */
#define DN_IS_OWNPG(t) (DN_IS_SMP(t) || DN_IS_FM(t))   /* its own five pages (no LFO page) */
#define DN_IS_POLY(t) ((DN_IS_SMP(t) && dn_sm.p.mode == DN_SMP_KEYS) || DN_IS_FM(t))   /* a key a note (no slides) */
#define DN_ONE(e) ((e) == DN_E_REESE || (e) == DN_E_BRKA || (e) == DN_E_BRKB || (e) == DN_E_SUB || (e) == DN_E_SMP || \
                   (e) == DN_E_FM)
#define DN_KEY0 53                                  /* key 0 is F3 */
/* FM's values (0.25): the voice's own, the macros, the operator shown (DN_PX_FOP) and its values (DN_PX_FO0 + DN_FMO_*) */
#define DN_PX_FM0 60
enum { DN_PX_FALG = DN_PX_FM0, DN_PX_FFB, DN_PX_FBRIGHT, DN_PX_FTIME, DN_PX_FATK, DN_PX_FREL, DN_PX_FOP, DN_PX_FLEVEL,
       DN_PX_FVOICES, DN_PX_FOKS, DN_PX_FLFS, DN_PX_FLFD, DN_PX_FPMD, DN_PX_FAMD, DN_PX_FLFW, DN_PX_FLFKS, DN_PX_FPMS,
       DN_PX_FTRNSP, DN_PX_FO0 };
#define DN_IS_FPX(id) ((id) >= DN_PX_FM0 && (id) < DN_PX_FO0 + DN_FMO_N)
#define DN_PX_BPM 100                               /* the OUT page's two non-engine dials */
#define DN_PX_OCT 101
#define DN_PX_OVL 102                               /* OUT: REESE's OVERLAP (SLIDE / CUT) */
#define DN_PX_CLICK 103                             /* SETUP: the click (OFF / REC / ON), its level, the count-in */
#define DN_PX_CLKLVL 104
#define DN_PX_CIN 105
#define DN_PX_VSENS 106                             /* SETUP: REESE's velocity sensitivity (0: velocity ignored) */
#define DN_PX_MSEL 107                              /* MIDI: which MIDI instrument (tracks 4-8), its channel, OVERLAP */
#define DN_PX_MCHAN 108
#define DN_PX_MOVL 109
#define DN_PX_PTRK 150                              /* PROJECT: the track, its instrument (FX: change to it) */
#define DN_PX_PENG 151
#define DN_PX_SENDA 152                             /* FX, MIX: the selected track's sends, the two returns */
#define DN_PX_SENDB 153
#define DN_PX_RETA 154
#define DN_PX_RETB 155
#define DN_PX_FXA 156                               /* FX A: its type, then its three values (156..159) */
#define DN_PX_FXB 160                               /* FX B: the same (160..163) */
#define DN_PX_FXA2 200                              /* 0.16: FX A's next four values (its 4th..7th: 200..203) */
#define DN_PX_FXB2 204                              /* FX B's (204..207) */
#define DN_IS_PPX(id) (((id) >= DN_PX_PTRK && (id) <= DN_PX_FXB + 3u) || ((id) >= DN_PX_FXA2 && (id) <= DN_PX_FXB2 + 3u))
#define DN_PX_SUB0 170                              /* SUB's values: 170 + SP_* (0.13) */
#define DN_PX_SMODE 164                             /* SAMPLER (0.23): ONE-SHOT / KEYS, the pad and the zone shown */
#define DN_PX_SPAD 165
#define DN_PX_SZONE 166
#define DN_PX_PD0 220                               /* the pad shown's values: 220 + PD_* */
#define DN_PX_ZN0 236                               /* the zone shown's: 236 + ZN_* */
#define DN_PX_KY0 246                               /* the keys': 246 + KY_* */
#define DN_IS_MPX(id) (((id) >= DN_PX_SMODE && (id) <= DN_PX_SZONE) || ((id) >= DN_PX_PD0 && (id) < DN_PX_KY0 + KY_N))
#define DN_IS_SPX(id) ((id) >= DN_PX_SUB0 && (id) < DN_PX_SUB0 + SP_COUNT)
#define DN_S(k) (DN_PX_SUB0 + (k))
#define DN_PX_NONE 0xFFu                            /* (a knob with nothing on it) */
#define DN_PX_LSHAPE 130                            /* the LFO page (the selected LFO): shape, rate, dest, depth ... */
#define DN_PX_LRATE 131
#define DN_PX_LDEST 132
#define DN_PX_LDEPTH 133
#define DN_PX_LSYNC 134                             /* ... and (FX: MORE) sync, mode, phase, smooth */
#define DN_PX_LMODE 135
#define DN_PX_LPHASE 136
#define DN_PX_LSMOOTH 137
#define DN_PX_L1RATE 140                            /* MOD's KNOB 3 / 4: LFO 1's rate and depth (where LRATE and LFLT
                                                     * were: a 0.8 sound's filter LFO is LFO 1 now) */
#define DN_PX_L1DEPTH 141
#define DN_PX_RFE 142                               /* 0.19: REESE's filter envelope ATK DEC SUS REL (142..145) */
#define DN_PX_SFE 146                               /* SUB's (146..149) */
#define DN_IS_FEX(id) ((id) >= DN_PX_RFE && (id) < DN_PX_SFE + 4u)
#define DN_PX_BSRC 110                              /* the BRK page: the loop, pitch, decay, crush of BREAKS A / B */
#define DN_PX_BPITCH 111
#define DN_PX_BDECAY 112
#define DN_PX_BCRUSH 113
#define DN_PX_BCHOKE 114                            /* BRK2: choke group, slice mode, stretch mode, level */
#define DN_PX_BSMODE 115
#define DN_PX_BSTRETCH 116
#define DN_PX_BLEVEL 117
#define DN_PX_SLSEL 120                             /* SLICE (chop mode): the chop, the cursor (coarse, fine), zoom */
#define DN_PX_SLMOVE 121
#define DN_PX_SLFINE 122
#define DN_PX_SLADD 123

static const char *const DN_PAGE_NAME[DN_NTABS] = {"OSC", "FILTER", "AMP", "MOD", "OUT", "SEQ", "BRK"};
static const uint8_t DN_BRK_PAGE[4] = {DN_PX_BSRC, DN_PX_BPITCH, DN_PX_BDECAY, DN_PX_BCRUSH};
static const uint8_t DN_BRK2_PAGE[4] = {DN_PX_BCHOKE, DN_PX_BSMODE, DN_PX_BSTRETCH, DN_PX_BLEVEL};
static const uint8_t DN_SLICE_PAGE[4] = {DN_PX_SLSEL, DN_PX_SLMOVE, DN_PX_SLFINE, DN_PX_SLADD};
static const uint8_t DN_PAGE[DN_NPAGES][4] = {
    {RP_OSC, RP_DTN, RP_BEAT, RP_SUB},
    {RP_CUT, RP_RES, RP_DRV, RP_NTCH},
    {RP_ATK, RP_DEC, RP_SUS, RP_REL},
    {RP_FENV, RP_GLIDE, DN_PX_L1RATE, DN_PX_L1DEPTH},
    {RP_LEVEL, RP_TRANS, DN_PX_BPM, DN_PX_OVL},
};
static const uint8_t DN_PAGE_BTN[DN_NPAGES] = {B_SEL, B_EDIT, B_ENV, B_LFO, B_GLO};
/* SUB's pages in the same places (OSC FILTER AMP MOD OUT), then OSC's and FILTER's second pages */
static const uint8_t DN_SUB_PAGE[DN_NPAGES][4] = {
    {DN_S(SP_SAW), DN_S(SP_PULSE), DN_S(SP_SUB), DN_S(SP_NOISE)},
    {DN_S(SP_CUT), DN_S(SP_RES), DN_S(SP_ENV), DN_S(SP_KEY)},
    {DN_S(SP_ATK), DN_S(SP_DEC), DN_S(SP_SUS), DN_S(SP_REL)},
    {DN_S(SP_GLIDE), DN_S(SP_GMODE), DN_S(SP_ACC), DN_S(SP_VCA)},
    {DN_S(SP_LEVEL), DN_S(SP_TRANS), DN_S(SP_SPLIT), DN_PX_OVL},
};
/* SAMPLER's pages (0.23), ONE-SHOT and KEYS, in the synths' places (SEL EDIT ENV LFO GLO) */
static const uint8_t DN_SMP_PG[2][DN_NPAGES][4] = {
    {{DN_PX_PD0 + PD_SAMPLE, DN_PX_PD0 + PD_START, DN_PX_PD0 + PD_END, DN_PX_PD0 + PD_REV},
     {DN_PX_PD0 + PD_CUT, DN_PX_PD0 + PD_RES, DN_PX_PD0 + PD_DRIVE, DN_PX_PD0 + PD_CRUSH},
     {DN_PX_PD0 + PD_ATK, DN_PX_PD0 + PD_HOLD, DN_PX_PD0 + PD_DEC, DN_PX_PD0 + PD_LEVEL},
     {DN_PX_PD0 + PD_PITCH, DN_PX_PD0 + PD_FINE, DN_PX_PD0 + PD_PAN, DN_PX_PD0 + PD_CHOKE},
     {DN_PX_SMODE, DN_PX_SPAD, DN_PX_BPM, DN_PX_NONE}},
    {{DN_PX_ZN0 + ZN_SAMPLE, DN_PX_ZN0 + ZN_ROOT, DN_PX_ZN0 + ZN_LOW, DN_PX_ZN0 + ZN_HIGH},
     {DN_PX_KY0 + KY_CUT, DN_PX_KY0 + KY_RES, DN_PX_KY0 + KY_DRIVE, DN_PX_KY0 + KY_CRUSH},
     {DN_PX_KY0 + KY_ATK, DN_PX_KY0 + KY_DEC, DN_PX_KY0 + KY_SUS, DN_PX_KY0 + KY_REL},
     {DN_PX_ZN0 + ZN_LOOP, DN_PX_ZN0 + ZN_LSTART, DN_PX_ZN0 + ZN_LEND, DN_PX_ZN0 + ZN_TUNE},
     {DN_PX_SMODE, DN_PX_SZONE, DN_PX_ZN0 + ZN_LEVEL, DN_PX_KY0 + KY_VOICES}}};
static const char *const DN_SMP_TAB[2][DN_NPAGES] = {{"PAD", "FILTER", "AMP", "MOD", "OUT"},
                                                     {"ZONE", "FILTER", "AMP", "LOOP", "OUT"}};
/* FM's pages (0.25), in the synths' places; each again: its second view (OP: OP+, EG: its levels, LFO: LFO+) */
static const uint8_t DN_FM_PG[2][DN_NPAGES][4] = {
    {{DN_PX_FALG, DN_PX_FFB, DN_PX_FBRIGHT, DN_PX_FTIME},
     {DN_PX_FOP, DN_PX_FO0 + DN_FMO_COARSE, DN_PX_FO0 + DN_FMO_FINE, DN_PX_FO0 + DN_FMO_OL},
     {DN_PX_FO0 + DN_FMO_R1, DN_PX_FO0 + DN_FMO_R2, DN_PX_FO0 + DN_FMO_R3, DN_PX_FO0 + DN_FMO_R4},
     {DN_PX_FLFS, DN_PX_FLFD, DN_PX_FPMD, DN_PX_FAMD},
     {DN_PX_FLEVEL, DN_PX_FVOICES, DN_PX_FATK, DN_PX_FREL}},
    {{DN_PX_FALG, DN_PX_FFB, DN_PX_FBRIGHT, DN_PX_FTIME},
     {DN_PX_FOP, DN_PX_FO0 + DN_FMO_DET, DN_PX_FO0 + DN_FMO_KVS, DN_PX_FO0 + DN_FMO_MODE},
     {DN_PX_FO0 + DN_FMO_L1, DN_PX_FO0 + DN_FMO_L2, DN_PX_FO0 + DN_FMO_L3, DN_PX_FO0 + DN_FMO_L4},
     {DN_PX_FLFW, DN_PX_FLFKS, DN_PX_FPMS, DN_PX_FOKS},
     {DN_PX_FLEVEL, DN_PX_FVOICES, DN_PX_FATK, DN_PX_FREL}}};
static const char *const DN_FM_TAB[2][DN_NPAGES] = {{"VOICE", "OP", "EG", "LFO", "OUT"},
                                                    {"VOICE", "OP+", "EG LVL", "LFO+", "OUT"}};
static const uint8_t DN_SUB_OSC2[4] = {DN_S(SP_PW), DN_S(SP_PWM), DN_S(SP_PWMR), DN_S(SP_SUBM)};
static const uint8_t DN_SUB_DIRT[4] = {DN_S(SP_DRIVE), DN_S(SP_DTYPE), DN_S(SP_CRUSH), DN_S(SP_TONE)};
static const uint8_t DN_SET_PAGE[4] = {DN_PX_CLICK, DN_PX_CLKLVL, DN_PX_CIN, DN_PX_VSENS};
static const uint8_t DN_MIDI_PAGE[4] = {DN_PX_MSEL, DN_PX_MCHAN, DN_PX_MOVL, DN_PX_NONE};
static const uint8_t DN_PROJ_PAGE[4] = {DN_PX_PTRK, DN_PX_PENG, DN_PX_NONE, DN_PX_NONE};
/* the FX page's views (FX: the next): MIX, FX A, its next values (when it has more than 3), FX B, its next */
static const uint8_t DN_FX_PAGE[5][4] = {{DN_PX_SENDA, DN_PX_SENDB, DN_PX_RETA, DN_PX_RETB},
                                         {DN_PX_FXA, DN_PX_FXA + 1u, DN_PX_FXA + 2u, DN_PX_FXA + 3u},
                                         {DN_PX_FXA2, DN_PX_FXA2 + 1u, DN_PX_FXA2 + 2u, DN_PX_FXA2 + 3u},
                                         {DN_PX_FXB, DN_PX_FXB + 1u, DN_PX_FXB + 2u, DN_PX_FXB + 3u},
                                         {DN_PX_FXB2, DN_PX_FXB2 + 1u, DN_PX_FXB2 + 2u, DN_PX_FXB2 + 3u}};
#define DN_NSND 7u                                  /* the sound pages, in SELECT's order (the tab row's too) */
static const uint8_t DN_SND[DN_NSND] = {0, 1, 2, 3, DN_PG_LFO, 4, DN_PG_SET};
static const char *const DN_SND_NAME[DN_NSND] = {"OSC", "FILTER", "AMP", "MOD", "LFO", "OUT", "SETUP"};
static const uint8_t DN_LFO_PAGE[2][4] = {{DN_PX_LSHAPE, DN_PX_LRATE, DN_PX_LDEST, DN_PX_LDEPTH},
                                          {DN_PX_LSYNC, DN_PX_LMODE, DN_PX_LPHASE, DN_PX_LSMOOTH}};
/* where an LFO goes */
enum { DN_LD_OFF, DN_LD_CUT, DN_LD_RES, DN_LD_DRV, DN_LD_NTCH, DN_LD_DTN, DN_LD_PITCH, DN_LD_SUB, DN_LD_LEVEL,
       DN_LD_FENV, DN_LD_APITCH, DN_LD_ADECAY, DN_LD_ACRUSH, DN_LD_ALEVEL, DN_LD_BPITCH, DN_LD_BDECAY, DN_LD_BCRUSH,
       DN_LD_BLEVEL, DN_LD_L1DEPTH, DN_LD_SCUT = DN_LD_L1DEPTH + DN_NLFO, DN_LD_SRES, DN_LD_SDRV, DN_LD_SPITCH,
       DN_LD_SLEVEL, DN_LD_SPW, DN_LD_SENV, DN_LD_SSUB, DN_LD_SCRUSH, DN_LD_STONE, DN_LD_SPULSE, DN_LD_SNOISE,
       DN_LD_N };
#define DN_LD_IS_DEPTH(d) ((d) >= DN_LD_L1DEPTH && (d) < DN_LD_L1DEPTH + DN_NLFO)
/* (the first ones are REESE's; SUB's own come after the depths, 0.13: an LFO set up for REESE never moves SUB) */
static const char *const DN_LD_NAME[DN_LD_N] = {"OFF", "CUTOFF", "RES", "DRIVE", "NOTCH", "DETUNE", "PITCH", "SUB",
    "LEVEL", "F.ENV", "A PITCH", "A DECAY", "A CRUSH", "A LEVEL", "B PITCH", "B DECAY", "B CRUSH", "B LEVEL",
    "L1 DEP", "L2 DEP", "L3 DEP", "L4 DEP", "S CUT", "S RES", "S DRIVE", "S PITCH", "S LEVEL", "S PW", "S ENV",
    "S SUB", "S CRUSH", "S TONE", "S PULSE", "S NOISE"};
static const uint8_t DN_LD_SP[DN_LD_N - DN_LD_SCUT] = {SP_CUT, SP_RES, SP_DRIVE, 0, SP_LEVEL, SP_PW, SP_ENV, SP_SUB,
                                                       SP_CRUSH, SP_TONE, SP_PULSE, SP_NOISE};
/* the LFO page's DEST knob goes through the destinations of the synth whose pages these are, then the breaks', then
 * the other LFOs' depths (an LFO already on the other synth's keeps it, shown by its name) */
static uint32_t dn_ld_list(int sub, uint32_t *out)
{
    uint32_t n = 0, d;
    out[n++] = DN_LD_OFF;
    if (sub)
        for (d = DN_LD_SCUT; d < DN_LD_N; d++)
            out[n++] = d;
    else
        for (d = DN_LD_CUT; d <= DN_LD_FENV; d++)
            out[n++] = d;
    for (d = DN_LD_APITCH; d < DN_LD_L1DEPTH + DN_NLFO; d++)
        out[n++] = d;
    return n;
}
static const uint8_t DN_LD_RP[DN_LD_N] = {0, RP_CUT, RP_RES, RP_DRV, RP_NTCH, RP_DTN, 0, RP_SUB, RP_LEVEL, RP_FENV};

/* a synth's sound, kept as a user sound (0.15): its values, its envelope sync, its four LFOs (DRAW points too) */
#define DN_NUSER 8u
typedef struct {
    uint8_t shape, sync, rate, dest;
    int8_t depth;
    uint8_t phase, mode, smooth, npts;
    int8_t pts[DN_LFO_PTS];
} dn_plfo_t;
typedef struct {
    uint8_t used, esync, pad[2];
    int8_t p[28];                                   /* (REESE's 18 or SUB's 27 values) */
    dn_plfo_t lfo[DN_NLFO];
} dn_patch_t;
typedef struct {
    uint32_t magic;
    uint8_t preset, page;
    int8_t oct;
    uint8_t pad;
    uint16_t bpm, pad2;
    int16_t p[RP_COUNT];
    struct { uint8_t src; int8_t pitch; uint8_t decay, crush; } brk[DN_NBRK];   /* (from 0.5: older saves lack it) */
    struct { uint8_t choke, smode, stretch, level; } brk2[DN_NBRK];             /* (from 0.6) */
    struct { uint8_t click, clk_lvl, cin, pad; } set;                           /* (from 0.9) */
    dn_lfo_p_t lfo[DN_NLFO];                                                    /* (from 0.9) */
    struct {                                                                    /* the project (from 0.12) */
        char name[12];
        uint8_t eng[8];                             /* each track's instrument (DN_E_*) */
        uint8_t send[2][8];                         /* each track's sends to FX A, B */
        uint8_t ret[2], pad[2];
        struct { uint8_t type, p[3]; } fx[2];      /* (each effect's first three values; the rest: fx_more) */
    } proj;
    struct { uint8_t preset, pad[3]; int16_t p[SP_COUNT]; } sub;                /* SUB (from 0.13) */
    dn_lfo_p_t sublfo[DN_NLFO];                                                 /* SUB's own LFOs (from 0.15) */
    dn_patch_t user[2][DN_NUSER];                                               /* the user sounds (from 0.15) */
    uint8_t fx_more[2][4];                                                      /* FX A / B's 4th..7th values (0.16) */
    int16_t fe[2][4];                                                           /* REESE's, SUB's filter envelopes (0.19) */
    int8_t user_fe[2][DN_NUSER][4];                                             /* ... and each user sound's */
} dn_save_t;
#define DN_SAVE_V016 (offsetof(dn_save_t, fe))
#define DN_SAVE_V015 (offsetof(dn_save_t, fx_more))
#define DN_SAVE_V014 (offsetof(dn_save_t, sublfo))
#define DN_SAVE_V012 (offsetof(dn_save_t, sub))
#define DN_SAVE_V011 (offsetof(dn_save_t, proj))
#define DN_SAVE_V08 (offsetof(dn_save_t, set))
#define DN_SAVE_V05 (DN_SAVE_V08 - DN_NBRK * 4u)
#define DN_SAVE_V04 (DN_SAVE_V08 - DN_NBRK * 8u)

/* a flash write stops everything, the audio too, for a moment (an erase can't run beside it): the time of the last
 * one, so the AUDIO LATE warning (dn_app_draw) doesn't count it (0.19.1: it showed on every SAVE) */
static uint32_t dn_flash_t;
static int dn_store(uint32_t obj, const void *src, uint32_t len)
{
    int rc;
    dn_flash_t = plat_ms();
    rc = plat_store_save(obj, src, len);
    dn_flash_t = plat_ms();
    return rc;
}
static reese_t dn_rs;
static dn_patch_t dn_user[2][DN_NUSER];             /* the user sounds: REESE's, SUB's (kept with the sound) */
static int8_t dn_user_fe[2][DN_NUSER][4];           /* (0.19) each one's filter envelope */
static dn_save_t dn_sv;                             /* (the one copy of the saved sound in RAM: 3.7 KB, off the stack; in .bss,
                                                     * 0.16: the pool keeps its headroom for the FX) */
typedef char dn_save_fits[sizeof(dn_save_t) <= PLAT_STORE_MAX ? 1 : -1];
static dn_sub_t dn_sb;                              /* SUB (0.13) */
static dn_smp_t dn_sm;                              /* SAMPLER (0.23) */
static dn_fm_t dn_fmx;                              /* FM (0.25) */
/* chords (0.27): 15 of them (a step's flags pick one: DN_ST_CHORD), each its notes above the step's, and the chord
 * each sounding note of each track started with (so its note-off lets go of the whole chord) */
static const int8_t dn_chord[DN_CHORDS][DN_CHORD_N] = {
    {3, 7}, {4, 7}, {3, 7, 10}, {4, 7, 11}, {4, 7, 10}, {3, 7, 10, 14}, {4, 7, 11, 14}, {4, 7, 14}, {2, 7}, {5, 7},
    {7, 12}, {3, 6}, {4, 8}, {3, 7, 9}, {4, 7, 9}};
static const char *const DN_CHORD_NAME[1u + DN_CHORDS] = {"OFF", "MIN", "MAJ", "MIN7", "MAJ7", "7", "MIN9", "MAJ9",
                                                         "ADD9", "SUS2", "SUS4", "5", "DIM", "AUG", "MIN6", "6"};
static uint8_t dn_chord_on[DN_SEQ_TRACKS][128];
static uint8_t dn_fm_base[DN_FM_PATCH];             /* FM's voice as loaded and edited (the macros go on top) */
static dn_fm_macro_t dn_fm_mac;
#define DN_SMCK 1536u                               /* its samples' decoder checkpoints (one per 256 samples) */
static uint32_t dn_sm_ck[DN_SMCK];
static uint32_t dn_sm_key[DN_SMP_NSRC];             /* each pad's / zone's sample by its identity (dn_brk_key) */
static dn_seq_t dn_sq;
static uint32_t dn_rev;                             /* moves on whenever the sound or the pattern changes (the editor) */
static dn_brk_t dn_bk[DN_NBRK] X0X_POOL;
/* the LFOs (0.15: each synth has its own four, part of its sound: 0-3 REESE's, 4-7 SUB's) */
#define DN_NLFO_ALL (2u * DN_NLFO)
static dn_lfo_p_t dn_lp[DN_NLFO_ALL];               /* their settings ... */
static dn_lfo_s_t dn_ls[DN_NLFO_ALL];               /* ... and where each one is (audio) */
/* ---- the project (0.12): its name, each track's sends, the FX rack (two send slots in one memory arena) ---- */
#define DN_FX_ARENA (176u * 1024u)                  /* (0.16: DELAY's 96 KB and the biggest reverb's 77 KB) */
static uint32_t dn_fx_mem[DN_FX_ARENA / 4u] X0X_POOL;
static dn_fx_p_t dn_fxp[2];                         /* FX A, B: their settings (the project's) ... */
static dn_fx_s_t dn_fxs[2];                         /* ... and their state (audio) */
static uint8_t dn_send[2][8];                       /* track t's send to FX A / B, 0..127 */
static uint8_t dn_ret[2] = {100, 100};              /* FX A / B's return, 100 = unity */
static char dn_pname[12] = "PROJECT 1";
static uint8_t dn_bk_src[DN_NBRK];                  /* each one's loop: an index into the sources (dn_src) */
static struct {
    uint8_t preset, page, dirty, play, ready, rec;
    uint8_t trk;                                    /* the selected track */
    uint8_t view;                                   /* the 16 steps in view on the SEQ page (0..3) */
    uint8_t rec_cur;                                /* the step-record cursor */
    int8_t step_held;                               /* the step key held on the SEQ page (-1: none) */
    int8_t step_key;                                /* the key holding it (the step can move away from that key) */
    uint8_t step_edited;                            /* a knob moved it (or it was just turned on): no toggle off */
    uint8_t key_page;                               /* the page the keys down were pressed on (they act for it) */
    uint8_t key_mode;                               /* ... and ROLL's mode then (2: REC on, the keys play: 0.27.1) */
    uint8_t roll_lo;                                /* ROLL: the lowest pitch in view */
    uint8_t roll_keys;                              /* ROLL: 0 STEP mode (white keys = steps), 1 KEYS mode (notes) */
    uint8_t roll_cur;                               /* ROLL KEYS: the cursor (a step) */
    uint8_t entry_len;                              /* ROLL KEYS: the length a key writes (quarter steps) */
    uint8_t drum_row, drum_top;                     /* DRUM: the slice row selected, the first in view */
    uint8_t sl_sel;                                 /* SLICE: the chop selected */
    uint8_t sl_zoom;                                /* SLICE: the view, the loop >> zoom wide (0..6) */
    uint32_t sl_cur, sl_view;                       /* SLICE: the cursor and the view's left edge, samples */
    uint32_t chop_t;                                /* chops changed at (ms): written to flash a moment later */
    uint32_t home_t0;                               /* HOME down (SLICE: a tap deletes a chop, held 1 s clears) */
    uint8_t click, clk_lvl, cin;                    /* SETUP: click OFF / REC / ON, its level, count-in bars */
    volatile uint32_t cin_left, cin_next;           /* the count-in (audio): samples left, to the next beat */
    volatile uint32_t cin_beat;                     /* (its beats so far) */
    uint32_t clk_n, clk_ph, clk_last;               /* the click voice (audio): samples left, phase; the step seen */
    uint8_t clk_acc;
    uint32_t clk_count;                             /* (clicks so far: the tests count them) */
    uint8_t lfo_sel, lfo_view;                      /* the LFO page: which LFO (1-4 of the synth's); MAIN / MORE / DRAW */
    uint8_t lfo_force;
    uint8_t store;                                  /* SAVE + PRESETS: the user slot to store into (1-8; 0 none) */
    uint8_t naming, nm_set, nm_slot, nm_len, nm_cur; /* (0.22) the name editor: 1 the project, 2 a user sound */
    uint8_t smp_pad, smp_zone;                      /* (0.23) SAMPLER: the pad and the zone its pages show */
    uint8_t fm_op, fm_alt;                          /* (0.25) FM: the operator shown (0 = OP1), each page's second view */
    uint16_t fm_preset;                             /* FM: the voice PRESETS is on (factory, then the project's) */
    char nm[12];
    uint8_t home_chord;                             /* HOME held and SELECT turned: an instrument picked */
    uint32_t home_dn;                               /* (0.29) when HOME went down: a tap opens HOME, a hold is FILL */
    uint8_t home_back;                              /* (0.29) the page HOME was opened from */
    uint8_t dark;                                   /* (0.30) the DARK skin (LIGHT the default) */
    uint8_t last_len[DN_SEQ_TRACKS], last_chord[DN_SEQ_TRACKS];   /* (0.29) the roll: the last note's length, chord */
    uint8_t fx_exact;                               /* (the editor sets an effect: no skipping past ones that don't fit) */                              /* the editor: the LFO values read set 1 (REESE) / 2 (SUB), 0: by page */
    uint8_t midi_sel;                               /* the MIDI page: MIDI 1..n (the tracks that play MIDI, in order) */
    uint8_t bk_last;                                /* the break the BRK pages showed last (0 A, 1 B) */
    uint8_t bk_force;                               /* the editor: the BRK values read B (2) / A (1) whatever is shown */
    uint8_t fx_view;                                /* the FX page: MIX, FX A, FX A's next, FX B, FX B's next */
    uint8_t sb_preset;                              /* SUB's factory sound */
    uint8_t syn_last;                               /* the synth whose pages were shown last (DN_E_REESE / SUB) */
    uint8_t peng;                                   /* PROJECT: the instrument chosen for the track, not yet in (0xFF) */
    uint8_t seq_held, seq_chord;                    /* SEQ down; a key went with it (a track chosen): no SEQ */
    uint32_t keys_ign;                              /* keys pressed with SEQ: ignored until they come up */
    uint32_t keys_raw;                              /* the keys down, as the panel says (keys_prev: as the app takes them) */
    uint8_t back_page;                              /* SEQ again goes back here (the instrument's page it came from) */
    uint8_t step_pg;                                /* a step held: which four of its values the knobs edit (FX) */
    int8_t ramp_a, ramp_b;                          /* a velocity ramp over steps a..b (b < 0: none) ... */
    uint8_t ramp_v0, ramp_v1;                       /* ... from, to */
    uint8_t rec_held, rec_chord;                    /* REC down on a sequencer page; used with a step (no record) */
    uint8_t pclip_ok;                               /* a pattern copied (SAVE + black key 10) */
    int8_t pen;                                     /* DRAW: the value a pressed point gets */
    uint8_t pt_view;                                /* DRAW: points 1-16 / 17-32 on the keys */
    int8_t pt_held;                                 /* DRAW: the point key held (-1: none) */
    uint32_t lfo_left;                              /* (audio) samples to the LFOs' next tick */
    uint8_t chop_rec;                               /* SLICE: REC lit, the keys cut (key n = chop n) */
    const char *msg;                                /* a word on the status line (a refusal, what happened) ... */
    uint32_t msg_t;                                 /* ... shown until this time (ms) */
    uint8_t home_done;
    uint8_t save_held, save_chord;                  /* SAVE down; it was used with OCT (copy / paste): no save */
    uint8_t clip_ok;
    dn_step_t clip[16];                             /* copy / paste: one bar of one track */
    uint8_t rec_step[NKEYS];                        /* REC: where each key's note went, and when it went down */
    uint32_t rec_tick[NKEYS];
    uint8_t rec_last;                               /* the step the last held note was written to (the host test) */
    uint8_t rec_grp;                                /* (0.27) REC: the step the keys held now went to (their chord) */
    uint8_t rec_notes[DN_CHORD_N + 1u], rec_n;      /* (0.28) the keys held into it: the chord they make goes there */
    uint32_t clear_t0;                              /* black key 9 went down (0: not held) */
    int8_t oct;
    uint8_t held[NKEYS], nheld;                     /* the keys down, oldest first */
    uint8_t sounding;                               /* the note the keys play now (0xFF: none) */
    uint8_t midi_note;                              /* the note MIDI plays now (0xFF: none) */
    uint32_t btn_prev, keys_prev;
    uint32_t redraw;                                /* DN_RD_* */
} dn;
enum { DN_RD_TOP = 1u, DN_RD_BOTTOM = 2u, DN_RD_ALL = 3u };

static uint32_t dn_trk_of(uint32_t e)              /* the track that plays instrument e (0xFF: none) */
{
    uint32_t t;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        if (dn_eng[t] == e)
            return t;
    return 0xFFu;
}
static uint32_t dn_midi_n(void)                     /* how many tracks play MIDI */
{
    uint32_t t, n = 0;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        n += DN_IS_MIDI(t);
    return n;
}
static uint32_t dn_midi_trk(uint32_t k)             /* "MIDI k+1": the (k+1)th MIDI track (0xFF: none) */
{
    uint32_t t, n = dn_midi_n();
    if (!n)
        return 0xFFu;
    k %= n;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        if (DN_IS_MIDI(t) && !k--)
            return t;
    return 0xFFu;
}
static uint32_t dn_rs_trk(void)                     /* REESE's track (0 if it's on none: its settings still edit) */
{
    uint32_t t = dn_trk_of(DN_E_REESE);
    return t < DN_SEQ_TRACKS ? t : 0u;
}
static uint32_t dn_syn_trk(void)                    /* the synth the sound pages show: its track */
{
    return DN_IS_SYN(dn.trk) ? dn.trk : dn_rs_trk();
}
static uint32_t dn_midi_idx(uint32_t t)             /* track t is "MIDI n+1" */
{
    uint32_t u, n = 0;
    for (u = 0; u < t && u < DN_SEQ_TRACKS; u++)
        n += DN_IS_MIDI(u);
    return n;
}

/* the output, every 8th sample, for the screen's scope (written in the audio interrupt, read by the drawing) */
#define DN_SCOPE 512u
static int16_t dn_scope[DN_SCOPE];
static volatile uint32_t dn_scope_w;

/* -------------------------------------------------------------------------------------------- audio side --- */
/* a break hit: first the other voices in its choke group stop (declicked), then the slice plays */
static void dn_brk_hit(uint32_t k, uint32_t slice, uint32_t vel, int rev)
{
    uint32_t j;
    if (dn_bk[k].choke)
        for (j = 0; j < DN_NBRK; j++)
            if (j != k && dn_bk[j].choke == dn_bk[k].choke)
                dn_brk_choke(&dn_bk[j]);
    dn_brk_trigger(&dn_bk[k], slice, vel, rev);
}

/* ---- the LFOs ---------------------------------------------------------------------------------------------- */
/* a 0.8 sound's filter LFO (LRATE, LFLT) becomes LFO 1: free, the same rate, a sine to CUTOFF at the same depth
 * (the same sweep: LFO depth on CUTOFF is the old LFLT's scale) */
static void dn_lfo_legacy(void)
{
    if (!dn_rs.p[RP_LFLT])
        return;
    dn_lfo_default(&dn_lp[0]);
    dn_lp[0].sync = 0;
    dn_lp[0].rate = (uint8_t)dn_clamp(dn_rs.p[RP_LRATE], 0, 127);
    dn_lp[0].dest = DN_LD_CUT;
    dn_lp[0].depth = (int8_t)dn_clamp(dn_rs.p[RP_LFLT], -64, 63);
    dn_rs.p[RP_LFLT] = 0;                           /* (REESE's own LFO stays silent from now on) */
}
static void dn_lfo_init_set(uint32_t set)           /* a synth's new sound: its LFOs off (REESE: its old filter LFO's) */
{
    uint32_t j;
    for (j = set * DN_NLFO; j < (set + 1u) * DN_NLFO; j++) {
        dn_lfo_default(&dn_lp[j]);
        dn_lfo_reset(&dn_ls[j], (int32_t)(0x9E3779B9u * (j + 1u)));
    }
    if (!set)
        dn_lfo_legacy();
}
static void dn_lfo_init_all(void) { dn_lfo_init_set(0); }   /* (REESE's: as before 0.15) */
static void dn_lfo_fix(dn_lfo_p_t *l)               /* an LFO from storage, checked */
{
    l->shape = (uint8_t)dn_clamp(l->shape, 0, DN_LS_N - 1);
    l->sync = l->sync ? 1u : 0u;
    l->rate = (uint8_t)dn_clamp(l->rate, 0, l->sync ? DN_LFO_NDIV - 1 : 127);
    l->dest = (uint8_t)dn_clamp(l->dest, 0, DN_LD_N - 1);
    l->depth = (int8_t)dn_clamp(l->depth, -64, 63);
    l->phase &= 127u;
    l->mode = (uint8_t)dn_clamp(l->mode, 0, DN_LM_N - 1);
    l->smooth &= 127u;
    l->npts = l->npts == 32u ? 32u : 16u;
}

/* ---- user sounds (0.15): a synth's sound (set 0 REESE, 1 SUB) into a slot, and back ----------------------------- */
static void dn_patch_store(uint32_t set, dn_patch_t *u)
{
    {                                               /* (0.19: its filter envelope, beside it) */
        int8_t *fe = dn_user_fe[set][u - dn_user[set]];
        const int16_t *src = set ? dn_sb.fe : dn_rs.fe;
        uint32_t j;
        for (j = 0; j < 4u; j++)
            fe[j] = (int8_t)dn_clamp(src[j], 0, 127);
    }
    uint32_t k, n = set ? SP_COUNT : RP_COUNT;
    for (k = 0; k < sizeof u->p; k++)
        u->p[k] = (int8_t)(k < n ? (set ? dn_sb.p[k] : dn_rs.p[k]) : 0);
    u->used = 1;
    u->esync = set ? dn_sb.esync : dn_rs.esync;
    u->pad[0] = u->pad[1] = 0;
    for (k = 0; k < DN_NLFO; k++) {
        const dn_lfo_p_t *l = &dn_lp[set * DN_NLFO + k];
        dn_plfo_t *q = &u->lfo[k];
        uint32_t i;
        q->shape = l->shape, q->sync = l->sync, q->rate = l->rate, q->dest = l->dest, q->depth = l->depth;
        q->phase = l->phase, q->mode = l->mode, q->smooth = l->smooth, q->npts = l->npts;
        for (i = 0; i < DN_LFO_PTS; i++)
            q->pts[i] = l->pts[i];
    }
}
static void dn_patch_load(uint32_t set, const dn_patch_t *u)   /* (inside the lock) */
{
    {
        const int8_t *fe = dn_user_fe[set][u - dn_user[set]];
        int16_t *dst = set ? dn_sb.fe : dn_rs.fe;
        uint32_t j;
        for (j = 0; j < 4u; j++)
            dst[j] = (int16_t)dn_clamp(fe[j], 0, 127);
    }
    uint32_t k, n = set ? SP_COUNT : RP_COUNT;
    for (k = 0; k < n; k++) {
        int32_t v = u->p[k];
        if (set)
            dn_sb.p[k] = (int16_t)dn_clamp(v, dn_sub_param_min(k), dn_sub_param_max(k));
        else
            dn_rs.p[k] = (int16_t)dn_clamp(v, reese_param_min(k), reese_param_max(k));
    }
    if (set)
        dn_sb.esync = u->esync ? 1u : 0u;
    else
        dn_rs.esync = u->esync ? 1u : 0u;
    for (k = 0; k < DN_NLFO; k++) {
        dn_lfo_p_t *l = &dn_lp[set * DN_NLFO + k];
        const dn_plfo_t *q = &u->lfo[k];
        uint32_t i;
        dn_lfo_default(l);
        l->shape = q->shape, l->sync = q->sync, l->rate = q->rate, l->dest = q->dest, l->depth = q->depth;
        l->phase = q->phase, l->mode = q->mode, l->smooth = q->smooth, l->npts = q->npts, l->pad[0] = 1;
        for (i = 0; i < DN_LFO_PTS; i++)
            l->pts[i] = q->pts[i];
        dn_lfo_fix(l);
        dn_lfo_reset(&dn_ls[set * DN_NLFO + k], (int32_t)(0x9E3779B9u * (set * DN_NLFO + k + 1u)));
    }
}
/* REESE's notes: a note that isn't a slide restarts the RETRIG / 1-SHOT LFOs */
static void dn_lfo_retrig(uint32_t set)             /* a new note (not a slide): its synth's RETRIG / 1-SHOT LFOs restart */
{
    uint32_t j;
    for (j = set * DN_NLFO; j < (set + 1u) * DN_NLFO; j++)
        if (dn_lp[j].mode != DN_LM_FREE) {
            dn_ls[j].acc = 0, dn_ls[j].ph = 0, dn_ls[j].done = 0;
            dn_ls[j].sh_a = dn_ls[j].sh_b;
        }
}
static void dn_rs_on(uint8_t note, uint8_t vel, int legato)
{
    int slide = legato && dn_rs.gate && dn_rs.active && dn_rs.p[RP_GLIDE];
    reese_note_on(&dn_rs, note, vel, legato);
    if (!slide)
        dn_lfo_retrig(0);
}
/* SUB's notes: the same (0.13: SUB's notes restart the RETRIG LFOs too; a legato note with a key held doesn't) */
static void dn_sb_on(uint8_t note, uint8_t vel, int legato, int accent)
{
    int held = legato && dn_sb.gate && dn_sb.active && dn_sb.stage;
    dn_sub_note_on(&dn_sb, note, vel, legato, accent);
    if (!held)
        dn_lfo_retrig(1);
}
/* the LFOs' tick (every 32 samples, audio): each one on, then onto its destination. Depth -64..63; full depth:
 * CUTOFF +-128 steps (the whole range), PITCH +-12 semitones, the other values +-64 steps; Ln DEPTH scales that
 * LFO's depth (x0 .. x2) */
static void dn_lfo_tick(uint32_t n)
{
    int32_t dm[DN_NLFO_ALL] = {0}, rm[RP_COUNT] = {0}, pitch = 0, bp[DN_NBRK] = {0}, bd[DN_NBRK] = {0}, bc[DN_NBRK] = {0};
    int32_t bl[DN_NBRK] = {0}, sm[SP_COUNT] = {0}, spitch = 0;
    uint32_t j, k;
    dn_sb.bpm = dn_sq.bpm;                          /* (SUB's synced envelope times) */
    for (j = 0; j < DN_NLFO_ALL; j++)
        dn_lfo_run(&dn_ls[j], &dn_lp[j], n, dn_sq.bpm);
    for (j = 0; j < DN_NLFO_ALL; j++)               /* the LFOs on other LFOs' depth first (Ln: its own synth's) */
        if (DN_LD_IS_DEPTH(dn_lp[j].dest))
            dm[(j & ~(DN_NLFO - 1u)) + dn_lp[j].dest - DN_LD_L1DEPTH] += (dn_ls[j].out * dn_lp[j].depth) >> 6;
    for (j = 0; j < DN_NLFO_ALL; j++) {
        uint32_t d = dn_lp[j].dest;
        int32_t dep = dn_clamp(dn_lp[j].depth * (32767 + dm[j]) / 32767, -128, 127), a = (dn_ls[j].out * dep) >> 6;
        if (!d || DN_LD_IS_DEPTH(d) || d >= DN_LD_N)
            continue;
        if (d >= DN_LD_SCUT) {                      /* SUB's: CUT a full sweep, PITCH +-12, the others +-64 steps */
            if (d == DN_LD_SPITCH)
                spitch += (a * 3) >> 5;
            else
                sm[DN_LD_SP[d - DN_LD_SCUT]] += d == DN_LD_SCUT ? a : a >> 1;
        } else if (d == DN_LD_CUT)
            rm[RP_CUT] += a;
        else if (d == DN_LD_PITCH)
            pitch += (a * 3) >> 5;                  /* 12 semitones x 256 at 32767 */
        else if (d < DN_LD_APITCH)
            rm[DN_LD_RP[d]] += a >> 1;
        else {
            k = d < DN_LD_BPITCH ? 0u : 1u;
            switch ((d - DN_LD_APITCH) % 4u) {
            case 0: bp[k] += (a * 3) >> 5; break;
            case 1: bd[k] += a >> 9; break;
            case 2: bc[k] += a >> 9; break;
            default: bl[k] += a >> 9; break;
            }
        }
    }
    for (k = 0; k < RP_COUNT; k++)
        dn_rs.mod[k] = rm[k];
    dn_rs.mod_pitch = pitch;
    for (k = 0; k < SP_COUNT; k++)
        dn_sb.mod[k] = sm[k];
    dn_sb.mod_pitch = spitch;
    for (k = 0; k < DN_NBRK; k++) {
        dn_brk_t *b = &dn_bk[k];
        b->mdecay = (int16_t)bd[k], b->mcrush = (int16_t)bc[k], b->mlevel = (int16_t)bl[k];
        if (bp[k] != b->mpitch) {
            b->mpitch = bp[k];
            dn_brk_tempo(b, b->bpm ? b->bpm : dn_sq.bpm);
        }
    }
}

/* the sequencer's notes: track 1 plays REESE, 2 and 3 BREAKS A / B (the note: the slice; the step's slide: reverse),
 * the others MIDI out on their channel (USB-MIDI event packets) */
static void dn_sq_on1(uint32_t t, uint8_t note, uint8_t vel, int slide, uint8_t flags)
{
    if (DN_IS_RS(t))
        dn_rs_on(note, vel, slide);
    else if (DN_IS_SUB(t))
        dn_sb_on(note, vel, slide, (flags & DN_ST_ACC) != 0);
    else if (DN_IS_BRK(t))
        dn_brk_hit(DN_BK(t), note, vel, (flags & DN_ST_SLIDE) != 0);
    else if (DN_IS_SMP(t)) {                        /* SAMPLER: a pad (DRUM's row), or a key of the map */
        if (dn_sm.p.mode == DN_SMP_ONESHOT)
            dn_smp_pad(&dn_sm, note, vel);
        else
            dn_smp_note_on(&dn_sm, note, vel);
    } else if (DN_IS_FM(t)) {
        dn_fm_note_on(&dn_fmx, note, (flags & DN_ST_ACC) && vel < 127u ? 127u : vel);
    } else if (DN_IS_MIDI(t))
        plat_midi_out(0x09u | (0x90u | dn_sq.pat.tr[t].chan) << 8 | (uint32_t)note << 16 | (uint32_t)vel << 24);
}
static void dn_sq_off1(uint32_t t, uint8_t note)
{
    if (DN_IS_BRK(t))
        dn_brk_release(&dn_bk[DN_BK(t)]);
    else if (DN_IS_SMP(t)) {
        if (dn_sm.p.mode == DN_SMP_KEYS)
            dn_smp_note_off(&dn_sm, note);
    } else if (DN_IS_FM(t))
        dn_fm_note_off(&dn_fmx, note);
    else if (DN_IS_MIDI(t))
        plat_midi_out(0x08u | (0x80u | dn_sq.pat.tr[t].chan) << 8 | (uint32_t)note << 16);
    else if (DN_IS_SUB(t)) {
        if (dn_sb.note == note && dn.sounding == 0xFF)
            dn_sub_note_off(&dn_sb);
    } else if (DN_IS_RS(t) && dn_rs.note == note && dn.sounding == 0xFF && dn.midi_note == 0xFF)
        reese_note_off(&dn_rs);                     /* (not a note the keys or MIDI in play now) */
}
/* (0.27) chords: the instruments that play several notes at once (FM, SAMPLER's keys, MIDI) play a step's chord too;
 * the others its own note */
#define DN_CHORDABLE(t) (DN_IS_POLY(t) || DN_IS_MIDI(t))
static void dn_sq_on(uint32_t t, uint8_t note, uint8_t vel, int slide, uint8_t flags)
{
    uint32_t c = DN_CHORDABLE(t) ? DN_ST_CHORD_OF(flags) : 0u, k;
    dn_sq_on1(t, note, vel, slide, flags);
    dn_chord_on[t][note & 127u] = (uint8_t)c;
    for (k = 0; c && k < DN_CHORD_N; k++) {
        int32_t n = (int32_t)note + dn_chord[c - 1u][k];
        if (dn_chord[c - 1u][k] && n >= 0 && n <= 127)
            dn_sq_on1(t, (uint8_t)n, vel, 0, (uint8_t)(flags & ~DN_ST_CHORD));
    }
}
static void dn_sq_off(uint32_t t, uint8_t note)
{
    uint32_t c = dn_chord_on[t][note & 127u], k;
    dn_sq_off1(t, note);
    dn_chord_on[t][note & 127u] = 0;
    for (k = 0; c && k < DN_CHORD_N; k++) {
        int32_t n = (int32_t)note + dn_chord[c - 1u][k];
        if (dn_chord[c - 1u][k] && n >= 0 && n <= 127)
            dn_sq_off1(t, (uint8_t)n);
    }
}
/* a chord's notes, the step's own first: how many (into out) */
static uint32_t dn_chord_notes(const dn_step_t *st, uint8_t out[1u + DN_CHORD_N])
{
    uint32_t c = DN_ST_CHORD_OF(st->flags), n = 0, k;
    out[n++] = st->note;
    for (k = 0; c && k < DN_CHORD_N; k++) {
        int32_t v = (int32_t)st->note + dn_chord[c - 1u][k];
        if (dn_chord[c - 1u][k] && v >= 0 && v <= 127)
            out[n++] = (uint8_t)v;
    }
    return n;
}
/* (0.28) the chord these notes make (REC: keys held together), in any order or inversion: its number (0: none of
 * the 15) and its root (*root: in the octave at or below the lowest note) */
static uint32_t dn_chord_match(const uint8_t *notes, uint32_t n, uint8_t *root)
{
    uint32_t i, c, k, j;
    uint8_t lo = 127, s[DN_CHORD_N + 1u];
    for (i = 0; i < n; i++) {                       /* (lowest first: it is the root when two chords fit) */
        for (j = i; j > 0u && s[j - 1u] > notes[i]; j--)
            s[j] = s[j - 1u];
        s[j] = notes[i];
    }
    notes = s;
    lo = n ? s[0] : 0u;
    for (i = 0; i < n; i++) {                       /* each note as the root: the others' distances, in one octave */
        uint32_t have = 0;
        for (j = 0; j < n; j++)
            have |= 1u << ((notes[j] + 120u - notes[i]) % 12u);
        for (c = 0; c < DN_CHORDS; c++) {
            uint32_t want = 1u;
            for (k = 0; k < DN_CHORD_N; k++)
                if (dn_chord[c][k])
                    want |= 1u << (dn_chord[c][k] % 12);
            if (have == want) {
                int32_t r = notes[i];
                while (r > lo)
                    r -= 12;
                *root = (uint8_t)(r < 0 ? r + 12 : r);
                return c + 1u;
            }
        }
    }
    *root = n ? notes[0] : lo;
    return 0;
}

static void dn_step_remember(uint32_t t, const dn_step_t *st);
/* (0.28) REC: note joins the keys held into step st; the step takes the chord they make, on its root (none of the
 * 15: the first key's note alone) */
static void dn_chord_rec(dn_step_t *st, uint8_t note)
{
    uint8_t root;
    uint32_t c;
    if (dn.rec_n >= sizeof dn.rec_notes)
        return;
    dn.rec_notes[dn.rec_n++] = note;
    c = dn_chord_match(dn.rec_notes, dn.rec_n, &root);
    dn_lock();
    st->note = c ? root : dn.rec_notes[0];
    st->flags = (uint8_t)((st->flags & ~DN_ST_CHORD) | (c << 4));
    dn_unlock();
    dn_step_remember(dn.trk, st);
}
static void dn_sq_clock(uint8_t st) { plat_midi_out(0x0Fu | (uint32_t)st << 8); }
/* ---- patterns: 8 an instrument (a track), the playing one in dn_sq.pat, the others here ------------------------
 * dn_bank[t][n] holds track t's pattern n; the one playing (dn_pcur[t]) lives in dn_sq.pat.tr[t] while it plays
 * (edits go there) and is copied back when another comes in. A change asked for while playing waits for the track's
 * pattern to end (dn_pq: queued; the sequencer's LST says its last pass); stopped, it's at once. A track keeps its
 * mute and MIDI channel across its patterns. Kept in flash, only the notes that are on (see dn_pat_save). */
#define DN_NPAT 8u
static dn_track_t dn_bank[DN_SEQ_TRACKS][DN_NPAT] X0X_POOL;
static uint8_t dn_pcur[DN_SEQ_TRACKS], dn_pq[DN_SEQ_TRACKS];   /* playing; queued (0xFF: none) */
static void dn_pat_switch(uint32_t t, uint32_t n)   /* (the lock held, or in the audio interrupt) */
{
    dn_track_t *live = &dn_sq.pat.tr[t];
    uint8_t mute = live->mute, chan = live->chan;
    dn_bank[t][dn_pcur[t]] = *live;
    *live = dn_bank[t][n];
    live->mute = mute;
    live->chan = chan;
    dn_pcur[t] = (uint8_t)n;
    dn_pq[t] = 0xFF;
    dn_sq.lst &= (uint8_t)~(1u << t);
    dn_seq_track_new(&dn_sq, t);
}
static void dn_sq_wrap(uint32_t t)                  /* (audio) a track's pattern ends: a queued one comes in */
{
    if (dn_pq[t] != 0xFF && dn_pq[t] != dn_pcur[t])
        dn_pat_switch(t, dn_pq[t]);
    else
        dn_pq[t] = 0xFF, dn_sq.lst &= (uint8_t)~(1u << t);
}
static const dn_seq_cb_t DN_SQ_CB = {dn_sq_on, dn_sq_off, dn_sq_clock, dn_sq_wrap};

/* the click: a short square burst, higher on the bar, fading in ~25 ms */
#define DN_CLK_LEN 1100u
static void dn_click_start(int acc)
{
    dn.clk_n = DN_CLK_LEN;
    dn.clk_ph = 0;
    dn.clk_acc = (uint8_t)acc;
    dn.clk_count++;
}
static void dn_click_render(int32_t *lr, uint32_t n)
{
    uint32_t i, inc = dn.clk_acc ? 2500u * 65536u / 44100u : 1500u * 65536u / 44100u;
    for (i = 0; i < n && dn.clk_n; i++) {
        int32_t a = (int32_t)(dn.clk_n * (uint32_t)dn.clk_lvl / DN_CLK_LEN) * (dn.clk_acc ? 90 : 60);
        int32_t v = (dn.clk_ph & 0x8000u) ? a : -a;
        lr[2u * i] += v;
        lr[2u * i + 1u] += v;
        dn.clk_ph += inc;
        dn.clk_n--;
    }
}
static uint32_t dn_beat_samples(void) { return 2646000u / (dn_sq.bpm ? dn_sq.bpm : 120u); }   /* 44100 * 60 / BPM */

/* the instruments, each into its own buffer, then into the mix and, at its track's sends, into FX A / B; the FX
 * after (k <= 32 frames: the LFOs' tick cuts every render there) */
#define DN_MIXN 32u
static void dn_mix_render(int32_t *lr, uint32_t k)
{
    static int32_t ib[2u * DN_MIXN], sa[2u * DN_MIXN], sb[2u * DN_MIXN];
    uint32_t e, i, t, na = 0, nb = 0;
    if (k > DN_MIXN)
        k = DN_MIXN;
    for (i = 0; i < 2u * k; i++)
        sa[i] = sb[i] = 0;
    for (e = DN_E_REESE; e < DN_E_N; e++) {
        if (e == DN_E_MIDI)
            continue;
        uint32_t a, b;
        for (i = 0; i < 2u * k; i++)
            ib[i] = 0;
        if (e == DN_E_REESE)
            reese_render(&dn_rs, ib, k);
        else if (e == DN_E_SUB)
            dn_sub_render(&dn_sb, ib, k);
        else if (e == DN_E_SMP)
            dn_smp_render(&dn_sm, ib, k);
        else if (e == DN_E_FM)
            dn_fm_render(&dn_fmx, ib, k);
        else
            dn_brk_render(&dn_bk[e - DN_E_BRKA], ib, k);   /* (a break on no track: only chop mode's loop) */
        t = dn_trk_of(e);
        a = t < 8u ? dn_send[0][t] : 0u;
        b = t < 8u ? dn_send[1][t] : 0u;
        for (i = 0; i < 2u * k; i++)
            lr[i] += ib[i];
        if (a)
            for (i = 0, na = 1; i < 2u * k; i++)
                sa[i] += (ib[i] * (int32_t)a) >> 7;
        if (b)
            for (i = 0, nb = 1; i < 2u * k; i++)
                sb[i] += (ib[i] * (int32_t)b) >> 7;
    }
    (void)na, (void)nb;                             /* (the effects run on: their tails ring out) */
    dn_fx_run(&dn_fxs[0], &dn_fxp[0], sa, lr, k, dn_sq.bpm, dn_ret[0]);
    dn_fx_run(&dn_fxs[1], &dn_fxp[1], sb, lr, k, dn_sq.bpm, dn_ret[1]);
}

/* The master limiter (0.16.1): with the reverbs' and MASSIVE's returns on top of the engines a mix went past full
 * scale, and the soft knee below clipped it (Jamie: "a buzz sometimes, like it's distorting"). Carnac OS's law: the
 * envelope jumps to a peak over the ceiling (no sample passes it) and comes back with tau ~0.15 s; the gain is ceiling /
 * envelope, the same both sides (the stereo image holds). The ceiling sits just under the knee, so the knee only
 * catches what slips through the float's rounding. */
#define DN_LIM_TOP 24000.f
static float dn_lim_env = DN_LIM_TOP;
static uint32_t dn_knee;
static void dn_master_limit(int32_t *lr, uint32_t n)
{
    float env = dn_lim_env;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t l = lr[2u * i], r = lr[2u * i + 1u];
        float a = (float)(l < 0 ? -l : l), b = (float)(r < 0 ? -r : r);
        if (b > a)
            a = b;
        if (env > DN_LIM_TOP)
            env = DN_LIM_TOP + (env - DN_LIM_TOP) * 0.99985f;
        if (a > env)
            env = a;
        if (env > DN_LIM_TOP) {
            float g = DN_LIM_TOP / env;
            lr[2u * i] = (int32_t)((float)l * g);
            lr[2u * i + 1u] = (int32_t)((float)r * g);
        }
    }
    dn_lim_env = env;
}

void dn_app_render(int32_t *lr, uint32_t n)
{
    uint32_t i, w = dn_scope_w, off = 0;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (!dn.ready)
        return;
    while (off < n) {                               /* cut at the sequencer's events: sample-accurate notes */
        uint32_t k = dn_seq_next(&dn_sq);
        if (dn.cin_left) {                          /* the count-in: a click a beat, then the sequencer starts */
            if (!dn.cin_next) {
                dn_click_start(dn.cin_beat % 4u == 0u);
                dn.cin_beat++;
                dn.cin_next = dn_beat_samples();
            }
            k = dn.cin_next < dn.cin_left ? dn.cin_next : dn.cin_left;
        }
        if (!dn.lfo_left) {
            dn_lfo_tick(32u);
            dn.lfo_left = 32u;
        }
        if (k > dn.lfo_left)
            k = dn.lfo_left;
        if (k > n - off)
            k = n - off;
        dn_mix_render(lr + 2u * off, k);
        dn_click_render(lr + 2u * off, k);
        if (dn.cin_left) {
            dn.cin_next -= k;
            dn.cin_left -= k;
            if (!dn.cin_left)
                dn_seq_start(&dn_sq, &DN_SQ_CB);   /* (its first step on the next sample: bar 1, beat 1) */
        } else {
            dn_seq_run(&dn_sq, k, &DN_SQ_CB);
        }
        dn.lfo_left -= k;
        if (dn_sq.playing && dn_sq.pos != dn.clk_last) {   /* a step began just now: on a beat, the click */
            uint32_t st = dn_sq.pos - 1u, j;
            dn.clk_last = dn_sq.pos;
            if (st == 0u)                           /* the pattern starts: the synced LFOs from their start */
                for (j = 0; j < DN_NLFO_ALL; j++)
                    if (dn_lp[j].sync)
                        dn_ls[j].acc = 0, dn_ls[j].ph = 0, dn_ls[j].done = 0;
            if (st % 4u == 0u && (dn.click == 2u || (dn.click == 1u && dn.rec)))
                dn_click_start(st % 16u == 0u);
        }
        off += k;
    }
    dn_master_limit(lr, n);                         /* (0.16.1: peaks turned down, not clipped) */
    for (i = 0; i < 2u * n; i++) {                  /* the master: a soft knee at the top, nothing ever wraps */
        if (lr[i] > 24576 || lr[i] < -24576) {
            lr[i] = (dn_softclip(lr[i] * 4 / 3) * 3) / 4;
            dn_knee++;                              /* (the host tests: how often the knee bends a sample) */
        }
    }
    for (i = 0; i < n; i += 8u)
        dn_scope[w++ & (DN_SCOPE - 1u)] = (int16_t)dn_clamp(lr[2u * i], -32768, 32767);
    dn_scope_w = w;
}

void dn_app_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t t = st & 0xF0u;
    if (!dn.ready)
        return;
    if (t == 0x90u && d2) {
        dn_rs_on(d1, d2, dn_rs.gate);               /* overlapping notes slide, as a mono synth does */
        dn.midi_note = d1;
    } else if ((t == 0x80u || t == 0x90u) && d1 == dn.midi_note) {
        dn.midi_note = 0xFF;
        if (dn.sounding == 0xFF)
            reese_note_off(&dn_rs);
    } else if (t == 0xB0u && d1 == 123u) {          /* all notes off */
        dn.midi_note = 0xFF;
        reese_note_off(&dn_rs);
    }
}

/* ----------------------------------------------------------------------------------------------- breaks --- */
/* the loops BREAKS can play: every loop sent to the three sample slots, in order (0.29: no built-in ones any more) */
#define DN_OLD_BUILTIN 2u                           /* (ROLLER and STEPPER, built in before 0.29: older saves count them) */
/* a saved loop number: 0.29's (top bit set), or one from before, which counted ROLLER and STEPPER first (theirs go to
 * the first loop) */
static uint8_t dn_brk_src_of(uint8_t v)
{
    return (v & 0x80u) ? (uint8_t)(v & 0x7Fu) : v >= DN_OLD_BUILTIN ? (uint8_t)(v - DN_OLD_BUILTIN) : 0u;
}
/* the sample slots: bytes used (4-bit ADPCM: half a byte a sample, plus each slot's header), and in all */
#define DN_SLOT_BYTES 0x14000u
#define DN_SLOT3_BYTES (64u * 1024u)                    /* (0.29: slot 3's top 16 KB holds settings) */
static uint32_t dn_smp_cap(void) { return (PLAT_NSLOTS - 1u) * DN_SLOT_BYTES + DN_SLOT3_BYTES; }
static uint32_t dn_smp_used(void)
{
    uint32_t used = 0, n, r;
    int k, z;
    char nm[9];
    for (k = 0; k < PLAT_NSLOTS; k++) {
        if (plat_slot_zones(k) > 0)
            used += 512u;
        for (z = 0; z < plat_slot_zones(k); z++)
            if (plat_slot(k, z, &n, &r, nm))
                used += (n + 1u) / 2u;
    }
    return used;
}
/* (0.25) a zone of a sample slot can hold DX7 voices (the editor's Project tab sends them with the samples): its data
 * starts "DNFMBNK", then how many, then each packed voice (128 bytes). Not a sample: the loops' list skips it. */
static const uint8_t *dn_fmz(int k, int z, uint32_t *cnt)
{
    static const char M[7] = {'D', 'N', 'F', 'M', 'B', 'N', 'K'};
    uint32_t n, r, i;
    char nm[9];
    const uint8_t *a = plat_slot(k, z, &n, &r, nm);
    if (!a || n < 16u)
        return 0;
    for (i = 0; i < 7u; i++)
        if (a[i] != (uint8_t)M[i])
            return 0;
    if (!a[7] || (n + 1u) / 2u < 8u + (uint32_t)a[7] * DN_FM_PACKED)
        return 0;
    *cnt = a[7];
    return a + 8;
}
static uint32_t dn_fmv_count(void)                  /* the project's DX7 voices */
{
    uint32_t n = 0, c;
    int k, z;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++)
            if (dn_fmz(k, z, &c))
                n += c;
    return n;
}
static const uint8_t *dn_fmv_get(uint32_t i)        /* project voice i, packed */
{
    uint32_t c;
    int k, z;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++) {
            const uint8_t *a = dn_fmz(k, z, &c);
            if (a && i < c)
                return a + i * DN_FM_PACKED;
            if (a)
                i -= c;
        }
    return 0;
}
static uint32_t dn_src_count(void)
{
    uint32_t n = 0, c;
    int k, z;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++)
            n += !dn_fmz(k, z, &c);
    return n;
}
static int dn_src_get(uint32_t i, dn_loop_t *L)
{
    int k, z;
    uint32_t j;
    for (j = 0; j < sizeof L->name; j++)
        L->name[j] = 0;
    L->adpcm = 0, L->n = 0;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++) {
            uint32_t c;
            if (dn_fmz(k, z, &c))                   /* (DX7 voices: not a loop) */
                continue;
            if (i-- == 0u) {
                char nm[9];
                uint32_t num = 0, d;
                int zz;
                L->adpcm = plat_slot(k, z, &L->n, &L->rate, nm);
                L->bars = dn_brk_bars_of(L->n, L->rate);
                for (d = 0; d <= (uint32_t)k; d++)  /* named as the breaks page numbers them: BR1, BR2 ... */
                    for (zz = 0; zz < (d == (uint32_t)k ? z + 1 : plat_slot_zones((int)d)); zz++)
                        num += !dn_fmz((int)d, zz, &c);
                {
                    char t[8];
                    reese_param_text(RP_LEVEL, (int32_t)num, t);
                    L->name[0] = 'B';
                    L->name[1] = 'R';
                    for (d = 0; t[d] && d < 6u; d++)
                        L->name[2u + d] = t[d];
                    L->name[2u + d] = 0;
                }
                return L->adpcm != 0;
            }
        }
    return 0;
}
/* BREAKS k plays loop src (main loop): the audio lets go of it first, the new loop is read, the speed set */
/* MANUAL slices, kept per loop (by its identity, dn_brk_key) in their own storage object, so the sample slots stay in
 * X0X's format and other firmwares still read them. Up to DN_MAN_N loops; the oldest one is forgotten first. */
#define DN_MAN_MAGIC1 0x31534E44u                   /* "DNS1": 24 loops (to 0.22) */
#define DN_MAN_MAGIC 0x32534E44u                    /* "DNS2" (0.23): 20 loops, the names, SAMPLER's settings */
#define DN_MAN_N1 24u
#define DN_MAN_N 20u
typedef struct {
    uint32_t key;
    uint8_t n, pad[3];
    uint16_t st[DN_BRK_MAXSL];                      /* where each slice starts, in 4-sample units */
} dn_man_t;
typedef struct {                                    /* SAMPLER's settings as kept (0.23) */
    uint8_t mode, pad0[3];
    int16_t pad[DN_SMP_PADS][PD_N];
    int16_t zone[DN_SMP_ZONES][ZN_N];
    int16_t key[KY_N], pad1;
    uint32_t src_key[DN_SMP_NSRC];                  /* each pad's / zone's sample's identity: found again if the
                                                     * slots change */
} dn_smp_keep_t;
typedef struct {
    uint32_t magic, count;
    dn_man_t t[DN_MAN_N];
    char uname[2][DN_NUSER][8];                     /* (0.22) the user sounds' names, REESE's and SUB's ("": USER n) */
    dn_smp_keep_t smp;                              /* (0.23) */
    struct {                                        /* (0.25) FM: the voice, packed, and its settings */
        uint8_t packed[DN_FM_PACKED];
        uint8_t opsw, voices, level, valid;         /* (valid: 0xA5) */
        int8_t mac[4];
        uint16_t preset, pad;
    } fm;
} dn_mans_t;
#define DN_MAN2_SIZE ((uint32_t)(sizeof(dn_mans_t) - sizeof(((dn_mans_t *)0)->fm)))   /* (0.23 / 0.24: no FM) */
static dn_mans_t dn_man X0X_POOL;
/* the object as 0.22 kept it (and 0.4-0.21, without the names): 24 loops' slices, then the names */
#define DN_MAN1_T 8u                                /* (where its loops start) */
#define DN_MAN1_NAMES (DN_MAN1_T + DN_MAN_N1 * sizeof(dn_man_t))
typedef char dn_man_fits[sizeof dn_man <= PLAT_STORE_MAX && DN_MAN1_NAMES + 128u <= sizeof dn_man ? 1 : -1];

static dn_man_t *dn_man_find(uint32_t key)
{
    uint32_t i;
    for (i = 0; i < dn_man.count && i < DN_MAN_N; i++)
        if (dn_man.t[i].key == key)
            return &dn_man.t[i];
    return 0;
}
/* the loop's manual slices into its voice (with the lock: the audio reads the table in use) */
static void dn_man_restore(dn_brk_t *b)
{
    const dn_man_t *m = dn_man_find(b->key);
    uint32_t i;
    dn_lock();
    b->n_man = 0;
    if (m && m->n) {
        for (i = 0; i < m->n && i < DN_BRK_MAXSL; i++)
            b->man_start[i] = (uint32_t)m->st[i] * 4u;
        b->n_man = m->n;
    }
    dn_brk_slices_apply(b);
    dn_unlock();
}
/* the voice's manual slices into the store (the newest last; full: the oldest goes) */
static void dn_man_keep(const dn_brk_t *b)
{
    dn_man_t *m = dn_man_find(b->key);
    uint32_t i;
    if (!m) {
        if (dn_man.count >= DN_MAN_N) {
            for (i = 0; i + 1u < DN_MAN_N; i++)
                dn_man.t[i] = dn_man.t[i + 1u];
            dn_man.count = DN_MAN_N - 1u;
        }
        m = &dn_man.t[dn_man.count++];
        m->key = b->key;
    }
    m->n = b->n_man;
    for (i = 0; i < b->n_man; i++)
        m->st[i] = (uint16_t)(b->man_start[i] / 4u);
}

/* the breaks page (web/breaks) reads and writes a loop's manual slices over SysEx (src/hardware: ED_SLICE_*) */
uint32_t dn_app_slices_get(uint32_t key, uint32_t *starts)
{
    const dn_man_t *m = dn_man_find(key);
    uint32_t i;
    if (!m)
        return 0;
    for (i = 0; i < m->n && i < DN_BRK_MAXSL; i++)
        starts[i] = (uint32_t)m->st[i] * 4u;
    return m->n;
}
static uint32_t dn_sl_old[DN_BRK_MAXSL];         /* the starts before a chop edit (to remap the steps) */
static void dn_sl_show(const dn_brk_t *b);
static void dn_sl_fix(void);
static void dn_chops_remap(uint32_t key, const uint32_t *old, uint32_t old_n);
static void dn_mark(void);
static void dn_num(char *o, int32_t v);
static uint32_t str_len_dn(const char *s);
static void dn_say(const char *m);
static void dn_chop_key(uint32_t c, uint32_t at);
static void dn_cat(char *o, const char *s);
#define DN_TAP_LATENCY_MS 12u                       /* what is heard is this far behind the decoder: a tap's place */
static void dn_step_drop(uint32_t to);

int dn_app_slices_set(uint32_t key, uint32_t n, const uint32_t *starts)
{
    dn_brk_t tmp;                                   /* (only key, n_man, man_start are read by dn_man_keep) */
    uint32_t i, k;
    if (n > DN_BRK_MAXSL)
        return 1;
    for (i = 1; i < n; i++)
        if (starts[i] <= starts[i - 1u])
            return 2;                               /* in order, or not at all */
    tmp.key = key;
    tmp.n_man = (uint8_t)n;
    for (i = 0; i < n; i++)
        tmp.man_start[i] = starts[i];
    dn_man_keep(&tmp);
    {
        uint32_t old_n = 0;                         /* the steps keep their audio (the nearest new chop) */
        for (k = 0; k < DN_NBRK; k++)
            if (dn_bk[k].ok && dn_bk[k].key == key && !old_n) {
                old_n = dn_bk[k].nslices;
                for (i = 0; i < old_n; i++)
                    dn_sl_old[i] = dn_bk[k].start[i];
            }
        for (k = 0; k < DN_NBRK; k++)               /* a loop playing now: its slices, and MANUAL */
            if (dn_bk[k].ok && dn_bk[k].key == key) {
                dn_bk[k].smode = n ? 2u : 1u;
                dn_man_restore(&dn_bk[k]);
            }
        if (old_n) {
            dn_lock();
            dn_chops_remap(key, dn_sl_old, old_n);
            dn_unlock();
            dn_mark();
        }
        dn_sl_fix();
    }
    dn_man.magic = DN_MAN_MAGIC;
    dn.redraw |= DN_RD_ALL;
    {                                               /* the saved sound gets MANUAL for those tracks too (only that:
                                                     * other edits not saved yet stay unsaved) */
        dn_save_t *sv = &dn_sv;
        if (plat_store_load(OBJ_SOUND, sv, sizeof *sv) == (int)sizeof *sv && sv->magic == DN_SAVE_MAGIC) {
            for (k = 0; k < DN_NBRK; k++)
                if (dn_bk[k].key == key)
                    sv->brk2[k].smode = dn_bk[k].smode;
            dn_store(OBJ_SOUND, sv, sizeof *sv);
        }
    }
    return dn_store(OBJ_SONG, &dn_man, sizeof dn_man) ? 3 : 0;
}

static void dn_brk_select(uint32_t k, uint32_t src)
{
    dn_loop_t L;
    if (src >= dn_src_count() || !dn_src_get(src, &L)) {
        src = 0;                                    /* gone (the slots were rewritten): the first loop, or none */
        if (!dn_src_get(0, &L)) {
            uint32_t j;                             /* (0.29) no loops at all: the break is silent until one comes */
            dn_lock();
            dn_bk[k].ok = 0;
            dn_bk[k].on = 0;
            dn_unlock();
            for (j = 0; j < sizeof dn_bk[k].loop.name; j++)
                dn_bk[k].loop.name[j] = 0;
            dn_bk_src[k] = 0;
            return;
        }
    }
    dn_lock();
    dn_bk[k].ok = 0;
    dn_bk[k].on = 0;
    dn_unlock();
    dn_brk_load(&dn_bk[k], &L);
    dn_man_restore(&dn_bk[k]);                      /* (the slices set by hand for this loop, if any) */
    dn_brk_tempo(&dn_bk[k], dn_sq.bpm);
    dn_bk_src[k] = (uint8_t)src;
}
/* the sample slots are about to change (an upload): nothing may read them until dn_app_slots_changed(1) */
/* SAMPLER's samples (0.23): each pad's and zone's sample from the project's store, with its checkpoints (shared by
 * the pads and zones playing the same sample). remap: first find each by its identity (the slots changed, or a project
 * loaded: a sample keeps its pads though its place in the list moved). Main loop; the audio sees no samples while it
 * runs (a short lock each side). */
static void dn_sm_resolve(int remap)
{
    static dn_smp_src_t ns[DN_SMP_NSRC];
    uint32_t i, j, used = 0, nsrc = dn_src_count();
    for (i = 0; i < DN_SMP_NSRC; i++) {
        int16_t *sp = i < DN_SMP_PADS ? &dn_sm.p.pad[i][PD_SAMPLE] : &dn_sm.p.zone[i - DN_SMP_PADS][ZN_SAMPLE];
        dn_loop_t L;
        uint8_t *z = (uint8_t *)&ns[i];
        for (j = 0; j < sizeof ns[i]; j++)
            z[j] = 0;
        if (remap && dn_sm_key[i] && *sp >= 0) {
            if ((uint32_t)*sp >= nsrc || !dn_src_get((uint32_t)*sp, &L) || dn_brk_key(&L) != dn_sm_key[i])
                for (j = 0; j < nsrc; j++)
                    if (dn_src_get(j, &L) && dn_brk_key(&L) == dn_sm_key[i]) {
                        *sp = (int16_t)j;
                        break;
                    }
        }
        if (*sp < 0 || (uint32_t)*sp >= nsrc || !dn_src_get((uint32_t)*sp, &L) || !L.adpcm)
            continue;
        dn_sm_key[i] = dn_brk_key(&L);
        for (j = 0; j < i; j++) {                   /* (the same sample as an earlier pad / zone: its checkpoints) */
            const int16_t *o = j < DN_SMP_PADS ? &dn_sm.p.pad[j][PD_SAMPLE] : &dn_sm.p.zone[j - DN_SMP_PADS][ZN_SAMPLE];
            if (*o == *sp && ns[j].adpcm) {
                ns[i] = ns[j];
                break;
            }
        }
        if (!ns[i].adpcm) {
            ns[i].adpcm = L.adpcm, ns[i].n = L.n, ns[i].rate = L.rate;
            ns[i].ck = dn_sm_ck + used;
            ns[i].nck = dn_smp_checkpoints(L.adpcm, L.n, dn_sm_ck + used, DN_SMCK - used);
            used += ns[i].nck;
        }
    }
    dn_lock();
    dn_smp_cut(&dn_sm);
    for (i = 0; i < DN_SMP_NSRC; i++)
        dn_sm.src[i] = ns[i];
    for (i = 0; i < DN_SMP_ZONES; i++)
        dn_smp_zone_prepare(&dn_sm, i);
    dn_unlock();
}

void dn_app_slots_changed(int done)
{
    dn_flash_t = plat_ms();                         /* (the editor writing samples to flash) */
    uint32_t k;
    if (done) {
        dn_sm_resolve(1);
    } else {                                        /* (SAMPLER lets go of the slots before they change) */
        dn_lock();
        dn_smp_cut(&dn_sm);
        for (k = 0; k < DN_SMP_NSRC; k++)
            dn_sm.src[k].adpcm = 0, dn_sm.src[k].nck = 0;
        dn_unlock();
    }
    for (k = 0; k < DN_NBRK; k++)
        {                                           /* (0.29: every loop is a slot's) */
            if (done) {
                dn_brk_select(k, dn_bk_src[k]);
            } else {
                dn_lock();
                dn_bk[k].ok = 0;
                dn_bk[k].on = 0;
                dn_unlock();
            }
        }
    dn.redraw |= DN_RD_ALL;
}
/* the break the BRK pages show: the selected track's, or the last one shown */
static uint32_t dn_bsel(void)
{
    if (dn.bk_force)
        return dn.bk_force - 1u;
    return DN_IS_BRK(dn.trk) ? (dn.bk_last = (uint8_t)DN_BK(dn.trk)) : dn.bk_last;
}

/* the FX page's dial under KNOB k: none where the effect has no value */
static int dn_fx_pi(uint32_t id, uint32_t *slot);
static uint32_t dn_fx_dial(uint32_t k)
{
    uint32_t id = DN_FX_PAGE[dn.fx_view % 5u][k], slot;
    int pi = dn.fx_view ? dn_fx_pi(id, &slot) : -1;
    if (pi >= 0 && dn_fx_pname(&dn_fxp[slot], (uint32_t)pi)[0] == '-')
        return DN_PX_NONE;
    return id;
}
/* the dial under KNOB k on this page */
static uint32_t dn_dial(uint32_t k)
{
    return dn.page == DN_PG_HOME ? DN_PX_NONE : dn.page == DN_PG_BRK ? DN_BRK_PAGE[k] : dn.page == DN_PG_BRK2 ? DN_BRK2_PAGE[k]
         : dn.page == DN_PG_SLICE ? DN_SLICE_PAGE[k] : dn.page == DN_PG_SET ? DN_SET_PAGE[k]
         : dn.page == DN_PG_LFO ? DN_LFO_PAGE[dn.lfo_view & 1u][k] : dn.page == DN_PG_MIDI ? DN_MIDI_PAGE[k]
         : dn.page == DN_PG_SOSC2 ? DN_SUB_OSC2[k] : dn.page == DN_PG_DIRT ? DN_SUB_DIRT[k]
         : dn.page == DN_PG_FENV ? (DN_IS_SUB(dn.trk) ? DN_PX_SFE : DN_PX_RFE) + k
         : dn.page < DN_NPAGES && DN_IS_SUB(dn.trk) ? DN_SUB_PAGE[dn.page][k]
         : dn.page < DN_NPAGES && DN_IS_SMP(dn.trk) ? DN_SMP_PG[dn_sm.p.mode ? 1 : 0][dn.page][k]
         : dn.page < DN_NPAGES && DN_IS_FM(dn.trk) ? DN_FM_PG[(dn.fm_alt >> dn.page) & 1u][dn.page][k]
         : dn.page == DN_PG_PROJ ? DN_PROJ_PAGE[k]
         : dn.page == DN_PG_FX ? dn_fx_dial(k)
         : DN_PAGE[dn.page % DN_NPAGES][k];
}

/* ------------------------------------------------------------------------------------------------ values --- */
/* the LFO values: the selected LFO's (the LFO page), or LFO 1's (MOD's shortcuts) */
#define DN_IS_LPX(id) ((id) >= DN_PX_LSHAPE && (id) <= DN_PX_L1DEPTH)
/* the LFO the LFO page shows: the synth's set (by the track's, or the editor's choice), its 1-4 */
#define DN_LBASE (dn.lfo_force ? (dn.lfo_force - 1u) * DN_NLFO : DN_IS_SUB(dn.trk) ? DN_NLFO : 0u)
#define DN_LSEL (DN_LBASE + dn.lfo_sel % DN_NLFO)
static dn_lfo_p_t *dn_lfo_px(uint32_t *id)
{
    if (*id == DN_PX_L1RATE || *id == DN_PX_L1DEPTH) {
        *id = *id == DN_PX_L1RATE ? DN_PX_LRATE : DN_PX_LDEPTH;
        return &dn_lp[0];
    }
    return &dn_lp[DN_LSEL];
}
static int32_t dn_lfo_get(uint32_t id)
{
    const dn_lfo_p_t *l = dn_lfo_px(&id);
    switch (id) {
    case DN_PX_LSHAPE: return l->shape;
    case DN_PX_LRATE: return l->rate;
    case DN_PX_LDEST: return l->dest;
    case DN_PX_LDEPTH: return l->depth;
    case DN_PX_LSYNC: return l->sync;
    case DN_PX_LMODE: return l->mode;
    case DN_PX_LPHASE: return l->phase;
    default: return l->smooth;
    }
}
static int32_t dn_lfo_lim(uint32_t id, int hi)
{
    const dn_lfo_p_t *l = dn_lfo_px(&id);
    switch (id) {
    case DN_PX_LSHAPE: return hi ? DN_LS_N - 1 : 0;
    case DN_PX_LRATE: return hi ? (l->sync ? (int32_t)DN_LFO_NDIV - 1 : 127) : 0;
    case DN_PX_LDEST: return hi ? DN_LD_N - 1 : 0;
    case DN_PX_LDEPTH: return hi ? 63 : -64;
    case DN_PX_LSYNC: return hi ? 1 : 0;
    case DN_PX_LMODE: return hi ? DN_LM_N - 1 : 0;
    default: return hi ? 127 : 0;
    }
}
static void dn_lfo_set(uint32_t id, int32_t v)      /* (inside the lock) */
{
    dn_lfo_p_t *l = dn_lfo_px(&id);
    switch (id) {
    case DN_PX_LSHAPE:
        if (v == DN_LS_DRAW && l->shape != DN_LS_DRAW && l->pad[0] == 0u) {
            l->pad[0] = 1;                          /* (the first DRAW: the points start as the shape it was) */
            dn_lfo_draw_shape(l, l->shape);
        }
        l->shape = (uint8_t)v;
        break;
    case DN_PX_LRATE: l->rate = (uint8_t)v; break;
    case DN_PX_LDEST: l->dest = (uint8_t)v; break;
    case DN_PX_LDEPTH: l->depth = (int8_t)v; break;
    case DN_PX_LSYNC:                               /* synced: 1/4; free: ~1 Hz */
        l->sync = (uint8_t)v;
        l->rate = v ? 7u : 64u;
        break;
    case DN_PX_LMODE: l->mode = (uint8_t)v; break;
    case DN_PX_LPHASE: l->phase = (uint8_t)v; break;
    default: l->smooth = (uint8_t)v; break;
    }
}
static void dn_lfo_text(uint32_t id, int32_t v, char out[8])
{
    const dn_lfo_p_t *l = dn_lfo_px(&id);
    const char *w = 0;
    uint32_t i;
    switch (id) {
    case DN_PX_LSHAPE: w = DN_LFO_SHAPE_NAME[dn_clamp(v, 0, DN_LS_N - 1)]; break;
    case DN_PX_LDEST: w = DN_LD_NAME[dn_clamp(v, 0, DN_LD_N - 1)]; break;
    case DN_PX_LSYNC: w = v ? "SYNC" : "FREE"; break;
    case DN_PX_LMODE: w = DN_LFO_MODE_NAME[dn_clamp(v, 0, DN_LM_N - 1)]; break;
    case DN_PX_LRATE:
        if (l->sync) {
            w = DN_LFO_DIV_NAME[dn_clamp(v, 0, DN_LFO_NDIV - 1)];
        } else {                                    /* 0.05 .. 20 Hz, log (the table's) */
            float hz = 0.05f;
            int32_t c;
            for (i = 0; i < (uint32_t)v; i++)
                hz *= 1.048309f;                    /* 400 ^ (1 / 127) */
            c = (int32_t)(hz * 100.0f + 0.5f);
            if (c < 1000) {
                out[0] = (char)('0' + c / 100), out[1] = '.', out[2] = (char)('0' + c / 10 % 10);
                out[3] = (char)('0' + c % 10), out[4] = 'H', out[5] = 'z', out[6] = 0;
            } else {
                c = (c + 5) / 10;
                dn_num(out, c / 10);
                i = (uint32_t)str_len_dn(out);
                out[i] = '.', out[i + 1u] = (char)('0' + c % 10), out[i + 2u] = 'H', out[i + 3u] = 'z', out[i + 4u] = 0;
            }
            return;
        }
        break;
    case DN_PX_LDEPTH: {
        char t[8];
        dn_num(t, v < 0 ? -v : v);
        out[0] = v < 0 ? '-' : '+';
        for (i = 0; t[i] && i < 5u; i++)
            out[i + 1u] = t[i];
        out[i + 1u] = 0;
        return;
    }
    case DN_PX_LPHASE:
        dn_num(out, v * 360 / 128);
        return;
    default:
        dn_num(out, v);
        return;
    }
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}

/* ---- the project's values: PROJECT (the track, its instrument) and FX (the sends, returns, the two slots) ---- */
static int dn_fx_fits(uint32_t a, uint32_t b) { return dn_fx_ram(a) + dn_fx_ram(b) <= DN_FX_ARENA; }
static void dn_fx_layout(void)                      /* (inside the lock) the arena: FX A's memory, then FX B's; MASSIVE
                                                     * takes what the other leaves (two: half each) */
{
    uint8_t *m = (uint8_t *)dn_fx_mem;
    uint32_t ta = dn_fxp[0].type, tb = dn_fxp[1].type;
    uint32_t a = (dn_fx_ram(ta) + 3u) & ~3u, b = (dn_fx_ram(tb) + 3u) & ~3u;
    if (dn_fx_flex(ta) && dn_fx_flex(tb))
        a = b = (DN_FX_ARENA / 2u) & ~3u;
    else if (dn_fx_flex(ta))
        a = DN_FX_ARENA - b;
    else if (dn_fx_flex(tb))
        b = DN_FX_ARENA - a;
    dn_fx_init(&dn_fxs[0], ta, a ? m : 0, a);
    dn_fx_init(&dn_fxs[1], tb, b ? m + a : 0, b);
}
/* an FX value's slot and its place among the effect's values (-1: the effect itself); -2: not an FX value */
static int dn_fx_pi(uint32_t id, uint32_t *slot)
{
    if (id >= DN_PX_FXA && id <= DN_PX_FXB + 3u) {
        *slot = id >= DN_PX_FXB;
        return (int)((id - DN_PX_FXA) % 4u) - 1;
    }
    if (id >= DN_PX_FXA2 && id <= DN_PX_FXB2 + 3u) {
        *slot = id >= DN_PX_FXB2;
        return 3 + (int)((id - DN_PX_FXA2) % 4u);
    }
    *slot = 0;
    return -2;
}
static void dn_fx_check(uint32_t k)                 /* an effect's values within their ranges */
{
    uint32_t i;
    if (dn_fxp[k].type >= DN_FX_N)
        dn_fx_default(&dn_fxp[k], DN_FX_OFF);
    for (i = 0; i < DN_FX_NP; i++)
        dn_fxp[k].p[i] = (uint8_t)dn_clamp(dn_fxp[k].p[i], 0, dn_fx_pmax(&dn_fxp[k], i));
}
static void dn_eng_set(uint32_t t, uint32_t e);
static void dn_legato_fix(void);
static void dn_goto(uint32_t page);
static const char *dn_inst_name(uint32_t t);
static int32_t dn_pj_get(uint32_t id)
{
    uint32_t t = dn.trk % DN_SEQ_TRACKS;
    if (id == DN_PX_PTRK)
        return (int32_t)t;
    if (id == DN_PX_PENG)
        return dn.peng != 0xFFu ? dn.peng : dn_eng[t];
    if (id == DN_PX_SENDA || id == DN_PX_SENDB)
        return dn_send[id - DN_PX_SENDA][t];
    if (id == DN_PX_RETA || id == DN_PX_RETB)
        return dn_ret[id - DN_PX_RETA];
    {
        uint32_t k;
        int i = dn_fx_pi(id, &k);
        return i < 0 ? dn_fxp[k].type : dn_fxp[k].p[i];
    }
}
static int32_t dn_pj_lim(uint32_t id, int hi)
{
    if (!hi)
        return 0;
    if (id == DN_PX_PTRK)
        return DN_SEQ_TRACKS - 1;
    if (id == DN_PX_PENG)
        return DN_E_N - 1;
    if (id < DN_PX_FXA)
        return 127;
    {
        uint32_t k;
        int i = dn_fx_pi(id, &k);
        return i < 0 ? DN_FX_N - 1 : dn_fx_pmax(&dn_fxp[k], (uint32_t)i);
    }
}
static const char *dn_pj_name(uint32_t id)
{
    static const char *const N[6] = {"TRACK", "INSTR", "SEND A", "SEND B", "RETURN A", "RETURN B"};
    uint32_t k;
    int i;
    if (id < DN_PX_FXA)
        return N[id - DN_PX_PTRK];
    i = dn_fx_pi(id, &k);
    if (i < 0)
        return k ? "FX B" : "FX A";
    return dn_fx_pname(&dn_fxp[k], (uint32_t)i);
}
static void dn_pj_text(uint32_t id, int32_t v, char out[8])
{
    const char *w = 0;
    uint32_t i;
    if (id == DN_PX_PTRK) {
        dn_num(out, v + 1);
        return;
    }
    if (id == DN_PX_PENG)
        w = DN_E_NAME[dn_clamp(v, 0, DN_E_N - 1)];
    else if (id >= DN_PX_FXA) {
        uint32_t k;
        int pi = dn_fx_pi(id, &k);
        if (pi < 0)
            w = DN_FX_NAME[dn_clamp(v, 0, DN_FX_N - 1)];
        else {                                      /* (the value as the effect reads it: v may be the one turned to) */
            dn_fx_p_t f = dn_fxp[k];
            f.p[pi] = (uint8_t)v;
            dn_fx_ptext(&dn_fxs[k], &f, (uint32_t)pi, dn_sq.bpm, out);
            return;
        }
    }
    if (!w) {
        dn_num(out, v);
        return;
    }
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}
static void dn_pj_set(uint32_t id, int32_t v)
{
    uint32_t t = dn.trk % DN_SEQ_TRACKS;
    if (id == DN_PX_PTRK) {                         /* (choosing a track changes nothing kept) */
        dn.trk = (uint8_t)v;
        dn.peng = 0xFF;
        dn.view = 0;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (id == DN_PX_PENG) {                         /* chosen, not yet in: FX puts it on the track */
        dn.peng = (uint8_t)v == dn_eng[t] ? 0xFFu : (uint8_t)v;
        if (dn.peng != 0xFFu)
            dn_say("FX: CHANGE IT");
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (id == DN_PX_SENDA || id == DN_PX_SENDB)
        dn_send[id - DN_PX_SENDA][t] = (uint8_t)v;
    else if (id == DN_PX_RETA || id == DN_PX_RETB)
        dn_ret[id - DN_PX_RETA] = (uint8_t)v;
    else {
        uint32_t k;
        int i = dn_fx_pi(id, &k);
        dn_fx_p_t *f = &dn_fxp[k];
        if (i < 0) {                                /* a new effect: its memory, or the next one along that fits
                                                     * (0.17: a DELAY that didn't fit beside the other slot's DELAY
                                                     * stopped the knob, so nothing after it could be reached) */
            uint32_t other = dn_fxp[!k].type;
            int32_t dir = v > (int32_t)f->type ? 1 : -1;
            while (!dn.fx_exact && v >= 0 && v < (int32_t)DN_FX_N &&
                   !dn_fx_fits(k ? other : (uint32_t)v, k ? (uint32_t)v : other))
                v += dir;                           /* (the editor's choice is exact: refused, not moved on) */
            if (dn.fx_exact && !dn_fx_fits(k ? other : (uint32_t)v, k ? (uint32_t)v : other))
                v = -1;
            if (v < 0 || v >= (int32_t)DN_FX_N || (uint32_t)v == f->type) {
                dn_say("NO MEMORY: FX OFF FIRST");
                dn.redraw |= DN_RD_ALL;
                return;
            }
            dn_lock();
            dn_fx_default(f, (uint32_t)v);
            dn_fx_layout();
            dn_unlock();
        } else {
            dn_fx_value(f, (uint32_t)i, (uint32_t)v);
        }
    }
    dn_mark();
}

/* SAMPLER's values (0.23): the mode, the pad / zone shown, and theirs */
static int16_t *dn_sm_val(uint32_t id)
{
    if (id >= DN_PX_KY0)
        return &dn_sm.p.key[id - DN_PX_KY0];
    if (id >= DN_PX_ZN0)
        return &dn_sm.p.zone[dn.smp_zone % DN_SMP_ZONES][id - DN_PX_ZN0];
    return &dn_sm.p.pad[dn.smp_pad % DN_SMP_PADS][id - DN_PX_PD0];
}
static int32_t dn_sm_get(uint32_t id)
{
    return id == DN_PX_SMODE ? dn_sm.p.mode : id == DN_PX_SPAD ? dn.smp_pad : id == DN_PX_SZONE ? dn.smp_zone
         : *dn_sm_val(id);
}
static int32_t dn_sm_lim(uint32_t id, int hi)
{
    if (id == DN_PX_SMODE)
        return hi ? 1 : 0;
    if (id == DN_PX_SPAD || id == DN_PX_SZONE)
        return hi ? (int32_t)(id == DN_PX_SPAD ? DN_SMP_PADS : DN_SMP_ZONES) - 1 : 0;
    if (id == DN_PX_PD0 + PD_SAMPLE || id == DN_PX_ZN0 + ZN_SAMPLE)
        return hi ? (int32_t)dn_src_count() - 1 : -1;
    if (id >= DN_PX_KY0)
        return hi ? dn_smp_ky_max(id - DN_PX_KY0) : dn_smp_ky_min(id - DN_PX_KY0);
    if (id >= DN_PX_ZN0)
        return hi ? dn_smp_zn_max(id - DN_PX_ZN0) : dn_smp_zn_min(id - DN_PX_ZN0);
    return hi ? dn_smp_pd_max(id - DN_PX_PD0) : dn_smp_pd_min(id - DN_PX_PD0);
}
static const char *dn_sm_name(uint32_t id)
{
    return id == DN_PX_SMODE ? "MODE" : id == DN_PX_SPAD ? "PAD" : id == DN_PX_SZONE ? "ZONE"
         : id >= DN_PX_KY0 ? dn_smp_ky_name(id - DN_PX_KY0) : id >= DN_PX_ZN0 ? dn_smp_zn_name(id - DN_PX_ZN0)
         : dn_smp_pd_name(id - DN_PX_PD0);
}
static void dn_sm_text(uint32_t id, int32_t v, char out[8])
{
    const char *w = 0;
    uint32_t i;
    if (id == DN_PX_SMODE)
        w = v ? "KEYS" : "1-SHOT";
    else if (id == DN_PX_SPAD || id == DN_PX_SZONE) {
        dn_num(out, v + 1);
        return;
    } else if (id == DN_PX_PD0 + PD_SAMPLE || id == DN_PX_ZN0 + ZN_SAMPLE) {
        static dn_loop_t L;
        w = v < 0 ? "OFF" : dn_src_get((uint32_t)v, &L) ? L.name : "?";
    } else {
        dn_smp_text(id >= DN_PX_KY0 ? 2u : id >= DN_PX_ZN0 ? 1u : 0u,
                    id >= DN_PX_KY0 ? id - DN_PX_KY0 : id >= DN_PX_ZN0 ? id - DN_PX_ZN0 : id - DN_PX_PD0, v, out);
        return;
    }
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}
static void dn_sm_set(uint32_t id, int32_t v)
{
    if (id == DN_PX_SPAD) {
        dn.smp_pad = (uint8_t)v;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (id == DN_PX_SZONE) {
        dn.smp_zone = (uint8_t)v;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    dn_lock();
    if (id == DN_PX_SMODE) {
        dn_smp_cut(&dn_sm);
        dn_sm.p.mode = (uint8_t)v;
    } else {
        *dn_sm_val(id) = (int16_t)v;
        if (id == DN_PX_ZN0 + ZN_LSTART || id == DN_PX_ZN0 + ZN_LEND)
            dn_smp_zone_prepare(&dn_sm, dn.smp_zone % DN_SMP_ZONES);
    }
    dn_unlock();
    if (id == DN_PX_PD0 + PD_SAMPLE || id == DN_PX_ZN0 + ZN_SAMPLE) {
        dn_sm_key[id == DN_PX_PD0 + PD_SAMPLE ? dn.smp_pad % DN_SMP_PADS : DN_SMP_PADS + dn.smp_zone % DN_SMP_ZONES] = 0;
        dn_sm_resolve(0);                           /* (a new sample: its checkpoints) */
    }
    if (id == DN_PX_SMODE)
        dn_legato_fix();
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}
/* FM's values (0.25): the voice's (dn_fm_base: edited, then the macros on top into the voice played), the macros,
 * the operator shown and its values, LEVEL and VOICES */
static const uint8_t DN_FM_VIDX[DN_PX_FO0 - DN_PX_FOKS] = {DN_FMV_OKS, DN_FMV_LFS, DN_FMV_LFD, DN_FMV_LPMD,
                                                           DN_FMV_LAMD, DN_FMV_LFW, DN_FMV_LFKS, DN_FMV_LPMS,
                                                           DN_FMV_TRNSP};
static int32_t dn_fm_idx(uint32_t id)                 /* the voice's value under id (-1: not one of the voice's) */
{
    if (id >= DN_PX_FO0)
        return 21 * (5 - (int32_t)(dn.fm_op % 6u)) + (int32_t)(id - DN_PX_FO0);   /* (OP1 is the last) */
    if (id == DN_PX_FALG)
        return DN_FMV_ALG;
    if (id == DN_PX_FFB)
        return DN_FMV_FB;
    if (id >= DN_PX_FOKS)
        return DN_FM_VIDX[id - DN_PX_FOKS];
    return -1;
}
static int8_t *dn_fm_macro(uint32_t id)
{
    return id == DN_PX_FBRIGHT ? &dn_fm_mac.bright : id == DN_PX_FTIME ? &dn_fm_mac.time
         : id == DN_PX_FATK ? &dn_fm_mac.attack : &dn_fm_mac.release;
}
static int32_t dn_fm_get(uint32_t id)
{
    int32_t i = dn_fm_idx(id);
    if (i >= 0)
        return dn_fm_base[i];
    return id == DN_PX_FOP ? dn.fm_op : id == DN_PX_FLEVEL ? dn_fmx.level : id == DN_PX_FVOICES ? dn_fmx.voices
         : *dn_fm_macro(id);
}
static int32_t dn_fm_lim(uint32_t id, int hi)
{
    int32_t i = dn_fm_idx(id);
    if (i >= 0)
        return hi ? dn_fm_vmax((uint32_t)i) : 0;
    return id == DN_PX_FOP ? (hi ? 5 : 0) : id == DN_PX_FLEVEL ? (hi ? 127 : 0) : id == DN_PX_FVOICES ? (hi ? 6 : 1)
         : hi ? 50 : -50;
}
static const char *dn_fm_name(uint32_t id)
{
    int32_t i = dn_fm_idx(id);
    if (id == DN_PX_FALG)
        return "ALGO";
    if (i >= 0)
        return dn_fm_vname((uint32_t)i);
    return id == DN_PX_FOP ? "OP" : id == DN_PX_FLEVEL ? "LEVEL" : id == DN_PX_FVOICES ? "VOICES"
         : id == DN_PX_FBRIGHT ? "BRIGHT" : id == DN_PX_FTIME ? "TIME" : id == DN_PX_FATK ? "ATTACK" : "RELEASE";
}
/* an operator's frequency: its ratio (COARSE 0 is 0.50) or, FIXED, Hz (10 ^ (COARSE & 3 + FINE / 100)) */
static void dn_fm_freq_text(char out[8])
{
    const uint8_t *o = dn_fm_base + 21u * (5u - dn.fm_op % 6u);
    int32_t c = o[DN_FMO_COARSE], f = o[DN_FMO_FINE], v, i = 0;
    char t[8];
    if (o[DN_FMO_MODE]) {
        float hz = fm_powf(10.f, (float)(c & 3) + (float)f * 0.01f);
        v = (int32_t)(hz + 0.5f);
        if (v >= 1000) {
            dn_num(t, v / 1000);
            dn_cat(t, ".");
            out[0] = 0;
            dn_cat(out, t);
            t[0] = (char)('0' + v / 100 % 10), t[1] = 'k', t[2] = 0;
            dn_cat(out, t);
        } else {
            dn_num(out, v);
            dn_cat(out, "Hz");
        }
        return;
    }
    v = (c ? c * 2 : 1) * (100 + f);                /* (the ratio x 200) */
    dn_num(t, v / 200);
    while (t[i])
        out[i] = t[i], i++;
    out[i++] = '.';
    v = (v % 200) / 2;
    out[i++] = (char)('0' + v / 10);
    out[i++] = (char)('0' + v % 10);
    out[i] = 0;
}
static void dn_fm_text(uint32_t id, int32_t v, char out[8])
{
    int32_t i = dn_fm_idx(id);
    if (id == DN_PX_FO0 + DN_FMO_COARSE || id == DN_PX_FO0 + DN_FMO_FINE) {
        dn_fm_freq_text(out);                       /* (COARSE and FINE: the frequency they make) */
        return;
    }
    if (i >= 0) {
        dn_fm_vtext((uint32_t)i, v, out);
        return;
    }
    if (id == DN_PX_FOP) {
        out[0] = 'O', out[1] = 'P', out[2] = (char)('1' + v), out[3] = 0;
        return;
    }
    if (id == DN_PX_FVOICES && v == 1) {
        out[0] = 0;
        dn_cat(out, "MONO");
        return;
    }
    out[0] = 0;
    if (id != DN_PX_FLEVEL && id != DN_PX_FVOICES && v > 0)
        dn_cat(out, "+");
    {
        char t[8];
        dn_num(t, v);
        dn_cat(out, t);
    }
}
/* the voice played: the voice with the macros on it (and the LFO set) */
static void dn_fm_sync(void)
{
    static uint8_t eff[DN_FM_PATCH];
    dn_fm_apply(dn_fm_base, &dn_fm_mac, eff);
    dn_lock();
    dn_fm_voice_load(&dn_fmx, eff);
    dn_unlock();
}
static void dn_fm_set(uint32_t id, int32_t v)
{
    int32_t i = dn_fm_idx(id);
    if (id == DN_PX_FOP) {
        dn.fm_op = (uint8_t)v;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (i >= 0)
        dn_fm_base[i] = (uint8_t)v;
    else if (id == DN_PX_FLEVEL)
        dn_fmx.level = (int16_t)v;
    else if (id == DN_PX_FVOICES)
        dn_fmx.voices = (uint8_t)v;
    else
        *dn_fm_macro(id) = (int8_t)v;
    dn_fm_sync();
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}
/* FM's voices for PRESETS: the factory's, then the project's (the DX7 voices sent from the editor) */
static uint32_t dn_fm_presets(void) { return dn_fm_factory_count() + dn_fmv_count(); }
static void dn_fm_load(uint32_t i)
{
    uint32_t nf = dn_fm_factory_count();
    if (i < nf) {
        dn_fm_factory(i, dn_fm_base);
    } else {
        const uint8_t *pk = dn_fmv_get(i - nf);
        if (!pk)
            return;
        dn_fm_unpack(pk, dn_fm_base);
    }
    dn.fm_preset = (uint16_t)i;
    dn_fm_sync();
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}
/* the EG tab names the operator its knobs edit: "OP3 EG", again "OP3 LVL" */
static const char *dn_fm_egtab(void)
{
    static char t[8];
    t[0] = 'O', t[1] = 'P', t[2] = (char)('1' + dn.fm_op % 6u), t[3] = ' ', t[4] = 0;
    dn_cat(t, (dn.fm_alt >> 2) & 1u ? "LVL" : "EG");
    return t;
}
/* the voice's name (its 10 characters, trailing spaces gone) */
static const char *dn_fm_vcname(void)
{
    static char nm[11];
    int32_t i;
    for (i = 0; i < 10; i++)
        nm[i] = dn_fm_base[DN_FMV_NAME + i] >= 32u && dn_fm_base[DN_FMV_NAME + i] < 127u
              ? (char)dn_fm_base[DN_FMV_NAME + i] : ' ';
    nm[10] = 0;
    for (i = 9; i >= 0 && nm[i] == ' '; i--)
        nm[i] = 0;
    return nm;
}

/* the filter envelopes' values (0.19): REESE's or SUB's fe[] */
static int16_t *dn_fe(uint32_t id) { return id >= DN_PX_SFE ? &dn_sb.fe[id - DN_PX_SFE] : &dn_rs.fe[id - DN_PX_RFE]; }
static int32_t dn_get(uint32_t id)
{
    if (DN_IS_MPX(id))
        return dn_sm_get(id);
    if (DN_IS_FPX(id))
        return dn_fm_get(id);
    if (DN_IS_FEX(id))
        return *dn_fe(id);
    if (DN_IS_SPX(id))
        return dn_sb.p[id - DN_PX_SUB0];
    if (DN_IS_PPX(id))
        return dn_pj_get(id);
    if (DN_IS_LPX(id))
        return dn_lfo_get(id);
    if (id == DN_PX_BPM)
        return dn_rs.bpm;
    if (id == DN_PX_OCT)
        return dn.oct;
    if (id == DN_PX_OVL)
        return DN_PAT_CUT(&dn_sq.pat) >> dn_syn_trk() & 1u;
    if (id == DN_PX_CLICK)
        return dn.click;
    if (id == DN_PX_CLKLVL)
        return dn.clk_lvl;
    if (id == DN_PX_CIN)
        return dn.cin;
    if (id == DN_PX_VSENS)
        return dn_rs.vsens;
    if (id == DN_PX_MSEL)
        return dn.midi_sel;
    if (id == DN_PX_MCHAN)
        return dn_midi_trk(dn.midi_sel) < 8u ? dn_sq.pat.tr[dn_midi_trk(dn.midi_sel)].chan : 0;
    if (id == DN_PX_MOVL)
        return dn_midi_trk(dn.midi_sel) < 8u ? DN_PAT_CUT(&dn_sq.pat) >> dn_midi_trk(dn.midi_sel) & 1u : 0;
    if (id == DN_PX_BSRC)
        return dn_bk_src[dn_bsel()];
    if (id == DN_PX_BPITCH)
        return dn_bk[dn_bsel()].pitch;
    if (id == DN_PX_BDECAY)
        return dn_bk[dn_bsel()].decay;
    if (id == DN_PX_BCRUSH)
        return dn_bk[dn_bsel()].crush;
    if (id == DN_PX_BCHOKE)
        return dn_bk[dn_bsel()].choke;
    if (id == DN_PX_BSMODE)
        return dn_bk[dn_bsel()].smode;
    if (id == DN_PX_BSTRETCH)
        return dn_bk[dn_bsel()].stretch;
    if (id == DN_PX_BLEVEL)
        return dn_bk[dn_bsel()].level;
    if (id == DN_PX_SLSEL)
        return dn.sl_sel;
    if (id == DN_PX_SLMOVE || id == DN_PX_SLFINE)
        return (int32_t)dn.sl_cur;
    if (id == DN_PX_SLADD)
        return dn.sl_zoom;
    return dn_rs.p[id];
}
static int32_t dn_min(uint32_t id)
{
    if (DN_IS_MPX(id))
        return dn_sm_lim(id, 0);
    if (DN_IS_FPX(id))
        return dn_fm_lim(id, 0);
    if (DN_IS_FEX(id))
        return 0;
    if (DN_IS_SPX(id))
        return dn_sub_param_min(id - DN_PX_SUB0);
    if (DN_IS_PPX(id))
        return dn_pj_lim(id, 0);
    if (DN_IS_LPX(id))
        return dn_lfo_lim(id, 0);
    if (id >= DN_PX_SLSEL)
        return 0;
    return id == DN_PX_BPM ? 60 : id == DN_PX_OVL || (id >= DN_PX_CLICK && id <= DN_PX_MOVL) ? 0 : id == DN_PX_OCT ? -3 : id == DN_PX_BPITCH ? -24 : id >= DN_PX_BSRC ? 0
         : reese_param_min(id);
}
static int32_t dn_max(uint32_t id)
{
    if (DN_IS_MPX(id))
        return dn_sm_lim(id, 1);
    if (DN_IS_FPX(id))
        return dn_fm_lim(id, 1);
    if (DN_IS_FEX(id))
        return 127;
    if (DN_IS_SPX(id))
        return dn_sub_param_max(id - DN_PX_SUB0);
    if (DN_IS_PPX(id))
        return dn_pj_lim(id, 1);
    if (DN_IS_LPX(id))
        return dn_lfo_lim(id, 1);
    if (id >= DN_PX_SLSEL)
        return id == DN_PX_SLSEL ? (int32_t)dn_bk[dn_bsel()].nslices - 1 : id == DN_PX_SLADD ? 6
             : (int32_t)dn_bk[dn_bsel()].loop.n;
    return id == DN_PX_BPM ? 200 : id == DN_PX_OVL || id == DN_PX_MOVL ? 1 : id == DN_PX_CLICK || id == DN_PX_CIN ? 2
         : id == DN_PX_CLKLVL || id == DN_PX_VSENS ? 127 : id == DN_PX_MSEL ? (dn_midi_n() ? (int32_t)dn_midi_n() - 1 : 0) : id == DN_PX_MCHAN ? 15 : id == DN_PX_OCT ? 3 : id == DN_PX_BSRC ? (dn_src_count() ? (int32_t)dn_src_count() - 1 : 0)
         : id == DN_PX_BPITCH ? 24 : id == DN_PX_BCHOKE ? 4 : id == DN_PX_BSMODE || id == DN_PX_BSTRETCH ? 2
         : id >= DN_PX_BDECAY ? 127 : reese_param_max(id);
}
static const char *dn_name(uint32_t id)
{
    static const char *const FN[4] = {"F.ATK", "F.DEC", "F.SUS", "F.REL"};
    if (DN_IS_MPX(id))
        return dn_sm_name(id);
    if (DN_IS_FPX(id))
        return dn_fm_name(id);
    if (DN_IS_FEX(id))
        return FN[(id - DN_PX_RFE) % 4u];
    static const char *const BN[8] = {"LOOP", "PITCH", "DECAY", "CRUSH", "CHOKE", "SLICES", "STRETCH", "LEVEL"};
    static const char *const SN[4] = {"CHOP", "CURSOR", "FINE", "ZOOM"};
    static const char *const LN[8] = {"SHAPE", "RATE", "DEST", "DEPTH", "SYNC", "MODE", "PHASE", "SMOOTH"};
    if (DN_IS_SPX(id))
        return dn_sub_param_name(id - DN_PX_SUB0);
    if (DN_IS_PPX(id))
        return dn_pj_name(id);
    if (id == DN_PX_L1RATE || id == DN_PX_L1DEPTH)
        return id == DN_PX_L1RATE ? "L1 RATE" : "L1 DEPTH";
    if (DN_IS_LPX(id))
        return LN[id - DN_PX_LSHAPE];
    if (id >= DN_PX_SLSEL)
        return SN[id - DN_PX_SLSEL];
    return id == DN_PX_BPM ? "BPM" : id == DN_PX_OVL ? "OVERLAP" : id == DN_PX_CLICK ? "CLICK"
         : id == DN_PX_CLKLVL ? "CLK LVL" : id == DN_PX_CIN ? "COUNT-IN" : id == DN_PX_VSENS ? "VEL SENS"
         : id == DN_PX_MSEL ? "MIDI" : id == DN_PX_MCHAN ? "CHANNEL" : id == DN_PX_MOVL ? "OVERLAP" : id == DN_PX_OCT ? "OCT" : id >= DN_PX_BSRC ? BN[id - DN_PX_BSRC]
         : reese_param_name(id);
}
static void dn_text(uint32_t id, int32_t v, char out[8])
{
    static const char *const ES[DN_ESYNC_N] = {"1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4",
                                               "1/2T", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
    if (((id == RP_ATK || id == RP_DEC || id == RP_REL) && dn_rs.esync) ||
        ((id == DN_S(SP_ATK) || id == DN_S(SP_DEC) || id == DN_S(SP_REL)) && dn_sb.esync) ||
        (DN_IS_FEX(id) && (id - DN_PX_RFE) % 4u != 2u && (id >= DN_PX_SFE ? dn_sb.esync : dn_rs.esync))) {
        const char *w = ES[dn_esync_idx(v)];        /* synced: the note length */
        uint32_t i;
        for (i = 0; w[i] && i < 7u; i++)
            out[i] = w[i];
        out[i] = 0;
        return;
    }
    if (DN_IS_SPX(id)) {
        dn_sub_param_text(id - DN_PX_SUB0, v, out);
        return;
    }
    if (DN_IS_MPX(id)) {
        dn_sm_text(id, v, out);
        return;
    }
    if (DN_IS_FPX(id)) {
        dn_fm_text(id, v, out);
        return;
    }
    if (DN_IS_PPX(id)) {
        dn_pj_text(id, v, out);
        return;
    }
    if (DN_IS_LPX(id)) {
        dn_lfo_text(id, v, out);
        return;
    }
    if (id == DN_PX_BSRC) {
        dn_loop_t L;
        uint32_t i;
        if (!dn_src_get((uint32_t)v, &L)) {         /* (0.29: no loops sent yet) */
            for (i = 0; i < 5u; i++)
                L.name[i] = "NONE"[i];
        }
        for (i = 0; i < 7u && L.name[i]; i++)
            out[i] = L.name[i];
        out[i] = 0;
        return;
    }
    if (id == DN_PX_SLSEL || id == DN_PX_SLADD) {   /* the chop's number; the zoom as 1x .. 64x */
        uint32_t i = 0;
        reese_param_text(RP_LEVEL, id == DN_PX_SLSEL ? v + 1 : 1 << v, out);
        if (id == DN_PX_SLADD) {
            while (out[i]) i++;
            out[i] = 'x', out[i + 1u] = 0;
        }
        return;
    }
    if (id == DN_PX_SLFINE) {                       /* FINE: the cursor from the selected chop, in samples */
        const dn_brk_t *b = &dn_bk[dn_bsel()];
        int32_t d = (int32_t)v - (int32_t)(dn.sl_sel < b->nslices ? b->start[dn.sl_sel] : 0u);
        char t[8];
        uint32_t i;
        dn_num(t, d < 0 ? -d : d);
        out[0] = d < 0 ? '-' : '+';
        for (i = 0; t[i] && i < 6u; i++)
            out[i + 1u] = t[i];
        out[i + 1u] = 0;
        return;
    }
    if (id == DN_PX_SLMOVE) {                       /* CURSOR: where it is, in ms into the loop */
        uint32_t r = dn_bk[dn_bsel()].loop.rate ? dn_bk[dn_bsel()].loop.rate : 22050u, i = 0;
        reese_param_text(RP_LEVEL, (int32_t)((uint32_t)v / (r / 1000u ? r / 1000u : 1u)), out);
        while (out[i] && i < 4u)
            i++;
        out[i] = 'm', out[i + 1u] = 's', out[i + 2u] = 0;
        return;
    }
    if (id == DN_PX_BCHOKE || id == DN_PX_BSMODE || id == DN_PX_BSTRETCH) {
        static const char *const T[3][5] = {{"OFF", "1", "2", "3", "4"}, {"GRID", "AUTO", "MANUAL"},
                                            {"REPITCH", "SLICE", "STRETCH"}};
        const char *t = T[id - DN_PX_BCHOKE][dn_clamp(v, 0, id == DN_PX_BCHOKE ? 4 : 2)];
        uint32_t i;
        for (i = 0; t[i] && i < 7u; i++)
            out[i] = t[i];
        out[i] = 0;
        return;
    }
    if (id == DN_PX_BDECAY && v >= 127) {
        out[0] = 'H', out[1] = 'O', out[2] = 'L', out[3] = 'D', out[4] = 0;
        return;
    }
    if (id == DN_PX_CLICK || id == DN_PX_CIN) {
        static const char *const C[2][3] = {{"OFF", "REC", "ON"}, {"OFF", "1 BAR", "2 BARS"}};
        const char *w = C[id == DN_PX_CIN][dn_clamp(v, 0, 2)];
        uint32_t i;
        for (i = 0; w[i]; i++)
            out[i] = w[i];
        out[i] = 0;
        return;
    }
    if (id == DN_PX_MSEL || id == DN_PX_MCHAN) {    /* MIDI 1..5; channel 1..16 */
        reese_param_text(RP_LEVEL, v + 1, out);
        return;
    }
    if (id == DN_PX_OVL || id == DN_PX_MOVL) {
        const char *w = v ? "CUT" : "SLIDE";
        uint32_t i;
        for (i = 0; w[i]; i++)
            out[i] = w[i];
        out[i] = 0;
        return;
    }
    if (id == DN_PX_BPM || id == DN_PX_OCT || id == DN_PX_BPITCH) {
        reese_param_text(RP_LEVEL, v, out);         /* (a plain number) */
        if (id == DN_PX_BPITCH && v > 0) {
            uint32_t i = 0;
            char t[8];
            reese_param_text(RP_LEVEL, v, t);
            out[0] = '+';
            for (; t[i]; i++)
                out[i + 1u] = t[i];
            out[i + 1u] = 0;
        }
        if (id == DN_PX_OCT && v > 0) {
            out[0] = '+';
            out[1] = (char)('0' + v);
            out[2] = 0;
        }
        return;
    }
    reese_param_text(id, v, out);
}
static void dn_set(uint32_t id, int32_t v)
{
    uint32_t k = dn_bsel();
    v = dn_clamp(v, dn_min(id), dn_max(id));
    if (v == dn_get(id))
        return;
    dn_rev++;
    if (DN_IS_PPX(id)) {
        dn_pj_set(id, v);
        return;
    }
    if (DN_IS_MPX(id)) {
        dn_sm_set(id, v);
        return;
    }
    if (DN_IS_FPX(id)) {
        dn_fm_set(id, v);
        return;
    }
    if (DN_IS_FEX(id)) {
        dn_lock();
        *dn_fe(id) = (int16_t)v;
        dn_unlock();
        dn.dirty = 1;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (DN_IS_SPX(id)) {
        dn_lock();
        dn_sb.p[id - DN_PX_SUB0] = (int16_t)v;
        dn_unlock();
        dn.dirty = 1;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (id == DN_PX_BSRC) {                         /* (outside the lock: reading the loop takes a few ms) */
        dn_brk_select(k, (uint32_t)v);
        dn.dirty = 1;
        dn.redraw |= DN_RD_ALL;
        return;
    }
    dn_lock();
    if (id == DN_PX_BPITCH)
        dn_bk[k].pitch = (int8_t)v;
    else if (id == DN_PX_BDECAY)
        dn_bk[k].decay = (uint8_t)v;
    else if (id == DN_PX_BCRUSH)
        dn_bk[k].crush = (uint8_t)v;
    else if (id == DN_PX_BCHOKE)
        dn_bk[k].choke = (uint8_t)v;
    else if (id == DN_PX_BSMODE) {
        dn_bk[k].smode = (uint8_t)v;
        dn_brk_slices_apply(&dn_bk[k]);             /* (inside the lock) */
    }
    else if (id == DN_PX_BSTRETCH) {
        dn_bk[k].stretch = (uint8_t)v;
        dn_brk_tempo(&dn_bk[k], dn_sq.bpm);         /* (the speeds differ by mode) */
    }
    else if (id == DN_PX_BLEVEL)
        dn_bk[k].level = (uint8_t)v;
    else if (id == DN_PX_BPM)
        dn_rs.bpm = dn_sq.bpm = (uint16_t)v;
    else if (id == DN_PX_OCT)
        dn.oct = (int8_t)v;
    else if (id == DN_PX_OVL) {
        uint32_t b = 1u << dn_syn_trk();
        DN_PAT_CUT(&dn_sq.pat) = (uint8_t)(v ? DN_PAT_CUT(&dn_sq.pat) | b : DN_PAT_CUT(&dn_sq.pat) & ~b);
    }
    else if (id == DN_PX_CLICK)
        dn.click = (uint8_t)v;
    else if (id == DN_PX_CLKLVL)
        dn.clk_lvl = (uint8_t)v;
    else if (id == DN_PX_CIN)
        dn.cin = (uint8_t)v;
    else if (id == DN_PX_VSENS)
        dn_rs.vsens = dn_sb.vsens = (uint8_t)v;     /* (both synths) */
    else if (id == DN_PX_MSEL) {
        dn.midi_sel = (uint8_t)v;
        if (dn_midi_trk(dn.midi_sel) < 8u)
            dn.trk = (uint8_t)dn_midi_trk(dn.midi_sel);   /* (the keys play it) */
    } else if (id == DN_PX_MCHAN) {
        if (dn_midi_trk(dn.midi_sel) < 8u)
            dn_sq.pat.tr[dn_midi_trk(dn.midi_sel)].chan = (uint8_t)v;
    } else if (id == DN_PX_MOVL) {
        if (dn_midi_trk(dn.midi_sel) < 8u) {
            uint32_t b = 1u << dn_midi_trk(dn.midi_sel);
            DN_PAT_CUT(&dn_sq.pat) = (uint8_t)(v ? DN_PAT_CUT(&dn_sq.pat) | b : DN_PAT_CUT(&dn_sq.pat) & ~b);
        }
    }
    else if (DN_IS_LPX(id))
        dn_lfo_set(id, v);
    else {
        dn_rs.p[id] = (int16_t)v;
        if (id == RP_LFLT)
            dn_lfo_legacy();                        /* (the editor's old filter LFO: LFO 1 now) */
    }
    dn_unlock();
    if (id == DN_PX_BPM || id == DN_PX_BPITCH)      /* the breaks' speed follows the tempo */
        for (k = 0; k < DN_NBRK; k++)
            dn_brk_tempo(&dn_bk[k], dn_sq.bpm);
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}

/* PRESETS: the factory sounds (the last INIT), then the user sounds USER 1-8 */
static int dn_strcmp_dn(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a != *b;
}
static const char *dn_sound_name(uint32_t set, uint32_t i)
{
    static char t[2][10];
    uint32_t nf = set ? dn_sub_preset_count() : reese_preset_count();
    if (i < nf)
        return set ? dn_sub_preset_name(i) : reese_preset_name(i);
    i -= nf;
    if (dn_user[set][i % DN_NUSER].used && dn_man.uname[set][i % DN_NUSER][0]) {   /* (0.22: its own name) */
        uint32_t j;
        for (j = 0; j < 8u; j++)
            t[set][j] = dn_man.uname[set][i % DN_NUSER][j];
        t[set][8] = 0;
        return t[set];
    }
    t[set][0] = 'U', t[set][1] = 'S', t[set][2] = 'E', t[set][3] = 'R', t[set][4] = ' ';
    t[set][5] = (char)('1' + i % DN_NUSER), t[set][6] = 0;
    if (!dn_user[set][i % DN_NUSER].used)
        t[set][6] = ' ', t[set][7] = '-', t[set][8] = 0;   /* (empty) */
    return t[set];
}
static void dn_load_preset(uint32_t i)
{
    uint32_t nf = reese_preset_count();
    if (i >= nf && !dn_user[0][(i - nf) % DN_NUSER].used) {
        dn.preset = (uint8_t)i;                     /* an empty user slot: shown (SAVE + PRESETS stores into it) */
        dn_say("EMPTY: SAVE+PRESETS STORES");
        dn.redraw |= DN_RD_ALL;
        return;
    }
    dn_lock();
    if (i < nf) {
        reese_load_preset(&dn_rs, i);
        dn_lfo_init_all();                          /* (a factory sound: its old filter LFO, if any, as LFO 1) */
    } else {
        dn_patch_load(0, &dn_user[0][(i - nf) % DN_NUSER]);
    }
    dn_unlock();
    dn.preset = (uint8_t)i;
    dn.dirty = 0;
    dn.redraw |= DN_RD_ALL;
}

static void dn_sub_preset(uint32_t i)
{
    uint8_t vs = dn_sb.vsens;
    uint32_t nf = dn_sub_preset_count();
    if (i >= nf && !dn_user[1][(i - nf) % DN_NUSER].used) {
        dn.sb_preset = (uint8_t)i;
        dn_say("EMPTY: SAVE+PRESETS STORES");
        dn.redraw |= DN_RD_ALL;
        return;
    }
    dn_lock();
    if (i < nf) {
        dn_sub_load_preset(&dn_sb, i);
        dn_sb.esync = 0;
        dn_lfo_init_set(1);                         /* (a factory sound: its LFOs off, but WOBBLE's) */
        if (!dn_strcmp_dn(dn_sub_preset_name(i), "WOBBLE")) {
            dn_lfo_p_t *l = &dn_lp[DN_NLFO];
            l->rate = 10, l->dest = DN_LD_SCUT, l->depth = 40, l->mode = DN_LM_RETRIG;   /* 1/8, on S CUT */
        }
    } else {
        dn_patch_load(1, &dn_user[1][(i - nf) % DN_NUSER]);
    }
    dn_sb.vsens = vs;
    dn_unlock();
    dn.sb_preset = (uint8_t)i;
    dn.dirty = 1;
    dn_rev++;
    dn.redraw |= DN_RD_ALL;
}

static void dn_save_fill(dn_save_t *sp)
{
#define s (*sp)
    uint32_t k;
    s.magic = DN_SAVE_MAGIC;
    s.preset = dn.preset;
    s.page = dn.page;
    s.oct = dn.oct;
    s.pad = 0;
    s.bpm = dn_rs.bpm;
    s.pad2 = 0;
    for (k = 0; k < RP_COUNT; k++)
        s.p[k] = dn_rs.p[k];
    for (k = 0; k < DN_NBRK; k++) {
        s.brk[k].src = (uint8_t)(dn_bk_src[k] | 0x80u);   /* (0.29: top bit, counted without the built-in loops) */
        s.brk[k].pitch = dn_bk[k].pitch;
        s.brk[k].decay = dn_bk[k].decay;
        s.brk[k].crush = dn_bk[k].crush;
        s.brk2[k].choke = dn_bk[k].choke;
        s.brk2[k].smode = dn_bk[k].smode;
        s.brk2[k].stretch = dn_bk[k].stretch;
        s.brk2[k].level = dn_bk[k].level;
    }
    s.set.click = dn.click;
    s.set.clk_lvl = dn.clk_lvl;
    s.set.cin = (uint8_t)(dn.cin | (dn.dark ? 0x40u : 0u));   /* (0.30: bit 6, the DARK skin) */
    s.set.pad = dn_rs.vsens;                        /* (0.11: VEL SENS) */
    for (k = 0; k < DN_NLFO; k++)
        s.lfo[k] = dn_lp[k];
    for (k = 0; k < sizeof s.proj.name; k++)        /* (0.12: the project) */
        s.proj.name[k] = dn_pname[k];
    for (k = 0; k < DN_SEQ_TRACKS; k++) {
        s.proj.eng[k] = dn_eng[k];
        s.proj.send[0][k] = dn_send[0][k];
        s.proj.send[1][k] = dn_send[1][k];
    }
    s.proj.ret[0] = dn_ret[0];
    s.proj.ret[1] = dn_ret[1];
    s.proj.pad[0] = s.proj.pad[1] = 0;
    for (k = 0; k < 2u; k++) {                      /* (0.16: seven values: three here, four at the end) */
        uint32_t j;
        s.proj.fx[k].type = dn_fxp[k].type;
        for (j = 0; j < 3u; j++)
            s.proj.fx[k].p[j] = dn_fxp[k].p[j];
        for (j = 0; j < 4u; j++)
            s.fx_more[k][j] = dn_fxp[k].p[3u + j];
    }
    for (k = 0; k < 4u; k++)                        /* (0.19: the filter envelopes) */
        s.fe[0][k] = dn_rs.fe[k], s.fe[1][k] = dn_sb.fe[k];
    for (k = 0; k < 2u * DN_NUSER * 4u; k++)
        s.user_fe[k / (DN_NUSER * 4u)][k / 4u % DN_NUSER][k % 4u] = dn_user_fe[k / (DN_NUSER * 4u)][k / 4u % DN_NUSER][k % 4u];
    s.sub.preset = dn.sb_preset;                    /* (0.13: SUB) */
    s.sub.pad[0] = dn_rs.esync;                     /* (the envelopes synced) */
    s.sub.pad[1] = dn_sb.esync;
    s.sub.pad[2] = 0;
    for (k = 0; k < SP_COUNT; k++)
        s.sub.p[k] = dn_sb.p[k];
    for (k = 0; k < DN_NLFO; k++)                   /* (0.15: SUB's LFOs, the user sounds) */
        s.sublfo[k] = dn_lp[DN_NLFO + k];
    for (k = 0; k < 2u * DN_NUSER; k++)
        s.user[k / DN_NUSER][k % DN_NUSER] = dn_user[k / DN_NUSER][k % DN_NUSER];
#undef s
}

/* ---- the patterns in flash: four store objects (OBJ_PAT0, OBJ_PAT1, OBJ_SET, OBJ_MOTION: 4 x 3840 bytes), as one
 * stream: "DNB1", a version, swing, the pattern's pad (OVERLAP CUT), then per track its playing pattern, mute,
 * channel, then per track and pattern its length, last note and the steps that are on (9 bytes each: where, then the
 * step). Room for ~1680 notes across all 64 patterns. */
#define DN_PB_MAGIC 0x31424E44u                     /* "DNB1" */
#define DN_PB_CHUNK 3840u
#define DN_PB_MAX (4u * DN_PB_CHUNK)
#define DN_PB_HEAD (10u + 3u * DN_SEQ_TRACKS + 3u * DN_SEQ_TRACKS * DN_NPAT)
static const uint8_t DN_PB_OBJ[4] = {OBJ_PAT0, OBJ_PAT1, OBJ_SET, OBJ_MOTION};
static uint8_t dn_pbuf[DN_PB_MAX] X0X_POOL;
static const dn_track_t *dn_pat_get(uint32_t t, uint32_t n)   /* track t's pattern n, wherever it is now */
{
    return n == dn_pcur[t] ? &dn_sq.pat.tr[t] : &dn_bank[t][n];
}
static uint32_t dn_pat_notes(void)                  /* the notes on, all patterns */
{
    uint32_t t, n, i, c = 0;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        for (n = 0; n < DN_NPAT; n++) {
            const dn_track_t *tr = dn_pat_get(t, n);
            for (i = 0; i < DN_SEQ_STEPS; i++)
                c += tr->st[i].flags & DN_ST_ON;
        }
    return c;
}
static uint32_t dn_pat_room(void) { return (DN_PB_MAX - DN_PB_HEAD) / 9u; }   /* notes that fit */
static uint32_t dn_pat_used(uint32_t t)             /* which of track t's patterns have notes (a bit each) */
{
    uint32_t n, i, m = 0;
    for (n = 0; n < DN_NPAT; n++) {
        const dn_track_t *tr = dn_pat_get(t, n);
        for (i = 0; i < DN_SEQ_STEPS && !(m >> n & 1u); i++)
            if (tr->st[i].flags & DN_ST_ON)
                m |= 1u << n;
    }
    return m;
}
static uint32_t dn_pat_pack(void)                   /* into dn_pbuf; its length (0: too many notes) */
{
    uint32_t t, n, i, o = 0;
    uint8_t *b = dn_pbuf;
    if (dn_pat_notes() > dn_pat_room())
        return 0;
    b[o++] = DN_PB_MAGIC & 255u, b[o++] = DN_PB_MAGIC >> 8 & 255u, b[o++] = DN_PB_MAGIC >> 16 & 255u;
    b[o++] = DN_PB_MAGIC >> 24;
    b[o++] = 1;
    b[o++] = dn_sq.pat.swing;
    b[o++] = dn_sq.pat.pad[0], b[o++] = dn_sq.pat.pad[1], b[o++] = dn_sq.pat.pad[2];
    b[o++] = DN_NPAT;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        b[o++] = dn_pcur[t], b[o++] = dn_sq.pat.tr[t].mute, b[o++] = dn_sq.pat.tr[t].chan;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        for (n = 0; n < DN_NPAT; n++) {
            const dn_track_t *tr = dn_pat_get(t, n);
            uint32_t at = o + 2u, c = 0;
            b[o++] = tr->len, b[o++] = tr->last_note, o++;
            for (i = 0; i < DN_SEQ_STEPS; i++) {
                const dn_step_t *st = &tr->st[i];
                if (!(st->flags & DN_ST_ON))
                    continue;
                b[o++] = (uint8_t)i, b[o++] = st->note, b[o++] = st->flags, b[o++] = st->len, b[o++] = st->vel;
                b[o++] = st->cond, b[o++] = st->rtim, b[o++] = st->rlen, b[o++] = (uint8_t)st->rvel;
                c++;
            }
            b[at] = (uint8_t)c;
        }
    return o;
}
static int dn_pat_unpack(uint32_t len)              /* from dn_pbuf; 0: not ours (the patterns stay as they are) */
{
    static dn_pattern_t p X0X_POOL;
    const uint8_t *b = dn_pbuf;
    uint32_t t, n, i, o = 10u, k;
    if (len < DN_PB_HEAD || (b[0] | b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24) != DN_PB_MAGIC || b[4] != 1u ||
        b[9] != DN_NPAT)
        return 0;
    dn_pattern_clear(&p);
    p.swing = b[5], p.pad[0] = b[6], p.pad[1] = b[7], p.pad[2] = b[8];
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        dn_pcur[t] = (uint8_t)(b[o] % DN_NPAT), p.tr[t].mute = b[o + 1u], p.tr[t].chan = b[o + 2u];
        o += 3u;
        dn_pq[t] = 0xFF;
    }
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        for (n = 0; n < DN_NPAT; n++) {
            dn_track_t *tr = &p.tr[t];
            uint32_t c;
            for (i = 0; i < DN_SEQ_STEPS; i++)
                dn_step_clear(&tr->st[i]);
            if (o + 3u > len)
                return 0;
            tr->len = b[o], tr->last_note = b[o + 1u], c = b[o + 2u];
            o += 3u;
            for (k = 0; k < c; k++, o += 9u) {
                dn_step_t *st;
                if (o + 9u > len || b[o] >= DN_SEQ_STEPS)
                    return 0;
                st = &tr->st[b[o]];
                st->note = b[o + 1u], st->flags = b[o + 2u], st->len = b[o + 3u], st->vel = b[o + 4u];
                st->cond = b[o + 5u], st->rtim = b[o + 6u], st->rlen = b[o + 7u], st->rvel = (int8_t)b[o + 8u];
            }
            if (!dn_pattern_valid(&p))              /* (checks and clamps the track just read) */
                return 0;
            dn_bank[t][n] = *tr;
        }
    if (o + 2u + DN_CHORDS * DN_CHORD_N <= len && b[o] == 'C' && b[o + 1u] == DN_CHORDS) {
        /* (0.27's patterns: the project's own shapes after the notes; each step's shape becomes the chord it is) */
        uint8_t to[DN_CHORDS + 1u] = {0}, nt[DN_CHORD_N + 1u], root;
        int8_t sh[DN_CHORDS + 1u] = {0};
        for (k = 1; k <= DN_CHORDS; k++) {
            uint32_t m = 1;
            nt[0] = 60;
            for (i = 0; i < DN_CHORD_N; i++) {
                int32_t d = (int8_t)b[o + 2u + (k - 1u) * DN_CHORD_N + i];
                if (d > 0 && d < 60)
                    nt[m++] = (uint8_t)(60 + d);
            }
            to[k] = (uint8_t)dn_chord_match(nt, m, &root);
            sh[k] = to[k] ? (int8_t)(root - 60) : 0;
        }
        for (t = 0; t < DN_SEQ_TRACKS; t++)
            for (n = 0; n < DN_NPAT; n++)
                for (i = 0; i < DN_SEQ_STEPS; i++) {
                    dn_step_t *st = &dn_bank[t][n].st[i];
                    uint32_t c = DN_ST_CHORD_OF(st->flags);
                    if (c) {
                        st->flags = (uint8_t)((st->flags & ~DN_ST_CHORD) | (uint32_t)to[c] << 4);
                        st->note = (uint8_t)dn_clamp((int32_t)st->note + sh[c], 0, 127);
                    }
                }
    }
    for (t = 0; t < DN_SEQ_TRACKS; t++) {           /* the playing ones into the sequencer */
        uint8_t mute = p.tr[t].mute, chan = p.tr[t].chan;
        p.tr[t] = dn_bank[t][dn_pcur[t]];
        p.tr[t].mute = mute, p.tr[t].chan = chan;
    }
    dn_sq.pat = p;
    return 1;
}
static int dn_pat_save(void)                        /* 0 kept; 1 too many notes; 2 the flash said no */
{
    uint32_t len = dn_pat_pack(), k;
    if (!len)
        return 1;
    for (k = 0; k < 4u; k++) {
        uint32_t from = k * DN_PB_CHUNK, n = len > from ? len - from : 0u;
        if (n > DN_PB_CHUNK)
            n = DN_PB_CHUNK;
        if (dn_store(DN_PB_OBJ[k], dn_pbuf + from, n ? n : 1u))   /* (an unused chunk: one byte) */
            return 2;
        if (n < DN_PB_CHUNK)
            break;
    }
    return 0;
}
static void dn_pat_load(void)                       /* at start-up: the patterns; or 0.10's one pattern as each track's
                                                     * pattern 1; or the demo */
{
    uint32_t len = 0, k, t, n;
    int got;
    for (k = 0; k < 4u; k++) {
        got = plat_store_load(DN_PB_OBJ[k], dn_pbuf + len, DN_PB_CHUNK);
        if (got <= 0)
            break;
        len += (uint32_t)got;
        if ((uint32_t)got < DN_PB_CHUNK)
            break;
    }
    if (dn_pat_unpack(len))
        return;
    for (t = 0; t < DN_SEQ_TRACKS; t++) {           /* (not ours: start clean) */
        dn_pcur[t] = 0, dn_pq[t] = 0xFF;
        for (n = 0; n < DN_NPAT; n++) {
            dn_track_t *tr = &dn_bank[t][n];
            uint32_t i;
            tr->len = 16, tr->mute = 0, tr->chan = (uint8_t)t, tr->last_note = 53;
            for (i = 0; i < DN_SEQ_STEPS; i++)
                dn_step_clear(&tr->st[i]);
        }
    }
    got = plat_store_load(OBJ_PAT0, dn_pbuf, DN_PB_CHUNK);   /* 0.10 and older: one pattern of 8 tracks */
    if (got <= 0 || !dn_pattern_from_old(&dn_sq.pat, dn_pbuf, (uint32_t)got))
        dn_pattern_demo(&dn_sq.pat);                /* first boot: the demo */
}
/* track t to pattern n: at once when stopped, else when its pattern ends (LST its last pass) */
static void dn_pat_select(uint32_t t, uint32_t n)
{
    if (n >= DN_NPAT)
        return;
    dn_lock();
    if (!dn_sq.playing) {
        if (n != dn_pcur[t])
            dn_pat_switch(t, n);
    } else if (n == dn_pcur[t]) {
        dn_pq[t] = 0xFF;                            /* (the playing one again: nothing queued) */
        dn_sq.lst &= (uint8_t)~(1u << t);
    } else {
        dn_pq[t] = (uint8_t)n;
        dn_sq.lst |= (uint8_t)(1u << t);
    }
    dn_unlock();
    dn_mark();
    dn.redraw |= DN_RD_ALL;
}

/* a saved project, checked: one REESE and one each of BREAKS A / B at most (else the factory layout) */
static void dn_proj_restore(const dn_save_t *sp)
{
    uint32_t k, n[DN_E_N] = {0};
    for (k = 0; k < DN_SEQ_TRACKS; k++)
        n[sp->proj.eng[k] < DN_E_N ? sp->proj.eng[k] : 0u]++;
    if (n[DN_E_REESE] <= 1u && n[DN_E_BRKA] <= 1u && n[DN_E_BRKB] <= 1u && n[DN_E_SUB] <= 1u && n[DN_E_SMP] <= 1u &&
        n[DN_E_FM] <= 1u)
        for (k = 0; k < DN_SEQ_TRACKS; k++)
            dn_eng[k] = sp->proj.eng[k] < DN_E_N ? sp->proj.eng[k] : (uint8_t)DN_E_OFF;
    for (k = 0; k < DN_SEQ_TRACKS; k++) {
        dn_send[0][k] = sp->proj.send[0][k] & 127u;
        dn_send[1][k] = sp->proj.send[1][k] & 127u;
    }
    dn_ret[0] = sp->proj.ret[0] & 127u;
    dn_ret[1] = sp->proj.ret[1] & 127u;
    for (k = 0; k < 2u; k++) {                      /* (the rest of the values: the effect's defaults, until
                                                     * a 0.16 save's fx_more, dn_app_init) */
        uint32_t j;
        dn_fx_default(&dn_fxp[k], sp->proj.fx[k].type < DN_FX_N ? sp->proj.fx[k].type : (uint32_t)DN_FX_OFF);
        for (j = 0; j < 3u && dn_fxp[k].type; j++)
            dn_fxp[k].p[j] = sp->proj.fx[k].p[j];
        dn_fx_check(k);
    }
    if (!dn_fx_fits(dn_fxp[0].type, dn_fxp[1].type))
        dn_fx_default(&dn_fxp[1], DN_FX_OFF);
    for (k = 0; k + 1u < sizeof dn_pname; k++) {
        char c = sp->proj.name[k];
        dn_pname[k] = c >= 32 && c < 127 ? c : 0;
    }
    dn_pname[sizeof dn_pname - 1u] = 0;
    if (!dn_pname[0])
        dn_pname[0] = 'P', dn_pname[1] = '1', dn_pname[2] = 0;
}

static void dn_sm_keep(void)                        /* (0.23) SAMPLER's settings into what SAVE writes */
{
    uint32_t k, j;
    dn_man.smp.mode = dn_sm.p.mode;
    for (k = 0; k < DN_SMP_PADS; k++)
        for (j = 0; j < PD_N; j++)
            dn_man.smp.pad[k][j] = dn_sm.p.pad[k][j];
    for (k = 0; k < DN_SMP_ZONES; k++)
        for (j = 0; j < ZN_N; j++)
            dn_man.smp.zone[k][j] = dn_sm.p.zone[k][j];
    for (j = 0; j < KY_N; j++)
        dn_man.smp.key[j] = dn_sm.p.key[j];
    for (k = 0; k < DN_SMP_NSRC; k++)
        dn_man.smp.src_key[k] = dn_sm_key[k];
}
static void dn_save(void)
{
    dn_sm_keep();
    dn_fm_pack(dn_fm_base, dn_man.fm.packed);       /* (0.25) FM's voice */
    dn_man.fm.opsw = dn_fm_base[DN_FMV_OPSW];
    dn_man.fm.voices = dn_fmx.voices;
    dn_man.fm.level = (uint8_t)dn_fmx.level;
    dn_man.fm.mac[0] = dn_fm_mac.bright, dn_man.fm.mac[1] = dn_fm_mac.time;
    dn_man.fm.mac[2] = dn_fm_mac.attack, dn_man.fm.mac[3] = dn_fm_mac.release;
    dn_man.fm.preset = dn.fm_preset;
    dn_man.fm.valid = 0xA5u;
    dn_save_t *ps = &dn_sv;
#define s (*ps)
    dn_save_fill(&s);
    dn_man.magic = DN_MAN_MAGIC;
    {
        int pr = dn_pat_save();
        if (dn_store(OBJ_SOUND, &s, sizeof s) == 0 && pr == 0 && dn_store(OBJ_SONG, &dn_man, sizeof dn_man) == 0)
            dn.dirty = 0;
        if (pr == 1)
            dn_say("PATTERN MEMORY FULL");
    }
    dn.redraw |= DN_RD_ALL;
#undef s
}

static void dn_skin(int dark);
void dn_app_init(void)
{
    dn_save_t *ps = &dn_sv;
    uint32_t k;
#define s (*ps)
    int got;
    dn_skin(0);                                     /* (0.30: LIGHT, unless the save says DARK) */
    reese_init(&dn_rs);
    dn_sub_init(&dn_sb);
    dn.syn_last = DN_E_REESE;
    dn_lfo_init_all();
    dn_lfo_init_set(1);
    for (k = 0; k < DN_NBRK; k++) {
        dn_brk_init(&dn_bk[k]);
        dn_bk_src[k] = (uint8_t)k;                  /* A: ROLLER, B: STEPPER */
        dn_bk[k].decay = 127;
        dn_bk[k].choke = 1;                         /* A and B choke each other: one sampler voice, as a break is */
    }
    dn_seq_init(&dn_sq);
    dn_fx_default(&dn_fxp[0], DN_FX_DELAY);         /* FX A: the delay, nothing sent to it yet; FX B: empty */
    dn_fx_default(&dn_fxp[1], DN_FX_OFF);
    dn.peng = 0xFF;
    dn.sounding = dn.midi_note = 0xFF;
    dn.step_held = -1;
    dn.ramp_b = -1;
    dn.pt_held = -1;
    dn.pen = 100;
    dn.entry_len = DN_LEN_STEP;
    dn.click = 1;                                   /* the click while recording; one bar's count-in */
    dn.clk_lvl = 70;
    dn.cin = 1;
    for (k = 0; k < NKEYS; k++)
        dn.rec_step[k] = 0xFF;
    dn.rec_grp = 0xFF;
    dn_pat_load();
    dn.redraw = DN_RD_ALL;
    got = plat_store_load(OBJ_SONG, &dn_man, sizeof dn_man);
    if (got > 0 && dn_man.magic == DN_MAN_MAGIC1 && dn_man.count <= DN_MAN_N1 && (got == (int)DN_MAN1_NAMES ||
                                                                               got == (int)DN_MAN1_NAMES + 128)) {
        /* (0.23) the older layout, in place: its newest 20 loops' slices, its names (0.22), SAMPLER as new */
        char nm[2 * DN_NUSER * 8];
        uint8_t *b = (uint8_t *)&dn_man;
        uint32_t keep = dn_man.count > DN_MAN_N ? DN_MAN_N : dn_man.count, from = dn_man.count - keep;
        for (k = 0; k < sizeof nm; k++)
            nm[k] = got == (int)DN_MAN1_NAMES + 128 ? (char)b[DN_MAN1_NAMES + k] : 0;
        for (k = 0; k < keep * sizeof(dn_man_t); k++)   /* (down: the copy never overtakes itself) */
            b[DN_MAN1_T + k] = b[DN_MAN1_T + from * sizeof(dn_man_t) + k];
        dn_man.count = keep;
        for (k = 0; k < sizeof nm; k++)
            (&dn_man.uname[0][0][0])[k] = nm[k];
        dn_man.smp.mode = 0xFFu;                    /* (no SAMPLER settings yet) */
        dn_man.magic = DN_MAN_MAGIC;
        got = (int)sizeof dn_man;
    }
    if (got >= (int)DN_MAN2_SIZE && got < (int)sizeof dn_man && dn_man.magic == DN_MAN_MAGIC) {
        uint8_t *b = (uint8_t *)&dn_man;            /* (0.25) a 0.23 / 0.24 object: no FM yet */
        for (k = (uint32_t)got; k < sizeof dn_man; k++)
            b[k] = 0;
        got = (int)sizeof dn_man;
    }
    if (got != (int)sizeof dn_man || dn_man.magic != DN_MAN_MAGIC || dn_man.count > DN_MAN_N) {
        uint8_t *z = (uint8_t *)&dn_man;            /* (nothing kept yet) */
        for (k = 0; k < sizeof dn_man; k++)
            z[k] = 0;
        dn_man.magic = DN_MAN_MAGIC;
        dn_man.smp.mode = 0xFFu;
    }
    dn_smp_init(&dn_sm);                            /* SAMPLER's settings, kept or new */
    if (dn_man.smp.mode <= 1u) {
        uint32_t j;
        dn_sm.p.mode = dn_man.smp.mode;
        for (k = 0; k < DN_SMP_PADS; k++)
            for (j = 0; j < PD_N; j++)
                dn_sm.p.pad[k][j] = (int16_t)dn_clamp(dn_man.smp.pad[k][j], dn_smp_pd_min(j), dn_smp_pd_max(j));
        for (k = 0; k < DN_SMP_ZONES; k++)
            for (j = 0; j < ZN_N; j++)
                dn_sm.p.zone[k][j] = (int16_t)dn_clamp(dn_man.smp.zone[k][j], dn_smp_zn_min(j), dn_smp_zn_max(j));
        for (j = 0; j < KY_N; j++)
            dn_sm.p.key[j] = (int16_t)dn_clamp(dn_man.smp.key[j], dn_smp_ky_min(j), dn_smp_ky_max(j));
        for (k = 0; k < DN_SMP_NSRC; k++)
            dn_sm_key[k] = dn_man.smp.src_key[k];
    }
    dn_sm_resolve(1);
    dn_fm_init(&dn_fmx);                            /* FM's voice, kept or the first factory one */
    if (dn_man.fm.valid == 0xA5u) {
        dn_fm_unpack(dn_man.fm.packed, dn_fm_base);
        dn_fm_base[DN_FMV_OPSW] = dn_man.fm.opsw & 0x3fu;
        dn_fmx.voices = (uint8_t)dn_clamp(dn_man.fm.voices, 1, DN_FM_NV);
        dn_fmx.level = (int16_t)dn_clamp(dn_man.fm.level, 0, 127);
        dn_fm_mac.bright = (int8_t)dn_clamp(dn_man.fm.mac[0], -50, 50);
        dn_fm_mac.time = (int8_t)dn_clamp(dn_man.fm.mac[1], -50, 50);
        dn_fm_mac.attack = (int8_t)dn_clamp(dn_man.fm.mac[2], -50, 50);
        dn_fm_mac.release = (int8_t)dn_clamp(dn_man.fm.mac[3], -50, 50);
        dn.fm_preset = dn_man.fm.preset;
    } else {
        dn_fm_factory(0, dn_fm_base);
        dn.fm_preset = 0;
    }
    dn_fm_sync();
    for (k = 0; k < sizeof dn_man.uname; k++) {    /* (printable, ended) */
        char *c = &dn_man.uname[0][0][0] + k;
        if (*c < 32 || *c > 126)
            *c = 0;
    }
    got = plat_store_load(OBJ_SOUND, &s, sizeof s);
    /* any save from 0.4 on (0.20.1: by its length, not an exact list: a save is its struct's size, padded to 4 bytes,
     * and the list held the unpadded offsets, so a save from 0.14, 0.15 or 0.16-0.18 was turned away and the FM-1
     * started from nothing after an update; Jamie: "every time I update the FW it clears my project"). Each part is
     * read if the save reaches past it; the parts only ever get added at the end. */
    if (got >= (int)DN_SAVE_V04 && got <= (int)sizeof s && s.magic == DN_SAVE_MAGIC) {
        dn.preset = (uint8_t)(s.preset % (reese_preset_count() + DN_NUSER));
        dn.page = (uint8_t)(DN_IS_SNDPG(s.page) ? s.page : 0u);
        dn.oct = (int8_t)dn_clamp(s.oct, -3, 3);
        dn_rs.bpm = (uint16_t)dn_clamp(s.bpm, 60, 200);
        dn_sq.bpm = dn_rs.bpm;
        for (k = 0; k < RP_COUNT; k++)
            dn_rs.p[k] = (int16_t)dn_clamp(s.p[k], reese_param_min(k), reese_param_max(k));
        dn_lfo_init_all();                          /* (0.8 and older: their filter LFO as LFO 1) */
        if (got >= (int)DN_SAVE_V012)
            dn_proj_restore(&s);                    /* 0.12: the project */
        if (got >= (int)DN_SAVE_V014) {             /* 0.13: SUB */
            dn.sb_preset = (uint8_t)(s.sub.preset % (dn_sub_preset_count() + DN_NUSER));
            dn_rs.esync = s.sub.pad[0] ? 1u : 0u;
            dn_sb.esync = s.sub.pad[1] ? 1u : 0u;
            for (k = 0; k < SP_COUNT; k++)
                dn_sb.p[k] = (int16_t)dn_clamp(s.sub.p[k], dn_sub_param_min(k), dn_sub_param_max(k));
        }
        if (got >= (int)DN_SAVE_V015)               /* 0.15: the user sounds */
            for (k = 0; k < 2u * DN_NUSER; k++)
                dn_user[k / DN_NUSER][k % DN_NUSER] = s.user[k / DN_NUSER][k % DN_NUSER];
        if (got >= (int)DN_SAVE_V016)               /* 0.16: the effects' other values */
            for (k = 0; k < 2u; k++) {
                uint32_t j;
                for (j = 0; j < 4u; j++)
                    dn_fxp[k].p[3u + j] = s.fx_more[k][j];
                dn_fx_check(k);
            }
        reese_fe_default(&dn_rs);                   /* 0.19: the filter envelopes (older saves: as they sounded, from
                                                     * the amp's settings) */
        dn_sub_fe_default(&dn_sb);
        for (k = 0; k < DN_NUSER; k++) {
            const int8_t *r = dn_user[0][k].p, *b = dn_user[1][k].p;
            dn_user_fe[0][k][0] = r[RP_ATK], dn_user_fe[0][k][1] = r[RP_DEC], dn_user_fe[0][k][2] = 0;
            dn_user_fe[0][k][3] = r[RP_REL];
            dn_user_fe[1][k][0] = b[SP_ATK], dn_user_fe[1][k][1] = b[SP_DEC], dn_user_fe[1][k][2] = b[SP_SUS];
            dn_user_fe[1][k][3] = b[SP_REL];
        }
        if (got >= (int)(offsetof(dn_save_t, user_fe) + sizeof s.user_fe)) {   /* (0.19) */
            for (k = 0; k < 4u; k++)
                dn_rs.fe[k] = (int16_t)dn_clamp(s.fe[0][k], 0, 127), dn_sb.fe[k] = (int16_t)dn_clamp(s.fe[1][k], 0, 127);
            for (k = 0; k < 2u * DN_NUSER * 4u; k++)
                dn_user_fe[k / (DN_NUSER * 4u)][k / 4u % DN_NUSER][k % 4u] =
                    (int8_t)dn_clamp(s.user_fe[k / (DN_NUSER * 4u)][k / 4u % DN_NUSER][k % 4u], 0, 127);
        }
        if (got >= (int)DN_SAVE_V011) {
            for (k = 0; k < DN_NLFO; k++) {         /* 0.9: the LFOs, checked */
                dn_lp[k] = s.lfo[k];
                if (got >= (int)DN_SAVE_V015)
                    dn_lp[DN_NLFO + k] = s.sublfo[k];   /* (0.15: SUB's) */
                dn_lfo_fix(&dn_lp[k]);
                dn_lfo_fix(&dn_lp[DN_NLFO + k]);
            }
            dn.click = (uint8_t)dn_clamp(s.set.click, 0, 2);
            dn.clk_lvl = (uint8_t)dn_clamp(s.set.clk_lvl, 0, 127);
            dn.cin = (uint8_t)dn_clamp(s.set.cin & 0x3Fu, 0, 2);
            dn_skin(s.set.cin & 0x40u);
            dn_rs.vsens = (uint8_t)dn_clamp(s.set.pad, 0, 127);
        }
        for (k = 0; got >= (int)DN_SAVE_V08 && k < DN_NBRK; k++) {
            dn_bk[k].choke = (uint8_t)dn_clamp(s.brk2[k].choke, 0, 4);
            dn_bk[k].smode = (uint8_t)dn_clamp(s.brk2[k].smode, 0, 2);
            dn_bk[k].stretch = (uint8_t)dn_clamp(s.brk2[k].stretch, 0, 2);
            dn_bk[k].level = (uint8_t)dn_clamp(s.brk2[k].level, 0, 127);
        }
        for (k = 0; got >= (int)DN_SAVE_V05 && k < DN_NBRK; k++) {
            dn_bk_src[k] = dn_brk_src_of(s.brk[k].src);
            dn_bk[k].pitch = (int8_t)dn_clamp(s.brk[k].pitch, -24, 24);
            dn_bk[k].decay = (uint8_t)dn_clamp(s.brk[k].decay, 0, 127);
            dn_bk[k].crush = (uint8_t)dn_clamp(s.brk[k].crush, 0, 127);
        }
    }
    dn_sq.bpm = dn_rs.bpm;
    dn_legato_fix();                                /* overlaps slide on REESE and MIDI, not on the breaks */
    dn_fx_layout();
    for (k = 0; k < DN_NBRK; k++)
        dn_brk_select(k, dn_bk_src[k]);
    dn.btn_prev = dn_in_buttons();                   /* what is held at start-up is no press */
    dn.keys_prev = dn.keys_raw = dn_in_keys();
    dn.ready = 1;
#undef s
}

/* ------------------------------------------------------------------------------------------------- input --- */
static uint32_t dn_slices(uint32_t t) { return DN_IS_BRK(t) && dn_bk[DN_BK(t)].nslices ? dn_bk[DN_BK(t)].nslices : 16u; }   /* (SAMPLER: 16 pads) */
/* what key k plays: a note (MIDI number); on a BREAKS track a slice, the octave moving 16 slices (a bar) */
static uint8_t dn_key_note(uint32_t k)
{
    if (DN_IS_PADS(dn.trk)) {                       /* the keys are the chops (SAMPLER's pads) in order, 27 to a shift: every
                                                     * chop has one key, none repeats (0xFF: no chop there) */
        int32_t v = (int32_t)k + 27 * dn.oct;
        return v >= 0 && v < (int32_t)dn_slices(dn.trk) ? (uint8_t)v : 0xFFu;
    }
    return (uint8_t)dn_clamp(DN_KEY0 + (int32_t)k + 12 * dn.oct, 0, 127);
}

/* the keys' layout from F3: white keys are the steps (16 of them), black keys the SEQ page's functions (11) */
static int dn_is_white(uint32_t k) { return (0xAD5u >> (k % 12u)) & 1u; }   /* F G A B C D E: 0 2 4 6 7 9 11 */
static int32_t dn_key_ord(uint32_t k)               /* which white (or black) key this is, from the left */
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += dn_is_white(i) == dn_is_white(k);
    return (int32_t)n;
}

static void dn_mark(void)
{
    dn_rev++;
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}

/* a note from the keys: track 1 is REESE, 2 and 3 a slice of BREAKS A / B, the others MIDI on their channel */
static void dn_play(uint8_t note, int legato)
{
    if (note == 0xFFu)
        return;                                     /* (a key past the last chop) */
    dn_lock();
    if (DN_IS_BRK(dn.trk)) {
        dn_brk_hit(DN_BK(dn.trk), note, 100, 0);
    } else if (DN_IS_SMP(dn.trk)) {                 /* SAMPLER: the pad (and its pages show it), or the key */
        if (dn_sm.p.mode == DN_SMP_ONESHOT) {
            dn_smp_pad(&dn_sm, note, 100);
            dn.smp_pad = (uint8_t)(note % DN_SMP_PADS);
        } else {
            int z = dn_smp_zone_of(&dn_sm, note);
            dn_smp_note_on(&dn_sm, note, 100);
            if (z >= 0)
                dn.smp_zone = (uint8_t)z;
        }
        dn.redraw |= DN_RD_ALL;
    } else if (DN_IS_RS(dn.trk)) {
        dn_rs_on(note, 100, legato);
    } else if (DN_IS_SUB(dn.trk)) {
        dn_sb_on(note, 100, legato, 0);
    } else if (DN_IS_FM(dn.trk)) {
        dn_fm_note_on(&dn_fmx, note, 100);
    } else if (DN_IS_MIDI(dn.trk)) {
        uint32_t ch = dn_sq.pat.tr[dn.trk].chan;
        if (dn.sounding != 0xFF)
            plat_midi_out(0x08u | (0x80u | ch) << 8 | (uint32_t)dn.sounding << 16);
        plat_midi_out(0x09u | (0x90u | ch) << 8 | (uint32_t)note << 16 | 100u << 24);
    }
    dn_unlock();
    dn.sounding = note;
}
static void dn_stop(void)
{
    dn_lock();
    if (DN_IS_BRK(dn.trk))
        dn_brk_release(&dn_bk[DN_BK(dn.trk)]);
    else if (DN_IS_MIDI(dn.trk) && dn.sounding != 0xFF)
        plat_midi_out(0x08u | (0x80u | dn_sq.pat.tr[dn.trk].chan) << 8 | (uint32_t)dn.sounding << 16);
    else if (DN_IS_RS(dn.trk) && dn.midi_note == 0xFF)
        reese_note_off(&dn_rs);
    else if (DN_IS_SUB(dn.trk))
        dn_sub_note_off(&dn_sb);
    else if (DN_IS_SMP(dn.trk) && dn_sm.p.mode == DN_SMP_KEYS && dn.sounding != 0xFF)
        dn_smp_note_off(&dn_sm, dn.sounding);
    else if (DN_IS_FM(dn.trk) && dn.sounding != 0xFF)
        dn_fm_note_off(&dn_fmx, dn.sounding);
    dn_unlock();
    dn.sounding = 0xFF;
}

/* overlapping notes slide on the synths and MIDI, never on the breaks: the sequencer's legato bits follow the tracks */
static void dn_legato_fix(void)
{
    uint32_t t, m = 0;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        if (!DN_IS_BRK(t) && !DN_IS_SMP(t) && !DN_IS_FM(t))   /* (SAMPLER, FM: every note its own: no slides) */
            m |= 1u << t;
    dn_sq.legato = (uint8_t)m;
}
/* (inside the lock) what track t plays, silenced */
static void dn_trk_silence(uint32_t t)
{
    dn_sq.gate[t] = 0;
    if (DN_IS_RS(t))
        reese_note_off(&dn_rs);
    else if (DN_IS_SUB(t))
        dn_sub_note_off(&dn_sb);
    else if (DN_IS_BRK(t))
        dn_brk_cut(&dn_bk[DN_BK(t)]);
    else if (DN_IS_SMP(t))
        dn_smp_cut(&dn_sm);
    else if (DN_IS_FM(t))
        dn_fm_all_off(&dn_fmx);
    else if (DN_IS_MIDI(t))
        plat_midi_out(0x0Bu | (0xB0u | dn_sq.pat.tr[t].chan) << 8 | 123u << 16);   /* all notes off */
}
/* track t plays instrument e from now. REESE and BREAKS A / B are one each: the track holding it gets t's old one
 * (they swap). The track's patterns stay as they are. */
static void dn_eng_set(uint32_t t, uint32_t e)
{
    uint32_t u, old;
    t %= DN_SEQ_TRACKS;
    old = dn_eng[t];
    if (e >= DN_E_N || e == old)
        return;
    dn_lock();
    dn_trk_silence(t);
    if (DN_ONE(e) && (u = dn_trk_of(e)) < 8u) {
        dn_trk_silence(u);
        dn_eng[u] = (uint8_t)old;
    }
    dn_eng[t] = (uint8_t)e;
    dn_legato_fix();
    dn_unlock();
    dn.sounding = 0xFF;
    dn.peng = 0xFF;
    dn_mark();
}

/* where the playhead is, in quarter steps since PLAY (under the lock: the audio moves it) */
static uint32_t dn_tick_now(void)
{
    uint32_t st = dn_sq.pos ? dn_sq.pos - 1u : 0u;
    int32_t q = dn_seq_step_q8(&dn_sq, st), into = q - dn_sq.until_step;
    return st * DN_LEN_STEP + (uint32_t)dn_clamp(into / (q / (int32_t)DN_LEN_STEP + 1), 0, (int32_t)DN_LEN_STEP - 1);
}

/* REC: a played note into the selected track. Playing: the step now, or the next one if the note came in its second
 * half (the nearest). Stopped: at the cursor, which moves on. Played under another key: a slide. */
/* (0.29) the roll remembers the last note put in, edited or picked up on track t: its note, length, chord; a step
 * turned on next gets them (not on the breaks or SAMPLER's pads, whose steps take their slice / pad) */
static void dn_step_remember(uint32_t t, const dn_step_t *st)
{
    t %= DN_SEQ_TRACKS;
    if (DN_IS_PADS(t))
        return;
    dn_sq.pat.tr[t].last_note = st->note;
    dn.last_len[t] = st->len;
    dn.last_chord[t] = (uint8_t)DN_ST_CHORD_OF(st->flags);
}
static uint32_t dn_rec_note(uint8_t note, int legato)
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i;
    dn_lock();
    if (dn_sq.playing) {
        i = dn_sq.cur[dn.trk];
        if (dn_sq.until_step < dn_seq_step_q8(&dn_sq, dn_sq.pos - 1u) / 2)
            i = (i + 1u) % tr->len;
    } else {
        i = dn.rec_cur % tr->len;
        dn.rec_cur = (uint8_t)((i + 1u) % tr->len);
    }
    tr->st[i].note = note;
    tr->st[i].flags = (uint8_t)(DN_ST_ON | (legato && !DN_IS_PADS(dn.trk) && !DN_IS_POLY(dn.trk) ? DN_ST_SLIDE : 0u));
    tr->st[i].len = DN_IS_PADS(dn.trk) ? DN_LEN_STEP : 3;
    tr->st[i].vel = 100;
    tr->last_note = note;
    dn_unlock();
    dn_step_remember(dn.trk, &tr->st[i]);
    dn_mark();
    return i;
}
/* REC while playing: the key came up, so the note it wrote gets the length it was held (to the nearest quarter step) */
static void dn_rec_release(uint32_t k)
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i = dn.rec_step[k], held;
    if (i == 0xFFu || i >= DN_SEQ_STEPS)
        return;
    dn_lock();
    held = dn_tick_now() - dn.rec_tick[k];
    if (!DN_IS_PADS(dn.trk) && (tr->st[i].flags & DN_ST_ON))
        tr->st[i].len = (uint8_t)dn_clamp((int32_t)held, 1, DN_LEN_MAX);
    dn_unlock();
    dn.rec_last = (uint8_t)i;
    dn.rec_step[k] = 0xFF;
    dn_mark();
}

static void dn_keys_play(uint32_t keys)             /* the sound's pages: the keys are notes */
{
    uint32_t down = keys & ~dn.keys_prev, up = dn.keys_prev & ~keys, k, i, j;
    for (k = 0; k < NKEYS; k++) {
        if (up & (1u << k)) {
            for (i = j = 0; i < dn.nheld; i++)
                if (dn.held[i] != k)
                    dn.held[j++] = dn.held[i];
            dn.nheld = (uint8_t)j;
            if (!j)
                dn.rec_grp = 0xFF;                  /* (all up: the next key a new step) */
            if (DN_IS_POLY(dn.trk)) {               /* (SAMPLER's keys, FM: each key its own note) */
                dn_lock();
                if (DN_IS_FM(dn.trk))
                    dn_fm_note_off(&dn_fmx, dn_key_note(k));
                else
                    dn_smp_note_off(&dn_sm, dn_key_note(k));
                dn_unlock();
            }
            if (dn.rec && dn_sq.playing)
                dn_rec_release(k);
        }
        if ((down & (1u << k)) && dn.page == DN_PG_SLICE && DN_IS_BRK(dn.trk) && dn.chop_rec && dn.oct >= 0) {
            dn_brk_t *bk = &dn_bk[DN_BK(dn.trk)];     /* REC lit: this key's chop where the loop is (or the cursor) */
            uint32_t c = k + 27u * (uint32_t)dn.oct, at = dn.sl_cur;
            if (bk->aud) {
                uint32_t lat = (bk->loop.rate ? bk->loop.rate : 22050u) * DN_TAP_LATENCY_MS / 1000u, pos;
                dn_lock();
                pos = dn_brk_pos(bk);
                dn_unlock();
                at = pos > lat ? pos - lat : 0u;    /* (your reaction: 12 ms) */
            }
            dn_chop_key(c, at);
            if (!bk->aud && dn.sl_sel == c) {       /* stopped: hear it from its new place */
                dn_play((uint8_t)c, 0);
                dn_lock();
                bk->stopend = 1;
                dn_unlock();
            }
            dn_sl_show(bk);
            dn.redraw |= DN_RD_ALL;
            continue;
        }
        if ((down & (1u << k)) && dn.nheld < NKEYS) {
            int legato = dn.nheld > 0u;             /* under a held key: a slide */
            dn_play(dn_key_note(k), legato || dn.play);
            if (dn.page == DN_PG_SLICE && DN_IS_BRK(dn.trk) && dn_key_note(k) != 0xFFu) {
                dn_brk_t *bk = &dn_bk[DN_BK(dn.trk)];  /* chop mode: the key selects its chop, heard to its end */
                dn.sl_sel = dn_key_note(k);
                dn.sl_cur = bk->start[dn.sl_sel];
                dn_lock();
                bk->stopend = 1;
                dn_unlock();
                dn_sl_show(bk);
                dn.redraw |= DN_RD_ALL;
            }
            dn.rec_step[k] = 0xFF;
            if (dn.rec && !dn.cin_left) {           /* (during the count-in the keys only play) */
                uint32_t at;
                if (DN_CHORDABLE(dn.trk) && dn.nheld > 0u && dn.rec_grp < DN_SEQ_STEPS) {
                    at = dn.rec_grp;                /* (0.27) a key down under one held: their chord, in its step */
                    dn_chord_rec(&dn_sq.pat.tr[dn.trk].st[at], dn_key_note(k));
                    dn_mark();
                } else {
                    at = dn_rec_note(dn_key_note(k), legato);
                    dn.rec_grp = (uint8_t)at;
                    dn.rec_notes[0] = dn_key_note(k), dn.rec_n = 1;
                }
                if (dn_sq.playing) {                /* (its length: when the key comes up) */
                    dn.rec_step[k] = (uint8_t)at;
                    dn_lock();
                    dn.rec_tick[k] = dn_tick_now();
                    dn_unlock();
                }
            }
            dn.held[dn.nheld++] = (uint8_t)k;
        }
    }
    if (up && dn.nheld == 0u && !dn.play && dn.sounding != 0xFF)
        dn_stop();
    else if (up && dn.nheld && !DN_IS_PADS(dn.trk) && !DN_IS_POLY(dn.trk) &&
             dn.sounding != dn_key_note(dn.held[dn.nheld - 1u]))
        dn_play(dn_key_note(dn.held[dn.nheld - 1u]), 1);       /* back to the last key still held */
    else if (up && !dn.nheld && dn.play && !DN_IS_PADS(dn.trk) && !DN_IS_POLY(dn.trk))
        dn_play(dn_key_note(0), 1);
}

/* SEQ, ROLL (STEP mode) and DRUM: white keys are steps, black keys functions. On DRUM a step plays the selected slice
 * row: a tap on an empty step puts that slice there, on a step with another slice moves it to this one */
static void dn_keys_seq(uint32_t keys)
{
    uint32_t down = keys & ~dn.keys_prev, up = dn.keys_prev & ~keys, k;
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    int drum = dn.key_page == DN_PG_DRUM;
    for (k = 0; k < NKEYS; k++) {
        int32_t o = dn_key_ord(k);
        if (dn_is_white(k)) {
            uint32_t i = (uint32_t)o + 16u * dn.view;
            if ((down & (1u << k)) && dn.save_held && dn.step_held < 0 && o < (int32_t)DN_NPAT) {
                dn_pat_select(dn.trk, (uint32_t)o);   /* SAVE held + white key 1-8: this instrument's pattern */
                dn.save_chord = 1;
                continue;
            }
            if ((down & (1u << k)) && i < tr->len && dn.step_held >= 0 && dn.step_key != (int8_t)k && dn.rec_held) {
                int32_t a = dn.step_held < (int32_t)i ? dn.step_held : (int32_t)i;   /* REC held: a velocity ramp */
                int32_t b = dn.step_held < (int32_t)i ? (int32_t)i : dn.step_held;
                dn.ramp_a = (int8_t)a, dn.ramp_b = (int8_t)b;
                dn.ramp_v0 = tr->st[a].vel, dn.ramp_v1 = tr->st[b].vel;
                dn.rec_chord = 1;
                dn.step_edited = 1;
                dn_say("RAMP: K3 FROM, K4 TO");
                continue;
            }
            if ((down & (1u << k)) && i < tr->len && dn.step_held >= 0 && dn.step_key != (int8_t)k) {
                dn_step_drop(i);                    /* a step held, another pressed: the note goes there */
                continue;
            }
            if ((down & (1u << k)) && i < tr->len) {
                dn.step_held = (int8_t)i;
                dn.step_key = (int8_t)k;
                dn.step_edited = 0;
                dn.step_pg = 0;
                dn.ramp_b = -1;
                if (drum && (tr->st[i].flags & DN_ST_ON) && tr->st[i].note != dn.drum_row) {
                    dn_lock();
                    tr->st[i].note = dn.drum_row;
                    dn_unlock();
                    dn.step_edited = 1;
                    dn_mark();
                } else if (!(tr->st[i].flags & DN_ST_ON)) {
                    dn_lock();
                    tr->st[i].flags |= DN_ST_ON;
                    tr->st[i].len = 3;              /* a fresh step: three quarters of a step (breaks: a whole one) */
                    if (DN_IS_BRK(dn.trk)) {         /* a break: the slice that belongs on this step (DRUM: the row's) */
                        const dn_brk_t *bk = &dn_bk[DN_BK(dn.trk)];
                        uint32_t gs = dn_brk_grid_slices(bk);     /* (AUTO / MANUAL: the hit nearest the step) */
                        tr->st[i].note = (uint8_t)(drum ? dn.drum_row : dn_brk_slice_at(bk, i % gs, gs));
                        tr->st[i].len = DN_LEN_STEP;
                    } else if (DN_IS_PADS(dn.trk)) {   /* SAMPLER's pads: the row's (or the pad shown) */
                        tr->st[i].note = (uint8_t)(drum ? dn.drum_row : dn.smp_pad);
                        tr->st[i].len = DN_LEN_STEP;
                    } else {                        /* (0.29) the last note's note, length and chord */
                        tr->st[i].note = tr->last_note;
                        tr->st[i].len = dn.last_len[dn.trk] ? dn.last_len[dn.trk] : 3u;
                        tr->st[i].flags = (uint8_t)((tr->st[i].flags & ~DN_ST_CHORD) |
                                                    (DN_CHORDABLE(dn.trk) ? (uint32_t)dn.last_chord[dn.trk] << 4 : 0u));
                    }
                    dn_unlock();
                    dn.step_edited = 1;             /* just turned on: letting go keeps it */
                    dn_mark();
                } else if (!drum) {
                    dn_step_remember(dn.trk, &tr->st[i]);   /* (0.29) one already there: the next is like it */
                }
            }
            if ((up & (1u << k)) && dn.step_held >= 0 && dn.step_key == (int8_t)k) {
                dn.ramp_b = -1;
                i = (uint32_t)dn.step_held;         /* (moved with SELECT: no longer under this key) */
                if (!dn.step_edited) {
                    dn_lock();
                    tr->st[i].flags &= (uint8_t)~DN_ST_ON;
                    dn_unlock();
                    dn_mark();
                }
                dn.step_held = -1;
                dn.redraw |= DN_RD_BOTTOM;
            }
        } else if (down & (1u << k)) {
            if (dn.save_held && (o == 9 || o == 10)) {  /* SAVE + black key 10 / 11: copy / paste the pattern */
                static dn_track_t clip;
                dn_track_t *live = &dn_sq.pat.tr[dn.trk];
                if (o == 9) {
                    clip = *live;
                    dn.pclip_ok = 1;
                    dn_say("PATTERN COPIED");
                } else if (dn.pclip_ok) {
                    uint8_t mute = live->mute, chan = live->chan;
                    dn_lock();
                    *live = clip;
                    live->mute = mute, live->chan = chan;
                    dn_unlock();
                    dn_mark();
                    dn_say("PATTERN PASTED");
                }
                dn.save_chord = 1;
            } else if (o < 8) {                     /* mutes */
                dn_lock();
                dn_sq.pat.tr[o].mute = (uint8_t)!dn_sq.pat.tr[o].mute;
                dn_unlock();
                dn_mark();
            } else if (o == 8) {
                dn.clear_t0 = plat_ms() | 1u;
            } else {
                int32_t pages = ((int32_t)tr->len + 15) / 16;
                dn.view = (uint8_t)dn_clamp((int32_t)dn.view + (o == 9 ? -1 : 1), 0, pages - 1);
                dn.redraw |= DN_RD_BOTTOM;
            }
        } else if ((up & (1u << k)) && o == 8) {
            dn.clear_t0 = 0;
            dn.redraw |= DN_RD_BOTTOM;
        }
    }
}

/* ROLL in KEYS mode: the keys are notes. A key writes its note at the cursor with the entry length and the cursor
 * moves past it; a key pressed under a held one writes a slide; the key sounds as it goes in */
static void dn_keys_entry(uint32_t keys)
{
    uint32_t down = keys & ~dn.keys_prev, up = dn.keys_prev & ~keys, k;
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    for (k = 0; k < NKEYS; k++) {
        if (down & (1u << k)) {
            uint32_t i = dn.roll_cur % tr->len, adv = (dn.entry_len + DN_LEN_STEP - 1u) / DN_LEN_STEP;
            uint8_t note = dn_key_note(k);
            int legato = (dn.keys_prev & ~(1u << k)) != 0u || (keys & ~down) != 0u;
            if (legato && DN_CHORDABLE(dn.trk) && dn.rec_grp < DN_SEQ_STEPS) {
                dn_chord_rec(&tr->st[dn.rec_grp], note);   /* (0.27) under a held key, FM / SAMPLER / MIDI: a chord */
                dn_play(note, 1);
                dn_mark();
                continue;
            }
            dn.rec_grp = (uint8_t)i;
            dn.rec_notes[0] = note, dn.rec_n = 1;
            dn_lock();
            tr->st[i].note = note;
            tr->st[i].len = dn.entry_len;
            tr->st[i].vel = 100;
            tr->st[i].flags = (uint8_t)(DN_ST_ON | (legato ? DN_ST_SLIDE : 0u));
            tr->last_note = note;
            dn_unlock();
            dn_step_remember(dn.trk, &tr->st[i]);
            dn_play(note, legato);
            dn.roll_cur = (uint8_t)((i + (adv ? adv : 1u)) % tr->len);
            dn.view = (uint8_t)(dn.roll_cur / 16u);
            dn_mark();
        }
    }
    if (up && !(keys & ((1u << NKEYS) - 1u))) {
        dn_stop();
        dn.rec_grp = 0xFF;
        if (DN_CHORDABLE(dn.trk))                   /* (poly: every key held lets go) */
            for (k = 0; k < NKEYS; k++)
                if ((up >> k) & 1u) {
                    dn_lock();
                    if (DN_IS_FM(dn.trk))
                        dn_fm_note_off(&dn_fmx, dn_key_note(k));
                    else if (DN_IS_SMP(dn.trk))
                        dn_smp_note_off(&dn_sm, dn_key_note(k));
                    dn_unlock();
                }
    }
}

/* the LFO page's DRAW: the white keys are the points in view (1-16, or 17-32: black keys 10 / 11). A key sets its
 * point to the pen (KNOB 1); held, KNOB 1 moves that point (and the pen). Black keys 1-8: SINE, TRI, SAW, SQUARE,
 * RANDOM into the points, INVERT, REVERSE, CLEAR */
static void dn_keys_draw(uint32_t keys)
{
    uint32_t down = keys & ~dn.keys_prev, up = dn.keys_prev & ~keys, k, i, n;
    dn_lfo_p_t *l = &dn_lp[DN_LSEL];
    n = l->npts == 32u ? 32u : 16u;
    for (k = 0; k < NKEYS; k++) {
        int32_t o = dn_key_ord(k);
        if (dn_is_white(k)) {
            uint32_t pt = (uint32_t)o + 16u * dn.pt_view;
            if ((down & (1u << k)) && pt < n) {
                dn_lock();
                l->pts[pt] = dn.pen;
                if (n == 16u)
                    l->pts[pt + 16u] = dn.pen;      /* (16 points: the second half mirrors, for 32 later) */
                dn_unlock();
                dn.pt_held = (int8_t)pt;
                dn_mark();
            }
            if ((up & (1u << k)) && dn.pt_held == (int8_t)pt)
                dn.pt_held = -1;
        } else if (down & (1u << k)) {
            static const uint8_t SH[5] = {DN_LS_SINE, DN_LS_TRI, DN_LS_SAWDN, DN_LS_SQUARE, DN_LS_SH};
            dn_lock();
            if (o < 5)
                dn_lfo_draw_shape(l, SH[o]);
            else if (o == 5)
                for (i = 0; i < DN_LFO_PTS; i++)
                    l->pts[i] = (int8_t)-l->pts[i];
            else if (o == 6)
                for (i = 0; i < n / 2u; i++) {
                    int8_t t = l->pts[i];
                    l->pts[i] = l->pts[n - 1u - i];
                    l->pts[n - 1u - i] = t;
                }
            else if (o == 7)
                for (i = 0; i < DN_LFO_PTS; i++)
                    l->pts[i] = 0;
            else if (o >= 9)
                dn.pt_view = (uint8_t)(o == 9 || n == 16u ? 0u : 1u);
            dn_unlock();
            dn_mark();
        }
    }
}
static void dn_lfo_draw_knob(uint32_t k, int32_t d)  /* DRAW: KNOB 1 the pen (or the held point), 2 SMOOTH, 3 POINTS,
                                                     * 4 the points in view */
{
    dn_lfo_p_t *l = &dn_lp[DN_LSEL];
    dn_lock();
    if (k == 0u) {
        dn.pen = (int8_t)dn_clamp(dn.pen + d * 5, -100, 100);
        if (dn.pt_held >= 0) {
            l->pts[dn.pt_held] = dn.pen;
            if (l->npts != 32u)
                l->pts[dn.pt_held + 16] = dn.pen;
        }
    } else if (k == 1u) {
        l->smooth = (uint8_t)dn_clamp(l->smooth + d * 4, 0, 127);
    } else if (k == 2u) {
        l->npts = d > 0 ? 32u : 16u;
        if (l->npts == 16u)
            dn.pt_view = 0;
    } else {
        dn.pt_view = (uint8_t)(d > 0 && l->npts == 32u ? 1u : 0u);
    }
    dn_unlock();
    dn_mark();
}

/* SEQ held + white key 1-8: track 1-8, and its instrument's page (on a sequencer page: its sequencer page) */
static void dn_trk_go2(uint32_t t, int seq);
static void dn_trk_go(uint32_t t) { dn_trk_go2(t, DN_IS_SEQPG(dn.page)); }
/* (0.29) track t's instrument: its sound pages (HOME, SEQ held + black key 1-8) */
static void dn_trk_open(uint32_t t)
{
    if (DN_IS_SEQPG(dn.page) || dn.page == DN_PG_HOME)
        dn.page = 0xFFu;                            /* (not a sound page: the instrument's first) */
    dn_trk_go2(t, 0);
}
static void dn_trk_go2(uint32_t t, int seq)
{
    t %= DN_SEQ_TRACKS;
    dn.trk = (uint8_t)t;
    dn.peng = 0xFF;
    if (DN_IS_MIDI(t))
        dn.midi_sel = (uint8_t)dn_midi_idx(t);
    if (DN_IS_BRK(t))
        dn.bk_last = (uint8_t)DN_BK(t);
    if (seq)
        dn_goto(DN_IS_PADS(t) ? DN_PG_DRUM : DN_PG_ROLL);
    else if (DN_IS_OWNPG(t))
        dn_goto(dn.page < DN_NPAGES ? dn.page : 0u);   /* (SAMPLER, FM: their pages) */
    else if (DN_IS_SYN(t)) {
        dn.syn_last = dn_eng[t];
        dn_goto(DN_IS_SNDPG(dn.page) && (DN_IS_SUB(t) || (dn.page != DN_PG_SOSC2 && dn.page != DN_PG_DIRT)) ? dn.page : 0u);
    }
    else if (DN_IS_BRK(t))
        dn_goto(DN_IS_BRKPG(dn.page) ? dn.page : DN_PG_BRK);
    else if (DN_IS_MIDI(t))
        dn_goto(DN_PG_MIDI);
    else
        dn_goto(DN_PG_PROJ);
    dn.trk = (uint8_t)t;                            /* (whatever the page chose) */
    dn.view = (uint8_t)dn_clamp(dn.view, 0, (dn_sq.pat.tr[t].len + 15) / 16 - 1);
    dn_say(dn_inst_name(t));
    dn.redraw |= DN_RD_ALL;
}

static void dn_keys(uint32_t raw)
{
    uint32_t down = raw & ~dn.keys_raw, k, keys;
    dn.keys_raw = raw;
    if (down && (dn_in_buttons() & (1u << B_SEQ))) {   /* SEQ held: the white keys 1-8 are the tracks (their
                                                     * sequencer on a sequencer page); (0.29) the black keys 1-8, OP1..:
                                                     * the track's instrument, its sound pages */
        for (k = 0; k < NKEYS; k++)
            if ((down >> k & 1u) && dn_key_ord(k) < 8) {
                if (dn_is_white(k))
                    dn_trk_go((uint32_t)dn_key_ord(k));
                else
                    dn_trk_open((uint32_t)dn_key_ord(k));
                dn.seq_chord = 1;
            }
        dn.keys_ign |= down;
    }
    dn.keys_ign &= raw;                             /* (a key pressed with SEQ: nothing until it comes up) */
    keys = raw & ~dn.keys_ign;
    if (keys == dn.keys_prev)
        return;
    if (!dn.keys_prev) {                            /* the keys follow the page they were pressed on */
        dn.key_page = dn.page;
        dn.key_mode = dn.roll_keys ? 1u : dn.rec && dn.step_held < 0 ? 2u : 0u;
    }
    if (dn.key_page == DN_PG_HOME) {                /* (0.29) HOME: a key 1-8, white or black, opens that track */
        uint32_t dw = keys & ~dn.keys_prev;
        for (k = 0; k < NKEYS; k++)
            if ((dw >> k & 1u) && dn_key_ord(k) < 8) {
                dn_trk_open((uint32_t)dn_key_ord(k));
                break;
            }
        dn.keys_prev = keys;
        return;
    }
    if (dn.key_page == DN_PG_ROLL && dn.key_mode == 2u)
        dn_keys_play(keys);                         /* (0.27.1) ROLL with REC on: the keys play and record, chords too */
    else if (dn.key_page == DN_PG_ROLL && dn.key_mode)
        dn_keys_entry(keys);
    else if (dn.key_page == DN_PG_LFO && dn.lfo_view == 2u)
        dn_keys_draw(keys);
    else if (DN_IS_SEQPG(dn.key_page))
        dn_keys_seq(keys);
    else
        dn_keys_play(keys);
    dn.keys_prev = keys;
}

static int32_t dn_accel(int32_t span, int32_t d)   /* a detent: one step, faster when turned fast */
{
    if (span > 40 && (d > 2 || d < -2))
        d *= 3;
    return d;
}

/* the view follows a held note: ROLL keeps its pitch a row inside the window, DRUM its slice row */
static void dn_view_follow(uint32_t note)
{
    if (dn.page == DN_PG_ROLL && !DN_IS_PADS(dn.trk)) {
        int32_t lo = dn.roll_lo;
        if ((int32_t)note < lo + 1)
            lo = (int32_t)note - 1;
        if ((int32_t)note > lo + (int32_t)DN_ROLL_ROWS - 2)
            lo = (int32_t)note - (int32_t)DN_ROLL_ROWS + 2;
        dn.roll_lo = (uint8_t)dn_clamp(lo, 0, 127 - DN_ROLL_ROWS + 1);
    } else if (dn.page == DN_PG_DRUM) {
        if (note < dn.drum_top)
            dn.drum_top = (uint8_t)note;
        if (note >= dn.drum_top + DN_DRUM_ROWS)
            dn.drum_top = (uint8_t)(note - DN_DRUM_ROWS + 1u);
    }
    dn.redraw |= DN_RD_ALL;
}

/* pick up and drop: with a step held, pressing another step's key moves the note there (SAVE held: copies it), in
 * this bar or, with black keys 10 / 11, another; onto a note it swaps them (nothing is lost). The held key keeps the
 * note, so pressing on moves it on. */
/* SAVE + REC on a sequencer page (0.24): x2. The bars with notes are copied into the empty bars after them: one bar
 * with notes in a 2- or 4-bar track goes to bar 2; 2 bars in a 4-bar track to bars 3-4. When the notes fill the
 * track, the track doubles (16 -> 32 -> 64 steps) and they're copied into the new half. Pressed again it doubles
 * again: one bar becomes 2, then 4. (Notes count by where they start.) */
static void dn_seq_double(void)
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i, span = tr->len, last = 0, any = 0;
    for (i = 0; i < tr->len; i++)
        if (tr->st[i].flags & DN_ST_ON)
            last = i, any = 1;
    if (!any) {
        dn_say("NOTHING TO COPY");
        return;
    }
    if (last < 16u && tr->len > 16u && tr->len % 16u == 0u)
        span = 16u;
    else if (last < 32u && tr->len == 64u)
        span = 32u;
    if (span == tr->len && tr->len * 2u > DN_SEQ_STEPS) {
        dn_say("FULL: 64 STEPS");
        return;
    }
    dn_lock();
    if (span == tr->len)
        tr->len = (uint8_t)(span * 2u);
    for (i = span; i < 2u * span && i < tr->len; i++)
        tr->st[i] = tr->st[i - span];
    dn_unlock();
    dn_mark();
    if (span % 16u == 0u) {
        static char t[24];
        char v[8];
        t[0] = 0;
        dn_cat(t, "COPIED: ");
        dn_num(v, (int32_t)(2u * span / 16u));
        dn_cat(t, v);
        dn_cat(t, " BARS");
        dn_say(t);
    } else {
        dn_say("COPIED: x2");
    }
    dn.redraw |= DN_RD_ALL;
}

static void dn_step_drop(uint32_t to)
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t from = (uint32_t)dn.step_held;
    int copy = dn.save_held != 0u;
    dn_step_t a = tr->st[from], b = tr->st[to];
    if (to == from)
        return;
    dn_lock();
    tr->st[to] = a;
    if (!copy) {
        if (b.flags & DN_ST_ON)
            tr->st[from] = b;
        else
            tr->st[from] = a, tr->st[from].flags = 0;   /* (the old place: off, its values kept) */
    }
    dn_unlock();
    if (copy)
        dn.save_chord = 1;                          /* (SAVE was a modifier: no save when it comes up) */
    dn.step_held = (int8_t)to;
    dn.step_edited = 1;
    dn_say(copy ? "COPIED" : (b.flags & DN_ST_ON) ? "SWAPPED" : "MOVED");
    dn_mark();
    dn.redraw |= DN_RD_ALL;
}

/* SELECT with a step held: the note moves in time (over the steps that have a note: it never overwrites one), and
 * the 16 steps in view follow it across the bars */
static void dn_step_move(int32_t d)
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    int32_t i = dn.step_held, L = tr->len ? tr->len : 1, to = i, n = d < 0 ? -d : d;
    while (n--) {
        int32_t j = to;
        do
            j += d < 0 ? -1 : 1;
        while (j >= 0 && j < L && (tr->st[j].flags & DN_ST_ON));
        if (j < 0 || j >= L)
            break;                                  /* (the track's ends: no further) */
        to = j;
    }
    if (to == i)
        return;
    dn_lock();
    tr->st[to] = tr->st[i];
    tr->st[i].flags = 0;
    dn_unlock();
    dn.step_held = (int8_t)to;
    dn.step_edited = 1;
    dn.view = (uint8_t)(to / 16);
    dn_mark();
    dn.redraw |= DN_RD_ALL;
}

/* (0.28) the held step's chord, d along OFF, MIN, MAJ ... 6 (KNOB 3 on its third page, or PRESETS; shown on the step's
 * readout: F3m7 by its note, MIN7 under CHORD) */
static void dn_chord_turn(int32_t d)
{
    dn_step_t *st = &dn_sq.pat.tr[dn.trk].st[dn.step_held];
    uint32_t c = (uint32_t)dn_clamp((int32_t)DN_ST_CHORD_OF(st->flags) + d, 0, DN_CHORDS);
    dn_lock();
    st->flags = (uint8_t)((st->flags & ~DN_ST_CHORD) | (c << 4));
    dn_unlock();
    dn_step_remember(dn.trk, st);                   /* (0.29) the next step's chord too */
    dn.step_edited = 1;
    dn_mark();
}

static void dn_seq_knob(uint32_t k, int32_t d)      /* KNOB 1..4 on the SEQ page */
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    if (dn.step_held >= 0 && dn.ramp_b >= 0 && (k == 2u || k == 3u)) {   /* a velocity ramp: its start, its end */
        int32_t a = dn.ramp_a, b = dn.ramp_b, i, n;
        if (k == 2u)
            dn.ramp_v0 = (uint8_t)dn_clamp(dn.ramp_v0 + dn_accel(127, d), 1, 127);
        else
            dn.ramp_v1 = (uint8_t)dn_clamp(dn.ramp_v1 + dn_accel(127, d), 1, 127);
        dn_lock();
        for (i = a, n = b - a; i <= b; i++)         /* the straight line from start to end, over the steps between */
            tr->st[i].vel = (uint8_t)(n ? dn.ramp_v0 + ((int32_t)dn.ramp_v1 - dn.ramp_v0) * (i - a) / n : dn.ramp_v0);
        dn_unlock();
        dn.step_edited = 1;
        dn_mark();
        return;
    }
    if (dn.step_held >= 0 && dn.step_pg) {         /* a step held, FX's second / third page */
        dn_step_t *st = &tr->st[dn.step_held];
        dn_lock();
        if (dn.step_pg == 1u) {                     /* SLIDE (break: reverse), ACCENT, RETRIG, its RATE */
            if (k == 0u)
                st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_SLIDE : st->flags & ~DN_ST_SLIDE);
            else if (k == 1u)
                st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_ACC : st->flags & ~DN_ST_ACC);
            else if (k == 2u)
                st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_RTRG : st->flags & ~DN_ST_RTRG);
            else
                st->rtim = (uint8_t)dn_clamp(st->rtim + d, 0, DN_RTIM_N - 1);
        } else {                                    /* the retrigs' LENGTH (0: the note's), their VELOCITY curve */
            if (k == 0u) {
                int32_t l = st->rlen, n = d < 0 ? -d : d;
                while (n--)
                    l = dn_clamp(l + (d < 0 ? -1 : 1) * (l < 8 ? 1 : l < 32 ? 2 : 4), 0, DN_LEN_MAX);
                st->rlen = (uint8_t)l;
            } else if (k == 1u) {
                st->rvel = (int8_t)dn_clamp(st->rvel + d * 8, -127, 127);
            } else if (k == 2u && DN_CHORDABLE(dn.trk)) {
                dn_unlock();                        /* (0.28) its CHORD */
                dn_chord_turn(d);
                return;
            }
        }
        dn_unlock();
        dn.step_edited = 1;
        dn_mark();
        return;
    }
    if (dn.step_held >= 0) {                        /* a step held: its NOTE (break: slice), LENGTH, VELOCITY,
                                                     * CONDITION (Digitakt's: %, FILL, PRE, NEI, 1ST, LST, A:B) */
        dn_step_t *st = &tr->st[dn.step_held];
        dn_lock();
        if (k == 0u) {
            st->note = (uint8_t)dn_clamp(st->note + d, 0, DN_IS_PADS(dn.trk) ? (int32_t)dn_slices(dn.trk) - 1 : 127);
            tr->last_note = st->note;
            if (dn.page == DN_PG_DRUM)
                dn.drum_row = st->note;             /* the row follows the slice */
            dn_view_follow(st->note);
        }
        else if (k == 1u)
        {                                           /* a quarter step at a time while short, coarser when long */
            int32_t l = st->len, n = d < 0 ? -d : d;
            while (n--)
                l = dn_clamp(l + (d < 0 ? -1 : 1) * (l < 8 ? 1 : l < 32 ? 2 : 4), 1, DN_LEN_MAX);
            st->len = (uint8_t)l;
        }
        else if (k == 2u)
            st->vel = (uint8_t)dn_clamp(st->vel + dn_accel(127, d), 1, 127);
        else
            st->cond = (uint8_t)dn_clamp(st->cond + d, 0, DN_CO_N - 1);
        dn_unlock();
        if (k < 2u)
            dn_step_remember(dn.trk, st);           /* (0.29) its note, length: the next step's */
        dn.step_edited = 1;
        dn_mark();
        return;
    }
    if (dn.page == DN_PG_ROLL) {                    /* ROLL: scroll, transpose (STEP) or entry length (KEYS), length,
                                                     * the bar in view (STEP) or the cursor (KEYS) */
        if (k == 0u) {
            dn.roll_lo = (uint8_t)dn_clamp(dn.roll_lo + d, 0, 127 - DN_ROLL_ROWS + 1);
        } else if (k == 1u && !dn.roll_keys) {
            uint32_t i;
            dn_lock();
            for (i = 0; i < DN_SEQ_STEPS; i++)
                tr->st[i].note = (uint8_t)dn_clamp(tr->st[i].note + d, 0, 127);
            tr->last_note = (uint8_t)dn_clamp(tr->last_note + d, 0, 127);
            dn_unlock();
            dn.roll_lo = (uint8_t)dn_clamp(dn.roll_lo + d, 0, 127 - DN_ROLL_ROWS + 1);
            dn_mark();
        } else if (k == 1u) {
            int32_t l = dn.entry_len, n = d < 0 ? -d : d;
            while (n--)
                l = dn_clamp(l + (d < 0 ? -1 : 1) * (l < 8 ? 1 : l < 32 ? 2 : 4), 1, DN_LEN_MAX);
            dn.entry_len = (uint8_t)l;
        } else if (k == 2u) {
            dn_lock();
            tr->len = (uint8_t)dn_clamp(tr->len + dn_accel(63, d), 1, DN_SEQ_STEPS);
            dn_unlock();
            dn_mark();
        } else if (dn.roll_keys) {
            dn.roll_cur = (uint8_t)(((int32_t)dn.roll_cur + d % (int32_t)tr->len + (int32_t)tr->len) % (int32_t)tr->len);
            dn.view = (uint8_t)(dn.roll_cur / 16u);
        } else {
            dn.view = (uint8_t)dn_clamp(dn.view + d, 0, (tr->len + 15) / 16 - 1);
        }
        dn.view = (uint8_t)dn_clamp(dn.view, 0, (tr->len + 15) / 16 - 1);
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (dn.page == DN_PG_DRUM && k != 1u && k != 2u) {   /* DRUM: K1 the slice row, K4 the bar (K2 length, K3 swing) */
        if (k == 0u) {
            int32_t n = (int32_t)dn_slices(dn.trk);
            dn.drum_row = (uint8_t)dn_clamp(dn.drum_row + d, 0, n - 1);
            if (dn.drum_row < dn.drum_top)
                dn.drum_top = dn.drum_row;
            if (dn.drum_row >= dn.drum_top + DN_DRUM_ROWS)
                dn.drum_top = (uint8_t)(dn.drum_row - DN_DRUM_ROWS + 1);
        } else {
            dn.view = (uint8_t)dn_clamp(dn.view + d, 0, (tr->len + 15) / 16 - 1);
        }
        dn.redraw |= DN_RD_ALL;
        return;
    }
    if (k == 0u) {
        dn.trk = (uint8_t)dn_clamp(dn.trk + d, 0, DN_SEQ_TRACKS - 1);
        dn.view = 0;
        dn.redraw |= DN_RD_ALL;
    } else if (k == 1u) {
        dn_lock();
        tr->len = (uint8_t)dn_clamp(tr->len + dn_accel(63, d), 1, DN_SEQ_STEPS);
        dn_unlock();
        dn.view = (uint8_t)dn_clamp(dn.view, 0, (tr->len + 15) / 16 - 1);
        dn_mark();
    } else if (k == 2u) {
        dn_lock();
        dn_sq.pat.swing = (uint8_t)dn_clamp(dn_sq.pat.swing + d, 50, 75);
        dn_unlock();
        dn_mark();
    } else {
        dn.view = (uint8_t)dn_clamp(dn.view + d, 0, (tr->len + 15) / 16 - 1);
        dn.redraw |= DN_RD_BOTTOM;
    }
}

/* the roll's pitch window: the octave holding the most of the track's notes (or around the last note played) */
static void dn_roll_fit(void)
{
    const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t lo, i, best = 0, best_lo = 0;
    for (lo = 0; lo + DN_ROLL_ROWS <= 128u; lo++) {
        uint32_t n = 0;
        for (i = 0; i < tr->len; i++)
            n += (tr->st[i].flags & DN_ST_ON) && tr->st[i].note >= lo && tr->st[i].note < lo + DN_ROLL_ROWS;
        if (n > best)
            best = n, best_lo = lo;
    }
    if (!best)
        best_lo = (uint32_t)dn_clamp((int32_t)tr->last_note - DN_ROLL_ROWS / 2, 0, 128 - DN_ROLL_ROWS);
    else
        best_lo = (uint32_t)dn_clamp((int32_t)best_lo - 1, 0, 128 - DN_ROLL_ROWS);   /* a row under the lowest */
    dn.roll_lo = (uint8_t)best_lo;
}

static void dn_goto(uint32_t page)
{
    if (dn.page != page) {
        dn.page = (uint8_t)page;
        dn.step_held = -1;
        if (DN_IS_SNDPG(page) && !DN_IS_SYN(dn.trk) &&
            !(DN_IS_OWNPG(dn.trk) && (page < DN_NPAGES || page == DN_PG_SET))) {   /* (SETUP: any instrument's) */
            /* a synth's pages: the synth shown last (REESE first); (SAMPLER keeps its own five) */
            uint32_t t = dn_trk_of(dn.syn_last);
            if (t >= 8u)
                t = dn_trk_of(dn.syn_last == DN_E_SUB ? DN_E_REESE : DN_E_SUB);
            if (t < 8u)
                dn.trk = (uint8_t)t;
        }
        if (DN_IS_SNDPG(page) && DN_IS_SYN(dn.trk))
            dn.syn_last = dn_eng[dn.trk];
        if (page == DN_PG_FENV && !DN_IS_SYN(dn.trk))
            dn.page = (uint8_t)(page = 2u);
        if ((page == DN_PG_SOSC2 || page == DN_PG_DIRT) && !DN_IS_SUB(dn.trk))
            dn.page = (uint8_t)(page = page == DN_PG_SOSC2 ? 0u : 1u);   /* (REESE has no second pages) */
        else if (page == DN_PG_MIDI && dn_midi_trk(dn.midi_sel) < 8u)
            dn.trk = (uint8_t)dn_midi_trk(dn.midi_sel);
        if (page == DN_PG_DRUM && !DN_IS_PADS(dn.trk)) {   /* DRUM edits a break track (the last one shown), or
                                                     * SAMPLER's pads */
            uint32_t t = dn_trk_of(DN_E_BRKA + dn.bk_last % 2u);
            if (t >= 8u)
                t = dn_trk_of(DN_E_BRKA + (dn.bk_last + 1u) % 2u);
            if (t < 8u)
                dn.trk = (uint8_t)t;
            else
                dn.page = (uint8_t)(page = DN_PG_ROLL);   /* (no break on any track: the roll) */
        }
        if (page == DN_PG_ROLL) {                   /* ROLL edits a synth or MIDI track */
            if (DN_IS_PADS(dn.trk))
                dn.trk = (uint8_t)dn_rs_trk();
            dn_roll_fit();
        }
        if (page == DN_PG_DRUM)
            dn.drum_row = (uint8_t)dn_clamp(dn.drum_row, 0, (int32_t)dn_slices(dn.trk) - 1);
        if (page == DN_PG_SLICE) {                  /* chop mode: the selection in range, the cursor on it */
            dn_sl_fix();
            dn.sl_cur = dn_bk[dn_bsel()].start[dn.sl_sel];
            dn_sl_show(&dn_bk[dn_bsel()]);
        } else if ((dn.chop_rec = 0), dn_bk[0].aud || dn_bk[1].aud) {   /* leaving it: keys play; the loop stops */
            dn_lock();
            if (dn_bk[0].aud) dn_brk_cut(&dn_bk[0]), dn_bk[0].aud = 0;
            if (dn_bk[1].aud) dn_brk_cut(&dn_bk[1]), dn_bk[1].aud = 0;
            dn_unlock();
        }
        dn.view = (uint8_t)dn_clamp(dn.view, 0, (dn_sq.pat.tr[dn.trk].len + 15) / 16 - 1);   /* (another track) */
        dn.redraw |= DN_RD_ALL;
    }
}

/* ---- chop mode (the SLICE page) ------------------------------------------------------------------------------
 * A cursor through the loop (KNOB 2 coarse, KNOB 3 fine, KNOB 4 zoom); REC chops at it (or, with the loop playing:
 * PLAY on this page, at the place playing: tap to chop by ear); KNOB 1 or a key selects a chop, FX moves it to the
 * cursor, HOME deletes it (held 1 s: clears them all). Editing makes the loop's slices MANUAL (keeping the ones you
 * see); MANUAL from nothing starts empty: one slice, the whole loop. The pattern's steps keep playing the same audio
 * when chops come and go (dn_chops_remap), and the chops go to flash a few seconds after the last change. */

static uint32_t dn_sl_width(const dn_brk_t *b)
{
    uint32_t w = b->loop.n >> dn.sl_zoom;
    return w < 256u ? 256u : w;
}
static void dn_sl_show(const dn_brk_t *b)            /* the view keeps the cursor in sight (no jumping about) */
{
    uint32_t w = dn_sl_width(b), n = b->loop.n;
    if (dn.sl_cur < dn.sl_view + w / 8u)
        dn.sl_view = dn.sl_cur > w / 8u ? dn.sl_cur - w / 8u : 0u;
    else if (dn.sl_cur > dn.sl_view + w - w / 8u)
        dn.sl_view = dn.sl_cur - (w - w / 8u);
    if (dn.sl_view + w > n)
        dn.sl_view = n > w ? n - w : 0u;
}
static void dn_sl_fix(void)                          /* the selection always inside the table (the 0.7 fault) */
{
    const dn_brk_t *b = &dn_bk[dn_bsel()];
    if (b->nslices && dn.sl_sel >= b->nslices)
        dn.sl_sel = (uint8_t)(b->nslices - 1u);
    if (dn.sl_cur >= b->loop.n)
        dn.sl_cur = 0;
    dn_sl_show(b);
}
/* every step that plays a chop of this loop moves to the chop now starting nearest where its chop started */
static void dn_chops_remap(uint32_t key, const uint32_t *old, uint32_t old_n)
{
    uint32_t t, i, j;
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        const dn_brk_t *b = &dn_bk[DN_IS_BRK(t) ? DN_BK(t) : 0u];
        dn_track_t *tr = &dn_sq.pat.tr[t];
        if (!DN_IS_BRK(t) || b->key != key || !b->ok)
            continue;
        for (i = 0; i < DN_SEQ_STEPS; i++) {
            uint32_t at, best = 0, bd = 0xFFFFFFFFu;
            if (!(tr->st[i].flags & DN_ST_ON) || tr->st[i].note >= old_n)
                continue;
            at = old[tr->st[i].note];
            for (j = 0; j < b->nslices; j++) {
                uint32_t d = b->start[j] > at ? b->start[j] - at : at - b->start[j];
                if (d < bd)
                    bd = d, best = j;
            }
            tr->st[i].note = (uint8_t)best;
        }
    }
}
/* the chops of BREAKS bi change: fn edits man_start / n_man under the lock; the table, the other voice playing the
 * same loop, the pattern, the store and the flash follow */
static void dn_sl_begin(dn_brk_t *b)
{
    uint32_t i;
    for (i = 0; i < b->nslices; i++)
        dn_sl_old[i] = b->start[i];
    if (b->smode != 2u) {                           /* editing what you see: it becomes MANUAL as it is */
        for (i = 0; i < b->nslices; i++)
            b->man_start[i] = b->start[i];
        b->n_man = b->nslices;
        b->smode = 2;
    }
}
static void dn_sl_end(uint32_t bi, uint32_t old_n)
{
    dn_brk_t *b = &dn_bk[bi];
    uint32_t k;
    dn_brk_slices_apply(b);
    dn_man_keep(b);
    for (k = 0; k < DN_NBRK; k++)                   /* the other BREAKS on the same loop, if MANUAL */
        if (k != bi && dn_bk[k].key == b->key && dn_bk[k].smode == 2u)
            dn_bk[k].n_man = 0, dn_unlock(), dn_man_restore(&dn_bk[k]), dn_lock();
    dn_chops_remap(b->key, dn_sl_old, old_n);
    dn.chop_t = plat_ms() | 1u;
    dn_mark();
    dn_sl_fix();
}

static void dn_say(const char *m)                   /* a word on the status line for 1.5 s */
{
    dn.msg = m;
    dn.msg_t = plat_ms() + 1500u;
    dn.redraw |= DN_RD_ALL;
}

/* a new chop (REC). On (or right by) a chop it splits the slice the cursor is in, in the middle: with one chop (the
 * whole loop, the cursor at its start) REC halves the loop, and turning on from there finds the hits. Never silent:
 * full or too short says so. */
static void dn_chop_at(uint32_t at, int split)
{
    uint32_t bi = dn_bsel(), i, ns, old_n;
    dn_brk_t *b = &dn_bk[bi];
    if (!b->ok)
        return;
    at = dn_brk_zero_near(b, at, 32u);
    dn_lock();
    old_n = b->nslices;
    dn_sl_begin(b);
    ns = b->n_man;
    for (i = 0; i < ns && b->man_start[i] < at; i++)
        ;
    if (ns >= DN_BRK_MAXSL) {
        dn_unlock();
        dn_say("FULL: 64 CHOPS");
        return;
    }
    if ((i < ns && b->man_start[i] < at + 64u) || (i > 0u && at < b->man_start[i - 1u] + 64u)) {
        uint32_t j = i < ns && b->man_start[i] < at + 64u ? i : i - 1u, lo, hi;   /* the chop it's on */
        if (!split) {
            dn_unlock();
            dn_say("TOO CLOSE TO A CHOP");
            return;
        }
        lo = b->man_start[j];                       /* that chop's slice, split in the middle */
        hi = j + 1u < ns ? b->man_start[j + 1u] : b->loop.n;
        if (hi < lo + 256u) {
            dn_unlock();
            dn_say("SLICE TOO SHORT TO SPLIT");
            return;
        }
        dn_unlock();
        at = dn_brk_zero_near(b, lo + (hi - lo) / 2u, 32u);
        dn_lock();
        for (i = 0; i < ns && b->man_start[i] < at; i++)
            ;
        if ((i < ns && b->man_start[i] < at + 64u) || (i > 0u && at < b->man_start[i - 1u] + 64u)) {
            dn_unlock();
            dn_say("SLICE TOO SHORT TO SPLIT");
            return;
        }
        dn.sl_cur = at;
        dn_say("SPLIT IN HALF");
    }
    for (ns = b->n_man; ns > i; ns--)
        b->man_start[ns] = b->man_start[ns - 1u];
    b->man_start[i] = at;
    b->n_man++;
    dn_sl_end(bi, old_n);
    dn_unlock();
    dn.sl_sel = (uint8_t)i;
    dn.redraw |= DN_RD_ALL;
}
/* the keys cut (REC lit on SLICE): key c's chop goes to `at` (where the loop plays, or the cursor). An existing chop
 * moves there, the next one (one past the last) is added; the chops stay in order, so a key whose place would land
 * before the chop below it or after the one above says so and cuts nothing. Nothing renumbers: the pattern's steps on
 * that chop now play from its new place. */
static void dn_chop_key(uint32_t c, uint32_t at)
{
    static char m[32];
    uint32_t bi = dn_bsel(), ns, old_n, i;
    dn_brk_t *b = &dn_bk[bi];
    char v[8];
    if (!b->ok)
        return;
    at = dn_brk_zero_near(b, at, 32u);
    dn_lock();
    old_n = b->nslices;
    dn_sl_begin(b);
    ns = b->n_man;
    m[0] = 0;
    if (c > ns || c >= DN_BRK_MAXSL) {              /* (a key past the next one: the ones before it first) */
        dn_cat(m, "NEXT IS CHOP ");
        dn_num(v, (int32_t)ns + 1);
        dn_cat(m, v);
    } else if (c > 0u && at < b->man_start[c - 1u] + 64u) {
        dn_cat(m, "CHOP ");
        dn_num(v, (int32_t)c + 1);
        dn_cat(m, v);
        dn_cat(m, " MUST COME AFTER ");
        dn_num(v, (int32_t)c);
        dn_cat(m, v);
    } else if (c + 1u < ns && at + 64u > b->man_start[c + 1u]) {
        dn_cat(m, "CHOP ");
        dn_num(v, (int32_t)c + 1);
        dn_cat(m, v);
        dn_cat(m, " MUST COME BEFORE ");
        dn_num(v, (int32_t)c + 2);
        dn_cat(m, v);
    } else if (at + 64u > b->loop.n) {
        dn_cat(m, "TOO NEAR THE END");
    }
    if (m[0]) {
        dn_unlock();
        dn_say(m);
        return;
    }
    for (i = 0; i < old_n && i < DN_BRK_MAXSL; i++)
        if (i == c)
            dn_sl_old[i] = at;                      /* (the steps on chop c stay on chop c) */
    b->man_start[c] = at;
    if (c == ns)
        b->n_man++;
    dn_sl_end(bi, old_n);
    dn_unlock();
    dn.sl_sel = (uint8_t)c;
    if (!b->aud)
        dn.sl_cur = at;
    dn_say(c == ns ? "ADDED" : "MOVED");
}

static void dn_chop_move(void)                      /* the selected chop to the cursor (FX) */
{
    uint32_t bi = dn_bsel(), old_n, ns, lo, hi, at;
    dn_brk_t *b = &dn_bk[bi];
    if (!b->ok)
        return;
    dn_lock();
    old_n = b->nslices;
    dn_sl_begin(b);
    ns = b->n_man;
    if (dn.sl_sel >= ns) {
        dn_unlock();
        return;
    }
    lo = dn.sl_sel ? b->man_start[dn.sl_sel - 1u] + 64u : 0u;
    hi = dn.sl_sel + 1u < ns ? b->man_start[dn.sl_sel + 1u] - 64u : b->loop.n - 64u;
    at = dn_brk_zero_near(b, dn.sl_cur, 32u);
    b->man_start[dn.sl_sel] = at < lo ? lo : at > hi ? hi : at;
    dn_sl_end(bi, old_n);
    dn_unlock();
    dn.redraw |= DN_RD_ALL;
}
static void dn_chop_delete(int all)                 /* the selected chop (HOME), or all of them (HOME held 1 s) */
{
    uint32_t bi = dn_bsel(), old_n, ns, i;
    dn_brk_t *b = &dn_bk[bi];
    if (!b->ok)
        return;
    dn_lock();
    old_n = b->nslices;
    dn_sl_begin(b);
    ns = b->n_man;
    if (all) {
        b->man_start[0] = 0;
        b->n_man = 1;
    } else if (ns > 1u && dn.sl_sel < ns) {
        for (i = dn.sl_sel; i + 1u < ns; i++)
            b->man_start[i] = b->man_start[i + 1u];
        b->n_man--;
        if (dn.sl_sel)
            dn.sl_sel--;
    }
    dn_sl_end(bi, old_n);
    dn_unlock();
    dn.redraw |= DN_RD_ALL;
}
static void dn_chop_copy(int grid)                   /* MANUAL from AUTO (SAVE + OCT-) or GRID (SAVE + OCT+) */
{
    uint32_t bi = dn_bsel(), old_n, i, ns;
    dn_brk_t *b = &dn_bk[bi];
    if (!b->ok)
        return;
    dn_lock();
    old_n = b->nslices;
    dn_sl_begin(b);
    ns = grid ? dn_brk_grid_slices(b) : b->n_auto;
    for (i = 0; i < ns && i < DN_BRK_MAXSL; i++)
        b->man_start[i] = grid ? i * (b->loop.n / ns) : b->auto_start[i];
    b->n_man = (uint8_t)(ns ? ns : 1u);
    dn_sl_end(bi, old_n);
    dn_unlock();
    dn.redraw |= DN_RD_ALL;
}

static void dn_slice_knob(uint32_t k, int32_t d)
{
    dn_brk_t *b = &dn_bk[dn_bsel()];
    uint32_t w = dn_sl_width(b);
    int32_t at = (int32_t)dn.sl_cur;
    if (!b->ok)
        return;
    if (k == 0u && b->nslices <= 1u) {              /* one chop (empty): KNOB 1 moves the cursor (as KNOB 2) */
        k = 1u;
        at += d * (int32_t)(w / 8u) * (d > 2 || d < -2 ? 4 : 1);
    } else if (k == 0u) {                           /* CHOP: select, the cursor to it, hear it */
        dn.sl_sel = (uint8_t)dn_clamp(dn.sl_sel + d, 0, (int32_t)b->nslices - 1);
        dn.sl_cur = b->start[dn.sl_sel];
        if (!b->aud) {
            dn_lock();
            dn_brk_hit(dn_bsel(), dn.sl_sel, 100, 0);
            dn_unlock();
        }
    } else if (k == 1u) {                           /* CURSOR: an eighth of the view a detent (4x turned fast) */
        at += d * (int32_t)(w / 8u) * (d > 2 || d < -2 ? 4 : 1);
    } else if (k == 2u) {                           /* FINE: 4 samples a detent (32 turned fast) */
        at += d * (d > 2 || d < -2 ? 32 : 4);
    } else {                                        /* ZOOM, around the cursor */
        dn.sl_zoom = (uint8_t)dn_clamp(dn.sl_zoom + d, 0, 6);
        dn.sl_view = dn.sl_cur > dn_sl_width(b) / 2u ? dn.sl_cur - dn_sl_width(b) / 2u : 0u;
    }
    if (k == 1u || k == 2u)
        dn.sl_cur = (uint32_t)dn_clamp(at, 0, (int32_t)b->loop.n - 1);
    dn_sl_show(b);
    dn.redraw |= DN_RD_ALL;
}

/* the chops reach flash a few seconds after the last change (the slot store; the sound's MANUAL with it) */
static void dn_chops_flush(void)
{
    dn_save_t *svp = &dn_sv;
    uint32_t k;
    if (!dn.chop_t || (int32_t)(plat_ms() - dn.chop_t) < 3000)
        return;
#define sv (*svp)
    dn.chop_t = 0;
    dn_man.magic = DN_MAN_MAGIC;
    dn_store(OBJ_SONG, &dn_man, sizeof dn_man);
    if (plat_store_load(OBJ_SOUND, &sv, sizeof sv) == (int)sizeof sv && sv.magic == DN_SAVE_MAGIC) {
        for (k = 0; k < DN_NBRK; k++)
            sv.brk2[k].smode = dn_bk[k].smode;
        dn_store(OBJ_SOUND, &sv, sizeof sv);
    } else {                                        /* nothing saved yet: the sound as it is, so MANUAL comes back */
        dn_save_fill(&sv);
        dn_store(OBJ_SOUND, &sv, sizeof sv);
    }
#undef sv
}

/* copy / paste: SAVE held with OCT- copies the bar in view of the selected track, with OCT+ pastes it there */
static void dn_copy_paste(int paste)
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i, at = 16u * dn.view;
    if (!paste) {
        for (i = 0; i < 16u; i++)
            dn.clip[i] = tr->st[(at + i) % DN_SEQ_STEPS];
        dn.clip_ok = 1;
        return;
    }
    if (!dn.clip_ok)
        return;
    dn_lock();
    for (i = 0; i < 16u && at + i < DN_SEQ_STEPS; i++)
        tr->st[at + i] = dn.clip[i];
    dn_unlock();
    dn_mark();
}

/* ---- the name editor (0.22): the project's name, or a user sound's. KNOB 1 / SELECT the place, KNOB 2 / PRESETS the
 * letter there, SAVE keeps it (and saves), HOME leaves it as it was ---- */
static const char DN_NM_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#&!.+";
static void dn_name_open(uint32_t what, uint32_t set, uint32_t slot, const char *from)
{
    uint32_t i, len = what == 1u ? 11u : 8u, n = 0;
    dn.naming = (uint8_t)what, dn.nm_set = (uint8_t)set, dn.nm_slot = (uint8_t)slot, dn.nm_len = (uint8_t)len;
    while (from && n < len && from[n])
        n++;
    for (i = 0; i < len; i++) {
        char c = i < n ? from[i] : ' ';
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        dn.nm[i] = c;
    }
    dn.nm[len] = 0;
    dn.nm_cur = (uint8_t)(n < len ? n : len - 1u);
    dn.redraw |= DN_RD_ALL;
}
static void dn_name_input(uint32_t press)
{
    int32_t d, k;
    uint32_t i;
    if ((d = dn_in_enc(EN_K1)) != 0 || (d = dn_in_enc(EN_SELECT)) != 0)
        dn.nm_cur = (uint8_t)dn_clamp((int32_t)dn.nm_cur + d, 0, (int32_t)dn.nm_len - 1);
    if ((d = dn_in_enc(EN_K2)) != 0 || (d = dn_in_enc(EN_PRESET)) != 0) {
        int32_t n = (int32_t)sizeof DN_NM_CH - 1, at = 0;
        for (k = 0; k < n; k++)
            if (DN_NM_CH[k] == dn.nm[dn.nm_cur])
                at = k;
        dn.nm[dn.nm_cur] = DN_NM_CH[((at + d) % n + n) % n];
    }
    (void)dn_in_enc(EN_K3), (void)dn_in_enc(EN_K4), (void)dn_in_enc(EN_ALGO);   /* (the others do nothing here) */
    if (press & (1u << B_SAVE)) {                   /* keep it: trailing spaces off, saved */
        char out[12];
        uint32_t len = dn.nm_len;
        while (len && dn.nm[len - 1u] == ' ')
            len--;
        for (i = 0; i < 12u; i++)
            out[i] = i < len ? dn.nm[i] : 0;
        if (dn.naming == 1u) {
            if (!len)
                out[0] = 'P', out[1] = '1', out[2] = 0;
            for (i = 0; i < sizeof dn_pname; i++)
                dn_pname[i] = i < 11u ? out[i] : 0;
        } else {
            for (i = 0; i < 8u; i++)
                dn_man.uname[dn.nm_set][dn.nm_slot % DN_NUSER][i] = out[i];
        }
        dn.naming = 0;
        dn_rev++;
        dn_save();
        dn_say("NAMED");
        dn.redraw |= DN_RD_ALL;
    } else if (press & (1u << B_HOME)) {
        dn.naming = 0;
        dn.home_chord = 1;                          /* (its release: not a tap to HOME) */
        dn_say("NAME LEFT AS IT WAS");
        dn.redraw |= DN_RD_ALL;
    } else if (press || d) {
        dn.redraw |= DN_RD_BOTTOM;
    }
    dn.redraw |= DN_RD_BOTTOM;
}

void dn_app_input(void)
{
    uint32_t btn = dn_in_buttons(), keys = dn_in_keys(), press = btn & ~dn.btn_prev, rel = dn.btn_prev & ~btn, k;
    int32_t d;
    dn.btn_prev = btn;
    if (keys != dn.keys_raw)
        dn_keys(keys);
    dn_chops_flush();
    if (dn.naming) {                                /* (0.22) the name editor has the controls */
        dn_name_input(press);
        return;
    }
    /* (0.22) HOME held + SELECT: the next / last track and its instrument's pages (HOME's own job then waits: a hold
     * on release, chop deletes cancelled, no FILL) */
    if (press & (1u << B_HOME))
        dn.home_chord = 0, dn.home_dn = plat_ms() | 1u;
    if ((btn & (1u << B_HOME)) && (d = dn_in_enc(EN_SELECT)) != 0) {
        static char t[24];
        uint32_t tr = (uint32_t)(((int32_t)dn.trk + d) % (int32_t)DN_SEQ_TRACKS + (int32_t)DN_SEQ_TRACKS) % DN_SEQ_TRACKS;
        dn.home_chord = 1;
        dn.home_done = 1;                           /* (SLICE: no chop deleted when HOME comes up) */
        dn_sq.fill = 0;
        dn_trk_go(tr);
        t[0] = 'T', t[1] = (char)('1' + tr), t[2] = ' ', t[3] = 0;
        dn_cat(t, dn_inst_name(tr));
        dn_say(t);
    }
    /* (0.22) SAVE held + FX: name the project (PROJECT) or the user sound loaded (a synth's pages) */
    if ((press & (1u << B_FX)) && dn.save_held && !DN_IS_SEQPG(dn.page) && dn.page != DN_PG_SLICE) {
        if (dn.page == DN_PG_PROJ) {
            dn_name_open(1u, 0u, 0u, dn_pname);
            press &= ~(1u << B_FX);
            dn.save_chord = 1;
        } else if (DN_IS_SNDPG(dn.page) && DN_IS_SYN(dn.trk)) {
            uint32_t set = DN_IS_SUB(dn.trk) ? 1u : 0u, nf = set ? dn_sub_preset_count() : reese_preset_count();
            uint32_t cur = set ? dn.sb_preset : dn.preset;
            if (cur >= nf && dn_user[set][(cur - nf) % DN_NUSER].used)
                dn_name_open(2u, set, (cur - nf) % DN_NUSER, dn_sound_name(set, cur));
            else
                dn_say("STORE IT FIRST: SAVE+PRESETS");
            press &= ~(1u << B_FX);
            dn.save_chord = 1;
        }
    }
    if (dn.page == DN_PG_SLICE) {                   /* chop mode's buttons (before the usual ones take them) */
        dn_brk_t *bk = &dn_bk[dn_bsel()];
        if ((press & (1u << B_REC)) && dn.save_held) {   /* SAVE + REC: a chop between the others, at the cursor (or
                                                     * where the loop plays); the ones after it move up a key */
            uint32_t at = dn.sl_cur;
            if (bk->aud) {
                uint32_t lat = (bk->loop.rate ? bk->loop.rate : 22050u) * DN_TAP_LATENCY_MS / 1000u, pos;
                dn_lock();
                pos = dn_brk_pos(bk);
                dn_unlock();
                at = pos > lat ? pos - lat : 0u;
            }
            dn_chop_at(at, !bk->aud);
            dn.save_chord = 1;
        } else if (press & (1u << B_REC)) {         /* REC: the keys cut (lit) / play (off) */
            dn.chop_rec = (uint8_t)!dn.chop_rec;
            dn_say(dn.chop_rec ? "KEYS CUT" : "KEYS PLAY");
        }
        if (press & (1u << B_FX))
            dn_chop_move();
        if (press & (1u << B_PLAY)) {               /* the loop round and round, from the cursor, to chop by ear */
            dn_lock();
            if (bk->aud)
                dn_brk_cut(bk), bk->aud = 0;
            else
                dn_brk_audition(bk, dn.sl_cur);
            dn_unlock();
        }
        if (press & (1u << B_HOME))
            dn.home_t0 = plat_ms() | 1u, dn.home_done = 0;
        if (dn.home_t0 && (btn & (1u << B_HOME)) && !dn.home_done && (int32_t)(plat_ms() - dn.home_t0) > 1000) {
            dn_chop_delete(1);                      /* held 1 s: every chop goes */
            dn.home_done = 1;
        }
        if (dn.home_t0 && !(btn & (1u << B_HOME))) {
            if (!dn.home_done)
                dn_chop_delete(0);                  /* a tap: the selected chop goes */
            dn.home_t0 = 0;
        }
        press &= ~((1u << B_REC) | (1u << B_FX) | (1u << B_PLAY) | (1u << B_HOME));
        dn.redraw |= DN_RD_TOP;
    }
    if (dn.clear_t0 && (int32_t)(plat_ms() - dn.clear_t0) > 1000) {   /* black key 9 held 1 s: the track's steps cleared */
        dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        dn_lock();
        for (k = 0; k < DN_SEQ_STEPS; k++)
            tr->st[k].flags = 0;
        dn_unlock();
        dn.clear_t0 = 0;
        dn_mark();
    }
    if (press & (1u << B_SAVE)) {                   /* SAVE acts when it comes up, unless OCT was used with it */
        dn.save_held = 1;
        dn.save_chord = 0;
    }
    if (dn.save_held && (press & ((1u << B_OCTDN) | (1u << B_OCTUP)))) {
        if (dn.page == DN_PG_SLICE)
            dn_chop_copy((press & (1u << B_OCTUP)) != 0u);   /* chops: from AUTO (OCT-) / GRID (OCT+) */
        else
            dn_copy_paste((press & (1u << B_OCTUP)) != 0u);
        dn.save_chord = 1;
        press &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }
    if (dn.save_held && !(btn & (1u << B_SAVE))) {
        dn.save_held = 0;
        if (dn.store) {                             /* SAVE + PRESETS: the synth's sound into that user slot, kept */
            uint32_t set = DN_IS_SUB(dn.trk) ? 1u : 0u, u = dn.store - 1u, j;
            static char t[16];
            char was[10];                           /* (0.22: the name it goes in with: the sound's own) */
            const char *w = dn_sound_name(set, set ? dn.sb_preset : dn.preset);
            for (j = 0; j < 9u; j++)
                was[j] = w[j];
            was[9] = 0;
            dn_patch_store(set, &dn_user[set][u]);
            if (set)
                dn.sb_preset = (uint8_t)(dn_sub_preset_count() + u);
            else
                dn.preset = (uint8_t)(reese_preset_count() + u);
            dn.store = 0;
            dn_save();
            t[0] = 0;
            dn_cat(t, "STORED USER ");
            t[12] = (char)('1' + u), t[13] = 0;
            dn_say(t);
            for (j = 0; j < 8u; j++)
                dn_man.uname[set][u][j] = 0;        /* (a new sound in the slot: its old name goes) */
            dn_name_open(2u, set, u, was[0] == 'U' && was[1] == 'S' && was[4] == ' ' && was[5] >= '1' && was[5] <= '8'
                                         ? "" : was);   /* (then a name for it: SAVE keeps, HOME leaves "USER n") */
        } else if (!dn.save_chord)
            dn_save();
    }
    if ((btn & (1u << B_OCTDN)) && (btn & (1u << B_OCTUP)))
        press &= ~((1u << B_OCTDN) | (1u << B_OCTUP));          /* both: the update-mode hold, not an octave */
    else if ((press & ((1u << B_OCTDN) | (1u << B_OCTUP))) && DN_IS_SEQPG(dn.page) && dn.step_held >= 0 &&
             !DN_IS_BRK(dn.trk)) {
        dn_seq_knob(0, (press & (1u << B_OCTUP)) ? 12 : -12);   /* a step held: its note an octave up / down */
    } else if (press & (1u << B_OCTDN))
        dn_set(DN_PX_OCT, dn.oct - 1);
    else if (press & (1u << B_OCTUP))
        dn_set(DN_PX_OCT, dn.oct + 1);
    for (k = 0; k < DN_NPAGES; k++) {
        if (!(press & (1u << DN_PAGE_BTN[k])))
            continue;
        if (k == 2u && DN_IS_SYN(dn.trk) && (dn.page == 2u || dn.page == DN_PG_FENV)) {
            dn_goto(dn.page == 2u ? DN_PG_FENV : 2u);   /* ENV again: AMP <-> FILT ENV (0.19) */
        } else if (k <= 1u && DN_IS_SUB(dn.trk) && (dn.page == k || dn.page == (k ? DN_PG_DIRT : DN_PG_SOSC2))) {
            /* SUB: SEL again OSC <-> OSC2 (PW, PWM, the sub), EDIT again FILTER <-> DIRT */
            dn_goto(dn.page == k ? (k ? DN_PG_DIRT : DN_PG_SOSC2) : k);
        } else if (DN_IS_FM(dn.trk) && k >= 1u && k <= 3u && dn.page == k) {
            dn.fm_alt ^= (uint8_t)(1u << k);        /* FM: OP again OP+, EG again its levels, LFO again LFO+ */
            dn.redraw |= DN_RD_ALL;
        } else if (k == 3u && !DN_IS_OWNPG(dn.trk) && (dn.page == 3u || dn.page == DN_PG_LFO)) {   /* LFO again: LFO 1, 2, 3, 4, back to MOD */
                if (dn.page == DN_PG_LFO && dn.lfo_sel + 1u >= DN_NLFO) {
                    dn_goto(3u);
                } else {
                    dn.lfo_sel = dn.page == DN_PG_LFO ? (uint8_t)(dn.lfo_sel + 1u) : 0u;
                    dn.lfo_view = 0;
                    dn_goto(DN_PG_LFO);
                    dn.redraw |= DN_RD_ALL;
                }
            } else
                dn_goto(k != 4u ? k : dn.page == 4u ? DN_PG_FX : dn.page == DN_PG_FX ? DN_PG_SET
                        : dn.page == DN_PG_SET ? DN_PG_MIDI : dn.page == DN_PG_MIDI ? DN_PG_PROJ : 4u);   /* GLO again:
                                                     * FX, SETUP, the MIDI instruments, PROJECT, then OUT */
        break;
    }
    if (press & (1u << B_SEQ))
        dn.seq_held = 1, dn.seq_chord = 0;
    if (dn.seq_held && !(btn & (1u << B_SEQ))) {    /* SEQ acts as it comes up: held, a white key 1-8 picks a track */
        dn.seq_held = 0;
        if (!dn.seq_chord)
            press |= 1u << B_SEQ;
    } else {
        press &= ~(1u << B_SEQ);
    }
    if (press & (1u << B_SEQ)) {                    /* SEQ: this instrument's sequencer page; again: back */
        if (DN_IS_SEQPG(dn.page)) {
            dn_goto(dn.back_page);
        } else {
            uint32_t back = dn.page;
            if (DN_IS_BRKPG(dn.page)) {
                dn_goto(DN_PG_DRUM);                /* (the break the BRK pages show) */
            } else {
                if (dn.page == DN_PG_MIDI && dn_midi_trk(dn.midi_sel) < 8u)
                    dn.trk = (uint8_t)dn_midi_trk(dn.midi_sel);
                else if (DN_IS_SNDPG(dn.page) && !DN_IS_SYN(dn.trk) && !DN_IS_OWNPG(dn.trk) &&
                         dn_trk_of(dn.syn_last) < 8u)
                    dn.trk = (uint8_t)dn_trk_of(dn.syn_last);   /* (a synth's pages: that synth's roll) */
                dn_goto(DN_IS_PADS(dn.trk) ? DN_PG_DRUM : DN_PG_ROLL);
            }
            dn.back_page = (uint8_t)back;
        }
    }
    if ((press & (1u << B_FX)) && (dn.page == 2u || dn.page == DN_PG_FENV) && DN_IS_SYN(dn.trk)) {   /* AMP: FX locks the envelope's times to the
                                                     * tempo (ATK DEC REL as note lengths) or frees them */
        uint8_t *e = DN_IS_SUB(dn.trk) ? &dn_sb.esync : &dn_rs.esync;
        dn_lock();
        *e = (uint8_t)!*e;
        dn_unlock();
        dn_say(*e ? "ENV SYNC: NOTE LENGTHS" : "ENV FREE: MS");
        dn_mark();
    }
    if ((press & (1u << B_FX)) && dn.page == DN_PG_FX) {   /* FX: MIX, FX A (and its next values when it has more
                                                     * than three), FX B (and its next) */
        do
            dn.fx_view = (uint8_t)((dn.fx_view + 1u) % 5u);
        while ((dn.fx_view == 2u || dn.fx_view == 4u) && dn_fx_np(dn_fxp[dn.fx_view == 4u].type) <= 3u);
        dn.redraw |= DN_RD_ALL;
    }
    if ((press & (1u << B_FX)) && dn.page == DN_PG_SET) {   /* (0.30) SETUP: FX, the other skin */
        dn_skin(!dn.dark);
        dn_say(dn.dark ? "SKIN: DARK" : "SKIN: LIGHT");
        dn_mark();
    }
    if ((press & (1u << B_FX)) && dn.page == DN_PG_PROJ) {   /* PROJECT: the instrument chosen goes on the track */
        if (dn.peng != 0xFFu) {
            uint32_t e = dn.peng;
            dn_eng_set(dn.trk, e);
            dn_say(e == DN_E_OFF ? "TRACK OFF" : "CHANGED");
        }
        dn.redraw |= DN_RD_ALL;
    }
    if ((press & (1u << B_FX)) && dn.page == DN_PG_LFO) {   /* LFO: the main knobs, MORE, DRAW (a DRAW shape) */
        dn.lfo_view = (uint8_t)((dn.lfo_view + 1u) % (dn_lp[DN_LSEL].shape == DN_LS_DRAW ? 3u : 2u));
        dn.pt_held = -1;
        dn.redraw |= DN_RD_ALL;
    }
    if ((press & (1u << B_FX)) && dn.page == DN_PG_DRUM && dn.save_held && dn.step_held < 0 && DN_IS_BRK(dn.trk)) {
        dn_track_t *tr = &dn_sq.pat.tr[dn.trk];     /* SAVE + FX on a break's grid: the break straight, each step its
                                                     * own slice (the hit nearest it) */
        const dn_brk_t *bk = &dn_bk[DN_BK(dn.trk)];
        uint32_t i, gs = dn_brk_grid_slices(bk);
        dn_lock();
        for (i = 0; i < tr->len; i++) {
            dn_step_clear(&tr->st[i]);
            tr->st[i].flags = DN_ST_ON;
            tr->st[i].note = (uint8_t)dn_brk_slice_at(bk, i % gs, gs);
            tr->st[i].len = DN_LEN_STEP;
        }
        dn_unlock();
        dn.save_chord = 1;
        dn_mark();
        dn_say("THE BREAK, STRAIGHT");
    } else if ((press & (1u << B_FX)) && DN_IS_SEQPG(dn.page) && dn.step_held >= 0) {
        dn.step_pg = (uint8_t)((dn.step_pg + 1u) % 3u);   /* a step held: its next four values on the knobs */
        dn.redraw |= DN_RD_ALL;
    } else if ((press & (1u << B_FX)) && dn.page == DN_PG_ROLL) {
        dn.roll_keys = (uint8_t)!dn.roll_keys;      /* ROLL: STEP <-> KEYS */
        dn.roll_cur = (uint8_t)(16u * dn.view);
        dn.redraw |= DN_RD_ALL;
    }
    if (press & (1u << B_ARP)) {                    /* BRK: the selected break track's (or the last one shown) */
        if (!DN_IS_BRK(dn.trk) && dn_trk_of(DN_E_BRKA + dn.bk_last % 2u) < 8u)
            dn.trk = (uint8_t)dn_trk_of(DN_E_BRKA + dn.bk_last % 2u);
        else if (!DN_IS_BRK(dn.trk) && dn_trk_of(DN_E_BRKA + (dn.bk_last + 1u) % 2u) < 8u)
            dn.trk = (uint8_t)dn_trk_of(DN_E_BRKA + (dn.bk_last + 1u) % 2u);
        dn_goto(dn.page == DN_PG_BRK ? DN_PG_BRK2 : dn.page == DN_PG_BRK2 ? DN_PG_SLICE : DN_PG_BRK);   /* ARP again:
                                                     * BRK's second page, the slice editor, and back */
        dn.redraw |= DN_RD_ALL;
    }
    if (press & (1u << B_PLAY)) {                   /* the transport */
        dn_lock();
        if (dn.cin_left) {
            dn.cin_left = 0;                        /* PLAY during the count-in: called off */
        } else if (!dn_sq.playing && dn.rec && dn.cin) {
            dn.cin_beat = 0;                        /* REC armed: a count-in first (bars of 4 beats) */
            dn.cin_next = 0;
            dn.clk_last = 0;
            dn.cin_left = 4u * dn.cin * dn_beat_samples();
        } else if (dn_sq.playing) {
            dn_seq_stop(&dn_sq, &DN_SQ_CB);
            for (k = 0; k < DN_NBRK; k++)
                dn_brk_cut(&dn_bk[k]);              /* (a HOLD break would play on to the end of its loop) */
        } else {
            dn.clk_last = 0;
            dn_seq_start(&dn_sq, &DN_SQ_CB);
        }
        dn_unlock();
        dn.redraw |= DN_RD_ALL;
    }
    if ((press & (1u << B_REC)) && DN_IS_SEQPG(dn.page) && dn.save_held && dn.step_held < 0) {
        dn_seq_double();                            /* SAVE + REC: x2 (0.24) */
        dn.save_chord = 1;
    } else if ((press & (1u << B_REC)) && DN_IS_SEQPG(dn.page)) {
        dn.rec_held = 1;                            /* a sequencer page: REC acts as it comes up, unless it was used
                                                     * with the steps (a velocity ramp) */
        dn.rec_chord = 0;
    } else if (press & (1u << B_REC)) {
        dn.rec = (uint8_t)!dn.rec;
        dn.rec_cur = 0;
        dn.redraw |= DN_RD_ALL;
    }
    if (dn.rec_held && !(btn & (1u << B_REC))) {
        dn.rec_held = 0;
        if (!dn.rec_chord) {
            dn.rec = (uint8_t)!dn.rec;
            dn.rec_cur = 0;
            dn.redraw |= DN_RD_ALL;
        }
    }
    {   /* FILL: HOME held on a sequencer page (not ROLL's KEYS, where HOME is a rest); (0.29) from 150 ms held, so a
         * tap can open HOME */
        uint8_t f = DN_IS_SEQPG(dn.page) && !(dn.page == DN_PG_ROLL && dn.roll_keys) && (btn & (1u << B_HOME)) &&
                    !dn.home_chord && (int32_t)(plat_ms() - dn.home_dn) >= DN_HOME_TAP_MS;
        if (f != dn_sq.fill) {
            dn_sq.fill = f;
            dn.redraw |= DN_RD_ALL;
        }
        if (DN_IS_SEQPG(dn.page) && !(dn.page == DN_PG_ROLL && dn.roll_keys))
            press &= ~(1u << B_HOME);
    }
    if ((rel & (1u << B_HOME)) && !dn.home_chord && dn.page == DN_PG_ROLL && dn.roll_keys) {   /* ROLL KEYS: a rest
                                                     * (0.22: when HOME comes up, unless it picked an instrument) */
        dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        dn_lock();
        tr->st[dn.roll_cur % tr->len].flags = 0;
        dn_unlock();
        dn.roll_cur = (uint8_t)((dn.roll_cur + 1u) % tr->len);
        dn.view = (uint8_t)(dn.roll_cur / 16u);
        dn_mark();
    } else if ((rel & (1u << B_HOME)) && !dn.home_chord && dn.page == DN_PG_HOME) {
        dn_trk_open(dn.trk);                        /* (0.29) HOME on HOME: the track picked with SELECT, opened */
    } else if ((rel & (1u << B_HOME)) && !dn.home_chord && dn.page != DN_PG_SLICE &&
               (!DN_IS_SEQPG(dn.page) || (int32_t)(plat_ms() - dn.home_dn) < DN_HOME_TAP_MS)) {
        dn_sq.fill = 0;                             /* (0.29) a tap: HOME, the eight tracks (a hold on a sequencer
                                                     * page was FILL) */
        dn.home_back = dn.page;
        dn_goto(DN_PG_HOME);
        dn.redraw |= DN_RD_ALL;
    }
    if ((d = dn_in_enc(EN_SELECT)) != 0) {
        if (dn.page == DN_PG_HOME) {                /* (0.29) HOME: the track picked (HOME again opens it) */
            dn.trk = (uint8_t)(((int32_t)dn.trk + d) % (int32_t)DN_SEQ_TRACKS + (int32_t)DN_SEQ_TRACKS) % DN_SEQ_TRACKS;
            dn.redraw |= DN_RD_ALL;
        } else if (DN_IS_SEQPG(dn.page) && dn.step_held >= 0) {
            dn_step_move(d);                        /* a step held: it moves in time */
        } else if (dn.page == DN_PG_ROLL) {
            /* (an instrument's page: SELECT moves a held note; the instrument is chosen on its sound pages) */
        } else if (dn.page == DN_PG_MIDI) {
            dn_set(DN_PX_MSEL, (int32_t)dn.midi_sel + (d > 0 ? 1 : -1));
        } else if (DN_IS_BRKPG(dn.page) || (dn.page == DN_PG_DRUM && DN_IS_BRK(dn.trk))) {
            uint32_t o = dn_trk_of(DN_IS_BRK(dn.trk) && DN_BK(dn.trk) ? DN_E_BRKA : DN_E_BRKB);
            if (o < 8u)                             /* BREAKS A <-> B (when both are on tracks) */
                dn.trk = (uint8_t)o;
            else if (!DN_IS_BRK(dn.trk))
                dn.bk_last ^= 1u;
            dn_sl_fix();
            dn.redraw |= DN_RD_ALL;
        } else {                                    /* the sound pages, round */
            int32_t at = 0;
            for (k = 0; k < DN_NSND; k++)
                if (DN_SND[k] == dn.page)
                    at = (int32_t)k;
            do                                      /* (SAMPLER: no LFO page) */
                at = ((at + d) % (int32_t)DN_NSND + (int32_t)DN_NSND) % (int32_t)DN_NSND;
            while (DN_SND[at] == DN_PG_LFO && DN_IS_OWNPG(dn.trk));
            dn_goto(DN_SND[at]);
        }
    }
    if ((d = dn_in_enc(EN_PRESET)) != 0 && dn.page == DN_PG_HOME) {
        /* (0.29) HOME: PRESETS does nothing */
    } else if (d && dn.step_held >= 0 && dn.ramp_b < 0 && DN_CHORDABLE(dn.trk) && DN_IS_SEQPG(dn.page)) {
        dn_chord_turn(d);                           /* (0.28) a step held, FM / SAMPLER / MIDI: its chord */
    } else if (d && DN_IS_SMP(dn.trk) && (DN_IS_SNDPG(dn.page) || DN_IS_SEQPG(dn.page))) {
        if (dn_sm.p.mode == DN_SMP_ONESHOT)         /* SAMPLER: PRESETS picks the pad (the zone) its pages show */
            dn_set(DN_PX_SPAD, (int32_t)dn.smp_pad + (d > 0 ? 1 : -1));
        else
            dn_set(DN_PX_SZONE, (int32_t)dn.smp_zone + (d > 0 ? 1 : -1));
        if (dn.page == DN_PG_DRUM)
            dn.drum_row = dn.smp_pad;
    } else if (d && DN_IS_FM(dn.trk) && (DN_IS_SNDPG(dn.page) || DN_IS_SEQPG(dn.page))) {
        uint32_t np = dn_fm_presets();              /* FM: the factory's voices, then the project's (round) */
        if (dn.save_held) {
            dn.save_chord = 1;
            dn_say("FM: SAVE KEEPS THIS VOICE");
        } else {
            dn_fm_load((uint32_t)((((int32_t)dn.fm_preset + d) % (int32_t)np + (int32_t)np) % (int32_t)np));
        }
    } else if (d && (DN_IS_BRKPG(dn.page) || dn.page == DN_PG_DRUM)) {
        dn_set(DN_PX_BSRC, dn_get(DN_PX_BSRC) + (d > 0 ? 1 : -1));   /* on BRK: the loop */
    } else if (d && dn.save_held) {                 /* SAVE held + PRESETS: the user slot to store this
                                                     * synth's sound in (when SAVE comes up) */
        uint32_t set = DN_IS_SUB(dn.trk) ? 1u : 0u, nf = set ? dn_sub_preset_count() : reese_preset_count();
        uint32_t cur = set ? dn.sb_preset : dn.preset;
        int32_t at = dn.store ? (int32_t)dn.store - 1 : cur >= nf ? (int32_t)(cur - nf) : -1;
        static char t[20];
        at = dn_clamp(at < 0 && d > 0 ? d - 1 : at + d, 0, (int32_t)DN_NUSER - 1);
        dn.store = (uint8_t)(at + 1);
        dn.save_chord = 1;
        t[0] = 0;
        dn_cat(t, "STORE TO USER ");
        t[14] = (char)('1' + at), t[15] = 0;
        dn_say(t);
    } else if (d && DN_IS_SUB(dn.trk)) {             /* SUB's sounds: factory, then USER 1-8 */
        int32_t n = (int32_t)(dn_sub_preset_count() + DN_NUSER);
        dn_sub_preset((uint32_t)((((int32_t)dn.sb_preset + d) % n + n) % n));
    } else if (d) {
        int32_t n = (int32_t)(reese_preset_count() + DN_NUSER);
        dn_load_preset((uint32_t)((((int32_t)dn.preset + d) % n + n) % n));
    }
    if ((d = dn_in_enc(EN_ALGO)) != 0)
        dn_set(DN_PX_BPM, (int32_t)dn_rs.bpm + d);
    for (k = 0; k < 4u; k++)
        if ((d = dn_in_enc(EN_K1 + (int)k)) != 0) {
            if (DN_IS_SEQPG(dn.page)) {
                dn_seq_knob(k, d);
            } else if (dn.page == DN_PG_SLICE) {
                dn_slice_knob(k, d);
            } else if (dn.page == DN_PG_LFO && dn.lfo_view == 2u) {
                dn_lfo_draw_knob(k, d);
            } else {
                uint32_t id = dn_dial(k);
                if (id == DN_PX_NONE)
                    continue;
                if (id == DN_PX_LDEST) {            /* DEST: along this synth's list (dn_ld_list) */
                    uint32_t l[DN_LD_N], n = dn_ld_list(DN_IS_SUB(dn.trk), l), i, at = 0, cur = (uint32_t)dn_get(id);
                    for (i = 0; i < n; i++)
                        if (l[i] == cur)
                            at = i + 1u;
                    at = at ? at - 1u : (d > 0 ? 0u : n);   /* (on the other synth's: from the list's end) */
                    at = (uint32_t)dn_clamp((int32_t)at + d, 0, (int32_t)n - 1);
                    dn_set(id, (int32_t)l[at]);
                    continue;
                }
                dn_set(id, dn_get(id) + (id == DN_PX_BSRC ? (d > 0 ? 1 : -1) : dn_accel(dn_max(id) - dn_min(id), d)));
            }
        }
}

/* ---------------------------------------------------------------------------------------------- the screen --- */
/* (0.30) two skins. LIGHT (the default): a warm white ground, ink text and rules, one orange accent for fills (the
 * engine's tag, the values' bars, the notes); a highlight is ink with white on it. DARK: Massive's look, graphite
 * panels and light-blue values (before 0.30). SETUP, FX: the other one. DREADNOUGHT's red only for its name. */
enum { DN_P_BG, DN_P_BAR, DN_P_PANEL, DN_P_EDGE, DN_P_WELL, DN_P_GRID, DN_P_LABEL, DN_P_DIMTXT, DN_P_BLUE, DN_P_BLUE_D,
       DN_P_TRACK, DN_P_KNOB, DN_P_KNOB_HI, DN_P_RED, DN_P_FG, DN_P_ACC, DN_P_ONACC, DN_P_KEYB, DN_P_KEYW, DN_P_N };
static uint16_t dn_pal[DN_P_N];
#define DN_C_BG dn_pal[DN_P_BG]                     /* the ground */
#define DN_C_BAR dn_pal[DN_P_BAR]                   /* the title and status bars; text on a highlight */
#define DN_C_PANEL dn_pal[DN_P_PANEL]
#define DN_C_EDGE dn_pal[DN_P_EDGE]                 /* rules */
#define DN_C_WELL dn_pal[DN_P_WELL]
#define DN_C_GRID dn_pal[DN_P_GRID]
#define DN_C_LABEL dn_pal[DN_P_LABEL]               /* names, labels */
#define DN_C_DIMTXT dn_pal[DN_P_DIMTXT]
#define DN_C_BLUE dn_pal[DN_P_BLUE]                 /* values, the highlight (LIGHT: ink) */
#define DN_C_BLUE_D dn_pal[DN_P_BLUE_D]
#define DN_C_TRACK dn_pal[DN_P_TRACK]
#define DN_C_KNOB dn_pal[DN_P_KNOB]
#define DN_C_KNOB_HI dn_pal[DN_P_KNOB_HI]
#define DN_C_RED dn_pal[DN_P_RED]
#define DN_C_FG dn_pal[DN_P_FG]                     /* the main text (DARK: white) */
#define DN_C_ACC dn_pal[DN_P_ACC]                   /* the accent's fills */
#define DN_C_ONACC dn_pal[DN_P_ONACC]               /* text on the accent */
#define DN_C_KEYB dn_pal[DN_P_KEYB]                 /* the roll's keyboard: black keys, white keys */
#define DN_C_KEYW dn_pal[DN_P_KEYW]
static void dn_skin(int dark)
{
    static const uint16_t L[DN_P_N] = {
        RGB(244, 243, 238), RGB(255, 255, 255), RGB(255, 255, 255), RGB(16, 17, 20), RGB(239, 238, 232),
        RGB(228, 226, 218), RGB(74, 76, 85), RGB(107, 110, 118), RGB(16, 17, 20), RGB(255, 90, 31),
        RGB(255, 255, 255), RGB(214, 212, 204), RGB(236, 235, 230), RGB(200, 38, 26), RGB(16, 17, 20),
        RGB(255, 90, 31), RGB(16, 17, 20), RGB(42, 44, 51), RGB(255, 255, 255)};
    static const uint16_t D[DN_P_N] = {
        RGB(44, 46, 50), RGB(24, 25, 28), RGB(62, 65, 70), RGB(88, 92, 98), RGB(18, 19, 21),
        RGB(36, 39, 44), RGB(176, 180, 186), RGB(120, 124, 130), RGB(98, 200, 255), RGB(40, 92, 124),
        RGB(30, 31, 34), RGB(92, 96, 102), RGB(130, 134, 140), RGB(232, 58, 46), C_WHITE,
        RGB(98, 200, 255), RGB(24, 25, 28), RGB(24, 25, 28), RGB(92, 96, 102)};
    uint32_t i;
    for (i = 0; i < DN_P_N; i++)
        dn_pal[i] = dark ? D[i] : L[i];
    dn.dark = (uint8_t)(dark != 0);
    dn.redraw |= DN_RD_ALL;
}

/* a point on a circle: angle a in 1/1024 of a turn, 0 = up, clockwise */
static void dn_polar(int32_t cx, int32_t cy, int32_t r, int32_t a, int32_t *x, int32_t *y)
{
    uint32_t ph = (uint32_t)a << 22;
    *x = cx + ((dn_sine(ph) * r + 16384) >> 15);
    *y = cy - ((dn_sine(ph + 0x40000000u) * r + 16384) >> 15);
}

static void dn_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}

/* the arc from angle a0 to a1 (1/1024 turn), radii r0..r1 */
static void dn_arc(int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t a0, int32_t a1, uint16_t c)
{
    int32_t a, r, x, y;
    if (a1 < a0) { a = a0; a0 = a1; a1 = a; }
    for (a = a0; a <= a1; a += 3)
        for (r = r0; r <= r1; r++) {
            dn_polar(cx, cy, r, a, &x, &y);
            cv_pset(x, y, c);
        }
}

#define DN_KA 384                                   /* the dial's sweep: +-135 degrees */
static void dn_knob(int32_t cx, int32_t cy, uint32_t id, int focus)
{
    int32_t lo = dn_min(id), hi = dn_max(id), v = dn_get(id), span = hi - lo > 0 ? hi - lo : 1, pos = v;
    int32_t av, a0 = lo < 0 ? 0 : -DN_KA, px, py, qx, qy;
    char t[8];
    if (id == DN_PX_LDEST) {                        /* DEST: the pointer at its place in this synth's list (the order
                                                     * the knob goes through), not at its number */
        uint32_t l[DN_LD_N], n = dn_ld_list(DN_IS_SUB(dn.trk), l), i;
        pos = 0;
        for (i = 0; i < n; i++)
            if (l[i] == (uint32_t)v)
                pos = (int32_t)i;
        lo = 0, span = n > 1u ? (int32_t)n - 1 : 1;
    }
    if (!dn.dark) {                                 /* (0.30) LIGHT: the name, the value big, a bar under it */
        int32_t x0 = cx - 26, w = 52, f, r = 0;
        const felucca_font_t *fn = &FONT_L;
        uint32_t i;
        cv_text(x0, cy - 44, &FONT_T, dn_name(id), DN_C_LABEL);
        dn_text(id, v, t);
        for (i = 0; t[i]; i++)                      /* (the big digits have no letters: those go a size down) */
            if ((uint8_t)t[i] > 57u)
                fn = &FONT_M;
        if (text_w(fn, t) > w)
            fn = text_w(&FONT_M, t) <= w ? &FONT_M : &FONT_S;
        cv_text(x0, cy - (fn == &FONT_L ? 22 : fn == &FONT_M ? 16 : 10), fn, t, DN_C_FG);
        cv_rect(x0, cy + 30, w, 5, DN_C_KNOB);
        if (lo < 0 && -2 * lo >= hi) {             /* (either way of 0: from a tick at the middle) */
            int32_t m = x0 + (int32_t)(-lo * w / span), e = x0 + (int32_t)((pos - lo) * w / span);
            cv_rect(e < m ? e : m, cy + 30, e < m ? m - e : e - m, 5, DN_C_ACC);
            cv_rect(m - 1, cy + 27, 2, 11, DN_C_FG);
        } else {
            f = (int32_t)((pos - lo) * w / span);
            cv_rect(x0, cy + 30, f, 5, DN_C_ACC);
        }
        if (id < RP_COUNT || DN_IS_SPX(id)) {       /* an LFO on it: its reach, a red line under the bar */
            uint32_t j;
            for (j = 0; j < DN_NLFO_ALL; j++) {
                uint32_t d = dn_lp[j].dest;
                int32_t dep = dn_lp[j].depth < 0 ? -dn_lp[j].depth : dn_lp[j].depth;
                if (id < RP_COUNT && d && d < DN_LD_APITCH && d != DN_LD_PITCH && DN_LD_RP[d] == id)
                    r += dep * (d == DN_LD_CUT ? 2 : 1);
                if (DN_IS_SPX(id) && d >= DN_LD_SCUT && d < DN_LD_N && d != DN_LD_SPITCH &&
                    DN_PX_SUB0 + DN_LD_SP[d - DN_LD_SCUT] == id)
                    r += dep * (d == DN_LD_SCUT ? 2 : 1);
            }
            if (r) {
                int32_t m0 = dn_clamp(v - r, lo, hi), m1 = dn_clamp(v + r, lo, hi);
                cv_rect(x0 + (int32_t)((m0 - lo) * w / span), cy + 37, (int32_t)((m1 - m0) * w / span) + 1, 2, DN_C_RED);
            }
        }
        (void)focus, (void)a0, (void)px, (void)py, (void)qx, (void)qy, (void)av;
        return;
    }
    av = -DN_KA + (2 * DN_KA * (pos - lo)) / span;
    dn_arc(cx, cy, 19, 22, -DN_KA, DN_KA, DN_C_TRACK);
    dn_arc(cx, cy, 19, 22, a0, av, focus ? DN_C_BLUE : DN_C_BLUE_D);
    if (id < RP_COUNT || DN_IS_SPX(id)) {           /* an LFO on it: its reach, red outside the ring (as Massive) */
        uint32_t j;
        int32_t r = 0;
        for (j = 0; j < DN_NLFO_ALL; j++) {
            uint32_t d = dn_lp[j].dest;
            int32_t dep = dn_lp[j].depth < 0 ? -dn_lp[j].depth : dn_lp[j].depth;
            if (id < RP_COUNT && d && d < DN_LD_APITCH && d != DN_LD_PITCH && DN_LD_RP[d] == id)
                r += dep * (d == DN_LD_CUT ? 2 : 1);
            if (DN_IS_SPX(id) && d >= DN_LD_SCUT && d < DN_LD_N && d != DN_LD_SPITCH &&
                DN_PX_SUB0 + DN_LD_SP[d - DN_LD_SCUT] == id)
                r += dep * (d == DN_LD_SCUT ? 2 : 1);
        }
        if (r) {
            int32_t m0 = dn_clamp(v - r, lo, hi), m1 = dn_clamp(v + r, lo, hi);
            dn_arc(cx, cy, 23, 25, -DN_KA + (2 * DN_KA * (m0 - lo)) / span, -DN_KA + (2 * DN_KA * (m1 - lo)) / span,
                   DN_C_RED);
        }
    }
    dn_disc(cx, cy, 15, DN_C_KNOB);
    dn_disc(cx - 2, cy - 3, 9, DN_C_KNOB_HI);
    dn_disc(cx, cy, 8, DN_C_KNOB);
    dn_polar(cx, cy, 5, av, &px, &py);
    dn_polar(cx, cy, 14, av, &qx, &qy);
    cv_line(px, py, qx, qy, DN_C_FG);
    cv_line(px + 1, py, qx + 1, qy, DN_C_FG);
    cv_text(cx - text_w(&FONT_T, dn_name(id)) / 2, cy - 42, &FONT_T, dn_name(id), DN_C_LABEL);
    dn_text(id, v, t);
    cv_text(cx - text_w(&FONT_XS, t) / 2, cy + 27, &FONT_XS, t, DN_C_BLUE);
}

static uint32_t str_len_dn(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}
static void dn_num(char *o, int32_t v) { reese_param_text(RP_LEVEL, v, o); }   /* (a plain number) */
static void dn_cat(char *o, const char *s)
{
    uint32_t n = str_len_dn(o), i;
    for (i = 0; s[i]; i++)
        o[n + i] = s[i];
    o[n + i] = 0;
}
/* a MIDI note as a name: C4 = 60 */
static void dn_note_name(char *o, int32_t n)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    n = dn_clamp(n, 0, 127);
    o[0] = 0;
    dn_cat(o, NM[n % 12]);
    if (n < 12)
        dn_cat(o, "-1");
    else {
        char d[8];
        dn_num(d, n / 12 - 1);
        dn_cat(o, d);
    }
}

static void dn_draw_overview(void);

/* SLICE: the selected slice's start zoomed in (twice the slice's length), every slice point in view, the selected one
 * white with its number */
/* SLICE (chop mode): the loop as wide as the zoom shows, every chop numbered (the selected one white), the cursor
 * red, the place playing blue when the loop runs; the view only moves to keep the cursor in sight */
static void dn_draw_slice_zoom(const dn_brk_t *b)
{
    static uint8_t pk[232];
    uint32_t i, w, from;
    char t[8];
    cv_rect(4, 50, 232, 42, DN_C_WELL);
    if (!b->ok)
        return;
    w = dn_sl_width(b);
    from = dn.sl_view;
    dn_brk_range_peaks(b, from, w, pk, 232u);
    cv_rect(4, 71, 232, 1, DN_C_GRID);
    for (i = 0; i < 232u; i++) {
        int32_t h = pk[i] * 20 / 255;
        cv_rect(4 + (int32_t)i, 71 - h, 1, 2 * h + 1, DN_C_BLUE);
    }
    for (i = 0; i < b->nslices; i++)
        if (b->start[i] >= from && b->start[i] < from + w) {
            int32_t x = 4 + (int32_t)((b->start[i] - from) * 232u / w);
            int sel = i == dn.sl_sel;
            cv_rect(x, 50, sel ? 2 : 1, 42, sel ? DN_C_FG : DN_C_LABEL);
            dn_num(t, (int32_t)i + 1);
            cv_text(x + 3, 51, &FONT_T, t, sel ? DN_C_FG : DN_C_DIMTXT);
        }
    if (dn.sl_cur >= from && dn.sl_cur < from + w)
        cv_rect(4 + (int32_t)((dn.sl_cur - from) * 232u / w), 50, 1, 42, DN_C_RED);
    if (b->aud) {
        uint32_t at = dn_brk_pos(b);
        if (at >= from && at < from + w)
            cv_rect(4 + (int32_t)((at - from) * 232u / w), 50, 1, 42, DN_C_BLUE_D);
    }
}

/* BRK: the loop's picture, its slices (bars brighter), the slice playing lit and where it has got to */
static void dn_draw_loop(const dn_brk_t *b, int32_t sel)
{
    uint32_t i, ns = b->nslices ? b->nslices : 16u, n = b->loop.n ? b->loop.n : 1u, per = n / 232u + 1u;
    cv_rect(4, 50, 232, 42, DN_C_WELL);
    if (!b->ok) {
        cv_text(120 - text_w(&FONT_T, "NO LOOP") / 2, 64, &FONT_T, "NO LOOP", DN_C_DIMTXT);
        return;
    }
    if (b->on)                                      /* the slice playing, and the one selected (DRUM / SLICE) */
        cv_rect(4 + (int32_t)(b->start[b->slice] / per), 50,
                (int32_t)((b->start[b->slice + 1u] - b->start[b->slice]) / per) + 1, 42, DN_C_GRID);
    if (sel >= 0 && (uint32_t)sel < ns)
        cv_rect(4 + (int32_t)(b->start[sel] / per), 50, (int32_t)((b->start[sel + 1] - b->start[sel]) / per) + 1, 42,
                DN_C_BLUE_D);
    for (i = 0; i < DN_BRK_PEAKS; i++) {
        int32_t h = b->peaks[i] * 20 / 255;
        cv_rect(4 + (int32_t)i, 71 - h, 1, 2 * h + 1, DN_C_BLUE);
    }
    for (i = 1; i < ns; i++)                        /* the slice points (bars of the grid brighter) */
        cv_rect(4 + (int32_t)(b->start[i] / per), 50, 1, 42,
                b->smode ? DN_C_LABEL : (i % 16u) ? (i % 4u ? DN_C_GRID : DN_C_TRACK) : DN_C_BLUE_D);
    if (b->on) {
        uint32_t at = b->start[b->slice] + (b->rev ? 0u : b->played >> 8);
        if (at < n)
            cv_rect(4 + (int32_t)(at / per), 50, 1, 42, DN_C_FG);
    }
}

/* a length in quarter steps as steps: "1/4", "1/2", "3/4", "1", "2 1/2" */
static void dn_len_text(char *o, uint32_t ticks)
{
    static const char *const FR[4] = {"", "1/4", "1/2", "3/4"};
    uint32_t whole = ticks / DN_LEN_STEP, fr = ticks % DN_LEN_STEP;
    o[0] = 0;
    if (whole) {
        dn_num(o, (int32_t)whole);
        if (fr)
            dn_cat(o, " ");
    }
    dn_cat(o, FR[fr]);
}

/* the pattern's length for the progress bar: the longest track that plays (16 steps if none) */
static uint32_t dn_pat_len(void)
{
    uint32_t t, i, L = 0;
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        const dn_track_t *tr = &dn_sq.pat.tr[t];
        if (tr->mute || tr->len <= L)
            continue;
        for (i = 0; i < tr->len; i++)
            if (tr->st[i].flags & DN_ST_ON) {
                L = tr->len;
                break;
            }
    }
    return L ? L : 16u;
}

/* the top bar's words: the engine's tag (the return) and the sound's name (*pn): the screen's, and the editor's
 * panel's (0.26) */
static const char *dn_top_names(const char **pn)
{
    const char *tag = "REESE";
    int brk = DN_IS_BRKPG(dn.page) || (dn.page == DN_PG_DRUM && !DN_IS_SMP(dn.trk));
    const dn_brk_t *bk = &dn_bk[dn_bsel()];
    *pn = dn_sound_name(0, dn.preset);
    if (dn.page == DN_PG_HOME) {                    /* (0.29) HOME: the project */
        *pn = dn_pname;
        return "HOME";
    }
    if (brk) {                                      /* BRK: which BREAKS, and its loop */
        tag = dn_bsel() ? "BRK B" : "BRK A";
        *pn = bk->loop.name;
    } else if ((dn.page == DN_PG_ROLL && DN_IS_MIDI(dn.trk)) || dn.page == DN_PG_MIDI) {   /* a MIDI instrument */
        static char ch[12], mt[8] = "MIDI ";
        uint32_t c = dn_sq.pat.tr[dn.trk % DN_SEQ_TRACKS].chan + 1u;
        mt[5] = (char)('1' + (DN_IS_MIDI(dn.trk) ? dn_midi_idx(dn.trk) % 9u : 0u));
        mt[6] = 0;
        tag = mt;
        ch[0] = 0;
        dn_cat(ch, "CHANNEL ");
        if (c >= 10u)
            dn_cat(ch, "1"), c -= 10u;
        ch[str_len_dn(ch) + 1u] = 0;
        ch[str_len_dn(ch)] = (char)('0' + c);
        *pn = ch;
    } else if (dn.page == DN_PG_PROJ || dn.page == DN_PG_FX) {   /* the project's name; FX: the rack */
        static char fx[32];
        tag = dn.page == DN_PG_PROJ ? "PROJECT" : "FX";
        *pn = dn_pname;
        if (dn.page == DN_PG_FX) {
            fx[0] = 0;
            dn_cat(fx, DN_FX_NAME[dn_fxp[0].type % DN_FX_N]);
            dn_cat(fx, " / ");
            dn_cat(fx, DN_FX_NAME[dn_fxp[1].type % DN_FX_N]);
            *pn = fx;
        }
    } else if (DN_IS_FM(dn.trk) && (DN_IS_SNDPG(dn.page) || DN_IS_SEQPG(dn.page))) {   /* FM: its voice */
        tag = "FM";
        *pn = dn_fm_vcname();
    } else if (DN_IS_SMP(dn.trk) && (DN_IS_SNDPG(dn.page) || DN_IS_SEQPG(dn.page))) {   /* SAMPLER: the pad / zone */
        static char sp[24];
        char v[8];
        int one = dn_sm.p.mode == DN_SMP_ONESHOT;
        int32_t smp = one ? dn_sm.p.pad[dn.smp_pad % DN_SMP_PADS][PD_SAMPLE]
                          : dn_sm.p.zone[dn.smp_zone % DN_SMP_ZONES][ZN_SAMPLE];
        tag = "SMPL";
        sp[0] = 0;
        dn_cat(sp, one ? "PAD " : "ZONE ");
        dn_num(v, (one ? dn.smp_pad : dn.smp_zone) + 1);
        dn_cat(sp, v);
        dn_cat(sp, " ");
        dn_sm_text(one ? DN_PX_PD0 + PD_SAMPLE : DN_PX_ZN0 + ZN_SAMPLE, smp, v);
        dn_cat(sp, v);
        *pn = sp;
    } else if (DN_IS_SUB(dn.trk) && (DN_IS_SNDPG(dn.page) || dn.page == DN_PG_ROLL)) {   /* SUB: its sound */
        tag = "SUB";
        *pn = dn_sound_name(1, dn.sb_preset);
    }
    return tag;
}
static void dn_draw_top(void)
{
    char t[16];
    int32_t x, i, w = (int32_t)dn_scope_w, start, y, py = 0;
    const char *pn = dn_sound_name(0, dn.preset);
    int brk = DN_IS_BRKPG(dn.page) || (dn.page == DN_PG_DRUM && !DN_IS_SMP(dn.trk));
    const dn_brk_t *bk = &dn_bk[dn_bsel()];
    cv_begin(240, 96, DN_C_BG);
    /* the title bar */
    cv_rect(0, 0, 240, 22, DN_C_BAR);
    cv_text(8, 3, &FONT_B, "DREADNOUGHT", DN_C_RED);
    reese_param_text(RP_LEVEL, dn_rs.bpm, t);
    x = cv_text(232 - text_w(&FONT_XS, t) - text_w(&FONT_T, " BPM"), 4, &FONT_XS, t, DN_C_LABEL);
    cv_text(x, 6, &FONT_T, " BPM", DN_C_DIMTXT);
    /* the sound: the engine's tag, the preset's name, changed or not */
    cv_rect(0, 22, 240, 24, DN_C_PANEL);
    cv_rect(0, 45, 240, 1, DN_C_EDGE);
    cv_rect(6, 27, 46, 14, DN_C_ACC);
    {
        const char *tag = dn_top_names(&pn);
        cv_text(29 - text_w(&FONT_T, tag) / 2, 28, &FONT_T, tag, DN_C_ONACC);
    }
    x = cv_text(60, 26, &FONT_S, pn, DN_C_FG);
    if (dn.dirty)
        cv_text(x + 2, 26, &FONT_S, "*", DN_C_BLUE);
    if (dn_sq.playing || dn.rec) {                  /* the transport: bar . beat, and REC */
        uint32_t st = dn_sq.pos ? dn_sq.pos - 1u : 0u;
        char pos[12];
        int32_t px;
        dn_num(pos, (int32_t)(st / 16u % 100u + 1u));
        px = (int32_t)str_len_dn(pos);
        pos[px++] = '.';
        pos[px++] = (char)('1' + st / 4u % 4u);
        pos[px] = 0;
        px = 234 - text_w(&FONT_XS, pos);
        if (dn_sq.playing) {
            cv_text(px, 27, &FONT_XS, pos, DN_C_FG);
            for (i = 0; i < 9; i++)                 /* a play triangle */
                cv_rect(px - 14 + i / 2, 29 + i / 2, 1, 10 - i / 2 * 2, DN_C_BLUE);
        }
        if (dn.rec)
            dn_disc(dn_sq.playing ? px - 24 : 228, 34, 4, DN_C_RED);
    } else {
        cv_text(234 - text_w(&FONT_T, "PRESETS"), 29, &FONT_T, "PRESETS", DN_C_DIMTXT);
    }
    if (dn.page == DN_PG_SLICE) {
        dn_draw_slice_zoom(bk);
        cv_blit(0, 0);
        return;
    }
    if (brk) {
        dn_draw_loop(bk, dn.page == DN_PG_DRUM ? (int32_t)dn.drum_row : -1);
        cv_blit(0, 0);
        return;
    }
    if (dn.page == DN_PG_ROLL) {
        dn_draw_overview();
        cv_blit(0, 0);
        return;
    }
    /* the progress bar: where the pattern is, to play against (the longest track playing; bars, beats; the beat
     * flashes, the bar's first brighter); the count-in counts down on it */
    {
        uint32_t L = dn_pat_len(), bars = (L + 15u) / 16u, st = dn_sq.pos ? (dn_sq.pos - 1u) % L : 0u, b, bx, cin;
        int32_t bw = 196 / (int32_t)(bars ? bars : 1u), x0 = 4, flash = 0;
        char pos[12];
        cin = dn.cin_left ? dn.cin_beat : 0u;
        cv_rect(4, 48, 232, 13, DN_C_WELL);
        for (b = 0; b < bars; b++) {
            bx = (uint32_t)(x0 + (int32_t)b * bw);
            cv_rect((int32_t)bx + 1, 50, bw - 2, 9, DN_C_GRID);
            for (i = 1; i < 4; i++)                 /* the beats */
                cv_rect((int32_t)bx + i * bw / 4, 50, 1, 9, DN_C_BG);
        }
        if (dn_sq.playing) {
            int32_t px = x0 + (int32_t)((st * (uint32_t)bw * bars) / (bars * 16u));
            cv_rect(x0 + 1, 52, px - x0, 5, DN_C_BLUE_D);
            cv_rect(px, 50, 2, 9, DN_C_FG);
            flash = st % 4u == 0u ? (st % 16u == 0u ? 2 : 1) : 0;
            dn_num(pos, (int32_t)(st / 16u + 1u));
            i = (int32_t)str_len_dn(pos);
            pos[i++] = '.';
            pos[i++] = (char)('1' + st / 4u % 4u);
            pos[i] = 0;
        } else if (dn.cin_left) {                   /* counting in: 1 2 3 4 (the beats so far, the bar's red) */
            flash = cin % 4u == 1u ? 2 : 1;
            dn_num(pos, (int32_t)((cin ? cin - 1u : 0u) % 4u + 1u));
            for (b = 0; b < cin && b < 4u * dn.cin; b++)
                cv_rect(x0 + 2 + (int32_t)(b % 4u) * bw / 4, 52, bw / 4 - 4, 5, DN_C_RED);
        } else {                                    /* stopped: how long, where PLAY starts */
            dn_num(pos, (int32_t)bars);
            dn_cat(pos, bars == 1u ? " BAR" : " BARS");
        }
        if (flash)
            cv_rect(202, 47, 34, 15, flash == 2 ? DN_C_FG : DN_C_BLUE);
        if (dn_sq.playing || dn.cin_left)
            cv_text(219 - text_w(&FONT_S, pos) / 2, 47, &FONT_S, pos, flash ? DN_C_BAR : DN_C_FG);
        else
            cv_text(219 - text_w(&FONT_T, pos) / 2, 50, &FONT_T, pos, DN_C_LABEL);
    }
    if (dn.page == DN_PG_PROJ) {                    /* PROJECT: the eight tracks and their instruments (the selected
                                                     * one lit; one chosen, not in yet: red) */
        uint32_t t;
        cv_rect(4, 63, 232, 30, DN_C_BG);
        for (t = 0; t < DN_SEQ_TRACKS; t++) {
            int32_t cx = 4 + (int32_t)(t % 4u) * 58, cy = 63 + (int32_t)(t / 4u) * 15;
            int sel = t == dn.trk;
            uint32_t e = sel && dn.peng != 0xFFu ? dn.peng : dn_eng[t];
            char c[12];
            cv_rect(cx, cy, 56, 14, sel ? DN_C_BLUE : DN_C_WELL);
            c[0] = (char)('1' + t), c[1] = ' ', c[2] = 0;
            dn_cat(c, DN_IS_MIDI(t) && !(sel && dn.peng != 0xFFu) ? dn_inst_name(t) : DN_E_NAME[e % DN_E_N]);
            cv_text(cx + 3, cy + 1, &FONT_T, c, sel && dn.peng != 0xFFu ? DN_C_RED : sel ? DN_C_BAR
                                               : e == DN_E_OFF ? DN_C_DIMTXT : DN_C_LABEL);
        }
        cv_blit(0, 0);
        return;
    }
    if (dn.page == DN_PG_LFO) {                     /* the LFO: its shape over a cycle, where it is, its value now */
        const dn_lfo_p_t *l = &dn_lp[DN_LSEL];
        const dn_lfo_s_t *ls = &dn_ls[DN_LSEL];
        int32_t vy, mx;
        cv_rect(4, 63, 232, 29, DN_C_WELL);
        cv_rect(4, 77, 200, 1, DN_C_GRID);
        for (x = 0; x < 200; x++) {
            y = 77 - dn_lfo_at(l, (uint32_t)x * 21474836u + ((uint32_t)l->phase << 25), ls->sh_a, ls->sh_b) * 13 / 32767;
            if (x)
                cv_line(4 + x, py, 5 + x, y, DN_C_BLUE);
            py = y;
        }
        mx = 4 + (int32_t)(ls->ph >> 16) * 200 / 65536;   /* where it is */
        cv_rect(mx, 64, 1, 27, DN_C_FG);
        vy = ls->out * 13 / 32767;                  /* its value: a meter at the right */
        cv_rect(212, 64, 18, 27, DN_C_GRID);
        if (vy >= 0)
            cv_rect(214, 77 - vy, 14, vy + 1, l->dest ? DN_C_BLUE : DN_C_DIMTXT);
        else
            cv_rect(214, 77, 14, -vy + 1, l->dest ? DN_C_BLUE : DN_C_DIMTXT);
        cv_blit(0, 0);
        return;
    }
    /* the scope: the last ~42 ms, from a rising zero crossing so it stands still */
    cv_rect(4, 63, 232, 29, DN_C_WELL);
    for (i = 1; i < 4; i++)
        cv_rect(4 + i * 58, 63, 1, 29, DN_C_GRID);
    cv_rect(4, 77, 232, 1, DN_C_GRID);
    start = w - 230;
    for (i = 0; i < 200; i++) {
        int32_t a = dn_scope[(uint32_t)(start - i - 1) & (DN_SCOPE - 1u)], b = dn_scope[(uint32_t)(start - i) & (DN_SCOPE - 1u)];
        if (a < 0 && b >= 0) {
            start -= i;
            break;
        }
    }
    for (x = 0; x < 230; x++) {
        int32_t s = dn_scope[(uint32_t)(start + x) & (DN_SCOPE - 1u)];
        y = 77 - (s * 13) / 14000;
        y = dn_clamp(y, 64, 91);
        if (x)
            cv_line(4 + x, py, 5 + x, y, DN_C_BLUE);
        py = y;
    }
    cv_blit(0, 0);
}

/* the SEQ page: eight track rows of the 16 steps in view, the playhead, the selected track; the status line says
 * what the knobs do (or the held step's values) */
static void dn_draw_seq(void)
{
    uint32_t t, i;
    char line[48], v[8];
    uint8_t cov[DN_SEQ_STEPS];
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    cv_rect(0, 17, 240, 1, DN_C_EDGE);
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        const dn_track_t *tr = &dn_sq.pat.tr[t];
        int32_t y = 21 + 12 * (int32_t)t, sel = t == dn.trk;
        line[0] = (char)('1' + t);                  /* the track: its number, R (REESE) or M (MIDI); red: muted */
        line[1] = ' ';
        line[2] = DN_IS_BRK(t) ? 'B' : DN_IS_MIDI(t) ? 'M' : DN_IS_RS(t) ? 'R' : '-';
        line[3] = 0;
        dn_pattern_coverage(&dn_sq.pat, t, cov);
        cv_rect(2, y, 25, 11, sel ? DN_C_BLUE : DN_C_BG);
        cv_text(5, y - 1, &FONT_T, line, tr->mute ? DN_C_RED : sel ? DN_C_BAR : DN_C_LABEL);
        for (i = 0; i < 16u; i++) {
            uint32_t si = i + 16u * dn.view;
            int32_t x = 30 + 13 * (int32_t)i;
            const dn_step_t *st = &tr->st[si];
            uint16_t c;
            if (si >= tr->len)
                continue;                           /* past the track's end: nothing */
            c = !(st->flags & DN_ST_ON) ? (i % 4u ? DN_C_TRACK : DN_C_WELL)
                : (st->flags & DN_ST_ACC) ? DN_C_BLUE : DN_C_BLUE_D;
            if ((st->flags & DN_ST_ON) && tr->mute)
                c = DN_C_KNOB;
            cv_rect(x, y, 11, 10, cov[si] == 1 ? (i % 4u ? DN_C_TRACK : DN_C_WELL) : c);
            if (cov[si] == 1)                       /* a note: as wide as it lasts (a short one, part of the cell) */
                cv_rect(x, y, st->len >= DN_LEN_STEP ? 11 : (int32_t)(11u * st->len / DN_LEN_STEP), 10, c);
            if (cov[si] == 2 || (cov[si] && si + 1u < tr->len && cov[si + 1u] == 2 && i < 15u)) {
                uint16_t hc = tr->mute ? DN_C_KNOB : DN_C_BLUE_D;   /* a long note: one bar through the steps it holds */
                if (cov[si] == 2)
                    cv_rect(x - 2, y + 2, 13, 6, hc);
                else
                    cv_rect(x + 11, y + 2, 2, 6, hc);
            }
            if ((st->flags & (DN_ST_ON | DN_ST_SLIDE)) == (DN_ST_ON | DN_ST_SLIDE))
                cv_rect(x - 2, y + 4, 3, 2, DN_C_BLUE);   /* a slide: joined to the step before */
            if ((int32_t)si == dn.step_held && sel)
                cv_rect(x + 3, y + 3, 5, 4, DN_C_FG);
            if (dn_sq.playing && dn_sq.cur[t] == si) {     /* the playhead */
                cv_rect(x, y, 11, 1, DN_C_FG);
                cv_rect(x, y + 9, 11, 1, DN_C_FG);
                cv_rect(x, y, 1, 10, DN_C_FG);
                cv_rect(x + 10, y, 1, 10, DN_C_FG);
            }
        }
    }
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    line[0] = 0;
    if (dn.step_held >= 0) {                        /* the held step */
        const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        const dn_step_t *st = &tr->st[dn.step_held];
        dn_cat(line, "STEP ");
        dn_num(v, dn.step_held + 1);
        dn_cat(line, v);
        dn_cat(line, "  ");
        if (DN_IS_BRK(dn.trk)) {                     /* a break step: its slice */
            dn_cat(line, "SL ");
            dn_num(v, st->note + 1);
        } else {
            dn_note_name(v, st->note + (DN_IS_RS(dn.trk) ? dn_rs.p[RP_TRANS] : 0));
        }
        dn_cat(line, v);
        dn_cat(line, "  LEN ");
        dn_len_text(v, st->len);
        dn_cat(line, v);
        if (st->flags & DN_ST_ACC)
            dn_cat(line, "  ACC");
        if (st->flags & DN_ST_SLIDE)
            dn_cat(line, DN_IS_BRK(dn.trk) ? "  REV" : "  SLIDE");
        cv_text(8, 125, &FONT_XS, line, DN_C_BLUE);
    } else if (dn.clear_t0) {
        cv_text(8, 125, &FONT_XS, "HOLD TO CLEAR THE TRACK", DN_C_RED);
    } else {
        const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        dn_cat(line, DN_IS_MIDI(dn.trk) ? "MIDI " : DN_E_NAME[dn_eng[dn.trk] % DN_E_N]);
        if (DN_IS_MIDI(dn.trk)) {
            dn_num(v, tr->chan + 1);
            dn_cat(line, v);
        }
        dn_cat(line, "  LEN ");
        dn_num(v, tr->len);
        dn_cat(line, v);
        dn_cat(line, "  SWG ");
        dn_num(v, dn_sq.pat.swing);
        dn_cat(line, v);
        dn_cat(line, "  ");
        dn_num(v, 16 * dn.view + 1);
        dn_cat(line, v);
        dn_cat(line, "-");
        dn_num(v, 16 * dn.view + 16);
        dn_cat(line, v);
        cv_text(8, 125, &FONT_XS, line, DN_C_LABEL);
    }
}

/* where a note on step i ends, in steps (exclusive): its length, cut by the next note on the track */
static uint32_t dn_note_end(const dn_track_t *tr, uint32_t i)
{
    uint32_t span = (tr->st[i].len + DN_LEN_STEP - 1u) / DN_LEN_STEP, j;
    for (j = 1; j < span; j++)
        if (tr->st[(i + j) % tr->len].flags & DN_ST_ON)
            return i + j;
    return i + span;
}

/* ROLL's top: the whole track at a glance, pitch up, the 16 steps in view boxed */
static void dn_draw_overview(void)
{
    const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i, L = tr->len, lo = 127, hi = 0;
    cv_rect(4, 50, 232, 42, DN_C_WELL);
    for (i = 0; i < L; i++)
        if (tr->st[i].flags & DN_ST_ON) {
            lo = tr->st[i].note < lo ? tr->st[i].note : lo;
            hi = tr->st[i].note > hi ? tr->st[i].note : hi;
        }
    {
        int32_t x0 = 4 + (int32_t)(16u * dn.view * 232u / L), x1 = 4 + (int32_t)((16u * dn.view + 16u) * 232u / L);
        x1 = x1 > 236 ? 236 : x1;
        cv_rect(x0, 50, x1 - x0, 1, DN_C_LABEL);
        cv_rect(x0, 91, x1 - x0, 1, DN_C_LABEL);
        cv_rect(x0, 50, 1, 42, DN_C_LABEL);
        cv_rect(x1 - 1, 50, 1, 42, DN_C_LABEL);
    }
    if (lo > hi)
        return;
    for (i = 0; i < L; i++)
        if (tr->st[i].flags & DN_ST_ON) {
            int32_t x = 4 + (int32_t)(i * 232u / L), w = (int32_t)((dn_note_end(tr, i) - i) * 232u / L);
            int32_t y = 86 - (int32_t)((tr->st[i].note - lo) * 32u / (hi - lo + 1u));
            if (x + w > 236)
                w = 236 - x;
            cv_rect(x, y, w > 1 ? w - 1 : 1, 3, (tr->st[i].flags & DN_ST_ACC) ? DN_C_BLUE : DN_C_BLUE_D);
        }
    if (dn_sq.playing)
        cv_rect(4 + (int32_t)(dn_sq.cur[dn.trk] * 232u / L), 50, 1, 42, DN_C_FG);
}

/* ROLL: a piano roll of the selected track: pitch up the screen (an octave in view, KNOB 1 scrolls), the 16 steps in
 * view across, every note a bar as long as it lasts, slides a ramp from the note before, accents bright */
/* a note's colour by its velocity: dim blue (soft) .. light blue (127; an accent) */
static uint16_t dn_vel_col(const dn_step_t *st)
{
    int32_t v = (st->flags & DN_ST_ACC) ? 127 : st->vel;
    if (!dn.dark)                                   /* (LIGHT: pale orange .. the accent) */
        return RGB(255, 200 - (200 - 90) * v / 127, 170 - (170 - 31) * v / 127);
    return RGB(30 + (98 - 30) * v / 127, 60 + (200 - 60) * v / 127, 90 + (255 - 90) * v / 127);
}
/* a note's marks: a condition (a red corner), its retrigs (white ticks where they fall), from x0 over w pixels */
static void dn_step_marks(const dn_step_t *st, int32_t x0, int32_t y, int32_t w, int32_t h)
{
    if (st->cond)
        cv_rect(x0, y, 3, 3, DN_C_RED);
    if (st->flags & DN_ST_RTRG) {
        uint32_t den = DN_RTIM_DEN[st->rtim % DN_RTIM_N], len = st->rlen ? st->rlen : st->len, j;
        int32_t px16 = 13 * 16;                     /* a whole note: 16 steps of 13 pixels */
        for (j = 1; j < 64u; j++) {
            int32_t x = x0 + (int32_t)j * px16 / (int32_t)den;
            if ((uint32_t)(x - x0) * DN_LEN_STEP >= len * 13u || x - x0 >= w)
                break;
            cv_rect(x, y + 1, 1, h - 2, DN_C_FG);
        }
    }
}

/* a held step's values on the status line: four, as the knobs have them (FX: the next four); a ramp: its ends */
static void dn_step_status(const dn_track_t *tr, int drum, int32_t trans)
{
    static const char *const L[3][4] = {{"NOTE", "LEN", "VEL", "COND"}, {"SLIDE", "ACC", "RETRIG", "RATE"},
                                        {"RT LEN", "RT VEL", "CHORD", ""}};
    const dn_step_t *st = &tr->st[dn.step_held];
    char v[4][12], t[8];
    uint32_t k, pg = dn.step_pg % 3u;
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    if (dn.ramp_b >= 0) {                           /* a velocity ramp */
        char line[40];
        line[0] = 0;
        dn_cat(line, "RAMP ");
        dn_num(t, dn.ramp_a + 1); dn_cat(line, t); dn_cat(line, "-");
        dn_num(t, dn.ramp_b + 1); dn_cat(line, t); dn_cat(line, "  VEL ");
        dn_num(t, dn.ramp_v0); dn_cat(line, t); dn_cat(line, " > ");
        dn_num(t, dn.ramp_v1); dn_cat(line, t);
        cv_text(8, 127, &FONT_T, line, DN_C_BLUE);
        return;
    }
    for (k = 0; k < 4u; k++)
        v[k][0] = 0;
    if (pg == 0u) {
        if (drum) {
            dn_cat(v[0], DN_IS_SMP(dn.trk) ? "PAD " : "SL ");
            dn_num(t, st->note + 1);
            dn_cat(v[0], t);
        } else {
            dn_note_name(v[0], st->note + trans);
            if (DN_ST_CHORD_OF(st->flags) && DN_CHORDABLE(dn.trk)) {   /* (0.27) a chord: root and name, F3m7 */
                static const char *const SH[DN_CHORDS] = {"m", "", "m7", "M7", "7", "m9", "M9", "a9", "s2", "s4", "5",
                                                          "o", "+", "m6", "6"};
                const char *n = SH[DN_ST_CHORD_OF(st->flags) - 1u];
                dn_cat(v[0], *n ? n : "M");
            }
        }
        dn_len_text(v[1], st->len);
        dn_num(v[2], (st->flags & DN_ST_ACC) ? 127 : st->vel);
        dn_cond_text(st->cond, v[3]);
    } else if (pg == 1u) {
        dn_cat(v[0], (st->flags & DN_ST_SLIDE) ? (drum ? "REV" : "ON") : "OFF");
        dn_cat(v[1], (st->flags & DN_ST_ACC) ? "ON" : "OFF");
        dn_cat(v[2], (st->flags & DN_ST_RTRG) ? "ON" : "OFF");
        dn_cat(v[3], "1/");
        dn_num(t, DN_RTIM_DEN[st->rtim % DN_RTIM_N]);
        dn_cat(v[3], t);
    } else {
        if (st->rlen)
            dn_len_text(v[0], st->rlen);
        else
            dn_cat(v[0], "NOTE");
        if (!st->rvel)
            dn_cat(v[1], "FLAT");
        else {
            dn_cat(v[1], st->rvel > 0 ? "IN " : "OUT ");
            dn_num(t, st->rvel > 0 ? st->rvel : -st->rvel);
            dn_cat(v[1], t);
        }
        if (DN_CHORDABLE(dn.trk))                   /* (0.28) the chord on it */
            dn_cat(v[2], DN_CHORD_NAME[DN_ST_CHORD_OF(st->flags)]);
        dn_cat(v[3], "FX: NEXT");
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (pg == 2u && k == 2u && !DN_CHORDABLE(dn.trk))
            continue;
        cv_text(cx - text_w(&FONT_T, L[pg][k]) / 2, 122, &FONT_T, L[pg][k], DN_C_DIMTXT);
        cv_text(cx - text_w(&FONT_T, v[k]) / 2, 132, &FONT_T, v[k], k == 3u && pg == 2u ? DN_C_LABEL : DN_C_BLUE);
    }
}

static void dn_draw_roll(void)
{
    const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i, r, v0 = 16u * dn.view, L = tr->len;
    int32_t trans = DN_IS_RS(dn.trk) ? dn_rs.p[RP_TRANS] : 0, prev_y = -1, prev_end = -1, prev_i = 0;
    uint32_t prev_len = 0, legato = (dn_sq.legato & ~DN_PAT_CUT(&dn_sq.pat)) >> dn.trk & 1u;
    char line[48], v[8];
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    for (r = 0; r < DN_ROLL_ROWS; r++) {            /* the rows: a keyboard down the side, black keys' rows darker */
        uint32_t p = dn.roll_lo + r, pc = (uint32_t)((int32_t)p + trans + 120) % 12u;
        int32_t y = 112 - 8 * (int32_t)r, black = (0x54Au >> pc) & 1u;
        cv_rect(30, y, 208, 8, black ? DN_C_WELL : DN_C_TRACK);
        cv_rect(2, y, 25, 7, black ? DN_C_KEYB : DN_C_KEYW);
        if (pc == 0u) {                             /* C: named, and a line under it */
            dn_note_name(v, (int32_t)p + trans);
            cv_text(4, y - 3, &FONT_T, v, dn.dark ? DN_C_BAR : DN_C_FG);
            cv_rect(30, y + 7, 208, 1, DN_C_GRID);
        }
    }
    for (i = 4; i < 16u; i += 4)                    /* the beats */
        cv_rect(30 + 13 * (int32_t)i, 24, 1, 96, DN_C_BG);
    for (i = 0; i < L; i++) {                       /* the notes (one that started earlier shows where it reaches) */
        const dn_step_t *st = &tr->st[i];
        int32_t x0, x1, y, row;
        uint32_t end;
        if (!(st->flags & DN_ST_ON))
            continue;
        end = dn_note_end(tr, i);
        row = (int32_t)st->note - (int32_t)dn.roll_lo;
        y = row < 0 ? 119 : row >= DN_ROLL_ROWS ? 21 : 113 - 8 * row;
        x0 = 30 + 13 * ((int32_t)i - (int32_t)v0);
        x1 = 30 + 13 * ((int32_t)end - (int32_t)v0) - 1;
        if (st->len < DN_LEN_STEP && end == i + 1u)
            x1 = x0 + (int32_t)(13u * st->len / DN_LEN_STEP) - 1;
        if (((st->flags & DN_ST_SLIDE) || (legato && prev_len > (i - (uint32_t)prev_i) * DN_LEN_STEP)) &&
            prev_y >= 0 && prev_end == (int32_t)i && x0 >= 30 && x0 < 238)
            cv_line(x0 - 4, prev_y + 2, x0 + 2, y + 2, DN_C_FG);   /* the slide (flagged, or the note before
                                                                     * reaches past this one): a ramp from it */
        prev_y = y;
        prev_end = (int32_t)end;
        prev_i = (int32_t)i;
        prev_len = st->len;
        if (x1 < 30 || x0 > 237)
            continue;
        x0 = x0 < 30 ? 30 : x0;
        x1 = x1 > 237 ? 237 : x1;
        if (row < 0 || row >= DN_ROLL_ROWS) {       /* out of view: a tick at the edge says which way */
            cv_rect(x0, y, x1 - x0 + 1, 2, DN_C_BLUE_D);
            continue;
        }
        if (DN_ST_CHORD_OF(st->flags) && DN_CHORDABLE(dn.trk)) {   /* (0.27) its chord's other notes, alike */
            uint8_t cn[1u + DN_CHORD_N];
            uint32_t m = dn_chord_notes(st, cn), q;
            for (q = 1; q < m; q++) {
                int32_t cr = (int32_t)cn[q] - (int32_t)dn.roll_lo, cy = 113 - 8 * cr;
                if (cr < 0 || cr >= DN_ROLL_ROWS)
                    continue;
                cv_rect(x0, cy, x1 - x0 + 1, 6, dn_vel_col(st));
                cv_rect(x0, cy, 2, 6, DN_C_BLUE);
            }
        }
        cv_rect(x0, y, x1 - x0 + 1, 6, dn_vel_col(st));   /* (brighter: louder) */
        cv_rect(x0, y, 2, 6, DN_C_BLUE);            /* its start */
        dn_step_marks(st, x0, y, x1 - x0 + 1, 6);
        if ((int32_t)i == dn.step_held) {
            cv_rect(x0, y - 1, x1 - x0 + 1, 1, DN_C_FG);
            cv_rect(x0, y + 6, x1 - x0 + 1, 1, DN_C_FG);
        }
    }
    if (dn_sq.playing && dn_sq.cur[dn.trk] >= v0 && dn_sq.cur[dn.trk] < v0 + 16u)
        cv_rect(30 + 13 * (int32_t)(dn_sq.cur[dn.trk] - v0), 20, 1, 100, DN_C_FG);
    if (dn.roll_keys && dn.roll_cur >= v0 && dn.roll_cur < v0 + 16u)   /* KEYS: the cursor */
        for (r = 0; r < 100u; r += 4u)
            cv_rect(30 + 13 * (int32_t)(dn.roll_cur - v0), 20 + (int32_t)r, 2, 2, DN_C_RED);
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    line[0] = 0;
    if (dn.step_held >= 0) {
        dn_step_status(tr, 0, trans);
        return;
    }
    dn_cat(line, dn.roll_keys ? "KEYS  AT " : dn.rec ? "REC: KEYS  " : "STEP  ");
    if (dn.roll_keys) {
        dn_num(v, dn.roll_cur + 1);
        dn_cat(line, v);
        dn_cat(line, "  LEN ");
        dn_len_text(v, dn.entry_len);
        dn_cat(line, v);
    } else {
        dn_cat(line, "T");
        v[0] = (char)('1' + dn.trk), v[1] = 0;
        dn_cat(line, v);
        dn_cat(line, "  LEN ");
        dn_num(v, (int32_t)L);
        dn_cat(line, v);
        dn_cat(line, "  ");
        dn_num(v, (int32_t)v0 + 1);
        dn_cat(line, v);
        dn_cat(line, "-");
        dn_num(v, (int32_t)v0 + 16);
        dn_cat(line, v);
    }
    cv_text(8, 125, &FONT_XS, line, DN_C_LABEL);
    cv_text(232 - text_w(&FONT_T, "FX: MODE"), 128, &FONT_T, "FX: MODE", DN_C_DIMTXT);
}

/* DRUM: the break track's slices as rows (KNOB 1 picks one), the steps across: a cell lit where that step plays that
 * slice, as long as it lasts, reversed ones marked */
static void dn_draw_drum(void)
{
    const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i, r, v0 = 16u * dn.view, ns = dn_slices(dn.trk);
    char line[48], v[8];
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    for (r = 0; r < DN_DRUM_ROWS && dn.drum_top + r < ns; r++) {
        uint32_t sl = dn.drum_top + r;
        int32_t y = 21 + 12 * (int32_t)r, sel = sl == dn.drum_row;
        v[0] = 0;
        dn_num(v, (int32_t)sl + 1);
        cv_rect(2, y, 25, 11, sel ? DN_C_BLUE : DN_C_BG);
        cv_text(5, y - 1, &FONT_T, v, sel ? DN_C_BAR : DN_C_LABEL);
        for (i = 0; i < 16u; i++) {
            uint32_t si = v0 + i;
            int32_t x = 30 + 13 * (int32_t)i;
            cv_rect(x, y, 11, 10, si >= tr->len ? DN_C_PANEL : i % 4u ? DN_C_TRACK : DN_C_WELL);
        }
    }
    for (i = 0; i < tr->len; i++) {                 /* the hits */
        const dn_step_t *st = &tr->st[i];
        int32_t row, x0, x1, y;
        if (!(st->flags & DN_ST_ON))
            continue;
        row = (int32_t)st->note - (int32_t)dn.drum_top;
        if (row < 0 || row >= DN_DRUM_ROWS)
            continue;
        y = 21 + 12 * row;
        x0 = 30 + 13 * ((int32_t)i - (int32_t)v0);
        x1 = 30 + 13 * ((int32_t)dn_note_end(tr, i) - (int32_t)v0) - 3;
        if (x1 < 30 || x0 > 237)
            continue;
        x0 = x0 < 30 ? 30 : x0;
        x1 = x1 > 237 ? 237 : x1;
        cv_rect(x0, y, x1 - x0 + 1, 10, tr->mute ? DN_C_KNOB : dn_vel_col(st));
        dn_step_marks(st, x0, y, x1 - x0 + 1, 10);
        if (st->flags & DN_ST_SLIDE)                /* reversed: an arrow pointing back */
            cv_line(x0 + 7, y + 2, x0 + 3, y + 5, DN_C_FG), cv_line(x0 + 3, y + 5, x0 + 7, y + 8, DN_C_FG);
        if ((int32_t)i == dn.step_held)
            cv_rect(x0 + 2, y + 3, 4, 4, DN_C_FG);
    }
    if (dn_sq.playing && dn_sq.cur[dn.trk] >= v0 && dn_sq.cur[dn.trk] < v0 + 16u)
        cv_rect(30 + 13 * (int32_t)(dn_sq.cur[dn.trk] - v0) + 5, 18, 1, 102, DN_C_FG);
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    line[0] = 0;
    dn_cat(line, DN_IS_SMP(dn.trk) ? "SAMPLER  PAD " : DN_BK(dn.trk) ? "BRK B  SL " : "BRK A  SL ");
    if (dn.step_held >= 0) {
        dn_step_status(tr, 1, 0);
        return;
    }
    dn_num(v, dn.drum_row + 1);
    dn_cat(line, v);
    dn_cat(line, "  LEN ");
    dn_num(v, tr->len);
    dn_cat(line, v);
    dn_cat(line, "  ");
    dn_num(v, (int32_t)v0 + 1);
    dn_cat(line, v);
    dn_cat(line, "-");
    dn_num(v, (int32_t)v0 + 16);
    dn_cat(line, v);
    cv_text(8, 125, &FONT_XS, line, DN_C_LABEL);
}

/* the tab row's pages: the sound pages (SELECT cycles them), the sequencer's (SEQ), the breaks' (ARP) */
#define DN_MAXTABS 8u
/* the instrument a track is, by name */
static const char *dn_inst_name(uint32_t t)
{
    static char m[8] = "MIDI ";
    t %= DN_SEQ_TRACKS;
    if (!DN_IS_MIDI(t))
        return DN_E_NAME[dn_eng[t] % DN_E_N];
    m[5] = (char)('1' + dn_midi_idx(t) % 9u), m[6] = 0;
    return m;
}
/* a sequencer page's tab row: the instrument, then its 8 patterns (the playing one lit, a queued one flashing, the
 * ones with notes marked), then how full pattern memory is */
static void dn_draw_pat_tabs(void)
{
    uint32_t t = dn.trk, n, used = dn_pat_used(t), pct = dn_pat_notes() * 100u / dn_pat_room();
    int32_t x = 66;
    char v[8];
    cv_rect(0, 0, 64, 17, DN_C_BLUE);
    cv_text(32 - text_w(&FONT_T, dn_inst_name(t)) / 2, 3, &FONT_T, dn_inst_name(t), DN_C_BAR);
    for (n = 0; n < DN_NPAT; n++, x += 17) {
        int cur = n == dn_pcur[t], q = n == dn_pq[t] && (plat_ms() / 200u) % 2u == 0u;
        uint16_t bg = cur ? DN_C_FG : q ? DN_C_BLUE : DN_C_PANEL;
        cv_rect(x, 1, 15, 15, bg);
        v[0] = (char)('1' + n), v[1] = 0;
        cv_text(x + 8 - text_w(&FONT_T, v) / 2, 3, &FONT_T, v, cur || q ? DN_C_BAR : (used >> n & 1u) ? DN_C_FG
                                                                                           : DN_C_DIMTXT);
        if ((used >> n & 1u) && !cur)
            cv_rect(x + 2, 13, 11, 2, DN_C_BLUE_D);
    }
    dn_num(v, (int32_t)pct);
    dn_cat(v, "%");
    cv_text(238 - text_w(&FONT_T, v), 3, &FONT_T, v, pct > 90u ? DN_C_RED : DN_C_DIMTXT);
}

/* (0.29) what track t plays, in a few letters: the sound, the loop, the voice, the channel */
static const char *dn_trk_sound(uint32_t t)
{
    static char m[12];
    t %= DN_SEQ_TRACKS;
    if (DN_IS_RS(t))
        return dn_sound_name(0, dn.preset);
    if (DN_IS_SUB(t))
        return dn_sound_name(1, dn.sb_preset);
    if (DN_IS_BRK(t))
        return dn_bk[DN_BK(t)].ok ? dn_bk[DN_BK(t)].loop.name : "NO LOOP";
    if (DN_IS_FM(t))
        return dn_fm_vcname();
    if (DN_IS_SMP(t))
        return dn_sm.p.mode == DN_SMP_ONESHOT ? "PADS" : "KEYS";
    if (DN_IS_MIDI(t)) {
        uint32_t c = dn_sq.pat.tr[t].chan + 1u;
        m[0] = 0;
        dn_cat(m, "CH ");
        if (c >= 10u)
            dn_cat(m, "1"), c -= 10u;
        m[str_len_dn(m) + 1u] = 0;
        m[str_len_dn(m)] = (char)('0' + c);
        return m;
    }
    return "";
}

/* (0.29) HOME: the eight tracks, each its number, instrument, sound; the picked one lit; a note playing lights a
 * dot, a muted track is dim. A key 1-8 opens one; SELECT picks, HOME opens it */
static void dn_draw_home(void)
{
    uint32_t t;
    cv_rect(0, 0, 64, 17, DN_C_BLUE);
    cv_text(32 - text_w(&FONT_T, "HOME") / 2, 3, &FONT_T, "HOME", DN_C_BAR);
    cv_text(236 - text_w(&FONT_XS, "KEY 1-8 OPENS A TRACK"), 4, &FONT_XS, "KEY 1-8 OPENS A TRACK", DN_C_DIMTXT);
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        int32_t x = 2 + (int32_t)(t % 4u) * 59, y = 21 + (int32_t)(t / 4u) * 50;
        int sel = t == dn.trk, off = dn_eng[t] == DN_E_OFF, mute = dn_sq.pat.tr[t].mute;
        const dn_track_t *tr = &dn_sq.pat.tr[t];
        uint16_t hi = dn.dark ? DN_C_BLUE : DN_C_ACC, on = dn.dark ? DN_C_BAR : DN_C_ONACC;   /* (LIGHT: orange) */
        uint16_t fg = sel ? on : off || mute ? DN_C_DIMTXT : DN_C_FG;
        char n[4] = {0, 0, 0, 0}, nm[10];
        uint32_t i;
        cv_rect(x, y, 57, 47, sel ? hi : DN_C_PANEL);
        if (!sel && !dn.dark)
            cv_rect(x, y, 57, 3, dn_eng[t] == DN_E_OFF ? DN_C_KNOB : DN_C_FG);   /* (LIGHT: a rule on top) */
        n[0] = (char)('1' + t);
        cv_text(x + 4, y + 2, &FONT_S, n, sel ? on : off ? DN_C_DIMTXT : DN_C_BLUE);
        if (dn_sq.playing && !mute && !off && dn_sq.cur[t] < tr->len && (tr->st[dn_sq.cur[t]].flags & DN_ST_ON))
            dn_disc(x + 49, y + 8, 4, sel ? on : DN_C_RED);   /* (a note on this step) */
        cv_text(x + 4, y + 21, &FONT_T, dn_inst_name(t), fg);
        for (i = 0; i < 9u && dn_trk_sound(t)[i]; i++)             /* (the sound's name, as much as fits) */
            nm[i] = dn_trk_sound(t)[i];
        nm[i] = 0;
        while (i > 0u && text_w(&FONT_XS, nm) > 50)
            nm[--i] = 0;
        cv_text(x + 4, y + 34, &FONT_XS, mute && !off ? "MUTED" : nm, sel ? on : mute ? DN_C_RED : DN_C_LABEL);
    }
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    {
        char l[24];
        l[0] = 'T', l[1] = (char)('1' + dn.trk), l[2] = ' ', l[3] = 0;
        dn_cat(l, dn_inst_name(dn.trk));
        cv_text(8, 127, &FONT_T, l, DN_C_FG);
        if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0)
            cv_text(232 - text_w(&FONT_T, dn.msg), 127, &FONT_T, dn.msg, DN_C_RED);
        else
            cv_text(232 - text_w(&FONT_T, "HOME: OPEN IT"), 127, &FONT_T, "HOME: OPEN IT", DN_C_DIMTXT);
    }
}

static uint32_t dn_tabs(uint8_t *ids, const char **nms)
{
    static const uint8_t BRK_T[3] = {DN_PG_BRK, DN_PG_BRK2, DN_PG_SLICE};
    static const char *const BRK_N[3] = {"BRK", "BRK2", "SLICE"};
    static const char *const MN[8] = {"MIDI 1", "MIDI 2", "MIDI 3", "MIDI 4", "MIDI 5", "MIDI 6", "MIDI 7", "MIDI 8"};
    static const char *const FN[5] = {"MIX", "FX A", "FX A 2", "FX B", "FX B 2"};
    uint32_t k, n = 0;
    if (dn.page == DN_PG_HOME) {                    /* (0.29) HOME: the eight tracks (the picked one lit) */
        static char hn[DN_SEQ_TRACKS][10];
        for (k = 0; k < DN_SEQ_TRACKS && k < DN_MAXTABS; k++) {
            hn[k][0] = (char)('1' + k), hn[k][1] = ' ', hn[k][2] = 0;
            dn_cat(hn[k], dn_inst_name(k));
            ids[n] = k == dn.trk ? DN_PG_HOME : 0xFFu, nms[n++] = hn[k];
        }
    } else if (dn.page == DN_PG_MIDI) {             /* the MIDI instruments (SELECT: the next) */
        for (k = 0; k < dn_midi_n() && k < 8u; k++)
            ids[n] = k == dn.midi_sel ? DN_PG_MIDI : 0xFFu, nms[n++] = MN[k];
        if (!n)
            ids[n] = DN_PG_MIDI, nms[n++] = "NO MIDI TRACK";
    } else if (dn.page == DN_PG_FX) {               /* the rack: MIX, FX A, FX B (FX: the next) */
        for (k = 0; k < 3u; k++) {                  /* (FX A's next values in FX A's place) */
            uint32_t v = k == 0u ? 0u : k == 1u ? (dn.fx_view == 2u ? 2u : 1u) : (dn.fx_view == 4u ? 4u : 3u);
            ids[n] = v == dn.fx_view ? DN_PG_FX : 0xFFu, nms[n++] = FN[v];
        }
    } else if (dn.page == DN_PG_PROJ) {
        ids[n] = DN_PG_PROJ, nms[n++] = "PROJECT";
    }
    else if (DN_IS_BRKPG(dn.page))
        for (k = 0; k < 3u; k++)
            ids[n] = BRK_T[k], nms[n++] = BRK_N[k];
    else if (dn.page != DN_PG_PROJ && dn.page != DN_PG_FX)
        for (k = 0; k < DN_NSND; k++) {
            static const char *const LN[DN_NLFO] = {"LFO1", "LFO2", "LFO3", "LFO4"};
            if (DN_IS_OWNPG(dn.trk)) {              /* SAMPLER, FM: their five (by mode / view), SETUP; no LFO */
                if (DN_SND[k] == DN_PG_LFO)
                    continue;
                ids[n] = DN_SND[k];
                nms[n++] = DN_SND[k] >= DN_NPAGES ? DN_SND_NAME[k]
                         : DN_IS_FM(dn.trk) && DN_SND[k] == 2u ? dn_fm_egtab()
                         : DN_IS_FM(dn.trk) ? DN_FM_TAB[(dn.fm_alt >> DN_SND[k]) & 1u][DN_SND[k]]
                         : DN_SMP_TAB[dn_sm.p.mode & 1u][DN_SND[k]];
                continue;
            }
            ids[n] = DN_SND[k];
            nms[n] = DN_SND[k] == DN_PG_LFO && dn.page == DN_PG_LFO ? LN[dn.lfo_sel % DN_NLFO] : DN_SND_NAME[k];
            if (DN_SND[k] == 0u && dn.page == DN_PG_SOSC2)   /* (SUB's second pages in their first's place) */
                ids[n] = DN_PG_SOSC2, nms[n] = "OSC2";
            if (DN_SND[k] == 1u && dn.page == DN_PG_DIRT)
                ids[n] = DN_PG_DIRT, nms[n] = "DIRT";
            if (DN_SND[k] == 2u && dn.page == DN_PG_FENV)
                ids[n] = DN_PG_FENV, nms[n] = "F.ENV";
            n++;
        }
    return n;
}

static void dn_draw_bottom(void)
{
    uint32_t k;
    int32_t x;
    char t[24];
    cv_begin(240, 144, DN_C_BG);
    if (dn.naming) {                                /* (0.22) the name editor */
        uint32_t i;
        char c[2] = {0, 0};
        t[0] = 0;
        if (dn.naming == 1u)
            dn_cat(t, "NAME THE PROJECT");
        else {
            dn_cat(t, dn.nm_set ? "NAME SUB USER " : "NAME REESE USER ");
            c[0] = (char)('1' + dn.nm_slot);
            dn_cat(t, c);
        }
        cv_rect(0, 0, 240, 17, DN_C_BLUE);
        cv_text(120 - text_w(&FONT_T, t) / 2, 3, &FONT_T, t, DN_C_BAR);
        for (i = 0; i < dn.nm_len; i++) {           /* a box a letter, the cursor's lit */
            int32_t bx = 120 - (int32_t)dn.nm_len * 10 + (int32_t)i * 20, on = i == dn.nm_cur;
            cv_rect(bx, 40, 18, 30, on ? DN_C_BLUE : DN_C_WELL);
            c[0] = dn.nm[i] == ' ' ? '_' : dn.nm[i];
            cv_text(bx + 9 - text_w(&FONT_M, c) / 2, 46, &FONT_M, c, on ? DN_C_BAR : dn.nm[i] == ' ' ? DN_C_DIMTXT
                                                                                              : DN_C_FG);
        }
        cv_text(120 - text_w(&FONT_XS, "K1 / SELECT: PLACE    K2 / PRESETS: LETTER") / 2, 88, &FONT_XS,
                "K1 / SELECT: PLACE    K2 / PRESETS: LETTER", DN_C_LABEL);
        cv_rect(0, 121, 240, 23, DN_C_BAR);
        cv_text(8, 127, &FONT_T, "SAVE: KEEP", DN_C_FG);
        cv_text(232 - text_w(&FONT_T, "HOME: CANCEL"), 127, &FONT_T, "HOME: CANCEL", DN_C_DIMTXT);
        cv_blit(0, 96);
        return;
    }
    if (dn.page == DN_PG_HOME) {                    /* (0.29) */
        dn_draw_home();
        cv_blit(0, 96);
        return;
    }
    {   /* the page tabs: the pages of this group only (the ones its button / SELECT cycle through) */
        uint8_t ids[DN_MAXTABS];
        const char *nms[DN_MAXTABS];
        uint32_t n = DN_IS_SEQPG(dn.page) ? 0u : dn_tabs(ids, nms), w;
        if (DN_IS_SEQPG(dn.page))
            dn_draw_pat_tabs();                     /* an instrument's sequencer: its name, its 8 patterns */
        for (k = 0, x = 0; k < n; k++) {
            int on = ids[k] == dn.page;
            w = k == n - 1u ? 240u - (uint32_t)x : 240u / n;
            cv_rect(x, 0, (int32_t)w - 1, 17, on ? DN_C_BLUE : DN_C_PANEL);
            cv_text(x + ((int32_t)w - 1 - text_w(&FONT_T, nms[k])) / 2, 3, &FONT_T, nms[k], on ? DN_C_BAR : DN_C_LABEL);
            x += (int32_t)w;
        }
    }
    if (DN_IS_SEQPG(dn.page)) {
        if (dn.page == DN_PG_SEQ)
            dn_draw_seq();
        else if (dn.page == DN_PG_ROLL)
            dn_draw_roll();
        else
            dn_draw_drum();
        if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0) {   /* a word (MOVED, SWAPPED, ...) over the right end */
            int32_t mw = text_w(&FONT_T, dn.msg);
            cv_rect(226 - mw, 123, mw + 12, 19, DN_C_BAR);
            cv_text(232 - mw, 127, &FONT_T, dn.msg, DN_C_RED);
        }
        cv_blit(0, 96);
        return;
    }
    if (dn.page == DN_PG_LFO && dn.lfo_view == 2u) {   /* DRAW: the points in view as bars, one a white key */
        const dn_lfo_p_t *l = &dn_lp[DN_LSEL];
        uint32_t n = l->npts == 32u ? 32u : 16u, i;
        char v[8];
        cv_rect(0, 17, 240, 104, DN_C_PANEL);
        cv_rect(6, 22, 228, 92, DN_C_WELL);
        cv_rect(6, 68, 228, 1, DN_C_GRID);
        for (i = 0; i < 16u; i++) {
            uint32_t pt = i + 16u * dn.pt_view;
            int32_t x = 8 + (int32_t)i * 14, h = pt < n ? l->pts[pt] * 42 / 100 : 0;
            uint16_t c = (int8_t)pt == dn.pt_held ? DN_C_FG : DN_C_BLUE;
            if (pt >= n)
                continue;
            if (h >= 0)
                cv_rect(x, 68 - h, 12, h + 1, c);
            else
                cv_rect(x, 68, 12, -h + 1, c);
        }
        {   /* the pen: a red line at its level */
            int32_t y = 68 - dn.pen * 42 / 100;
            cv_rect(6, y, 228, 1, DN_C_RED);
        }
        cv_rect(0, 121, 240, 23, DN_C_BAR);
        t[0] = 0;
        dn_cat(t, "PEN ");
        dn_num(v, dn.pen);
        dn_cat(t, v);
        dn_cat(t, dn.pt_view ? "  17-32" : n == 32u ? "  1-16" : "");
        cv_text(8, 127, &FONT_T, t, DN_C_FG);
        cv_text(232 - text_w(&FONT_T, "K2 SMOOTH K3 PTS FX"), 127, &FONT_T, "K2 SMOOTH K3 PTS FX", DN_C_LABEL);
        cv_blit(0, 96);
        return;
    }
    /* the four dials: KNOB 1..4 */
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    cv_rect(0, 17, 240, 1, DN_C_EDGE);
    for (k = 0; k < 4u; k++)
        if (dn_dial(k) != DN_PX_NONE)
            dn_knob(30 + 60 * (int32_t)k, 67, dn_dial(k), 1);
    /* the status line */
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' ';
    dn_text(DN_PX_OCT, dn.oct, t + 4);
    cv_text(8, 125, &FONT_XS, t, DN_C_LABEL);
    if (dn.play)
        cv_text(70, 125, &FONT_XS, "HOLD", DN_C_BLUE);
    if (dn.page == DN_PG_LFO) {                     /* the LFO page: which view, what FX does */
        const dn_lfo_p_t *l = &dn_lp[DN_LSEL];
        const char *nx = dn.lfo_view == 0u ? "FX: MORE" : dn.lfo_view == 1u && l->shape == DN_LS_DRAW ? "FX: DRAW"
                                                                                                    : "FX: BACK";
        cv_rect(0, 121, 240, 23, DN_C_BAR);
        cv_text(8, 127, &FONT_T, dn.lfo_view ? "MORE: SYNC MODE PHASE SMOOTH" : "LFO: AGAIN FOR THE NEXT", DN_C_LABEL);
        cv_text(232 - text_w(&FONT_T, nx), 127, &FONT_T, nx, DN_C_FG);
        cv_blit(0, 96);
        return;
    }
    if (dn.page == DN_PG_SLICE) {                   /* chop mode: which chop, where the cursor is, what to do */
        const dn_brk_t *b = &dn_bk[dn_bsel()];
        uint32_t r = b->loop.rate ? b->loop.rate : 22050u, cs = dn.sl_cur / (r / 100u ? r / 100u : 1u);
        char l[48], v[12];
        l[0] = 0;
        dn_cat(l, "CHOP ");
        dn_num(v, (int32_t)dn.sl_sel + 1);
        dn_cat(l, v);
        dn_cat(l, "/");
        dn_num(v, (int32_t)b->nslices);
        dn_cat(l, v);
        dn_cat(l, "  ");
        dn_num(v, (int32_t)(cs / 100u));
        dn_cat(l, v);
        dn_cat(l, ".");
        v[0] = (char)('0' + cs / 10u % 10u), v[1] = (char)('0' + cs % 10u), v[2] = 's', v[3] = 0;
        dn_cat(l, v);
        cv_rect(0, 121, 240, 23, DN_C_BAR);
        cv_text(8, 127, &FONT_T, l, DN_C_FG);
        if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0)
            cv_text(232 - text_w(&FONT_T, dn.msg), 127, &FONT_T, dn.msg, DN_C_RED);
        else
        {
            const char *h = dn.chop_rec ? (b->aud ? "KEYS CUT ON THE HITS" : "KEYS CUT AT CURSOR")
                          : b->aud ? "REC: KEYS CUT" : "REC KEYS CUT  FX MOVE";
            cv_text(232 - text_w(&FONT_T, h), 127, &FONT_T, h, dn.chop_rec ? DN_C_RED : DN_C_LABEL);
        }
        cv_blit(0, 96);
        return;
    }
    if (dn.page == DN_PG_PROJ) {                    /* PROJECT: the samples' memory, the FX memory, the load */
        char c[40], v[8];
        uint32_t used = dn_smp_used() / 1024u, cap = dn_smp_cap() / 1024u;
        c[0] = 0;
        dn_cat(c, "SMP ");
        dn_num(v, (int32_t)used);
        dn_cat(c, v);
        dn_cat(c, "/");
        dn_num(v, (int32_t)cap);
        dn_cat(c, v);
        dn_cat(c, "K FX ");
        dn_num(v, (int32_t)((dn_fxs[0].bytes + dn_fxs[1].bytes) / 1024u));
        dn_cat(c, v);
        dn_cat(c, "/");
        dn_num(v, (int32_t)(DN_FX_ARENA / 1024u));
        dn_cat(c, v);
        dn_cat(c, "K CPU ");
        dn_num(v, (int32_t)plat_cpu_pct());
        dn_cat(c, v);
        dn_cat(c, " PK ");                          /* (0.16.1: the peak since the last look) */
        dn_num(v, (int32_t)plat_cpu_peak_pct());
        dn_cat(c, v);
        dn_cat(c, "%");
        cv_rect(0, 121, 240, 23, DN_C_BAR);
        if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0)
            cv_text(232 - text_w(&FONT_T, dn.msg), 127, &FONT_T, dn.msg, DN_C_RED);
        else
            cv_text(8, 125, &FONT_XS, c, DN_C_LABEL);
        cv_blit(0, 96);
        return;
    }
    if (dn.page == DN_PG_BRK2) {                    /* BRK2: the audio's load now (STRETCH costs more) */
        char c[12] = "CPU ";
        dn_num(c + 4, (int32_t)plat_cpu_pct());
        dn_cat(c, "%");
        cv_text(104, 125, &FONT_XS, c, DN_C_LABEL);
    } else {
        t[0] = 'T'; t[1] = (char)('1' + dn.trk); t[2] = 0;
        cv_text(120, 125, &FONT_XS, t, dn.trk ? DN_C_LABEL : DN_C_DIMTXT);
    }
    if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0) {   /* a word (NO MEMORY ...) over the right end */
        int32_t mw = text_w(&FONT_T, dn.msg);
        cv_rect(mw > 90 ? 100 : 226 - mw, 121, mw > 90 ? 140 : mw + 14, 23, DN_C_BAR);
        cv_text(232 - mw, 127, &FONT_T, dn.msg, DN_C_RED);
    } else if (dn.dirty)
        cv_text(232 - text_w(&FONT_XS, "SAVE?"), 125, &FONT_XS, "SAVE?", DN_C_BLUE);
    else
        cv_text(232 - text_w(&FONT_XS, "SAVED"), 125, &FONT_XS, "SAVED", DN_C_DIMTXT);
    cv_blit(0, 96);
}

void dn_app_draw(int force)
{
    uint32_t lit = 0;
    if (force)
        dn.redraw = DN_RD_ALL;
    uint32_t keys = dn.keys_prev, dim = 0, k;
    static uint32_t shown_pos;
    if ((DN_IS_SEQPG(dn.page) || dn.page == DN_PG_HOME) && dn_sq.playing && dn_sq.pos != shown_pos)
        dn.redraw |= DN_RD_BOTTOM;                  /* the playhead moved */
    shown_pos = dn_sq.pos;
    {                                               /* (0.16.1) the audio ran late (out of CPU): say so; PROJECT's
                                                     * load line kept fresh */
        static uint32_t xr_seen, proj_t;
        uint32_t xr = plat_xruns();
        if (xr != xr_seen) {
            if ((xr_seen || xr > 2u) && plat_ms() - dn_flash_t > 1500u)   /* (the start-up's own aside; a flash write's
                                                     * pause is not the CPU running out) */
                dn_say("AUDIO LATE: CPU FULL");
            xr_seen = xr;
        }
        if (dn.page == DN_PG_PROJ && plat_ms() - proj_t >= 500u) {
            proj_t = plat_ms();
            dn.redraw |= DN_RD_BOTTOM;
        }
    }
    if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) <= 0) {
        dn.msg = 0;                                 /* a status-line word ran out */
        dn.redraw |= DN_RD_BOTTOM;
    }
    dn_draw_top();                                  /* every frame: the scope moves */
    if (dn.redraw & DN_RD_BOTTOM)
        dn_draw_bottom();
    dn.redraw = 0;
    lit |= 1u << (dn.page == DN_PG_HOME ? B_HOME : DN_IS_SEQPG(dn.page) ? B_SEQ : DN_IS_BRKPG(dn.page) ? B_ARP : DN_IS_GLOPG(dn.page) ? B_GLO
                  : dn.page == DN_PG_LFO ? B_LFO : dn.page == DN_PG_SOSC2 ? B_SEL : dn.page == DN_PG_DIRT ? B_EDIT
                  : dn.page == DN_PG_FENV ? B_ENV
                  : DN_PAGE_BTN[dn.page % DN_NPAGES]);
    if ((dn.page == DN_PG_LFO && dn.lfo_view) || (dn.page == DN_PG_FX && dn.fx_view))
        lit |= 1u << B_FX;
    if ((dn.page == 2u || dn.page == DN_PG_FENV) && DN_IS_SYN(dn.trk) && (DN_IS_SUB(dn.trk) ? dn_sb.esync : dn_rs.esync))
        lit |= 1u << B_FX;                          /* (AMP: the envelope synced) */
    if (dn.page == DN_PG_PROJ && dn.peng != 0xFFu && (plat_ms() / 250u) % 2u)
        lit |= 1u << B_FX;                          /* (an instrument chosen: FX blinks to put it in) */
    if (dn.page == DN_PG_ROLL && dn.roll_keys)
        lit |= 1u << B_FX;
    if (dn.dirty)
        lit |= 1u << B_SAVE;
    if (dn_sq.playing || (dn.cin_left && (plat_ms() / 125u) % 2u == 0u))
        lit |= 1u << B_PLAY;                        /* (counting in: it blinks) */
    if (dn.rec)
        lit |= 1u << B_REC;
    if (dn.page == DN_PG_HOME)
        lit |= 1u << B_HOME;
    if (DN_IS_SEQPG(dn.page) && !(dn.page == DN_PG_ROLL && (dn.roll_keys || dn.rec))) {   /* the steps on, the playhead dim; the
                                                     * tracks playing (DRUM: the steps playing the selected slice) */
        const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        keys = 0;
        for (k = 0; k < NKEYS; k++) {
            int32_t o = dn_key_ord(k);
            uint32_t si = (uint32_t)o + 16u * dn.view;
            if (dn_is_white(k)) {
                if (si < tr->len && (tr->st[si].flags & DN_ST_ON) &&
                    (dn.page != DN_PG_DRUM || tr->st[si].note == dn.drum_row))
                    keys |= 1u << k;
                else if (si < tr->len && (tr->st[si].flags & DN_ST_ON))
                    dim |= 1u << k;
                else if (dn_sq.playing && dn_sq.cur[dn.trk] == si)
                    dim |= 1u << k;
            } else if (o < 8 && !dn_sq.pat.tr[o].mute) {
                keys |= 1u << k;
            }
        }
    }
    if (dn.page == DN_PG_HOME) {                    /* (0.29) HOME: white keys 1-8 lit for the tracks with an
                                                     * instrument (the picked one dim), black keys 1-8 as notes play */
        keys = 0;
        for (k = 0; k < NKEYS; k++) {
            int32_t o = dn_key_ord(k);
            const dn_track_t *tr;
            if (o >= 8)
                continue;
            tr = &dn_sq.pat.tr[o];
            if (dn_is_white(k) && dn_eng[o] != DN_E_OFF)
                *(o == (int32_t)dn.trk ? &dim : &keys) |= 1u << k;
            else if (!dn_is_white(k) && dn_sq.playing && !tr->mute && dn_sq.cur[o] < tr->len &&
                     (tr->st[dn_sq.cur[o]].flags & DN_ST_ON))
                keys |= 1u << k;
        }
    }
    if (dn.page == DN_PG_SLICE && DN_IS_BRK(dn.trk)) {   /* chop mode: the keys with a chop dim, the selected lit;
                                                     * cutting (REC lit): the next key to cut lit too */
        uint32_t ns = dn_slices(dn.trk);
        if (dn.chop_rec)
            lit |= 1u << B_REC;
        for (k = 0; k < NKEYS; k++) {
            uint32_t c = k + 27u * (uint32_t)dn.oct;
            if (dn.oct < 0 || c > ns || (c == ns && !dn.chop_rec))
                continue;
            if (c == dn.sl_sel || c == ns)
                keys |= 1u << k;
            else
                dim |= 1u << k;
        }
    }
    plat_keys_dim(dim);
    plat_leds(lit, keys);
    dn_lit_btn = lit, dn_lit_keys = keys;
}

#include "dn_editor.c"                              /* the web editor's commands */
