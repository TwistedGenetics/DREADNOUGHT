// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the processor. Copyright (C) 2026 Jamie (TwistedGenetics).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dn_core.h"
#ifndef DN_NO_EDITOR_PORT
#include <juce_audio_devices/juce_audio_devices.h>
#endif
#include <cstring>

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

// ------------------------------------------------------------------------------------------------ FrameFifo ---
void FrameFifo::put(const uint8_t* d, int n)
{
    const auto w = fifo.write(n);
    if (w.blockSize1 > 0)
        std::memcpy(buf.data() + w.startIndex1, d, (size_t)w.blockSize1);
    if (w.blockSize2 > 0)
        std::memcpy(buf.data() + w.startIndex2, d + w.blockSize1, (size_t)w.blockSize2);
}
void FrameFifo::get(uint8_t* d, int n)
{
    const auto r = fifo.read(n);
    if (r.blockSize1 > 0)
        std::memcpy(d, buf.data() + r.startIndex1, (size_t)r.blockSize1);
    if (r.blockSize2 > 0)
        std::memcpy(d + r.blockSize1, buf.data() + r.startIndex2, (size_t)r.blockSize2);
}
bool FrameFifo::push(const uint8_t* d, int n)
{
    if (n <= 0 || n > 65535 || fifo.getFreeSpace() < n + 2)
        return false;
    const uint8_t h[2] = { (uint8_t)n, (uint8_t)(n >> 8) };
    put(h, 2);
    put(d, n);
    return true;
}
int FrameFifo::pop(uint8_t* out, int max)
{
    if (fifo.getNumReady() < 2)
        return 0;
    uint8_t h[2];
    get(h, 2);
    const int n = h[0] | h[1] << 8;
    if (n > max) {                                  // (too long: skipped)
        std::vector<uint8_t> skip((size_t)n);
        get(skip.data(), n);
        return 0;
    }
    get(out, n);
    return n;
}

// ------------------------------------------------------------------------------------------- the editor's port ---
// (0.45) a MIDI port the web editor finds, as it finds the FM-1's: its frames go to the core (on the audio thread,
// between blocks, or here when the host is quiet) and the answers come back out through this thread
#if !defined(DN_NO_EDITOR_PORT) && !JUCE_WINDOWS     // (Windows has no virtual MIDI ports)
class EditorLink : public juce::MidiInputCallback, private juce::Thread
{
public:
    explicit EditorLink(DreadnoughtProcessor& p) : juce::Thread("DREADNOUGHT editor port"), proc(p)
    {
        out = juce::MidiOutput::createNewDevice(name);
        if (out != nullptr)
            in = juce::MidiInput::createNewDevice(name, this);
        if (in != nullptr) {
            in->start();
            startThread();
        } else {
            out.reset();
        }
    }
    ~EditorLink() override
    {
        if (in != nullptr)
            in->stop();
        stopThread(1000);
        in.reset();
        out.reset();
    }
    bool ok() const { return in != nullptr && out != nullptr; }
    void wake() { notify(); }
    const juce::String name { "DREADNOUGHT Plugin" };

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isSysEx())
            proc.editorFrame(m.getRawData(), m.getRawDataSize());
    }
    void run() override
    {
        std::vector<uint8_t> f(70000);
        while (!threadShouldExit()) {
            wait(20);
            int n;
            while ((n = proc.editorReply(f.data(), (int)f.size())) > 0)
                if (out != nullptr)
                    out->sendMessageNow(juce::MidiMessage(f.data(), n));
        }
    }
    DreadnoughtProcessor& proc;
    std::unique_ptr<juce::MidiInput> in;
    std::unique_ptr<juce::MidiOutput> out;
};
#else
class EditorLink                                    // (dn_shot, Windows: no port; dn_shot hands its frames in directly)
{
public:
    bool ok() const { return false; }
    void wake() {}
    const juce::String name { "DREADNOUGHT Plugin" };
};
#endif

DreadnoughtProcessor::DreadnoughtProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    owner = !coreTaken.exchange(true);
    srcL.reserve(1 << 16);
    srcR.reserve(1 << 16);
    srcL.assign(1, 0.0f);                          // (one frame of history before the first)
    srcR.assign(1, 0.0f);
    if (owner) {
        const juce::SpinLock::ScopedLockType l(coreLock);
        dnc_set_draw(0);                           // (the plugin's own GUI: the FM-1's screen isn't drawn)
        dnc_init();
    }
    // the DAW's parameters: each instrument's main values (whichever track it is on), 0..1 over the value's range.
    // The same list in every instance (the names are the core's own, fixed), so a project's automation always finds
    // its parameters.
    juce::StringArray used;
    for (int e = 1; e < dnc_engine_count(); ++e) {
        uint16_t ids[8];
        const uint32_t n = dnc_auto_ids(e, ids);
        for (uint32_t k = 0; k < n; ++k) {
            auto a = std::make_unique<Auto>();
            a->engine = e;
            a->id = ids[k];
            int32_t cur = 0;
            juce::String pname;
            {
                const juce::SpinLock::ScopedLockType l(coreLock);
                a->lo = dnc_auto_min(e, ids[k]);
                a->hi = dnc_auto_max(e, ids[k]);
                cur = dnc_auto_get(e, ids[k]);
                pname = dnc_name(ids[k], 0, 0);
            }
            if (a->hi <= a->lo)
                a->hi = a->lo + 1;
            a->applied = cur;
            const juce::String ename(dnc_engine_name(e));
            juce::String pid = (ename + "_" + pname).toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789_ ");
            pid = pid.replaceCharacter(' ', '_');
            while (used.contains(pid))
                pid << "_2";
            used.add(pid);
            Auto* ap = a.get();
            auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([this, ap](float v, int) {
                const int32_t iv = ap->lo + (int32_t)std::lround(v * (float)(ap->hi - ap->lo));
                if (!owner)
                    return juce::String(iv);
                char t[16];
                withCore([&] { dnc_text(ap->id, 0, 0, iv, t); return 0; });
                return juce::String(t);
            });
            a->p = new juce::AudioParameterFloat(juce::ParameterID(pid, 1), ename + " " + pname,
                                                 juce::NormalisableRange<float>(0.0f, 1.0f),
                                                 (float)(cur - a->lo) / (float)(a->hi - a->lo), attrs);
            addParameter(a->p);
            autos.push_back(std::move(a));
        }
    }
#if !defined(DN_NO_EDITOR_PORT) && !JUCE_WINDOWS
    if (owner)
        link = std::make_unique<EditorLink>(*this);
#endif
    if (owner)
        startTimerHz(10);
}

DreadnoughtProcessor::~DreadnoughtProcessor()
{
    stopTimer();
    link.reset();
    if (owner)
        coreTaken = false;
}

DreadnoughtProcessor::LinkState DreadnoughtProcessor::linkState() const
{
    if (link == nullptr || !link->ok())
        return linkNone;
    return juce::Time::getMillisecondCounterHiRes() - lastFrameMs.load() < 3000.0 ? linkTalking : linkReady;
}
juce::String DreadnoughtProcessor::linkPortName() const { return link != nullptr ? link->name : juce::String(); }

void DreadnoughtProcessor::editorFrame(const uint8_t* d, int n)
{
    if (!owner || !edIn.push(d, n))
        return;
    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    if (juce::Time::getMillisecondCounterHiRes() - lastProcessMs.load() > 250.0) {   // (the host quiet: answered here)
        const juce::SpinLock::ScopedLockType l(coreLock);
        serveEditor();
    }
}

int DreadnoughtProcessor::editorReply(uint8_t* out, int max) { return edOut.pop(out, max); }

void DreadnoughtProcessor::serveEditor()
{
    if (edIn.empty())
        return;
    static uint8_t f[70000], r[2048];
    int n, k = 0;
    while (k++ < 64 && (n = edIn.pop(f, (int)sizeof f)) > 0) {
        dnc_sysex(f, (uint32_t)n);
        uint32_t m;
        while ((m = dnc_sysex_out(r, sizeof r)) > 0)
            edOut.push(r, (int)m);
    }
    if (link != nullptr)
        link->wake();
}

bool DreadnoughtProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DreadnoughtProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType l(coreLock);
    hostRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    step = (double)DNC_RATE / hostRate;
    const size_t need = (size_t)((double)juce::jmax(samplesPerBlock, 512) * step) * 2u + 4096u;
    if (srcL.capacity() < need) {
        srcL.reserve(need);
        srcR.reserve(need);
    }
    srcL.assign(1, 0.0f);
    srcR.assign(1, 0.0f);
    pos = 1.0;
}

void DreadnoughtProcessor::preview(std::vector<float> mono, double rate)
{
    const juce::SpinLock::ScopedLockType l(previewLock);
    pv = std::move(mono);
    pvPos = 0.0;
    pvStep = rate / hostRate;
}

void DreadnoughtProcessor::renderChunk()
{
    dnc_render(chunk.data(), (uint32_t)chunkFrames);
    for (int i = 0; i < chunkFrames; ++i) {
        srcL.push_back(chunk[(size_t)(2 * i)]);
        srcR.push_back(chunk[(size_t)(2 * i + 1)]);
    }
}

void DreadnoughtProcessor::applyAutomation()
{
    for (auto& a : autos) {
        const int32_t want = a->lo + (int32_t)std::lround(a->p->get() * (float)(a->hi - a->lo));
        if (want != a->applied.load()) {
            dnc_auto_set(a->engine, a->id, want);
            a->applied = want;
        }
    }
}

void DreadnoughtProcessor::timerCallback()
{
    std::vector<std::pair<juce::AudioParameterFloat*, float>> moves;
    withCore([&] {
        for (auto& a : autos) {
            const int32_t cur = dnc_auto_get(a->engine, a->id);
            if (cur != a->applied.load()) {              // (changed on the GUI, by a preset, a loaded project ...)
                a->applied = cur;
                moves.push_back({ a->p, (float)(cur - a->lo) / (float)(a->hi - a->lo) });
            }
        }
        return 0;
    });
    for (auto& m : moves)
        m.first->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, m.second));
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
    bool locked = false;                            // (the GUI holds the core for microseconds: wait that long)
    for (int k = 0; k < 4000 && !(locked = coreLock.tryEnter()); ++k)
        ;
    if (!locked) {                                  // (a state being saved or loaded: a quiet block)
        midi.clear();
        return;
    }
    struct Exit
    {
        juce::SpinLock& l;
        ~Exit() { l.exit(); }
    } exitAtEnd { coreLock };                       // (the lock taken above, let go however this returns)
    if (syncToHost.load()) {
        if (auto* ph = getPlayHead()) {
            if (const auto p = ph->getPosition())
                dnc_transport(p->getIsPlaying() ? 1 : 0, p->getBpm().orFallback(0.0));
        }
    }
    applyAutomation();
    serveEditor();

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
    {
        const juce::SpinLock::ScopedTryLockType pl(previewLock);   // (a sample heard from the samples page)
        if (pl.isLocked() && !pv.empty()) {
            for (int i = 0; i < n && pvPos + 1.0 < (double)pv.size(); ++i) {
                const int k = (int)pvPos;
                const float f = (float)(pvPos - k), v = 0.8f * (pv[(size_t)k] + f * (pv[(size_t)k + 1] - pv[(size_t)k]));
                L[i] += v;
                if (R != nullptr)
                    R[i] += v;
                pvPos += pvStep;
            }
            if (pvPos + 1.0 >= (double)pv.size())
                pv.clear();
        }
    }

    uint8_t m[3];
    while (dnc_midi_out(m))                         // (MIDI tracks: to the host)
        midi.addEvent(juce::MidiMessage(m[0], m[1], m[2]), 0);
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
    applyAutomation();
    serveEditor();
    if (ms > 0)
        dnc_idle((uint32_t)ms);
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
        ++rev;
    }
}

juce::AudioProcessorEditor* DreadnoughtProcessor::createEditor() { return new DreadnoughtEditor(*this); }

#ifndef DN_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreadnoughtProcessor(); }
#endif
