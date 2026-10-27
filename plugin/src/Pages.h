// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the pages. SOUND (the track's instrument, every value), PATTERN (its 8 patterns, a piano roll or
// a drum grid, each step's details), EFFECTS (FX A, FX B, every track's sends), SAMPLES (your samples: drop files in,
// hear them, name them, take them out). Copyright (C) 2026 Jamie (TwistedGenetics).
#pragma once
#include "Widgets.h"

namespace dn
{
class Page : public juce::Component
{
public:
    explicit Page(Ui& u) : ui(u) {}
    virtual void rebuild() = 0;                      // (the track or its instrument changed)
    virtual void poll() = 0;                         // (15 times a second: what changed elsewhere)
protected:
    Ui& ui;
};

class SlicesView;
class PadsView;
class SoundPage : public Page
{
public:
    explicit SoundPage(Ui& u);
    ~SoundPage() override;
    void rebuild() override;
    void poll() override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    FlowView flow;
    bool empty = false;
    std::unique_ptr<SlicesView> slices;
    std::unique_ptr<PadsView> pads;
    int builtFor = -1, builtMode = -1;
};

class FxPage : public Page
{
public:
    explicit FxPage(Ui& u);
    void rebuild() override;
    void poll() override;
    void resized() override;
private:
    FlowView flow;
    std::unique_ptr<juce::Component> sends;
    int fxA = -1, fxB = -1;
};

class PatternGrid;
class StepInspector;
class PatternPage : public Page
{
public:
    explicit PatternPage(Ui& u);
    ~PatternPage() override;
    void rebuild() override;
    void poll() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    int selectedStep = -1;
    void stepChanged();
private:
    std::array<std::unique_ptr<juce::TextButton>, 8> patBtn;
    std::unique_ptr<FieldControl> lenField, swingField;
    std::unique_ptr<PatternGrid> grid;
    std::unique_ptr<StepInspector> inspector;
};

class SamplesPage : public Page, public juce::FileDragAndDropTarget
{
public:
    explicit SamplesPage(Ui& u);
    ~SamplesPage() override;
    void rebuild() override;
    void poll() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void addFiles(const juce::StringArray& files);   // (also the tests)
    void remove(int i);
    void play(int i);
private:
    struct Row;
    juce::Viewport view;
    juce::Component list;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton addBtn { "ADD SAMPLES..." };
    juce::ComboBox rate, fold;
    juce::String status;
    bool dragOver = false;
    uint32_t shown = 0xFFFFFFFFu, usedKB = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
