// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the SAMPLES page. Your samples in the FM-1's three sample slots (224 KB in all, so a project
// made here fits on the FM-1): drop WAV / AIFF / FLAC / MP3 files on the plugin (or ADD SAMPLES...), each converted
// as the web editor converts one (folded to mono, resampled through a low-pass to the rate picked, trimmed, faded,
// 4-bit ADPCM); hear one, rename it, take it out. BREAKS and SAMPLER pick them by their place in the list.
// Copyright (C) 2026 Jamie (TwistedGenetics).
#include <juce_audio_formats/juce_audio_formats.h>
#include "Pages.h"

namespace dn
{
static const int kRates[] = { 44100, 32000, 26040, 22050, 16000, 11025, 8000 };

struct SamplesPage::Row : public juce::Component
{
    Row(SamplesPage& p, int i) : page(p), index(i)
    {
        name.setFont(font(20.0f, 700));
        name.setColour(juce::TextEditor::backgroundColourId, panel);
        name.onReturnKey = [this] { commitName(); };
        name.onFocusLost = [this] { commitName(); };
        addAndMakeVisible(name);
        play.onClick = [this] { page.play(index); };
        del.onClick = [this] { page.remove(index); };
        addAndMakeVisible(play);
        addAndMakeVisible(del);
    }
    void commitName()
    {
        const auto t = name.getText().substring(0, 23);
        page.ui.core([&] { dnc_sample_set_name((uint32_t)index, t.toRawUTF8()); return 0; });
    }
    void resized() override
    {
        name.setBounds(60, 8, 300, 36);
        play.setBounds(getWidth() - 210, 8, 96, 36);
        del.setBounds(getWidth() - 106, 8, 96, 36);
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.0f, 3.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(r, 8.0f, 1.5f);
        g.setColour(ink);
        g.setFont(font(22.0f, 700));
        g.drawText(juce::String(index + 1), 8, 0, 44, getHeight(), juce::Justification::centred);
        g.setFont(font(18.0f, 600));
        g.drawText(info, 376, 0, getWidth() - 376 - 220, getHeight(), juce::Justification::centredLeft);
    }
    SamplesPage& page;
    int index;
    juce::String info;
    juce::TextEditor name;
    juce::TextButton play { "HEAR" }, del { "REMOVE" };
};

SamplesPage::SamplesPage(Ui& u) : Page(u)
{
    addAndMakeVisible(view);
    view.setViewedComponent(&list, false);
    view.setScrollBarsShown(true, false);
    view.setScrollBarThickness(12);
    addBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Samples for DREADNOUGHT", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::canSelectMultipleItems,
                             [this](const juce::FileChooser& fc) {
                                 juce::StringArray f;
                                 for (auto& r : fc.getResults())
                                     f.add(r.getFullPathName());
                                 if (!f.isEmpty())
                                     addFiles(f);
                             });
    };
    addAndMakeVisible(addBtn);
    for (int i = 0; i < 7; ++i)
        rate.addItem(i == 2 ? "26 kHz (SP-1200)" : juce::String(kRates[i] / 1000.0, kRates[i] % 1000 ? 1 : 0) + " kHz", i + 1);
    rate.setSelectedId(4, juce::dontSendNotification);   // (22 kHz: the editor's default)
    fold.addItem("MONO: L + R", 1);
    fold.addItem("MONO: LEFT", 2);
    fold.addItem("MONO: RIGHT", 3);
    fold.addItem("MONO: SIDE", 4);
    fold.setSelectedId(1, juce::dontSendNotification);
    for (auto* c : { &rate, &fold }) {
        c->setColour(juce::ComboBox::backgroundColourId, panel);
        c->setColour(juce::ComboBox::textColourId, ink);
        c->setColour(juce::ComboBox::arrowColourId, ink);
        addAndMakeVisible(c);
    }
}
SamplesPage::~SamplesPage() { rows.clear(); }

void SamplesPage::rebuild()
{
    rows.clear();
    list.removeAllChildren();
    uint32_t n = 0;
    std::vector<juce::String> names, infos;
    ui.core([&] {
        n = dnc_sample_count();
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t len = 0, r = 0;
            int kind = 0;
            dnc_sample(i, &len, &r, &kind);
            names.push_back(dnc_sample_name(i));
            const int loop = dnc_loop_of_sample(i);
            juce::String s = kind == 1 ? "DX7 VOICES (FM)" : kind == 2 ? "WAVETABLE (WAVE)"
                           : juce::String((double)len / juce::jmax(1u, r), 2) + " s   " + juce::String(r / 1000.0, 1) + " kHz";
            s << "   " << juce::String((len + 1) / 2 / 1024.0, 1) << " KB";
            if (loop >= 0)
                s << "   LOOP BR" << (loop + 1);
            infos.push_back(s);
        }
        return 0;
    });
    for (uint32_t i = 0; i < n; ++i) {
        auto r = std::make_unique<Row>(*this, (int)i);
        r->name.setText(names[i], false);
        r->info = infos[i];
        list.addAndMakeVisible(*r);
        rows.push_back(std::move(r));
    }
    shown = n;
    resized();
    repaint();
}

void SamplesPage::poll()
{
    uint32_t n = 0, kb = 0;
    ui.core([&] {
        n = dnc_sample_count();
        kb = dnc_slots_used();
        return 0;
    });
    if (kb != usedKB) {
        usedKB = kb;
        repaint();
    }
    if (n != shown)
        rebuild();
}

void SamplesPage::resized()
{
    addBtn.setBounds(0, 0, 200, 44);
    rate.setBounds(216, 0, 200, 44);
    fold.setBounds(424, 0, 190, 44);
    view.setBounds(0, 116, getWidth(), getHeight() - 116);
    const int w = view.getMaximumVisibleWidth() > 0 ? view.getMaximumVisibleWidth() : getWidth() - 12;
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds(0, (int)i * 58, w, 58);
    list.setSize(w, (int)rows.size() * 58 + 4);
}

void SamplesPage::paint(juce::Graphics& g)
{
    const float room = 224.0f, f = juce::jlimit(0.0f, 1.0f, (float)usedKB / room);
    auto bar = juce::Rectangle<float>(630.0f, 6.0f, (float)getWidth() - 630.0f, 32.0f);
    g.setColour(well);
    g.fillRoundedRectangle(bar, 6.0f);
    g.setColour(f > 0.9f ? hot : tint(hot, 0.6f));
    g.fillRoundedRectangle(bar.withWidth(juce::jmax(8.0f, bar.getWidth() * f)), 6.0f);
    g.setColour(ink);
    g.setFont(font(18.0f, 700));
    g.drawText(juce::String((int)usedKB) + " KB OF 224 KB USED (THE FM-1'S SAMPLE MEMORY)", bar.toNearestInt().reduced(10, 0),
               juce::Justification::centredLeft);
    g.setFont(font(17.0f, 600));
    g.setColour(status.isEmpty() ? soft : ink);
    g.drawFittedText(status.isNotEmpty() ? status
                                         : "DROP AUDIO FILES ANYWHERE ON THE PLUGIN. NEW SAMPLES TAKE THE RATE AND MONO FOLD PICKED ABOVE. "
                                           "BREAKS PLAY THEM AS LOOPS (BR1, BR2 ...), SAMPLER ON ITS PADS AND ZONES.",
                     0, 52, getWidth(), 56, juce::Justification::centredLeft, 2);
    if (dragOver) {
        g.setColour(tint(hot, 0.25f).withAlpha(0.9f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
        g.setColour(ink);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 12.0f, 3.0f);
        g.setFont(font(34.0f, 700));
        g.drawText("DROP TO ADD", getLocalBounds(), juce::Justification::centred);
    }
}

void SamplesPage::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    addFiles(files);
}

void SamplesPage::addFiles(const juce::StringArray& files)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const int to = kRates[juce::jlimit(0, 6, rate.getSelectedId() - 1)], how = fold.getSelectedId();
    std::vector<std::vector<uint8_t>> enc;
    std::vector<uint32_t> lens;
    std::vector<juce::String> names;
    juce::StringArray bad;
    for (auto& path : files) {
        juce::File f(path);
        std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
        if (!rd || rd->lengthInSamples <= 0 || rd->sampleRate <= 0) {
            bad.add(f.getFileName());
            continue;
        }
        const int n = (int)juce::jmin<juce::int64>(rd->lengthInSamples, (juce::int64)(rd->sampleRate * 60.0));
        juce::AudioBuffer<float> b((int)juce::jmax(1u, rd->numChannels), n);
        rd->read(&b, 0, n, 0, true, true);
        std::vector<float> mono((size_t)n);
        const float* L = b.getReadPointer(0);
        const float* R = b.getNumChannels() > 1 ? b.getReadPointer(1) : L;
        for (int i = 0; i < n; ++i)
            mono[(size_t)i] = how == 2 ? L[i] : how == 3 ? R[i] : how == 4 ? (L[i] - R[i]) * 0.5f : (L[i] + R[i]) * 0.5f;
        std::vector<uint8_t> out((size_t)((int64_t)n * to / (int64_t)rd->sampleRate / 2 + 16));
        const uint32_t m = dnc_convert(mono.data(), (uint32_t)n, (uint32_t)rd->sampleRate, (uint32_t)to, 1, out.data(), (uint32_t)out.size());
        if (m < 2) {
            bad.add(f.getFileName());
            continue;
        }
        out.resize((m + 1) / 2);
        enc.push_back(std::move(out));
        lens.push_back(m);
        names.push_back(f.getFileNameWithoutExtension().toUpperCase().substring(0, 23));
    }
    int rc = 0;
    uint32_t n0 = 0;
    if (!enc.empty())
        ui.core([&] {
            n0 = dnc_sample_count();
            std::vector<dnc_part_t> parts(n0 + enc.size());
            for (uint32_t i = 0; i < n0; ++i) {
                int kind;
                parts[i].adpcm = dnc_sample(i, &parts[i].n, &parts[i].rate, &kind);
                parts[i].bytes = (parts[i].n + 1) / 2;
            }
            for (size_t k = 0; k < enc.size(); ++k)
                parts[n0 + k] = { enc[k].data(), (uint32_t)enc[k].size(), lens[k], (uint32_t)to };
            std::vector<std::vector<uint8_t>> keep;          // (the old ones copied: the slots are rewritten)
            keep.reserve(n0);
            for (uint32_t i = 0; i < n0; ++i) {
                keep.emplace_back(parts[i].adpcm, parts[i].adpcm + parts[i].bytes);
                parts[i].adpcm = keep.back().data();
            }
            rc = dnc_samples_store(parts.data(), (uint32_t)parts.size());
            if (rc == 0)
                for (size_t k = 0; k < names.size(); ++k)
                    dnc_sample_set_name(n0 + (uint32_t)k, names[k].toRawUTF8());
            return 0;
        });
    status = rc != 0 ? juce::String("THAT DOESN'T FIT IN THE 224 KB: PICK A LOWER RATE, OR REMOVE A SAMPLE FIRST.")
           : enc.empty() ? juce::String()
                         : juce::String((int)enc.size()) + (enc.size() == 1 ? " SAMPLE ADDED." : " SAMPLES ADDED.");
    if (!bad.isEmpty())
        status << "  NOT READ: " << bad.joinIntoString(", ");
    rebuild();
}

void SamplesPage::remove(int i)
{
    int rc = 0;
    ui.core([&] {
        const uint32_t n = dnc_sample_count();
        std::vector<dnc_part_t> parts;
        std::vector<std::vector<uint8_t>> keep;
        std::vector<juce::String> names;
        keep.reserve(n);
        for (uint32_t k = 0; k < n; ++k) {
            if ((int)k == i)
                continue;
            dnc_part_t p {};
            int kind;
            p.adpcm = dnc_sample(k, &p.n, &p.rate, &kind);
            p.bytes = (p.n + 1) / 2;
            keep.emplace_back(p.adpcm, p.adpcm + p.bytes);
            p.adpcm = keep.back().data();
            parts.push_back(p);
            names.push_back(dnc_sample_name(k));
        }
        rc = dnc_samples_store(parts.data(), (uint32_t)parts.size());
        if (rc == 0)
            for (uint32_t k = 0; k < 48; ++k)
                dnc_sample_set_name(k, k < names.size() ? names[k].toRawUTF8() : "");
        return 0;
    });
    status = rc == 0 ? "SAMPLE " + juce::String(i + 1) + " REMOVED." : juce::String("COULDN'T REMOVE IT.");
    rebuild();
}

void SamplesPage::play(int i)
{
    std::vector<float> x;
    uint32_t r = 22050;
    ui.core([&] {
        uint32_t n = 0;
        int kind = 0;
        const uint8_t* a = dnc_sample((uint32_t)i, &n, &r, &kind);
        if (a != nullptr && kind == 0) {
            std::vector<int16_t> s(n);
            dnc_ima_decode(a, n, s.data());
            x.resize(n);
            for (uint32_t k = 0; k < n; ++k)
                x[k] = s[k] / 32768.0f;
        }
        return 0;
    });
    if (!x.empty())
        ui.proc.preview(std::move(x), (double)r);
}
}
