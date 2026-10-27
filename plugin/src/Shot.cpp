// SPDX-License-Identifier: GPL-3.0-only
// dn_shot: the plugin's editor drawn to a PNG, with no host (the tests, the manual). The processor plays at 48 kHz
// for a while (past the splash) and the panel is worked as the steps say:
//   dn_shot OUT.png [STEP ...]   STEP: ms:N  tap:BUTTON (0..13)  key:K (0..26)  turn:ENC:N (0..6)
//                                note:CH:NOTE
// It also checks the sound (a note at 48 kHz, through the resampler: heard, no NaN) and the state (saved, loaded).
// Copyright (C) 2026 Jamie (TwistedGenetics).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
float peak = 0.0f;
bool bad = false;
void play(DreadnoughtProcessor& p, int ms, juce::MidiBuffer* midi = nullptr)
{
    juce::AudioBuffer<float> b(2, 480);
    juce::MidiBuffer none;
    for (int i = 0; i < ms / 10; ++i) {
        juce::MidiBuffer& m = (midi != nullptr && i == 0) ? *midi : none;
        p.processBlock(b, m);
        none.clear();
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < b.getNumSamples(); ++s) {
                const float v = b.getSample(c, s);
                if (std::isnan(v) || std::abs(v) > 1.0f)
                    bad = true;
                peak = juce::jmax(peak, std::abs(v));
            }
    }
}
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: dn_shot OUT.png [ms:N tap:B turn:E:N note:CH:NOTE ...]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    int fails = 0;
    {
        DreadnoughtProcessor proc;
        proc.prepareToPlay(48000.0, 480);
        play(proc, 1600);                           // (past the splash)
        peak = 0.0f;
        {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)110), 100);
            play(proc, 300, &m);
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 45), 0);
            play(proc, 400, &off);
        }
        std::printf("%s a note at 48 kHz: peak %.3f\n", peak > 0.05f && !bad ? "  ok  " : "  FAIL", peak);
        fails += !(peak > 0.05f && !bad);
        {
            juce::MemoryBlock st, st2, st3;         // (the state: saved and loaded twice, the same the second time;
            proc.getStateInformation(st);           //  the first may differ: a start leaves a SEQ page for HOME's)
            proc.setStateInformation(st.getData(), (int)st.getSize());
            play(proc, 1600);
            proc.getStateInformation(st2);
            proc.setStateInformation(st2.getData(), (int)st2.getSize());
            play(proc, 1600);
            proc.getStateInformation(st3);
            const bool same = st2 == st3 && st.getSize() > 100 && st.getSize() == st3.getSize();
            std::printf("%s the state: %d bytes, saved and loaded the same\n", same ? "  ok  " : "  FAIL",
                        (int)st.getSize());
            fails += !same;
        }
        for (int i = 2; i < argc; ++i) {
            const auto a = juce::StringArray::fromTokens(argv[i], ":", "");
            if (a[0] == "ms")
                play(proc, a[1].getIntValue());
            else if (a[0] == "tap") {
                proc.post(DreadnoughtProcessor::evButton, a[1].getIntValue(), 1);
                play(proc, 60);
                proc.post(DreadnoughtProcessor::evButton, a[1].getIntValue(), 0);
                play(proc, 60);
            } else if (a[0] == "key") {
                proc.post(DreadnoughtProcessor::evKey, a[1].getIntValue(), 1);
                play(proc, 60);
                proc.post(DreadnoughtProcessor::evKey, a[1].getIntValue(), 0);
                play(proc, 60);
            } else if (a[0] == "turn") {
                proc.post(DreadnoughtProcessor::evTurn, a[1].getIntValue(), a[2].getIntValue());
                play(proc, 60);
            } else if (a[0] == "note") {
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(a[1].getIntValue(), a[2].getIntValue(), (juce::uint8)100), 0);
                play(proc, 200, &m);
            }
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* e = static_cast<DreadnoughtEditor*>(ed.get());
        e->refreshNow();
        const auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat png;
        if (!os.openedOk() || !png.writeImageToStream(img, os)) {
            std::printf("  FAIL could not write %s\n", argv[1]);
            ++fails;
        }
        ed.reset();
        proc.releaseResources();
    }
    std::printf(fails ? "PLUGIN SHOT FAILED\n" : "PLUGIN SHOT: ok\n");
    return fails != 0;
}
