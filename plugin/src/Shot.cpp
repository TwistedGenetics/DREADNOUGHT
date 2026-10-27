// SPDX-License-Identifier: GPL-3.0-only
// dn_shot: the plugin with no host (the tests, the manual). It plays at 48 kHz (through the resampler: a note heard,
// no NaN), saves and loads its state, takes a sample as if dropped on it, and draws its pages to PNGs:
//   dn_shot DIR          DIR/plugin-<page>.png for every page, and the SOUND page of every instrument
// Copyright (C) 2026 Jamie (TwistedGenetics).
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dn_core.h"

namespace
{
float peak = 0.0f;
bool bad = false;
int fails = 0;
void check(const char* what, bool ok)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    fails += !ok;
}
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
void shot(DreadnoughtEditor& e, const juce::File& f)
{
    const auto img = e.createComponentSnapshot(e.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat png;
    if (!os.openedOk() || !png.writeImageToStream(img, os))
        check(("write " + f.getFileName()).toRawUTF8(), false);
}
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: dn_shot DIR\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    dir.createDirectory();
    {
        DreadnoughtProcessor proc;
        proc.prepareToPlay(48000.0, 480);
        play(proc, 300);
        peak = 0.0f;
        {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)110), 100);
            play(proc, 300, &m);
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 45), 0);
            play(proc, 400, &off);
        }
        check("a note at 48 kHz, heard (no NaN)", peak > 0.05f && !bad);
        {
            juce::MemoryBlock st, st2, st3;         // (the state: saved and loaded twice, the same the second time)
            proc.getStateInformation(st);
            proc.setStateInformation(st.getData(), (int)st.getSize());
            play(proc, 200);
            proc.getStateInformation(st2);
            proc.setStateInformation(st2.getData(), (int)st2.getSize());
            play(proc, 200);
            proc.getStateInformation(st3);
            check("the state saved and loaded, the same", st2 == st3 && st.getSize() > 1000);
        }
        {
            const uint8_t info[] = { 0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7 };   // (the web editor's INFO, as the port hands it)
            proc.editorFrame(info, (int)sizeof info);
            play(proc, 30);
            uint8_t r[256];
            const int n = proc.editorReply(r, (int)sizeof r);
            check("the web editor's INFO answered", n > 10 && juce::String((const char*)r + 5, (size_t)juce::jmin(24, n - 5)).contains("PLUGIN"));
        }
        check("the DAW sees the instruments' values as parameters", proc.getParameters().size() > 40);
        {
            auto* p0 = dynamic_cast<juce::AudioParameterFloat*>(proc.getParameters()[0]);   // (REESE's first: CUTOFF)
            int32_t before = 0, after = 0;
            uint16_t ids[8];
            dnc_auto_ids(1, ids);
            proc.withCore([&] { before = dnc_auto_get(1, ids[0]); return 0; });
            p0->setValueNotifyingHost(before > 60 ? 0.1f : 0.9f);
            play(proc, 20);
            proc.withCore([&] { after = dnc_auto_get(1, ids[0]); return 0; });
            check("... automation moves the instrument", p0 != nullptr && after != before);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto& e = *static_cast<DreadnoughtEditor*>(ed.get());
        {
            juce::WavAudioFormat wav;                // (a sample dropped on the plugin)
            const juce::File f = dir.getChildFile("dropped-hit.wav");
            f.deleteFile();
            juce::AudioBuffer<float> b(2, 22050);
            for (int i = 0; i < 22050; ++i) {
                b.setSample(0, i, 0.7f * std::sin(i * 0.07f) * std::exp(-i / 5000.0f));
                b.setSample(1, i, b.getSample(0, i));
            }
            {
                std::unique_ptr<juce::OutputStream> os(new juce::FileOutputStream(f));
                auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions().withSampleRate(44100.0).withNumChannels(2).withBitsPerSample(16));
                if (w != nullptr)
                    w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            }
            uint32_t n0 = 0, n1 = 0;
            juce::String nm;
            proc.withCore([&] { n0 = dnc_sample_count(); return 0; });
            e.filesDropped(juce::StringArray(f.getFullPathName()), 0, 0);
            proc.withCore([&] { n1 = dnc_sample_count(); nm = dnc_sample_name(n0); return 0; });
            check("a WAV dropped on the plugin: a new sample", n1 == n0 + 1 && nm == "DROPPED-HIT");
            f.deleteFile();
        }
        play(proc, 100);
        e.show(0, DreadnoughtEditor::tabSound);
        shot(e, dir.getChildFile("plugin-sound-reese.png"));
        e.show(0, DreadnoughtEditor::tabPattern);
        shot(e, dir.getChildFile("plugin-pattern-reese.png"));
        e.show(1, DreadnoughtEditor::tabSound);
        shot(e, dir.getChildFile("plugin-sound-breaks.png"));
        e.show(1, DreadnoughtEditor::tabPattern);
        shot(e, dir.getChildFile("plugin-pattern-breaks.png"));
        e.show(0, DreadnoughtEditor::tabFx);
        shot(e, dir.getChildFile("plugin-effects.png"));
        e.show(0, DreadnoughtEditor::tabSamples);
        shot(e, dir.getChildFile("plugin-samples.png"));
        const char* const names[] = { "", "reese", "breaks-a", "breaks-b", "midi", "sub", "sampler", "fm", "wave", "juno", "sampler2" };
        for (int en : { 5, 6, 7, 8, 9 }) {           // (the others on track 4, each in turn)
            proc.withCore([&] { dnc_track_set_engine(3, en); return 0; });
            play(proc, 50);
            e.show(3, DreadnoughtEditor::tabSound);
            shot(e, dir.getChildFile(juce::String("plugin-sound-") + names[en] + ".png"));
        }
        proc.withCore([&] { dnc_track_set_engine(3, 6); dnc_track_select(3); dnc_set(dnc_id_special(5), 0, 0, 1); return 0; });   // (SAMPLER: KEYS)
        play(proc, 50);
        e.show(3, DreadnoughtEditor::tabSound);
        shot(e, dir.getChildFile("plugin-sound-sampler-keys.png"));
        proc.withCore([&] { dnc_track_set_engine(3, 7); dnc_set_skin(1); return 0; });   // (the LIGHT skin too)
        play(proc, 50);
        e.refreshNow();
        e.show(3, DreadnoughtEditor::tabSound);
        shot(e, dir.getChildFile("plugin-light-sound-fm.png"));
        e.show(0, DreadnoughtEditor::tabPattern);
        shot(e, dir.getChildFile("plugin-light-pattern-reese.png"));
        {
            int sk = -1;
            juce::MemoryBlock st;                   // (the skin kept with the project)
            proc.getStateInformation(st);
            proc.withCore([&] { dnc_set_skin(0); return 0; });
            proc.setStateInformation(st.getData(), (int)st.getSize());
            proc.withCore([&] { sk = dnc_skin(); return 0; });
            check("the skin is kept with the project", sk == 1);
            proc.withCore([&] { dnc_set_skin(0); return 0; });
        }
        ed.reset();
        proc.releaseResources();
    }
    std::printf(fails ? "PLUGIN SHOT FAILED\n" : "PLUGIN SHOT: ok\n");
    return fails != 0;
}
