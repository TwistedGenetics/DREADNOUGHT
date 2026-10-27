// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the SOUND page (the track's instrument: every value, in sections; BREAKS' loop and its slices;
// SAMPLER's pads or zones) and the EFFECTS page. Copyright (C) 2026 Jamie (TwistedGenetics).
#include "Pages.h"
#include <map>

namespace dn
{
// ---------------------------------------------------------------------------------- BREAKS: the loop, its slices ---
class SlicesView : public juce::Component
{
public:
    SlicesView(Ui& u, int brk) : ui(u), b(brk)
    {
        autoBtn.onClick = [this] {
            ui.core([&] { return dnc_brk_slices_set(b, nullptr, 0); });
            poll();
        };
        addAndMakeVisible(autoBtn);
        poll();
    }
    void poll()
    {
        const uint8_t* a = nullptr;
        uint32_t n = 0, r = 0, st[64], ns = 0;
        int man = 0, ok = 0;
        ui.core([&] {
            ok = dnc_brk_info(b, &a, &n, &r, st, &ns, &man);
            return 0;
        });
        if (!ok) {
            if (has) {
                has = false;
                repaint();
            }
            return;
        }
        std::vector<uint32_t> s(st, st + ns);
        if (!has || a != adpcm || n != len || s != starts || man != manual) {
            has = true;
            manual = man;
            starts = s;
            rate = r;
            if (a != adpcm || n != len) {            // (a new loop: its shape again)
                adpcm = a, len = n;
                pcm.resize(n);
                dnc_ima_decode(a, n, pcm.data());
                peaks.clear();
            }
            repaint();
        }
    }
    void resized() override
    {
        peaks.clear();
        autoBtn.setBounds(getWidth() - 132, 6, 120, 32);
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(r, 10.0f, 1.5f);
        g.setColour(ink);
        g.setFont(font(20.0f, 700));
        juce::String t = b ? "BREAKS B" : "BREAKS A";
        if (has)
            t << "   " << (int)starts.size() << " SLICES   " << (manual ? "SET BY HAND" : "AUTO")
              << "   " << juce::String((double)len / juce::jmax(1u, rate), 2) << " s";
        else
            t << "   NO LOOP: ADD SAMPLES ON THE SAMPLES PAGE";
        g.drawText(t, 16, 6, getWidth() - 160, 32, juce::Justification::centredLeft);
        g.setColour(soft);
        g.setFont(font(16.0f, 500));
        g.drawText("CLICK: HEAR A SLICE   DRAG A LINE: MOVE IT   DOUBLE-CLICK: A NEW SLICE   RIGHT-CLICK A LINE: REMOVE",
                   16, getHeight() - 30, getWidth() - 32, 24, juce::Justification::centredLeft);
        if (!has || len == 0)
            return;
        const auto w = wave();
        const int W = (int)w.getWidth();
        if ((int)peaks.size() != W) {
            peaks.assign((size_t)W, { 0.0f, 0.0f });
            for (int x = 0; x < W; ++x) {
                const size_t i0 = (size_t)((uint64_t)x * len / (uint64_t)W), i1 = juce::jmax(i0 + 1, (size_t)((uint64_t)(x + 1) * len / (uint64_t)W));
                float lo = 0.0f, hi = 0.0f;
                for (size_t i = i0; i < i1 && i < pcm.size(); ++i) {
                    lo = juce::jmin(lo, pcm[i] / 32768.0f);
                    hi = juce::jmax(hi, pcm[i] / 32768.0f);
                }
                peaks[(size_t)x] = { lo, hi };
            }
        }
        g.setColour(well);
        g.fillRoundedRectangle(w, 6.0f);
        for (size_t s = 0; s < starts.size(); ++s) {     // (every other slice shaded)
            if (s % 2)
                continue;
            const float x0 = xOf(starts[s]), x1 = s + 1 < starts.size() ? xOf(starts[s + 1]) : w.getRight();
            g.setColour(tint(ui.accent, 0.18f));
            g.fillRect(x0, w.getY(), x1 - x0, w.getHeight());
        }
        g.setColour(deep(ui.accent));
        const float cy = w.getCentreY(), hh = w.getHeight() * 0.48f;
        for (int x = 0; x < W; ++x)
            g.drawVerticalLine((int)w.getX() + x, cy - peaks[(size_t)x].second * hh, cy - peaks[(size_t)x].first * hh + 1.0f);
        g.setFont(font(15.0f, 700));
        for (size_t s = 0; s < starts.size(); ++s) {
            const float x = xOf(starts[s]);
            g.setColour(dragIdx == (int)s ? hot : ink);
            g.drawLine(x, w.getY(), x, w.getBottom(), dragIdx == (int)s ? 3.0f : 1.5f);
            g.setColour(ink);
            g.drawText(juce::String((int)s + 1), (int)x + 3, (int)w.getY() + 2, 30, 18, juce::Justification::centredLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!has)
            return;
        const int near = nearLine(e.position.x);
        if (e.mods.isPopupMenu()) {
            if (near > 0) {                          // (the first slice stays at the start)
                auto s = starts;
                s.erase(s.begin() + near);
                store(s);
            }
            return;
        }
        if (near > 0) {
            dragIdx = near;
            repaint();
            return;
        }
        heard = sliceAt(e.position.x);               // (a slice heard)
        ui.core([&] { dnc_audition(ui.track, heard, 110, 1); return 0; });
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragIdx <= 0)
            return;
        const uint32_t lo = starts[(size_t)dragIdx - 1] + 64, hi = (size_t)dragIdx + 1 < starts.size() ? starts[(size_t)dragIdx + 1] - 64 : len - 64;
        starts[(size_t)dragIdx] = juce::jlimit(lo, juce::jmax(lo, hi), posOf(e.position.x));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragIdx > 0) {
            store(starts);
            dragIdx = -1;
        }
        if (heard >= 0) {
            const int h = heard;
            ui.core([&] { dnc_audition(ui.track, h, 0, 0); return 0; });
            heard = -1;
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!has || starts.size() >= 64)
            return;
        const uint32_t p = posOf(e.position.x);
        auto s = starts;
        s.push_back(p);
        std::sort(s.begin(), s.end());
        s.erase(std::unique(s.begin(), s.end()), s.end());
        store(s);
    }

private:
    juce::Rectangle<float> wave() const { return getLocalBounds().toFloat().reduced(16.0f, 0.0f).withTrimmedTop(44.0f).withTrimmedBottom(36.0f); }
    float xOf(uint32_t p) const { const auto w = wave(); return w.getX() + w.getWidth() * (float)p / (float)juce::jmax(1u, len); }
    uint32_t posOf(float x) const { const auto w = wave(); return (uint32_t)juce::jlimit(0.0f, (float)len - 1.0f, (x - w.getX()) / w.getWidth() * (float)len); }
    int nearLine(float x) const
    {
        for (size_t s = 0; s < starts.size(); ++s)
            if (std::abs(xOf(starts[s]) - x) < 6.0f)
                return (int)s;
        return -1;
    }
    int sliceAt(float x) const
    {
        const uint32_t p = posOf(x);
        int k = 0;
        for (size_t s = 0; s < starts.size(); ++s)
            if (starts[s] <= p)
                k = (int)s;
        return k;
    }
    void store(const std::vector<uint32_t>& s)
    {
        ui.core([&] { return dnc_brk_slices_set(b, s.data(), (uint32_t)s.size()); });
        poll();
    }
    Ui& ui;
    int b;
    bool has = false;
    int manual = 0, dragIdx = -1, heard = -1;
    const uint8_t* adpcm = nullptr;
    uint32_t len = 0, rate = 0;
    std::vector<uint32_t> starts;
    std::vector<int16_t> pcm;
    std::vector<std::pair<float, float>> peaks;
    juce::TextButton autoBtn { "AUTO SLICES" };
};

// ------------------------------------------------------------------------------ SAMPLER: its pads, its zones ---
class PadsView : public juce::Component
{
public:
    PadsView(Ui& u, std::function<void()> picked) : ui(u), onPick(std::move(picked)) { poll(); }
    void poll()
    {
        int m = -1;
        std::vector<juce::String> t;
        ui.core([&] {
            m = dnc_smp_mode(ui.track);
            const int n = m == 1 ? 8 : 16, sel = m == 1 ? DNC_SEL_ZONE : DNC_SEL_PAD;
            const uint32_t id = dnc_id_special(m == 1 ? 1 : 0);
            for (int i = 0; i < n; ++i) {
                char a[16];
                dnc_text(id, sel, i, dnc_get(id, sel, i), a);
                juce::String s(a);
                if (m == 1) {
                    char lo[16], hi[16];
                    dnc_text(dnc_id_special(3), sel, i, dnc_get(dnc_id_special(3), sel, i), lo);
                    dnc_text(dnc_id_special(4), sel, i, dnc_get(dnc_id_special(4), sel, i), hi);
                    s << "\n" << lo << " - " << hi;
                }
                t.push_back(s);
            }
            return 0;
        });
        if (m != mode || t != texts) {
            mode = m, texts = t;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override
    {
        const int n = mode == 1 ? 8 : 16, cols = 8, rws = (n + cols - 1) / cols, sel = mode == 1 ? DNC_SEL_ZONE : DNC_SEL_PAD;
        const float cw = (float)getWidth() / cols, ch = (float)getHeight() / rws;
        for (int i = 0; i < n; ++i) {
            auto r = juce::Rectangle<float>((i % cols) * cw, (i / cols) * ch, cw, ch).reduced(4.0f);
            const bool on = ui.selIdx[(size_t)sel] == i;
            const bool lit = on || down == i;        // (the pad picked: its colour, dark text; BLOCKS: the rest pale)
            g.setColour(lit ? chip(ui.accent) : skin == skinBlocks ? chip(ui.accent, true) : panel);
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(on ? ink : edge);
            g.drawRoundedRectangle(r, 8.0f, on ? 3.0f : 1.5f);
            g.setColour(lit || skin == skinBlocks ? onChip : ink);
            g.setFont(font(19.0f, 700));
            g.drawText((mode == 1 ? "ZONE " : "PAD ") + juce::String(i + 1), r.reduced(8.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);
            g.setFont(font(17.0f, 600));
            g.drawFittedText(i < (int)texts.size() ? texts[(size_t)i] : "", r.reduced(8.0f, 4.0f).withTrimmedTop(22.0f).toNearestInt(),
                             juce::Justification::topLeft, 2, 0.8f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int n = mode == 1 ? 8 : 16, cols = 8, rws = (n + cols - 1) / cols;
        const int i = (int)(e.position.x / ((float)getWidth() / cols)) + cols * (int)(e.position.y / ((float)getHeight() / rws));
        if (i < 0 || i >= n)
            return;
        ui.selIdx[(size_t)(mode == 1 ? DNC_SEL_ZONE : DNC_SEL_PAD)] = i;
        down = i;
        note = mode == 1 ? -1 : i;
        ui.core([&] {
            if (mode == 1)                           // (a zone: its root note)
                note = dnc_get(dnc_id_special(2), DNC_SEL_ZONE, i);
            dnc_audition(ui.track, note, 110, 1);
            return 0;
        });
        repaint();
        if (onPick)
            onPick();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (note >= 0) {
            const int nn = note;
            ui.core([&] { dnc_audition(ui.track, nn, 0, 0); return 0; });
        }
        note = -1;
        down = -1;
        repaint();
    }
private:
    Ui& ui;
    std::function<void()> onPick;
    int mode = -2, down = -1, note = -1;
    std::vector<juce::String> texts;
};

// ------------------------------------------------------------------------------------------------- SOUND ---
SoundPage::SoundPage(Ui& u) : Page(u) { addAndMakeVisible(flow); }
SoundPage::~SoundPage() { flow.clear(); }

void SoundPage::rebuild()
{
    std::vector<dnc_section_t> secs(48);
    uint32_t n = 0;
    int e = 0, mode = -1, brk = -1;
    ui.core([&] {
        dnc_track_select(ui.track);
        n = dnc_sections(ui.track, secs.data(), 48);
        e = dnc_track_engine(ui.track);
        mode = dnc_smp_mode(ui.track);
        brk = dnc_brk_of_track(ui.track);
        return 0;
    });
    flow.clear();
    slices.reset();
    pads.reset();
    auto refreshAll = [this] {
        flow.refresh();
        if (pads)
            pads->repaint();
        repaint();
        for (auto* c : flow.getViewedComponent()->getChildren())
            c->repaint();
    };
    if (brk >= 0) {
        slices = std::make_unique<SlicesView>(ui, brk);
        flow.addHead(slices.get(), 250);
    }
    if (mode >= 0) {
        pads = std::make_unique<PadsView>(ui, refreshAll);
        flow.addHead(pads.get(), mode == 1 ? 84 : 160);
    }
    std::map<int, std::vector<uint16_t>> tables;      // (operators, LFOs, matrix slots: each a table of all of them)
    std::map<int, juce::String> tableName;
    for (uint32_t i = 0; i < n; ++i) {
        const int sl = secs[i].sel;
        if (sl == DNC_SEL_OP || sl == DNC_SEL_LFO || sl == DNC_SEL_MSLOT) {
            for (int k = 0; k < secs[i].n; ++k)
                tables[sl].push_back(secs[i].id[k]);
            if (!tableName.count(sl))
                tableName[sl] = sl == DNC_SEL_OP ? "OPERATORS" : sl == DNC_SEL_LFO ? "LFOS" : "MOD MATRIX";
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (tables.count(secs[i].sel))
            continue;
        const bool padSel = mode >= 0 && (secs[i].sel == DNC_SEL_PAD || secs[i].sel == DNC_SEL_ZONE);   // (the pads pick)
        auto p = std::make_unique<SectionPanel>(ui, secs[i], refreshAll, !padSel);
        for (auto* c : p->getChildren())
            if (auto* v = dynamic_cast<ValueControl*>(c))
                v->onChange = [this, v] {
                    if (v->id() == dnc_id_special(5) && ui.onStructure)   // (SAMPLER's MODE: other values)
                        juce::MessageManager::callAsync([this] { if (ui.onStructure) ui.onStructure(); });
                };
        flow.add(std::move(p));
    }
    for (auto& t : tables)
        flow.addTail(std::make_unique<MatrixPanel>(ui, tableName[t.first], t.first, t.second), t.first == DNC_SEL_OP);
    builtFor = e;
    builtMode = mode;
    empty = n == 0;
    flow.setVisible(!empty);
    repaint();
    flow.layout();
    flow.refresh();
}

void SoundPage::poll()
{
    int e = 0, mode = -1;
    ui.core([&] {
        e = dnc_track_engine(ui.track);
        mode = dnc_smp_mode(ui.track);
        return 0;
    });
    if (e != builtFor || mode != builtMode) {
        rebuild();
        return;
    }
    flow.refresh();
    if (slices)
        slices->poll();
    if (pads)
        pads->poll();
}

void SoundPage::resized() { flow.setBounds(getLocalBounds()); }

void SoundPage::paint(juce::Graphics& g)
{
    if (!empty)
        return;
    g.setColour(ink);
    g.setFont(font(26.0f, 700));
    g.drawFittedText("NOTHING ON THIS TRACK\n\nCHANGE (BESIDE ITS NAME, ON THE LEFT) PICKS ITS INSTRUMENT",
                     getLocalBounds().reduced(40), juce::Justification::centred, 4);
}

// ----------------------------------------------------------------------------------------------- EFFECTS ---
// every track's sends, in a row: the track (its instrument's colour), SEND A, SEND B
class SendsPanel : public juce::Component
{
public:
    explicit SendsPanel(Ui& u) : ui(u)
    {
        for (int t = 0; t < 8; ++t)
            for (int k = 0; k < 2; ++k) {
                auto c = std::make_unique<ValueControl>(ui, dnc_id_send(k), DNC_SEL_TRACK);
                c->setFixedIndex(t);
                c->setTile(k * 3);                   // (BLOCKS: SEND A orange, SEND B blue)
                c->refresh();
                addAndMakeVisible(*c);
                ctl.push_back(std::move(c));
            }
    }
    void refresh()
    {
        for (auto& c : ctl)
            c->refresh();
        std::array<juce::String, 8> nm;
        std::array<uint32_t, 8> col {};
        ui.core([&] {
            for (int t = 0; t < 8; ++t)
                nm[(size_t)t] = dnc_track_name(t), col[(size_t)t] = dnc_track_colour(t);
            return 0;
        });
        if (nm != names || col != cols) {
            names = nm, cols = col;
            repaint();
        }
    }
    void resized() override
    {
        const float cw = (float)(getWidth() - 16) / 8.0f;
        for (int t = 0; t < 8; ++t)
            for (int k = 0; k < 2; ++k)
                ctl[(size_t)(t * 2 + k)]->setBounds((int)(8 + t * cw + k * cw / 2), 78, (int)(cw / 2), SectionPanel::cellH);
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(r, 10.0f, 1.5f);
        g.setColour(ink);
        g.setFont(font(20.0f, 700));
        g.drawText("SENDS: EACH TRACK INTO FX A AND FX B", 16, 6, getWidth() - 32, 30, juce::Justification::centredLeft);
        const float cw = (float)(getWidth() - 16) / 8.0f;
        for (int t = 0; t < 8; ++t) {
            auto h = juce::Rectangle<float>(8 + t * cw, 40, cw, 34).reduced(3.0f, 2.0f);
            const juce::Colour tc(0xFF000000u | cols[(size_t)t]);
            g.setColour(chip(tc));
            g.fillRoundedRectangle(h, 6.0f);
            g.setColour(tc.getPerceivedBrightness() > 0.4f ? onChip : ink);
            g.setFont(font(18.0f, 700));
            g.drawFittedText(juce::String(t + 1) + "  " + names[(size_t)t], h.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
private:
    Ui& ui;
    std::vector<std::unique_ptr<ValueControl>> ctl;
    std::array<juce::String, 8> names;
    std::array<uint32_t, 8> cols {};
};

FxPage::FxPage(Ui& u) : Page(u) { addAndMakeVisible(flow); }

void FxPage::rebuild()
{
    std::vector<dnc_section_t> secs(8);
    uint32_t n = 0;
    ui.core([&] {
        n = dnc_fx_sections(secs.data(), 8);
        fxA = n > 1 ? dnc_get(secs[1].id[0], 0, 0) : -1;
        fxB = n > 2 ? dnc_get(secs[2].id[0], 0, 0) : -1;
        return 0;
    });
    flow.clear();
    sends = std::make_unique<SendsPanel>(ui);
    flow.addHead(sends.get(), 40 + 38 + SectionPanel::cellH + 12);
    for (uint32_t i = 0; i < n; ++i) {
        auto p = std::make_unique<SectionPanel>(ui, secs[i], nullptr);
        flow.add(std::move(p));
    }
    flow.layout();
    flow.refresh();
}

void FxPage::poll()
{
    std::vector<dnc_section_t> secs(8);
    int a = -1, b = -1;
    ui.core([&] {
        const uint32_t n = dnc_fx_sections(secs.data(), 8);
        a = n > 1 ? dnc_get(secs[1].id[0], 0, 0) : -1;
        b = n > 2 ? dnc_get(secs[2].id[0], 0, 0) : -1;
        return 0;
    });
    if (a != fxA || b != fxB) {                      // (another effect: its own values)
        rebuild();
        return;
    }
    flow.refresh();
    if (sends)
        static_cast<SendsPanel*>(sends.get())->refresh();
}

void FxPage::resized() { flow.setBounds(getLocalBounds()); }
}
