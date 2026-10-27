// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the processor. The C core (the FM-1's app and engines) runs on the audio thread at
// 44.1 kHz, resampled to the host's rate; the editor's presses and turns reach it through a queue, and its screen
// comes back as a copy. Copyright (C) 2026 Jamie (TwistedGenetics).
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

class DreadnoughtProcessor : public juce::AudioProcessor
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

    // ---- for the editor (the message thread) ----
    enum EvKind : uint8_t { evButton, evKey, evTurn, evAllUp };
    void post(EvKind kind, int id, int value);       // a press, a key, a turn: the audio thread takes it next block
    bool active() const { return owner; }            // (one per process: the core keeps its state in globals)
    // the screen: copied when it changed since `serial` (true), 240 x 240 ARGB
    bool screen(uint32_t& serial, juce::Image& into);
    uint32_t ledsButtons() const { return ledBtn.load(); }
    uint32_t ledsKeys() const { return ledKeys.load(); }
    uint32_t ledsKeysDim() const { return ledDim.load(); }
    std::atomic<bool> syncToHost { true };           // the sequencer starts and stops with the host, at its tempo
    void idleTick();                                 // the editor's timer: the panel and screen while the host is quiet

    // (0.45) the web editor over the plugin's MIDI port ("DREADNOUGHT Plugin": macOS, Linux)
    enum LinkState { linkNone, linkReady, linkTalking };
    LinkState linkState() const;
    juce::String linkPortName() const;
    uint32_t slotsKB() const { return slotsUsed.load(); }
    void editorFrame(const uint8_t* d, int n);       // (the port's thread) a frame from the editor
    int editorReply(uint8_t* out, int max);          // (the port's thread) an answer to send

private:
    void applyEvents();
    void serveEditor();                              // (the core's lock held) the editor's frames answered
    void publishScreen();
    void renderChunk();

    bool owner = false;
    juce::SpinLock coreLock;                          // the core: the audio thread (try) and state save / load

    struct Ev { uint8_t kind; int8_t id; int16_t value; };
    juce::AbstractFifo fifo { 512 };
    std::array<Ev, 512> evs {};
    juce::SpinLock postLock;                          // (several posting threads would be rare: kept safe anyway)

    juce::SpinLock screenLock;
    std::vector<uint32_t> screenPx;
    std::atomic<uint32_t> screenSerial { 0 };
    uint32_t lastCoreSerial = 0xFFFFFFFFu;
    std::atomic<uint32_t> ledBtn { 0 }, ledKeys { 0 }, ledDim { 0 };

    // the resampler: the core's frames (44.1 kHz) to the host's rate, cubic (Hermite)
    double step = 1.0, pos = 1.0;
    std::vector<float> srcL, srcR;
    std::array<float, 64> chunk {};
    std::atomic<double> lastProcessMs { 0.0 };
    double idleLastMs = 0.0;

    FrameFifo edIn { 1 << 18 }, edOut { 1 << 18 };
    std::atomic<double> lastFrameMs { -1.0e9 };
    std::atomic<uint32_t> slotsUsed { 0 };
    std::unique_ptr<EditorLink> link;
};
