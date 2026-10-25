/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the web editor's commands (web/editor), on the editor frame (F0 7D 46 4C cmd args F7). Copyright (C)
 * 2026 Jamie (TwistedGenetics). A unity-build fragment at the end of dn_app.c: the device glue (src/hardware) passes
 * every command it does not know to dn_ed_cmd, and so does the host harness (tests/host/dn_host.c: `sysex`).
 *
 * All values are 7-bit bytes. A number of n "digits" is n x 7 bits, low first; a signed value is sent + 8192 in two
 * digits. Bytes (the pattern) go 7 to 8: a byte of the seven top bits, then the seven bytes' low bits (as SMP_READ).
 *
 *   60 STATE                   -> rev(5) bpm(2) playing rec preset trk swing dirty slicesA slicesB presets loops
 *                                 patsize(2) nparams step(2)       rev changes whenever the sound or pattern does
 *   61 PARAMS                  -> n, then each value (2, +8192)      the order: DN_ED_PARAM (REESE, BPM, BREAKS A, B)
 *   62 PARAM_SET i v(2)        -> rc (0 set)
 *   63 PAT_READ off(2) n       -> off(2), bytes                      the pattern as the FM-1 holds it (dn_pattern_t,
 *                                                                    little-endian), n <= 200
 *   64 PAT_WRITE off(2) bytes  -> rc                                 into a copy...
 *   65 PAT_COMMIT              -> rc (0 in use; 1 refused)           ...which then replaces the pattern, checked
 *   66 TRANSPORT on            -> playing
 *   67 SAVE                    -> rc (0 kept)
 *   68 PRESET i                -> rc                                 a factory REESE sound
 *   69 NAMES kind from         -> count from, names (0 after each)   kind 0 REESE presets, 1 the loops BREAKS can play
 *   70 LFO_PTS_GET l           -> l, npts, 32 points (2, +100)      an LFO's DRAW shape (0.9)
 *   71 LFO_PTS_SET l npts pts  -> rc (0 set)                         32 points (2 each, +100), -100..100
 *   72 LFO_NOW                 -> 8 x (phase (2): 14 bits, value (2): +8192, 14 bits)   where the LFOs are
 *   73 PATS                    -> per track: playing, queued (127 none), used (2: a bit a pattern); then notes (2), room (2)
 *   74 PAT_SEL t n             -> rc (0)                             track t to pattern n (stopped: now; playing: when
 *                                                                    its pattern ends) (0.11; 8 patterns a track)
 *   75 PROJ                    -> name (12 chars, 0-padded), eng x8, send A x8, send B x8, return A, B, FX A {type,
 *                                 p0, p1, p2}, FX B {...}, samples used (3), samples room (3), FX memory used KB (2),
 *                                 FX memory KB (2), cpu %, kinds of instrument, kinds of FX   (0.12),
 *                                 FX A {p3..p6}, FX B {p3..p6}                                (0.16)
 *   76 PROJ_SET field v(2)     -> rc (0 set; 1 refused: no memory for that FX, or a bad field)
 *                                 fields: 0-7 track n's instrument (REESE / BRK A / BRK B swap), 8-15 send A,
 *                                 16-23 send B, 24-25 returns, 26-29 FX A type p0 p1 p2, 30-33 FX B's,
 *                                 34-37 FX A p3..p6, 38-41 FX B's (0.16)
 *   77 PROJ_NAME chars...      -> rc (0)                             up to 11 characters, 32-126
 * NAMES kinds from 0.12: 2 the instruments, 3 the effects, 4 the delay's times; 5 SUB's factory sounds (0.13).
 *   78 SUB_PRESET i            -> rc                                 SUB's factory sound i (0.13)
 *   79 PARAMS_HI               -> n, n x value (2, +8192)           the values past PARAMS' 7-bit index (0.19):
 *      PARAMS_HI i v(2)        -> rc (0 set)                         0-3 REESE's filter envelope ATK DEC SUS REL,
 *                                                                    4-7 SUB's
 *   80 SMP_GET kind i          -> kind, i, n, n x value (2, +8192)   SAMPLER (0.23): kind 0 pad i's values (dn_smp.h
 *                                                                    PD_*, 16), 1 zone i's (ZN_*, 10), 2 the keys'
 *                                                                    (KY_*, 9), 3 the mode (0 ONE-SHOT, 1 KEYS)
 *   81 SMP_SET kind i k v(2)   -> rc (0 set)                         one of those (a SAMPLE: the loops' order, -1 OFF)
 *      (0.36: kinds 4-7 the same of SAMPLER 2)
 *   82 SMP_PLAY note on [2]    -> rc                                 a hit (ONE-SHOT: note is the pad) or a key of the
 *                                                                    map, on / off: the editor's audition
 *   83 FM_GET                  -> the FM voice (156: dn_fm.h's unpacked voice, OP6 first), LEVEL, VOICES, the macros
 *                                 BRIGHT TIME ATTACK RELEASE (+64 each), the voice PRESETS is on (2), how many (2) (0.25)
 *   84 FM_SET k(2) v(2)        -> rc (0 set)                         k 0..155 a value of the voice, 156 LEVEL, 157
 *                                                                    VOICES, 158..161 the macros (v + 8192)
 *   85 FM_LOAD i(2)            -> rc                                 PRESETS' voice i (factory, then the project's)
 *      FM_LOAD 156 bytes       -> rc                                 this voice (the editor's: a bank's, auditioned)
 *   86 FM_PLAY note on         -> rc                                 FM plays a note (on / off): the audition
 * NAMES kind 6 (0.25): FM's voices, the factory's then the project's.
 *   90 WT_GET                  -> WAVE (0.32): how many values (WP_N), each (2, +8192: dn_wt.h WP_*), the factory sound
 *                                 PRESETS is on (2), how many (2), the tables (2), the factory's of them (2)
 *   91 WT_SET k v(2)           -> rc (0 set)                         value WP_k (TABLE: the tables' order)
 *   92 WT_LOAD i(2)            -> rc                                 factory sound i
 *   93 WT_PLAY note on         -> rc                                 WAVE plays a note (on / off): the audition
 * NAMES kind 7 (0.32): WAVE's tables, the factory's then the slots'; kind 8: its factory sounds.
 *   94 JU_GET                  -> JUNO (0.36): how many values (JP_N), each (2, +8192: dn_ju.h JP_*), the factory sound
 *                                 PRESETS is on (2), how many (2)
 *   95 JU_SET k v(2)           -> rc (0 set)                         value JP_k
 *   96 JU_LOAD i(2)            -> rc                                 factory sound i
 *   97 JU_PLAY note on         -> rc                                 JUNO plays a note (on / off): the audition
 * NAMES kind 9 (0.36): JUNO's factory sounds.
 *   87 UI_STATE                -> what the screen shows (0.26, the editor's panel): page, track, its instrument, flags
 *                                 (1 playing, 2 rec, 4 changed, 8 naming, 16 HOLD, 32 a sequencer page, 64 a step held),
 *                                 OCT + 8, BPM (2), position (2), the tag and the sound's name (strings), the tabs (count,
 *                                 the one lit (127 none), names), the 4 knobs (1 there / 0; then id (2), name, value,
 *                                 min, max (2 each, + 8192), text), the message, the left of the status line, the lit
 *                                 buttons (2) and keys (4), the name editor's cursor. Strings: up to 24 chars, 0 after
 *   89 CHORDS                  -> the 15 chords (MIN .. 6), 5 notes over the root each (+64; 0: none) (0.28: fixed)
 *   88 UI_INPUT kind id v      -> rc                                 as if on the panel: kind 0 button down, 1 up, 2 key
 *                                                                    down, 3 up (id: B_* / key 0..26), 4 an encoder
 *                                                                    turned (id EN_*, v + 64 detents), 5 all let go
 * From 0.13 PARAMS ends with SUB's values (dn_sub.h SP_*, 27 of them).
 * From 0.11 a step in the pattern is 8 bytes {note, flags, len, vel, cond, rtim, rlen, rvel}: dn_seq.h.
 * From 0.9 PARAMS also has the LFOs, 8 each after BREAKS B: SHAPE SYNC RATE DEST DEPTH PHASE MODE SMOOTH (SYNC
 * before RATE: setting SYNC resets RATE).
 */
enum { ED_STATE = 60, ED_PARAMS, ED_PARAM_SET, ED_PAT_READ, ED_PAT_WRITE, ED_PAT_COMMIT, ED_TRANSPORT, ED_SAVE,
       ED_PRESETN, ED_NAMES, ED_LFO_PTS_GET, ED_LFO_PTS_SET, ED_LFO_NOW, ED_PATS, ED_PAT_SEL, ED_PROJ, ED_PROJ_SET,
       ED_PROJ_NAME, ED_SUB_PRESET, ED_PARAMS_HI, ED_SMP_GET, ED_SMP_SET, ED_SMP_PLAY, ED_FM_GET, ED_FM_SET,
       ED_FM_LOAD, ED_FM_PLAY, ED_UI_STATE, ED_UI_INPUT, ED_CHORDS, ED_WT_GET, ED_WT_SET, ED_WT_LOAD, ED_WT_PLAY,
       ED_JU_GET, ED_JU_SET, ED_JU_LOAD, ED_JU_PLAY };
#define DN_ED_NHI 8u

static const uint8_t DN_ED_BRK[8] = {DN_PX_BSRC, DN_PX_BPITCH, DN_PX_BDECAY, DN_PX_BCRUSH,
                                     DN_PX_BCHOKE, DN_PX_BSMODE, DN_PX_BSTRETCH, DN_PX_BLEVEL};
static const uint8_t DN_ED_LFO[8] = {DN_PX_LSHAPE, DN_PX_LSYNC, DN_PX_LRATE, DN_PX_LDEST, DN_PX_LDEPTH, DN_PX_LPHASE,
                                     DN_PX_LMODE, DN_PX_LSMOOTH};
#define DN_ED_NBASE (RP_COUNT + 1u + 2u * 8u)       /* (0.7 / 0.8: these) */
#define DN_ED_NPARAM_09 (DN_ED_NBASE + DN_NLFO * 8u)
#define DN_ED_NPARAM_13 (DN_ED_NPARAM_09 + SP_COUNT) /* (0.13: SUB's after the LFOs) */
#define DN_ED_NPARAM (DN_ED_NPARAM_13 + DN_NLFO * 8u) /* (0.15: SUB's own LFOs; those before are REESE's) */
/* the editor frames the device glue must take whole (not into its 64-byte copy) */
static int dn_ed_long(uint32_t cmd) { return cmd >= 60u && cmd <= 89u; }
static void dn_ed_str(void (*put)(uint32_t), const char *t, uint32_t max)
{
    uint32_t i;
    for (i = 0; t && t[i] && i < max; i++)
        put((uint8_t)t[i] & 0x7Fu);
    put(0);
}
/* SAMPLER's values by kind (0 pad, 1 zone, 2 keys, 3 mode): the app's id for value k, and how many */
static uint32_t dn_ed_smp_n(uint32_t kind) { return kind == 0u ? PD_N : kind == 1u ? ZN_N : kind == 2u ? KY_N : 1u; }
static uint32_t dn_ed_smp_id(uint32_t kind, uint32_t k)
{
    return kind == 0u ? DN_PX_PD0 + k : kind == 1u ? DN_PX_ZN0 + k : kind == 2u ? DN_PX_KY0 + k : DN_PX_SMODE;
}
static dn_pattern_t dn_ed_pat X0X_POOL;             /* PAT_WRITE's copy, until PAT_COMMIT */

/* editor parameter i: the app's id, and which BREAKS (0 / 1; -1 none) */
static uint32_t dn_ed_id(uint32_t i, int32_t *brk)
{
    *brk = -1;
    if (i >= DN_ED_NPARAM_13) {                     /* SUB's LFOs (brk: 20 + which) */
        i -= DN_ED_NPARAM_13;
        *brk = 20 + (int32_t)(i / 8u);
        return DN_ED_LFO[i % 8u];
    }
    if (i >= DN_ED_NPARAM_09)
        return DN_PX_SUB0 + (i - DN_ED_NPARAM_09);
    if (i >= DN_ED_NBASE) {                         /* an LFO's (brk: 10 + which LFO) */
        i -= DN_ED_NBASE;
        *brk = 10 + (int32_t)(i / 8u);
        return DN_ED_LFO[i % 8u];
    }
    if (i < RP_COUNT)
        return i;
    if (i == RP_COUNT)
        return DN_PX_BPM;
    i -= RP_COUNT + 1u;
    *brk = (int32_t)(i / 8u);
    return DN_ED_BRK[i % 8u];
}
static int32_t dn_ed_get(uint32_t i)
{
    int32_t brk, v;
    uint32_t id = dn_ed_id(i, &brk);
    uint8_t ls = dn.lfo_sel;
    if (brk >= 10)
        dn.lfo_sel = (uint8_t)(brk % 10), dn.lfo_force = (uint8_t)(brk / 10);   /* (that synth's LFO, whatever
                                                     * track is selected) */
    else if (brk >= 0)
        dn.bk_force = (uint8_t)(brk + 1);           /* (the BRK values read BREAKS A / B, whichever track it's on) */
    v = dn_get(id);
    dn.bk_force = 0;
    dn.lfo_force = 0;
    dn.lfo_sel = ls;
    return v;
}
static int dn_ed_set(uint32_t i, int32_t v)
{
    int32_t brk;
    uint32_t id;
    uint8_t ls = dn.lfo_sel;
    if (i >= DN_ED_NPARAM)
        return 1;
    id = dn_ed_id(i, &brk);
    if (brk >= 10)
        dn.lfo_sel = (uint8_t)(brk % 10), dn.lfo_force = (uint8_t)(brk / 10);
    else if (brk >= 0)
        dn.bk_force = (uint8_t)(brk + 1);
    dn_set(id, v);
    dn.bk_force = 0;
    dn.lfo_force = 0;
    dn.lfo_sel = ls;
    return 0;
}

static void dn_ed_num(void (*put)(uint32_t), uint32_t v, uint32_t digits)
{
    while (digits--) {
        put(v & 0x7Fu);
        v >>= 7;
    }
}

int dn_ed_cmd(uint32_t cmd, const uint8_t *a, uint32_t na, void (*put)(uint32_t))
{
    uint32_t i, j;
    switch (cmd) {
    case ED_STATE:
        dn_ed_num(put, dn_rev, 5);
        dn_ed_num(put, dn_sq.bpm, 2);
        put(dn_sq.playing);
        put(dn.rec);
        put(dn.preset);
        put(dn.trk);
        put(dn_sq.pat.swing);
        put(dn.dirty);
        put(dn_bk[0].nslices);
        put(dn_bk[1].nslices);
        put(reese_preset_count() + DN_NUSER);    /* (0.15: the factory sounds, then USER 1-8) */
        put(dn_src_count());
        dn_ed_num(put, sizeof(dn_pattern_t), 2);
        put(DN_ED_NPARAM);
        dn_ed_num(put, dn_sq.playing ? dn_sq.pos : 0u, 2);
        return 1;
    case ED_PARAMS:
        put(DN_ED_NPARAM);
        for (i = 0; i < DN_ED_NPARAM; i++)
            dn_ed_num(put, (uint32_t)(dn_ed_get(i) + 8192), 2);
        return 1;
    case ED_PARAM_SET:
        if (na < 3u)
            return 0;
        put((uint32_t)dn_ed_set(a[0], (int32_t)(a[1] | a[2] << 7) - 8192));
        return 1;
    case ED_PAT_READ: {
        const uint8_t *src = (const uint8_t *)&dn_sq.pat;
        uint32_t off, n;
        if (na < 3u)
            return 0;
        off = a[0] | (uint32_t)a[1] << 7;
        n = a[2] > 200u ? 200u : a[2];
        if (off >= sizeof(dn_pattern_t))
            return 0;
        if (n > sizeof(dn_pattern_t) - off)
            n = sizeof(dn_pattern_t) - off;
        dn_ed_num(put, off, 2);
        for (i = 0; i < n; i += 7) {
            uint32_t m = 0;
            for (j = 0; j < 7u && i + j < n; j++)
                m |= (uint32_t)(src[off + i + j] >> 7) << j;
            put(m);
            for (j = 0; j < 7u && i + j < n; j++)
                put(src[off + i + j] & 0x7Fu);
        }
        return 1;
    }
    case ED_PAT_WRITE: {
        uint8_t *dst = (uint8_t *)&dn_ed_pat;
        uint32_t off, k = 0;
        if (na < 2u)
            return 0;
        off = a[0] | (uint32_t)a[1] << 7;
        a += 2;
        na -= 2u;
        while (na) {                                /* 7 to 8, as above */
            uint32_t m = *a++;
            na--;
            for (j = 0; j < 7u && na; j++, na--, k++)
                if (off + k < sizeof(dn_pattern_t))
                    dst[off + k] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
                else
                    a++;
        }
        put(off + k <= sizeof(dn_pattern_t) ? 0u : 1u);
        return 1;
    }
    case ED_PAT_COMMIT:
        if (!dn_pattern_valid(&dn_ed_pat)) {        /* (checks, clamps, brings an older one up to date) */
            put(1);
            return 1;
        }
        dn_lock();
        dn_sq.pat = dn_ed_pat;
        dn_unlock();
        dn_mark();
        put(0);
        return 1;
    case ED_TRANSPORT:
        if (na < 1u)
            return 0;
        dn_lock();
        if (a[0] && !dn_sq.playing) {
            dn.clk_last = 0;
            dn_seq_start(&dn_sq, &DN_SQ_CB);
        }
        else if (!a[0] && dn_sq.playing) {
            dn_seq_stop(&dn_sq, &DN_SQ_CB);
            for (i = 0; i < DN_NBRK; i++)
                dn_brk_cut(&dn_bk[i]);
        }
        dn_unlock();
        dn.redraw |= DN_RD_ALL;
        put(dn_sq.playing);
        return 1;
    case ED_LFO_PTS_GET: {
        const dn_lfo_p_t *l;
        if (na < 1u || a[0] >= DN_NLFO_ALL)
            return 0;
        l = &dn_lp[a[0]];
        put(a[0]);
        put(l->npts);
        for (i = 0; i < DN_LFO_PTS; i++)
            dn_ed_num(put, (uint32_t)(l->pts[i] + 100), 2);
        return 1;
    }
    case ED_LFO_PTS_SET: {
        dn_lfo_p_t *l;
        if (na < 2u + 2u * DN_LFO_PTS || a[0] >= DN_NLFO_ALL) {
            put(1);
            return 1;
        }
        l = &dn_lp[a[0]];
        dn_lock();
        l->npts = a[1] == 32u ? 32u : 16u;
        l->pad[0] = 1;                              /* (drawn: SHAPE to DRAW keeps these points) */
        for (i = 0; i < DN_LFO_PTS; i++)
            l->pts[i] = (int8_t)dn_clamp((int32_t)(a[2u + 2u * i] | a[3u + 2u * i] << 7) - 100, -100, 100);
        dn_unlock();
        dn_mark();
        put(0);
        return 1;
    }
    case ED_PATS:
        for (i = 0; i < DN_SEQ_TRACKS; i++) {
            put(dn_pcur[i]);
            put(dn_pq[i] == 0xFF ? 127u : dn_pq[i]);
            dn_ed_num(put, dn_pat_used(i), 2);
        }
        dn_ed_num(put, dn_pat_notes(), 2);
        dn_ed_num(put, dn_pat_room(), 2);
        return 1;
    case ED_PAT_SEL:
        if (na < 2u || a[0] >= DN_SEQ_TRACKS || a[1] >= DN_NPAT)
            return 0;
        dn_pat_select(a[0], a[1]);
        dn_rev++;
        put(0);
        return 1;
    case ED_LFO_NOW:
        for (i = 0; i < DN_NLFO_ALL; i++) {         /* (0.15: REESE's 4, then SUB's 4) */
            dn_ed_num(put, dn_ls[i].ph >> 18, 2);
            dn_ed_num(put, (uint32_t)((dn_ls[i].out >> 2) + 8192), 2);
        }
        return 1;
    case ED_PROJ: {
        uint32_t k;
        for (i = 0; i < 12u; i++)
            put(i < sizeof dn_pname ? (uint8_t)dn_pname[i] & 0x7Fu : 0u);
        for (i = 0; i < DN_SEQ_TRACKS; i++)
            put(dn_eng[i]);
        for (k = 0; k < 2u; k++)
            for (i = 0; i < DN_SEQ_TRACKS; i++)
                put(dn_send[k][i]);
        put(dn_ret[0]);
        put(dn_ret[1]);
        for (k = 0; k < 2u; k++) {
            put(dn_fxp[k].type);
            for (i = 0; i < 3u; i++)
                put(dn_fxp[k].p[i] & 0x7Fu);
        }
        dn_ed_num(put, dn_smp_used(), 3);
        dn_ed_num(put, dn_smp_cap(), 3);
        dn_ed_num(put, (dn_fxs[0].bytes + dn_fxs[1].bytes) / 1024u, 2);
        dn_ed_num(put, DN_FX_ARENA / 1024u, 2);
        put(plat_cpu_pct() > 127u ? 127u : plat_cpu_pct());
        put(DN_E_N);
        put(DN_FX_N);
        for (k = 0; k < 2u; k++)                    /* (0.16: each effect's 4th..7th values) */
            for (i = 3; i < DN_FX_NP; i++)
                put(dn_fxp[k].p[i] & 0x7Fu);
        return 1;
    }
    case ED_PROJ_SET: {
        uint32_t f, t;
        int32_t v;
        uint8_t trk = dn.trk;
        if (na < 3u)
            return 0;
        f = a[0];
        v = (int32_t)(a[1] | a[2] << 7);
        if (f < 8u) {
            dn_eng_set(f, (uint32_t)v);
            put(dn_eng[f] == (uint32_t)v ? 0u : 1u);
            return 1;
        }
        if (f >= 42u) {
            put(1);
            return 1;
        }
        t = f < 24u ? (f - 8u) % 8u : 0u;
        dn.trk = (uint8_t)t;                        /* (the sends are the selected track's) */
        {
            uint32_t id = f < 16u ? DN_PX_SENDA : f < 24u ? DN_PX_SENDB : f < 26u ? DN_PX_RETA + (f - 24u)
                        : f < 34u ? DN_PX_FXA + (f - 26u) : f < 38u ? DN_PX_FXA2 + (f - 34u) : DN_PX_FXB2 + (f - 38u);
            dn.fx_exact = 1;
            dn_set(id, v);
            dn.fx_exact = 0;
            put(dn_get(id) == dn_clamp(v, dn_min(id), dn_max(id)) ? 0u : 1u);
        }
        dn.trk = trk;
        dn.redraw |= DN_RD_ALL;
        return 1;
    }
    case ED_PARAMS_HI:
        if (!na) {
            put(DN_ED_NHI);
            for (i = 0; i < DN_ED_NHI; i++)
                dn_ed_num(put, (uint32_t)(dn_get(DN_PX_RFE + i) + 8192), 2);
            return 1;
        }
        if (na < 3u || a[0] >= DN_ED_NHI)
            return 0;
        dn_set(DN_PX_RFE + a[0], (int32_t)(a[1] | a[2] << 7) - 8192);
        dn_rev++;
        put(0);
        return 1;
    case ED_SMP_GET:
    case ED_SMP_SET: {
        uint32_t kind, idx, n, k;
        uint8_t pad = dn.smp_pad, zone = dn.smp_zone;
        if (na < 2u || a[0] > 7u || (a[0] % 4u == 0u && a[1] >= DN_SMP_PADS) || (a[0] % 4u == 1u && a[1] >= DN_SMP_ZONES))
            return 0;
        dn_smc = a[0] >= 4u;                        /* (0.36: kinds 4-7 are SAMPLER 2's 0-3) */
        kind = a[0] % 4u;
        idx = a[1];
        n = dn_ed_smp_n(kind);
        if (kind == 0u)
            dn.smp_pad = (uint8_t)idx;              /* (the values read / set are the pad's / zone's shown) */
        else if (kind == 1u)
            dn.smp_zone = (uint8_t)idx;
        if (cmd == ED_SMP_GET) {
            put(a[0]);
            put(idx);
            put(n);
            for (k = 0; k < n; k++)
                dn_ed_num(put, (uint32_t)(dn_get(dn_ed_smp_id(kind, k)) + 8192), 2);
        } else if (na < 5u || a[2] >= n) {
            put(1);
        } else {
            dn_set(dn_ed_smp_id(kind, a[2]), (int32_t)(a[3] | a[4] << 7) - 8192);
            dn_rev++;
            put(0);
        }
        dn.smp_pad = pad;
        dn.smp_zone = zone;
        dn_smc_sync();
        return 1;
    }
    case ED_SMP_PLAY:
        if (na < 2u)
            return 0;
        dn_smc = na >= 3u && a[2];                  /* (0.36: a third byte 1: SAMPLER 2) */
        dn_lock();
        if (dn_sm.p.mode == DN_SMP_ONESHOT) {
            if (a[1])
                dn_smp_pad(&dn_sm, a[0] % DN_SMP_PADS, 100);
        } else if (a[1]) {
            dn_smp_note_on(&dn_sm, a[0], 100);
        } else {
            dn_smp_note_off(&dn_sm, a[0]);
        }
        dn_unlock();
        dn_smc_sync();
        put(0);
        return 1;
    case ED_FM_GET:
        for (i = 0; i < DN_FM_PATCH; i++)
            put(dn_fm_base[i] & 0x7Fu);
        put((uint32_t)dn_fmx.level & 0x7Fu);
        put(dn_fmx.voices);
        put((uint32_t)(dn_fm_mac.bright + 64));
        put((uint32_t)(dn_fm_mac.time + 64));
        put((uint32_t)(dn_fm_mac.attack + 64));
        put((uint32_t)(dn_fm_mac.release + 64));
        dn_ed_num(put, dn.fm_preset, 2);
        dn_ed_num(put, dn_fm_presets(), 2);
        return 1;
    case ED_FM_SET: {
        uint32_t k;
        int32_t v;
        if (na < 4u)
            return 0;
        k = a[0] | (uint32_t)a[1] << 7;
        v = (int32_t)(a[2] | a[3] << 7) - 8192;
        if (k < DN_FM_PATCH) {
            if (k == DN_FMV_OPSW)
                v &= 0x3f;
            else if (k >= DN_FMV_NAME)
                v = v < 32 ? 32 : v > 126 ? 126 : v;
            else
                v = dn_clamp(v, 0, dn_fm_vmax(k));
            dn_fm_base[k] = (uint8_t)v;
            dn_fm_sync();
            dn.dirty = 1;
        } else if (k == 156u) {
            dn_fm_set(DN_PX_FLEVEL, dn_clamp(v, 0, 127));
        } else if (k == 157u) {
            dn_fm_set(DN_PX_FVOICES, dn_clamp(v, 1, DN_FM_NV));
        } else if (k <= 161u) {
            static const uint8_t MAC[4] = {DN_PX_FBRIGHT, DN_PX_FTIME, DN_PX_FATK, DN_PX_FREL};
            dn_fm_set(MAC[k - 158u], dn_clamp(v, -50, 50));
        } else {
            put(1);
            return 1;
        }
        dn_rev++;
        dn.redraw |= DN_RD_ALL;
        put(0);
        return 1;
    }
    case ED_FM_LOAD:
        if (na >= DN_FM_PATCH) {                    /* a whole voice */
            for (i = 0; i < DN_FM_PATCH; i++)
                dn_fm_base[i] = (uint8_t)(i == DN_FMV_OPSW ? a[i] & 0x3fu : i >= DN_FMV_NAME ? a[i]
                                          : (uint32_t)dn_clamp(a[i], 0, dn_fm_vmax(i)));
            dn_fm_sync();
            dn.dirty = 1;
        } else if (na >= 2u && (uint32_t)(a[0] | a[1] << 7) < dn_fm_presets()) {
            dn_fm_load(a[0] | (uint32_t)a[1] << 7);
        } else {
            put(1);
            return 1;
        }
        dn_rev++;
        dn.redraw |= DN_RD_ALL;
        put(0);
        return 1;
    case ED_FM_PLAY:
        if (na < 2u)
            return 0;
        dn_lock();
        if (a[1])
            dn_fm_note_on(&dn_fmx, a[0], 100);
        else
            dn_fm_note_off(&dn_fmx, a[0]);
        dn_unlock();
        put(0);
        return 1;
    case ED_UI_STATE: {
        const char *pn, *tag;
        uint8_t ids[DN_MAXTABS];
        const char *nms[DN_MAXTABS];
        uint32_t n = 0, sel = 127u, k, seq = DN_IS_SEQPG(dn.page) != 0;
        char st[40];
        put(dn.page);
        put(dn.trk);
        put(dn_eng[dn.trk % DN_SEQ_TRACKS]);
        put((dn_sq.playing ? 1u : 0u) | (dn.rec ? 2u : 0u) | (dn.dirty ? 4u : 0u) | (dn.naming ? 8u : 0u) |
            (dn.play ? 16u : 0u) | (seq ? 32u : 0u) | (dn.step_held >= 0 ? 64u : 0u));
        put((uint32_t)(dn.oct + 8));
        dn_ed_num(put, dn_sq.bpm, 2);
        dn_ed_num(put, dn_sq.playing ? dn_sq.pos : 0u, 2);
        if (dn.naming) {                            /* (the name editor: its letters as the name) */
            static char nm[13];
            for (k = 0; k < dn.nm_len && k < 12u; k++)
                nm[k] = dn.nm[k];
            nm[k] = 0;
            tag = "NAME";
            pn = nm;
        } else {
            tag = dn_top_names(&pn);
        }
        dn_ed_str(put, tag, 8);
        dn_ed_str(put, pn, 20);
        if (!seq && !dn.naming)
            n = dn_tabs(ids, nms);
        for (k = 0; k < n; k++)
            if (ids[k] == dn.page)
                sel = k;
        put(n);
        put(sel);
        for (k = 0; k < n; k++)
            dn_ed_str(put, nms[k], 8);
        for (k = 0; k < 4u; k++) {
            uint32_t id = seq || dn.naming || (dn.page == DN_PG_LFO && dn.lfo_view == 2u) ? DN_PX_NONE : dn_dial(k);
            char tx[12];
            if (id == DN_PX_NONE) {
                put(0);
                continue;
            }
            put(1);
            dn_ed_num(put, id, 2);
            dn_ed_str(put, dn_name(id), 8);
            dn_ed_num(put, (uint32_t)(dn_get(id) + 8192), 2);
            dn_ed_num(put, (uint32_t)(dn_min(id) + 8192), 2);
            dn_ed_num(put, (uint32_t)(dn_max(id) + 8192), 2);
            dn_text(id, dn_get(id), tx);
            dn_ed_str(put, tx, 8);
        }
        dn_ed_str(put, dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0 ? dn.msg : "", 24);
        st[0] = 0;
        if (dn.page == DN_PG_PROJ) {
            char v[8];
            dn_cat(st, "CPU ");
            dn_num(v, (int32_t)plat_cpu_pct());
            dn_cat(st, v);
            dn_cat(st, "% PK ");
            dn_num(v, (int32_t)plat_cpu_peak_pct());
            dn_cat(st, v);
            dn_cat(st, "%");
        }
        dn_ed_str(put, st, 24);
        dn_ed_num(put, dn_lit_btn, 2);
        dn_ed_num(put, dn_lit_keys, 4);
        put(dn.nm_cur);
        return 1;
    }
    case ED_UI_INPUT: {
        uint32_t kind, id;
        if (na < 2u)
            return 0;
        kind = a[0];
        id = a[1];
        if (kind == 0u && id < NB)
            dn_vbtn |= 1u << id;
        else if (kind == 1u && id < NB)
            dn_vbtn &= ~(1u << id);
        else if (kind == 2u && id < NKEYS)
            dn_vkeys |= 1u << id;
        else if (kind == 3u && id < NKEYS)
            dn_vkeys &= ~(1u << id);
        else if (kind == 4u && id < NE && na >= 3u)
            dn_venc[id] += (int32_t)a[2] - 64;
        else if (kind == 5u)
            dn_vbtn = 0, dn_vkeys = 0;
        else {
            put(1);
            return 1;
        }
        put(0);
        return 1;
    }
    case ED_WT_GET:                                 /* (0.32) WAVE */
        put(WP_N);
        for (i = 0; i < WP_N; i++)
            dn_ed_num(put, (uint32_t)(dn_wv.p[i] + 8192), 2);
        dn_ed_num(put, dn.wv_preset, 2);
        dn_ed_num(put, dn_wt_preset_count(), 2);
        dn_ed_num(put, dn_wv_ntab(), 2);
        dn_ed_num(put, DN_WT_FACTORY, 2);
        return 1;
    case ED_WT_SET: {
        int32_t v;
        if (na < 3u)
            return 0;
        if (a[0] >= WP_N) {
            put(1);
            return 1;
        }
        v = (int32_t)(a[1] | a[2] << 7) - 8192;
        dn_set(DN_W(a[0]), v);
        put(dn_get(DN_W(a[0])) == dn_clamp(v, dn_min(DN_W(a[0])), dn_max(DN_W(a[0]))) ? 0u : 1u);
        return 1;
    }
    case ED_WT_LOAD:
        if (na < 2u || (uint32_t)(a[0] | a[1] << 7) >= dn_wt_preset_count()) {
            put(1);
            return 1;
        }
        dn_wv_load(a[0] | (uint32_t)a[1] << 7);
        dn_rev++;
        put(0);
        return 1;
    case ED_WT_PLAY:
        if (na < 2u)
            return 0;
        dn_lock();
        if (a[1])
            dn_wt_note_on(&dn_wv, a[0], 100);
        else
            dn_wt_note_off(&dn_wv, a[0]);
        dn_unlock();
        put(0);
        return 1;
    case ED_JU_GET:                                 /* (0.36) JUNO */
        put(JP_N);
        for (i = 0; i < JP_N; i++)
            dn_ed_num(put, (uint32_t)(dn_ju.p[i] + 8192), 2);
        dn_ed_num(put, dn.ju_preset, 2);
        dn_ed_num(put, dn_ju_preset_count(), 2);
        return 1;
    case ED_JU_SET: {
        int32_t v;
        if (na < 3u)
            return 0;
        if (a[0] >= JP_N) {
            put(1);
            return 1;
        }
        v = (int32_t)(a[1] | a[2] << 7) - 8192;
        dn_set(DN_J(a[0]), v);
        put(dn_get(DN_J(a[0])) == dn_clamp(v, dn_min(DN_J(a[0])), dn_max(DN_J(a[0]))) ? 0u : 1u);
        return 1;
    }
    case ED_JU_LOAD:
        if (na < 2u || (uint32_t)(a[0] | a[1] << 7) >= dn_ju_preset_count()) {
            put(1);
            return 1;
        }
        dn_ju_load(a[0] | (uint32_t)a[1] << 7);
        dn_rev++;
        put(0);
        return 1;
    case ED_JU_PLAY:
        if (na < 2u)
            return 0;
        dn_lock();
        if (a[1])
            dn_ju_note_on(&dn_ju, a[0], 100);
        else
            dn_ju_note_off(&dn_ju, a[0]);
        dn_unlock();
        put(0);
        return 1;
    case ED_CHORDS:                                 /* (0.28) the 15 chords, fixed: each its notes over the root */
        for (i = 0; i < DN_CHORDS; i++)
            for (j = 0; j < DN_CHORD_N; j++)
                put((uint32_t)(dn_chord[i][j] + 64));
        return 1;
    case ED_SUB_PRESET:
        if (na < 1u || a[0] >= dn_sub_preset_count() + DN_NUSER)
            return 0;
        dn_sub_preset(a[0]);
        put(0);
        return 1;
    case ED_PROJ_NAME:
        for (i = 0; i + 1u < sizeof dn_pname; i++)
            dn_pname[i] = i < na && a[i] >= 32u && a[i] < 127u ? (char)a[i] : 0;
        dn_pname[sizeof dn_pname - 1u] = 0;
        if (!dn_pname[0])
            dn_pname[0] = 'P', dn_pname[1] = '1', dn_pname[2] = 0;
        dn_mark();
        put(0);
        return 1;
    case ED_SAVE:
        dn_save();
        put(dn.dirty);
        return 1;
    case ED_PRESETN:
        if (na < 1u || a[0] >= reese_preset_count() + DN_NUSER)
            return 0;
        dn_load_preset(a[0]);
        dn_rev++;
        put(0);
        return 1;
    case ED_NAMES: {
        uint32_t kind, from, count, used = 0;
        if (na < 2u)
            return 0;
        kind = a[0];
        from = a[1];
        count = kind == 1u ? dn_src_count() : kind == 2u ? DN_E_N : kind == 3u ? DN_FX_N : kind == 4u ? DN_FX_NDIV
              : kind == 5u ? dn_sub_preset_count() + DN_NUSER : kind == 6u ? dn_fm_presets()
              : kind == 7u ? dn_wv_ntab() : kind == 8u ? dn_wt_preset_count() : kind == 9u ? dn_ju_preset_count()
              : reese_preset_count() + DN_NUSER;
        put(count);
        put(from);
        for (i = from; i < count && used < 180u; i++) {
            char nm[12];
            const char *s;
            if (kind == 7u) {                       /* (0.32: WAVE's tables, its factory sounds) */
                dn_wt_name(dn_wv_tab(i), nm);
                s = nm;
            } else if (kind == 8u) {
                s = dn_wt_preset_name(i);
            } else if (kind == 9u) {
                s = dn_ju_preset_name(i);
            } else if (kind == 6u) {                /* (FM: a factory voice's name, or a project voice's 10) */
                if (i < dn_fm_factory_count()) {
                    s = dn_fm_factory_name(i);
                } else {
                    const uint8_t *pk = dn_fmv_get(i - dn_fm_factory_count());
                    for (j = 0; j < 10u; j++)
                        nm[j] = pk && pk[118u + j] >= 32u && pk[118u + j] < 127u ? (char)pk[118u + j] : ' ';
                    for (j = 10u; j > 0u && nm[j - 1u] == ' '; j--)
                        ;
                    nm[j] = 0;
                    s = nm;
                }
            } else if (kind >= 2u) {
                s = kind == 2u ? DN_E_NAME[i] : kind == 3u ? DN_FX_NAME[i] : kind == 4u ? DN_FX_DIV_NAME[i]
                  : dn_sound_name(1, i);
            } else if (kind) {
                dn_loop_t L;
                nm[0] = 0;
                if (dn_src_get(i, &L))
                    for (j = 0; j < 9u; j++)
                        nm[j] = L.name[j];
                nm[9] = 0;
                s = nm;
            } else {
                s = dn_sound_name(0, i);
            }
            for (j = 0; s[j] && j < 12u; j++, used++)
                put((uint8_t)s[j] & 0x7Fu);
            put(0);
            used++;
        }
        return 1;
    }
    default:
        return 0;
    }
}
