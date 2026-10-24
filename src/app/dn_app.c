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
#include <stddef.h>

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
#define DN_IS_SNDPG(p) ((p) < DN_NPAGES || (p) == DN_PG_SET)
#define DN_ROLL_ROWS 12                             /* the piano roll's pitches in view (an octave) */
#define DN_DRUM_ROWS 8                              /* the drum grid's slices in view */
#define DN_NBRK 2u                                  /* BREAKS A and B: tracks 2 and 3 */
#define DN_IS_BRK(t) ((t) == 1u || (t) == 2u)
#define DN_KEY0 53                                  /* key 0 is F3 */
#define DN_PX_BPM 100                               /* the OUT page's two non-engine dials */
#define DN_PX_OCT 101
#define DN_PX_OVL 102                               /* OUT: REESE's OVERLAP (SLIDE / CUT) */
#define DN_PX_CLICK 103                             /* SETUP: the click (OFF / REC / ON), its level, the count-in */
#define DN_PX_CLKLVL 104
#define DN_PX_CIN 105
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
    {RP_FENV, RP_GLIDE, RP_LRATE, RP_LFLT},
    {RP_LEVEL, RP_TRANS, DN_PX_BPM, DN_PX_OVL},
};
static const uint8_t DN_PAGE_BTN[DN_NPAGES] = {B_SEL, B_EDIT, B_ENV, B_LFO, B_GLO};
static const uint8_t DN_SET_PAGE[4] = {DN_PX_CLICK, DN_PX_CLKLVL, DN_PX_CIN, DN_PX_OCT};
#define DN_NSND 6u                                  /* the sound pages, in SELECT's order (the tab row's too) */
static const uint8_t DN_SND[DN_NSND] = {0, 1, 2, 3, 4, DN_PG_SET};
static const char *const DN_SND_NAME[DN_NSND] = {"OSC", "FILTER", "AMP", "MOD", "OUT", "SETUP"};

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
} dn_save_t;
#define DN_SAVE_V08 (offsetof(dn_save_t, set))
#define DN_SAVE_V05 (DN_SAVE_V08 - DN_NBRK * 4u)
#define DN_SAVE_V04 (DN_SAVE_V08 - DN_NBRK * 8u)

static reese_t dn_rs;
static dn_seq_t dn_sq;
static uint32_t dn_rev;                             /* moves on whenever the sound or the pattern changes (the editor) */
static dn_brk_t dn_bk[DN_NBRK] X0X_POOL;
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
    uint8_t key_mode;                               /* ... and ROLL's mode then */
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
    const char *msg;                                /* a word on the status line (a refusal, what happened) ... */
    uint32_t msg_t;                                 /* ... shown until this time (ms) */
    uint8_t home_done;
    uint8_t save_held, save_chord;                  /* SAVE down; it was used with OCT (copy / paste): no save */
    uint8_t clip_ok;
    dn_step_t clip[16];                             /* copy / paste: one bar of one track */
    uint8_t rec_step[NKEYS];                        /* REC: where each key's note went, and when it went down */
    uint32_t rec_tick[NKEYS];
    uint8_t rec_last;                               /* the step the last held note was written to (the host test) */
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

/* the sequencer's notes: track 1 plays REESE, 2 and 3 BREAKS A / B (the note: the slice; the step's slide: reverse),
 * the others MIDI out on their channel (USB-MIDI event packets) */
static void dn_sq_on(uint32_t t, uint8_t note, uint8_t vel, int slide, uint8_t flags)
{
    if (t == 0u)
        reese_note_on(&dn_rs, note, vel, slide);
    else if (DN_IS_BRK(t))
        dn_brk_hit(t - 1u, note, vel, (flags & DN_ST_SLIDE) != 0);
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
        if (k > n - off)
            k = n - off;
        reese_render(&dn_rs, lr + 2u * off, k);
        dn_brk_render(&dn_bk[0], lr + 2u * off, k);
        dn_brk_render(&dn_bk[1], lr + 2u * off, k);
        dn_click_render(lr + 2u * off, k);
        if (dn.cin_left) {
            dn.cin_next -= k;
            dn.cin_left -= k;
            if (!dn.cin_left)
                dn_seq_start(&dn_sq, &DN_SQ_CB);   /* (its first step on the next sample: bar 1, beat 1) */
        } else {
            dn_seq_run(&dn_sq, k, &DN_SQ_CB);
        }
        if (dn_sq.playing && dn_sq.pos != dn.clk_last) {   /* a step began just now: on a beat, the click */
            uint32_t st = dn_sq.pos - 1u;
            dn.clk_last = dn_sq.pos;
            if (st % 4u == 0u && (dn.click == 2u || (dn.click == 1u && dn.rec)))
                dn_click_start(st % 16u == 0u);
        }
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
/* MANUAL slices, kept per loop (by its identity, dn_brk_key) in their own storage object, so the sample slots stay in
 * X0X's format and other firmwares still read them. Up to DN_MAN_N loops; the oldest one is forgotten first. */
#define DN_MAN_MAGIC 0x31534E44u                    /* "DNS1" */
#define DN_MAN_N 24u
typedef struct {
    uint32_t key;
    uint8_t n, pad[3];
    uint16_t st[DN_BRK_MAXSL];                      /* where each slice starts, in 4-sample units */
} dn_man_t;
static struct {
    uint32_t magic, count;
    dn_man_t t[DN_MAN_N];
} dn_man X0X_POOL;
typedef char dn_man_fits[sizeof dn_man <= PLAT_STORE_MAX ? 1 : -1];

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
static void dn_say(const char *m);
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
        static dn_save_t sv;
        if (plat_store_load(OBJ_SOUND, &sv, sizeof sv) == (int)sizeof sv && sv.magic == DN_SAVE_MAGIC) {
            for (k = 0; k < DN_NBRK; k++)
                if (dn_bk[k].key == key)
                    sv.brk2[k].smode = dn_bk[k].smode;
            plat_store_save(OBJ_SOUND, &sv, sizeof sv);
        }
    }
    return plat_store_save(OBJ_SONG, &dn_man, sizeof dn_man) ? 3 : 0;
}

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
    dn_man_restore(&dn_bk[k]);                      /* (the slices set by hand for this loop, if any) */
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

/* the dial under KNOB k on this page */
static uint32_t dn_dial(uint32_t k)
{
    return dn.page == DN_PG_BRK ? DN_BRK_PAGE[k] : dn.page == DN_PG_BRK2 ? DN_BRK2_PAGE[k]
         : dn.page == DN_PG_SLICE ? DN_SLICE_PAGE[k] : dn.page == DN_PG_SET ? DN_SET_PAGE[k] : DN_PAGE[dn.page % DN_NPAGES][k];
}

/* ------------------------------------------------------------------------------------------------ values --- */
static int32_t dn_get(uint32_t id)
{
    if (id == DN_PX_BPM)
        return dn_rs.bpm;
    if (id == DN_PX_OCT)
        return dn.oct;
    if (id == DN_PX_OVL)
        return DN_PAT_CUT(&dn_sq.pat) & 1u;
    if (id == DN_PX_CLICK)
        return dn.click;
    if (id == DN_PX_CLKLVL)
        return dn.clk_lvl;
    if (id == DN_PX_CIN)
        return dn.cin;
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
    if (id >= DN_PX_SLSEL)
        return 0;
    return id == DN_PX_BPM ? 60 : id == DN_PX_OVL || (id >= DN_PX_CLICK && id <= DN_PX_CIN) ? 0 : id == DN_PX_OCT ? -3 : id == DN_PX_BPITCH ? -24 : id >= DN_PX_BSRC ? 0
         : reese_param_min(id);
}
static int32_t dn_max(uint32_t id)
{
    if (id >= DN_PX_SLSEL)
        return id == DN_PX_SLSEL ? (int32_t)dn_bk[dn_bsel()].nslices - 1 : id == DN_PX_SLADD ? 6
             : (int32_t)dn_bk[dn_bsel()].loop.n;
    return id == DN_PX_BPM ? 200 : id == DN_PX_OVL ? 1 : id == DN_PX_CLICK || id == DN_PX_CIN ? 2
         : id == DN_PX_CLKLVL ? 127 : id == DN_PX_OCT ? 3 : id == DN_PX_BSRC ? (int32_t)dn_src_count() - 1
         : id == DN_PX_BPITCH ? 24 : id == DN_PX_BCHOKE ? 4 : id == DN_PX_BSMODE || id == DN_PX_BSTRETCH ? 2
         : id >= DN_PX_BDECAY ? 127 : reese_param_max(id);
}
static const char *dn_name(uint32_t id)
{
    static const char *const BN[8] = {"LOOP", "PITCH", "DECAY", "CRUSH", "CHOKE", "SLICES", "STRETCH", "LEVEL"};
    static const char *const SN[4] = {"CHOP", "CURSOR", "FINE", "ZOOM"};
    if (id >= DN_PX_SLSEL)
        return SN[id - DN_PX_SLSEL];
    return id == DN_PX_BPM ? "BPM" : id == DN_PX_OVL ? "OVERLAP" : id == DN_PX_CLICK ? "CLICK"
         : id == DN_PX_CLKLVL ? "CLK LVL" : id == DN_PX_CIN ? "COUNT-IN" : id == DN_PX_OCT ? "OCT" : id >= DN_PX_BSRC ? BN[id - DN_PX_BSRC]
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
    if (id == DN_PX_OVL) {
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
    else if (id == DN_PX_OVL)
        DN_PAT_CUT(&dn_sq.pat) = (uint8_t)((DN_PAT_CUT(&dn_sq.pat) & ~1u) | (v ? 1u : 0u));
    else if (id == DN_PX_CLICK)
        dn.click = (uint8_t)v;
    else if (id == DN_PX_CLKLVL)
        dn.clk_lvl = (uint8_t)v;
    else if (id == DN_PX_CIN)
        dn.cin = (uint8_t)v;
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
        s.brk[k].src = dn_bk_src[k];
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
    s.set.cin = dn.cin;
    s.set.pad = 0;
#undef s
}

static void dn_save(void)
{
    dn_save_t s;
    dn_save_fill(&s);
    dn_man.magic = DN_MAN_MAGIC;
    if (plat_store_save(OBJ_SOUND, &s, sizeof s) == 0 && plat_store_save(OBJ_PAT0, &dn_sq.pat, sizeof dn_sq.pat) == 0 &&
        plat_store_save(OBJ_SONG, &dn_man, sizeof dn_man) == 0)
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
        dn_bk[k].choke = 1;                         /* A and B choke each other: one sampler voice, as a break is */
    }
    dn_seq_init(&dn_sq);
    dn_sq.legato = 0xF9u;                           /* overlaps slide on REESE and MIDI, not on the breaks */
    dn.sounding = dn.midi_note = 0xFF;
    dn.step_held = -1;
    dn.entry_len = DN_LEN_STEP;
    dn.click = 1;                                   /* the click while recording; one bar's count-in */
    dn.clk_lvl = 70;
    dn.cin = 1;
    for (k = 0; k < NKEYS; k++)
        dn.rec_step[k] = 0xFF;
    if (plat_store_load(OBJ_PAT0, &dn_sq.pat, sizeof dn_sq.pat) != (int)sizeof dn_sq.pat || !dn_pattern_valid(&dn_sq.pat))
        dn_pattern_demo(&dn_sq.pat);                /* first boot (or a pattern from another firmware): the demo */
    dn.redraw = DN_RD_ALL;
    if (plat_store_load(OBJ_SONG, &dn_man, sizeof dn_man) != (int)sizeof dn_man || dn_man.magic != DN_MAN_MAGIC ||
        dn_man.count > DN_MAN_N)
        dn_man.magic = dn_man.count = 0;            /* (no manual slices yet) */
    got = plat_store_load(OBJ_SOUND, &s, sizeof s);
    if ((got == (int)sizeof s || got == (int)DN_SAVE_V08 || got == (int)DN_SAVE_V05 || got == (int)DN_SAVE_V04) &&
        s.magic == DN_SAVE_MAGIC) {
        dn.preset = (uint8_t)(s.preset % reese_preset_count());
        dn.page = (uint8_t)(s.page % DN_NPAGES);
        dn.oct = (int8_t)dn_clamp(s.oct, -3, 3);
        dn_rs.bpm = (uint16_t)dn_clamp(s.bpm, 60, 200);
        dn_sq.bpm = dn_rs.bpm;
        for (k = 0; k < RP_COUNT; k++)
            dn_rs.p[k] = (int16_t)dn_clamp(s.p[k], reese_param_min(k), reese_param_max(k));
        if (got == (int)sizeof s) {
            dn.click = (uint8_t)dn_clamp(s.set.click, 0, 2);
            dn.clk_lvl = (uint8_t)dn_clamp(s.set.clk_lvl, 0, 127);
            dn.cin = (uint8_t)dn_clamp(s.set.cin, 0, 2);
        }
        for (k = 0; got >= (int)DN_SAVE_V08 && k < DN_NBRK; k++) {
            dn_bk[k].choke = (uint8_t)dn_clamp(s.brk2[k].choke, 0, 4);
            dn_bk[k].smode = (uint8_t)dn_clamp(s.brk2[k].smode, 0, 2);
            dn_bk[k].stretch = (uint8_t)dn_clamp(s.brk2[k].stretch, 0, 2);
            dn_bk[k].level = (uint8_t)dn_clamp(s.brk2[k].level, 0, 127);
        }
        for (k = 0; got >= (int)DN_SAVE_V05 && k < DN_NBRK; k++) {
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
    if (DN_IS_BRK(dn.trk)) {                        /* the keys are the chops in order, 27 to a shift: every
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
        dn_brk_hit(dn.trk - 1u, note, 100, 0);
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

/* where the playhead is, in quarter steps since PLAY (under the lock: the audio moves it) */
static uint32_t dn_tick_now(void)
{
    uint32_t st = dn_sq.pos ? dn_sq.pos - 1u : 0u;
    int32_t q = dn_seq_step_q8(&dn_sq, st), into = q - dn_sq.until_step;
    return st * DN_LEN_STEP + (uint32_t)dn_clamp(into / (q / (int32_t)DN_LEN_STEP + 1), 0, (int32_t)DN_LEN_STEP - 1);
}

/* REC: a played note into the selected track. Playing: the step now, or the next one if the note came in its second
 * half (the nearest). Stopped: at the cursor, which moves on. Played under another key: a slide. */
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
    tr->st[i].flags = (uint8_t)(DN_ST_ON | (legato && !DN_IS_BRK(dn.trk) ? DN_ST_SLIDE : 0u));
    tr->st[i].len = DN_IS_BRK(dn.trk) ? DN_LEN_STEP : 3;
    tr->st[i].vel = 100;
    tr->last_note = note;
    dn_unlock();
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
    if (!DN_IS_BRK(dn.trk) && (tr->st[i].flags & DN_ST_ON))
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
            if (dn.rec && dn_sq.playing)
                dn_rec_release(k);
        }
        if ((down & (1u << k)) && dn.nheld < NKEYS) {
            int legato = dn.nheld > 0u;             /* under a held key: a slide */
            dn_play(dn_key_note(k), legato || dn.play);
            if (dn.page == DN_PG_SLICE && DN_IS_BRK(dn.trk) && dn_key_note(k) != 0xFFu) {
                dn_brk_t *bk = &dn_bk[dn.trk - 1u];  /* chop mode: the key selects its chop, heard to its end */
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
                uint32_t at = dn_rec_note(dn_key_note(k), legato);
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
    else if (up && dn.nheld && !DN_IS_BRK(dn.trk) && dn.sounding != dn_key_note(dn.held[dn.nheld - 1u]))
        dn_play(dn_key_note(dn.held[dn.nheld - 1u]), 1);       /* back to the last key still held */
    else if (up && !dn.nheld && dn.play && !DN_IS_BRK(dn.trk))
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
            if ((down & (1u << k)) && i < tr->len && dn.step_held >= 0 && dn.step_key != (int8_t)k) {
                dn_step_drop(i);                    /* a step held, another pressed: the note goes there */
                continue;
            }
            if ((down & (1u << k)) && i < tr->len) {
                dn.step_held = (int8_t)i;
                dn.step_key = (int8_t)k;
                dn.step_edited = 0;
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
                        const dn_brk_t *bk = &dn_bk[dn.trk - 1u];
                        uint32_t gs = dn_brk_grid_slices(bk);     /* (AUTO / MANUAL: the hit nearest the step) */
                        tr->st[i].note = (uint8_t)(drum ? dn.drum_row : dn_brk_slice_at(bk, i % gs, gs));
                        tr->st[i].len = DN_LEN_STEP;
                    } else {
                        tr->st[i].note = tr->last_note;
                    }
                    dn_unlock();
                    dn.step_edited = 1;             /* just turned on: letting go keeps it */
                    dn_mark();
                }
            }
            if ((up & (1u << k)) && dn.step_held >= 0 && dn.step_key == (int8_t)k) {
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
            dn_lock();
            tr->st[i].note = note;
            tr->st[i].len = dn.entry_len;
            tr->st[i].vel = 100;
            tr->st[i].flags = (uint8_t)(DN_ST_ON | (legato ? DN_ST_SLIDE : 0u));
            tr->last_note = note;
            dn_unlock();
            dn_play(note, legato);
            dn.roll_cur = (uint8_t)((i + (adv ? adv : 1u)) % tr->len);
            dn.view = (uint8_t)(dn.roll_cur / 16u);
            dn_mark();
        }
    }
    if (up && !(keys & ((1u << NKEYS) - 1u)))
        dn_stop();
}

static void dn_keys(uint32_t keys)
{
    if (!dn.keys_prev) {                            /* the keys follow the page they were pressed on */
        dn.key_page = dn.page;
        dn.key_mode = dn.roll_keys;
    }
    if (dn.key_page == DN_PG_ROLL && dn.key_mode)
        dn_keys_entry(keys);
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
    if (dn.page == DN_PG_ROLL && !DN_IS_BRK(dn.trk)) {
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
    if (!copy)
        tr->st[from] = (b.flags & DN_ST_ON) ? b : (dn_step_t){a.note, 0, a.len, a.vel};
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

static void dn_seq_knob(uint32_t k, int32_t d)      /* KNOB 1..4 on the SEQ page */
{
    dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    if (dn.step_held >= 0) {                        /* a step held: its note, length, accent, slide */
        dn_step_t *st = &tr->st[dn.step_held];
        dn_lock();
        if (k == 0u) {
            st->note = (uint8_t)dn_clamp(st->note + d, 0, DN_IS_BRK(dn.trk) ? (int32_t)dn_slices(dn.trk) - 1 : 127);
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
            st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_ACC : st->flags & ~DN_ST_ACC);
        else
            st->flags = (uint8_t)(d > 0 ? st->flags | DN_ST_SLIDE : st->flags & ~DN_ST_SLIDE);
        dn_unlock();
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
        if (page == DN_PG_ROLL) {                   /* ROLL edits a synth or MIDI track; DRUM a break track */
            if (DN_IS_BRK(dn.trk))
                dn.trk = 0;
            dn_roll_fit();
        } else if (page == DN_PG_DRUM && !DN_IS_BRK(dn.trk)) {
            dn.trk = 1;
        }
        if (page == DN_PG_DRUM)
            dn.drum_row = (uint8_t)dn_clamp(dn.drum_row, 0, (int32_t)dn_slices(dn.trk) - 1);
        if (page == DN_PG_SLICE) {                  /* chop mode: the selection in range, the cursor on it */
            dn_sl_fix();
            dn.sl_cur = dn_bk[dn_bsel()].start[dn.sl_sel];
            dn_sl_show(&dn_bk[dn_bsel()]);
        } else if (dn_bk[0].aud || dn_bk[1].aud) {  /* leaving it: the loop preview stops */
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
#define DN_TAP_LATENCY_MS 12u                       /* what is heard is this far behind the decoder: a tap's place */

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
    for (t = 1; t <= DN_NBRK; t++) {
        const dn_brk_t *b = &dn_bk[t - 1u];
        dn_track_t *tr = &dn_sq.pat.tr[t];
        if (b->key != key || !b->ok)
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
    static dn_save_t sv;
    uint32_t k;
    if (!dn.chop_t || (int32_t)(plat_ms() - dn.chop_t) < 3000)
        return;
    dn.chop_t = 0;
    dn_man.magic = DN_MAN_MAGIC;
    plat_store_save(OBJ_SONG, &dn_man, sizeof dn_man);
    if (plat_store_load(OBJ_SOUND, &sv, sizeof sv) == (int)sizeof sv && sv.magic == DN_SAVE_MAGIC) {
        for (k = 0; k < DN_NBRK; k++)
            sv.brk2[k].smode = dn_bk[k].smode;
        plat_store_save(OBJ_SOUND, &sv, sizeof sv);
    } else {                                        /* nothing saved yet: the sound as it is, so MANUAL comes back */
        dn_save_fill(&sv);
        plat_store_save(OBJ_SOUND, &sv, sizeof sv);
    }
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

void dn_app_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), press = btn & ~dn.btn_prev, k;
    int32_t d;
    dn.btn_prev = btn;
    if (keys != dn.keys_prev)
        dn_keys(keys);
    dn_chops_flush();
    if (dn.page == DN_PG_SLICE) {                   /* chop mode's buttons (before the usual ones take them) */
        dn_brk_t *bk = &dn_bk[dn_bsel()];
        if (press & (1u << B_REC)) {                /* CHOP: at the cursor, or where the loop is playing */
            uint32_t at = dn.sl_cur;
            if (bk->aud) {
                uint32_t lat = (bk->loop.rate ? bk->loop.rate : 22050u) * DN_TAP_LATENCY_MS / 1000u, pos;
                dn_lock();
                pos = dn_brk_pos(bk);
                dn_unlock();
                at = pos > lat ? pos - lat : 0u;
            }
            dn_chop_at(at, !bk->aud);              /* (tapping along: never a split) */
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
        if (!dn.save_chord)
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
    for (k = 0; k < DN_NPAGES; k++)
        if (press & (1u << DN_PAGE_BTN[k]))
            dn_goto(k == 4u && dn.page == 4u ? DN_PG_SET : k);   /* GLO again: OUT's second page, SETUP */
    if (press & (1u << B_SEQ))                      /* SEQ: the overview, then ROLL, then DRUM, then round again */
        dn_goto(dn.page == DN_PG_SEQ ? DN_PG_ROLL : dn.page == DN_PG_ROLL ? DN_PG_DRUM : DN_PG_SEQ);
    if ((press & (1u << B_FX)) && dn.page == DN_PG_ROLL) {
        dn.roll_keys = (uint8_t)!dn.roll_keys;      /* ROLL: STEP <-> KEYS */
        dn.roll_cur = (uint8_t)(16u * dn.view);
        dn.redraw |= DN_RD_ALL;
    }
    if (press & (1u << B_ARP)) {                    /* BRK: BREAKS A (or B, with track 3 selected) */
        if (!DN_IS_BRK(dn.trk))
            dn.trk = 1;
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
    if (press & (1u << B_REC)) {
        dn.rec = (uint8_t)!dn.rec;
        dn.rec_cur = 0;
        dn.redraw |= DN_RD_ALL;
    }
    if ((press & (1u << B_HOME)) && dn.page == DN_PG_ROLL && dn.roll_keys) {   /* ROLL KEYS: a rest */
        dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
        dn_lock();
        tr->st[dn.roll_cur % tr->len].flags = 0;
        dn_unlock();
        dn.roll_cur = (uint8_t)((dn.roll_cur + 1u) % tr->len);
        dn.view = (uint8_t)(dn.roll_cur / 16u);
        dn_mark();
    } else if (press & (1u << B_HOME)) {            /* hold a note */
        dn.play = (uint8_t)!dn.play;
        if (dn.play && !dn.nheld)
            dn_play(dn_key_note(0), 0);
        else if (!dn.play && !dn.nheld)
            dn_stop();
        dn.redraw |= DN_RD_BOTTOM;
    }
    if ((d = plat_enc(EN_SELECT)) != 0) {
        if (DN_IS_SEQPG(dn.page) && dn.step_held >= 0) {
            dn_step_move(d);                        /* a step held: it moves in time */
        } else if (dn.page == DN_PG_SEQ) {
            dn_seq_knob(0, d > 0 ? 1 : -1);         /* the track */
        } else if (dn.page == DN_PG_ROLL) {         /* the next synth / MIDI track (not the breaks) */
            int32_t t = dn.trk;
            do
                t = (t + (d > 0 ? 1 : -1) + (int32_t)DN_SEQ_TRACKS) % (int32_t)DN_SEQ_TRACKS;
            while (DN_IS_BRK((uint32_t)t));
            dn.trk = (uint8_t)t;
            dn.view = 0;
            dn_roll_fit();
            dn.redraw |= DN_RD_ALL;
        } else if (DN_IS_BRKPG(dn.page) || dn.page == DN_PG_DRUM) {
            dn.trk = dn.trk == 1u ? 2u : 1u;        /* BREAKS A <-> B */
            dn_sl_fix();
            dn.redraw |= DN_RD_ALL;
        } else {                                    /* the sound pages, round */
            int32_t at = 0;
            for (k = 0; k < DN_NSND; k++)
                if (DN_SND[k] == dn.page)
                    at = (int32_t)k;
            at = ((at + d) % (int32_t)DN_NSND + (int32_t)DN_NSND) % (int32_t)DN_NSND;
            dn_goto(DN_SND[at]);
        }
    }
    if ((d = plat_enc(EN_PRESET)) != 0 && (DN_IS_BRKPG(dn.page) || dn.page == DN_PG_DRUM)) {
        dn_set(DN_PX_BSRC, dn_get(DN_PX_BSRC) + (d > 0 ? 1 : -1));   /* on BRK: the loop */
    } else if (d) {
        int32_t n = (int32_t)reese_preset_count();
        dn_load_preset((uint32_t)((((int32_t)dn.preset + d) % n + n) % n));
    }
    if ((d = plat_enc(EN_ALGO)) != 0)
        dn_set(DN_PX_BPM, (int32_t)dn_rs.bpm + d);
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) {
            if (DN_IS_SEQPG(dn.page)) {
                dn_seq_knob(k, d);
            } else if (dn.page == DN_PG_SLICE) {
                dn_slice_knob(k, d);
            } else {
                uint32_t id = dn_dial(k);
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
            cv_rect(x, 50, sel ? 2 : 1, 42, sel ? C_WHITE : DN_C_LABEL);
            dn_num(t, (int32_t)i + 1);
            cv_text(x + 3, 51, &FONT_T, t, sel ? C_WHITE : DN_C_DIMTXT);
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
            cv_rect(4 + (int32_t)(at / per), 50, 1, 42, C_WHITE);
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

static void dn_draw_top(void)
{
    char t[16];
    int32_t x, i, w = (int32_t)dn_scope_w, start, y, py = 0;
    const char *pn = reese_preset_name(dn.preset);
    int brk = DN_IS_BRKPG(dn.page) || dn.page == DN_PG_DRUM;
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
    } else if (dn.page == DN_PG_ROLL && dn.trk) {   /* ROLL on a MIDI track */
        char tag[8] = "MIDI ";
        tag[5] = (char)('1' + dn_sq.pat.tr[dn.trk].chan % 9u);
        tag[6] = 0;
        cv_text(29 - text_w(&FONT_T, tag) / 2, 28, &FONT_T, tag, DN_C_BAR);
        pn = "TRACK";
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
            cv_rect(px, 50, 2, 9, C_WHITE);
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
            cv_rect(202, 47, 34, 15, flash == 2 ? C_WHITE : DN_C_BLUE);
        if (dn_sq.playing || dn.cin_left)
            cv_text(219 - text_w(&FONT_S, pos) / 2, 47, &FONT_S, pos, flash ? DN_C_BAR : C_WHITE);
        else
            cv_text(219 - text_w(&FONT_T, pos) / 2, 50, &FONT_T, pos, DN_C_LABEL);
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
        cv_rect(4 + (int32_t)(dn_sq.cur[dn.trk] * 232u / L), 50, 1, 42, C_WHITE);
}

/* ROLL: a piano roll of the selected track: pitch up the screen (an octave in view, KNOB 1 scrolls), the 16 steps in
 * view across, every note a bar as long as it lasts, slides a ramp from the note before, accents bright */
static void dn_draw_roll(void)
{
    const dn_track_t *tr = &dn_sq.pat.tr[dn.trk];
    uint32_t i, r, v0 = 16u * dn.view, L = tr->len;
    int32_t trans = dn.trk == 0u ? dn_rs.p[RP_TRANS] : 0, prev_y = -1, prev_end = -1, prev_i = 0;
    uint32_t prev_len = 0, legato = (dn_sq.legato & ~DN_PAT_CUT(&dn_sq.pat)) >> dn.trk & 1u;
    char line[48], v[8];
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    for (r = 0; r < DN_ROLL_ROWS; r++) {            /* the rows: a keyboard down the side, black keys' rows darker */
        uint32_t p = dn.roll_lo + r, pc = (uint32_t)((int32_t)p + trans + 120) % 12u;
        int32_t y = 112 - 8 * (int32_t)r, black = (0x54Au >> pc) & 1u;
        cv_rect(30, y, 208, 8, black ? DN_C_WELL : DN_C_TRACK);
        cv_rect(2, y, 25, 7, black ? DN_C_BAR : DN_C_KNOB);
        if (pc == 0u) {                             /* C: named, and a line under it */
            dn_note_name(v, (int32_t)p + trans);
            cv_text(4, y - 3, &FONT_T, v, DN_C_BAR);
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
            cv_line(x0 - 4, prev_y + 2, x0 + 2, y + 2, C_WHITE);   /* the slide (flagged, or the note before
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
        cv_rect(x0, y, x1 - x0 + 1, 6, (st->flags & DN_ST_ACC) ? DN_C_BLUE : DN_C_BLUE_D);
        cv_rect(x0, y, 2, 6, DN_C_BLUE);            /* its start */
        if ((int32_t)i == dn.step_held) {
            cv_rect(x0, y - 1, x1 - x0 + 1, 1, C_WHITE);
            cv_rect(x0, y + 6, x1 - x0 + 1, 1, C_WHITE);
        }
    }
    if (dn_sq.playing && dn_sq.cur[dn.trk] >= v0 && dn_sq.cur[dn.trk] < v0 + 16u)
        cv_rect(30 + 13 * (int32_t)(dn_sq.cur[dn.trk] - v0), 20, 1, 100, C_WHITE);
    if (dn.roll_keys && dn.roll_cur >= v0 && dn.roll_cur < v0 + 16u)   /* KEYS: the cursor */
        for (r = 0; r < 100u; r += 4u)
            cv_rect(30 + 13 * (int32_t)(dn.roll_cur - v0), 20 + (int32_t)r, 2, 2, DN_C_RED);
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    line[0] = 0;
    if (dn.step_held >= 0) {
        const dn_step_t *st = &tr->st[dn.step_held];
        dn_cat(line, "STEP ");
        dn_num(v, dn.step_held + 1);
        dn_cat(line, v);
        dn_cat(line, "  ");
        dn_note_name(v, st->note + trans);
        dn_cat(line, v);
        dn_cat(line, "  ");
        dn_len_text(v, st->len);
        dn_cat(line, v);
        if (st->flags & DN_ST_ACC)
            dn_cat(line, "  ACC");
        if (st->flags & DN_ST_SLIDE)
            dn_cat(line, "  SLIDE");
        cv_text(8, 125, &FONT_XS, line, DN_C_BLUE);
        return;
    }
    dn_cat(line, dn.roll_keys ? "KEYS  AT " : "STEP  ");
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
        cv_rect(x0, y, x1 - x0 + 1, 10, tr->mute ? DN_C_KNOB : (st->flags & DN_ST_ACC) ? DN_C_BLUE : DN_C_BLUE_D);
        if (st->flags & DN_ST_SLIDE)                /* reversed: an arrow pointing back */
            cv_line(x0 + 7, y + 2, x0 + 3, y + 5, C_WHITE), cv_line(x0 + 3, y + 5, x0 + 7, y + 8, C_WHITE);
        if ((int32_t)i == dn.step_held)
            cv_rect(x0 + 2, y + 3, 4, 4, C_WHITE);
    }
    if (dn_sq.playing && dn_sq.cur[dn.trk] >= v0 && dn_sq.cur[dn.trk] < v0 + 16u)
        cv_rect(30 + 13 * (int32_t)(dn_sq.cur[dn.trk] - v0) + 5, 18, 1, 102, C_WHITE);
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    line[0] = 0;
    dn_cat(line, dn.trk == 1u ? "BRK A  SL " : "BRK B  SL ");
    if (dn.step_held >= 0) {
        const dn_step_t *st = &tr->st[dn.step_held];
        dn_num(v, st->note + 1);
        dn_cat(line, v);
        dn_cat(line, "  ");
        dn_len_text(v, st->len);
        dn_cat(line, v);
        if (st->flags & DN_ST_ACC)
            dn_cat(line, "  ACC");
        if (st->flags & DN_ST_SLIDE)
            dn_cat(line, "  REV");
        cv_text(8, 125, &FONT_XS, line, DN_C_BLUE);
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
static uint32_t dn_tabs(uint8_t *ids, const char **nms)
{
    static const uint8_t SEQ_T[3] = {DN_PG_SEQ, DN_PG_ROLL, DN_PG_DRUM};
    static const char *const SEQ_N[3] = {"OVERVIEW", "ROLL", "DRUM"};
    static const uint8_t BRK_T[3] = {DN_PG_BRK, DN_PG_BRK2, DN_PG_SLICE};
    static const char *const BRK_N[3] = {"BRK", "BRK2", "SLICE"};
    uint32_t k, n = 0;
    if (DN_IS_SEQPG(dn.page))
        for (k = 0; k < 3u; k++)
            ids[n] = SEQ_T[k], nms[n++] = SEQ_N[k];
    else if (DN_IS_BRKPG(dn.page))
        for (k = 0; k < 3u; k++)
            ids[n] = BRK_T[k], nms[n++] = BRK_N[k];
    else
        for (k = 0; k < DN_NSND; k++)
            ids[n] = DN_SND[k], nms[n++] = DN_SND_NAME[k];
    return n;
}

static void dn_draw_bottom(void)
{
    uint32_t k;
    int32_t x;
    char t[24];
    cv_begin(240, 144, DN_C_BG);
    {   /* the page tabs: the pages of this group only (the ones its button / SELECT cycle through) */
        uint8_t ids[DN_MAXTABS];
        const char *nms[DN_MAXTABS];
        uint32_t n = dn_tabs(ids, nms), w;
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
    /* the four dials: KNOB 1..4 */
    cv_rect(0, 17, 240, 104, DN_C_PANEL);
    cv_rect(0, 17, 240, 1, DN_C_EDGE);
    for (k = 0; k < 4u; k++)
        dn_knob(30 + 60 * (int32_t)k, 67, dn_dial(k), 1);
    /* the status line */
    cv_rect(0, 121, 240, 23, DN_C_BAR);
    t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' ';
    dn_text(DN_PX_OCT, dn.oct, t + 4);
    cv_text(8, 125, &FONT_XS, t, DN_C_LABEL);
    if (dn.play)
        cv_text(70, 125, &FONT_XS, "HOLD", DN_C_BLUE);
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
        cv_text(8, 127, &FONT_T, l, C_WHITE);
        if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) > 0)
            cv_text(232 - text_w(&FONT_T, dn.msg), 127, &FONT_T, dn.msg, DN_C_RED);
        else
            cv_text(232 - text_w(&FONT_T, b->aud ? "REC: CHOP HERE" : b->nslices <= 1u ? "CURSOR, THEN REC"
                                                                    : "REC CHOP FX MOVE HOME DEL"),
                    127, &FONT_T, b->aud ? "REC: CHOP HERE" : b->nslices <= 1u ? "CURSOR, THEN REC"
                                                              : "REC CHOP FX MOVE HOME DEL", DN_C_LABEL);
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
    if (DN_IS_SEQPG(dn.page) && dn_sq.playing && dn_sq.pos != shown_pos)
        dn.redraw |= DN_RD_BOTTOM;                  /* the playhead moved */
    shown_pos = dn_sq.pos;
    if (dn.msg && (int32_t)(dn.msg_t - plat_ms()) <= 0) {
        dn.msg = 0;                                 /* a status-line word ran out */
        dn.redraw |= DN_RD_BOTTOM;
    }
    dn_draw_top();                                  /* every frame: the scope moves */
    if (dn.redraw & DN_RD_BOTTOM)
        dn_draw_bottom();
    dn.redraw = 0;
    lit |= 1u << (DN_IS_SEQPG(dn.page) ? B_SEQ : DN_IS_BRKPG(dn.page) ? B_ARP : dn.page == DN_PG_SET ? B_GLO
                  : DN_PAGE_BTN[dn.page]);
    if (dn.page == DN_PG_ROLL && dn.roll_keys)
        lit |= 1u << B_FX;
    if (dn.dirty)
        lit |= 1u << B_SAVE;
    if (dn_sq.playing || (dn.cin_left && (plat_ms() / 125u) % 2u == 0u))
        lit |= 1u << B_PLAY;                        /* (counting in: it blinks) */
    if (dn.rec)
        lit |= 1u << B_REC;
    if (dn.play)
        lit |= 1u << B_HOME;
    if (DN_IS_SEQPG(dn.page) && !(dn.page == DN_PG_ROLL && dn.roll_keys)) {   /* the steps on, the playhead dim; the
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
    if (dn.page == DN_PG_SLICE && DN_IS_BRK(dn.trk)) {   /* chop mode: the keys with a chop dim, the selected lit */
        uint32_t ns = dn_slices(dn.trk);
        for (k = 0; k < NKEYS; k++) {
            uint32_t c = k + 27u * (uint32_t)dn.oct;
            if (dn.oct < 0 || c >= ns)
                continue;
            if (c == dn.sl_sel)
                keys |= 1u << k;
            else
                dim |= 1u << k;
        }
    }
    plat_keys_dim(dim);
    plat_leds(lit, keys);
}

#include "dn_editor.c"                              /* the web editor's commands */
