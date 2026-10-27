// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the processor. The C core (the FM-1's app and engines) runs on the audio thread at
// 44.1 kHz, resampled to the host's rate. The GUI reads and changes it through withCore() (the core's lock, held for
// microseconds); the DAW automates the instruments' main values; the web editor reaches it through a MIDI port.
// Copyright (C) 2026 Jamie (TwistedGenetics).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

// frames of bytes through a lock-free ring (one thread writes, one reads): each a length (2 bytes), then its bytes
class FrameFifo
{
public:
    explicit FrameFifo(int size) : fifo(size), buf((size_t)size) {}
    bool push(const uint8_t* d, int n);
    int pop(uint8_t* out, int max);                  // a frame's length (0: none)
    bool empty() const { return fifo.getNumReady() == 0; }
private:
    void put(const uint8_t* d, int n);
    void get(uint8_t* d, int n);
    juce::AbstractFifo fifo;
    std::vector<uint8_t> buf;
};

class EditorLink;

class DreadnoughtProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DreadnoughtProcessor();
    ~DreadnoughtProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;              // (the double one: JUCE's, unused)

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DREADNOUGHT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- for the GUI (the message thread) ----
    bool active() const { return owner; }            // (one per process: the core keeps its state in globals)
    template <class F> auto withCore(F&& f)          // f() with the core to itself (keep it short: microseconds)
    {
        const juce::SpinLock::ScopedLockType l(coreLock);
        return f();
    }
    std::atomic<bool> syncToHost { true };           // the sequencer starts and stops with the host, at its tempo
    void idleTick();                                 // the GUI's timer: the core kept going while the host is quiet
    void preview(std::vector<float> mono, double rate);   // a sample heard (the samples page), over the output
    uint32_t revision() const { return rev.load(); } // changes when a state was loaded (the GUI reads everything again)

    // the web editor over the plugin's MIDI port ("DREADNOUGHT Plugin": macOS, Linux)
    enum LinkState { linkNone, linkReady, linkTalking };
    LinkState linkState() const;
    juce::String linkPortName() const;
    void editorFrame(const uint8_t* d, int n);       // (the port's thread) a frame from the editor
    int editorReply(uint8_t* out, int max);          // (the port's thread) an answer to send

private:
    void serveEditor();                              // (the core's lock held) the editor's frames answered
    void renderChunk();
    void applyAutomation();                          // (the core's lock held) the DAW's moves into the core
    void timerCallback() override;                   // the core's values out to the DAW's parameters

    bool owner = false;
    juce::SpinLock coreLock;

    // the resampler: the core's frames (44.1 kHz) to the host's rate, cubic (Hermite)
    double hostRate = 44100.0, step = 1.0, pos = 1.0;
    std::vector<float> srcL, srcR;
    std::array<float, 64> chunk {};
    std::atomic<double> lastProcessMs { 0.0 };
    double idleLastMs = 0.0;
    std::atomic<uint32_t> rev { 1 };

    FrameFifo edIn { 1 << 18 }, edOut { 1 << 18 };
    std::atomic<double> lastFrameMs { -1.0e9 };
    std::unique_ptr<EditorLink> link;

    struct Auto
    {
        int engine;
        uint32_t id;
        int32_t lo, hi;
        juce::AudioParameterFloat* p;
        std::atomic<int32_t> applied;
    };
    std::vector<std::unique_ptr<Auto>> autos;

    juce::SpinLock previewLock;
    std::vector<float> pv;
    double pvPos = 0.0, pvStep = 1.0;
};
