/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT plugin core (0.45): the GUI's API, a unity-build fragment included by dn_core.c after the app. The
 * plugin's own GUI edits the instruments with the mouse through this, not through the FM-1's panel: the tracks and
 * their instruments, every value of every instrument (the FM-1's own, with its names, ranges and texts: dn_get /
 * dn_set / dn_min / dn_max / dn_name / dn_text) grouped into sections as the FM-1's pages group them, the presets,
 * the patterns, the samples and the breaks' slices. Some values belong to a pad, a zone, an operator, an LFO or a
 * matrix slot: the caller says which (sel, idx) and the app's selection is set to it for the call.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */

/* ------------------------------------------------------------------------------------------------- tracks --- */
int dnc_engine_count(void) { return DN_E_N; }
const char *dnc_engine_name(int e) { return e >= 0 && e < (int)DN_E_N ? DN_E_NAME[e] : ""; }
int dnc_track_engine(int t) { return dn_eng[(uint32_t)t % DN_SEQ_TRACKS]; }
const char *dnc_track_name(int t) { return dn_inst_name((uint32_t)t % DN_SEQ_TRACKS); }
int dnc_track(void) { return dn.trk; }
void dnc_track_select(int t)
{
    t = (int)((uint32_t)t % DN_SEQ_TRACKS);
    dn.trk = (uint8_t)t;
    dn_smc_sync();
    dn.peng = 0xFF;
    if (DN_IS_MIDI(t))
        dn.midi_sel = (uint8_t)dn_midi_idx((uint32_t)t);
    if (DN_IS_BRK(t))
        dn.bk_last = (uint8_t)DN_BK(t);
    if (DN_IS_SYN(t))
        dn.syn_last = dn_eng[t];
}
void dnc_track_set_engine(int t, int e)
{
    dn_eng_set((uint32_t)t % DN_SEQ_TRACKS, (uint32_t)e);
    dnc_track_select(dn.trk);
}
int dnc_track_mute(int t) { return dn_sq.pat.tr[(uint32_t)t % DN_SEQ_TRACKS].mute != 0; }
void dnc_track_set_mute(int t, int m)
{
    dn_sq.pat.tr[(uint32_t)t % DN_SEQ_TRACKS].mute = (uint8_t)(m != 0);
    if (m)
        dn_trk_silence((uint32_t)t % DN_SEQ_TRACKS);
    dn_mark();
}
uint32_t dnc_track_colour(int t)                    /* the instrument's colour (BLOCKS), 0xRRGGBB */
{
    uint16_t c = DN_E_COL[dn_eng[(uint32_t)t % DN_SEQ_TRACKS] % DN_E_N];
    return ((uint32_t)((c >> 11) & 31u) * 255u / 31u) << 16 | ((uint32_t)((c >> 5) & 63u) * 255u / 63u) << 8 |
           (uint32_t)(c & 31u) * 255u / 31u;
}
/* a note from the GUI (a pad clicked, an audition) on track t, as the sequencer plays it */
void dnc_audition(int t, int note, int vel, int on)
{
    t = (int)((uint32_t)t % DN_SEQ_TRACKS);
    if (note < 0 || note > 127)
        return;
    if (on)
        dn_sq_on1((uint32_t)t, (uint8_t)note, (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel), 0, 0);
    else
        dn_sq_off1((uint32_t)t, (uint8_t)note);
}

/* ------------------------------------------------------------------------------------------------ values --- */
static uint32_t dnc_ctx(uint32_t id, int sel, int idx)   /* the app's selection set for (sel, idx); the id to use */
{
    if (idx < 0)
        idx = 0;
    switch (sel) {
    case DNC_SEL_PAD: dn.smp_pad = (uint8_t)((uint32_t)idx % DN_SMP_PADS); break;
    case DNC_SEL_ZONE: dn.smp_zone = (uint8_t)((uint32_t)idx % DN_SMP_ZONES); break;
    case DNC_SEL_OP: dn.fm_op = (uint8_t)((uint32_t)idx % 6u); break;
    case DNC_SEL_LFO: dn.lfo_sel = (uint8_t)((uint32_t)idx % DN_NLFO); break;
    case DNC_SEL_MSLOT:
        dn.wv_slot = (uint8_t)((uint32_t)idx % DN_WT_NMOD);
        if (id >= DN_W(WP_M0) && id < DN_W(WP_M0 + 3))
            id += 3u * dn.wv_slot;
        break;
    default: break;
    }
    dn_smc_sync();
    return id;
}
/* (DNC_SEL_TRACK: a track's own value, its sends: that track for the call, the selection kept) */
static uint8_t trk_keep;
static uint32_t dnc_in(uint32_t id, int sel, int idx)
{
    trk_keep = dn.trk;
    if (sel == DNC_SEL_TRACK) {
        dn.trk = (uint8_t)((uint32_t)(idx < 0 ? 0 : idx) % DN_SEQ_TRACKS);
        dn_smc_sync();
        return id;
    }
    return dnc_ctx(id, sel, idx);
}
static void dnc_out(void)
{
    if (dn.trk != trk_keep) {
        dn.trk = trk_keep;
        dn_smc_sync();
    }
}
int32_t dnc_get(uint32_t id, int sel, int idx) { int32_t v = dn_get(dnc_in(id, sel, idx)); dnc_out(); return v; }
int32_t dnc_min(uint32_t id, int sel, int idx) { int32_t v = dn_min(dnc_in(id, sel, idx)); dnc_out(); return v; }
int32_t dnc_max(uint32_t id, int sel, int idx) { int32_t v = dn_max(dnc_in(id, sel, idx)); dnc_out(); return v; }
const char *dnc_name(uint32_t id, int sel, int idx) { const char *v = dn_name(dnc_in(id, sel, idx)); dnc_out(); return v; }
void dnc_text(uint32_t id, int sel, int idx, int32_t v, char out[16])
{
    char t[16] = {0};
    id = dnc_in(id, sel, idx);
    dn_text(id, v, t);
    dnc_out();
    t[15] = 0;
    memcpy(out, t, 16);
}
void dnc_set(uint32_t id, int sel, int idx, int32_t v)
{
    id = dnc_in(id, sel, idx);
    v = dn_clamp(v, dn_min(id), dn_max(id));
    if (id == DN_PX_LDEST) {                         /* (DEST: only this synth's) */
        uint32_t l[DN_LD_N], n = dn_ld_list(DN_IS_SUB(dn.trk), l), i, ok = 0;
        for (i = 0; i < n; i++)
            ok |= l[i] == (uint32_t)v;
        if (!ok) {
            dnc_out();
            return;
        }
    }
    dn_set(id, v);
    dnc_out();
}
/* the values a value can take, in order (a list's: LFO DEST, this synth's only); how many (at most max) */
uint32_t dnc_choices(uint32_t id, int sel, int idx, int32_t *out, uint32_t max)
{
    uint32_t n = 0;
    int32_t v, lo, hi;
    id = dnc_in(id, sel, idx);
    if (id == DN_PX_LDEST) {
        uint32_t l[DN_LD_N], c = dn_ld_list(DN_IS_SUB(dn.trk), l), i;
        for (i = 0; i < c && n < max; i++)
            out[n++] = (int32_t)l[i];
        dnc_out();
        return n;
    }
    lo = dn_min(id), hi = dn_max(id);
    for (v = lo; v <= hi && n < max; v++)
        out[n++] = v;
    dnc_out();
    return n;
}

/* --------------------------------------------------------------------------------------------- sections --- */
static uint32_t sec_n, sec_max;
static dnc_section_t *sec_out;
static void sec_add(const char *name, int sel, const uint16_t *ids, uint32_t n)
{
    dnc_section_t *s;
    uint32_t i;
    if (sec_n >= sec_max)
        return;
    s = &sec_out[sec_n];
    memset(s, 0, sizeof *s);
    s->name = name;
    s->sel = (uint8_t)sel;
    for (i = 0; i < n && s->n < DNC_SEC_MAX; i++)
        if (ids[i] != DN_PX_NONE)
            s->id[s->n++] = ids[i];
    if (s->n)
        sec_n++;
}
static void sec_add8(const char *name, int sel, const uint8_t *ids, uint32_t n)
{
    uint16_t w[DNC_SEC_MAX];
    uint32_t i;
    for (i = 0; i < n && i < DNC_SEC_MAX; i++)
        w[i] = ids[i];
    sec_add(name, sel, w, i);
}
/* track t's instrument as sections of values (the FM-1's pages, every view): how many */
uint32_t dnc_sections(int t, dnc_section_t *out, uint32_t max)
{
    uint32_t e, k;
    t = (int)((uint32_t)t % DN_SEQ_TRACKS);
    e = dn_eng[t];
    sec_out = out, sec_max = max, sec_n = 0;
    if (e == DN_E_REESE) {
        static const uint16_t FE[4] = {DN_PX_RFE, DN_PX_RFE + 1, DN_PX_RFE + 2, DN_PX_RFE + 3};
        static const uint16_t MOD[2] = {RP_FENV, RP_GLIDE};
        static const uint16_t OUT[4] = {RP_LEVEL, RP_TRANS, DN_PX_OVL, DN_PX_VSENS};
        sec_add8("OSCILLATORS", 0, DN_PAGE[0], 4);
        sec_add8("FILTER", 0, DN_PAGE[1], 4);
        sec_add("FILTER ENV", 0, FE, 4);
        sec_add8("AMP ENV", 0, DN_PAGE[2], 4);
        sec_add("MOD", 0, MOD, 2);
        sec_add("OUT", 0, OUT, 4);
    } else if (e == DN_E_SUB) {
        static const uint16_t FE[4] = {DN_PX_SFE, DN_PX_SFE + 1, DN_PX_SFE + 2, DN_PX_SFE + 3};
        sec_add8("OSCILLATORS", 0, DN_SUB_PAGE[0], 4);
        sec_add8("PULSE / SUB", 0, DN_SUB_OSC2, 4);
        sec_add8("FILTER", 0, DN_SUB_PAGE[1], 4);
        sec_add8("DIRT", 0, DN_SUB_DIRT, 4);
        sec_add("FILTER ENV", 0, FE, 4);
        sec_add8("AMP ENV", 0, DN_SUB_PAGE[2], 4);
        sec_add8("MOD", 0, DN_SUB_PAGE[3], 4);
        {
            static const uint16_t OUT[5] = {DN_S(SP_LEVEL), DN_S(SP_TRANS), DN_S(SP_SPLIT), DN_PX_OVL, DN_PX_VSENS};
            sec_add("OUT", 0, OUT, 5);
        }
    } else if (e == DN_E_SMP || e == DN_E_SMP2) {
        const dn_smp_t *sm = &dn_smx[e == DN_E_SMP2 ? 1 : 0];
        static const uint16_t MODE_O[1] = {DN_PX_SMODE};
        static const uint16_t MODE_K[2] = {DN_PX_SMODE, DN_PX_KY0 + KY_VOICES};
        if (sm->p.mode == DN_SMP_ONESHOT) {
            sec_add("MODE", 0, MODE_O, 1);
            for (k = 0; k < 4u; k++)
                sec_add8(k == 0 ? "PAD: SAMPLE" : k == 1 ? "PAD: FILTER" : k == 2 ? "PAD: AMP" : "PAD: PITCH, PAN",
                         DNC_SEL_PAD, DN_SMP_PG[0][k], 4);
        } else {
            static const uint16_t ZL[2] = {DN_PX_ZN0 + ZN_LEVEL, DN_PX_ZN0 + ZN_START};
            sec_add("MODE", 0, MODE_K, 2);
            sec_add8("ZONE", DNC_SEL_ZONE, DN_SMP_PG[1][0], 4);
            sec_add8("ZONE: LOOP, TUNE", DNC_SEL_ZONE, DN_SMP_PG[1][3], 4);
            sec_add("ZONE: LEVEL", DNC_SEL_ZONE, ZL, 2);
            sec_add8("FILTER", 0, DN_SMP_PG[1][1], 4);
            sec_add8("AMP ENV", 0, DN_SMP_PG[1][2], 4);
        }
    } else if (e == DN_E_FM) {
        static const uint16_t VOICE[5] = {DN_PX_FALG, DN_PX_FFB, DN_PX_FOKS, DN_PX_FTRNSP, DN_PX_FVOICES};
        static const uint16_t MAC[4] = {DN_PX_FBRIGHT, DN_PX_FTIME, DN_PX_FATK, DN_PX_FREL};
        static const uint16_t OUT[1] = {DN_PX_FLEVEL};
        static const uint16_t LFO[8] = {DN_PX_FLFW, DN_PX_FLFS, DN_PX_FLFD, DN_PX_FLFKS, DN_PX_FPMD, DN_PX_FAMD,
                                        DN_PX_FPMS};
        static const uint16_t OP[6] = {DN_PX_FO0 + DN_FMO_OL, DN_PX_FO0 + DN_FMO_COARSE, DN_PX_FO0 + DN_FMO_FINE,
                                       DN_PX_FO0 + DN_FMO_DET, DN_PX_FO0 + DN_FMO_MODE, DN_PX_FO0 + DN_FMO_KVS};
        static const uint16_t EG[8] = {DN_PX_FO0 + DN_FMO_R1, DN_PX_FO0 + DN_FMO_R2, DN_PX_FO0 + DN_FMO_R3,
                                       DN_PX_FO0 + DN_FMO_R4, DN_PX_FO0 + DN_FMO_L1, DN_PX_FO0 + DN_FMO_L2,
                                       DN_PX_FO0 + DN_FMO_L3, DN_PX_FO0 + DN_FMO_L4};
        sec_add("VOICE", 0, VOICE, 5);
        sec_add("MACROS", 0, MAC, 4);
        sec_add("OPERATOR", DNC_SEL_OP, OP, 6);
        sec_add("OPERATOR EG", DNC_SEL_OP, EG, 8);
        sec_add("LFO", 0, LFO, 7);
        sec_add("OUT", 0, OUT, 1);
    } else if (e == DN_E_WAVE) {
        uint32_t pg, vw;
        for (pg = 0; pg < DN_NPAGES; pg++)
            for (vw = 0; vw < DN_WV_NVIEW[pg]; vw++) {
                int mx = pg == 3u && vw == 4u;
                sec_add(mx ? "MATRIX" : DN_WV_TAB[pg][vw], mx ? DNC_SEL_MSLOT : 0, DN_WV_PG[pg][vw] + (mx ? 1 : 0),
                        mx ? 3u : 4u);
            }
    } else if (e == DN_E_JUNO) {
        uint32_t pg, vw;
        for (pg = 0; pg < DN_NPAGES; pg++)
            for (vw = 0; vw < DN_JU_NVIEW[pg]; vw++)
                sec_add(DN_JU_TAB[pg][vw], 0, DN_JU_PG[pg][vw], 4);
    } else if (e == DN_E_BRKA || e == DN_E_BRKB) {
        sec_add8("LOOP", 0, DN_BRK_PAGE, 4);
        sec_add8("SLICES, LEVEL", 0, DN_BRK2_PAGE, 4);
    } else if (e == DN_E_MIDI) {
        static const uint16_t M[2] = {DN_PX_MCHAN, DN_PX_MOVL};
        sec_add("MIDI OUT", 0, M, 2);
    }
    if (e == DN_E_REESE || e == DN_E_SUB) {         /* (their LFOs, 1-4: the synth's own set) */
        static const uint16_t L[8] = {DN_PX_LSHAPE, DN_PX_LRATE, DN_PX_LDEST, DN_PX_LDEPTH, DN_PX_LSYNC, DN_PX_LMODE,
                                      DN_PX_LPHASE, DN_PX_LSMOOTH};
        sec_add("LFO", DNC_SEL_LFO, L, 8);
    }
    if (e != DN_E_OFF) {                            /* the track's sends */
        static const uint16_t S[2] = {DN_PX_SENDA, DN_PX_SENDB};
        sec_add("SENDS", 0, S, 2);
    }
    return sec_n;
}
/* the effects: their returns, then FX A, FX B (type and every value the effect has) */
uint32_t dnc_fx_sections(dnc_section_t *out, uint32_t max)
{
    static const uint16_t RET[2] = {DN_PX_RETA, DN_PX_RETB};
    uint16_t a[8], b[8];
    uint32_t k, na = 0, nb = 0, slot;
    sec_out = out, sec_max = max, sec_n = 0;
    for (k = 0; k < 4u; k++) {
        uint32_t ida = DN_PX_FXA + k, idb = DN_PX_FXB + k;
        if (k == 0u || dn_fx_pname(&dn_fxp[0], (uint32_t)dn_fx_pi(ida, &slot))[0] != '-')
            a[na++] = (uint16_t)ida;
        if (k == 0u || dn_fx_pname(&dn_fxp[1], (uint32_t)dn_fx_pi(idb, &slot))[0] != '-')
            b[nb++] = (uint16_t)idb;
    }
    for (k = 0; k < 4u; k++) {
        uint32_t ida = DN_PX_FXA2 + k, idb = DN_PX_FXB2 + k;
        int pa = dn_fx_pi(ida, &slot), pb = dn_fx_pi(idb, &slot);
        if (pa >= 0 && dn_fx_pname(&dn_fxp[0], (uint32_t)pa)[0] != '-')
            a[na++] = (uint16_t)ida;
        if (pb >= 0 && dn_fx_pname(&dn_fxp[1], (uint32_t)pb)[0] != '-')
            b[nb++] = (uint16_t)idb;
    }
    sec_add("RETURNS", 0, RET, 2);
    sec_add("FX A", 0, a, na);
    sec_add("FX B", 0, b, nb);
    return sec_n;
}
uint32_t dnc_id_bpm(void) { return DN_PX_BPM; }
uint32_t dnc_id_send(int b) { return b ? DN_PX_SENDB : DN_PX_SENDA; }
uint32_t dnc_id_special(int which)                  /* SAMPLER's: 0 a pad's SAMPLE, 1 a zone's, 2-4 its ROOT LOW HIGH,
                                                     * 5 the MODE */
{
    static const uint16_t W[6] = {DN_PX_PD0 + PD_SAMPLE, DN_PX_ZN0 + ZN_SAMPLE, DN_PX_ZN0 + ZN_ROOT,
                                  DN_PX_ZN0 + ZN_LOW, DN_PX_ZN0 + ZN_HIGH, DN_PX_SMODE};
    return which >= 0 && which < 6 ? W[which] : DN_PX_NONE;
}
int dnc_smp_mode(int t)                              /* track t's SAMPLER: 0 ONE-SHOT, 1 KEYS (-1: not one) */
{
    t = (int)((uint32_t)t % DN_SEQ_TRACKS);
    return DN_IS_SMP(t) ? dn_smx[DN_SMI(t)].p.mode : -1;
}
uint32_t dnc_id_lfo_shape(void) { return DN_PX_LSHAPE; }

/* -------------------------------------------------------------------------------------------- presets --- */
/* the selected track's instrument's sounds: REESE / SUB the factory's then USER 1-8, FM the factory's then the
 * project's voices, WAVE and JUNO the factory's; the others none */
uint32_t dnc_preset_count(void)
{
    uint32_t t = dn.trk;
    return DN_IS_RS(t) ? reese_preset_count() + DN_NUSER : DN_IS_SUB(t) ? dn_sub_preset_count() + DN_NUSER
         : DN_IS_FM(t) ? dn_fm_presets() : DN_IS_WAVE(t) ? dn_wt_preset_count() : DN_IS_JUNO(t) ? dn_ju_preset_count()
         : 0u;
}
int dnc_preset_current(void)
{
    uint32_t t = dn.trk;
    return DN_IS_RS(t) ? dn.preset : DN_IS_SUB(t) ? dn.sb_preset : DN_IS_FM(t) ? dn.fm_preset
         : DN_IS_WAVE(t) ? dn.wv_preset : DN_IS_JUNO(t) ? dn.ju_preset : -1;
}
const char *dnc_preset_name(uint32_t i)
{
    static char nm[12];
    uint32_t t = dn.trk;
    if (DN_IS_RS(t) || DN_IS_SUB(t))
        return dn_sound_name(DN_IS_SUB(t) ? 1u : 0u, i);
    if (DN_IS_FM(t)) {
        uint32_t nf = dn_fm_factory_count(), j;
        const uint8_t *pk;
        if (i < nf)
            return dn_fm_factory_name(i);
        pk = dn_fmv_get(i - nf);
        if (!pk)
            return "";
        for (j = 0; j < 10u; j++)                   /* (a packed voice's name: its last 10 bytes) */
            nm[j] = pk[118 + j] >= 32u && pk[118 + j] < 127u ? (char)pk[118 + j] : ' ';
        nm[10] = 0;
        return nm;
    }
    if (DN_IS_WAVE(t))
        return dn_wt_preset_name(i);
    if (DN_IS_JUNO(t))
        return dn_ju_preset_name(i);
    return "";
}
int dnc_preset_used(uint32_t i)                      /* (an empty USER slot: 0) */
{
    uint32_t t = dn.trk, nf;
    if (!DN_IS_SYN(t))
        return 1;
    nf = DN_IS_SUB(t) ? dn_sub_preset_count() : reese_preset_count();
    return i < nf || dn_user[DN_IS_SUB(t) ? 1 : 0][(i - nf) % DN_NUSER].used;
}
void dnc_preset_load(uint32_t i)
{
    uint32_t t = dn.trk;
    if (i >= dnc_preset_count())
        return;
    if (DN_IS_RS(t))
        dn_load_preset(i);
    else if (DN_IS_SUB(t))
        dn_sub_preset(i);
    else if (DN_IS_FM(t))
        dn_fm_load(i);
    else if (DN_IS_WAVE(t))
        dn_wv_load(i);
    else if (DN_IS_JUNO(t))
        dn_ju_load(i);
}
/* REESE / SUB: the sound now into USER u (0-7), named (8 characters; empty: "USER n") */
int dnc_user_store(uint32_t u, const char *name)
{
    uint32_t t = dn.trk, set, j;
    if (!DN_IS_SYN(t) || u >= DN_NUSER)
        return -1;
    set = DN_IS_SUB(t) ? 1u : 0u;
    dn_patch_store(set, &dn_user[set][u]);
    if (set)
        dn.sb_preset = (uint8_t)(dn_sub_preset_count() + u);
    else
        dn.preset = (uint8_t)(reese_preset_count() + u);
    for (j = 0; j < 8u; j++) {
        char c = name && j < str_len_dn(name) ? name[j] : 0;
        dn_man.uname[set][u][j] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c >= 32 && c < 127 ? c : 0;
    }
    dn_save();
    return 0;
}

/* ---------------------------------------------------------------------------------------------- patterns --- */
int dnc_pat_current(int t) { return dn_pcur[(uint32_t)t % DN_SEQ_TRACKS]; }
int dnc_pat_queued(int t) { return dn_pq[(uint32_t)t % DN_SEQ_TRACKS] == 0xFFu ? -1 : dn_pq[(uint32_t)t % DN_SEQ_TRACKS]; }
uint32_t dnc_pat_used(int t) { return dn_pat_used((uint32_t)t % DN_SEQ_TRACKS); }
void dnc_pat_select(int t, int n) { dn_pat_select((uint32_t)t % DN_SEQ_TRACKS, (uint32_t)n); }
int dnc_track_len(int t) { return dn_sq.pat.tr[(uint32_t)t % DN_SEQ_TRACKS].len; }
void dnc_track_set_len(int t, int len)
{
    dn_sq.pat.tr[(uint32_t)t % DN_SEQ_TRACKS].len = (uint8_t)dn_clamp(len, 1, (int32_t)DN_SEQ_STEPS);
    dn_mark();
}
int dnc_swing(void) { return dn_sq.pat.swing; }
void dnc_set_swing(int s)
{
    dn_sq.pat.swing = (uint8_t)dn_clamp(s, 50, 75);
    dn_mark();
}
void dnc_step_get(int t, int i, dnc_step_t *out)
{
    const dn_step_t *s = &dn_sq.pat.tr[(uint32_t)t % DN_SEQ_TRACKS].st[(uint32_t)i % DN_SEQ_STEPS];
    out->note = s->note, out->flags = s->flags, out->len = s->len, out->vel = s->vel;
    out->cond = s->cond, out->rtim = s->rtim, out->rlen = s->rlen, out->rvel = s->rvel;
}
void dnc_step_set(int t, int i, const dnc_step_t *in)
{
    dn_step_t *s = &dn_sq.pat.tr[(uint32_t)t % DN_SEQ_TRACKS].st[(uint32_t)i % DN_SEQ_STEPS];
    s->note = in->note & 127u;
    s->flags = in->flags;
    s->len = (uint8_t)(in->len ? in->len : 1u);
    s->vel = (uint8_t)dn_clamp(in->vel, 1, 127);
    s->cond = in->cond;
    s->rtim = (uint8_t)(in->rtim % DN_RTIM_N);
    s->rlen = in->rlen;
    s->rvel = in->rvel;
    if (s->flags & DN_ST_ON)
        dn_step_remember((uint32_t)t % DN_SEQ_TRACKS, s);
    dn_mark();
}
void dnc_step_default(int t, dnc_step_t *out)      /* a new step on track t: the last note put in, or the slice */
{
    dn_step_t s;
    dn_step_clear(&s);
    t = (int)((uint32_t)t % DN_SEQ_TRACKS);
    if (!DN_IS_PADS(t)) {
        s.note = dn_sq.pat.tr[t].last_note;
        if (dn.last_len[t])
            s.len = dn.last_len[t];
    }
    out->note = s.note, out->flags = DN_ST_ON, out->len = s.len, out->vel = s.vel;
    out->cond = 0, out->rtim = s.rtim, out->rlen = 0, out->rvel = 0;
}
int dnc_playing_step(int t)                          /* the step track t plays now (-1: stopped) */
{
    return dn_sq.playing && dn_sq.pos ? dn_sq.cur[(uint32_t)t % DN_SEQ_TRACKS] : -1;
}
int dnc_is_pads(int t) { return DN_IS_PADS((uint32_t)t % DN_SEQ_TRACKS) ? 1 : 0; }
uint32_t dnc_pads(int t)                             /* the rows of a pads track: its slices, or SAMPLER's 16 pads */
{
    t = (int)((uint32_t)t % DN_SEQ_TRACKS);
    return DN_IS_BRK(t) ? dn_bk[DN_BK(t)].nslices : DN_IS_SMP(t) ? DN_SMP_PADS : 0u;
}
void dnc_cond_text(int c, char out[8]) { dn_cond_text((uint32_t)c & 0x7Fu, out); }
int dnc_cond_count(void) { return DN_CO_N; }
int dnc_rtim_den(int i) { return DN_RTIM_DEN[(uint32_t)i % DN_RTIM_N]; }
int dnc_rtim_count(void) { return DN_RTIM_N; }
const char *dnc_chord_name(int c) { return DN_CHORD_NAME[(uint32_t)c % (DN_CHORDS + 1u)]; }
int dnc_chord_count(void) { return DN_CHORDS + 1; }
const char *dnc_hitfx_name(int c)
{
    return (uint32_t)c < DN_HFX_N ? DN_HFX_NAME[c] : "";
}
int dnc_hitfx_count(void) { return DN_HFX_N; }
int dnc_track_chords(int t) { return DN_CHORDABLE((uint32_t)t % DN_SEQ_TRACKS) ? 1 : 0; }
int dnc_track_hitfx(int t) { return DN_HITFX((uint32_t)t % DN_SEQ_TRACKS) ? 1 : 0; }
int dnc_transport_toggle(void)                      /* the plugin's PLAY (not following the host) */
{
    if (dn_sq.playing)
        dnc_stop();
    else
        dnc_start();
    return dn_sq.playing != 0;
}

/* ---------------------------------------------------------------------------------------- the breaks' slices --- */
/* BREAKS A / B: its loop (ADPCM from predictor 0, n samples at rate), its slices now (starts, how many) and whether
 * they are hand-set (MANUAL); 0: no loop */
int dnc_brk_info(int b, const uint8_t **adpcm, uint32_t *n, uint32_t *rate, uint32_t *starts, uint32_t *ns, int *manual)
{
    const dn_brk_t *k = &dn_bk[(uint32_t)b % DN_NBRK];
    uint32_t i;
    if (!k->ok || !k->loop.adpcm)
        return 0;
    *adpcm = k->loop.adpcm, *n = k->loop.n, *rate = k->loop.rate;
    *ns = k->nslices;
    for (i = 0; i < k->nslices && i < DN_BRK_MAXSL; i++)
        starts[i] = k->start[i];
    *manual = k->smode == 2u;
    return 1;
}
int dnc_brk_slices_set(int b, const uint32_t *starts, uint32_t n)   /* hand-set slices (0: back to AUTO) */
{
    const dn_brk_t *k = &dn_bk[(uint32_t)b % DN_NBRK];
    int rc;
    if (!k->ok)
        return -1;
    rc = dn_app_slices_set(k->key, n, starts);
    dn_mark();
    return rc;
}
int dnc_brk_of_track(int t) { return DN_IS_BRK((uint32_t)t % DN_SEQ_TRACKS) ? (int)DN_BK((uint32_t)t % DN_SEQ_TRACKS) : -1; }

/* --------------------------------------------------------------------------------------------- the samples --- */
/* every zone of the three slots in order (samples, and DX7 voices or wavetables sent with them): kind 0 a sample,
 * 1 DX7 voices, 2 a wavetable */
static char smp_names[48][24];                      /* (the plugin's names for them, by place; kept in the state) */
uint32_t dnc_sample_count(void)
{
    uint32_t n = 0;
    int k;
    for (k = 0; k < PLAT_NSLOTS; k++)
        n += (uint32_t)plat_slot_zones(k);
    return n;
}
static int smp_at(uint32_t i, int *kk, int *zz)
{
    int k, z;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++)
            if (i-- == 0u) {
                *kk = k, *zz = z;
                return 1;
            }
    return 0;
}
const uint8_t *dnc_sample(uint32_t i, uint32_t *n, uint32_t *rate, int *kind)
{
    int k, z;
    uint32_t c;
    char nm[9];
    const uint8_t *a;
    if (!smp_at(i, &k, &z))
        return 0;
    a = plat_slot(k, z, n, rate, nm);
    *kind = dn_fmz(k, z, &c) ? 1 : dn_wtz(k, z) ? 2 : 0;
    return a;
}
const char *dnc_sample_name(uint32_t i) { return i < 48u ? smp_names[i] : ""; }
void dnc_sample_set_name(uint32_t i, const char *s)
{
    uint32_t j;
    if (i >= 48u)
        return;
    for (j = 0; j < 23u && s && s[j]; j++)
        smp_names[i][j] = s[j];
    smp_names[i][j] = 0;
}
int dnc_loop_of_sample(uint32_t i)                  /* its place among the loops BREAKS and SAMPLER pick (-1: not one) */
{
    int k, z, kk, zz, n = 0;
    if (!smp_at(i, &kk, &zz))
        return -1;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++) {
            if (k == kk && z == zz)
                return dn_zone_skip(k, z) ? -1 : n;
            n += !dn_zone_skip(k, z);
        }
    return -1;
}
/* the slots written from these parts, in order (as the editor packs them: media.js packPlan, slotImage): 0, or -1
 * (they don't fit: nothing changed) */
static uint32_t even_u(uint32_t n) { return n + (n & 1u); }
int dnc_samples_store(const dnc_part_t *p, uint32_t np)
{
    static uint8_t img[3][0x14000];
    int slot_of[48];
    uint32_t used[3] = {0, 0, 0}, zones[3] = {0, 0, 0}, i, k = 0;
    if (np > 48u)
        return -1;
    for (i = 0; i < np; i++) {                       /* (the plan first: all of it fits, or nothing changes) */
        uint32_t b = even_u(p[i].bytes);
        if (k < 3u && (used[k] + b > slot_size(k) - SMP_USER_DATA || zones[k] >= 16u))
            k++;
        if (k >= 3u || b > slot_size(k) - SMP_USER_DATA)
            return -1;
        slot_of[i] = (int)k;
        used[k] += b;
        zones[k]++;
    }
    for (k = 0; k < 3u; k++) {
        smp_user_hdr_t h;
        uint32_t off = 0, z = 0, last = 0;
        memset(img[k], 0xFF, sizeof img[k]);
        memset(&h, 0, sizeof h);
        for (i = 0; i < np; i++)
            if (slot_of[i] == (int)k)
                last = i;
        for (i = 0; i < np; i++) {
            smp_zone_t *zn;
            if (slot_of[i] != (int)k)
                continue;
            zn = &h.zone[z];
            memcpy(img[k] + SMP_USER_DATA + off, p[i].adpcm, p[i].bytes);
            zn->off = off, zn->n = p[i].n, zn->ls = 0, zn->le = p[i].n ? p[i].n - 1u : 0u;
            zn->rate = (uint32_t)(((uint64_t)(p[i].rate ? p[i].rate : 22050u) * 65536u + 22050u) / 44100u);
            zn->root16 = (int16_t)((36 + (int)z) * 16);
            zn->lo = zn->hi = (uint8_t)(36u + z);
            off += i == last ? p[i].bytes : even_u(p[i].bytes);
            z++;
        }
        if (!z)
            continue;
        h.magic = SMP_USER_MAGIC, h.version = 1, h.nz = (uint8_t)z;
        h.name[0] = 'S', h.name[1] = 'M', h.name[2] = 'P', h.name[3] = (char)('1' + k);
        h.data_len = off;
        h.crc = crc32_z(img[k] + SMP_USER_DATA, off);
        memcpy(img[k], &h, sizeof h);
    }
    dn_app_slots_changed(0);                        /* (the instruments let go first) */
    for (k = 0; k < 3u; k++) {
        slot_clear(k);
        if (((const smp_user_hdr_t *)(const void *)img[k])->magic == SMP_USER_MAGIC) {
            memcpy(slot_mem(k), img[k], slot_size(k));
            smp_user_scan(k);
        }
    }
    dn_app_slots_changed(1);
    dn_mark();
    return 0;
}
uint32_t dnc_samples_room(void) { return 3u * SMP_USER_SIZE - 3u * SMP_USER_DATA - (SMP_USER_SIZE - 64u * 1024u); }

/* a sample converted as the editor converts one (media.js: resample through a low-pass, trim silence off the ends,
 * 2 ms fades, peak at 0.95, IMA ADPCM): mono in at src_rate, out at rate (`out` holds (n + 1) / 2 bytes of the
 * result); the samples out */
static void lowpass_f(const float *x, float *out, uint32_t n, double fc, int taps)
{
    static double h[255];
    double sum = 0.0;
    int i, m = (taps - 1) / 2;
    for (i = 0; i < taps; i++) {
        int k = i - m;
        double w = 0.42 - 0.5 * cos(2.0 * M_PI * i / (taps - 1)) + 0.08 * cos(4.0 * M_PI * i / (taps - 1));
        h[i] = (k == 0 ? 2.0 * fc : sin(2.0 * M_PI * fc * k) / (M_PI * k)) * w;
        sum += h[i];
    }
    for (i = 0; i < taps; i++)
        h[i] /= sum;
    for (uint32_t j = 0; j < n; j++) {
        double acc = 0.0;
        int64_t j0 = (int64_t)j - m < 0 ? 0 : (int64_t)j - m, j1 = (int64_t)j + m > (int64_t)n - 1 ? (int64_t)n - 1 : (int64_t)j + m;
        for (int64_t q = j0; q <= j1; q++)
            acc += x[q] * h[q - (int64_t)j + m];
        out[j] = (float)acc;
    }
}
static const int16_t IMA_STEP_T[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
    55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX_T[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
void dnc_ima_encode(const int16_t *s, uint32_t n, uint8_t *out)
{
    int32_t pred = 0, idx = 0;
    uint32_t i;
    memset(out, 0, (n + 1u) / 2u);
    for (i = 0; i < n; i++) {
        int32_t step = IMA_STEP_T[idx], diff = s[i] - pred, vd = step >> 3;
        uint32_t code = 0;
        if (diff < 0)
            code = 8, diff = -diff;
        if (diff >= step) code |= 4, diff -= step, vd += step;
        if (diff >= step >> 1) code |= 2, diff -= step >> 1, vd += step >> 1;
        if (diff >= step >> 2) code |= 1, vd += step >> 2;
        pred = code & 8u ? pred - vd : pred + vd;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IMA_IDX_T[code & 7u];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        out[i >> 1] |= (uint8_t)(i & 1u ? code << 4 : code);
    }
}
void dnc_ima_decode(const uint8_t *b, uint32_t n, int16_t *out)
{
    int32_t pred = 0, idx = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t code = (b[i >> 1] >> (i & 1u ? 4 : 0)) & 15u;
        int32_t step = IMA_STEP_T[idx], vd = step >> 3;
        if (code & 4u) vd += step;
        if (code & 2u) vd += step >> 1;
        if (code & 1u) vd += step >> 2;
        pred = code & 8u ? pred - vd : pred + vd;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IMA_IDX_T[code & 7u];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        out[i] = (int16_t)pred;
    }
}
uint32_t dnc_convert(const float *x, uint32_t n, uint32_t src_rate, uint32_t rate, int trim, uint8_t *out,
                     uint32_t out_max)
{
    float *lp = 0, *r;
    int16_t *s;
    uint32_t m, k, a = 0, b, len, f;
    double pk = 1e-9, g, step;
    const float *src = x;
    if (!n || !src_rate || !rate)
        return 0;
    if (rate < src_rate) {
        lp = (float *)malloc(n * sizeof *lp);
        if (!lp)
            return 0;
        lowpass_f(x, lp, n, 0.45 * rate / src_rate, rate < src_rate / 3u ? 127 : 63);
        src = lp;
    }
    m = rate == src_rate ? n : (uint32_t)(((uint64_t)(n - 1u) * rate) / src_rate) + 1u;
    r = (float *)malloc(m * sizeof *r);
    s = (int16_t *)malloc(m * sizeof *s);
    if (!r || !s) {
        free(lp), free(r), free(s);
        return 0;
    }
    step = (double)src_rate / rate;
    for (k = 0; k < m; k++) {                        /* (Catmull-Rom) */
        double p = k * step, fr;
        int64_t i = (int64_t)p;
        float y0, y1, y2, y3;
        fr = p - (double)i;
#define AT(j) src[(j) < 0 ? 0 : (j) >= (int64_t)n ? (int64_t)n - 1 : (j)]
        y0 = AT(i - 1), y1 = AT(i), y2 = AT(i + 1), y3 = AT(i + 2);
#undef AT
        r[k] = (float)(y1 + 0.5 * fr * (y2 - y0 + fr * (2 * y0 - 5 * y1 + 4 * y2 - y3 + fr * (3 * (y1 - y2) + y3 - y0))));
    }
    for (k = 0; k < m; k++)
        pk = fabs(r[k]) > pk ? fabs(r[k]) : pk;
    b = m;
    if (trim) {
        double th = pk * 0.001;
        while (a < b && fabs(r[a]) < th) a++;
        while (b > a && fabs(r[b - 1u]) < th) b--;
    }
    len = b > a ? b - a : 1u;
    if ((len + 1u) / 2u > out_max)
        len = out_max * 2u;
    f = (uint32_t)(rate * 0.002 + 0.5);
    f = f ? f : 1u;
    g = 0.95 / pk;
    for (k = 0; k < len; k++) {
        double w = 1.0, v;
        if (trim) {
            double w1 = (double)k / f, w2 = (double)(len - 1u - k) / f;
            w = w1 < w ? w1 : w;
            w = w2 < w ? w2 : w;
        }
        v = r[a + k < m ? a + k : m - 1u] * g * w * 32767.0;
        s[k] = (int16_t)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : (int32_t)(v < 0 ? v - 0.5 : v + 0.5));
    }
    dnc_ima_encode(s, len, out);
    free(lp), free(r), free(s);
    return len;
}

/* ------------------------------------------------------------------------------------------- the status word --- */
const char *dnc_message(void)                        /* what the FM-1's status line would say now ("" none) */
{
    return dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0 ? dn.msg : "";
}
int dnc_dirty(void) { return dn.dirty != 0; }
void dnc_save(void) { dn_save(); }

/* ------------------------------------------------------------------------------------------ host automation --- */
/* each instrument's values a DAW can automate (the instrument's own, whichever track it is on): up to 8 each */
static const uint16_t AUTO_RS[8] = {RP_CUT, RP_RES, RP_DRV, RP_DTN, RP_FENV, RP_DEC, RP_GLIDE, RP_LEVEL};
static const uint16_t AUTO_SB[8] = {DN_S(SP_CUT), DN_S(SP_RES), DN_S(SP_ENV), DN_S(SP_DRIVE), DN_S(SP_PW),
                                    DN_S(SP_DEC), DN_S(SP_GLIDE), DN_S(SP_LEVEL)};
static const uint16_t AUTO_FM[6] = {DN_PX_FBRIGHT, DN_PX_FTIME, DN_PX_FATK, DN_PX_FREL, DN_PX_FFB, DN_PX_FLEVEL};
static const uint16_t AUTO_WV[8] = {DN_W(WP_POS), DN_W(WP_WAMT), DN_W(WP_CUT), DN_W(WP_RES), DN_W(WP_FENV),
                                    DN_W(WP_DRIVE), DN_W(WP_BPOS), DN_W(WP_LEVEL)};
static const uint16_t AUTO_JU[8] = {DN_J(JP_CUT), DN_J(JP_RES), DN_J(JP_FENV), DN_J(JP_PWM), DN_J(JP_ATK),
                                    DN_J(JP_DEC), DN_J(JP_REL), DN_J(JP_LEVEL)};
static const uint16_t AUTO_SM[4] = {DN_PX_KY0 + KY_CUT, DN_PX_KY0 + KY_RES, DN_PX_KY0 + KY_DRIVE, DN_PX_KY0 + KY_CRUSH};
static const uint16_t AUTO_BK[4] = {DN_PX_BPITCH, DN_PX_BDECAY, DN_PX_BCRUSH, DN_PX_BLEVEL};
uint32_t dnc_auto_ids(int e, uint16_t *out)
{
    const uint16_t *a = 0;
    uint32_t n = 0, i;
    switch (e) {
    case DN_E_REESE: a = AUTO_RS, n = 8; break;
    case DN_E_SUB: a = AUTO_SB, n = 8; break;
    case DN_E_FM: a = AUTO_FM, n = 6; break;
    case DN_E_WAVE: a = AUTO_WV, n = 8; break;
    case DN_E_JUNO: a = AUTO_JU, n = 8; break;
    case DN_E_SMP: case DN_E_SMP2: a = AUTO_SM, n = 4; break;
    case DN_E_BRKA: case DN_E_BRKB: a = AUTO_BK, n = 4; break;
    default: break;
    }
    for (i = 0; i < n; i++)
        out[i] = a[i];
    return n;
}
/* a value of instrument e (on whatever track), as the automation sets it: the app's selection pointed at it */
static void auto_ctx(int e, uint8_t *keep_trk, uint8_t *keep_smc, uint8_t *keep_bk)
{
    *keep_trk = dn.trk, *keep_smc = dn_smc, *keep_bk = dn.bk_force;
    if (e == DN_E_SMP || e == DN_E_SMP2)
        dn_smc = (uint8_t)(e == DN_E_SMP2);
    if (e == DN_E_BRKA || e == DN_E_BRKB)
        dn.bk_force = (uint8_t)(e - DN_E_BRKA + 1);
    if (e == DN_E_SUB || e == DN_E_REESE) {          /* (the shared values, OVERLAP: none automated) */
        uint32_t t;
        for (t = 0; t < DN_SEQ_TRACKS; t++)
            if (dn_eng[t] == (uint8_t)e)
                dn.trk = (uint8_t)t;
    }
}
int32_t dnc_auto_get(int e, uint32_t id)
{
    uint8_t a, b, c;
    int32_t v;
    auto_ctx(e, &a, &b, &c);
    v = dn_get(id);
    dn.trk = a, dn_smc = b, dn.bk_force = c;
    return v;
}
void dnc_auto_set(int e, uint32_t id, int32_t v)
{
    uint8_t a, b, c;
    auto_ctx(e, &a, &b, &c);
    if (dn_get(id) != dn_clamp(v, dn_min(id), dn_max(id)))
        dn_set(id, dn_clamp(v, dn_min(id), dn_max(id)));
    dn.trk = a, dn_smc = b, dn.bk_force = c;
}
int32_t dnc_auto_min(int e, uint32_t id) { uint8_t a, b, c; int32_t v; auto_ctx(e, &a, &b, &c); v = dn_min(id); dn.trk = a, dn_smc = b, dn.bk_force = c; return v; }
int32_t dnc_auto_max(int e, uint32_t id) { uint8_t a, b, c; int32_t v; auto_ctx(e, &a, &b, &c); v = dn_max(id); dn.trk = a, dn_smc = b, dn.bk_force = c; return v; }
