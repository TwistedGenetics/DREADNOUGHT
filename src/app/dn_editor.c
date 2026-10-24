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
 *                                 FX memory KB (2), cpu %, kinds of instrument, kinds of FX   (0.12)
 *   76 PROJ_SET field v(2)     -> rc (0 set; 1 refused: no memory for that FX, or a bad field)
 *                                 fields: 0-7 track n's instrument (REESE / BRK A / BRK B swap), 8-15 send A,
 *                                 16-23 send B, 24-25 returns, 26-29 FX A type p0 p1 p2, 30-33 FX B's
 *   77 PROJ_NAME chars...      -> rc (0)                             up to 11 characters, 32-126
 * NAMES kinds from 0.12: 2 the instruments, 3 the effects, 4 the delay's times; 5 SUB's factory sounds (0.13).
 *   78 SUB_PRESET i            -> rc                                 SUB's factory sound i (0.13)
 * From 0.13 PARAMS ends with SUB's values (dn_sub.h SP_*, 27 of them).
 * From 0.11 a step in the pattern is 8 bytes {note, flags, len, vel, cond, rtim, rlen, rvel}: dn_seq.h.
 * From 0.9 PARAMS also has the LFOs, 8 each after BREAKS B: SHAPE SYNC RATE DEST DEPTH PHASE MODE SMOOTH (SYNC
 * before RATE: setting SYNC resets RATE).
 */
enum { ED_STATE = 60, ED_PARAMS, ED_PARAM_SET, ED_PAT_READ, ED_PAT_WRITE, ED_PAT_COMMIT, ED_TRANSPORT, ED_SAVE,
       ED_PRESETN, ED_NAMES, ED_LFO_PTS_GET, ED_LFO_PTS_SET, ED_LFO_NOW, ED_PATS, ED_PAT_SEL, ED_PROJ, ED_PROJ_SET,
       ED_PROJ_NAME, ED_SUB_PRESET };

static const uint8_t DN_ED_BRK[8] = {DN_PX_BSRC, DN_PX_BPITCH, DN_PX_BDECAY, DN_PX_BCRUSH,
                                     DN_PX_BCHOKE, DN_PX_BSMODE, DN_PX_BSTRETCH, DN_PX_BLEVEL};
static const uint8_t DN_ED_LFO[8] = {DN_PX_LSHAPE, DN_PX_LSYNC, DN_PX_LRATE, DN_PX_LDEST, DN_PX_LDEPTH, DN_PX_LPHASE,
                                     DN_PX_LMODE, DN_PX_LSMOOTH};
#define DN_ED_NBASE (RP_COUNT + 1u + 2u * 8u)       /* (0.7 / 0.8: these) */
#define DN_ED_NPARAM_09 (DN_ED_NBASE + DN_NLFO * 8u)
#define DN_ED_NPARAM_13 (DN_ED_NPARAM_09 + SP_COUNT) /* (0.13: SUB's after the LFOs) */
#define DN_ED_NPARAM (DN_ED_NPARAM_13 + DN_NLFO * 8u) /* (0.15: SUB's own LFOs; those before are REESE's) */
/* the editor frames the device glue must take whole (not into its 64-byte copy) */
static int dn_ed_long(uint32_t cmd) { return cmd >= 60u && cmd <= 79u; }
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
        dn_ed_num(put, (dn_fx_ram(dn_fxp[0].type) + dn_fx_ram(dn_fxp[1].type)) / 1024u, 2);
        dn_ed_num(put, DN_FX_ARENA / 1024u, 2);
        put(plat_cpu_pct() > 127u ? 127u : plat_cpu_pct());
        put(DN_E_N);
        put(DN_FX_N);
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
        if (f >= 34u) {
            put(1);
            return 1;
        }
        t = f < 24u ? (f - 8u) % 8u : 0u;
        dn.trk = (uint8_t)t;                        /* (the sends are the selected track's) */
        {
            uint32_t id = f < 16u ? DN_PX_SENDA : f < 24u ? DN_PX_SENDB : f < 26u ? DN_PX_RETA + (f - 24u)
                        : DN_PX_FXA + (f - 26u);
            dn_set(id, v);
            put(dn_get(id) == dn_clamp(v, dn_min(id), dn_max(id)) ? 0u : 1u);
        }
        dn.trk = trk;
        dn.redraw |= DN_RD_ALL;
        return 1;
    }
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
              : kind == 5u ? dn_sub_preset_count() + DN_NUSER : reese_preset_count() + DN_NUSER;
        put(count);
        put(from);
        for (i = from; i < count && used < 180u; i++) {
            char nm[10];
            const char *s;
            if (kind >= 2u) {
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
