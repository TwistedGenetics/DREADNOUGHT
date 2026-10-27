// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the editor (0.45), a normal instrument's: the eight tracks on the left, the selected one's
// SOUND / PATTERN / EFFECTS / SAMPLES pages, presets, tempo and transport at the top. Vector-drawn at any size.
// Copyright (C) 2026 Jamie (TwistedGenetics).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Pages.h"
#include "PluginProcessor.h"

class DreadnoughtEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit DreadnoughtEditor(DreadnoughtProcessor&);
    ~DreadnoughtEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;

    static constexpr int baseW = 1280, baseH = 800;
    // (dn_shot, the tests) a page and a track shown now, everything read now
    enum Tab { tabSound, tabPattern, tabFx, tabSamples };
    void show(int track, Tab tab);
    void refreshNow();
    dn::SamplesPage& samplesPage();

private:
    void timerCallback() override;
    struct Root;
    DreadnoughtProcessor& proc;
    dn::Look look;
    std::unique_ptr<Root> root;
};
