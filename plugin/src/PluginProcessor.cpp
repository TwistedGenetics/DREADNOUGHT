// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the processor. Copyright (C) 2026 Jamie (TwistedGenetics).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dn_core.h"

namespace
{
std::atomic<bool> coreTaken { false };             // the core's one instance (its state is in globals)

inline float hermite(const float* x, int i, float t)
{
    const float xm1 = x[i - 1], x0 = x[i], x1 = x[i + 1], x2 = x[i + 2];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
constexpr uint32_t stateMagic = 0x4C504E44u;        // "DNPL"
constexpr int chunkFrames = 32;
}

DreadnoughtProcessor::DreadnoughtProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    owner = !coreTaken.exchange(true);
    screenPx.assign(DNC_W * DNC_H, 0xFFFFFFFFu);
    srcL.reserve(1 << 16);
    srcR.reserve(1 << 16);
    srcL.assign(1, 0.0f);                          // (one frame of history before the first)
    srcR.assign(1, 0.0f);
    if (owner) {
        const juce::SpinLock::ScopedLockType l(coreLock);
        dnc_init();
        publishScreen();
    }
}

DreadnoughtProcessor::~DreadnoughtProcessor()
{
    if (owner)
        coreTaken = false;
}

bool DreadnoughtProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DreadnoughtProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType l(coreLock);
    step = (double)DNC_RATE / (sampleRate > 0.0 ? sampleRate : 44100.0);
    const size_t need = (size_t)((double)juce::jmax(samplesPerBlock, 512) * step) * 2u + 4096u;
    if (srcL.capacity() < need) {
        srcL.reserve(need);
        srcR.reserve(need);
    }
    srcL.assign(1, 0.0f);
    srcR.assign(1, 0.0f);
    pos = 1.0;
}

void DreadnoughtProcessor::post(EvKind kind, int id, int value)
{
    const juce::SpinLock::ScopedLockType l(postLock);
    const auto w = fifo.write(1);
    Ev e { (uint8_t)kind, (int8_t)id, (int16_t)juce::jlimit(-32768, 32767, value) };
    if (w.blockSize1 > 0)
        evs[(size_t)w.startIndex1] = e;
    else if (w.blockSize2 > 0)
        evs[(size_t)w.startIndex2] = e;
}

void DreadnoughtProcessor::applyEvents()           // (the core's lock held)
{
    const auto r = fifo.read(fifo.getNumReady());
    auto apply = [](const Ev& e) {
        switch (e.kind) {
        case evButton: dnc_button(e.id, e.value); break;
        case evKey: dnc_key(e.id, e.value); break;
        case evTurn: dnc_turn(e.id, e.value); break;
        case evAllUp: dnc_all_up(); break;
        default: break;
        }
    };
    for (int i = 0; i < r.blockSize1; ++i)
        apply(evs[(size_t)(r.startIndex1 + i)]);
    for (int i = 0; i < r.blockSize2; ++i)
        apply(evs[(size_t)(r.startIndex2 + i)]);
}

void DreadnoughtProcessor::publishScreen()          // (the core's lock held)
{
    ledBtn = dnc_leds_buttons();
    ledKeys = dnc_leds_keys();
    ledDim = dnc_leds_keys_dim();
    const uint32_t s = dnc_screen_serial();
    if (s == lastCoreSerial)
        return;
    const juce::SpinLock::ScopedTryLockType l(screenLock);
    if (!l.isLocked())
        return;                                    // (the editor is copying it: next time)
    dnc_screen_copy(screenPx.data());
    lastCoreSerial = s;
    ++screenSerial;
}

bool DreadnoughtProcessor::screen(uint32_t& serial, juce::Image& into)
{
    if (serial == screenSerial.load())
        return false;
    const juce::SpinLock::ScopedLockType l(screenLock);
    serial = screenSerial.load();
    juce::Image::BitmapData bd(into, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < (int)DNC_H; ++y)
        for (int x = 0; x < (int)DNC_W; ++x)
            bd.setPixelColour(x, y, juce::Colour(screenPx[(size_t)y * DNC_W + (size_t)x]));
    return true;
}

void DreadnoughtProcessor::renderChunk()
{
    dnc_render(chunk.data(), (uint32_t)chunkFrames);
    for (int i = 0; i < chunkFrames; ++i) {
        srcL.push_back(chunk[(size_t)(2 * i)]);
        srcR.push_back(chunk[(size_t)(2 * i + 1)]);
    }
}

void DreadnoughtProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    lastProcessMs = juce::Time::getMillisecondCounterHiRes();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (!owner) {
        midi.clear();
        return;
    }
    const juce::SpinLock::ScopedTryLockType tl(coreLock);
    if (!tl.isLocked()) {                           // (the state being saved or loaded: a quiet block)
        midi.clear();
        return;
    }
    if (syncToHost.load()) {
        if (auto* ph = getPlayHead()) {
            if (const auto p = ph->getPosition())
                dnc_transport(p->getIsPlaying() ? 1 : 0, p->getBpm().orFallback(0.0));
        }
    }
    applyEvents();

    juce::MidiBuffer in;
    in.swapWith(midi);
    auto it = in.cbegin();
    auto deliver = [&](int upTo) {
        for (; it != in.cend() && (*it).samplePosition <= upTo; ++it) {
            const auto m = (*it).getMessage();
            const auto* d = m.getRawData();
            const int sz = m.getRawDataSize();
            if (sz >= 1 && sz <= 3 && d[0] >= 0x80 && d[0] < 0xF0)
                dnc_midi(d[0], sz > 1 ? d[1] : 0, sz > 2 ? d[2] : 0);
        }
    };

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < n; ++i) {
        while (pos + 2.0 >= (double)srcL.size()) {  // (the cubic needs the frame after next)
            deliver(i);
            renderChunk();
        }
        const int i0 = (int)pos;
        const float t = (float)(pos - (double)i0);
        const float l = hermite(srcL.data(), i0, t), r = hermite(srcR.data(), i0, t);
        if (R != nullptr) {
            L[i] = l;
            R[i] = r;
        } else {
            L[i] = 0.5f * (l + r);
        }
        pos += step;
    }
    deliver(n);
    const int drop = (int)pos - 1;                  // (keep one frame before the next one read)
    if (drop > 0) {
        srcL.erase(srcL.begin(), srcL.begin() + drop);
        srcR.erase(srcR.begin(), srcR.begin() + drop);
        pos -= (double)drop;
    }

    uint8_t m[3];
    while (dnc_midi_out(m))                         // (MIDI tracks: to the host)
        midi.addEvent(juce::MidiMessage(m[0], m[1], m[2]), 0);
    publishScreen();
}

void DreadnoughtProcessor::idleTick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (!owner || now - lastProcessMs.load() < 250.0) {
        idleLastMs = now;
        return;
    }
    const int ms = (int)juce::jlimit(0.0, 100.0, now - idleLastMs);
    idleLastMs = now;
    const juce::SpinLock::ScopedLockType l(coreLock);
    applyEvents();
    if (ms > 0)
        dnc_idle((uint32_t)ms);
    publishScreen();
}

void DreadnoughtProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (!owner)
        return;
    std::vector<uint8_t> core(dnc_state_max());
    uint32_t n;
    {
        const juce::SpinLock::ScopedLockType l(coreLock);
        n = dnc_state_save(core.data(), (uint32_t)core.size());
        publishScreen();
    }
    juce::MemoryOutputStream out(destData, false);
    out.writeInt((int)stateMagic);
    out.writeInt(1);                                // (this layout)
    out.writeByte(syncToHost.load() ? 1 : 0);
    out.writeInt((int)n);
    out.write(core.data(), n);
}

void DreadnoughtProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (!owner || sizeInBytes < 13)
        return;
    juce::MemoryInputStream in(data, (size_t)sizeInBytes, false);
    if ((uint32_t)in.readInt() != stateMagic || in.readInt() != 1)
        return;
    const bool sync = in.readByte() != 0;
    const int n = in.readInt();
    if (n <= 0 || n > sizeInBytes - 13)
        return;
    std::vector<uint8_t> core((size_t)n);
    in.read(core.data(), n);
    syncToHost = sync;
    const juce::SpinLock::ScopedLockType l(coreLock);
    if (dnc_state_load(core.data(), (uint32_t)n) == 0) {
        dnc_init();
        publishScreen();
    }
}

juce::AudioProcessorEditor* DreadnoughtProcessor::createEditor() { return new DreadnoughtEditor(*this); }

#ifndef DN_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreadnoughtProcessor(); }
#endif
