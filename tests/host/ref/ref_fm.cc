// The FM reference player (tests only): Dexed's MSFA as Dexed's PluginProcessor plays it. C entry points for fm_test.c.
#include <stdint.h>
#include <string.h>
#include <memory>
#include "msfa/synth.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/env.h"
#include "msfa/lfo.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/controllers.h"
#include "msfa/fm_core.h"
#define private public                              /* (the test resets a note's feedback memory) */
#include "msfa/dx7note.h"
#undef private
#include "msfa/tuning.h"

struct StdTuning : public TuningState {
    int32_t midinote_to_logfreq(int n) override { return 50857777 + ((1 << 24) / 12) * n; }
};
void dexed_trace(const char *, const char *, ...) {}

static FmCore core;
static Controllers ctrls;
static std::shared_ptr<TuningState> tun;

extern "C" void ref_fm_init(void)
{
    Sin::init();
    Exp2::init();
    Tanh::init();
    Freqlut::init(44100.0);
    Lfo::init(44100.0);
    PitchEnv::init(44100.0);
    Env::init_sr(44100.0);
    Porta::init_sr(44100.0);
    tun = std::make_shared<StdTuning>();
    memset(&ctrls, 0, sizeof ctrls);
    ctrls.pitch_cc = 0x2000;
    ctrls.pitch_range_up = ctrls.pitch_range_dn = 2;
    ctrls.masterTune = 0;
    ctrls.mpeEnabled = false;
    ctrls.modwheel_cc = ctrls.breath_cc = ctrls.foot_cc = ctrls.aftertouch_cc = 0;
    ctrls.refresh();
    ctrls.core = &core;
}
/* sins and exp2s as MSFA made them (the generated tables are checked against these) */
extern "C" const int32_t *ref_fm_sintab(void) { return sintab; }
extern "C" const int32_t *ref_fm_exp2tab(void) { return exp2tab; }
extern int32_t lut[];
extern "C" const int32_t *ref_fm_freqlut(void) { return lut; }
extern "C" int32_t ref_fm_freq(int32_t lf) { return Freqlut::lookup(lf); }

/* notes[i] (0: none) start at blocks on[i] and end at off[i]; out: n samples, as Dexed's float x 32768 */
extern "C" void ref_fm_play(const uint8_t *patch, int nnotes, const int *notes, const int *vels, const int *on,
                            const int *off, int n, int16_t *out)
{
    static Dx7Note *v[16];
    static bool live[16], key[16];
    static int vnote[16];
    static int64_t vseq[16];
    int64_t seq = 0;
    int i, b, nb = n / N, cur = 0;
    Lfo lfo;
    memset((void *)&lfo, 0, sizeof lfo);            /* (Dexed's is a member of its processor: zeroed) */
    for (i = 0; i < 16; i++) {
        if (!v[i]) v[i] = new Dx7Note(tun, nullptr);
        live[i] = key[i] = false, vnote[i] = -1, vseq[i] = 0;
        v[i]->oscSync();
        v[i]->fb_buf_[0] = v[i]->fb_buf_[1] = 0;
    }
    lfo.reset(patch + 137);
    for (b = 0; b < nb; b++) {
        for (i = 0; i < nnotes; i++) {
            if (off[i] == b) {
                for (int k = 0; k < 6; k++)
                    if (key[k] && vnote[k] == notes[i]) { key[k] = false; v[k]->keyup(); break; }
            }
            if (on[i] == b) {                       /* (PluginProcessor::keydown with 6 voices) */
                int pitch = notes[i] + patch[144] - 24, best = cur, bs = -1, k = cur;
                bool trig = true;
                for (int j = 0; j < 6; j++) {
                    if (key[k]) trig = false;
                    int sc = (!(live[k] && v[k]->isPlaying()) ? 4 : 0) + (!key[k] ? 2 : 0) + (vnote[k] == notes[i] ? 1 : 0);
                    if (sc > bs || (sc == bs && vseq[k] < vseq[best])) best = k, bs = sc;
                    k = (k + 1) % 6;
                }
                if (trig) lfo.keydown();
                cur = (best + 1) % 6;
                bool steal = live[best] && v[best]->isPlaying();
                v[best]->init(patch, pitch, vels[i], 1, &ctrls);
                if (patch[136] && !steal) v[best]->oscSync();
                else if (!patch[136])               /* (the same pitch playing: its phases) */
                    for (int k2 = 0; k2 < 6; k2++)
                        if (k2 != best && live[k2] && v[k2]->isPlaying() && vnote[k2] == notes[i]) {
                            v[best]->transferPhase(*v[k2]);
                            break;
                        }
                live[best] = key[best] = true, vnote[best] = notes[i], vseq[best] = ++seq;
            }
        }
        int32_t lv = lfo.getsample(), ld = lfo.getdelay();
        float sum[N] = {0};
        for (i = 0; i < 6; i++) {
            if (!live[i]) continue;
            int32_t buf[N] = {0};
            v[i]->compute(buf, lv, ld, &ctrls);
            for (int j = 0; j < N; j++) {
                int32_t val = buf[j] >> 4;
                int c = val < -(1 << 24) ? -0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
                sum[j] += (float)c / 32768.f;
            }
        }
        for (int j = 0; j < N; j++) {
            float x = sum[j] * 32768.f;
            out[b * N + j] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
    }
}
