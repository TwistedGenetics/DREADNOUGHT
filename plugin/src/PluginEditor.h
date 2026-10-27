// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the editor, laid out as the FM-1 (and the web editor's panel): the three encoders and
// OCT on the left, the screen, the four knobs and the two rows of buttons on the right, the 27 keys below.
// Copyright (C) 2026 Jamie (TwistedGenetics).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class DreadnoughtEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DreadnoughtEditor(DreadnoughtProcessor&);
    ~DreadnoughtEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType) override;
    void refreshNow();                              // (dn_shot: the screen and the lights now, not on the timer)

    static constexpr int baseW = 1112, baseH = 724;

private:
    void timerCallback() override;
    DreadnoughtProcessor& proc;
    struct Panel;
    std::unique_ptr<Panel> panel;
    std::array<bool, 27> kbDown {};
};
