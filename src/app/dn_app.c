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
 *   HOME                     hold a note (F3) to tweak with both hands free
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

#define DN_SAVE_MAGIC 0x31524E44u                   /* "DNR1" */
#define DN_NPAGES 5u                                /* REESE's pages; then SEQ, then BRK */
#define DN_PG_SEQ DN_NPAGES
#define DN_PG_BRK (DN_NPAGES + 1u)
#define DN_NTABS (DN_NPAGES + 2u)
#define DN_NBRK 2u                                  /* BREAKS A and B: tracks 2 and 3 */
#define DN_IS_BRK(t) ((t) == 1u || (t) == 2u)
#define DN_KEY0 53                                  /* key 0 is F3 */
#define DN_PX_BPM 100                               /* the OUT page's two non-engine dials */
#define DN_PX_OCT 101
#define DN_PX_BSRC 110                              /* the BRK page: the loop, pitch, decay, crush of BREAKS A / B */
#define DN_PX_BPITCH 111
#define DN_PX_BDECAY 112
#define DN_PX_BCRUSH 113

static const char *const DN_PAGE_NAME[DN_NTABS] = {"OSC", "FILTER", "AMP", "MOD", "OUT", "SEQ", "BRK"};
static const uint8_t DN_BRK_PAGE[4] = {DN_PX_BSRC, DN_PX_BPITCH, DN_PX_BDECAY, DN_PX_BCRUSH};
static const uint8_t DN_PAGE[DN_NPAGES][4] = {
    {RP_OSC, RP_DTN, RP_BEAT, RP_SUB},
    {RP_CUT, RP_RES, RP_DRV, RP_NTCH},
    {RP_ATK, RP_DEC, RP_SUS, RP_REL},
    {RP_FENV, RP_GLIDE, RP_LRATE, RP_LFLT},
    {RP_LEVEL, RP_TRANS, DN_PX_BPM, DN_PX_OCT},
};
static const uint8_t DN_PAGE_BTN[DN_NPAGES] = {B_SEL, B_EDIT, B_ENV, B_LFO, B_GLO};

typedef struct {
    uint32_t magic;
    uint8_t preset, page;
    int8_t oct;
    uint8_t pad;
    uint16_t bpm, pad2;
    int16_t p[RP_COUNT];
    struct { uint8_t src; int8_t pitch; uint8_t decay, crush; } brk[DN_NBRK];   /* (from 0.5: older saves lack it) */
} dn_save_t;
#define DN_SAVE_V04 (sizeof(dn_save_t) - DN_NBRK * 4u)

static reese_t dn_rs;
static dn_seq_t dn_sq;
static dn_brk_t dn_bk[DN_NBRK] X0X_POOL;
static uint8_t dn_bk_src[DN_NBRK];                  /* each one's loop: an index into the sources (dn_src) */
static struct {
    uint8_t preset, page, dirty, play, ready, rec;
    uint8_t trk;                                    /* the selected track */
    uint8_t view;                                   /* the 16 steps in view on the SEQ page (0..3) */
    uint8_t rec_cur;                                /* the step-record cursor */
    int8_t step_held;                               /* the step key held on the SEQ page (-1: none) */
    uint8_t step_edited;                            /* a knob moved it (or it was just turned on): no toggle off */
    uint8_t key_page_seq;                           /* the keys down belong to the SEQ page (steps, not notes) */
    uint32_t clear_t0;                              /* black key 9 went down (0: not held) */
    int8_t oct;
    uint8_t held[NKEYS], nheld;                     /* the keys down, oldest first */
    uint8_t sounding;                               /* the note the keys play now (0xFF: none) */
    uint8_t midi_note;                              /* the note MIDI plays now (0xFF: none) */
    uint32_t btn_prev, keys_prev;
    uint32_t redraw;                                /* DN_RD_* */
} dn;
enum { DN_RD_TOP = 1u, DN_RD_BOTTOM = 2u, DN_RD_ALL = 3u };

/* the output, every 8th sample, for the screen's scope (written in the audio interrupt, read by the drawing) */
#define DN_SCOPE 512u
static int16_t dn_scope[DN_SCOPE];
static volatile uint32_t dn_scope_w;

/* -------------------------------------------------------------------------------------------- audio side --- */
/* the sequencer's notes: track 1 plays REESE, 2 and 3 BREAKS A / B (the note: the slice; the step's slide: reverse),
 * the others MIDI out on their channel (USB-MIDI event packets) */
static void dn_sq_on(uint32_t t, uint8_t note, uint8_t vel, int slide, uint8_t flags)
{
    if (t == 0u)
        reese_note_on(&dn_rs, note, vel, slide);
    else if (DN_IS_BRK(t))
        dn_brk_trigger(&dn_bk[t - 1u], note, vel, (flags & DN_ST_SLIDE) != 0);
    else
        plat_midi_out(0x09u | (0x90u | dn_sq.pat.tr[t].chan) << 8 | (uint32_t)note << 16 | (uint32_t)vel << 24);
}
static void dn_sq_off(uint32_t t, uint8_t note)
{
    if (DN_IS_BRK(t))
        dn_brk_release(&dn_bk[t - 1u]);
    else if (t != 0u)
        plat_midi_out(0x08u | (0x80u | dn_sq.pat.tr[t].chan) << 8 | (uint32_t)note << 16);
    else if (dn_rs.note == note && dn.sounding == 0xFF && dn.midi_note == 0xFF)
        reese_note_off(&dn_rs);                     /* (not a note the keys or MIDI in play now) */
}
static void dn_sq_clock(uint8_t st) { plat_midi_out(0x0Fu | (uint32_t)st << 8); }
static const dn_seq_cb_t DN_SQ_CB = {dn_sq_on, dn_sq_off, dn_sq_clock};

void dn_app_render(int32_t *lr, uint32_t n)
{
    uint32_t i, w = dn_scope_w, off = 0;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (!dn.ready)
        return;
    while (off < n) {                               /* cut at the sequencer's events: sample-accurate notes */
        uint32_t k = dn_seq_next(&dn_sq);
        if (k > n - off)
            k = n - off;
        reese_render(&dn_rs, lr + 2u * off, k);
        dn_brk_render(&dn_bk[0], lr + 2u * off, k);
        dn_brk_render(&dn_bk[1], lr + 2u * off, k);
        dn_seq_run(&dn_sq, k, &DN_SQ_CB);
        off += k;
    }
    for (i = 0; i < 2u * n; i++)                    /* the master: a soft knee at the top, nothing ever wraps */
        lr[i] = lr[i] > 24576 || lr[i] < -24576 ? (dn_softclip(lr[i] * 4 / 3) * 3) / 4 : lr[i];
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
        reese_note_on(&dn_rs, d1, d2, dn_rs.gate);  /* overlapping notes slide, as a mono synth does */
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
/* the loops BREAKS can play: the built-in ones, then every loop uploaded to the three sample slots, in order */
static uint32_t dn_src_count(void)
{
    uint32_t n = DN_BRK_BUILTIN;
    int k;
    for (k = 0; k < PLAT_NSLOTS; k++)
        n += (uint32_t)plat_slot_zones(k);
    return n;
}
static int dn_src_get(uint32_t i, dn_loop_t *L)
{
    int k, z;
    uint32_t j;
    for (j = 0; j < sizeof L->name; j++)
        L->name[j] = 0;
    if (i < DN_BRK_BUILTIN) {
        L->adpcm = DN_BRK_BUILTIN_LOOPS[i].adpcm;
        L->n = DN_BRK_BUILTIN_LOOPS[i].n;
        L->rate = DN_BRK_BUILTIN_LOOPS[i].rate;
        L->bars = DN_BRK_BUILTIN_LOOPS[i].bars;
        for (j = 0; DN_BRK_BUILTIN_LOOPS[i].name[j] && j < 8u; j++)
            L->name[j] = DN_BRK_BUILTIN_LOOPS[i].name[j];
        return 1;
    }
    i -= DN_BRK_BUILTIN;
    for (k = 0; k < PLAT_NSLOTS; k++)
        for (z = 0; z < plat_slot_zones(k); z++, i--)
            if (i == 0u) {
                char nm[9];
                uint32_t num = 0, d;
                L->adpcm = plat_slot(k, z, &L->n, &L->rate, nm);
                L->bars = dn_brk_bars_of(L->n, L->rate);
                for (d = 0; d < (uint32_t)k; d++)   /* named as the breaks page numbers them: BR1, BR2 ... */
                    num += (uint32_t)plat_slot_zones((int)d);
                num += (uint32_t)z + 1u;
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
    return 0;
}
/* BREAKS k plays loop src (main loop): the audio lets go of it first, the new loop is read, the speed set */
static void dn_brk_select(uint32_t k, uint32_t src)
{
    dn_loop_t L;
    if (src >= dn_src_count() || !dn_src_get(src, &L)) {
        src = k % DN_BRK_BUILTIN;                   /* gone (the slots were rewritten): a built-in loop */
        dn_src_get(src, &L);
    }
    dn_lock();
    dn_bk[k].ok = 0;
    dn_bk[k].on = 0;
    dn_unlock();
    dn_brk_load(&dn_bk[k], &L);
    dn_brk_tempo(&dn_bk[k], dn_sq.bpm);
    dn_bk_src[k] = (uint8_t)src;
}
/* the sample slots are about to change (an upload): nothing may read them until dn_app_slots_changed(1) */
void dn_app_slots_changed(int done)
{
    uint32_t k;
    for (k = 0; k < DN_NBRK; k++)
        if (dn_bk_src[k] >= DN_BRK_BUILTIN) {
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
static uint32_t dn_bsel(void) { return dn.trk == 2u ? 1u : 0u; }   /* the BRK page shows B with track 3 selected */

/* ------------------------------------------------------------------------------------------------ values --- */
static int32_t dn_get(uint32_t id)
{
    if (id == DN_PX_BPM)
        return dn_rs.bpm;
    if (id == DN_PX_OCT)
        return dn.oct;
    if (id == DN_PX_BSRC)
        return dn_bk_src[dn_bsel()];
    if (id == DN_PX_BPITCH)
        return dn_bk[dn_bsel()].pitch;
    if (id == DN_PX_BDECAY)
        return dn_bk[dn_bsel()].decay;
    if (id == DN_PX_BCRUSH)
        return dn_bk[dn_bsel()].crush;
    return dn_rs.p[id];
}
static int32_t dn_min(uint32_t id)
{
    return id == DN_PX_BPM ? 60 : id == DN_PX_OCT ? -3 : id == DN_PX_BPITCH ? -24 : id >= DN_PX_BSRC ? 0
         : reese_param_min(id);
}
static int32_t dn_max(uint32_t id)
{
    return id == DN_PX_BPM ? 200 : id == DN_PX_OCT ? 3 : id == DN_PX_BSRC ? (int32_t)dn_src_count() - 1
         : id == DN_PX_BPITCH ? 24 : id >= DN_PX_BDECAY ? 127 : reese_param_max(id);
}
static const char *dn_name(uint32_t id)
{
    static const char *const BN[4] = {"LOOP", "PITCH", "DECAY", "CRUSH"};
    return id == DN_PX_BPM ? "BPM" : id == DN_PX_OCT ? "OCT" : id >= DN_PX_BSRC ? BN[id - DN_PX_BSRC]
         : reese_param_name(id);
}
static void dn_text(uint32_t id, int32_t v, char out[8])
{
    if (id == DN_PX_BSRC) {
        dn_loop_t L;
        uint32_t i;
        if (!dn_src_get((uint32_t)v, &L))
            L.name[0] = '?', L.name[1] = 0;
        for (i = 0; i < 7u && L.name[i]; i++)
            out[i] = L.name[i];
        out[i] = 0;
        return;
    }
    if (id == DN_PX_BDECAY && v >= 127) {
        out[0] = 'H', out[1] = 'O', out[2] = 'L', out[3] = 'D', out[4] = 0;
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
    else if (id == DN_PX_BPM)
        dn_rs.bpm = dn_sq.bpm = (uint16_t)v;
    else if (id == DN_PX_OCT)
        dn.oct = (int8_t)v;
    else
        dn_rs.p[id] = (int16_t)v;
    dn_unlock();
    if (id == DN_PX_BPM || id == DN_PX_BPITCH)      /* the breaks' speed follows the tempo */
        for (k = 0; k < DN_NBRK; k++)
            dn_brk_tempo(&dn_bk[k], dn_sq.bpm);
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}

static void dn_load_preset(uint32_t i)
{
    dn_lock();
    reese_load_preset(&dn_rs, i);
    dn_unlock();
    dn.preset = (uint8_t)i;
    dn.dirty = 0;
    dn.redraw |= DN_RD_ALL;
}

static void dn_save(void)
{
    dn_save_t s;
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
        s.brk[k].src = dn_bk_src[k];
        s.brk[k].pitch = dn_bk[k].pitch;
        s.brk[k].decay = dn_bk[k].decay;
        s.brk[k].crush = dn_bk[k].crush;
    }
    if (plat_store_save(OBJ_SOUND, &s, sizeof s) == 0 && plat_store_save(OBJ_PAT0, &dn_sq.pat, sizeof dn_sq.pat) == 0)
        dn.dirty = 0;
    dn.redraw |= DN_RD_ALL;
}

void dn_app_init(void)
{
    dn_save_t s;
    uint32_t k;
    int got;
    reese_init(&dn_rs);
    for (k = 0; k < DN_NBRK; k++) {
        dn_brk_init(&dn_bk[k]);
        dn_bk_src[k] = (uint8_t)k;                  /* A: ROLLER, B: STEPPER */
        dn_bk[k].decay = 127;
    }
    dn_seq_init(&dn_sq);
    dn.sounding = dn.midi_note = 0xFF;
    dn.step_held = -1;
    if (plat_store_load(OBJ_PAT0, &dn_sq.pat, sizeof dn_sq.pat) != (int)sizeof dn_sq.pat || !dn_pattern_valid(&dn_sq.pat))
        dn_pattern_demo(&dn_sq.pat);                /* first boot (or a pattern from another firmware): the demo */
    dn.redraw = DN_RD_ALL;
    got = plat_store_load(OBJ_SOUND, &s, sizeof s);
    if ((got == (int)sizeof s || got == (int)DN_SAVE_V04) && s.magic == DN_SAVE_MAGIC) {
        dn.preset = (uint8_t)(s.preset % reese_preset_count());
        dn.page = (uint8_t)(s.page % DN_NPAGES);
        dn.oct = (int8_t)dn_clamp(s.oct, -3, 3);
        dn_rs.bpm = (uint16_t)dn_clamp(s.bpm, 60, 200);
        dn_sq.bpm = dn_rs.bpm;
        for (k = 0; k < RP_COUNT; k++)
            dn_rs.p[k] = (int16_t)dn_clamp(s.p[k], reese_param_min(k), reese_param_max(k));
        for (k = 0; got == (int)sizeof s && k < DN_NBRK; k++) {
            dn_bk_src[k] = s.brk[k].src;
            dn_bk[k].pitch = (int8_t)dn_clamp(s.brk[k].pitch, -24, 24);
            dn_bk[k].decay = (uint8_t)dn_clamp(s.brk[k].decay, 0, 127);
            dn_bk[k].crush = (uint8_t)dn_clamp(s.brk[k].crush, 0, 127);
        }
    }
    dn_sq.bpm = dn_rs.bpm;
    for (k = 0; k < DN_NBRK; k++)
        dn_brk_select(k, dn_bk_src[k]);
    dn.btn_prev = plat_buttons();                   /* what is held at start-up is no press */
    dn.keys_prev = plat_keys();
    dn.ready = 1;
}

/* ------------------------------------------------------------------------------------------------- input --- */
static uint32_t dn_slices(uint32_t t) { return DN_IS_BRK(t) && dn_bk[t - 1u].nslices ? dn_bk[t - 1u].nslices : 16u; }
/* what key k plays: a note (MIDI number); on a BREAKS track a slice, the octave moving 16 slices (a bar) */
static uint8_t dn_key_note(uint32_t k)
{
    if (DN_IS_BRK(dn.trk)) {
        int32_t n = (int32_t)dn_slices(dn.trk), v = ((int32_t)k + 16 * dn.oct) % n;
        return (uint8_t)(v < 0 ? v + n : v);
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
    dn.dirty = 1;
    dn.redraw |= DN_RD_ALL;
}

/* a note from the keys: track 1 is REESE, 2 and 3 a slice of BREAKS A / B, the others MIDI on their channel */
static void dn_play(uint8_t note, int legato)
{
    dn_lock();
    if (DN_IS_BRK(dn.trk)) {
        dn_brk_trigger(&dn_bk[dn.trk - 1u], note, 100, 0);
    } else if (dn.trk == 0u) {
        reese_note_on(&dn_rs, note, 100, legato);
    } else {
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
        dn_brk_release(&dn_bk[dn.trk - 1u]);
    else if (dn.trk != 0u && dn.sounding != 0xFF)
        plat_midi_out(0x08u | (0x80u | dn_sq.pat.tr[dn.trk].chan) << 8 | (uint32_t)dn.sounding << 16);
    else if (dn.trk == 0u && dn.midi_note == 0xFF)
        reese_note_off(&dn_rs);
    dn_unlock();
    dn.sounding = 0xFF;
}

/* REC: a played note into the selected track. Playing: the step now, or the next one if the note came in its second
 * half (the nearest). Stopped: at the cursor, which moves on. Played under another key: a slide. */
static void dn_rec_note(uint8_t note, int legato)
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
    tr->st[i].flags = (uint8_t)(DN_ST_ON | (legato && !DN_IS_BRK(dn.trk) ? DN_ST_SLIDE : 0u));
    tr->st[i].len = DN_IS_BRK(dn.trk) ? DN_LEN_STEP : 3;
    tr->st[i].vel = 100;
    tr->last_note = note;
    dn_unlock();
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
        }
        if ((down & (1u << k)) && dn.nheld < NKEYS) {
            int legato = dn.nheld > 0u;             /* under a held key: a slide */
            dn_play(dn_key_note(k), legato || dn.play);
            if (dn.rec)
                dn_rec_note(dn_key_note(k), legato);
            dn.held[dn.nheld++] = (uint8_t)k;
        }
    }
    if (up && dn.nheld == 0u && !dn.play && dn.sounding != 0xFF)
        dn_stop();
    else if (up && dn.nheld && !DN_IS_BRK(dn.trk) && dn.sounding != dn_key_note(dn.held[dn.nheld - 1u]))
        dn_play(dn_key_note(dn.held[dn.nheld - 1u]), 1);       /* back to the last key still held */
    else if (up && !dn.nheld && dn.play && !DN_IS_BRK(dn.trk))
        dn_play(dn_key_note(0), 1);
}

static void dn_keys_seq(uint32_t keys)              /* the SEQ page: white keys are steps, black keys functions */
{
    uint32_t down = keys & ~dn.keys_prev, up = dn.keys_prev & ~keys, k;
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    for (k = 0; k < NKEYS; k++) {
        int32_t o = dn_key_ord(k);
        if (dn_is_white(k)) {
            uint32_t i = (uint32_t)o + 16u * dn.view;
            if ((down & (1u << k)) && i < tr->len) {
                dn.step_held = (int8_t)i;
                dn.step_edited = 0;
                if (!(tr->st[i].flags & DN_ST_ON)) {
                    dn_lock();
                    tr->st[i].flags |= DN_ST_ON;
                    tr->st[i].len = 3;              /* a fresh step: three quarters of a step (breaks: a whole one) */
                    if (DN_IS_BRK(dn.trk)) {         /* a break: the slice that belongs on this step, whole */
                        tr->st[i].note = (uint8_t)(i % dn_slices(dn.trk));
                        tr->st[i].len = DN_LEN_STEP;
                    } else {
                        tr->st[i].note = tr->last_note;
                    }
                    dn_unlock();
                    dn.step_edited = 1;             /* just turned on: letting go keeps it */
                    dn_mark();
                }
            }
            if ((up & (1u << k)) && dn.step_held == (int8_t)i) {
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
            if (o < 8) {                            /* mutes */
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

static void dn_keys(uint32_t keys)
{
    if (!dn.keys_prev)                              /* the keys follow the page they were pressed on */
        dn.key_page_seq = dn.page == DN_PG_SEQ;
    if (dn.key_page_seq)
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

static void dn_seq_knob(uint32_t k, int32_t d)      /* KNOB 1..4 on the SEQ page */
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    if (dn.step_held >= 0) {                        /* a step held: its note, length, accent, slide */
        dn_step_t *st = &tr->st[dn.step_held];
        dn_lock();
        if (k == 0u)
            st->note = (uint8_t)dn_clamp(st->note + d, 0, DN_IS_BRK(dn.trk) ? (int32_t)dn_slices(dn.trk) - 1 : 127),
            tr->last_note = st->note;
        else if (k == 1u)
        {                                           /* a quarter step at a time while short, coarser when long */
            int32_t l = st->len, n = d < 0 ? -d : d;
            while (n--)
                l = dn_clamp(l + (d < 0 ? -1 : 1) * (l < 8 ? 1 : l < 32 ? 2 : 4), 1, DN_LEN_MAX);
            st->len = (uint8_t)l;
        }
        else if (k == 2u)
            st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_ACC : st->flags & ~DN_ST_ACC);
        else
            st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_SLIDE : st->flags & ~DN_ST_SLIDE);
        dn_unlock();
        dn.step_edited = 1;
        dn_mark();
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

static void dn_goto(uint32_t page)
{
    if (dn.page != page) {
        dn.page = (uint8_t)page;
        dn.step_held = -1;
        dn.redraw |= DN_RD_BOTTOM;
    }
}

void dn_app_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), press = btn & ~dn.btn_prev, k;
    int32_t d;
    dn.btn_prev = btn;
    if (keys != dn.keys_prev)
        dn_keys(keys);
    if (dn.clear_t0 && plat_ms() - dn.clear_t0 > 1000u) {   /* black key 9 held 1 s: the track's steps cleared */
        dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        dn_lock();
        for (k = 0; k < DN_SEQ_STEPS; k++)
            tr->st[k].flags = 0;
        dn_unlock();
        dn.clear_t0 = 0;
        dn_mark();
    }
    if ((btn & (1u << B_OCTDN)) && (btn & (1u << B_OCTUP)))
        press &= ~((1u << B_OCTDN) | (1u << B_OCTUP));          /* both: the update-mode hold, not an octave */
    else if (press & (1u << B_OCTDN))
        dn_set(DN_PX_OCT, dn.oct - 1);
    else if (press & (1u << B_OCTUP))
        dn_set(DN_PX_OCT, dn.oct + 1);
    for (k = 0; k < DN_NPAGES; k++)
        if (press & (1u << DN_PAGE_BTN[k]))
            dn_goto(k);
    if (press & (1u << B_SEQ))
        dn_goto(DN_PG_SEQ);
    if (press & (1u << B_ARP)) {                    /* BRK: BREAKS A (or B, with track 3 selected) */
        if (!DN_IS_BRK(dn.trk))
            dn.trk = 1;
        dn_goto(DN_PG_BRK);
        dn.redraw |= DN_RD_ALL;
    }
    if (press & (1u << B_SAVE))
        dn_save();
    if (press & (1u << B_PLAY)) {                   /* the transport */
        dn_lock();
        if (dn_sq.playing) {
            dn_seq_stop(&dn_sq, &DN_SQ_CB);
            for (k = 0; k < DN_NBRK; k++)
                dn_brk_cut(&dn_bk[k]);              /* (a HOLD break would play on to the end of its loop) */
        } else
            dn_seq_start(&dn_sq, &DN_SQ_CB);
        dn_unlock();
        dn.redraw |= DN_RD_ALL;
    }
    if (press & (1u << B_REC)) {
        dn.rec = (uint8_t)!dn.rec;
        dn.rec_cur = 0;
        dn.redraw |= DN_RD_ALL;
    }
    if (press & (1u << B_HOME)) {                   /* hold a note */
        dn.play = (uint8_t)!dn.play;
        if (dn.play && !dn.nheld)
            dn_play(dn_key_note(0), 0);
        else if (!dn.play && !dn.nheld)
            dn_stop();
        dn.redraw |= DN_RD_BOTTOM;
    }
    if ((d = plat_enc(EN_SELECT)) != 0) {
        if (dn.page == DN_PG_SEQ)
            dn_seq_knob(0, d > 0 ? 1 : -1);         /* the track */
        else if (dn.page == DN_PG_BRK) {
            dn.trk = dn.trk == 1u ? 2u : 1u;        /* BREAKS A <-> B */
            dn.redraw |= DN_RD_ALL;
        } else
            dn_goto((uint32_t)(((int32_t)dn.page + d % (int32_t)DN_NPAGES + (int32_t)DN_NPAGES) % (int32_t)DN_NPAGES));
    }
    if ((d = plat_enc(EN_PRESET)) != 0 && dn.page == DN_PG_BRK) {
        dn_set(DN_PX_BSRC, dn_get(DN_PX_BSRC) + (d > 0 ? 1 : -1));   /* on BRK: the loop */
    } else if (d) {
        int32_t n = (int32_t)reese_preset_count();
        dn_load_preset((uint32_t)((((int32_t)dn.preset + d) % n + n) % n));
    }
    if ((d = plat_enc(EN_ALGO)) != 0)
        dn_set(DN_PX_BPM, (int32_t)dn_rs.bpm + d);
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) {
            if (dn.page == DN_PG_SEQ) {
                dn_seq_knob(k, d);
            } else {
                uint32_t id = dn.page == DN_PG_BRK ? DN_BRK_PAGE[k] : DN_PAGE[dn.page][k];
                dn_set(id, dn_get(id) + (id == DN_PX_BSRC ? (d > 0 ? 1 : -1) : dn_accel(dn_max(id) - dn_min(id), d)));
            }
        }
}

/* ---------------------------------------------------------------------------------------------- the screen --- */
/* Massive's look: graphite panels, light-blue values, one knob row; DREADNOUGHT's red only for its name */
#define DN_C_BG RGB(44, 46, 50)
#define DN_C_BAR RGB(24, 25, 28)
#define DN_C_PANEL RGB(62, 65, 70)
#define DN_C_EDGE RGB(88, 92, 98)
#define DN_C_WELL RGB(18, 19, 21)
#define DN_C_GRID RGB(36, 39, 44)
#define DN_C_LABEL RGB(176, 180, 186)
#define DN_C_DIMTXT RGB(120, 124, 130)
#define DN_C_BLUE RGB(98, 200, 255)
#define DN_C_BLUE_D RGB(40, 92, 124)
#define DN_C_TRACK RGB(30, 31, 34)
#define DN_C_KNOB RGB(92, 96, 102)
#define DN_C_KNOB_HI RGB(130, 134, 140)
#define DN_C_RED RGB(232, 58, 46)

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
    int32_t lo = dn_min(id), hi = dn_max(id), v = dn_get(id), span = hi - lo > 0 ? hi - lo : 1;
    int32_t av = -DN_KA + (2 * DN_KA * (v - lo)) / span, a0 = lo < 0 ? 0 : -DN_KA, px, py, qx, qy;
    char t[8];
    dn_arc(cx, cy, 19, 22, -DN_KA, DN_KA, DN_C_TRACK);
    dn_arc(cx, cy, 19, 22, a0, av, focus ? DN_C_BLUE : DN_C_BLUE_D);
    dn_disc(cx, cy, 15, DN_C_KNOB);
    dn_disc(cx - 2, cy - 3, 9, DN_C_KNOB_HI);
    dn_disc(cx, cy, 8, DN_C_KNOB);
    dn_polar(cx, cy, 5, av, &px, &py);
    dn_polar(cx, cy, 14, av, &qx, &qy);
    cv_line(px, py, qx, qy, C_WHITE);
    cv_line(px + 1, py, qx + 1, qy, C_WHITE);
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

/* BRK: the loop's picture, its slices (bars brighter), the slice playing lit and where it has got to */
static void dn_draw_loop(const dn_brk_t *b)
{
    uint32_t i, ns = b->nslices ? b->nslices : 16u;
    cv_rect(4, 50, 232, 42, DN_C_WELL);
    if (!b->ok) {
        cv_text(120 - text_w(&FONT_T, "NO LOOP") / 2, 64, &FONT_T, "NO LOOP", DN_C_DIMTXT);
        return;
    }
    if (b->on)
        cv_rect(4 + (int32_t)(b->slice * 232u / ns), 50, (int32_t)(232u / ns), 42, DN_C_GRID);
    for (i = 1; i < ns; i++)
        cv_rect(4 + (int32_t)(i * 232u / ns), 50, 1, 42, (i % 16u) ? (i % 4u ? DN_C_GRID : DN_C_TRACK) : DN_C_BLUE_D);
    for (i = 0; i < DN_BRK_PEAKS; i++) {
        int32_t h = b->peaks[i] * 20 / 255;
        cv_rect(4 + (int32_t)i, 71 - h, 1, 2 * h + 1, DN_C_BLUE);
    }
    if (b->on && b->loop.n) {
        uint32_t at = b->slice * (b->loop.n / ns) + (b->rev ? 0u : b->played >> 8);
        if (at < b->loop.n)
            cv_rect(4 + (int32_t)(at / (b->loop.n / 232u + 1u)), 50, 1, 42, C_WHITE);
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

static void dn_draw_top(void)
{
    char t[16];
    int32_t x, i, w = (int32_t)dn_scope_w, start, y, py = 0;
    const char *pn = reese_preset_name(dn.preset);
    int brk = dn.page == DN_PG_BRK;
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
    cv_rect(6, 27, 46, 14, DN_C_BLUE);
    if (brk) {                                      /* BRK: which BREAKS, and its loop */
        const char *tag = dn_bsel() ? "BRK B" : "BRK A";
        cv_text(29 - text_w(&FONT_T, tag) / 2, 28, &FONT_T, tag, DN_C_BAR);
        pn = bk->loop.name;
    } else {
        cv_text(29 - text_w(&FONT_T, "REESE") / 2, 28, &FONT_T, "REESE", DN_C_BAR);
    }
    x = cv_text(60, 26, &FONT_S, pn, C_WHITE);
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
            cv_text(px, 27, &FONT_XS, pos, C_WHITE);
            for (i = 0; i < 9; i++)                 /* a play triangle */
                cv_rect(px - 14 + i / 2, 29 + i / 2, 1, 10 - i / 2 * 2, DN_C_BLUE);
        }
        if (dn.rec)
            dn_disc(dn_sq.playing ? px - 24 : 228, 34, 4, DN_C_RED);
    } else {
        cv_text(234 - text_w(&FONT_T, "PRESETS"), 29, &FONT_T, "PRESETS", DN_C_DIMTXT);
    }
    if (brk) {
        dn_draw_loop(bk);
        cv_blit(0, 0);
        return;
    }
    /* the scope: the last ~42 ms, from a rising zero crossing so it stands still */
    cv_rect(4, 50, 232, 42, DN_C_WELL);
    for (i = 1; i < 4; i++)
        cv_rect(4 + i * 58, 50, 1, 42, DN_C_GRID);
    cv_rect(4, 71, 232, 1, DN_C_GRID);
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
        y = 71 - (s * 20) / 14000;
        y = dn_clamp(y, 51, 91);
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
        line[2] = DN_IS_BRK(t) ? 'B' : t ? 'M' : 'R';
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
                cv_rect(x + 3, y + 3, 5, 4, C_WHITE);
            if (dn_sq.playing && dn_sq.cur[t] == si) {     /* the playhead */
                cv_rect(x, y, 11, 1, C_WHITE);
                cv_rect(x, y + 9, 11, 1, C_WHITE);
                cv_rect(x, y, 1, 10, C_WHITE);
                cv_rect(x + 10, y, 1, 10, C_WHITE);
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
            dn_note_name(v, st->note + (dn.trk == 0u ? dn_rs.p[RP_TRANS] : 0));
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
        dn_cat(line, DN_IS_BRK(dn.trk) ? (dn.trk == 1u ? "BRK A" : "BRK B") : dn.trk ? "MIDI " : "REESE");
        if (dn.trk && !DN_IS_BRK(dn.trk)) {
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

static void dn_draw_bottom(void)
{
    uint32_t k;
    int32_t x;
    char t[24];
    cv_begin(240, 144, DN_C_BG);
    /* the page tabs */
    for (k = 0, x = 0; k < DN_NTABS; k++) {
        int32_t w = k == DN_NTABS - 1u ? 240 - x : 34;
        int on = k == dn.page;
        cv_rect(x, 0, w - 1, 17, on ? DN_C_BLUE : DN_C_PANEL);
        cv_text(x + (w - 1 - text_w(&FONT_T, DN_PAGE_NAME[k])) / 2, 3, &FONT_T, DN_PAGE_NAME[k],
                on ? DN_C_BAR : DN_C_LABEL);
        x += w;
    }
    if (dn.page == DN_PG_SEQ) {
        dn_draw_seq();
        cv_blit(0, 96);
        return;
    }
    /* the four dials: KNOB 1..4 */
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    cv_rect(0, 17, 240, 1, DN_C_EDGE);
    for (k = 0; k < 4u; k++)
        dn_knob(30 + 60 * (int32_t)k, 67, dn.page == DN_PG_BRK ? DN_BRK_PAGE[k] : DN_PAGE[dn.page][k], 1);
    /* the status line */
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' ';
    dn_text(DN_PX_OCT, dn.oct, t + 4);
    cv_text(8, 125, &FONT_XS, t, DN_C_LABEL);
    if (dn.play)
        cv_text(70, 125, &FONT_XS, "HOLD", DN_C_BLUE);
    t[0] = 'T'; t[1] = (char)('1' + dn.trk); t[2] = 0;
    cv_text(120, 125, &FONT_XS, t, dn.trk ? DN_C_LABEL : DN_C_DIMTXT);
    if (dn.dirty)
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
    if (dn.page == DN_PG_SEQ && dn_sq.playing && dn_sq.pos != shown_pos)
        dn.redraw |= DN_RD_BOTTOM;                  /* the playhead moved */
    shown_pos = dn_sq.pos;
    dn_draw_top();                                  /* every frame: the scope moves */
    if (dn.redraw & DN_RD_BOTTOM)
        dn_draw_bottom();
    dn.redraw = 0;
    lit |= 1u << (dn.page == DN_PG_SEQ ? B_SEQ : dn.page == DN_PG_BRK ? B_ARP : DN_PAGE_BTN[dn.page]);
    if (dn.dirty)
        lit |= 1u << B_SAVE;
    if (dn_sq.playing)
        lit |= 1u << B_PLAY;
    if (dn.rec)
        lit |= 1u << B_REC;
    if (dn.play)
        lit |= 1u << B_HOME;
    if (dn.page == DN_PG_SEQ) {                     /* the steps that are on, the playhead dim; the tracks playing */
        const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        keys = 0;
        for (k = 0; k < NKEYS; k++) {
            int32_t o = dn_key_ord(k);
            uint32_t si = (uint32_t)o + 16u * dn.view;
            if (dn_is_white(k)) {
                if (si < tr->len && (tr->st[si].flags & DN_ST_ON))
                    keys |= 1u << k;
                else if (dn_sq.playing && dn_sq.cur[dn.trk] == si)
                    dim |= 1u << k;
            } else if (o < 8 && !dn_sq.pat.tr[o].mute) {
                keys |= 1u << k;
            }
        }
    }
    plat_keys_dim(dim);
    plat_leds(lit, keys);
}
